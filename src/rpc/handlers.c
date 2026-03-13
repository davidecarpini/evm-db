#include "evmdb/rpc.h"
#include "evmdb/state.h"
#include "evmdb/evm.h"
#include "evmdb/hex.h"
#include "evmdb/log.h"
#include "evmdb/types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Forward declaration from json.c */
int evmdb_json_parse_method(const char *json, char *method, size_t method_len,
                            char *id_buf, size_t id_len);
int evmdb_json_parse_params(const char *json, char params[][256],
                            int max_params);

/*
 * JSON-RPC method handlers.
 *
 * Each handler reads from state/evm, formats a JSON result string,
 * and returns it. The caller wraps it in the JSON-RPC response envelope.
 */

/* Access rpc internals — defined in server.c, we access via struct */
struct evmdb_rpc {
    evmdb_state_t *state;
    evmdb_evm_t   *evm;
    uint64_t       chain_id;
    int            server_fd;
    volatile int   running;
};

/* ---- Helper: build JSON-RPC response ------------------------------------ */

static char *make_response(const char *id, const char *result) {
    size_t len = strlen(id) + strlen(result) + 64;
    char *buf = malloc(len);
    if (!buf) return NULL;
    snprintf(buf, len,
        "{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":%s}", id, result);
    return buf;
}

static char *make_error(const char *id, int code, const char *message) {
    size_t len = strlen(id) + strlen(message) + 128;
    char *buf = malloc(len);
    if (!buf) return NULL;
    snprintf(buf, len,
        "{\"jsonrpc\":\"2.0\",\"id\":%s,\"error\":{\"code\":%d,\"message\":\"%s\"}}",
        id, code, message);
    return buf;
}

/* ---- Method handlers ---------------------------------------------------- */

static char *handle_chain_id(evmdb_rpc_t *rpc, const char *id) {
    char hex[32];
    evmdb_hex_from_uint64(rpc->chain_id, hex, sizeof(hex));

    char result[64];
    snprintf(result, sizeof(result), "\"%s\"", hex);
    return make_response(id, result);
}

static char *handle_block_number(evmdb_rpc_t *rpc, const char *id) {
    uint64_t num = 0;
    evmdb_state_get_block_number(rpc->state, &num);

    char hex[32];
    evmdb_hex_from_uint64(num, hex, sizeof(hex));

    char result[64];
    snprintf(result, sizeof(result), "\"%s\"", hex);
    return make_response(id, result);
}

static char *handle_gas_price(evmdb_rpc_t *rpc, const char *id) {
    (void)rpc;
    /* Return a low default gas price: 1 gwei */
    return make_response(id, "\"0x3b9aca00\"");
}

static char *handle_get_balance(evmdb_rpc_t *rpc, const char *id,
                                char params[][256], int param_count) {
    if (param_count < 1) {
        return make_error(id, -32602, "missing address parameter");
    }

    evmdb_address_t addr;
    if (evmdb_hex_decode(params[0], addr.bytes, 20) != 20) {
        return make_error(id, -32602, "invalid address");
    }

    evmdb_bytes32_t balance;
    evmdb_state_get_balance(rpc->state, &addr, &balance);

    char hex[67];
    evmdb_hex_encode(balance.bytes, 32, hex, sizeof(hex));

    char result[128];
    snprintf(result, sizeof(result), "\"%s\"", hex);
    return make_response(id, result);
}

static char *handle_get_transaction_count(evmdb_rpc_t *rpc, const char *id,
                                          char params[][256],
                                          int param_count) {
    if (param_count < 1) {
        return make_error(id, -32602, "missing address parameter");
    }

    evmdb_address_t addr;
    if (evmdb_hex_decode(params[0], addr.bytes, 20) != 20) {
        return make_error(id, -32602, "invalid address");
    }

    uint64_t nonce = 0;
    evmdb_state_get_nonce(rpc->state, &addr, &nonce);

    char hex[32];
    evmdb_hex_from_uint64(nonce, hex, sizeof(hex));

    char result[64];
    snprintf(result, sizeof(result), "\"%s\"", hex);
    return make_response(id, result);
}

