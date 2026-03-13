#include "evmdb/evm.h"
#include "evmdb/log.h"

#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

/*
 * EVM executor using the EVMC interface.
 *
 * In the MVP, we provide a stub that returns success for all calls.
 * The real implementation will load evmone as a shared library via EVMC
 * and implement the host interface to read/write state from Redis.
 *
 * EVMC flow:
 * 1. Load evmone via evmc_load_and_create()
 * 2. For each tx, call evmc_execute() with:
 *    - The EVM bytecode (contract code from Redis)
 *    - The message (sender, value, gas, input data)
 *    - A host context that implements get_storage, set_storage, etc.
 *      backed by Redis
 * 3. Collect the result (status, gas used, output, logs)
 */

struct evmdb_evm {
    void *evmone_handle;  /* dlopen handle to libevmone */
    /* struct evmc_vm *vm; — will hold evmone instance */
    int initialized;
};

evmdb_evm_t *evmdb_evm_create(const char *evmone_path) {
    evmdb_evm_t *evm = calloc(1, sizeof(*evm));
    if (!evm) return NULL;

    /*
     * TODO: Load evmone shared library via EVMC.
     *
     * const char *path = evmone_path ? evmone_path : "libevmone.so";
     * evm->evmone_handle = dlopen(path, RTLD_NOW);
     * if (!evm->evmone_handle) {
     *     LOG_ERROR("failed to load evmone: %s", dlerror());
     *     free(evm);
     *     return NULL;
     * }
     *
     * evmc_create_fn create = dlsym(evm->evmone_handle, "evmc_create_evmone");
     * evm->vm = create();
     */

    evm->initialized = 1;
    LOG_INFO("EVM executor created (stub mode — evmone not yet linked)");

    return evm;
}

void evmdb_evm_destroy(evmdb_evm_t *evm) {
    if (!evm) return;
    if (evm->evmone_handle) {
        dlclose(evm->evmone_handle);
    }
    free(evm);
}

/*
 * Execute a transaction.
 *
 * Full implementation will:
 * 1. Validate nonce
 * 2. Deduct gas upfront
 * 3. If to_is_null: CREATE (deploy contract)
 *    Else: CALL (execute contract code or transfer)
 * 4. Call evmc_execute() with host callbacks reading Redis
 * 5. Apply state changes (balance transfers, storage writes)
 * 6. Refund unused gas
 * 7. Collect logs
 */
int evmdb_evm_execute_tx(evmdb_evm_t *evm, evmdb_state_t *state,
                         const evmdb_tx_t *tx, const evmdb_block_t *block,
                         evmdb_exec_result_t *result) {
    if (!evm || !evm->initialized) {
        snprintf(result->error, sizeof(result->error), "EVM not initialized");
        return -1;
    }

    (void)state;
    (void)block;

    /*
     * STUB: Accept all transactions as successful.
     * Replace with real EVMC execution.
     */

    /* Validate nonce */
    uint64_t current_nonce = 0;
    evmdb_state_get_nonce(state, &tx->from, &current_nonce);
    if (tx->nonce != current_nonce) {
        snprintf(result->error, sizeof(result->error),
                 "nonce mismatch: expected %llu, got %llu",
                 (unsigned long long)current_nonce,
                 (unsigned long long)tx->nonce);
        result->success = false;
        result->gas_used = 0;
        return 0; /* not an internal error, tx just fails */
    }

    /* Increment nonce */
    evmdb_state_set_nonce(state, &tx->from, current_nonce + 1);

    /* Base gas cost: 21000 for simple transfer */
    uint64_t intrinsic_gas = 21000;
    for (size_t i = 0; i < tx->data.len; i++) {
        intrinsic_gas += (tx->data.data[i] == 0) ? 4 : 16;
    }

    if (intrinsic_gas > tx->gas_limit) {
        snprintf(result->error, sizeof(result->error), "intrinsic gas too low");
        result->success = false;
        result->gas_used = tx->gas_limit;
        return 0;
    }

    /*
     * TODO: Actual EVM execution via evmone goes here.
     * For now, just mark as successful with intrinsic gas used.
     */

    result->success = true;
    result->gas_used = intrinsic_gas;
    result->output.data = NULL;
    result->output.len = 0;
    result->logs = NULL;
    result->log_count = 0;
    result->error[0] = '\0';

    return 0;
}

int evmdb_evm_call(evmdb_evm_t *evm, evmdb_state_t *state,
                   const evmdb_address_t *from, const evmdb_address_t *to,
                   const evmdb_bytes_t *data, uint64_t gas,
                   evmdb_exec_result_t *result) {
    if (!evm || !evm->initialized) {
        snprintf(result->error, sizeof(result->error), "EVM not initialized");
        return -1;
    }

    (void)state;
    (void)from;
    (void)to;
    (void)data;
    (void)gas;

    /* STUB: Return empty result */
    result->success = true;
    result->gas_used = 21000;
    result->output.data = NULL;
    result->output.len = 0;
    result->logs = NULL;
    result->log_count = 0;
    result->error[0] = '\0';

    return 0;
}
