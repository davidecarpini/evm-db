#ifndef EVMDB_HEX_H
#define EVMDB_HEX_H

#include <stdint.h>
#include <stddef.h>

/* Encode bytes to hex string with 0x prefix. out must be at least len*2+3. */
int evmdb_hex_encode(const uint8_t *data, size_t len, char *out, size_t out_len);

/* Decode 0x-prefixed hex string to bytes. out must be at least strlen(hex)/2.
   Returns number of bytes written, or -1 on error. */
int evmdb_hex_decode(const char *hex, uint8_t *out, size_t out_len);

/* Encode uint64 to 0x-prefixed hex (no leading zeros). */
int evmdb_hex_from_uint64(uint64_t val, char *out, size_t out_len);

/* Decode 0x-prefixed hex to uint64. Returns 0 on success. */
int evmdb_hex_to_uint64(const char *hex, uint64_t *out);

#endif /* EVMDB_HEX_H */
