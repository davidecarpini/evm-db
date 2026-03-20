#include "evmdb/evm.h"
#include "evmdb/crypto.h"
#include "evmdb/log.h"

#include <evmc/evmc.h>
#include <evmc/helpers.h>
#include <evmone/evmone.h>

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

struct evmdb_evm {
    struct evmc_vm *vm;
    int initialized;
};

typedef struct {
    evmdb_address_t address;
    evmdb_bytes32_t key;
    evmdb_bytes32_t value;
} storage_write_t;

typedef struct {
    evmdb_state_t         *state;
    evmdb_block_context_t  block_ctx;
    evmdb_address_t        tx_origin;
    evmdb_bytes32_t        tx_gas_price;
    storage_write_t       *writes;
    size_t                 write_count;
    size_t                 write_cap;
} evm_exec_context_t;

static void address_to_evmc(const evmdb_address_t *src, evmc_address *dst) {
    memcpy(dst->bytes, src->bytes, sizeof(dst->bytes));
}

static void address_from_evmc(const evmc_address *src, evmdb_address_t *dst) {
    memcpy(dst->bytes, src->bytes, sizeof(dst->bytes));
}

static void bytes32_to_evmc(const evmdb_bytes32_t *src, evmc_bytes32 *dst) {
    memcpy(dst->bytes, src->bytes, sizeof(dst->bytes));
}

static void bytes32_from_evmc(const evmc_bytes32 *src, evmdb_bytes32_t *dst) {
    memcpy(dst->bytes, src->bytes, sizeof(dst->bytes));
}

static void bytes32_from_uint64(uint64_t value, evmdb_bytes32_t *out) {
    memset(out->bytes, 0, sizeof(out->bytes));
    for (size_t i = 0; i < 8; i++) {
        out->bytes[31 - i] = (uint8_t)(value & 0xff);
        value >>= 8;
    }
}

static bool bytes32_is_zero(const evmdb_bytes32_t *value) {
    for (size_t i = 0; i < sizeof(value->bytes); i++) {
        if (value->bytes[i] != 0) {
            return false;
        }
    }
    return true;
}

static void exec_context_destroy(evm_exec_context_t *ctx) {
    free(ctx->writes);
    ctx->writes = NULL;
    ctx->write_count = 0;
    ctx->write_cap = 0;
}

static bool exec_context_get_write(const evm_exec_context_t *ctx,
                                   const evmdb_address_t *address,
                                   const evmdb_bytes32_t *key,
                                   evmdb_bytes32_t *value) {
    for (size_t i = ctx->write_count; i > 0; i--) {
        const storage_write_t *write = &ctx->writes[i - 1];
        if (memcmp(&write->address, address, sizeof(*address)) == 0 &&
            memcmp(&write->key, key, sizeof(*key)) == 0) {
            if (value) {
                memcpy(value, &write->value, sizeof(*value));
            }
            return true;
        }
    }
    return false;
}

static int exec_context_set_write(evm_exec_context_t *ctx,
                                  const evmdb_address_t *address,
                                  const evmdb_bytes32_t *key,
                                  const evmdb_bytes32_t *value) {
    for (size_t i = 0; i < ctx->write_count; i++) {
        storage_write_t *write = &ctx->writes[i];
        if (memcmp(&write->address, address, sizeof(*address)) == 0 &&
            memcmp(&write->key, key, sizeof(*key)) == 0) {
            memcpy(&write->value, value, sizeof(*value));
            return 0;
        }
    }

    if (ctx->write_count == ctx->write_cap) {
        size_t new_cap = ctx->write_cap ? ctx->write_cap * 2 : 8;
        storage_write_t *writes = realloc(ctx->writes,
                                          new_cap * sizeof(*writes));
        if (!writes) {
            return -1;
        }
        ctx->writes = writes;
        ctx->write_cap = new_cap;
    }

    ctx->writes[ctx->write_count].address = *address;
    ctx->writes[ctx->write_count].key = *key;
    ctx->writes[ctx->write_count].value = *value;
    ctx->write_count++;
    return 0;
}

static int exec_context_commit(evm_exec_context_t *ctx) {
    for (size_t i = 0; i < ctx->write_count; i++) {
        storage_write_t *write = &ctx->writes[i];
        if (evmdb_state_set_storage(ctx->state, &write->address, &write->key,
                                    &write->value) != 0) {
            return -1;
        }
    }
    return 0;
}

