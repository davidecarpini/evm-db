#ifndef EVMDB_EVM_H
#define EVMDB_EVM_H

#include "evmdb/types.h"
#include "evmdb/state.h"

/* ---- EVM executor ------------------------------------------------------- */

typedef struct evmdb_evm evmdb_evm_t;

/* Initialize EVM executor. Loads evmone shared library.
   evmone_path: path to libevmone.so/dylib, or NULL to search default paths.
   Returns NULL on failure. */
evmdb_evm_t *evmdb_evm_create(const char *evmone_path);

void evmdb_evm_destroy(evmdb_evm_t *evm);

/* Execute a transaction against the current state.
   Reads and writes state via the provided state handle.
   Fills result struct. Returns 0 on success (even if tx reverts). */
int evmdb_evm_execute_tx(evmdb_evm_t *evm, evmdb_state_t *state,
                         const evmdb_tx_t *tx, const evmdb_block_t *block,
                         evmdb_exec_result_t *result);

/* Execute a read-only call (eth_call). Does not modify state. */
int evmdb_evm_call(evmdb_evm_t *evm, evmdb_state_t *state,
                   const evmdb_address_t *from, const evmdb_address_t *to,
                   const evmdb_bytes_t *data, uint64_t gas,
                   evmdb_exec_result_t *result);

#endif /* EVMDB_EVM_H */
