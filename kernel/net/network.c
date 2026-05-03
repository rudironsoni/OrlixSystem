/* IXLand - Network/Socket Syscalls
 * Canonical owner for socket syscalls:
 * - socket(), socketpair(), bind(), listen(), accept(), accept4()
 * - connect(), shutdown()
 * - send(), sendto(), recv(), recvfrom()
 * - setsockopt(), getsockopt()
 * - getsockname(), getpeername()
 * - sendmsg(), recvmsg()
 *
 * Linux-shaped canonical owner - iOS mediation as implementation detail
 */

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "../sync.h"

/* iOS Network framework includes */
#import <Network/Network.h>

/* ============================================================================
 * NETWORK STATE
 * ============================================================================ */

/* Socket table entry - maps virtual fds to real iOS sockets */
typedef struct {
    int used;
    int domain;
    int type;
    int protocol;
    nw_connection_t connection;
    nw_listener_t listener;
    struct sockaddr_storage local_addr;
    struct sockaddr_storage remote_addr;
    socklen_t local_addr_len;
    socklen_t remote_addr_len;
} socket_entry_t;

#define MAX_SOCKETS 256
static socket_entry_t socket_table[MAX_SOCKETS];
static kernel_mutex_t socket_table_lock = KERNEL_MUTEX_INITIALIZER;

/* Network initialization state */
static atomic_int network_initialized = 0;

/* ============================================================================
 * SOCKET TABLE MANAGEMENT
 * ============================================================================ */

static int socket_alloc(void) {
    kernel_mutex_lock(&socket_table_lock);
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (!socket_table[i].used) {
            socket_table[i].used = 1;
            kernel_mutex_unlock(&socket_table_lock);
            return i;
        }
    }
    kernel_mutex_unlock(&socket_table_lock);
    errno = EMFILE;
    return -1;
}

static void socket_free(int fd) {
    if (fd < 0 || fd >= MAX_SOCKETS)
        return;

    kernel_mutex_lock(&socket_table_lock);
    if (socket_table[fd].used) {
        /* Release iOS Network resources */
        if (socket_table[fd].connection) {
            nw_connection_cancel(socket_table[fd].connection);
            socket_table[fd].connection = NULL;
        }
        if (socket_table[fd].listener) {
            nw_listener_cancel(socket_table[fd].listener);
            socket_table[fd].listener = NULL;
        }
        memset(&socket_table[fd], 0, sizeof(socket_entry_t));
    }
    kernel_mutex_unlock(&socket_table_lock);
}

static socket_entry_t *socket_get(int fd) {
    if (fd < 0 || fd >= MAX_SOCKETS) {
        errno = EBADF;
        return NULL;
    }
    if (!socket_table[fd].used) {
        errno = EBADF;
        return NULL;
    }
    return &socket_table[fd];
}

/* ============================================================================
 * NETWORK INITIALIZATION
 * ============================================================================ */

static int network_init_impl(void) {
    if (atomic_load(&network_initialized)) {
        return 0;
    }

    /* Initialize socket table */
    memset(socket_table, 0, sizeof(socket_table));

    atomic_store(&network_initialized, 1);
    return 0;
}

static int network_deinit_impl(void) {
    if (!atomic_load(&network_initialized)) {
        return 0;
    }

    /* Close all sockets */
    kernel_mutex_lock(&socket_table_lock);
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (socket_table[i].used) {
            if (socket_table[i].connection) {
                nw_connection_cancel(socket_table[i].connection);
            }
            if (socket_table[i].listener) {
                nw_listener_cancel(socket_table[i].listener);
            }
        }
    }
    memset(socket_table, 0, sizeof(socket_table));
    kernel_mutex_unlock(&socket_table_lock);

    atomic_store(&network_initialized, 0);
    return 0;
}

/* ============================================================================
 * SOCKET CREATION
 * ============================================================================ */

