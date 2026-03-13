#include "evmdb/rpc.h"
#include "evmdb/log.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
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
    int            server_fd;
    volatile int   running;
};

/* Defined in handlers.c */
int evmdb_rpc_handle_request(evmdb_rpc_t *rpc, const char *request,
                             size_t req_len, char **response, size_t *resp_len);

evmdb_rpc_t *evmdb_rpc_create(evmdb_state_t *state, evmdb_evm_t *evm,
                               uint64_t chain_id) {
    evmdb_rpc_t *rpc = calloc(1, sizeof(*rpc));
    if (!rpc) return NULL;

    rpc->state = state;
    rpc->evm = evm;
    rpc->chain_id = chain_id;
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

    /* Find JSON body (after \r\n\r\n) */
    const char *body = strstr(buf, "\r\n\r\n");
    if (body) {
        body += 4;
    } else {
        body = buf; /* Maybe raw JSON without HTTP headers */
    }
    size_t body_len = strlen(body);

    /* Process JSON-RPC request */
    char *response = NULL;
    size_t resp_len = 0;
    evmdb_rpc_handle_request(rpc, body, body_len, &response, &resp_len);

    /* Send HTTP response */
    if (response) {
        char header[256];
        int hlen = snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %zu\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: POST, OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type\r\n"
            "\r\n", resp_len);

        write(client_fd, header, (size_t)hlen);
        write(client_fd, response, resp_len);
        free(response);
    }

    free(buf);
    close(client_fd);
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

        /* TODO: In production, handle in thread pool or use epoll/kqueue.
           For MVP, handle synchronously. */
        handle_client(rpc, client_fd);
    }

    return 0;
}

void evmdb_rpc_stop(evmdb_rpc_t *rpc) {
    rpc->running = 0;
    if (rpc->server_fd >= 0) {
        shutdown(rpc->server_fd, SHUT_RDWR);
    }
}
