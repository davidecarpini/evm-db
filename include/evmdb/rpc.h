#ifndef EVMDB_RPC_H
#define EVMDB_RPC_H

#include "evmdb/types.h"
#include "evmdb/state.h"
#include "evmdb/evm.h"

/* ---- JSON-RPC server ---------------------------------------------------- */

typedef struct evmdb_rpc evmdb_rpc_t;

/* Create JSON-RPC server. Does not start listening yet. */
evmdb_rpc_t *evmdb_rpc_create(evmdb_state_t *state, evmdb_evm_t *evm,
                               uint64_t chain_id, uint64_t gas_limit,
                               uint64_t base_fee);

void evmdb_rpc_destroy(evmdb_rpc_t *rpc);

/* Start listening. Blocks until evmdb_rpc_stop() is called.
   Returns 0 on success. */
int evmdb_rpc_listen(evmdb_rpc_t *rpc, const char *host, int port);

/* Signal the server to stop. Thread-safe. */
void evmdb_rpc_stop(evmdb_rpc_t *rpc);

/* ---- Supported JSON-RPC methods ----------------------------------------- *
 *
 * eth_chainId
 * eth_blockNumber
 * eth_getBalance
 * eth_getTransactionCount
 * eth_getCode
 * eth_getStorageAt
 * eth_call
 * eth_estimateGas
 * eth_sendRawTransaction
 * eth_getBlockByNumber
 * eth_getBlockByHash
 * eth_getTransactionByHash
 * eth_getTransactionReceipt
 * eth_gasPrice
 * eth_feeHistory
 * net_version
 * web3_clientVersion
 *
 * -------------------------------------------------------------------------- */

#endif /* EVMDB_RPC_H */
