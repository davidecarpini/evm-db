#include "evmdb/block.h"
#include "evmdb/crypto.h"
#include "evmdb/log.h"

#include <secp256k1.h>
#include <secp256k1_recovery.h>
#include <stdlib.h>
#include <string.h>

/*
 * RLP decoding and transaction parsing.
 *
 * RLP (Recursive Length Prefix) is Ethereum's serialization format.
 * A signed transaction is either:
 * - Legacy: RLP([nonce, gasPrice, gasLimit, to, value, data, v, r, s])
 * - EIP-2930: 0x01 || RLP([chainId, nonce, gasPrice, gasLimit, to, value,
 *                           data, accessList, signatureYParity, signatureR,
 *                           signatureS])
 * - EIP-1559: 0x02 || RLP([chainId, nonce, maxPriorityFeePerGas,
 *                           maxFeePerGas, gasLimit, to, value, data,
 *                           accessList, signatureYParity, signatureR,
 *                           signatureS])
 */

/* ---- Minimal RLP decoder ------------------------------------------------ */

typedef struct {
    const uint8_t *data;
    size_t         pos;
    size_t         len;
} rlp_reader_t;

/* Returns 0 on success. Sets *out_data and *out_len to the item's content. */
static int rlp_decode_item(rlp_reader_t *r, const uint8_t **out_data,
                           size_t *out_len) {
    if (r->pos >= r->len) return -1;

    uint8_t prefix = r->data[r->pos];

    if (prefix <= 0x7f) {
        /* Single byte */
        *out_data = &r->data[r->pos];
        *out_len = 1;
        r->pos++;
        return 0;
    }

    if (prefix <= 0xb7) {
        /* Short string: 0-55 bytes */
        size_t str_len = prefix - 0x80;
        r->pos++;
        if (r->pos + str_len > r->len) return -1;
        *out_data = &r->data[r->pos];
        *out_len = str_len;
        r->pos += str_len;
        return 0;
    }

    if (prefix <= 0xbf) {
        /* Long string */
        size_t len_bytes = prefix - 0xb7;
        r->pos++;
        if (r->pos + len_bytes > r->len) return -1;
        size_t str_len = 0;
        for (size_t i = 0; i < len_bytes; i++) {
            str_len = (str_len << 8) | r->data[r->pos + i];
        }
        r->pos += len_bytes;
        if (r->pos + str_len > r->len) return -1;
        *out_data = &r->data[r->pos];
        *out_len = str_len;
        r->pos += str_len;
        return 0;
    }

    if (prefix <= 0xf7) {
        /* Short list: total payload 0-55 bytes */
        size_t list_len = prefix - 0xc0;
        r->pos++;
        *out_data = &r->data[r->pos];
        *out_len = list_len;
        /* Don't advance pos — caller will parse list contents */
        return 0;
    }

    /* Long list */
    size_t len_bytes = prefix - 0xf7;
    r->pos++;
    if (r->pos + len_bytes > r->len) return -1;
    size_t list_len = 0;
    for (size_t i = 0; i < len_bytes; i++) {
        list_len = (list_len << 8) | r->data[r->pos + i];
    }
    r->pos += len_bytes;
    *out_data = &r->data[r->pos];
    *out_len = list_len;
    return 0;
}

static uint64_t rlp_to_uint64(const uint8_t *data, size_t len) {
    uint64_t val = 0;
    for (size_t i = 0; i < len && i < 8; i++) {
        val = (val << 8) | data[i];
    }
    return val;
}

static void rlp_to_bytes32(const uint8_t *data, size_t len,
                           evmdb_bytes32_t *out) {
    memset(out->bytes, 0, 32);
    if (len > 32) len = 32;
    /* Right-align (big-endian) */
    memcpy(out->bytes + (32 - len), data, len);
}

static void rlp_to_address(const uint8_t *data, size_t len,
                           evmdb_address_t *out) {
    memset(out->bytes, 0, 20);
    if (len > 20) len = 20;
    memcpy(out->bytes + (20 - len), data, len);
}

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
} rlp_buffer_t;