static void copy_output(evmdb_exec_result_t *result, const uint8_t *data,
                        size_t len) {
    result->output.data = NULL;
    result->output.len = 0;

    if (len == 0) {
        return;
    }

    result->output.data = malloc(len);
    if (!result->output.data) {
        return;
    }

    memcpy(result->output.data, data, len);
    result->output.len = len;
}

static size_t rlp_encode_bytes(const uint8_t *data, size_t len, uint8_t *out) {
    if (len == 1 && data[0] <= 0x7f) {
        out[0] = data[0];
        return 1;
    }

    out[0] = (uint8_t)(0x80 + len);
    if (len > 0) {
        memcpy(out + 1, data, len);
    }
    return len + 1;
}

static size_t rlp_encode_uint64(uint64_t value, uint8_t *out) {
    uint8_t encoded[8];
    size_t len = 0;

    if (value == 0) {
        out[0] = 0x80;
        return 1;
    }

    while (value > 0) {
        encoded[7 - len] = (uint8_t)(value & 0xff);
        value >>= 8;
        len++;
    }

    return rlp_encode_bytes(encoded + (8 - len), len, out);
}

static size_t rlp_encode_list(const uint8_t *payload, size_t len, uint8_t *out) {
    out[0] = (uint8_t)(0xc0 + len);
    if (len > 0) {
        memcpy(out + 1, payload, len);
    }
    return len + 1;
}

static evm_exec_context_t *as_exec_context(struct evmc_host_context *context) {
    return (evm_exec_context_t *)context;
}

static bool host_account_exists(struct evmc_host_context *context,
                                const evmc_address *address) {
    evm_exec_context_t *ctx = as_exec_context(context);
    evmdb_address_t addr;
    evmdb_bytes32_t balance;
    uint64_t nonce = 0;
    evmdb_bytes_t code = {0};
    bool exists = false;

    address_from_evmc(address, &addr);
    evmdb_state_get_nonce(ctx->state, &addr, &nonce);
    evmdb_state_get_balance(ctx->state, &addr, &balance);
    evmdb_state_get_code(ctx->state, &addr, &code);

    exists = nonce != 0 || !bytes32_is_zero(&balance) || code.len != 0;
    free(code.data);
    return exists;
}

static evmc_bytes32 host_get_storage(struct evmc_host_context *context,
                                     const evmc_address *address,
                                     const evmc_bytes32 *key) {
    evm_exec_context_t *ctx = as_exec_context(context);
    evmdb_address_t addr;
    evmdb_bytes32_t storage_key;
    evmdb_bytes32_t value;
    evmc_bytes32 out = {{0}};

    address_from_evmc(address, &addr);
    bytes32_from_evmc(key, &storage_key);

    if (!exec_context_get_write(ctx, &addr, &storage_key, &value)) {
        evmdb_state_get_storage(ctx->state, &addr, &storage_key, &value);
    }

    bytes32_to_evmc(&value, &out);
    return out;
}

static enum evmc_storage_status host_set_storage(struct evmc_host_context *context,
                                                 const evmc_address *address,
                                                 const evmc_bytes32 *key,
                                                 const evmc_bytes32 *value) {
    evm_exec_context_t *ctx = as_exec_context(context);
    evmdb_address_t addr;
    evmdb_bytes32_t storage_key;
    evmdb_bytes32_t current;
    evmdb_bytes32_t next;
    bool has_overlay;

    address_from_evmc(address, &addr);
    bytes32_from_evmc(key, &storage_key);
    bytes32_from_evmc(value, &next);

    has_overlay = exec_context_get_write(ctx, &addr, &storage_key, &current);
    if (!has_overlay) {
        evmdb_state_get_storage(ctx->state, &addr, &storage_key, &current);
    }

    if (exec_context_set_write(ctx, &addr, &storage_key, &next) != 0) {
        return EVMC_STORAGE_ASSIGNED;
    }

    if (memcmp(&current, &next, sizeof(current)) == 0) {
        return EVMC_STORAGE_ASSIGNED;
    }
    if (bytes32_is_zero(&current) && !bytes32_is_zero(&next)) {
        return EVMC_STORAGE_ADDED;
    }
    if (!bytes32_is_zero(&current) && bytes32_is_zero(&next)) {
        return EVMC_STORAGE_DELETED;
    }
    return EVMC_STORAGE_MODIFIED;
}

static evmc_uint256be host_get_balance(struct evmc_host_context *context,
                                       const evmc_address *address) {
    evm_exec_context_t *ctx = as_exec_context(context);
    evmdb_address_t addr;
    evmdb_bytes32_t balance;
    evmc_uint256be out = {{0}};

    address_from_evmc(address, &addr);
    evmdb_state_get_balance(ctx->state, &addr, &balance);
    bytes32_to_evmc(&balance, &out);
    return out;
}

