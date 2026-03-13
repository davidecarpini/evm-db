#ifndef EVMDB_BLOCK_H
#define EVMDB_BLOCK_H

#include "evmdb/types.h"
#include "evmdb/state.h"

/* ---- Block producer ----------------------------------------------------- */

typedef struct evmdb_block_producer evmdb_block_producer_t;

/* Create a block producer.
   coinbase: address that receives fees.
   gas_limit: max gas per block.
   block_time_ms: milliseconds between blocks (0 = per-tx blocks). */
evmdb_block_producer_t *evmdb_block_producer_create(
    const evmdb_address_t *coinbase, uint64_t gas_limit,
    uint64_t block_time_ms);

void evmdb_block_producer_destroy(evmdb_block_producer_t *bp);

/* Add an executed transaction to the pending block. */
int evmdb_block_producer_add_tx(evmdb_block_producer_t *bp,
                                const evmdb_tx_t *tx,
                                const evmdb_receipt_t *receipt);

/* Seal the current block: compute roots, sign, store in Redis.
   Returns the produced block. Caller must free tx_hashes. */
int evmdb_block_producer_seal(evmdb_block_producer_t *bp,
                              evmdb_state_t *state, evmdb_block_t *out);

/* Check if it's time to seal based on block_time_ms or gas usage. */
bool evmdb_block_producer_should_seal(const evmdb_block_producer_t *bp);

/* ---- Transaction decode/encode ------------------------------------------ */

/* Decode RLP-encoded signed transaction. Returns 0 on success. */
int evmdb_tx_decode(const uint8_t *raw, size_t raw_len, evmdb_tx_t *tx);

/* Recover sender address from signature. Returns 0 on success. */
int evmdb_tx_recover_sender(evmdb_tx_t *tx);

/* Free dynamically allocated fields in a tx. */
void evmdb_tx_free(evmdb_tx_t *tx);

/* Free dynamically allocated fields in a receipt. */
void evmdb_receipt_free(evmdb_receipt_t *receipt);

/* Free dynamically allocated fields in a block. */
void evmdb_block_free(evmdb_block_t *block);

#endif /* EVMDB_BLOCK_H */
