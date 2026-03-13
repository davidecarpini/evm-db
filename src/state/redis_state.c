#include "evmdb/state.h"
#include "evmdb/hex.h"
#include "evmdb/log.h"

#include <stdlib.h>
#include <string.h>

/* ---- Key formatting ----------------------------------------------------- */

#define ADDR_HEX_LEN 43 /* "0x" + 40 hex + '\0' */
#define KEY_BUF_LEN  128

static void addr_to_hex(const evmdb_address_t *addr, char *out) {
    evmdb_hex_encode(addr->bytes, 20, out, ADDR_HEX_LEN);
}

static void bytes32_to_hex(const evmdb_bytes32_t *b, char *out) {
    evmdb_hex_encode(b->bytes, 32, out, 67);
}

/* ---- Connection --------------------------------------------------------- */

int evmdb_state_init(evmdb_state_t *state, const char *host, int port,
                     const char *password, int db) {
    struct timeval tv = {.tv_sec = 2, .tv_usec = 0};

    state->ctx = redisConnectWithTimeout(host, port, tv);
    if (!state->ctx || state->ctx->err) {
        LOG_ERROR("redis connect: %s",
                  state->ctx ? state->ctx->errstr : "alloc failed");
        return -1;
    }

    /* Auth if needed */
    if (password && password[0]) {
        redisReply *r = redisCommand(state->ctx, "AUTH %s", password);
        if (!r || r->type == REDIS_REPLY_ERROR) {
            LOG_ERROR("redis auth failed");
            if (r) freeReplyObject(r);
            return -1;
        }
        freeReplyObject(r);
    }

    /* Select DB */
    if (db > 0) {
        redisReply *r = redisCommand(state->ctx, "SELECT %d", db);
        if (r) freeReplyObject(r);
    }

    /* Use same connection for reads (in production, point to replica) */
    state->ctx_read = state->ctx;

    return 0;
}

void evmdb_state_close(evmdb_state_t *state) {
    if (state->ctx) {
        redisFree(state->ctx);
        state->ctx = NULL;
    }
    /* Don't free ctx_read if it's the same pointer */
    state->ctx_read = NULL;
}

/* ---- Account ------------------------------------------------------------ */

int evmdb_state_get_account(evmdb_state_t *state, const evmdb_address_t *addr,
                            evmdb_account_t *out) {
    char addr_hex[ADDR_HEX_LEN];
    addr_to_hex(addr, addr_hex);

    redisReply *r = redisCommand(state->ctx_read,
        "HMGET account:%s nonce balance codehash", addr_hex);

    if (!r || r->type != REDIS_REPLY_ARRAY || r->elements != 3) {
        if (r) freeReplyObject(r);
        /* Return zero account */
        memset(out, 0, sizeof(*out));
        memcpy(&out->address, addr, sizeof(*addr));
        return 0;
    }

    memcpy(&out->address, addr, sizeof(*addr));

    /* nonce */
    if (r->element[0]->type == REDIS_REPLY_STRING) {
        out->nonce = (uint64_t)strtoull(r->element[0]->str, NULL, 10);
    } else {
        out->nonce = 0;
    }

    /* balance */
    if (r->element[1]->type == REDIS_REPLY_STRING) {
        evmdb_hex_decode(r->element[1]->str, out->balance.bytes, 32);
    } else {
        memset(&out->balance, 0, sizeof(out->balance));
    }

    /* codehash */
    if (r->element[2]->type == REDIS_REPLY_STRING) {
        evmdb_hex_decode(r->element[2]->str, out->code_hash.bytes, 32);
    } else {
        memset(&out->code_hash, 0, sizeof(out->code_hash));
    }

    freeReplyObject(r);
    return 0;
}

int evmdb_state_set_account(evmdb_state_t *state, const evmdb_account_t *acct) {
    char addr_hex[ADDR_HEX_LEN];
    addr_to_hex(&acct->address, addr_hex);

    char bal_hex[67], code_hex[67];
    bytes32_to_hex(&acct->balance, bal_hex);
    bytes32_to_hex(&acct->code_hash, code_hex);

    redisReply *r = redisCommand(state->ctx,
        "HSET account:%s nonce %llu balance %s codehash %s",
        addr_hex, (unsigned long long)acct->nonce, bal_hex, code_hex);

    if (r) freeReplyObject(r);
    return 0;
}

int evmdb_state_get_balance(evmdb_state_t *state, const evmdb_address_t *addr,
                            evmdb_bytes32_t *out) {
    char addr_hex[ADDR_HEX_LEN];
    addr_to_hex(addr, addr_hex);

    redisReply *r = redisCommand(state->ctx_read,
        "HGET account:%s balance", addr_hex);

    if (r && r->type == REDIS_REPLY_STRING) {
        evmdb_hex_decode(r->str, out->bytes, 32);
    } else {
        memset(out, 0, sizeof(*out));
    }

    if (r) freeReplyObject(r);
    return 0;
}

int evmdb_state_set_balance(evmdb_state_t *state, const evmdb_address_t *addr,
                            const evmdb_bytes32_t *balance) {
    char addr_hex[ADDR_HEX_LEN];
    addr_to_hex(addr, addr_hex);

    char bal_hex[67];
    bytes32_to_hex(balance, bal_hex);

    redisReply *r = redisCommand(state->ctx,
        "HSET account:%s balance %s", addr_hex, bal_hex);

    if (r) freeReplyObject(r);
    return 0;
}

