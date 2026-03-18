#ifndef EVMDB_TYPES_H
#define EVMDB_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ---- Ethereum primitive types ------------------------------------------- */

typedef struct {
    uint8_t bytes[20];
} evmdb_address_t;

typedef struct {
    uint8_t bytes[32];
} evmdb_bytes32_t;

typedef struct {
    uint8_t bytes[32];
} evmdb_hash_t;

typedef struct {
    uint8_t *data;
    size_t   len;
} evmdb_bytes_t;

/* ---- Account ------------------------------------------------------------ */

typedef struct {
    evmdb_address_t address;
    uint64_t        nonce;
    evmdb_bytes32_t balance;      /* 256-bit unsigned, big-endian */
    evmdb_hash_t    code_hash;
} evmdb_account_t;

/* ---- Transaction (RLP-decoded) ------------------------------------------ */

typedef enum {
    EVMDB_TX_LEGACY    = 0,
    EVMDB_TX_EIP2930   = 1,
    EVMDB_TX_EIP1559   = 2,
} evmdb_tx_type_t;

typedef struct {
    evmdb_tx_type_t type;
    uint64_t        chain_id;
    uint64_t        nonce;
    evmdb_bytes32_t gas_price;          /* legacy */
    evmdb_bytes32_t max_fee_per_gas;    /* EIP-1559 */
    evmdb_bytes32_t max_priority_fee;   /* EIP-1559 */
    uint64_t        gas_limit;
    evmdb_address_t to;
    bool            to_is_null;         /* true = contract creation */
    evmdb_bytes32_t value;
    evmdb_bytes_t   data;               /* calldata */

    /* signature */
    uint8_t         v;
    evmdb_bytes32_t r;
    evmdb_bytes32_t s;

    /* computed */
    evmdb_hash_t    hash;               /* keccak256 of signed tx */
    evmdb_address_t from;               /* recovered sender */
    evmdb_bytes_t   raw;                /* original RLP-encoded bytes */
} evmdb_tx_t;

/* ---- Log / Event -------------------------------------------------------- */

typedef struct {
    evmdb_address_t address;
    evmdb_bytes32_t topics[4];
    uint8_t         topic_count;
    evmdb_bytes_t   data;
} evmdb_log_t;

/* ---- Transaction Receipt ------------------------------------------------ */

typedef struct {
    evmdb_hash_t    tx_hash;
    uint64_t        tx_index;
    uint64_t        block_number;
    evmdb_hash_t    block_hash;
    evmdb_address_t from;
    evmdb_address_t to;
    uint64_t        gas_used;
    uint64_t        cumulative_gas;
    bool            status;             /* 1 = success, 0 = revert */
    evmdb_log_t    *logs;
    size_t          log_count;
    evmdb_bytes_t   return_data;
} evmdb_receipt_t;

/* ---- Block context (synthetic — no real block production) --------------- */

typedef struct {
    uint64_t        number;
    uint64_t        timestamp;
    evmdb_address_t coinbase;
    uint64_t        gas_limit;
    uint64_t        base_fee;
    uint64_t        chain_id;
} evmdb_block_context_t;

/* ---- Execution result --------------------------------------------------- */

typedef struct {
    bool            success;
    uint64_t        gas_used;
    evmdb_bytes_t   output;
    evmdb_log_t    *logs;
    size_t          log_count;
    char            error[256];
} evmdb_exec_result_t;

#endif /* EVMDB_TYPES_H */
