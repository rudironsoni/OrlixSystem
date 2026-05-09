#include "socket.h"

#ifdef SIGPIPE
#undef SIGPIPE
#endif
#define __ASSEMBLY__ 1
#include <asm-generic/signal.h>
#undef __ASSEMBLY__

#include <errno.h>
#include <limits.h>
#include <linux/poll.h>
#include <linux/socket.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "internal/private/kernel_socket_compat.h"
#include "kernel_sync.h"
#include "../signal.h"
#include "../task.h"
#include "../wait_queue.h"

void poll_notify_readiness_impl(void);

#define ORLIX_SOCKET_BUFFER_SIZE 65536U
#define ORLIX_UNIX_NAME_MAX 107U /* sizeof(sockaddr_un.sun_path) - 1 */
#define ORLIX_SOCKET_NONBLOCK 00004000 /* Linux O_NONBLOCK / SOCK_NONBLOCK */
#define ORLIX_LINUX_SOL_SOCKET 1
#define ORLIX_LINUX_SO_REUSEADDR 2
#define ORLIX_LINUX_SO_TYPE 3
#define ORLIX_LINUX_SO_ERROR 4
#define ORLIX_LINUX_SO_SNDBUF 7
#define ORLIX_LINUX_SO_RCVBUF 8
#define ORLIX_LINUX_SO_KEEPALIVE 9
#define ORLIX_LINUX_SO_ACCEPTCONN 30
#define ORLIX_LINUX_SO_PROTOCOL 38
#define ORLIX_LINUX_SO_DOMAIN 39

struct socket_address_un {
    __kernel_sa_family_t family;
    char path[ORLIX_UNIX_NAME_MAX + 1];
};

struct socket_namespace_entry {
    struct socket_namespace_entry *next;
    unsigned char name[ORLIX_UNIX_NAME_MAX];
    size_t name_len;
    struct socket_state *sock;
};

struct socket_datagram {
    struct socket_datagram *next;
    size_t len;
    bool sender_name_valid;
    size_t sender_name_len;
    unsigned char sender_name[ORLIX_UNIX_NAME_MAX];
    unsigned char data[];
};

struct socket_state {
    atomic_int refs;
    unsigned long long id;
    int domain;
    int type;
    int protocol;

    struct socket_state *peer;
    bool peer_writes_open;
    bool writes_open;

    bool is_bound;
    bool namespace_registered;
    unsigned char bound_name[ORLIX_UNIX_NAME_MAX];
    size_t bound_name_len;
    bool peer_name_valid;
    unsigned char peer_name[ORLIX_UNIX_NAME_MAX];
    size_t peer_name_len;
    bool connected_name_valid;
    unsigned char connected_name[ORLIX_UNIX_NAME_MAX];
    size_t connected_name_len;
    int pending_error;
    int sendbuf;
    int recvbuf;
    bool reuseaddr;
    bool keepalive;

    bool is_listening;
    int backlog;
    struct socket_state *accept_queue_head;
    struct socket_state *accept_queue_tail;
    struct socket_state *accept_queue_next;
    int accept_queue_len;

    unsigned char buffer[ORLIX_SOCKET_BUFFER_SIZE];
    size_t head;
    size_t len;
    struct socket_datagram *dgram_head;
    struct socket_datagram *dgram_tail;
    size_t dgram_count;
    size_t dgram_bytes;
    struct wait_queue_head wait;
};

static kernel_mutex_t socket_namespace_lock = KERNEL_MUTEX_INITIALIZER;
static struct socket_namespace_entry *socket_namespace_entries;
static atomic_ullong next_socket_id = 1;

static size_t socket_space_locked(const struct socket_state *sock) {
    return ORLIX_SOCKET_BUFFER_SIZE - sock->len;
}

static void socket_wake_all_locked(struct socket_state *sock) {
    wait_queue_wake_all_locked(&sock->wait);
}

static bool socket_is_stream(int type) { return type == SOCK_STREAM; }
static bool socket_is_dgram(int type) { return type == SOCK_DGRAM; }

static bool socket_is_supported_type(int type) {
    return socket_is_stream(type) || socket_is_dgram(type);
}

static __u32 socket_unix_addr_len(size_t name_len) {
    if (name_len == 0) {
        return (__u32)offsetof(struct socket_address_un, path);
    }
    return (__u32)(offsetof(struct socket_address_un, path) + 1 + name_len);
}

static void socket_store_peer_name(struct socket_state *sock,
                                   const unsigned char *name,
                                   size_t name_len,
                                   bool valid) {
    if (!sock) {
        return;
    }
    sock->peer_name_valid = valid;
    sock->peer_name_len = 0;
    if (valid && name && name_len > 0 && name_len <= ORLIX_UNIX_NAME_MAX) {
        memcpy(sock->peer_name, name, name_len);
        sock->peer_name_len = name_len;
    }
}

