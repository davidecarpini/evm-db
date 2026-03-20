#include "evmdb/crypto.h"
#include "evmdb/hex.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void expect_hash(const char *input, const char *expected_hex) {
    evmdb_hash_t hash;
    char actual_hex[67];

    evmdb_keccak256((const uint8_t *)input, strlen(input), &hash);
    evmdb_hex_encode(hash.bytes, sizeof(hash.bytes), actual_hex,
                     sizeof(actual_hex));

    assert(strcmp(actual_hex, expected_hex) == 0);
}

int main(void) {
    expect_hash("", "0xc5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470");
    expect_hash("abc", "0x4e03657aea45a94fc7d47ba826c8d667c0d1e6e33a64a036ec44f58fa12d6c45");

    printf("test_crypto: all tests passed\n");
    return 0;
}