static int socket_impl(int domain, int type, int protocol) {
    int fd = socket_alloc();
    if (fd < 0) {
        return -1;
    }

    socket_entry_t *sock = &socket_table[fd];
    sock->domain = domain;
    sock->type = type;
    sock->protocol = protocol;

    /* Map to iOS Network framework based on domain/type */
    switch (domain) {
    case AF_INET:
    case AF_INET6:
        /* TCP/UDP sockets - use BSD sockets for now */
        /* TODO: Implement using Network.framework for modern iOS */
        break;

    case AF_UNIX:
        /* Unix domain sockets - not supported on iOS */
        errno = EAFNOSUPPORT;
        socket_free(fd);
        return -1;

    default:
        errno = EAFNOSUPPORT;
        socket_free(fd);
        return -1;
    }

    return fd;
}

static int socketpair_impl(int domain, int type, int protocol, int sv[2]) {
    /* Socket pairs not supported on iOS (sandbox restriction) */
    (void)domain;
    (void)type;
    (void)protocol;
    (void)sv;
    errno = EOPNOTSUPP;
    return -1;
}

/* ============================================================================
 * CONNECTION MANAGEMENT
 * ============================================================================ */

static int connect_impl(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    socket_entry_t *sock = socket_get(sockfd);
    if (!sock) {
        return -1;
    }

    /* Validate address */
    if (!addr || addrlen == 0) {
        errno = EINVAL;
        return -1;
    }

    /* Copy remote address */
    if (addrlen > sizeof(sock->remote_addr)) {
        addrlen = sizeof(sock->remote_addr);
    }
    memcpy(&sock->remote_addr, addr, addrlen);
    sock->remote_addr_len = addrlen;

    /* For TCP sockets, create Network.framework connection */
    if (sock->type == SOCK_STREAM) {
        /* Create endpoint from address */
        nw_endpoint_t endpoint = NULL;

        if (addr->sa_family == AF_INET) {
            struct sockaddr_in *sin = (struct sockaddr_in *)addr;
            char addr_str[INET_ADDRSTRLEN];
            char port_str[6];
            inet_ntop(AF_INET, &sin->sin_addr, addr_str, sizeof(addr_str));
            snprintf(port_str, sizeof(port_str), "%u", ntohs(sin->sin_port));
            endpoint = nw_endpoint_create_host(addr_str, port_str);
        } else if (addr->sa_family == AF_INET6) {
            struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)addr;
            char addr_str[INET6_ADDRSTRLEN];
            char port_str[6];
            inet_ntop(AF_INET6, &sin6->sin6_addr, addr_str, sizeof(addr_str));
            snprintf(port_str, sizeof(port_str), "%u", ntohs(sin6->sin6_port));
            endpoint = nw_endpoint_create_host(addr_str, port_str);
        }

        if (!endpoint) {
            errno = EINVAL;
            return -1;
        }

        /* Create TCP parameters */
        nw_parameters_t params = nw_parameters_create_secure_tcp(
            NW_PARAMETERS_DISABLE_PROTOCOL, NW_PARAMETERS_DEFAULT_CONFIGURATION);

        /* Create connection */
        sock->connection = nw_connection_create(endpoint, params);
        nw_release(endpoint);
        nw_release(params);

        if (!sock->connection) {
            errno = ENOMEM;
            return -1;
        }

        /* Start connection */
        nw_connection_set_queue(sock->connection, dispatch_get_main_queue());

        dispatch_semaphore_t sem = dispatch_semaphore_create(0);
        __block int connect_error = 0;

        nw_connection_set_state_changed_handler(
            sock->connection, ^(nw_connection_state_t state, nw_error_t error) {
            (void)error;
            if (state == nw_connection_state_ready) {
                connect_error = 0;
            } else if (state == nw_connection_state_failed ||
                       state == nw_connection_state_cancelled) {
                connect_error = -1;
            }
            dispatch_semaphore_signal(sem);
        });

        nw_connection_start(sock->connection);

        /* Wait for connection with timeout */
        dispatch_time_t timeout = dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_SEC);
        if (dispatch_semaphore_wait(sem, timeout) != 0) {
            nw_connection_cancel(sock->connection);
            sock->connection = NULL;
            errno = ETIMEDOUT;
            return -1;
        }

        if (connect_error != 0) {
            nw_connection_cancel(sock->connection);
            sock->connection = NULL;
            errno = ECONNREFUSED;
            return -1;
        }
    }

    return 0;
}

