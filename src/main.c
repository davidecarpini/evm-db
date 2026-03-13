#include "evmdb/config.h"
#include "evmdb/state.h"
#include "evmdb/evm.h"
#include "evmdb/block.h"
#include "evmdb/rpc.h"
#include "evmdb/log.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t g_running = 1;

static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

static void print_banner(void) {
    printf(
        "\n"
        "  ╔═══════════════════════════════════════╗\n"
        "  ║         evm-db v0.1.0                 ║\n"
        "  ║  Redis-backed EVM execution engine    ║\n"
        "  ╚═══════════════════════════════════════╝\n"
        "\n"
    );
}

/*
 * Main sequencer loop:
 *
 * 1. Pop raw signed tx from Redis queue (blocking)
 * 2. Decode RLP, recover sender
 * 3. Execute against current state via evmone
 * 4. Write state diffs to Redis
 * 5. Add tx + receipt to pending block
 * 6. Seal block when timer/gas threshold hit
 * 7. Publish new block via Redis Pub/Sub
 */
static int run_sequencer(evmdb_config_t *cfg) {
    int rc;

    /* ---- State (Redis) ---- */
    evmdb_state_t state;
    rc = evmdb_state_init(&state, cfg->redis_host, cfg->redis_port,
                          cfg->redis_password, cfg->redis_db);
    if (rc != 0) {
        LOG_ERROR("failed to connect to Redis at %s:%d", cfg->redis_host,
                  cfg->redis_port);
        return 1;
    }
    LOG_INFO("connected to Redis at %s:%d", cfg->redis_host, cfg->redis_port);

    /* ---- EVM ---- */
    evmdb_evm_t *evm = evmdb_evm_create(NULL);
    if (!evm) {
        LOG_ERROR("failed to initialize EVM executor");
        evmdb_state_close(&state);
        return 1;
    }
    LOG_INFO("EVM executor initialized");

    /* ---- Block producer ---- */
    evmdb_address_t coinbase = {0}; /* TODO: load from sequencer key */
    evmdb_block_producer_t *bp = evmdb_block_producer_create(
        &coinbase, cfg->block_gas_limit, cfg->block_time_ms);
    if (!bp) {
        LOG_ERROR("failed to create block producer");
        evmdb_evm_destroy(evm);
        evmdb_state_close(&state);
        return 1;
    }

    /* ---- JSON-RPC server (runs in separate thread) ---- */
    evmdb_rpc_t *rpc = evmdb_rpc_create(&state, evm, cfg->chain_id);
    if (!rpc) {
        LOG_ERROR("failed to create RPC server");
        evmdb_block_producer_destroy(bp);
        evmdb_evm_destroy(evm);
        evmdb_state_close(&state);
        return 1;
    }

    /* TODO: start RPC listener in a thread */
    LOG_INFO("RPC server ready on %s:%d", cfg->rpc_host, cfg->rpc_port);

    /* ---- Sequencer loop ---- */
    LOG_INFO("sequencer running (chain_id=%lu, block_gas_limit=%lu, block_time=%lums)",
             (unsigned long)cfg->chain_id,
             (unsigned long)cfg->block_gas_limit,
             (unsigned long)cfg->block_time_ms);

    while (g_running) {
        /* 1. Pop next transaction (1s timeout to check g_running) */
        evmdb_bytes_t raw_tx = {0};
        rc = evmdb_state_pop_tx(&state, &raw_tx, 1);
        if (rc != 0) {
            /* Timeout or error — check if we should seal a time-based block */
            if (evmdb_block_producer_should_seal(bp)) {
                evmdb_block_t block = {0};
                if (evmdb_block_producer_seal(bp, &state, &block) == 0) {
                    LOG_INFO("block %lu sealed (%lu txs, %lu gas)",
                             (unsigned long)block.number,
                             (unsigned long)block.tx_count,
                             (unsigned long)block.gas_used);
                    evmdb_state_publish_block(&state, block.number,
                                             &block.hash);
                    evmdb_block_free(&block);
                }
            }
            continue;
        }

        /* 2. Decode transaction */
        evmdb_tx_t tx = {0};
        rc = evmdb_tx_decode(raw_tx.data, raw_tx.len, &tx);
        free(raw_tx.data);
        if (rc != 0) {
            LOG_WARN("failed to decode transaction, skipping");
            continue;
        }

        /* 3. Recover sender */
        rc = evmdb_tx_recover_sender(&tx);
        if (rc != 0) {
            LOG_WARN("failed to recover tx sender, skipping");
            evmdb_tx_free(&tx);
            continue;
        }

        /* 4. Execute */
        evmdb_block_t current_block = {0}; /* TODO: fill from state */
        evmdb_exec_result_t result = {0};
        rc = evmdb_evm_execute_tx(evm, &state, &tx, &current_block, &result);
        if (rc != 0) {
            LOG_ERROR("EVM execution error: %s", result.error);
            evmdb_tx_free(&tx);
            continue;
        }

        LOG_DEBUG("tx %s: %s (gas: %lu)",
                  result.success ? "OK" : "REVERT",
                  result.error[0] ? result.error : "success",
                  (unsigned long)result.gas_used);

        /* 5. Build receipt and add to block */
        evmdb_receipt_t receipt = {
            .tx_hash     = tx.hash,
            .from        = tx.from,
            .to          = tx.to,
            .gas_used    = result.gas_used,
            .status      = result.success,
            .logs        = result.logs,
            .log_count   = result.log_count,
        };
        evmdb_block_producer_add_tx(bp, &tx, &receipt);

        /* 6. Seal block if needed */
        if (evmdb_block_producer_should_seal(bp)) {
            evmdb_block_t block = {0};
            if (evmdb_block_producer_seal(bp, &state, &block) == 0) {
                LOG_INFO("block %lu sealed (%lu txs, %lu gas)",
                         (unsigned long)block.number,
                         (unsigned long)block.tx_count,
                         (unsigned long)block.gas_used);
                evmdb_state_publish_block(&state, block.number, &block.hash);
                evmdb_block_free(&block);
            }
        }

        evmdb_tx_free(&tx);
        free(result.output.data);
    }

    /* ---- Cleanup ---- */
    LOG_INFO("shutting down...");
    evmdb_rpc_stop(rpc);
    evmdb_rpc_destroy(rpc);
    evmdb_block_producer_destroy(bp);
    evmdb_evm_destroy(evm);
    evmdb_state_close(&state);

    return 0;
}

int main(int argc, char **argv) {
    print_banner();

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* Load config */
    evmdb_config_t cfg;
    evmdb_config_defaults(&cfg);

    const char *config_path = "config/evmdb.toml";
    if (argc > 1) {
        config_path = argv[1];
    }

    if (evmdb_config_load(&cfg, config_path) != 0) {
        LOG_WARN("config file not found at %s, using defaults", config_path);
    }

    evmdb_log_init((evmdb_log_level_t)cfg.log_level);

    return run_sequencer(&cfg);
}
