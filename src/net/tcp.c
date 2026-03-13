#include "evmdb/log.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

/*
 * TCP utilities for low-latency networking.
 *
 * For the MVP, the RPC server uses basic POSIX sockets.
 * For production, this would be replaced with:
 * - kqueue (macOS) or io_uring (Linux) for async I/O
 * - Connection pooling
 * - HTTP/2 or raw TCP for lower overhead
 */

/* Configure a socket for low-latency operation. */
int evmdb_tcp_set_nodelay(int fd) {
    int flag = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) < 0) {
        LOG_WARN("TCP_NODELAY: %s", strerror(errno));
        return -1;
    }
    return 0;
}

/* Set socket receive/send buffer sizes. */
int evmdb_tcp_set_buffers(int fd, int recv_size, int send_size) {
    if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &recv_size,
                   sizeof(recv_size)) < 0) {
        LOG_WARN("SO_RCVBUF: %s", strerror(errno));
    }
    if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &send_size,
                   sizeof(send_size)) < 0) {
        LOG_WARN("SO_SNDBUF: %s", strerror(errno));
    }
    return 0;
}