static size_t host_get_code_size(struct evmc_host_context *context,
                                 const evmc_address *address) {
    evm_exec_context_t *ctx = as_exec_context(context);
    evmdb_address_t addr;
    evmdb_bytes_t code = {0};
    size_t size;

    address_from_evmc(address, &addr);
    evmdb_state_get_code(ctx->state, &addr, &code);
    size = code.len;
    free(code.data);
    return size;
}

static evmc_bytes32 host_get_code_hash(struct evmc_host_context *context,
                                       const evmc_address *address) {
    evm_exec_context_t *ctx = as_exec_context(context);
    evmdb_address_t addr;
    evmdb_bytes_t code = {0};
    evmdb_hash_t hash;
    evmc_bytes32 out = {{0}};

    address_from_evmc(address, &addr);
    evmdb_state_get_code(ctx->state, &addr, &code);
    evmdb_keccak256(code.data, code.len, &hash);
    bytes32_to_evmc((const evmdb_bytes32_t *)&hash, &out);
    free(code.data);
    return out;
}

static size_t host_copy_code(struct evmc_host_context *context,
                             const evmc_address *address, size_t code_offset,
                             uint8_t *buffer_data, size_t buffer_size) {
    evm_exec_context_t *ctx = as_exec_context(context);
    evmdb_address_t addr;
    evmdb_bytes_t code = {0};
    size_t copied = 0;

    address_from_evmc(address, &addr);
    evmdb_state_get_code(ctx->state, &addr, &code);

    if (code_offset < code.len) {
        copied = code.len - code_offset;
        if (copied > buffer_size) {
            copied = buffer_size;
        }
        memcpy(buffer_data, code.data + code_offset, copied);
    }

    free(code.data);
    return copied;
}

static bool host_selfdestruct(struct evmc_host_context *context,
                              const evmc_address *address,
                              const evmc_address *beneficiary) {
    (void)context;
    (void)address;
    (void)beneficiary;
    return false;
}

static struct evmc_result host_call(struct evmc_host_context *context,
                                    const struct evmc_message *msg) {
    (void)context;
    (void)msg;
    return evmc_make_result(EVMC_FAILURE, 0, 0, NULL, 0);
}

static struct evmc_tx_context host_get_tx_context(struct evmc_host_context *context) {
    evm_exec_context_t *ctx = as_exec_context(context);
    struct evmc_tx_context tx_ctx;

    memset(&tx_ctx, 0, sizeof(tx_ctx));
    bytes32_to_evmc(&ctx->tx_gas_price, &tx_ctx.tx_gas_price);
    address_to_evmc(&ctx->tx_origin, &tx_ctx.tx_origin);
    address_to_evmc(&ctx->block_ctx.coinbase, &tx_ctx.block_coinbase);
    tx_ctx.block_number = (int64_t)ctx->block_ctx.number;
    tx_ctx.block_timestamp = (int64_t)ctx->block_ctx.timestamp;
    tx_ctx.block_gas_limit = (int64_t)ctx->block_ctx.gas_limit;
    bytes32_from_uint64(ctx->block_ctx.chain_id,
                        (evmdb_bytes32_t *)&tx_ctx.chain_id);
    bytes32_from_uint64(ctx->block_ctx.base_fee,
                        (evmdb_bytes32_t *)&tx_ctx.block_base_fee);
    return tx_ctx;
}

static evmc_bytes32 host_get_block_hash(struct evmc_host_context *context,
                                        int64_t number) {
    (void)context;
    (void)number;
    return (evmc_bytes32){{0}};
}

static void host_emit_log(struct evmc_host_context *context,
                          const evmc_address *address, const uint8_t *data,
                          size_t data_size, const evmc_bytes32 topics[],
                          size_t topics_count) {
    (void)context;
    (void)address;
    (void)data;
    (void)data_size;
    (void)topics;
    (void)topics_count;
}

static enum evmc_access_status host_access_account(
    struct evmc_host_context *context, const evmc_address *address) {
    (void)context;
    (void)address;
    return EVMC_ACCESS_COLD;
}

static enum evmc_access_status host_access_storage(
    struct evmc_host_context *context, const evmc_address *address,
    const evmc_bytes32 *key) {
    (void)context;
    (void)address;
    (void)key;
    return EVMC_ACCESS_COLD;
}

