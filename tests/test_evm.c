#include "evmdb/block.h"
#include "evmdb/evm.h"
#include "evmdb/state.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_REDIS_DB 14

static evmdb_state_t state;
static evmdb_evm_t *evm;

static void setup(void) {
    int rc = evmdb_state_init(&state, "127.0.0.1", 6379, NULL, TEST_REDIS_DB);
    if (rc != 0) {
        printf("test_evm: SKIPPED (Redis not available)\n");
        exit(0);
    }

    redisReply *reply = redisCommand(state.ctx, "FLUSHDB");
    if (reply) {
        freeReplyObject(reply);
    }

    evm = evmdb_evm_create(NULL);
    assert(evm != NULL);
}

static void teardown(void) {
    redisReply *reply = redisCommand(state.ctx, "FLUSHDB");
    if (reply) {
        freeReplyObject(reply);
    }

    evmdb_evm_destroy(evm);
    evmdb_state_close(&state);
}

static void test_contract_creation_stores_code(void) {
    static const uint8_t code[] = {0x60, 0x00, 0x60, 0x00, 0xf3};
    evmdb_tx_t tx = {0};
    evmdb_block_context_t block_ctx = {
        .number = 0,
        .timestamp = 0,
        .coinbase = {{0}},
        .gas_limit = 30000000,
        .base_fee = 1000000,
        .chain_id = 100100,
    };
    evmdb_exec_result_t result = {0};
    evmdb_address_t contract_address;
    evmdb_bytes_t stored_code;
    uint64_t nonce = 0;
    int rc;

    for (size_t i = 0; i < sizeof(tx.from.bytes); i++) {
        tx.from.bytes[i] = (uint8_t)(i + 1);
    }
    tx.nonce = 0;
    tx.gas_limit = 100000;
    tx.to_is_null = true;
    tx.data.data = malloc(sizeof(code));
    assert(tx.data.data != NULL);
    memcpy(tx.data.data, code, sizeof(code));
    tx.data.len = sizeof(code);

    rc = evmdb_evm_execute_tx(evm, &state, &tx, &block_ctx, &result);
    assert(rc == 0);
    assert(result.success);

    evmdb_state_get_nonce(&state, &tx.from, &nonce);
    assert(nonce == 1);

    rc = evmdb_evm_compute_create_address(&tx.from, 0, &contract_address);
    assert(rc == 0);

    evmdb_state_get_code(&state, &contract_address, &stored_code);
    assert(stored_code.len == sizeof(code));
    assert(memcmp(stored_code.data, code, sizeof(code)) == 0);

    free(stored_code.data);
    evmdb_tx_free(&tx);
}

static void test_call_reads_storage(void) {
    static const uint8_t code[] = {0x60, 0x00, 0x54, 0x60, 0x00,
                                   0x52, 0x60, 0x20, 0x60, 0x00, 0xf3};
    evmdb_address_t contract = {{0x11}};
    evmdb_address_t caller = {{0x22}};
    evmdb_bytes32_t key = {{0}};
    evmdb_bytes32_t value = {{0}};
    evmdb_bytes_t data = {0};
    evmdb_block_context_t block_ctx = {
        .number = 0,
        .timestamp = 0,
        .coinbase = {{0}},
        .gas_limit = 30000000,
        .base_fee = 1000000,
        .chain_id = 100100,
    };
    evmdb_exec_result_t result = {0};
    int rc;

    value.bytes[31] = 0x2a;
    evmdb_state_set_code(&state, &contract, code, sizeof(code));
    evmdb_state_set_storage(&state, &contract, &key, &value);

    rc = evmdb_evm_call(evm, &state, &caller, &contract, &data, &block_ctx,
                        100000, &result);
    assert(rc == 0);
    assert(result.success);
    assert(result.output.len == 32);
    assert(result.output.data[31] == 0x2a);

    free(result.output.data);
}

static void test_tx_executes_contract_code(void) {
    static const uint8_t code[] = {0x60, 0x01, 0x60, 0x00, 0x55, 0x00};
    evmdb_address_t contract = {{0x33}};
    evmdb_tx_t tx = {0};
    evmdb_block_context_t block_ctx = {
        .number = 0,
        .timestamp = 0,
        .coinbase = {{0}},
        .gas_limit = 30000000,
        .base_fee = 1000000,
        .chain_id = 100100,
    };
    evmdb_exec_result_t result = {0};
    evmdb_bytes32_t key = {{0}};
    evmdb_bytes32_t value = {{0}};
    uint64_t nonce = 0;
    int rc;

    evmdb_state_set_code(&state, &contract, code, sizeof(code));

    tx.from.bytes[19] = 0x44;
    tx.to = contract;
    tx.nonce = 0;
    tx.gas_limit = 100000;
    tx.to_is_null = false;

    rc = evmdb_evm_execute_tx(evm, &state, &tx, &block_ctx, &result);
    assert(rc == 0);
    assert(result.success);

    evmdb_state_get_storage(&state, &contract, &key, &value);
    assert(value.bytes[31] == 0x01);
    evmdb_state_get_nonce(&state, &tx.from, &nonce);
    assert(nonce == 1);
}

int main(void) {
    setup();
    test_contract_creation_stores_code();
    test_call_reads_storage();
    test_tx_executes_contract_code();
    teardown();

    printf("test_evm: all tests passed\n");
    return 0;
}
