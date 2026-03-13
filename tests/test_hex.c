#include "evmdb/hex.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_encode(void) {
    uint8_t data[] = {0xde, 0xad, 0xbe, 0xef};
    char out[12];
    assert(evmdb_hex_encode(data, 4, out, sizeof(out)) == 0);
    assert(strcmp(out, "0xdeadbeef") == 0);
}

static void test_decode(void) {
    uint8_t out[4];
    int n = evmdb_hex_decode("0xdeadbeef", out, sizeof(out));
    assert(n == 4);
    assert(out[0] == 0xde);
    assert(out[1] == 0xad);
    assert(out[2] == 0xbe);
    assert(out[3] == 0xef);
}

static void test_decode_no_prefix(void) {
    uint8_t out[2];
    int n = evmdb_hex_decode("ff00", out, sizeof(out));
    assert(n == 2);
    assert(out[0] == 0xff);
    assert(out[1] == 0x00);
}

static void test_uint64(void) {
    char hex[32];
    assert(evmdb_hex_from_uint64(0, hex, sizeof(hex)) == 0);
    assert(strcmp(hex, "0x0") == 0);

    assert(evmdb_hex_from_uint64(255, hex, sizeof(hex)) == 0);
    assert(strcmp(hex, "0xff") == 0);

    assert(evmdb_hex_from_uint64(21000, hex, sizeof(hex)) == 0);
    assert(strcmp(hex, "0x5208") == 0);

    uint64_t val;
    assert(evmdb_hex_to_uint64("0x5208", &val) == 0);
    assert(val == 21000);

    assert(evmdb_hex_to_uint64("0x0", &val) == 0);
    assert(val == 0);
}

static void test_roundtrip(void) {
    uint8_t original[32];
    for (int i = 0; i < 32; i++) original[i] = (uint8_t)i;

    char hex[67];
    assert(evmdb_hex_encode(original, 32, hex, sizeof(hex)) == 0);

    uint8_t decoded[32];
    int n = evmdb_hex_decode(hex, decoded, sizeof(decoded));
    assert(n == 32);
    assert(memcmp(original, decoded, 32) == 0);
}

int main(void) {
    test_encode();
    test_decode();
    test_decode_no_prefix();
    test_uint64();
    test_roundtrip();

    printf("test_hex: all tests passed\n");
    return 0;
}
