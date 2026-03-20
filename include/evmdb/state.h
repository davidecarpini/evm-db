#ifndef EVMDB_STATE_H
#define EVMDB_STATE_H

#include "evmdb/types.h"
#include <hiredis/hiredis.h>
#include <pthread.h>

/* ---- Redis state interface ---------------------------------------------- */

typedef struct {
    redisContext *ctx;           /* write connection */
    redisContext *ctx_read;      /* read connection (can be replica) */
    pthread_mutex_t lock;        /* thread safety for concurrent RPC */
} evmdb_state_t;

/* Connect to Redis. Returns 0 on success. */
int evmdb_state_init(evmdb_state_t *state, const char *host, int port,
                     const char *password, int db);

void evmdb_state_close(evmdb_state_t *state);

/* ---- Account operations ------------------------------------------------- */

int evmdb_state_get_account(evmdb_state_t *state, const evmdb_address_t *addr,
                            evmdb_account_t *out);

int evmdb_state_set_account(evmdb_state_t *state, const evmdb_account_t *acct);

int evmdb_state_get_balance(evmdb_state_t *state, const evmdb_address_t *addr,
                            evmdb_bytes32_t *out);

int evmdb_state_set_balance(evmdb_state_t *state, const evmdb_address_t *addr,
                            const evmdb_bytes32_t *balance);

int evmdb_state_get_nonce(evmdb_state_t *state, const evmdb_address_t *addr,
                          uint64_t *out);

int evmdb_state_set_nonce(evmdb_state_t *state, const evmdb_address_t *addr,
                          uint64_t nonce);

/* ---- Storage ------------------------------------------------------------ */

int evmdb_state_get_storage(evmdb_state_t *state, const evmdb_address_t *addr,
                            const evmdb_bytes32_t *key, evmdb_bytes32_t *out);

int evmdb_state_set_storage(evmdb_state_t *state, const evmdb_address_t *addr,
                            const evmdb_bytes32_t *key,
                            const evmdb_bytes32_t *value);

/* ---- Code --------------------------------------------------------------- */

int evmdb_state_get_code(evmdb_state_t *state, const evmdb_address_t *addr,
                         evmdb_bytes_t *out);

int evmdb_state_set_code(evmdb_state_t *state, const evmdb_address_t *addr,
                         const uint8_t *code, size_t code_len);

int evmdb_state_get_receipt(evmdb_state_t *state, const evmdb_hash_t *tx_hash,
                            evmdb_bytes_t *out);

int evmdb_state_set_receipt(evmdb_state_t *state, const evmdb_hash_t *tx_hash,
                            const uint8_t *data, size_t data_len);

/* ---- Block number ------------------------------------------------------- */

int evmdb_state_get_block_number(evmdb_state_t *state, uint64_t *out);
int evmdb_state_set_block_number(evmdb_state_t *state, uint64_t number);

#endif /* EVMDB_STATE_H */