static int socket_copy_name_out(const unsigned char *name,
                                size_t name_len,
                                struct sockaddr *addr,
                                __u32 *addrlen) {
    struct socket_address_un unix_addr;
    __u32 actual_len;
    __u32 copy_len;

    if (!addrlen) {
        errno = EFAULT;
        return -1;
    }

    actual_len = socket_unix_addr_len(name_len);
    if (!addr) {
        *addrlen = actual_len;
        return 0;
    }

    memset(&unix_addr, 0, sizeof(unix_addr));
    unix_addr.family = AF_UNIX;
    if (name_len > 0) {
        unix_addr.path[0] = '\0';
        memcpy(&unix_addr.path[1], name, name_len);
    }

    copy_len = *addrlen < actual_len ? *addrlen : actual_len;
    memcpy(addr, &unix_addr, copy_len);
    *addrlen = actual_len;
    return 0;
}

static int socket_parse_unix_name(const struct sockaddr *addr,
                                  __u32 addrlen,
                                  unsigned char *name_out,
                                  size_t *name_len_out) {
    const struct socket_address_un *un;
    size_t sun_path_len;
    size_t name_len;

    if (!addr || !name_out || !name_len_out) {
        errno = EFAULT;
        return -1;
    }
    if (addrlen < offsetof(struct socket_address_un, path) + 1) {
        errno = EINVAL;
        return -1;
    }

    un = (const struct socket_address_un *)addr;
    if (un->family != AF_UNIX) {
        errno = EAFNOSUPPORT;
        return -1;
    }

    sun_path_len = addrlen - offsetof(struct socket_address_un, path);
    if (sun_path_len == 0 || sun_path_len > sizeof(un->path)) {
        errno = EINVAL;
        return -1;
    }
    if (un->path[0] != '\0') {
        errno = EOPNOTSUPP;
        return -1;
    }
    if (sun_path_len < 2) {
        errno = EINVAL;
        return -1;
    }

    name_len = sun_path_len - 1;
    if (name_len == 0 || name_len > ORLIX_UNIX_NAME_MAX) {
        errno = EINVAL;
        return -1;
    }

    memcpy(name_out, &un->path[1], name_len);
    *name_len_out = name_len;
    return 0;
}

static void socket_store_connected_name(struct socket_state *sock,
                                        const unsigned char *name,
                                        size_t name_len,
                                        bool valid) {
    if (!sock) {
        return;
    }
    sock->connected_name_valid = valid;
    sock->connected_name_len = 0;
    if (valid && name && name_len > 0 && name_len <= ORLIX_UNIX_NAME_MAX) {
        memcpy(sock->connected_name, name, name_len);
        sock->connected_name_len = name_len;
    }
}

static bool socket_name_equals(const unsigned char *a, size_t a_len, const unsigned char *b, size_t b_len) {
    return a_len == b_len && (a_len == 0 || memcmp(a, b, a_len) == 0);
}

static struct socket_state *socket_namespace_lookup_locked(const unsigned char *name, size_t name_len) {
    for (struct socket_namespace_entry *e = socket_namespace_entries; e; e = e->next) {
        if (socket_name_equals(e->name, e->name_len, name, name_len)) {
            return e->sock;
        }
    }
    return NULL;
}

static int socket_namespace_register_impl(struct socket_state *sock, const unsigned char *name, size_t name_len) {
    struct socket_namespace_entry *entry;

    if (!sock || !name || name_len == 0 || name_len > ORLIX_UNIX_NAME_MAX) {
        errno = EINVAL;
        return -1;
    }

    entry = calloc(1, sizeof(*entry));
    if (!entry) {
        errno = ENOMEM;
        return -1;
    }
    memcpy(entry->name, name, name_len);
    entry->name_len = name_len;
    entry->sock = sock;

    kernel_mutex_lock(&socket_namespace_lock);
    if (socket_namespace_lookup_locked(name, name_len)) {
        kernel_mutex_unlock(&socket_namespace_lock);
        free(entry);
        errno = EADDRINUSE;
        return -1;
    }
    socket_retain_impl(sock);
    entry->next = socket_namespace_entries;
    socket_namespace_entries = entry;
    sock->namespace_registered = true;
    kernel_mutex_unlock(&socket_namespace_lock);
    return 0;
}

static void socket_namespace_unregister_impl(struct socket_state *sock, const unsigned char *name, size_t name_len) {
    struct socket_namespace_entry **pp;

    if (!sock || !name || name_len == 0 || name_len > ORLIX_UNIX_NAME_MAX) {
        return;
    }

    kernel_mutex_lock(&socket_namespace_lock);
    pp = &socket_namespace_entries;
    while (*pp) {
        struct socket_namespace_entry *cur = *pp;
        if (cur->sock == sock && socket_name_equals(cur->name, cur->name_len, name, name_len)) {
            *pp = cur->next;
            sock->namespace_registered = false;
            kernel_mutex_unlock(&socket_namespace_lock);
            socket_release_impl(cur->sock);
            free(cur);
            return;
        }
        pp = &cur->next;
    }
    kernel_mutex_unlock(&socket_namespace_lock);
}

static struct socket_state *socket_lookup_named_peer(const unsigned char *name, size_t name_len) {
    struct socket_state *peer = NULL;

    if (!name || name_len == 0 || name_len > ORLIX_UNIX_NAME_MAX) {
        errno = EINVAL;
        return NULL;
    }