static void rlp_buffer_free(rlp_buffer_t *buf) {
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

static int rlp_buffer_reserve(rlp_buffer_t *buf, size_t additional) {
    size_t needed = buf->len + additional;
    size_t new_cap = buf->cap ? buf->cap : 64;
    uint8_t *new_data;

    if (needed <= buf->cap) {
        return 0;
    }

    while (new_cap < needed) {
        new_cap *= 2;
    }

    new_data = realloc(buf->data, new_cap);
    if (!new_data) {
        return -1;
    }

    buf->data = new_data;
    buf->cap = new_cap;
    return 0;
}

static int rlp_buffer_append(rlp_buffer_t *buf, const uint8_t *data,
                             size_t len) {
    if (rlp_buffer_reserve(buf, len) != 0) {
        return -1;
    }

    if (len > 0) {
        memcpy(buf->data + buf->len, data, len);
    }
    buf->len += len;
    return 0;
}

static int rlp_buffer_append_byte(rlp_buffer_t *buf, uint8_t value) {
    return rlp_buffer_append(buf, &value, 1);
}

static int rlp_append_length(rlp_buffer_t *buf, size_t len, uint8_t short_base,
                             uint8_t long_base) {
    uint8_t len_bytes[8];
    size_t len_len = 0;

    if (len <= 55) {
        return rlp_buffer_append_byte(buf, (uint8_t)(short_base + len));
    }

    while (len > 0) {
        len_bytes[7 - len_len] = (uint8_t)(len & 0xff);
        len >>= 8;
        len_len++;
    }

    if (rlp_buffer_append_byte(buf, (uint8_t)(long_base + len_len)) != 0) {
        return -1;
    }

    return rlp_buffer_append(buf, len_bytes + (8 - len_len), len_len);
}

static int rlp_append_bytes(rlp_buffer_t *buf, const uint8_t *data, size_t len) {
    if (len == 1 && data && data[0] <= 0x7f) {
        return rlp_buffer_append_byte(buf, data[0]);
    }

    if (rlp_append_length(buf, len, 0x80, 0xb7) != 0) {
        return -1;
    }

    return rlp_buffer_append(buf, data, len);
}

static int rlp_append_uint64(rlp_buffer_t *buf, uint64_t value) {
    uint8_t bytes[8];
    size_t len = 0;

    if (value == 0) {
        return rlp_append_bytes(buf, NULL, 0);
    }

    while (value > 0) {
        bytes[7 - len] = (uint8_t)(value & 0xff);
        value >>= 8;
        len++;
    }

    return rlp_append_bytes(buf, bytes + (8 - len), len);
}

static int rlp_append_bytes32(rlp_buffer_t *buf, const evmdb_bytes32_t *value) {
    size_t offset = 0;

    while (offset < 32 && value->bytes[offset] == 0) {
        offset++;
    }

    if (offset == 32) {
        return rlp_append_bytes(buf, NULL, 0);
    }

    return rlp_append_bytes(buf, value->bytes + offset, 32 - offset);
}

static int rlp_append_address(rlp_buffer_t *buf, const evmdb_address_t *addr,
                              bool is_null) {
    if (is_null) {
        return rlp_append_bytes(buf, NULL, 0);
    }

    return rlp_append_bytes(buf, addr->bytes, sizeof(addr->bytes));
}

static int rlp_append_empty_list(rlp_buffer_t *buf) {
    return rlp_buffer_append_byte(buf, 0xc0);
}

static int rlp_wrap_list(rlp_buffer_t *out, const rlp_buffer_t *payload) {
    if (rlp_append_length(out, payload->len, 0xc0, 0xf7) != 0) {
        return -1;
    }

    return rlp_buffer_append(out, payload->data, payload->len);
}

int evmdb_tx_signing_hash(const evmdb_tx_t *tx, evmdb_hash_t *out) {
    rlp_buffer_t payload = {0};
    rlp_buffer_t encoded = {0};
    int rc = -1;

    if (!tx || !out) {
        return -1;
    }

    if (tx->type == EVMDB_TX_EIP1559) {
        if (rlp_append_uint64(&payload, tx->chain_id) != 0 ||
            rlp_append_uint64(&payload, tx->nonce) != 0 ||
            rlp_append_bytes32(&payload, &tx->max_priority_fee) != 0 ||
            rlp_append_bytes32(&payload, &tx->max_fee_per_gas) != 0 ||
            rlp_append_uint64(&payload, tx->gas_limit) != 0 ||
            rlp_append_address(&payload, &tx->to, tx->to_is_null) != 0 ||
            rlp_append_bytes32(&payload, &tx->value) != 0 ||
            rlp_append_bytes(&payload, tx->data.data, tx->data.len) != 0 ||
            rlp_append_empty_list(&payload) != 0) {
            goto out;
        }

        if (rlp_buffer_append_byte(&encoded, 0x02) != 0 ||
            rlp_wrap_list(&encoded, &payload) != 0) {
            goto out;
        }
    } else if (tx->type == EVMDB_TX_LEGACY) {
        if (rlp_append_uint64(&payload, tx->nonce) != 0 ||
            rlp_append_bytes32(&payload, &tx->gas_price) != 0 ||
            rlp_append_uint64(&payload, tx->gas_limit) != 0 ||
            rlp_append_address(&payload, &tx->to, tx->to_is_null) != 0 ||
            rlp_append_bytes32(&payload, &tx->value) != 0 ||
            rlp_append_bytes(&payload, tx->data.data, tx->data.len) != 0) {
            goto out;
        }

        if (tx->chain_id != 0) {
            evmdb_bytes32_t zero = {{0}};

            if (rlp_append_uint64(&payload, tx->chain_id) != 0 ||
                rlp_append_bytes32(&payload, &zero) != 0 ||
                rlp_append_bytes32(&payload, &zero) != 0) {
                goto out;
            }
        }

        if (rlp_wrap_list(&encoded, &payload) != 0) {
            goto out;
        }
    } else {
        goto out;
    }

    evmdb_keccak256(encoded.data, encoded.len, out);
    rc = 0;

out:
    rlp_buffer_free(&payload);
    rlp_buffer_free(&encoded);
    return rc;
}

/* ---- Public API --------------------------------------------------------- */

int evmdb_tx_decode(const uint8_t *raw, size_t raw_len, evmdb_tx_t *tx) {
    if (!raw || raw_len == 0 || !tx) return -1;

    memset(tx, 0, sizeof(*tx));

    /* Store raw bytes */
    tx->raw.data = malloc(raw_len);
    if (!tx->raw.data) return -1;
    memcpy(tx->raw.data, raw, raw_len);
    tx->raw.len = raw_len;

    /* Determine tx type */
    const uint8_t *payload = raw;
    size_t payload_len = raw_len;

    if (raw[0] == 0x01) {
        tx->type = EVMDB_TX_EIP2930;
        payload++;
        payload_len--;
    } else if (raw[0] == 0x02) {
        tx->type = EVMDB_TX_EIP1559;
        payload++;
        payload_len--;
    } else {
        tx->type = EVMDB_TX_LEGACY;
    }

    /* Decode outer list */
    rlp_reader_t reader = {.data = payload, .pos = 0, .len = payload_len};
    const uint8_t *list_data;
    size_t list_len;
    if (rlp_decode_item(&reader, &list_data, &list_len) != 0) {
        return -1;
    }

    /* Create a reader for list contents */
    rlp_reader_t lr = {.data = list_data, .pos = 0, .len = list_len};
    const uint8_t *item;
    size_t item_len;

    if (tx->type == EVMDB_TX_EIP1559) {
        /* chainId, nonce, maxPriorityFee, maxFee, gasLimit, to, value,
           data, accessList, v, r, s */

        /* chainId */
        if (rlp_decode_item(&lr, &item, &item_len) != 0) return -1;
        tx->chain_id = rlp_to_uint64(item, item_len);

        /* nonce */
        if (rlp_decode_item(&lr, &item, &item_len) != 0) return -1;
        tx->nonce = rlp_to_uint64(item, item_len);

        /* maxPriorityFeePerGas */
        if (rlp_decode_item(&lr, &item, &item_len) != 0) return -1;
        rlp_to_bytes32(item, item_len, &tx->max_priority_fee);

        /* maxFeePerGas */
        if (rlp_decode_item(&lr, &item, &item_len) != 0) return -1;
        rlp_to_bytes32(item, item_len, &tx->max_fee_per_gas);

        /* gasLimit */
        if (rlp_decode_item(&lr, &item, &item_len) != 0) return -1;
        tx->gas_limit = rlp_to_uint64(item, item_len);

        /* to */
        if (rlp_decode_item(&lr, &item, &item_len) != 0) return -1;
        if (item_len == 0) {
            tx->to_is_null = true;
        } else {
            rlp_to_address(item, item_len, &tx->to);
        }

        /* value */
        if (rlp_decode_item(&lr, &item, &item_len) != 0) return -1;
        rlp_to_bytes32(item, item_len, &tx->value);

        /* data (calldata) */
        if (rlp_decode_item(&lr, &item, &item_len) != 0) return -1;
        if (item_len > 0) {
            tx->data.data = malloc(item_len);
            if (tx->data.data) {
                memcpy(tx->data.data, item, item_len);
                tx->data.len = item_len;
            }
        }

        /* accessList — skip for now */
        if (rlp_decode_item(&lr, &item, &item_len) != 0) return -1;

        /* signatureYParity (v) */
        if (rlp_decode_item(&lr, &item, &item_len) != 0) return -1;
        tx->v = rlp_to_uint64(item, item_len);

        /* r */
        if (rlp_decode_item(&lr, &item, &item_len) != 0) return -1;
        rlp_to_bytes32(item, item_len, &tx->r);

        /* s */
        if (rlp_decode_item(&lr, &item, &item_len) != 0) return -1;
        rlp_to_bytes32(item, item_len, &tx->s);

    } else if (tx->type == EVMDB_TX_LEGACY) {
        /* nonce, gasPrice, gasLimit, to, value, data, v, r, s */

        if (rlp_decode_item(&lr, &item, &item_len) != 0) return -1;
        tx->nonce = rlp_to_uint64(item, item_len);

        if (rlp_decode_item(&lr, &item, &item_len) != 0) return -1;
        rlp_to_bytes32(item, item_len, &tx->gas_price);

        if (rlp_decode_item(&lr, &item, &item_len) != 0) return -1;
        tx->gas_limit = rlp_to_uint64(item, item_len);

        if (rlp_decode_item(&lr, &item, &item_len) != 0) return -1;
        if (item_len == 0) {
            tx->to_is_null = true;
        } else {
            rlp_to_address(item, item_len, &tx->to);
        }

        if (rlp_decode_item(&lr, &item, &item_len) != 0) return -1;
        rlp_to_bytes32(item, item_len, &tx->value);

        if (rlp_decode_item(&lr, &item, &item_len) != 0) return -1;
        if (item_len > 0) {
            tx->data.data = malloc(item_len);
            if (tx->data.data) {
                memcpy(tx->data.data, item, item_len);
                tx->data.len = item_len;
            }
        }

        if (rlp_decode_item(&lr, &item, &item_len) != 0) return -1;
        tx->v = rlp_to_uint64(item, item_len);

        if (rlp_decode_item(&lr, &item, &item_len) != 0) return -1;
        rlp_to_bytes32(item, item_len, &tx->r);

        if (rlp_decode_item(&lr, &item, &item_len) != 0) return -1;
        rlp_to_bytes32(item, item_len, &tx->s);

        /* Derive chain_id from v for EIP-155 */
        if (tx->v >= 35) {
            tx->chain_id = (tx->v - 35) / 2;
        }
    }
    /* TODO: EIP-2930 */

    evmdb_keccak256(raw, raw_len, &tx->hash);

    return 0;
}

int evmdb_tx_recover_sender(evmdb_tx_t *tx) {
    evmdb_hash_t signing_hash;
    evmdb_hash_t pubkey_hash;
    secp256k1_context *ctx = NULL;
    secp256k1_ecdsa_recoverable_signature sig;
    secp256k1_pubkey pubkey;
    uint8_t compact_sig[64];
    uint8_t pubkey_bytes[65];
    size_t pubkey_len = sizeof(pubkey_bytes);
    int recid = -1;

    if (!tx || evmdb_tx_signing_hash(tx, &signing_hash) != 0) {
        return -1;
    }

    if (tx->type == EVMDB_TX_EIP1559 || tx->type == EVMDB_TX_EIP2930) {
        if (tx->v > 1) {
            return -1;
        }
        recid = (int)tx->v;
    } else if (tx->v == 27 || tx->v == 28) {
        recid = (int)(tx->v - 27);
    } else if (tx->v >= 35) {
        recid = (int)((tx->v - 35) % 2);
    } else {
        return -1;
    }

    memcpy(compact_sig, tx->r.bytes, 32);
    memcpy(compact_sig + 32, tx->s.bytes, 32);

    ctx = secp256k1_context_create(SECP256K1_CONTEXT_VERIFY);
    if (!ctx) {
        return -1;
    }

    if (!secp256k1_ecdsa_recoverable_signature_parse_compact(ctx, &sig,
                                                             compact_sig,
                                                             recid) ||
        !secp256k1_ecdsa_recover(ctx, &pubkey, &sig, signing_hash.bytes) ||
        !secp256k1_ec_pubkey_serialize(ctx, pubkey_bytes, &pubkey_len, &pubkey,
                                       SECP256K1_EC_UNCOMPRESSED)) {
        secp256k1_context_destroy(ctx);
        memset(&tx->from, 0, sizeof(tx->from));
        return -1;
    }

    secp256k1_context_destroy(ctx);

    evmdb_keccak256(pubkey_bytes + 1, 64, &pubkey_hash);
    memcpy(tx->from.bytes, pubkey_hash.bytes + 12, sizeof(tx->from.bytes));
    return 0;
}

void evmdb_tx_free(evmdb_tx_t *tx) {
    if (tx->data.data) {
        free(tx->data.data);
        tx->data.data = NULL;
    }
    if (tx->raw.data) {
        free(tx->raw.data);
        tx->raw.data = NULL;
    }
}

void evmdb_receipt_free(evmdb_receipt_t *receipt) {
    if (receipt->logs) {
        for (size_t i = 0; i < receipt->log_count; i++) {
            free(receipt->logs[i].data.data);
        }
        free(receipt->logs);
        receipt->logs = NULL;
    }
    free(receipt->return_data.data);
    receipt->return_data.data = NULL;
}

