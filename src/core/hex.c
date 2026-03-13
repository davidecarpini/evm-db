#include "evmdb/hex.h"

#include <string.h>
#include <stdio.h>

static const char hex_chars[] = "0123456789abcdef";

int evmdb_hex_encode(const uint8_t *data, size_t len, char *out,
                     size_t out_len) {
    size_t needed = len * 2 + 3; /* "0x" + hex + '\0' */
    if (out_len < needed) {
        return -1;
    }

    out[0] = '0';
    out[1] = 'x';

    for (size_t i = 0; i < len; i++) {
        out[2 + i * 2]     = hex_chars[(data[i] >> 4) & 0x0f];
        out[2 + i * 2 + 1] = hex_chars[data[i] & 0x0f];
    }
    out[2 + len * 2] = '\0';

    return 0;
}

static int hex_char_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int evmdb_hex_decode(const char *hex, uint8_t *out, size_t out_len) {
    if (!hex) return -1;

    /* Skip 0x prefix */
    if (hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) {
        hex += 2;
    }

    size_t hex_len = strlen(hex);
    if (hex_len % 2 != 0) return -1;

    size_t byte_len = hex_len / 2;
    if (byte_len > out_len) return -1;

    for (size_t i = 0; i < byte_len; i++) {
        int hi = hex_char_val(hex[i * 2]);
        int lo = hex_char_val(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return -1;
        out[i] = (uint8_t)((hi << 4) | lo);
    }

    return (int)byte_len;
}

int evmdb_hex_from_uint64(uint64_t val, char *out, size_t out_len) {
    if (val == 0) {
        if (out_len < 4) return -1;
        memcpy(out, "0x0", 4);
        return 0;
    }

    char buf[19]; /* "0x" + max 16 hex digits + '\0' */
    int len = snprintf(buf, sizeof(buf), "0x%llx", (unsigned long long)val);
    if (len < 0 || (size_t)len >= out_len) return -1;

    memcpy(out, buf, (size_t)len + 1);
    return 0;
}

int evmdb_hex_to_uint64(const char *hex, uint64_t *out) {
    if (!hex || !out) return -1;

    if (hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) {
        hex += 2;
    }

    *out = 0;
    for (; *hex; hex++) {
        int v = hex_char_val(*hex);
        if (v < 0) return -1;
        *out = (*out << 4) | (uint64_t)v;
    }

    return 0;
}