    kernel_mutex_lock(&socket_namespace_lock);
    peer = socket_namespace_lookup_locked(name, name_len);
    if (peer) {
        socket_retain_impl(peer);
    }
    kernel_mutex_unlock(&socket_namespace_lock);
    if (!peer) {
        errno = ENOENT;
    }
    return peer;
}

static size_t socket_datagram_space_locked(const struct socket_state *sock) {
    return sock->recvbuf > 0 && (size_t)sock->recvbuf > sock->dgram_bytes
               ? (size_t)sock->recvbuf - sock->dgram_bytes
               : 0;
}

static void socket_datagram_queue_free_all(struct socket_state *sock) {
    struct socket_datagram *msg;

    if (!sock) {
        return;
    }
    msg = sock->dgram_head;
    sock->dgram_head = NULL;
    sock->dgram_tail = NULL;
    sock->dgram_count = 0;
    sock->dgram_bytes = 0;
    while (msg) {
        struct socket_datagram *next = msg->next;
        free(msg);
        msg = next;
    }
}

static ssize_t socket_queue_datagram_locked(struct socket_state *target,
                                            const void *buf,
                                            size_t len,
                                            const unsigned char *sender_name,
                                            size_t sender_name_len,
                                            bool sender_name_valid) {
    struct socket_datagram *msg;

    if (!target) {
        errno = EBADF;
        return -1;
    }
    if (len > ORLIX_SOCKET_BUFFER_SIZE || len > (size_t)target->recvbuf) {
        errno = EMSGSIZE;
        return -1;
    }
    if (!target->writes_open) {
        errno = ECONNREFUSED;
        return -1;
    }
    if (socket_datagram_space_locked(target) < len) {
        errno = EAGAIN;
        return -1;
    }

    msg = calloc(1, sizeof(*msg) + len);
    if (!msg) {
        errno = ENOMEM;
        return -1;
    }
    msg->len = len;
    msg->sender_name_valid = sender_name_valid;
    if (sender_name_valid && sender_name && sender_name_len > 0 && sender_name_len <= ORLIX_UNIX_NAME_MAX) {
        msg->sender_name_len = sender_name_len;
        memcpy(msg->sender_name, sender_name, sender_name_len);
    }
    memcpy(msg->data, buf, len);
    if (!target->dgram_tail) {
        target->dgram_head = msg;
        target->dgram_tail = msg;
    } else {
        target->dgram_tail->next = msg;
        target->dgram_tail = msg;
    }
    target->dgram_count++;
    target->dgram_bytes += len;
    socket_wake_all_locked(target);
    return (ssize_t)len;
}

struct socket_state *socket_create_impl(int domain, int type, int protocol) {
    struct socket_state *sock;

    if (domain != AF_UNIX) {
        errno = EAFNOSUPPORT;
        return NULL;
    }
    if (!socket_is_supported_type(type)) {
        errno = EPROTOTYPE;
        return NULL;
    }

    sock = calloc(1, sizeof(*sock));
    if (!sock) {
        errno = ENOMEM;
        return NULL;
    }

    atomic_init(&sock->refs, 1);
    sock->id = atomic_fetch_add(&next_socket_id, 1);
    sock->domain = domain;
    sock->type = type;
    sock->protocol = protocol;
    sock->peer = NULL;
    sock->peer_writes_open = false;
    sock->writes_open = true;
    sock->is_bound = false;
    sock->namespace_registered = false;
    sock->bound_name_len = 0;
    sock->peer_name_valid = false;
    sock->peer_name_len = 0;
    sock->connected_name_valid = false;
    sock->connected_name_len = 0;
    sock->pending_error = 0;
    sock->sendbuf = (int)ORLIX_SOCKET_BUFFER_SIZE;
    sock->recvbuf = (int)ORLIX_SOCKET_BUFFER_SIZE;
    sock->reuseaddr = false;
    sock->keepalive = false;
    sock->is_listening = false;
    sock->backlog = 0;
    sock->accept_queue_head = NULL;
    sock->accept_queue_tail = NULL;
    sock->accept_queue_next = NULL;
    sock->accept_queue_len = 0;
    sock->head = 0;
    sock->len = 0;
    sock->dgram_head = NULL;
    sock->dgram_tail = NULL;
    sock->dgram_count = 0;
    sock->dgram_bytes = 0;
    wait_queue_init(&sock->wait);
    return sock;
}

int socketpair_create_impl(int domain,
                           int type,
                           int protocol,
                           struct socket_state **a_out,
                           struct socket_state **b_out) {
    struct socket_state *a;
    struct socket_state *b;

    if (!a_out || !b_out) {
        errno = EFAULT;
        return -1;
    }
    *a_out = NULL;
    *b_out = NULL;

    a = socket_create_impl(domain, type, protocol);
    b = socket_create_impl(domain, type, protocol);
    if (!a || !b) {
        socket_release_impl(a);
        socket_release_impl(b);
        return -1;
    }

    a->peer = b;
    b->peer = a;
    a->peer_writes_open = true;
    b->peer_writes_open = true;
    a->peer_name_valid = true;
    b->peer_name_valid = true;

    *a_out = a;
    *b_out = b;
    return 0;
}

