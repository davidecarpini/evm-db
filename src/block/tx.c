#include "evmdb/block.h"
#include "evmdb/log.h"

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
        tx->v = (uint8_t)rlp_to_uint64(item, item_len);

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
        tx->v = (uint8_t)rlp_to_uint64(item, item_len);

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

    /*
     * TODO: Compute tx hash = keccak256(raw)
     * Requires a keccak256 implementation.
     */

    return 0;
}

int evmdb_tx_recover_sender(evmdb_tx_t *tx) {
    /*
     * TODO: Recover sender address from (v, r, s) signature.
     *
     * Steps:
     * 1. Compute signing hash (RLP of tx fields without v, r, s)
     * 2. Use secp256k1_ecdsa_recover to get public key
     * 3. Keccak256 of public key, take last 20 bytes = address
     *
     * Requires: libsecp256k1 with recovery module + keccak256.
     */

    /* STUB: set from to zero address */
    memset(&tx->from, 0, sizeof(tx->from));

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

void evmdb_block_free(evmdb_block_t *block) {
    free(block->tx_hashes);
    block->tx_hashes = NULL;
}
