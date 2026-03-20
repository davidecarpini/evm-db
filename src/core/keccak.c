#include "evmdb/crypto.h"

#include <string.h>

static uint64_t rotl64(uint64_t value, unsigned shift) {
    if (shift == 0) {
        return value;
    }
    return (value << shift) | (value >> (64 - shift));
}

static uint64_t load64_le(const uint8_t *data) {
    uint64_t value = 0;
    for (size_t i = 0; i < 8; i++) {
        value |= ((uint64_t)data[i]) << (8 * i);
    }
    return value;
}

static void store64_le(uint8_t *out, uint64_t value) {
    for (size_t i = 0; i < 8; i++) {
        out[i] = (uint8_t)((value >> (8 * i)) & 0xff);
    }
}

static void keccakf(uint64_t state[25]) {
    static const uint64_t round_constants[24] = {
        0x0000000000000001ULL, 0x0000000000008082ULL,
        0x800000000000808aULL, 0x8000000080008000ULL,
        0x000000000000808bULL, 0x0000000080000001ULL,
        0x8000000080008081ULL, 0x8000000000008009ULL,
        0x000000000000008aULL, 0x0000000000000088ULL,
        0x0000000080008009ULL, 0x000000008000000aULL,
        0x000000008000808bULL, 0x800000000000008bULL,
        0x8000000000008089ULL, 0x8000000000008003ULL,
        0x8000000000008002ULL, 0x8000000000000080ULL,
        0x000000000000800aULL, 0x800000008000000aULL,
        0x8000000080008081ULL, 0x8000000000008080ULL,
        0x0000000080000001ULL, 0x8000000080008008ULL,
    };
    static const unsigned rho[5][5] = {
        { 0, 36,  3, 41, 18 },
        { 1, 44, 10, 45,  2 },
        { 62, 6, 43, 15, 61 },
        { 28, 55, 25, 21, 56 },
        { 27, 20, 39,  8, 14 },
    };

    for (size_t round = 0; round < 24; round++) {
        uint64_t c[5];
        uint64_t d[5];
        uint64_t b[25];

        for (size_t x = 0; x < 5; x++) {
            c[x] = state[x] ^ state[x + 5] ^ state[x + 10]
                 ^ state[x + 15] ^ state[x + 20];
        }

        for (size_t x = 0; x < 5; x++) {
            d[x] = c[(x + 4) % 5] ^ rotl64(c[(x + 1) % 5], 1);
        }

        for (size_t y = 0; y < 5; y++) {
            for (size_t x = 0; x < 5; x++) {
                state[x + (5 * y)] ^= d[x];
            }
        }

        for (size_t y = 0; y < 5; y++) {
            for (size_t x = 0; x < 5; x++) {
                size_t new_x = y;
                size_t new_y = (2 * x + 3 * y) % 5;
                b[new_x + (5 * new_y)] = rotl64(state[x + (5 * y)], rho[x][y]);
            }
        }

        for (size_t y = 0; y < 5; y++) {
            for (size_t x = 0; x < 5; x++) {
                state[x + (5 * y)] = b[x + (5 * y)]
                    ^ ((~b[((x + 1) % 5) + (5 * y)])
                    & b[((x + 2) % 5) + (5 * y)]);
            }
        }

        state[0] ^= round_constants[round];
    }
}

void evmdb_keccak256(const uint8_t *data, size_t len, evmdb_hash_t *out) {
    const size_t rate = 136;
    uint64_t state[25] = {0};
    uint8_t block[136] = {0};

    while (len >= rate) {
        for (size_t lane = 0; lane < rate / 8; lane++) {
            state[lane] ^= load64_le(data + (lane * 8));
        }
        keccakf(state);
        data += rate;
        len -= rate;
    }

    if (len > 0) {
        memcpy(block, data, len);
    }
    block[len] = 0x01;
    block[rate - 1] |= 0x80;

    for (size_t lane = 0; lane < rate / 8; lane++) {
        state[lane] ^= load64_le(block + (lane * 8));
    }
    keccakf(state);

    for (size_t lane = 0; lane < 4; lane++) {
        store64_le(out->bytes + (lane * 8), state[lane]);
    }
}