static int bind_impl(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    socket_entry_t *sock = socket_get(sockfd);
    if (!sock) {
        return -1;
    }

    /* Validate address */
    if (!addr || addrlen == 0) {
        errno = EINVAL;
        return -1;
    }

    /* Copy local address */
    if (addrlen > sizeof(sock->local_addr)) {
        addrlen = sizeof(sock->local_addr);
    }
    memcpy(&sock->local_addr, addr, addrlen);
    sock->local_addr_len = addrlen;

    return 0;
}

static int listen_impl(int sockfd, int backlog) {
    socket_entry_t *sock = socket_get(sockfd);
    if (!sock) {
        return -1;
    }

    (void)backlog;

    /* For TCP sockets, create listener */
    if (sock->type != SOCK_STREAM) {
        errno = EOPNOTSUPP;
        return -1;
    }

    /* Create parameters */
    nw_parameters_t params = nw_parameters_create_secure_tcp(NW_PARAMETERS_DEFAULT_CONFIGURATION,
        NW_PARAMETERS_DISABLE_PROTOCOL);

    /* Create listener */
    sock->listener = nw_listener_create(params);
    nw_release(params);

    if (!sock->listener) {
        errno = ENOMEM;
        return -1;
    }

    /* Configure listener */
    nw_listener_set_queue(sock->listener, dispatch_get_main_queue());
    nw_listener_set_new_connection_handler(sock->listener, ^(nw_connection_t connection) {
        /* Handle new connection - store for accept */
        /* TODO: Implement accept queue */
        nw_release(connection);
    });

    /* Start listener */
    nw_listener_start(sock->listener);

    return 0;
}

static int accept_impl(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    socket_entry_t *sock = socket_get(sockfd);
    if (!sock) {
        return -1;
    }

    (void)addr;
    (void)addrlen;

    if (!sock->listener) {
        errno = EINVAL;
        return -1;
    }

    /* TODO: Implement accept queue */
    errno = EAGAIN;
    return -1;
}

static int accept4_impl(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags) {
    (void)flags;
    return accept_impl(sockfd, addr, addrlen);
}

/* ============================================================================
 * DATA TRANSFER
 * ============================================================================ */

