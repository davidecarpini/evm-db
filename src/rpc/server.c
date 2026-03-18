#include "evmdb/rpc.h"
#include "evmdb/log.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#define RPC_MAX_REQUEST_SIZE (1024 * 1024) /* 1MB */
#define RPC_BACKLOG 128

struct evmdb_rpc {
    evmdb_state_t *state;
    evmdb_evm_t   *evm;
    uint64_t       chain_id;
    uint64_t       gas_limit;
    uint64_t       base_fee;
    int            server_fd;
    volatile int   running;
};

/* Defined in handlers.c */
int evmdb_rpc_handle_request(evmdb_rpc_t *rpc, const char *request,
                             size_t req_len, char **response, size_t *resp_len);

evmdb_rpc_t *evmdb_rpc_create(evmdb_state_t *state, evmdb_evm_t *evm,
                               uint64_t chain_id, uint64_t gas_limit,
                               uint64_t base_fee) {
    evmdb_rpc_t *rpc = calloc(1, sizeof(*rpc));
    if (!rpc) return NULL;

    rpc->state = state;
    rpc->evm = evm;
    rpc->chain_id = chain_id;
    rpc->gas_limit = gas_limit;
    rpc->base_fee = base_fee;
    rpc->server_fd = -1;
    rpc->running = 0;

    return rpc;
}

void evmdb_rpc_destroy(evmdb_rpc_t *rpc) {
    if (!rpc) return;
    if (rpc->server_fd >= 0) {
        close(rpc->server_fd);
    }
    free(rpc);
}

/*
 * Handle a single client connection.
 * Reads an HTTP request, extracts the JSON body, processes it,
 * and sends an HTTP response.
 */
static void handle_client(evmdb_rpc_t *rpc, int client_fd) {
    char *buf = malloc(RPC_MAX_REQUEST_SIZE);
    if (!buf) {
        close(client_fd);
        return;
    }

    /* Read request */
    ssize_t n = read(client_fd, buf, RPC_MAX_REQUEST_SIZE - 1);
    if (n <= 0) {
        free(buf);
        close(client_fd);
        return;
    }
    buf[n] = '\0';

    /* Handle CORS preflight (OPTIONS request) */
    if (strncmp(buf, "OPTIONS", 7) == 0) {
        const char *cors_response =
            "HTTP/1.1 204 No Content\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: POST, GET, OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
            "Access-Control-Max-Age: 86400\r\n"
            "Content-Length: 0\r\n"
            "\r\n";
        write(client_fd, cors_response, strlen(cors_response));
        free(buf);
        close(client_fd);
        return;
    }

    /* Handle Content-Length for proper body reading */
    const char *cl_header = strstr(buf, "Content-Length:");
    if (!cl_header) cl_header = strstr(buf, "content-length:");
    size_t content_length = 0;
    if (cl_header) {
        content_length = (size_t)atol(cl_header + 15);
    }

    /* Find JSON body (after \r\n\r\n) */
    const char *body = strstr(buf, "\r\n\r\n");
    if (body) {
        body += 4;
    } else {
        body = buf; /* Maybe raw JSON without HTTP headers */
    }

    /* Read remaining body if we haven't received it all */
    size_t body_received = (size_t)(buf + n - body);
    if (content_length > body_received && content_length < RPC_MAX_REQUEST_SIZE) {
        size_t remaining = content_length - body_received;
        ssize_t extra = read(client_fd, buf + n, remaining);
        if (extra > 0) {
            buf[n + extra] = '\0';
        }
    }

    size_t body_len = strlen(body);

    /* Process JSON-RPC request */
    char *response = NULL;
    size_t resp_len = 0;
    evmdb_rpc_handle_request(rpc, body, body_len, &response, &resp_len);

    /* Send HTTP response */
    if (response) {
        char header[512];
        int hlen = snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %zu\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: POST, GET, OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
            "Connection: close\r\n"
            "\r\n", resp_len);

        write(client_fd, header, (size_t)hlen);
        write(client_fd, response, resp_len);
        free(response);
    }

    free(buf);
    close(client_fd);
}

typedef struct { evmdb_rpc_t *rpc; int fd; } client_args_t;

static void *client_thread_fn(void *arg) {
    client_args_t *a = (client_args_t *)arg;
    handle_client(a->rpc, a->fd);
    free(a);
    return NULL;
}

int evmdb_rpc_listen(evmdb_rpc_t *rpc, const char *host, int port) {
    rpc->server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (rpc->server_fd < 0) {
        LOG_ERROR("socket: %s", strerror(errno));
        return -1;
    }

    int opt = 1;
    setsockopt(rpc->server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons((uint16_t)port),
    };
    inet_pton(AF_INET, host, &addr.sin_addr);

    if (bind(rpc->server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("bind: %s", strerror(errno));
        close(rpc->server_fd);
        return -1;
    }

    if (listen(rpc->server_fd, RPC_BACKLOG) < 0) {
        LOG_ERROR("listen: %s", strerror(errno));
        close(rpc->server_fd);
        return -1;
    }

    LOG_INFO("JSON-RPC server listening on %s:%d", host, port);
    rpc->running = 1;

    while (rpc->running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(rpc->server_fd,
                               (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (rpc->running) {
                LOG_WARN("accept: %s", strerror(errno));
            }
            continue;
        }

        /* Thread-per-connection for concurrent Metamask requests */
        typedef struct { evmdb_rpc_t *rpc; int fd; } client_args_t;
        client_args_t *args = malloc(sizeof(client_args_t));
        if (!args) { close(client_fd); continue; }
        args->rpc = rpc;
        args->fd = client_fd;

        pthread_t t;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&t, &attr, client_thread_fn, args) != 0) {
            free(args);
            close(client_fd);
        }
        pthread_attr_destroy(&attr);
    }

    return 0;
}

void evmdb_rpc_stop(evmdb_rpc_t *rpc) {
    rpc->running = 0;
    if (rpc->server_fd >= 0) {
        shutdown(rpc->server_fd, SHUT_RDWR);
    }
}