static char *handle_get_code(evmdb_rpc_t *rpc, const char *id,
                             char params[][256], int param_count) {
    if (param_count < 1) {
        return make_error(id, -32602, "missing address parameter");
    }

    evmdb_address_t addr;
    if (evmdb_hex_decode(params[0], addr.bytes, 20) != 20) {
        return make_error(id, -32602, "invalid address");
    }

    evmdb_bytes_t code;
    evmdb_state_get_code(rpc->state, &addr, &code);

    if (code.len == 0) {
        return make_response(id, "\"0x\"");
    }

    size_t hex_len = code.len * 2 + 3;
    char *hex = malloc(hex_len);
    if (!hex) {
        free(code.data);
        return make_error(id, -32603, "internal error");
    }
    evmdb_hex_encode(code.data, code.len, hex, hex_len);
    free(code.data);

    size_t result_len = hex_len + 4;
    char *result = malloc(result_len);
    snprintf(result, result_len, "\"%s\"", hex);
    free(hex);

    char *resp = make_response(id, result);
    free(result);
    return resp;
}

static char *handle_send_raw_transaction(evmdb_rpc_t *rpc, const char *id,
                                         char params[][256],
                                         int param_count) {
    if (param_count < 1) {
        return make_error(id, -32602, "missing raw transaction parameter");
    }

    /* Decode hex to bytes */
    size_t hex_len = strlen(params[0]);
    size_t max_bytes = hex_len / 2;
    uint8_t *raw = malloc(max_bytes);
    if (!raw) {
        return make_error(id, -32603, "internal error");
    }

    int byte_len = evmdb_hex_decode(params[0], raw, max_bytes);
    if (byte_len <= 0) {
        free(raw);
        return make_error(id, -32602, "invalid hex data");
    }

    /* Push to Redis tx queue */
    evmdb_state_push_tx(rpc->state, raw, (size_t)byte_len);

    /*
     * TODO: Compute and return the actual tx hash.
     * For now, return a placeholder.
     */
    free(raw);
    return make_response(id,
        "\"0x0000000000000000000000000000000000000000000000000000000000000000\"");
}

static char *handle_net_version(evmdb_rpc_t *rpc, const char *id) {
    char result[32];
    snprintf(result, sizeof(result), "\"%llu\"",
             (unsigned long long)rpc->chain_id);
    return make_response(id, result);
}

static char *handle_web3_client_version(evmdb_rpc_t *rpc, const char *id) {
    (void)rpc;
    return make_response(id, "\"evmdb/0.1.0\"");
}

/* ---- Dispatcher --------------------------------------------------------- */

int evmdb_rpc_handle_request(evmdb_rpc_t *rpc, const char *request,
                             size_t req_len, char **response,
                             size_t *resp_len) {
    (void)req_len;

    char method[64] = {0};
    char id_buf[32] = "1";
    evmdb_json_parse_method(request, method, sizeof(method),
                            id_buf, sizeof(id_buf));

    char params[8][256] = {{0}};
    int param_count = evmdb_json_parse_params(request, params, 8);

    char *result = NULL;

    LOG_DEBUG("RPC: %s (id=%s, %d params)", method, id_buf, param_count);

    if (strcmp(method, "eth_chainId") == 0) {
        result = handle_chain_id(rpc, id_buf);
    } else if (strcmp(method, "eth_blockNumber") == 0) {
        result = handle_block_number(rpc, id_buf);
    } else if (strcmp(method, "eth_gasPrice") == 0) {
        result = handle_gas_price(rpc, id_buf);
    } else if (strcmp(method, "eth_getBalance") == 0) {
        result = handle_get_balance(rpc, id_buf, params, param_count);
    } else if (strcmp(method, "eth_getTransactionCount") == 0) {
        result = handle_get_transaction_count(rpc, id_buf, params,
                                              param_count);
    } else if (strcmp(method, "eth_getCode") == 0) {
        result = handle_get_code(rpc, id_buf, params, param_count);
    } else if (strcmp(method, "eth_sendRawTransaction") == 0) {
        result = handle_send_raw_transaction(rpc, id_buf, params,
                                             param_count);
    } else if (strcmp(method, "net_version") == 0) {
        result = handle_net_version(rpc, id_buf);
    } else if (strcmp(method, "web3_clientVersion") == 0) {
        result = handle_web3_client_version(rpc, id_buf);
    } else if (strcmp(method, "eth_call") == 0) {
        /* TODO: implement eth_call with EVM execution */
        result = make_response(id_buf, "\"0x\"");
    } else if (strcmp(method, "eth_estimateGas") == 0) {
        /* TODO: implement proper gas estimation */
        result = make_response(id_buf, "\"0x5208\""); /* 21000 */
    } else {
        result = make_error(id_buf, -32601,
                           "method not found");
        LOG_WARN("unsupported RPC method: %s", method);
    }

    if (result) {
        *response = result;
        *resp_len = strlen(result);
    } else {
        *response = NULL;
        *resp_len = 0;
    }

    return 0;
}