static ssize_t send_impl(int sockfd, const void *buf, size_t len, int flags) {
    socket_entry_t *sock = socket_get(sockfd);
    if (!sock) {
        return -1;
    }

    if (!buf || len == 0) {
        return 0;
    }

    (void)flags;

    if (!sock->connection) {
        errno = ENOTCONN;
        return -1;
    }

    /* Create data */
    dispatch_data_t data = dispatch_data_create(buf, len, NULL, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
    if (!data) {
        errno = ENOMEM;
        return -1;
    }

    /* Send data */
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    __block ssize_t bytes_sent = 0;

    nw_connection_send(sock->connection, data, NW_CONNECTION_DEFAULT_MESSAGE_CONTEXT, true,
        ^(nw_error_t error) {
        if (error) {
            bytes_sent = -1;
        } else {
            bytes_sent = len;
        }
        dispatch_semaphore_signal(sem);
    });

    dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
    dispatch_release(sem);
    dispatch_release(data);

    if (bytes_sent < 0) {
        errno = EPIPE;
        return -1;
    }

    return bytes_sent;
}

static ssize_t recv_impl(int sockfd, void *buf, size_t len, int flags) {
    socket_entry_t *sock = socket_get(sockfd);
    if (!sock) {
        return -1;
    }

    if (!buf || len == 0) {
        return 0;
    }

    (void)flags;

    if (!sock->connection) {
        errno = ENOTCONN;
        return -1;
    }

    /* Receive data */
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    __block ssize_t bytes_received = 0;

    nw_connection_receive(sock->connection, 1, (uint32_t)len,
        ^(dispatch_data_t content, nw_content_context_t context, bool is_complete,
          nw_error_t error) {
        (void)context;
        (void)is_complete;
        if (content) {
            const void *data_ptr = NULL;
            size_t data_len = 0;

            dispatch_data_t mapped =
                dispatch_data_create_map(content, &data_ptr, &data_len);
            if (mapped && data_ptr && data_len > 0) {
                size_t copy_len = data_len < len ? data_len : len;
                memcpy(buf, data_ptr, copy_len);
                bytes_received = copy_len;
                dispatch_release(mapped);
            }
        }

        if (error) {
            bytes_received = -1;
        }

        dispatch_semaphore_signal(sem);
    });

    dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
    dispatch_release(sem);

    if (bytes_received < 0) {
        errno = ECONNRESET;
        return -1;
    }

    return bytes_received;
}

static ssize_t sendto_impl(int sockfd, const void *buf, size_t len, int flags,
    const struct sockaddr *dest_addr, socklen_t addrlen) {
    /* For connectionless sockets */
    (void)dest_addr;
    (void)addrlen;
    return send_impl(sockfd, buf, len, flags);
}

static ssize_t recvfrom_impl(int sockfd, void *buf, size_t len, int flags,
    struct sockaddr *src_addr, socklen_t *addrlen) {
    /* For connectionless sockets */
    (void)src_addr;
    (void)addrlen;
    return recv_impl(sockfd, buf, len, flags);
}

/* ============================================================================
 * SOCKET OPTIONS
 * ============================================================================ */

static int setsockopt_impl(int sockfd, int level, int optname, const void *optval,
    socklen_t optlen) {
    socket_entry_t *sock = socket_get(sockfd);
    if (!sock) {
        return -1;
    }

    (void)level;
    (void)optname;
    (void)optval;
    (void)optlen;

    /* Socket options not fully implemented in this version */
    return 0;
}

static int getsockopt_impl(int sockfd, int level, int optname, void *optval, socklen_t *optlen) {
    socket_entry_t *sock = socket_get(sockfd);
    if (!sock) {
        return -1;
    }

    (void)level;
    (void)optname;

    if (optval && optlen && *optlen >= sizeof(int)) {
        *(int *)optval = 0;
        *optlen = sizeof(int);
    }

    return 0;
}

/* ============================================================================
 * SHUTDOWN
 * ============================================================================ */

static int shutdown_impl(int sockfd, int how) {
    socket_entry_t *sock = socket_get(sockfd);
    if (!sock) {
        return -1;
    }

    (void)how;

    if (sock->connection) {
        nw_connection_cancel(sock->connection);
        sock->connection = NULL;
    }

    return 0;
}

/* ============================================================================
 * SOCKET NAME AND PEER
 * ============================================================================ */

static int getsockname_impl(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    socket_entry_t *sock = socket_get(sockfd);
    if (!sock) {
        return -1;
    }

    if (!addr || !addrlen) {
        errno = EFAULT;
        return -1;
    }

    if (sock->local_addr_len == 0) {
        errno = ENOTSOCK;
        return -1;
    }

    socklen_t len = sock->local_addr_len;
    if (*addrlen < len) {
        len = *addrlen;
    }

    memcpy(addr, &sock->local_addr, len);
    *addrlen = sock->local_addr_len;

    return 0;
}

static int getpeername_impl(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    socket_entry_t *sock = socket_get(sockfd);
    if (!sock) {
        return -1;
    }

    if (!addr || !addrlen) {
        errno = EFAULT;
        return -1;
    }

    if (!sock->connection) {
        errno = ENOTCONN;
        return -1;
    }

    if (sock->remote_addr_len == 0) {
        errno = ENOTCONN;
        return -1;
    }

    socklen_t len = sock->remote_addr_len;
    if (*addrlen < len) {
        len = *addrlen;
    }

    memcpy(addr, &sock->remote_addr, len);
    *addrlen = sock->remote_addr_len;

    return 0;
}

/* ============================================================================
 * MESSAGE I/O
 * ============================================================================ */

static ssize_t sendmsg_impl(int sockfd, const struct msghdr *msg, int flags) {
    socket_entry_t *sock = socket_get(sockfd);
    if (!sock) {
        return -1;
    }

    if (!msg) {
        errno = EFAULT;
        return -1;
    }

    if (!sock->connection) {
        errno = ENOTCONN;
        return -1;
    }

    (void)flags;

    /* Calculate total size from iovec array */
    size_t total_len = 0;
    for (int i = 0; i < msg->msg_iovlen; i++) {
        if (msg->msg_iov[i].iov_len > 0 && msg->msg_iov[i].iov_base) {
            total_len += msg->msg_iov[i].iov_len;
        }
    }

    if (total_len == 0) {
        return 0;
    }

    /* Flatten iovec into single buffer */
    char *buf = (char *)malloc(total_len);
    if (!buf) {
        errno = ENOMEM;
        return -1;
    }

    size_t offset = 0;
    for (int i = 0; i < msg->msg_iovlen; i++) {
        if (msg->msg_iov[i].iov_len > 0 && msg->msg_iov[i].iov_base) {
            memcpy(buf + offset, msg->msg_iov[i].iov_base, msg->msg_iov[i].iov_len);
            offset += msg->msg_iov[i].iov_len;
        }
    }

    /* Create data and send */
    dispatch_data_t data = dispatch_data_create(buf, total_len, NULL, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
    if (!data) {
        free(buf);
        errno = ENOMEM;
        return -1;
    }

    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    __block ssize_t bytes_sent = 0;

    nw_connection_send(sock->connection, data, NW_CONNECTION_DEFAULT_MESSAGE_CONTEXT, true,
        ^(nw_error_t error) {
        if (error) {
            bytes_sent = -1;
        } else {
            bytes_sent = offset;
        }
        dispatch_semaphore_signal(sem);
    });

    dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
    dispatch_release(sem);
    dispatch_release(data);
    free(buf);

    if (bytes_sent < 0) {
        errno = EPIPE;
        return -1;
    }

    return bytes_sent;
}

static ssize_t recvmsg_impl(int sockfd, struct msghdr *msg, int flags) {
    socket_entry_t *sock = socket_get(sockfd);
    if (!sock) {
        return -1;
    }

    if (!msg) {
        errno = EFAULT;
        return -1;
    }

    if (!sock->connection) {
        errno = ENOTCONN;
        return -1;
    }

    (void)flags;

    /* Calculate total available buffer size */
    size_t total_len = 0;
    for (int i = 0; i < msg->msg_iovlen; i++) {
        if (msg->msg_iov[i].iov_len > 0 && msg->msg_iov[i].iov_base) {
            total_len += msg->msg_iov[i].iov_len;
        }
    }

    if (total_len == 0) {
        return 0;
    }

    /* Receive into temporary buffer */
    char *buf = (char *)malloc(total_len);
    if (!buf) {
        errno = ENOMEM;
        return -1;
    }

    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    __block ssize_t bytes_received = 0;
    __block dispatch_data_t received_data = NULL;

    nw_connection_receive(sock->connection, 1, (uint32_t)total_len,
        ^(dispatch_data_t content, nw_content_context_t context, bool is_complete, nw_error_t receive_error) {
        if (receive_error) {
            bytes_received = -1;
        } else if (content) {
            const void *bytes = NULL;
            size_t len = 0;
            dispatch_data_t map = dispatch_data_create_map(content, &bytes, &len);
            if (bytes && len > 0) {
                size_t to_copy = len;
                if (to_copy > total_len) {
                    to_copy = total_len;
                }
                memcpy(buf, bytes, to_copy);
                bytes_received = to_copy;
            }
            if (map) {
                dispatch_release(map);
            }
        }
        if (content) {
            received_data = content;
            dispatch_retain(received_data);
        }
        dispatch_semaphore_signal(sem);
    });

    dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
    dispatch_release(sem);
    if (received_data) {
        dispatch_release(received_data);
    }

    if (bytes_received < 0) {
        free(buf);
        errno = ECONNRESET;
        return -1;
    }

    /* Distribute received data into iovec array */
    size_t remaining = bytes_received;
    size_t offset = 0;
    for (int i = 0; i < msg->msg_iovlen && remaining > 0; i++) {
        if (msg->msg_iov[i].iov_len > 0 && msg->msg_iov[i].iov_base) {
            size_t to_copy = msg->msg_iov[i].iov_len;
            if (to_copy > remaining) {
                to_copy = remaining;
            }
            memcpy(msg->msg_iov[i].iov_base, buf + offset, to_copy);
            offset += to_copy;
            remaining -= to_copy;
        }
    }

    free(buf);

    /* Set address info if requested */
    if (msg->msg_name && msg->msg_namelen) {
        if (sock->remote_addr_len > 0 && sock->remote_addr_len <= msg->msg_namelen) {
            memcpy(msg->msg_name, &sock->remote_addr, sock->remote_addr_len);
            msg->msg_namelen = sock->remote_addr_len;
        } else {
            msg->msg_namelen = 0;
        }
    }

    msg->msg_controllen = 0;
    msg->msg_flags = 0;

    return bytes_received;
}

/* ============================================================================
 * Public Canonical Socket Syscalls
 * ============================================================================ */

__attribute__((visibility("default"))) int socket(int domain, int type, int protocol) {
    return socket_impl(domain, type, protocol);
}

__attribute__((visibility("default"))) int socketpair(int domain, int type, int protocol, int sv[2]) {
    return socketpair_impl(domain, type, protocol, sv);
}

__attribute__((visibility("default"))) int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    return connect_impl(sockfd, addr, addrlen);
}

__attribute__((visibility("default"))) int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    return bind_impl(sockfd, addr, addrlen);
}

