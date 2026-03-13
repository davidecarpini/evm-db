#include "evmdb/block.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_tx_free_null(void) {
    evmdb_tx_t tx = {0};
    evmdb_tx_free(&tx); /* should not crash */
}

static void test_receipt_free_null(void) {
    evmdb_receipt_t receipt = {0};
    evmdb_receipt_free(&receipt);
}

static void test_block_free_null(void) {
    evmdb_block_t block = {0};
    evmdb_block_free(&block);
}

/*
 * TODO: Add RLP decode tests with real Ethereum signed transactions.
 * Test vectors can be taken from the Ethereum test suite:
 * https://github.com/ethereum/tests/tree/develop/TransactionTests
 */

int main(void) {
    test_tx_free_null();
    test_receipt_free_null();
    test_block_free_null();

    printf("test_tx: all tests passed\n");
    return 0;
}
