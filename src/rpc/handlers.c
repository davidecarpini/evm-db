#include "evmdb/rpc.h"
#include "evmdb/state.h"
#include "evmdb/evm.h"
#include "evmdb/block.h"
#include "evmdb/hex.h"
#include "evmdb/log.h"
#include "evmdb/types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int evmdb_json_parse_method(const char *json, char *method, size_t method_len,
                            char *id_buf, size_t id_len);
int evmdb_json_parse_params(const char *json, char params[][256],
                            int max_params);

struct evmdb_rpc {
    evmdb_state_t *state;
    evmdb_evm_t   *evm;
    uint64_t       chain_id;
    uint64_t       gas_limit;
    uint64_t       base_fee;
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

/* ---- Synthetic block context -------------------------------------------- */

static evmdb_block_context_t make_block_context(evmdb_rpc_t *rpc) {
    uint64_t block_num = 0;
    evmdb_state_get_block_number(rpc->state, &block_num);

    evmdb_block_context_t ctx = {
        .number    = block_num,
        .timestamp = (uint64_t)time(NULL),
        .coinbase  = {{0}},
        .gas_limit = rpc->gas_limit,
        .base_fee  = rpc->base_fee,
        .chain_id  = rpc->chain_id,
    };
    return ctx;
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
    char hex[32];
    evmdb_hex_from_uint64(rpc->base_fee, hex, sizeof(hex));

    char result[64];
    snprintf(result, sizeof(result), "\"%s\"", hex);
    return make_response(id, result);
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

    const char *p = hex + 2;
    while (*p == '0' && *(p + 1) != '\0') p++;
    char trimmed[67];
    snprintf(trimmed, sizeof(trimmed), "0x%s", p);

    char result[128];
    snprintf(result, sizeof(result), "\"%s\"", trimmed);
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

static char *handle_get_block_by_number(evmdb_rpc_t *rpc, const char *id,
                                        char params[][256],
                                        int param_count) {
    (void)params;
    (void)param_count;

    evmdb_block_context_t ctx = make_block_context(rpc);

    char num_hex[32], timestamp_hex[32], gas_limit_hex[32], base_fee_hex[32];
    evmdb_hex_from_uint64(ctx.number, num_hex, sizeof(num_hex));
    evmdb_hex_from_uint64(ctx.timestamp, timestamp_hex, sizeof(timestamp_hex));
    evmdb_hex_from_uint64(ctx.gas_limit, gas_limit_hex, sizeof(gas_limit_hex));
    evmdb_hex_from_uint64(ctx.base_fee, base_fee_hex, sizeof(base_fee_hex));

    size_t buf_size = 2048;
    char *buf = malloc(buf_size);
    if (!buf) return make_error(id, -32603, "internal error");

    snprintf(buf, buf_size,
        "{"
        "\"number\":\"%s\","
        "\"hash\":\"0x0000000000000000000000000000000000000000000000000000000000000000\","
        "\"parentHash\":\"0x0000000000000000000000000000000000000000000000000000000000000000\","
        "\"nonce\":\"0x0000000000000000\","
        "\"sha3Uncles\":\"0x1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347\","
        "\"logsBloom\":\"0x00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000\","
        "\"transactionsRoot\":\"0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421\","
        "\"stateRoot\":\"0x0000000000000000000000000000000000000000000000000000000000000000\","
        "\"receiptsRoot\":\"0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421\","
        "\"miner\":\"0x0000000000000000000000000000000000000000\","
        "\"difficulty\":\"0x0\","
        "\"totalDifficulty\":\"0x0\","
        "\"extraData\":\"0x\","
        "\"size\":\"0x0\","
        "\"gasLimit\":\"%s\","
        "\"gasUsed\":\"0x0\","
        "\"timestamp\":\"%s\","
        "\"transactions\":[],"
        "\"uncles\":[],"
        "\"baseFeePerGas\":\"%s\""
        "}",
        num_hex, gas_limit_hex, timestamp_hex, base_fee_hex);

    char *resp = make_response(id, buf);
    free(buf);
    return resp;
}

static char *handle_fee_history(evmdb_rpc_t *rpc, const char *id) {
    uint64_t block_num = 0;
    evmdb_state_get_block_number(rpc->state, &block_num);

    char num_hex[32], base_fee_hex[32];
    evmdb_hex_from_uint64(block_num, num_hex, sizeof(num_hex));
    evmdb_hex_from_uint64(rpc->base_fee, base_fee_hex, sizeof(base_fee_hex));

    size_t buf_size = 512;
    char *buf = malloc(buf_size);
    if (!buf) return make_error(id, -32603, "internal error");

    snprintf(buf, buf_size,
        "{"
        "\"oldestBlock\":\"%s\","
        "\"baseFeePerGas\":[\"%s\",\"%s\"],"
        "\"gasUsedRatio\":[0.0],"
        "\"reward\":[[\"%s\"]]"
        "}",
        num_hex, base_fee_hex, base_fee_hex, base_fee_hex);

    char *resp = make_response(id, buf);
    free(buf);
    return resp;
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

    /* Decode transaction */
    evmdb_tx_t tx = {0};
    int rc = evmdb_tx_decode(raw, (size_t)byte_len, &tx);
    free(raw);
    if (rc != 0) {
        return make_error(id, -32602, "failed to decode transaction");
    }

    /* Recover sender */
    rc = evmdb_tx_recover_sender(&tx);
    if (rc != 0) {
        evmdb_tx_free(&tx);
        return make_error(id, -32602, "failed to recover sender");
    }

    /* Build synthetic block context and execute */
    evmdb_block_context_t block_ctx = make_block_context(rpc);
    evmdb_exec_result_t result = {0};
    rc = evmdb_evm_execute_tx(rpc->evm, rpc->state, &tx, &block_ctx, &result);

    if (rc != 0) {
        char *resp = make_error(id, -32603, result.error);
        evmdb_tx_free(&tx);
        return resp;
    }

    /* Increment block number (each tx = one block) */
    evmdb_state_set_block_number(rpc->state, block_ctx.number + 1);

    LOG_INFO("tx executed: %s (gas: %lu)",
             result.success ? "OK" : "REVERT",
             (unsigned long)result.gas_used);

    /* Return tx hash */
    char hash_hex[67];
    evmdb_hex_encode(tx.hash.bytes, 32, hash_hex, sizeof(hash_hex));

    char result_str[128];
    snprintf(result_str, sizeof(result_str), "\"%s\"", hash_hex);

    char *resp = make_response(id, result_str);
    evmdb_tx_free(&tx);
    free(result.output.data);
    return resp;
}

static char *handle_net_version(evmdb_rpc_t *rpc, const char *id) {
    char result[32];
    snprintf(result, sizeof(result), "\"%llu\"",
             (unsigned long long)rpc->chain_id);
    return make_response(id, result);
}

static char *handle_web3_client_version(evmdb_rpc_t *rpc, const char *id) {
    (void)rpc;
    return make_response(id, "\"evmdb/0.2.0\"");
}

/* ---- Single request dispatcher ------------------------------------------ */

static char *dispatch_single(evmdb_rpc_t *rpc, const char *request) {
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
        result = make_response(id_buf, "\"0x\"");
    } else if (strcmp(method, "eth_estimateGas") == 0) {
        result = make_response(id_buf, "\"0x5208\"");
    } else if (strcmp(method, "eth_getBlockByNumber") == 0 ||
               strcmp(method, "eth_getBlockByHash") == 0) {
        result = handle_get_block_by_number(rpc, id_buf, params, param_count);
    } else if (strcmp(method, "eth_getTransactionByHash") == 0) {
        result = make_response(id_buf, "null");
    } else if (strcmp(method, "eth_getTransactionReceipt") == 0) {
        result = make_response(id_buf, "null");
    } else if (strcmp(method, "eth_getLogs") == 0) {
        result = make_response(id_buf, "[]");
    } else if (strcmp(method, "eth_feeHistory") == 0) {
        result = handle_fee_history(rpc, id_buf);
    } else if (strcmp(method, "eth_maxPriorityFeePerGas") == 0) {
        char hex[32];
        evmdb_hex_from_uint64(rpc->base_fee, hex, sizeof(hex));
        char buf[64];
        snprintf(buf, sizeof(buf), "\"%s\"", hex);
        result = make_response(id_buf, buf);
    } else if (strcmp(method, "eth_accounts") == 0) {
        result = make_response(id_buf, "[]");
    } else if (strcmp(method, "eth_syncing") == 0) {
        result = make_response(id_buf, "false");
    } else if (strcmp(method, "eth_mining") == 0) {
        result = make_response(id_buf, "false");
    } else if (strcmp(method, "eth_hashrate") == 0) {
        result = make_response(id_buf, "\"0x0\"");
    } else if (strcmp(method, "eth_getStorageAt") == 0) {
        result = make_response(id_buf,
            "\"0x0000000000000000000000000000000000000000000000000000000000000000\"");
    } else if (strcmp(method, "eth_getBlockTransactionCountByNumber") == 0) {
        result = make_response(id_buf, "\"0x0\"");
    } else if (strcmp(method, "eth_protocolVersion") == 0) {
        result = make_response(id_buf, "\"0x42\"");
    } else if (strcmp(method, "eth_subscribe") == 0) {
        result = make_error(id_buf, -32601, "subscriptions not supported over HTTP");
    } else {
        result = make_error(id_buf, -32601, "method not found");
        LOG_WARN("unsupported RPC method: %s", method);
    }

    return result;
}

/* ---- Batch-aware dispatcher --------------------------------------------- */

int evmdb_rpc_handle_request(evmdb_rpc_t *rpc, const char *request,
                             size_t req_len, char **response,
                             size_t *resp_len) {
    (void)req_len;

    while (*request == ' ' || *request == '\t' || *request == '\n'
           || *request == '\r') {
        request++;
    }

    if (*request == '[') {
        size_t buf_cap = 4096;
        size_t buf_len = 0;
        char *buf = malloc(buf_cap);
        if (!buf) { *response = NULL; *resp_len = 0; return -1; }
        buf[buf_len++] = '[';

        const char *p = request + 1;
        int first = 1;

        while (*p) {
            while (*p == ' ' || *p == ',' || *p == '\t' || *p == '\n'
                   || *p == '\r') {
                p++;
            }
            if (*p == ']' || *p == '\0') break;

            if (*p == '{') {
                const char *start = p;
                int depth = 0;
                do {
                    if (*p == '{') depth++;
                    else if (*p == '}') depth--;
                    p++;
                } while (*p && depth > 0);

                size_t item_len = (size_t)(p - start);
                char *item = malloc(item_len + 1);
                if (!item) continue;
                memcpy(item, start, item_len);
                item[item_len] = '\0';

                char *single_resp = dispatch_single(rpc, item);
                free(item);

                if (single_resp) {
                    size_t sr_len = strlen(single_resp);
                    while (buf_len + sr_len + 4 > buf_cap) {
                        buf_cap *= 2;
                        buf = realloc(buf, buf_cap);
                        if (!buf) { *response = NULL; *resp_len = 0; return -1; }
                    }
                    if (!first) buf[buf_len++] = ',';
                    memcpy(buf + buf_len, single_resp, sr_len);
                    buf_len += sr_len;
                    free(single_resp);
                    first = 0;
                }
            } else {
                p++;
            }
        }

        if (buf_len + 2 > buf_cap) {
            buf = realloc(buf, buf_cap + 2);
        }
        buf[buf_len++] = ']';
        buf[buf_len] = '\0';

        *response = buf;
        *resp_len = buf_len;
        return 0;
    }

    char *result = dispatch_single(rpc, request);
    if (result) {
        *response = result;
        *resp_len = strlen(result);
    } else {
        *response = NULL;
        *resp_len = 0;
    }
    return 0;
}