unsigned long long socket_identity_impl(const struct socket_state *sock) {
    return sock ? sock->id : 0;
}

void socket_retain_impl(struct socket_state *sock) {
    if (sock) {
        atomic_fetch_add(&sock->refs, 1);
    }
}

void socket_release_impl(struct socket_state *sock) {
    struct socket_state *peer;
    unsigned char name[ORLIX_UNIX_NAME_MAX];
    size_t name_len = 0;
    bool was_bound = false;
    struct socket_state *accepted_head = NULL;

    if (!sock) {
        return;
    }
    if (atomic_fetch_sub(&sock->refs, 1) != 1) {
        return;
    }

    wait_queue_lock(&sock->wait);
    sock->writes_open = false;
    peer = sock->peer;
    sock->peer = NULL;
    was_bound = sock->is_bound;
    if (was_bound) {
        name_len = sock->bound_name_len;
        if (name_len > 0 && name_len <= ORLIX_UNIX_NAME_MAX) {
            memcpy(name, sock->bound_name, name_len);
        } else {
            name_len = 0;
        }
        sock->is_bound = false;
        sock->bound_name_len = 0;
    }
    sock->connected_name_valid = false;
    sock->connected_name_len = 0;
    sock->is_listening = false;
    accepted_head = sock->accept_queue_head;
    sock->accept_queue_head = NULL;
    sock->accept_queue_tail = NULL;
    sock->accept_queue_len = 0;
    socket_datagram_queue_free_all(sock);
    socket_wake_all_locked(sock);
    wait_queue_unlock(&sock->wait);

    if (sock->namespace_registered && was_bound && name_len > 0) {
        socket_namespace_unregister_impl(sock, name, name_len);
    }

    if (peer) {
        wait_queue_lock(&peer->wait);
        peer->peer_writes_open = false;
        socket_wake_all_locked(peer);
        wait_queue_unlock(&peer->wait);
        poll_notify_readiness_impl();
    }

    while (accepted_head) {
        struct socket_state *next = accepted_head->accept_queue_next;
        accepted_head->accept_queue_next = NULL;
        socket_release_impl(accepted_head);
        accepted_head = next;
    }

    wait_queue_destroy(&sock->wait);
    free(sock);
    poll_notify_readiness_impl();
}

int socket_shutdown_impl(struct socket_state *sock, int how) {
    if (!sock) {
        errno = EBADF;
        return -1;
    }

    if (how != SHUT_RD && how != SHUT_WR && how != SHUT_RDWR) {
        errno = EINVAL;
        return -1;
    }

    wait_queue_lock(&sock->wait);
    if (how == SHUT_WR || how == SHUT_RDWR) {
        sock->writes_open = false;
        if (sock->peer) {
            wait_queue_lock(&sock->peer->wait);
            sock->peer->peer_writes_open = false;
            socket_wake_all_locked(sock->peer);
            wait_queue_unlock(&sock->peer->wait);
        }
    }
    socket_wake_all_locked(sock);
    wait_queue_unlock(&sock->wait);
    poll_notify_readiness_impl();
    return 0;
}

int socket_connect_impl(struct socket_state *sock, const struct sockaddr *addr, __u32 addrlen) {
    size_t name_len;
    unsigned char name[ORLIX_UNIX_NAME_MAX];
    struct socket_state *listener = NULL;
    struct socket_state *server_side = NULL;

    if (!sock || !addr) {
        errno = EFAULT;
        return -1;
    }
    if (socket_parse_unix_name(addr, addrlen, name, &name_len) != 0) {
        return -1;
    }

    wait_queue_lock(&sock->wait);
    if (sock->peer) {
        wait_queue_unlock(&sock->wait);
        errno = EISCONN;
        return -1;
    }
    if (socket_is_dgram(sock->type)) {
        struct socket_state *peer;

        wait_queue_unlock(&sock->wait);
        peer = socket_lookup_named_peer(name, name_len);
        if (!peer) {
            return -1;
        }
        wait_queue_lock(&peer->wait);
        if (peer->type != sock->type || peer->domain != sock->domain || !peer->is_bound) {
            wait_queue_unlock(&peer->wait);
            socket_release_impl(peer);
            errno = EPROTOTYPE;
            return -1;
        }
        wait_queue_unlock(&peer->wait);
        socket_release_impl(peer);

        wait_queue_lock(&sock->wait);
        socket_store_connected_name(sock, name, name_len, true);
        socket_store_peer_name(sock, name, name_len, true);
        socket_wake_all_locked(sock);
        wait_queue_unlock(&sock->wait);
        poll_notify_readiness_impl();
        return 0;
    }
    wait_queue_unlock(&sock->wait);

    listener = socket_lookup_named_peer(name, name_len);
    if (!listener) {
        return -1;
    }

    wait_queue_lock(&listener->wait);
    if (!listener->is_listening) {
        wait_queue_unlock(&listener->wait);
        socket_release_impl(listener);
        errno = ECONNREFUSED;
        return -1;
    }
    if (listener->backlog > 0 && listener->accept_queue_len >= listener->backlog) {
        wait_queue_unlock(&listener->wait);
        socket_release_impl(listener);
        errno = EAGAIN;
        return -1;
    }
    wait_queue_unlock(&listener->wait);

    server_side = socket_create_impl(AF_UNIX, sock->type, sock->protocol);
    if (!server_side) {
        socket_release_impl(listener);
        return -1;
    }

    wait_queue_lock(&sock->wait);
    wait_queue_lock(&server_side->wait);
    sock->peer = server_side;
    sock->peer_writes_open = true;
    socket_store_peer_name(sock, listener->bound_name, listener->bound_name_len, listener->is_bound);
    server_side->peer = sock;
    server_side->peer_writes_open = true;
    server_side->is_bound = listener->is_bound;
    server_side->bound_name_len = listener->bound_name_len;
    if (listener->bound_name_len > 0) {
        memcpy(server_side->bound_name, listener->bound_name, listener->bound_name_len);
    }
    server_side->namespace_registered = false;
    socket_store_peer_name(server_side, sock->bound_name, sock->bound_name_len, true);
    wait_queue_unlock(&server_side->wait);
    socket_wake_all_locked(sock);
    wait_queue_unlock(&sock->wait);

    wait_queue_lock(&listener->wait);
    server_side->accept_queue_next = NULL;
    if (!listener->accept_queue_tail) {
        listener->accept_queue_head = server_side;
        listener->accept_queue_tail = server_side;
    } else {
        listener->accept_queue_tail->accept_queue_next = server_side;
        listener->accept_queue_tail = server_side;
    }
    listener->accept_queue_len++;
    socket_wake_all_locked(listener);
    wait_queue_unlock(&listener->wait);

    socket_release_impl(listener);
    poll_notify_readiness_impl();
    return 0;
}

