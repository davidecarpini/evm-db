#include "evmdb/block.h"
#include "evmdb/log.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_TXS_PER_BLOCK 10000

struct evmdb_block_producer {
    evmdb_address_t coinbase;
    uint64_t        gas_limit;
    uint64_t        block_time_ms;

    /* Pending block state */
    evmdb_hash_t   *pending_tx_hashes;
    size_t          pending_tx_count;
    uint64_t        pending_gas_used;
    uint64_t        pending_start_ms;
};

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

evmdb_block_producer_t *evmdb_block_producer_create(
    const evmdb_address_t *coinbase, uint64_t gas_limit,
    uint64_t block_time_ms) {

    evmdb_block_producer_t *bp = calloc(1, sizeof(*bp));
    if (!bp) return NULL;

    memcpy(&bp->coinbase, coinbase, sizeof(*coinbase));
    bp->gas_limit = gas_limit;
    bp->block_time_ms = block_time_ms;

    bp->pending_tx_hashes = calloc(MAX_TXS_PER_BLOCK, sizeof(evmdb_hash_t));
    if (!bp->pending_tx_hashes) {
        free(bp);
        return NULL;
    }

    bp->pending_tx_count = 0;
    bp->pending_gas_used = 0;
    bp->pending_start_ms = now_ms();

    return bp;
}

void evmdb_block_producer_destroy(evmdb_block_producer_t *bp) {
    if (!bp) return;
    free(bp->pending_tx_hashes);
    free(bp);
}

int evmdb_block_producer_add_tx(evmdb_block_producer_t *bp,
                                const evmdb_tx_t *tx,
                                const evmdb_receipt_t *receipt) {
    if (bp->pending_tx_count >= MAX_TXS_PER_BLOCK) {
        LOG_WARN("block full, cannot add more transactions");
        return -1;
    }

    memcpy(&bp->pending_tx_hashes[bp->pending_tx_count],
           &tx->hash, sizeof(evmdb_hash_t));
    bp->pending_tx_count++;
    bp->pending_gas_used += receipt->gas_used;

    return 0;
}

bool evmdb_block_producer_should_seal(const evmdb_block_producer_t *bp) {
    /* Seal if we have transactions and time is up */
    if (bp->pending_tx_count == 0) {
        return false;
    }

    /* Block time trigger */
    if (bp->block_time_ms > 0) {
        uint64_t elapsed = now_ms() - bp->pending_start_ms;
        if (elapsed >= bp->block_time_ms) {
            return true;
        }
    } else {
        /* block_time_ms == 0 means seal after every tx */
        return true;
    }

    /* Gas limit trigger */
    if (bp->pending_gas_used >= bp->gas_limit) {
        return true;
    }

    /* Max tx count trigger */
    if (bp->pending_tx_count >= MAX_TXS_PER_BLOCK) {
        return true;
    }

    return false;
}

int evmdb_block_producer_seal(evmdb_block_producer_t *bp,
                              evmdb_state_t *state, evmdb_block_t *out) {
    /* Get current block number */
    uint64_t current_number = 0;
    evmdb_state_get_block_number(state, &current_number);

    uint64_t new_number = current_number + 1;

    memset(out, 0, sizeof(*out));
    out->number = new_number;
    memcpy(&out->coinbase, &bp->coinbase, sizeof(evmdb_address_t));
    out->timestamp = (uint64_t)time(NULL);
    out->gas_limit = bp->gas_limit;
    out->gas_used = bp->pending_gas_used;
    out->tx_count = bp->pending_tx_count;

    /* Copy tx hashes */
    if (bp->pending_tx_count > 0) {
        out->tx_hashes = calloc(bp->pending_tx_count, sizeof(evmdb_hash_t));
        if (out->tx_hashes) {
            memcpy(out->tx_hashes, bp->pending_tx_hashes,
                   bp->pending_tx_count * sizeof(evmdb_hash_t));
        }
    }

    /*
     * TODO: Compute state_root (Merkle root of all account/storage state).
     * TODO: Compute tx_root (Merkle root of tx hashes).
     * TODO: Compute receipts_root.
     * TODO: Compute block hash = keccak256(RLP(header)).
     * TODO: Sign block with sequencer key.
     *
     * For now, block.hash is left as zeros.
     */

    /* Update block number in Redis */
    evmdb_state_set_block_number(state, new_number);

    /* TODO: Store full block in Redis as block:<number> */

    /* Reset pending state */
    bp->pending_tx_count = 0;
    bp->pending_gas_used = 0;
    bp->pending_start_ms = now_ms();

    return 0;
}
