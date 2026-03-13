#include "evmdb/types.h"

#include <string.h>

/*
 * EIP-1559 base fee calculation.
 *
 * After each block, the base fee adjusts:
 * - If block was >50% full: base fee increases (up to 12.5%)
 * - If block was <50% full: base fee decreases (up to 12.5%)
 * - If block was exactly 50% full: no change
 *
 * This keeps fees stable under varying load.
 */

#define EIP1559_ELASTICITY_MULTIPLIER 2
#define EIP1559_BASE_FEE_CHANGE_DENOMINATOR 8

uint64_t evmdb_gas_calc_base_fee(uint64_t parent_base_fee,
                                  uint64_t parent_gas_used,
                                  uint64_t parent_gas_limit) {
    uint64_t parent_gas_target = parent_gas_limit / EIP1559_ELASTICITY_MULTIPLIER;

    if (parent_gas_used == parent_gas_target) {
        return parent_base_fee;
    }

    if (parent_gas_used > parent_gas_target) {
        /* Increase base fee */
        uint64_t gas_used_delta = parent_gas_used - parent_gas_target;
        uint64_t fee_delta = parent_base_fee * gas_used_delta
                             / parent_gas_target
                             / EIP1559_BASE_FEE_CHANGE_DENOMINATOR;
        if (fee_delta < 1) fee_delta = 1;
        return parent_base_fee + fee_delta;
    }

    /* Decrease base fee */
    uint64_t gas_used_delta = parent_gas_target - parent_gas_used;
    uint64_t fee_delta = parent_base_fee * gas_used_delta
                         / parent_gas_target
                         / EIP1559_BASE_FEE_CHANGE_DENOMINATOR;
    if (fee_delta >= parent_base_fee) {
        return 1; /* minimum base fee */
    }
    return parent_base_fee - fee_delta;
}

/*
 * Calculate intrinsic gas cost of a transaction.
 * EIP-2028: 16 gas per non-zero byte, 4 gas per zero byte.
 * 21000 base cost for transfer, 53000 for contract creation.
 */
uint64_t evmdb_gas_intrinsic(const uint8_t *data, size_t data_len,
                              int is_create) {
    uint64_t gas = is_create ? 53000 : 21000;

    for (size_t i = 0; i < data_len; i++) {
        gas += (data[i] == 0) ? 4 : 16;
    }

    return gas;
}