static evmc_bytes32 host_get_transient_storage(struct evmc_host_context *context,
                                               const evmc_address *address,
                                               const evmc_bytes32 *key) {
    (void)context;
    (void)address;
    (void)key;
    return (evmc_bytes32){{0}};
}

static void host_set_transient_storage(struct evmc_host_context *context,
                                       const evmc_address *address,
                                       const evmc_bytes32 *key,
                                       const evmc_bytes32 *value) {
    (void)context;
    (void)address;
    (void)key;
    (void)value;
}

static const struct evmc_host_interface HOST_INTERFACE = {
    .account_exists = host_account_exists,
    .get_storage = host_get_storage,
    .set_storage = host_set_storage,
    .get_balance = host_get_balance,
    .get_code_size = host_get_code_size,
    .get_code_hash = host_get_code_hash,
    .copy_code = host_copy_code,
    .selfdestruct = host_selfdestruct,
    .call = host_call,
    .get_tx_context = host_get_tx_context,
    .get_block_hash = host_get_block_hash,
    .emit_log = host_emit_log,
    .access_account = host_access_account,
    .access_storage = host_access_storage,
    .get_transient_storage = host_get_transient_storage,
    .set_transient_storage = host_set_transient_storage,
};

static int execute_contract(evmdb_evm_t *evm, evmdb_state_t *state,
                            const evmdb_block_context_t *block_ctx,
                            const evmdb_address_t *origin,
                            const evmdb_address_t *sender,
                            const evmdb_address_t *to,
                            const evmdb_bytes_t *data,
                            const evmdb_bytes32_t *value, uint64_t gas,
                            uint32_t flags, bool commit_state,
                            evmdb_exec_result_t *result) {
    evmdb_bytes_t code = {0};
    evm_exec_context_t exec_ctx = {0};
    struct evmc_message msg = {0};
    struct evmc_result exec_result;
    evmdb_block_context_t default_block_ctx = {0};
    enum evmc_status_code status;
    uint64_t gas_left = 0;

    evmdb_state_get_code(state, to, &code);
    if (code.len == 0) {
        result->success = true;
        result->gas_used = 0;
        result->output.data = NULL;
        result->output.len = 0;
        result->logs = NULL;
        result->log_count = 0;
        result->error[0] = '\0';
        return 0;
    }

    exec_ctx.state = state;
    exec_ctx.block_ctx = block_ctx ? *block_ctx : default_block_ctx;
    exec_ctx.tx_origin = *origin;
    bytes32_from_uint64(exec_ctx.block_ctx.base_fee, &exec_ctx.tx_gas_price);

    msg.kind = EVMC_CALL;
    msg.flags = flags;
    msg.depth = 0;
    msg.gas = (int64_t)gas;
    address_to_evmc(to, &msg.recipient);
    address_to_evmc(sender, &msg.sender);
    msg.input_data = data ? data->data : NULL;
    msg.input_size = data ? data->len : 0;
    bytes32_to_evmc(value, &msg.value);
    address_to_evmc(to, &msg.code_address);

    exec_result = evmc_execute(evm->vm, &HOST_INTERFACE,
                               (struct evmc_host_context *)&exec_ctx,
                               EVMC_CANCUN, &msg, code.data, code.len);
    free(code.data);

    status = exec_result.status_code;
    if (status < 0) {
        snprintf(result->error, sizeof(result->error), "%s",
                 evmc_status_code_to_string(status));
        evmc_release_result(&exec_result);
        exec_context_destroy(&exec_ctx);
        return -1;
    }

    if (exec_result.gas_left > 0) {
        gas_left = (uint64_t)exec_result.gas_left;
    }

    result->success = status == EVMC_SUCCESS;
    result->gas_used = gas - gas_left;
    result->logs = NULL;
    result->log_count = 0;
    result->error[0] = '\0';
    copy_output(result, exec_result.output_data, exec_result.output_size);

    if (status != EVMC_SUCCESS) {
        snprintf(result->error, sizeof(result->error), "%s",
                 evmc_status_code_to_string(status));
    } else if (commit_state && exec_context_commit(&exec_ctx) != 0) {
        evmc_release_result(&exec_result);
        exec_context_destroy(&exec_ctx);
        snprintf(result->error, sizeof(result->error),
                 "failed to commit state changes");
        free(result->output.data);
        result->output.data = NULL;
        result->output.len = 0;
        return -1;
    }

    evmc_release_result(&exec_result);
    exec_context_destroy(&exec_ctx);
    return 0;
}

