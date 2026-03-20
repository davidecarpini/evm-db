#ifndef EVMDB_CRYPTO_H
#define EVMDB_CRYPTO_H

#include "evmdb/types.h"

#include <stddef.h>
#include <stdint.h>

void evmdb_keccak256(const uint8_t *data, size_t len, evmdb_hash_t *out);

#endif /* EVMDB_CRYPTO_H */
