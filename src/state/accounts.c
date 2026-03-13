#include "evmdb/state.h"
#include "evmdb/log.h"

#include <string.h>

/* ---- Utility: 256-bit balance arithmetic -------------------------------- */

/* Compare two 256-bit big-endian values.
   Returns -1 if a < b, 0 if a == b, 1 if a > b. */
int evmdb_balance_cmp(const evmdb_bytes32_t *a, const evmdb_bytes32_t *b) {
    return memcmp(a->bytes, b->bytes, 32);
}

/* Subtract b from a, store result in out. All big-endian 256-bit.
   Returns -1 if underflow (a < b). */
int evmdb_balance_sub(const evmdb_bytes32_t *a, const evmdb_bytes32_t *b,
                      evmdb_bytes32_t *out) {
    int borrow = 0;
    for (int i = 31; i >= 0; i--) {
        int diff = (int)a->bytes[i] - (int)b->bytes[i] - borrow;
        if (diff < 0) {
            diff += 256;
            borrow = 1;
        } else {
            borrow = 0;
        }
        out->bytes[i] = (uint8_t)diff;
    }
    return borrow ? -1 : 0;
}

/* Add a + b, store result in out. All big-endian 256-bit.
   Returns -1 on overflow. */
int evmdb_balance_add(const evmdb_bytes32_t *a, const evmdb_bytes32_t *b,
                      evmdb_bytes32_t *out) {
    int carry = 0;
    for (int i = 31; i >= 0; i--) {
        int sum = (int)a->bytes[i] + (int)b->bytes[i] + carry;
        out->bytes[i] = (uint8_t)(sum & 0xff);
        carry = sum >> 8;
    }
    return carry ? -1 : 0;
}

/* Set a 256-bit value from a uint64_t. Big-endian. */
void evmdb_balance_from_uint64(uint64_t val, evmdb_bytes32_t *out) {
    memset(out->bytes, 0, 32);
    for (int i = 0; i < 8; i++) {
        out->bytes[31 - i] = (uint8_t)(val >> (i * 8));
    }
}

/* Check if a 256-bit value is zero. */
int evmdb_balance_is_zero(const evmdb_bytes32_t *val) {
    for (int i = 0; i < 32; i++) {
        if (val->bytes[i] != 0) return 0;
    }
    return 1;
}
