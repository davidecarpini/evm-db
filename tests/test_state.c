#include "evmdb/state.h"
#include "evmdb/hex.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * State tests require a running Redis instance on localhost:6379.
 * Uses DB 15 to avoid conflicts with production data.
 * Flushes DB 15 before and after tests.
 */

#define TEST_REDIS_DB 15

static evmdb_state_t state;

static void setup(void) {
    int rc = evmdb_state_init(&state, "127.0.0.1", 6379, NULL, TEST_REDIS_DB);
    if (rc != 0) {
        printf("test_state: SKIPPED (Redis not available)\n");
        exit(0);
    }
    /* Flush test DB */
    redisReply *r = redisCommand(state.ctx, "FLUSHDB");
    if (r) freeReplyObject(r);
}

static void teardown(void) {
    redisReply *r = redisCommand(state.ctx, "FLUSHDB");
    if (r) freeReplyObject(r);
    evmdb_state_close(&state);
}

static void test_nonce(void) {
    evmdb_address_t addr = {{0x01, 0x02, 0x03}};
    uint64_t nonce;

    evmdb_state_get_nonce(&state, &addr, &nonce);
    assert(nonce == 0);

    evmdb_state_set_nonce(&state, &addr, 42);
    evmdb_state_get_nonce(&state, &addr, &nonce);
    assert(nonce == 42);
}

static void test_balance(void) {
    evmdb_address_t addr = {{0xAA, 0xBB, 0xCC}};
    evmdb_bytes32_t balance;

    evmdb_state_get_balance(&state, &addr, &balance);
    /* Should be zero */
    int is_zero = 1;
    for (int i = 0; i < 32; i++) {
        if (balance.bytes[i] != 0) { is_zero = 0; break; }
    }
    assert(is_zero);

    /* Set balance to 1 ETH = 10^18 = 0xDE0B6B3A7640000 */
    evmdb_bytes32_t one_eth = {{0}};
    one_eth.bytes[24] = 0x0D;
    one_eth.bytes[25] = 0xE0;
    one_eth.bytes[26] = 0xB6;
    one_eth.bytes[27] = 0xB3;
    one_eth.bytes[28] = 0xA7;
    one_eth.bytes[29] = 0x64;
    one_eth.bytes[30] = 0x00;
    one_eth.bytes[31] = 0x00;

    evmdb_state_set_balance(&state, &addr, &one_eth);
    evmdb_state_get_balance(&state, &addr, &balance);
    assert(memcmp(balance.bytes, one_eth.bytes, 32) == 0);
}

static void test_storage(void) {
    evmdb_address_t addr = {{0xDE, 0xAD}};
    evmdb_bytes32_t key = {{0}};
    key.bytes[31] = 0x01;

    evmdb_bytes32_t value;
    evmdb_state_get_storage(&state, &addr, &key, &value);
    /* Should be zero */

    evmdb_bytes32_t new_val = {{0}};
    new_val.bytes[31] = 0xFF;
    evmdb_state_set_storage(&state, &addr, &key, &new_val);
    evmdb_state_get_storage(&state, &addr, &key, &value);
    assert(value.bytes[31] == 0xFF);
}

static void test_code(void) {
    evmdb_address_t addr = {{0xCA, 0xFE}};
    uint8_t bytecode[] = {0x60, 0x00, 0x60, 0x00, 0xFD}; /* PUSH0 PUSH0 REVERT */

    evmdb_state_set_code(&state, &addr, bytecode, sizeof(bytecode));

    evmdb_bytes_t code;
    evmdb_state_get_code(&state, &addr, &code);
    assert(code.len == sizeof(bytecode));
    assert(memcmp(code.data, bytecode, sizeof(bytecode)) == 0);
    free(code.data);
}

static void test_receipt(void) {
    evmdb_hash_t tx_hash = {{0x12, 0x34, 0x56}};
    const char *json = "{\"status\":\"0x1\"}";
    evmdb_bytes_t receipt;

    evmdb_state_set_receipt(&state, &tx_hash, (const uint8_t *)json,
                            strlen(json));
    evmdb_state_get_receipt(&state, &tx_hash, &receipt);
    assert(receipt.len == strlen(json));
    assert(memcmp(receipt.data, json, receipt.len) == 0);
    free(receipt.data);
}

static void test_block_number(void) {
    uint64_t num;
    evmdb_state_get_block_number(&state, &num);
    assert(num == 0);

    evmdb_state_set_block_number(&state, 12345);
    evmdb_state_get_block_number(&state, &num);
    assert(num == 12345);
}

int main(void) {
    setup();

    test_nonce();
    test_balance();
    test_storage();
    test_code();
    test_receipt();
    test_block_number();

    teardown();

    printf("test_state: all tests passed\n");
    return 0;
}
