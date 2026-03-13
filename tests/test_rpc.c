#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Test the JSON parser directly */
extern int evmdb_json_parse_method(const char *json, char *method,
                                   size_t method_len, char *id_buf,
                                   size_t id_len);
extern int evmdb_json_parse_params(const char *json, char params[][256],
                                   int max_params);

static void test_parse_method(void) {
    const char *json = "{\"jsonrpc\":\"2.0\",\"method\":\"eth_blockNumber\","
                       "\"params\":[],\"id\":42}";

    char method[64];
    char id[32];
    evmdb_json_parse_method(json, method, sizeof(method), id, sizeof(id));

    assert(strcmp(method, "eth_blockNumber") == 0);
    assert(strcmp(id, "42") == 0);
}

static void test_parse_params(void) {
    const char *json = "{\"jsonrpc\":\"2.0\",\"method\":\"eth_getBalance\","
                       "\"params\":[\"0xaabbccdd\",\"latest\"],\"id\":1}";

    char params[8][256];
    int count = evmdb_json_parse_params(json, params, 8);

    assert(count == 2);
    assert(strcmp(params[0], "0xaabbccdd") == 0);
    assert(strcmp(params[1], "latest") == 0);
}

static void test_parse_no_params(void) {
    const char *json = "{\"jsonrpc\":\"2.0\",\"method\":\"eth_chainId\","
                       "\"id\":1}";

    char params[8][256];
    int count = evmdb_json_parse_params(json, params, 8);
    assert(count == 0);
}

int main(void) {
    test_parse_method();
    test_parse_params();
    test_parse_no_params();

    printf("test_rpc: all tests passed\n");
    return 0;
}
