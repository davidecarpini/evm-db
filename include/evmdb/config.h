#ifndef EVMDB_CONFIG_H
#define EVMDB_CONFIG_H

#include <stdint.h>

typedef struct {
    /* Redis */
    const char *redis_host;
    int         redis_port;
    const char *redis_password;      /* NULL if no auth */
    int         redis_db;

    /* Network */
    const char *rpc_host;
    int         rpc_port;

    /* Chain */
    uint64_t    chain_id;
    uint64_t    block_gas_limit;
    uint64_t    block_time_ms;       /* 0 = produce block per tx */
    uint64_t    base_fee_initial;    /* initial base fee in wei */

    /* Sequencer */
    const char *sequencer_key_path;  /* path to secp256k1 private key */

    /* Logging */
    int         log_level;           /* 0=error 1=warn 2=info 3=debug */
} evmdb_config_t;

/* Load config from file, returns 0 on success */
int evmdb_config_load(evmdb_config_t *cfg, const char *path);

/* Load config with defaults */
void evmdb_config_defaults(evmdb_config_t *cfg);

#endif /* EVMDB_CONFIG_H */