int socket_bind_impl(struct socket_state *sock, const struct sockaddr *addr, __u32 addrlen) {
    unsigned char name[ORLIX_UNIX_NAME_MAX];
    size_t name_len;

    if (!sock || !addr) {
        errno = EFAULT;
        return -1;
    }
    if (socket_parse_unix_name(addr, addrlen, name, &name_len) != 0) {
        return -1;
    }

    wait_queue_lock(&sock->wait);
    if (sock->is_bound) {
        wait_queue_unlock(&sock->wait);
        errno = EINVAL;
        return -1;
    }
    memcpy(sock->bound_name, name, name_len);
    sock->bound_name_len = name_len;
    sock->is_bound = true;
    sock->namespace_registered = false;
    wait_queue_unlock(&sock->wait);

    if (socket_namespace_register_impl(sock, sock->bound_name, sock->bound_name_len) != 0) {
        wait_queue_lock(&sock->wait);
        sock->is_bound = false;
        sock->bound_name_len = 0;
        wait_queue_unlock(&sock->wait);
        return -1;
    }

    return 0;
}

int socket_listen_impl(struct socket_state *sock, int backlog) {
    if (!sock) {
        errno = EBADF;
        return -1;
    }
    if (backlog < 0) {
        errno = EINVAL;
        return -1;
    }

    wait_queue_lock(&sock->wait);
    if (!socket_is_stream(sock->type)) {
        wait_queue_unlock(&sock->wait);
        errno = EOPNOTSUPP;
        return -1;
    }
    if (!sock->is_bound) {
        wait_queue_unlock(&sock->wait);
        errno = EINVAL;
        return -1;
    }
    sock->is_listening = true;
    sock->backlog = backlog;
    socket_wake_all_locked(sock);
    wait_queue_unlock(&sock->wait);
    poll_notify_readiness_impl();
    return 0;
}

struct socket_state *socket_accept_impl(struct socket_state *sock,
                                        struct sockaddr *addr,
                                        __u32 *addrlen,
                                        int flags) {
    bool nonblock = (flags & ORLIX_SOCKET_NONBLOCK) != 0;
    struct socket_state *accepted;

    if (!sock) {
        errno = EBADF;
        return NULL;
    }
    if (addr) {
        errno = EOPNOTSUPP;
        return NULL;
    }
    if (addrlen) {
        *addrlen = 0;
    }

    wait_queue_lock(&sock->wait);
    if (!sock->is_listening) {
        wait_queue_unlock(&sock->wait);
        errno = EINVAL;
        return NULL;
    }
    while (!sock->accept_queue_head) {
        if (nonblock) {
            wait_queue_unlock(&sock->wait);
            errno = EAGAIN;
            return NULL;
        }
        if (wait_queue_wait_locked_interruptible(&sock->wait) != 0) {
            wait_queue_unlock(&sock->wait);
            errno = EINTR;
            return NULL;
        }
        if (!sock->is_listening) {
            wait_queue_unlock(&sock->wait);
            errno = EINVAL;
            return NULL;
        }
    }

    accepted = sock->accept_queue_head;
    sock->accept_queue_head = accepted->accept_queue_next;
    if (!sock->accept_queue_head) {
        sock->accept_queue_tail = NULL;
    }
    accepted->accept_queue_next = NULL;
    if (sock->accept_queue_len > 0) {
        sock->accept_queue_len--;
    }
    socket_wake_all_locked(sock);
    wait_queue_unlock(&sock->wait);
    poll_notify_readiness_impl();
    return accepted;
}

