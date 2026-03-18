#include "evmdb/config.h"
#include "evmdb/state.h"
#include "evmdb/evm.h"
#include "evmdb/rpc.h"
#include "evmdb/log.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile sig_atomic_t g_running = 1;

static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

static void print_banner(void) {
    printf(
        "\n"
        "  ╔═══════════════════════════════════════╗\n"
        "  ║         evm-db v0.2.0                 ║\n"
        "  ║  Redis-backed EVM state engine        ║\n"
        "  ╚═══════════════════════════════════════╝\n"
        "\n"
    );
}

static int run(evmdb_config_t *cfg) {
    /* ---- State (Redis) ---- */
    evmdb_state_t state;
    int rc = evmdb_state_init(&state, cfg->redis_host, cfg->redis_port,
                              cfg->redis_password, cfg->redis_db);
    if (rc != 0) {
        LOG_ERROR("failed to connect to Redis at %s:%d",
                  cfg->redis_host, cfg->redis_port);
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

    /* ---- JSON-RPC server (blocks on listen) ---- */
    evmdb_rpc_t *rpc = evmdb_rpc_create(&state, evm, cfg->chain_id,
                                         cfg->gas_limit, cfg->base_fee);
    if (!rpc) {
        LOG_ERROR("failed to create RPC server");
        evmdb_evm_destroy(evm);
        evmdb_state_close(&state);
        return 1;
    }

    LOG_INFO("chain_id=%lu, gas_limit=%lu, base_fee=%lu wei",
             (unsigned long)cfg->chain_id,
             (unsigned long)cfg->gas_limit,
             (unsigned long)cfg->base_fee);

    LOG_INFO("RPC listening on %s:%d", cfg->rpc_host, cfg->rpc_port);

    /* Blocks until evmdb_rpc_stop() or SIGINT */
    evmdb_rpc_listen(rpc, cfg->rpc_host, cfg->rpc_port);

    /* ---- Cleanup ---- */
    LOG_INFO("shutting down...");
    evmdb_rpc_destroy(rpc);
    evmdb_evm_destroy(evm);
    evmdb_state_close(&state);

    return 0;
}

int main(int argc, char **argv) {
    print_banner();

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

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

    return run(&cfg);
}