int evmdb_state_get_nonce(evmdb_state_t *state, const evmdb_address_t *addr,
                          uint64_t *out) {
    char addr_hex[ADDR_HEX_LEN];
    addr_to_hex(addr, addr_hex);

    redisReply *r = redisCommand(state->ctx_read,
        "HGET account:%s nonce", addr_hex);

    if (r && r->type == REDIS_REPLY_STRING) {
        *out = (uint64_t)strtoull(r->str, NULL, 10);
    } else {
        *out = 0;
    }

    if (r) freeReplyObject(r);
    return 0;
}

int evmdb_state_set_nonce(evmdb_state_t *state, const evmdb_address_t *addr,
                          uint64_t nonce) {
    char addr_hex[ADDR_HEX_LEN];
    addr_to_hex(addr, addr_hex);

    redisReply *r = redisCommand(state->ctx,
        "HSET account:%s nonce %llu", addr_hex, (unsigned long long)nonce);

    if (r) freeReplyObject(r);
    return 0;
}

/* ---- Storage ------------------------------------------------------------ */

int evmdb_state_get_storage(evmdb_state_t *state, const evmdb_address_t *addr,
                            const evmdb_bytes32_t *key,
                            evmdb_bytes32_t *out) {
    char addr_hex[ADDR_HEX_LEN];
    addr_to_hex(addr, addr_hex);

    char key_hex[67];
    bytes32_to_hex(key, key_hex);

    redisReply *r = redisCommand(state->ctx_read,
        "HGET storage:%s %s", addr_hex, key_hex);

    if (r && r->type == REDIS_REPLY_STRING) {
        evmdb_hex_decode(r->str, out->bytes, 32);
    } else {
        memset(out, 0, sizeof(*out));
    }

    if (r) freeReplyObject(r);
    return 0;
}

int evmdb_state_set_storage(evmdb_state_t *state, const evmdb_address_t *addr,
                            const evmdb_bytes32_t *key,
                            const evmdb_bytes32_t *value) {
    char addr_hex[ADDR_HEX_LEN];
    addr_to_hex(addr, addr_hex);

    char key_hex[67], val_hex[67];
    bytes32_to_hex(key, key_hex);
    bytes32_to_hex(value, val_hex);

    redisReply *r = redisCommand(state->ctx,
        "HSET storage:%s %s %s", addr_hex, key_hex, val_hex);

    if (r) freeReplyObject(r);
    return 0;
}

/* ---- Code --------------------------------------------------------------- */

int evmdb_state_get_code(evmdb_state_t *state, const evmdb_address_t *addr,
                         evmdb_bytes_t *out) {
    char addr_hex[ADDR_HEX_LEN];
    addr_to_hex(addr, addr_hex);

    redisReply *r = redisCommand(state->ctx_read,
        "GET code:%s", addr_hex);

    if (r && r->type == REDIS_REPLY_STRING) {
        out->len = (size_t)r->len;
        out->data = malloc(out->len);
        if (out->data) {
            memcpy(out->data, r->str, out->len);
        }
    } else {
        out->data = NULL;
        out->len = 0;
    }

    if (r) freeReplyObject(r);
    return 0;
}

int evmdb_state_set_code(evmdb_state_t *state, const evmdb_address_t *addr,
                         const uint8_t *code, size_t code_len) {
    char addr_hex[ADDR_HEX_LEN];
    addr_to_hex(addr, addr_hex);

    redisReply *r = redisCommand(state->ctx,
        "SET code:%s %b", addr_hex, code, code_len);

    if (r) freeReplyObject(r);
    return 0;
}

/* ---- Block number ------------------------------------------------------- */

int evmdb_state_get_block_number(evmdb_state_t *state, uint64_t *out) {
    redisReply *r = redisCommand(state->ctx_read, "GET block:latest");
    if (r && r->type == REDIS_REPLY_STRING) {
        *out = (uint64_t)strtoull(r->str, NULL, 10);
    } else {
        *out = 0;
    }
    if (r) freeReplyObject(r);
    return 0;
}

int evmdb_state_set_block_number(evmdb_state_t *state, uint64_t number) {
    redisReply *r = redisCommand(state->ctx,
        "SET block:latest %llu", (unsigned long long)number);
    if (r) freeReplyObject(r);
    return 0;
}

/* ---- Transaction queue -------------------------------------------------- */

int evmdb_state_push_tx(evmdb_state_t *state, const uint8_t *raw_tx,
                        size_t raw_len) {
    redisReply *r = redisCommand(state->ctx,
        "RPUSH tx:queue %b", raw_tx, raw_len);
    if (r) freeReplyObject(r);
    return 0;
}

int evmdb_state_pop_tx(evmdb_state_t *state, evmdb_bytes_t *out,
                       int timeout_sec) {
    redisReply *r = redisCommand(state->ctx,
        "BLPOP tx:queue %d", timeout_sec);

    if (!r || r->type != REDIS_REPLY_ARRAY || r->elements < 2) {
        if (r) freeReplyObject(r);
        return -1; /* timeout or error */
    }

    /* element[0] = key name, element[1] = value */
    redisReply *val = r->element[1];
    out->len = (size_t)val->len;
    out->data = malloc(out->len);
    if (out->data) {
        memcpy(out->data, val->str, out->len);
    }

    freeReplyObject(r);
    return 0;
}

/* ---- Pub/Sub ------------------------------------------------------------ */

int evmdb_state_publish_block(evmdb_state_t *state, uint64_t block_number,
                              const evmdb_hash_t *block_hash) {
    char hash_hex[67];
    evmdb_hex_encode(block_hash->bytes, 32, hash_hex, sizeof(hash_hex));

    redisReply *r = redisCommand(state->ctx,
        "PUBLISH chain:blocks %llu:%s",
        (unsigned long long)block_number, hash_hex);

    if (r) freeReplyObject(r);
    return 0;
}