int socket_getsockname_impl(struct socket_state *sock, struct sockaddr *addr, __u32 *addrlen) {
    unsigned char name[ORLIX_UNIX_NAME_MAX];
    size_t name_len = 0;

    if (!sock) {
        errno = EBADF;
        return -1;
    }

    wait_queue_lock(&sock->wait);
    if (sock->is_bound && sock->bound_name_len > 0) {
        name_len = sock->bound_name_len;
        memcpy(name, sock->bound_name, name_len);
    }
    wait_queue_unlock(&sock->wait);
    return socket_copy_name_out(name, name_len, addr, addrlen);
}

int socket_getpeername_impl(struct socket_state *sock, struct sockaddr *addr, __u32 *addrlen) {
    unsigned char name[ORLIX_UNIX_NAME_MAX];
    size_t name_len = 0;
    bool valid;

    if (!sock) {
        errno = EBADF;
        return -1;
    }

    wait_queue_lock(&sock->wait);
    valid = sock->peer_name_valid;
    if (valid && sock->peer_name_len > 0) {
        name_len = sock->peer_name_len;
        memcpy(name, sock->peer_name, name_len);
    }
    wait_queue_unlock(&sock->wait);

    if (!valid) {
        errno = ENOTCONN;
        return -1;
    }

    return socket_copy_name_out(name, name_len, addr, addrlen);
}

static int socket_copy_int_opt(void *optval, __u32 *optlen, int value) {
    int copy_value = value;
    __u32 copy_len;

    if (!optlen || !optval) {
        errno = EFAULT;
        return -1;
    }
    if (*optlen < (__u32)sizeof(int)) {
        errno = EINVAL;
        return -1;
    }
    copy_len = *optlen < (__u32)sizeof(copy_value) ? *optlen : (__u32)sizeof(copy_value);
    memcpy(optval, &copy_value, copy_len);
    *optlen = (__u32)sizeof(copy_value);
    return 0;
}

int socket_getsockopt_impl(struct socket_state *sock,
                           int level,
                           int optname,
                           void *optval,
                           __u32 *optlen) {
    int value;

    if (!sock) {
        errno = EBADF;
        return -1;
    }
    if (level != ORLIX_LINUX_SOL_SOCKET) {
        errno = ENOPROTOOPT;
        return -1;
    }

    wait_queue_lock(&sock->wait);
    switch (optname) {
    case ORLIX_LINUX_SO_TYPE:
        value = sock->type;
        break;
    case ORLIX_LINUX_SO_DOMAIN:
        value = sock->domain;
        break;
    case ORLIX_LINUX_SO_PROTOCOL:
        value = sock->protocol;
        break;
    case ORLIX_LINUX_SO_ACCEPTCONN:
        value = sock->is_listening ? 1 : 0;
        break;
    case ORLIX_LINUX_SO_ERROR:
        value = sock->pending_error;
        sock->pending_error = 0;
        break;
    case ORLIX_LINUX_SO_SNDBUF:
        value = sock->sendbuf;
        break;
    case ORLIX_LINUX_SO_RCVBUF:
        value = sock->recvbuf;
        break;
    case ORLIX_LINUX_SO_REUSEADDR:
        value = sock->reuseaddr ? 1 : 0;
        break;
    case ORLIX_LINUX_SO_KEEPALIVE:
        value = sock->keepalive ? 1 : 0;
        break;
    default:
        wait_queue_unlock(&sock->wait);
        errno = ENOPROTOOPT;
        return -1;
    }
    wait_queue_unlock(&sock->wait);
    return socket_copy_int_opt(optval, optlen, value);
}

int socket_setsockopt_impl(struct socket_state *sock,
                           int level,
                           int optname,
                           const void *optval,
                           __u32 optlen) {
    int value;

    if (!sock) {
        errno = EBADF;
        return -1;
    }
    if (level != ORLIX_LINUX_SOL_SOCKET) {
        errno = ENOPROTOOPT;
        return -1;
    }
    if (!optval || optlen < (__u32)sizeof(int)) {
        errno = EINVAL;
        return -1;
    }

    memcpy(&value, optval, sizeof(value));
    wait_queue_lock(&sock->wait);
    switch (optname) {
    case ORLIX_LINUX_SO_REUSEADDR:
        sock->reuseaddr = value != 0;
        break;
    case ORLIX_LINUX_SO_KEEPALIVE:
        sock->keepalive = value != 0;
        break;
    case ORLIX_LINUX_SO_SNDBUF:
        if (value <= 0) {
            wait_queue_unlock(&sock->wait);
            errno = EINVAL;
            return -1;
        }
        sock->sendbuf = value > INT_MAX / 2 ? INT_MAX / 2 : value;
        break;
    case ORLIX_LINUX_SO_RCVBUF:
        if (value <= 0) {
            wait_queue_unlock(&sock->wait);
            errno = EINVAL;
            return -1;
        }
        sock->recvbuf = value > INT_MAX / 2 ? INT_MAX / 2 : value;
        break;
    default:
        wait_queue_unlock(&sock->wait);
        errno = ENOPROTOOPT;
        return -1;
    }
    wait_queue_unlock(&sock->wait);
    return 0;
}

