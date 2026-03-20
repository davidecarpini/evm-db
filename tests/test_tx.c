#include "evmdb/block.h"
#include "evmdb/crypto.h"

#include <assert.h>
#include <secp256k1.h>
#include <secp256k1_recovery.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_tx_free_null(void) {
    evmdb_tx_t tx = {0};
    evmdb_tx_free(&tx); /* should not crash */
}

static void test_receipt_free_null(void) {
    evmdb_receipt_t receipt = {0};
    evmdb_receipt_free(&receipt);
}

static void uint64_to_bytes32(uint64_t value, evmdb_bytes32_t *out) {
    memset(out, 0, sizeof(*out));

    for (int i = 31; i >= 0 && value > 0; i--) {
        out->bytes[i] = (uint8_t)(value & 0xff);
        value >>= 8;
    }
}

static void test_tx_recover_sender_eip1559(void) {
    static const uint8_t initcode[] = {0x60, 0x2a, 0x60, 0x00, 0x52,
                                       0x60, 0x20, 0x60, 0x00, 0xf3};
    const uint8_t seckey[32] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    };
    secp256k1_context *ctx;
    secp256k1_pubkey pubkey;
    secp256k1_ecdsa_recoverable_signature signature;
    uint8_t compact_sig[64];
    uint8_t pubkey_bytes[65];
    size_t pubkey_len = sizeof(pubkey_bytes);
    evmdb_hash_t signing_hash;
    evmdb_hash_t pubkey_hash;
    evmdb_tx_t tx = {0};
    int recid = 0;

    ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN |
                                   SECP256K1_CONTEXT_VERIFY);
    assert(ctx != NULL);
    assert(secp256k1_ec_pubkey_create(ctx, &pubkey, seckey) == 1);
    assert(secp256k1_ec_pubkey_serialize(ctx, pubkey_bytes, &pubkey_len,
                                         &pubkey,
                                         SECP256K1_EC_UNCOMPRESSED) == 1);
    evmdb_keccak256(pubkey_bytes + 1, 64, &pubkey_hash);

    tx.type = EVMDB_TX_EIP1559;
    tx.chain_id = 100100;
    tx.nonce = 7;
    tx.gas_limit = 100000;
    tx.to_is_null = true;
    uint64_to_bytes32(1000000000, &tx.max_priority_fee);
    uint64_to_bytes32(2000000000, &tx.max_fee_per_gas);
    tx.data.data = malloc(sizeof(initcode));
    assert(tx.data.data != NULL);
    memcpy(tx.data.data, initcode, sizeof(initcode));
    tx.data.len = sizeof(initcode);

    assert(evmdb_tx_signing_hash(&tx, &signing_hash) == 0);
    assert(secp256k1_ecdsa_sign_recoverable(ctx, &signature,
                                            signing_hash.bytes, seckey,
                                            NULL, NULL) == 1);
    assert(secp256k1_ecdsa_recoverable_signature_serialize_compact(
               ctx, compact_sig, &recid, &signature) == 1);

    memcpy(tx.r.bytes, compact_sig, 32);
    memcpy(tx.s.bytes, compact_sig + 32, 32);
    tx.v = (uint64_t)recid;

    assert(evmdb_tx_recover_sender(&tx) == 0);
    assert(memcmp(tx.from.bytes, pubkey_hash.bytes + 12, 20) == 0);

    secp256k1_context_destroy(ctx);
    evmdb_tx_free(&tx);
}

int main(void) {
    test_tx_free_null();
    test_receipt_free_null();
    test_tx_recover_sender_eip1559();

    printf("test_tx: all tests passed\n");
    return 0;
}