__attribute__((visibility("default"))) int listen(int sockfd, int backlog) {
    return listen_impl(sockfd, backlog);
}

__attribute__((visibility("default"))) int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    return accept_impl(sockfd, addr, addrlen);
}

__attribute__((visibility("default"))) int accept4(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags) {
    return accept4_impl(sockfd, addr, addrlen, flags);
}

__attribute__((visibility("default"))) ssize_t send(int sockfd, const void *buf, size_t len, int flags) {
    return send_impl(sockfd, buf, len, flags);
}

__attribute__((visibility("default"))) ssize_t recv(int sockfd, void *buf, size_t len, int flags) {
    return recv_impl(sockfd, buf, len, flags);
}

__attribute__((visibility("default"))) ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
    const struct sockaddr *dest_addr, socklen_t addrlen) {
    return sendto_impl(sockfd, buf, len, flags, dest_addr, addrlen);
}

__attribute__((visibility("default"))) ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
    struct sockaddr *src_addr, socklen_t *addrlen) {
    return recvfrom_impl(sockfd, buf, len, flags, src_addr, addrlen);
}

__attribute__((visibility("default"))) int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen) {
    return setsockopt_impl(sockfd, level, optname, optval, optlen);
}

__attribute__((visibility("default"))) int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen) {
    return getsockopt_impl(sockfd, level, optname, optval, optlen);
}

__attribute__((visibility("default"))) int shutdown(int sockfd, int how) {
    return shutdown_impl(sockfd, how);
}

__attribute__((visibility("default"))) int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    return getsockname_impl(sockfd, addr, addrlen);
}

__attribute__((visibility("default"))) int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    return getpeername_impl(sockfd, addr, addrlen);
}

__attribute__((visibility("default"))) ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags) {
    return sendmsg_impl(sockfd, msg, flags);
}

__attribute__((visibility("default"))) ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags) {
    return recvmsg_impl(sockfd, msg, flags);
}

/* Network initialization - internal use only */
int network_init(void) {
    return network_init_impl();
}

int network_deinit(void) {
    return network_deinit_impl();
}