__kernel_ssize_t socket_sendto_impl(struct socket_state *sock,
                                    const void *buf,
                                    size_t len,
                                    int flags,
                                    const struct sockaddr *dest_addr,
                                    __u32 addrlen) {
    struct socket_state *peer;
    bool nonblock;
    size_t to_write;
    unsigned char name[ORLIX_UNIX_NAME_MAX];
    size_t name_len = 0;
    bool sender_name_valid = false;
    unsigned char sender_name[ORLIX_UNIX_NAME_MAX];
    size_t sender_name_len = 0;

    if (!sock) {
        errno = EBADF;
        return -1;
    }
    if (!buf && len > 0) {
        errno = EFAULT;
        return -1;
    }
    if (len == 0) {
        return 0;
    }

    nonblock = (flags & MSG_DONTWAIT) != 0;

    if (socket_is_dgram(sock->type)) {
        peer = NULL;

        wait_queue_lock(&sock->wait);
        if (!sock->writes_open) {
            wait_queue_unlock(&sock->wait);
            errno = EPIPE;
            return -1;
        }
        if (sock->is_bound && sock->bound_name_len > 0) {
            sender_name_valid = true;
            sender_name_len = sock->bound_name_len;
            memcpy(sender_name, sock->bound_name, sender_name_len);
        }
        if (sock->peer) {
            peer = sock->peer;
            socket_retain_impl(peer);
        } else if (dest_addr) {
            if (socket_parse_unix_name(dest_addr, addrlen, name, &name_len) != 0) {
                wait_queue_unlock(&sock->wait);
                return -1;
            }
        } else if (sock->connected_name_valid) {
            name_len = sock->connected_name_len;
            memcpy(name, sock->connected_name, name_len);
        } else {
            wait_queue_unlock(&sock->wait);
            errno = EDESTADDRREQ;
            return -1;
        }
        wait_queue_unlock(&sock->wait);

        if (!peer) {
            peer = socket_lookup_named_peer(name, name_len);
            if (!peer) {
                return -1;
            }
        }

        wait_queue_lock(&peer->wait);
        if (peer->type != sock->type || peer->domain != sock->domain) {
            wait_queue_unlock(&peer->wait);
            socket_release_impl(peer);
            errno = EPROTOTYPE;
            return -1;
        }
        while (socket_datagram_space_locked(peer) < len) {
            if (!peer->writes_open) {
                wait_queue_unlock(&peer->wait);
                socket_release_impl(peer);
                errno = ECONNREFUSED;
                return -1;
            }
            if (nonblock) {
                wait_queue_unlock(&peer->wait);
                socket_release_impl(peer);
                errno = EAGAIN;
                return -1;
            }
            if (wait_queue_wait_locked_interruptible(&peer->wait) != 0) {
                wait_queue_unlock(&peer->wait);
                socket_release_impl(peer);
                errno = EINTR;
                return -1;
            }
        }
        to_write = socket_queue_datagram_locked(peer, buf, len, sender_name, sender_name_len, sender_name_valid);
        wait_queue_unlock(&peer->wait);
        socket_release_impl(peer);
        if (to_write >= 0) {
            poll_notify_readiness_impl();
        }
        return (__kernel_ssize_t)to_write;
    }

    wait_queue_lock(&sock->wait);
    if (!sock->writes_open) {
        wait_queue_unlock(&sock->wait);
        errno = EPIPE;
        return -1;
    }
    peer = sock->peer;
    if (!peer || !sock->peer_writes_open) {
        wait_queue_unlock(&sock->wait);
        signal_generate_task(get_current(), SIGPIPE);
        errno = EPIPE;
        return -1;
    }
    wait_queue_unlock(&sock->wait);

    wait_queue_lock(&peer->wait);
    while (socket_space_locked(peer) == 0) {
        if (!peer->peer_writes_open) {
            wait_queue_unlock(&peer->wait);
            signal_generate_task(get_current(), SIGPIPE);
            errno = EPIPE;
            return -1;
        }
        if (nonblock) {
            wait_queue_unlock(&peer->wait);
            errno = EAGAIN;
            return -1;
        }
        if (wait_queue_wait_locked_interruptible(&peer->wait) != 0) {
            wait_queue_unlock(&peer->wait);
            errno = EINTR;
            return -1;
        }
    }

    to_write = len < socket_space_locked(peer) ? len : socket_space_locked(peer);
    {
        size_t tail = (peer->head + peer->len) % ORLIX_SOCKET_BUFFER_SIZE;
        size_t first = to_write;
        if (first > ORLIX_SOCKET_BUFFER_SIZE - tail) {
            first = ORLIX_SOCKET_BUFFER_SIZE - tail;
        }
        memcpy(peer->buffer + tail, buf, first);
        if (first < to_write) {
            memcpy(peer->buffer, (const unsigned char *)buf + first, to_write - first);
        }
    }
    peer->len += to_write;
    socket_wake_all_locked(peer);
    wait_queue_unlock(&peer->wait);
    poll_notify_readiness_impl();
    return (__kernel_ssize_t)to_write;
}