int evmdb_evm_compute_create_address(const evmdb_address_t *sender,
                                     uint64_t nonce,
                                     evmdb_address_t *out) {
    uint8_t payload[32];
    uint8_t encoded[64];
    evmdb_hash_t hash;
    size_t payload_len = 0;
    size_t encoded_len = 0;

    if (!sender || !out) {
        return -1;
    }

    payload_len += rlp_encode_bytes(sender->bytes, sizeof(sender->bytes),
                                    payload + payload_len);
    payload_len += rlp_encode_uint64(nonce, payload + payload_len);
    encoded_len = rlp_encode_list(payload, payload_len, encoded);

    evmdb_keccak256(encoded, encoded_len, &hash);
    memcpy(out->bytes, hash.bytes + 12, sizeof(out->bytes));
    return 0;
}

evmdb_evm_t *evmdb_evm_create(const char *evmone_path) {
    evmdb_evm_t *evm = calloc(1, sizeof(*evm));

    if (!evm) {
        return NULL;
    }

    (void)evmone_path;

    evm->vm = evmc_create_evmone();
    if (!evm->vm) {
        free(evm);
        return NULL;
    }

    evm->initialized = 1;
    LOG_INFO("EVM executor initialized with evmone");
    return evm;
}

void evmdb_evm_destroy(evmdb_evm_t *evm) {
    if (!evm) return;
    if (evm->vm) {
        evmc_destroy(evm->vm);
    }
    free(evm);
}

int evmdb_evm_execute_tx(evmdb_evm_t *evm, evmdb_state_t *state,
                         const evmdb_tx_t *tx,
                         const evmdb_block_context_t *block_ctx,
                         evmdb_exec_result_t *result) {
    evmdb_bytes32_t zero_value = {{0}};
    uint64_t current_nonce = 0;
    uint64_t intrinsic_gas = 21000;

    if (!evm || !evm->initialized) {
        snprintf(result->error, sizeof(result->error), "EVM not initialized");
        return -1;
    }

    if (block_ctx && tx->chain_id != 0 && tx->chain_id != block_ctx->chain_id) {
        snprintf(result->error, sizeof(result->error),
                 "chain id mismatch: expected %llu, got %llu",
                 (unsigned long long)block_ctx->chain_id,
                 (unsigned long long)tx->chain_id);
        return -1;
    }

    evmdb_state_get_nonce(state, &tx->from, &current_nonce);
    if (tx->nonce != current_nonce) {
        snprintf(result->error, sizeof(result->error),
                 "nonce mismatch: expected %llu, got %llu",
                 (unsigned long long)current_nonce,
                 (unsigned long long)tx->nonce);
        return -1;
    }

    for (size_t i = 0; i < tx->data.len; i++) {
        intrinsic_gas += (tx->data.data[i] == 0) ? 4 : 16;
    }

    if (intrinsic_gas > tx->gas_limit) {
        snprintf(result->error, sizeof(result->error), "intrinsic gas too low");
        return -1;
    }

    result->output.data = NULL;
    result->output.len = 0;
    result->logs = NULL;
    result->log_count = 0;

    if (tx->to_is_null) {
        evmdb_address_t contract_address;

        if (evmdb_evm_compute_create_address(&tx->from, current_nonce,
                                             &contract_address) != 0) {
            snprintf(result->error, sizeof(result->error),
                     "failed to derive contract address");
            return -1;
        }

        evmdb_state_set_code(state, &contract_address, tx->data.data,
                             tx->data.len);
        result->success = true;
        result->gas_used = intrinsic_gas;
        result->error[0] = '\0';
    } else {
        int rc = execute_contract(evm, state, block_ctx, &tx->from, &tx->from,
                                  &tx->to, &tx->data, &zero_value,
                                  tx->gas_limit - intrinsic_gas, 0, true,
                                  result);
        if (rc != 0) {
            return rc;
        }
        result->gas_used += intrinsic_gas;
    }

    evmdb_state_set_nonce(state, &tx->from, current_nonce + 1);
    return 0;
}

int evmdb_evm_call(evmdb_evm_t *evm, evmdb_state_t *state,
                   const evmdb_address_t *from, const evmdb_address_t *to,
                   const evmdb_bytes_t *data,
                   const evmdb_block_context_t *block_ctx, uint64_t gas,
                   evmdb_exec_result_t *result) {
    evmdb_bytes32_t zero_value = {{0}};

    if (!evm || !evm->initialized) {
        snprintf(result->error, sizeof(result->error), "EVM not initialized");
        return -1;
    }

    return execute_contract(evm, state, block_ctx, from, from, to, data,
                            &zero_value, gas, EVMC_STATIC, false, result);
}
