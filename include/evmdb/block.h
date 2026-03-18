#ifndef EVMDB_BLOCK_H
#define EVMDB_BLOCK_H

#include "evmdb/types.h"

/* ---- Transaction decode/encode ------------------------------------------ */

/* Decode RLP-encoded signed transaction. Returns 0 on success. */
int evmdb_tx_decode(const uint8_t *raw, size_t raw_len, evmdb_tx_t *tx);

/* Recover sender address from signature. Returns 0 on success. */
int evmdb_tx_recover_sender(evmdb_tx_t *tx);

/* Free dynamically allocated fields in a tx. */
void evmdb_tx_free(evmdb_tx_t *tx);

/* Free dynamically allocated fields in a receipt. */
void evmdb_receipt_free(evmdb_receipt_t *receipt);

#endif /* EVMDB_BLOCK_H */