__kernel_ssize_t socket_recvfrom_impl(struct socket_state *sock,
                                      void *buf,
                                      size_t len,
                                      int flags,
                                      struct sockaddr *src_addr,
                                      __u32 *addrlen) {
    bool nonblock;

    if (!sock) {
        errno = EBADF;
        return -1;
    }
    if (!buf && len > 0) {
        errno = EFAULT;
        return -1;
    }
    if (len == 0) {
        return 0;
    }

    nonblock = (flags & MSG_DONTWAIT) != 0;

    wait_queue_lock(&sock->wait);
    if (socket_is_dgram(sock->type)) {
        struct socket_datagram *msg;
        size_t to_read;
        unsigned char sender_name[ORLIX_UNIX_NAME_MAX];
        size_t sender_name_len = 0;
        bool sender_name_valid = false;

        while (!sock->dgram_head) {
            if (!sock->peer_writes_open && !sock->connected_name_valid && !sock->peer) {
                wait_queue_unlock(&sock->wait);
                return 0;
            }
            if (nonblock) {
                wait_queue_unlock(&sock->wait);
                errno = EAGAIN;
                return -1;
            }
            if (wait_queue_wait_locked_interruptible(&sock->wait) != 0) {
                wait_queue_unlock(&sock->wait);
                errno = EINTR;
                return -1;
            }
        }

        msg = sock->dgram_head;
        sock->dgram_head = msg->next;
        if (!sock->dgram_head) {
            sock->dgram_tail = NULL;
        }
        if (sock->dgram_count > 0) {
            sock->dgram_count--;
        }
        if (sock->dgram_bytes >= msg->len) {
            sock->dgram_bytes -= msg->len;
        } else {
            sock->dgram_bytes = 0;
        }
        sender_name_valid = msg->sender_name_valid;
        sender_name_len = msg->sender_name_len;
        if (sender_name_valid && sender_name_len > 0) {
            memcpy(sender_name, msg->sender_name, sender_name_len);
        }
        to_read = len < msg->len ? len : msg->len;
        memcpy(buf, msg->data, to_read);
        free(msg);
        socket_wake_all_locked(sock);
        wait_queue_unlock(&sock->wait);
        if (src_addr || addrlen) {
            if (socket_copy_name_out(sender_name, sender_name_valid ? sender_name_len : 0, src_addr, addrlen) != 0) {
                return -1;
            }
        }
        poll_notify_readiness_impl();
        return (__kernel_ssize_t)to_read;
    }

    while (sock->len == 0) {
        if (!sock->peer_writes_open) {
            wait_queue_unlock(&sock->wait);
            return 0;
        }
        if (nonblock) {
            wait_queue_unlock(&sock->wait);
            errno = EAGAIN;
            return -1;
        }
        if (wait_queue_wait_locked_interruptible(&sock->wait) != 0) {
            wait_queue_unlock(&sock->wait);
            errno = EINTR;
            return -1;
        }
    }

    {
        size_t to_read = len < sock->len ? len : sock->len;
        size_t first = to_read;
        if (first > ORLIX_SOCKET_BUFFER_SIZE - sock->head) {
            first = ORLIX_SOCKET_BUFFER_SIZE - sock->head;
        }
        memcpy(buf, sock->buffer + sock->head, first);
        if (first < to_read) {
            memcpy((unsigned char *)buf + first, sock->buffer, to_read - first);
        }
        sock->head = (sock->head + to_read) % ORLIX_SOCKET_BUFFER_SIZE;
        sock->len -= to_read;
        socket_wake_all_locked(sock);
        wait_queue_unlock(&sock->wait);
        poll_notify_readiness_impl();
        return (__kernel_ssize_t)to_read;
    }
}

__kernel_ssize_t socket_send_impl(struct socket_state *sock, const void *buf, size_t len, int flags) {
    return socket_sendto_impl(sock, buf, len, flags, NULL, 0);
}

__kernel_ssize_t socket_recv_impl(struct socket_state *sock, void *buf, size_t len, int flags) {
    return socket_recvfrom_impl(sock, buf, len, flags, NULL, NULL);
}

short socket_poll_revents_impl(struct socket_state *sock, short events) {
    short revents = 0;

    if (!sock) {
        return POLLNVAL;
    }

    wait_queue_lock(&sock->wait);
    if ((events & (POLLIN | POLLRDNORM)) &&
        ((socket_is_dgram(sock->type) && sock->dgram_head != NULL) ||
         (socket_is_stream(sock->type) && sock->len > 0))) {
        revents |= events & (POLLIN | POLLRDNORM);
    }
    if (!sock->peer_writes_open) {
        revents |= POLLHUP;
    }
    if ((events & (POLLOUT | POLLWRNORM)) && sock->writes_open) {
        revents |= events & (POLLOUT | POLLWRNORM);
    }
    wait_queue_unlock(&sock->wait);
    return revents;
}
