#include "evmdb/config.h"
#include "evmdb/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void evmdb_config_defaults(evmdb_config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));

    cfg->redis_host      = "127.0.0.1";
    cfg->redis_port      = 6379;
    cfg->redis_password  = NULL;
    cfg->redis_db        = 0;

    cfg->rpc_host        = "0.0.0.0";
    cfg->rpc_port        = 8545;

    cfg->chain_id        = 100100;    /* unique chain id */
    cfg->block_gas_limit = 30000000;  /* 30M, same as Ethereum */
    cfg->block_time_ms   = 10;        /* 10ms blocks */
    cfg->base_fee_initial = 1000000;  /* 0.001 gwei — very cheap */

    cfg->sequencer_key_path = NULL;

    cfg->log_level       = 2;         /* info */
}

int evmdb_config_load(evmdb_config_t *cfg, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        return -1;
    }

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        /* Skip comments and empty lines */
        if (line[0] == '#' || line[0] == '\n' || line[0] == '[') {
            continue;
        }

        char key[64], value[256];
        if (sscanf(line, " %63[^= ] = %255[^\n]", key, value) != 2) {
            continue;
        }

        /* Strip quotes from value */
        size_t vlen = strlen(value);
        if (vlen >= 2 && value[0] == '"' && value[vlen - 1] == '"') {
            value[vlen - 1] = '\0';
            memmove(value, value + 1, vlen - 1);
        }

        if (strcmp(key, "redis_host") == 0) {
            cfg->redis_host = strdup(value);
        } else if (strcmp(key, "redis_port") == 0) {
            cfg->redis_port = atoi(value);
        } else if (strcmp(key, "redis_password") == 0) {
            cfg->redis_password = strdup(value);
        } else if (strcmp(key, "redis_db") == 0) {
            cfg->redis_db = atoi(value);
        } else if (strcmp(key, "rpc_host") == 0) {
            cfg->rpc_host = strdup(value);
        } else if (strcmp(key, "rpc_port") == 0) {
            cfg->rpc_port = atoi(value);
        } else if (strcmp(key, "chain_id") == 0) {
            cfg->chain_id = (uint64_t)atoll(value);
        } else if (strcmp(key, "block_gas_limit") == 0) {
            cfg->block_gas_limit = (uint64_t)atoll(value);
        } else if (strcmp(key, "block_time_ms") == 0) {
            cfg->block_time_ms = (uint64_t)atoll(value);
        } else if (strcmp(key, "base_fee_initial") == 0) {
            cfg->base_fee_initial = (uint64_t)atoll(value);
        } else if (strcmp(key, "sequencer_key") == 0) {
            cfg->sequencer_key_path = strdup(value);
        } else if (strcmp(key, "log_level") == 0) {
            cfg->log_level = atoi(value);
        }
    }

    fclose(f);
    return 0;
}
