#include "socket.h"

#include <linux/errno.h>
#include <linux/atomic.h>
#include <linux/gfp_types.h>
#include <linux/string.h>

#include "internal/mutex.h"
#include "../signal.h"
#include "../task.h"
#include "../wait_queue.h"

#include <uapi/linux/poll.h>
#include <uapi/asm-generic/socket.h>
#include <asm-generic/signal.h>
#include <uapi/linux/socket.h>

extern void *__kmalloc_noprof(size_t size, gfp_t flags);
extern void kfree(const void *objp);

void poll_notify_readiness_impl(void);

static void *err_ptr_impl(long error) {
    return (void *)(intptr_t)error;
}

static long ptr_err_impl(const void *ptr) {
    return (long)(intptr_t)ptr;
}

static bool is_err_impl(const void *ptr) {
    long error = ptr_err_impl(ptr);
    return error < 0 && error >= -4095;
}

#define ERR_PTR(err) err_ptr_impl(err)
#define PTR_ERR(ptr) ptr_err_impl(ptr)
#define IS_ERR(ptr) is_err_impl(ptr)

#define SOCKET_BUFFER_SIZE 65536U
#define UNIX_NAME_MAX ((size_t)(MAX_PATH - 1))

struct socket_namespace_entry {
    struct socket_namespace_entry *next;
    unsigned char name[UNIX_NAME_MAX];
    size_t name_len;
    struct socket_state *sock;
};

struct socket_datagram {
    struct socket_datagram *next;
    size_t len;
    bool sender_name_valid;
    size_t sender_name_len;
    unsigned char sender_name[UNIX_NAME_MAX];
    unsigned char data[];
};

struct socket_state {
    atomic_t refs;
    unsigned long long id;
    int domain;
    int type;
    int protocol;
    bool datagram;
    struct socket_state *peer;
    bool peer_writes_open;
    bool writes_open;

    bool is_bound;
    bool namespace_registered;
    unsigned char bound_name[UNIX_NAME_MAX];
    size_t bound_name_len;
    bool peer_name_valid;
    unsigned char peer_name[UNIX_NAME_MAX];
    size_t peer_name_len;
    bool connected_name_valid;
    unsigned char connected_name[UNIX_NAME_MAX];
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

    unsigned char buffer[SOCKET_BUFFER_SIZE];
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
static atomic64_t next_socket_id = ATOMIC64_INIT(1);

static size_t socket_space_locked(const struct socket_state *sock) {
    return SOCKET_BUFFER_SIZE - sock->len;
}

static void socket_wake_all_locked(struct socket_state *sock) {
    wait_queue_wake_all_locked(&sock->wait);
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
    if (valid && name && name_len > 0 && name_len <= UNIX_NAME_MAX) {
        memcpy(sock->peer_name, name, name_len);
        sock->peer_name_len = name_len;
    }
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
    if (valid && name && name_len > 0 && name_len <= UNIX_NAME_MAX) {
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

    if (!sock || !name || name_len == 0 || name_len > UNIX_NAME_MAX) {
        return -EINVAL;
    }

    entry = __kmalloc_noprof(sizeof(*entry), GFP_KERNEL | __GFP_ZERO);
    if (!entry) {
        return -ENOMEM;
    }
    memcpy(entry->name, name, name_len);
    entry->name_len = name_len;
    entry->sock = sock;

    kernel_mutex_lock(&socket_namespace_lock);
    if (socket_namespace_lookup_locked(name, name_len)) {
        kernel_mutex_unlock(&socket_namespace_lock);
        kfree(entry);
        return -EADDRINUSE;
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

    if (!sock || !name || name_len == 0 || name_len > UNIX_NAME_MAX) {
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
            kfree(cur);
            return;
        }
        pp = &cur->next;
    }
    kernel_mutex_unlock(&socket_namespace_lock);
}

static struct socket_state *socket_lookup_named_peer(const unsigned char *name, size_t name_len) {
    struct socket_state *peer = NULL;

    if (!name || name_len == 0 || name_len > UNIX_NAME_MAX) {
        return ERR_PTR(-EINVAL);
    }

    kernel_mutex_lock(&socket_namespace_lock);
    peer = socket_namespace_lookup_locked(name, name_len);
    if (peer) {
        socket_retain_impl(peer);
    }
    kernel_mutex_unlock(&socket_namespace_lock);
    if (!peer) {
        return ERR_PTR(-ENOENT);
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
        kfree(msg);
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
        return -EBADF;
    }
    if (len > SOCKET_BUFFER_SIZE || len > (size_t)target->recvbuf) {
        return -EMSGSIZE;
    }
    if (!target->writes_open) {
        return -ECONNREFUSED;
    }
    if (socket_datagram_space_locked(target) < len) {
        return -EAGAIN;
    }

    msg = __kmalloc_noprof(sizeof(*msg) + len, GFP_KERNEL | __GFP_ZERO);
    if (!msg) {
        return -ENOMEM;
    }
    msg->len = len;
    msg->sender_name_valid = sender_name_valid;
    if (sender_name_valid && sender_name && sender_name_len > 0 && sender_name_len <= UNIX_NAME_MAX) {
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

struct socket_state *socket_create_impl(int domain, int type, int protocol, bool datagram) {
    struct socket_state *sock;

    sock = __kmalloc_noprof(sizeof(*sock), GFP_KERNEL | __GFP_ZERO);
    if (!sock) {
        return ERR_PTR(-ENOMEM);
    }

    atomic_set(&sock->refs, 1);
    sock->id = (unsigned long long)atomic64_inc_return(&next_socket_id);
    sock->domain = domain;
    sock->type = type;
    sock->protocol = protocol;
    sock->datagram = datagram;
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
    sock->sendbuf = (int)SOCKET_BUFFER_SIZE;
    sock->recvbuf = (int)SOCKET_BUFFER_SIZE;
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
                           bool datagram,
                           struct socket_state **a_out,
                           struct socket_state **b_out) {
    struct socket_state *a;
    struct socket_state *b;

    if (!a_out || !b_out) {
        return -EFAULT;
    }
    *a_out = NULL;
    *b_out = NULL;

    a = socket_create_impl(domain, type, protocol, datagram);
    b = socket_create_impl(domain, type, protocol, datagram);
    if (IS_ERR(a) || IS_ERR(b)) {
        int ret = IS_ERR(a) ? (int)PTR_ERR(a) : (int)PTR_ERR(b);
        if (!IS_ERR(a)) {
            socket_release_impl(a);
        }
        if (!IS_ERR(b)) {
            socket_release_impl(b);
        }
        return ret;
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
        atomic_inc(&sock->refs);
    }
}

void socket_release_impl(struct socket_state *sock) {
    struct socket_state *peer;
    unsigned char name[UNIX_NAME_MAX];
    size_t name_len = 0;
    bool was_bound = false;
    struct socket_state *accepted_head = NULL;

    if (!sock) {
        return;
    }
    if (atomic_dec_return(&sock->refs) != 0) {
        return;
    }

    wait_queue_lock(&sock->wait);
    sock->writes_open = false;
    peer = sock->peer;
    sock->peer = NULL;
    was_bound = sock->is_bound;
    if (was_bound) {
        name_len = sock->bound_name_len;
        if (name_len > 0 && name_len <= UNIX_NAME_MAX) {
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
    kfree(sock);
    poll_notify_readiness_impl();
}

int socket_shutdown_impl(struct socket_state *sock, bool shut_read, bool shut_write) {
    if (!sock) {
        return -EBADF;
    }

    if (!shut_read && !shut_write) {
        return -EINVAL;
    }

    wait_queue_lock(&sock->wait);
    if (shut_write) {
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

int socket_connect_impl(struct socket_state *sock, const unsigned char *name, size_t name_len) {
    struct socket_state *listener = NULL;
    struct socket_state *server_side = NULL;

    if (!sock || !name || name_len == 0 || name_len > UNIX_NAME_MAX) {
        return -EFAULT;
    }

    wait_queue_lock(&sock->wait);
    if (sock->peer) {
        wait_queue_unlock(&sock->wait);
        return -EISCONN;
    }
    if (sock->datagram) {
        struct socket_state *peer;

        wait_queue_unlock(&sock->wait);
        peer = socket_lookup_named_peer(name, name_len);
        if (IS_ERR(peer)) {
            return (int)PTR_ERR(peer);
        }
        wait_queue_lock(&peer->wait);
        if (peer->type != sock->type || peer->domain != sock->domain || !peer->is_bound) {
            wait_queue_unlock(&peer->wait);
            socket_release_impl(peer);
            return -EPROTOTYPE;
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
    if (IS_ERR(listener)) {
        return (int)PTR_ERR(listener);
    }

    wait_queue_lock(&listener->wait);
    if (!listener->is_listening) {
        wait_queue_unlock(&listener->wait);
        socket_release_impl(listener);
        return -ECONNREFUSED;
    }
    if (listener->backlog > 0 && listener->accept_queue_len >= listener->backlog) {
        wait_queue_unlock(&listener->wait);
        socket_release_impl(listener);
        return -EAGAIN;
    }
    wait_queue_unlock(&listener->wait);

    server_side = socket_create_impl(sock->domain, sock->type, sock->protocol, sock->datagram);
    if (IS_ERR(server_side)) {
        int ret = (int)PTR_ERR(server_side);
        socket_release_impl(listener);
        return ret;
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

int socket_bind_impl(struct socket_state *sock, const unsigned char *name, size_t name_len) {
    if (!sock || !name || name_len == 0 || name_len > UNIX_NAME_MAX) {
        return -EFAULT;
    }

    wait_queue_lock(&sock->wait);
    if (sock->is_bound) {
        wait_queue_unlock(&sock->wait);
        return -EINVAL;
    }
    memcpy(sock->bound_name, name, name_len);
    sock->bound_name_len = name_len;
    sock->is_bound = true;
    sock->namespace_registered = false;
    wait_queue_unlock(&sock->wait);

    {
        int ret = socket_namespace_register_impl(sock, sock->bound_name, sock->bound_name_len);
        if (ret != 0) {
        wait_queue_lock(&sock->wait);
        sock->is_bound = false;
        sock->bound_name_len = 0;
        wait_queue_unlock(&sock->wait);
        return ret;
        }
    }

    return 0;
}

int socket_listen_impl(struct socket_state *sock, int backlog) {
    if (!sock) {
        return -EBADF;
    }
    if (backlog < 0) {
        return -EINVAL;
    }

    wait_queue_lock(&sock->wait);
    if (sock->datagram) {
        wait_queue_unlock(&sock->wait);
        return -EOPNOTSUPP;
    }
    if (!sock->is_bound) {
        wait_queue_unlock(&sock->wait);
        return -EINVAL;
    }
    sock->is_listening = true;
    sock->backlog = backlog;
    socket_wake_all_locked(sock);
    wait_queue_unlock(&sock->wait);
    poll_notify_readiness_impl();
    return 0;
}

struct socket_state *socket_accept_impl(struct socket_state *sock, bool nonblock) {
    struct socket_state *accepted;

    if (!sock) {
        return ERR_PTR(-EBADF);
    }

    wait_queue_lock(&sock->wait);
    if (!sock->is_listening) {
        wait_queue_unlock(&sock->wait);
        return ERR_PTR(-EINVAL);
    }
    while (!sock->accept_queue_head) {
        if (nonblock) {
            wait_queue_unlock(&sock->wait);
            return ERR_PTR(-EAGAIN);
        }
        if (wait_queue_wait_locked_interruptible(&sock->wait) != 0) {
            wait_queue_unlock(&sock->wait);
            return ERR_PTR(-EINTR);
        }
        if (!sock->is_listening) {
            wait_queue_unlock(&sock->wait);
            return ERR_PTR(-EINVAL);
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

int socket_getsockname_impl(struct socket_state *sock, unsigned char *name_out, size_t *name_len_out) {
    size_t name_len = 0;

    if (!sock || !name_len_out) {
        return -EBADF;
    }

    wait_queue_lock(&sock->wait);
    if (sock->is_bound && sock->bound_name_len > 0) {
        name_len = sock->bound_name_len;
        if (name_out) {
            memcpy(name_out, sock->bound_name, name_len);
        }
    }
    wait_queue_unlock(&sock->wait);
    *name_len_out = name_len;
    return 0;
}

int socket_getpeername_impl(struct socket_state *sock, unsigned char *name_out, size_t *name_len_out) {
    size_t name_len = 0;
    bool valid;

    if (!sock || !name_len_out) {
        return -EBADF;
    }

    wait_queue_lock(&sock->wait);
    valid = sock->peer_name_valid;
    if (valid && sock->peer_name_len > 0) {
        name_len = sock->peer_name_len;
        if (name_out) {
            memcpy(name_out, sock->peer_name, name_len);
        }
    }
    wait_queue_unlock(&sock->wait);

    if (!valid) {
        return -ENOTCONN;
    }

    *name_len_out = name_len;
    return 0;
}

static int socket_copy_int_opt(void *optval, __u32 *optlen, int value) {
    int copy_value = value;
    __u32 copy_len;

    if (!optlen || !optval) {
        return -EFAULT;
    }
    if (*optlen < (__u32)sizeof(int)) {
        return -EINVAL;
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
        return -EBADF;
    }
    if (level != SOL_SOCKET) {
        return -ENOPROTOOPT;
    }

    wait_queue_lock(&sock->wait);
    switch (optname) {
    case SO_TYPE:
        value = sock->type;
        break;
    case SO_DOMAIN:
        value = sock->domain;
        break;
    case SO_PROTOCOL:
        value = sock->protocol;
        break;
    case SO_ACCEPTCONN:
        value = sock->is_listening ? 1 : 0;
        break;
    case SO_ERROR:
        value = sock->pending_error;
        sock->pending_error = 0;
        break;
    case SO_SNDBUF:
        value = sock->sendbuf;
        break;
    case SO_RCVBUF:
        value = sock->recvbuf;
        break;
    case SO_REUSEADDR:
        value = sock->reuseaddr ? 1 : 0;
        break;
    case SO_KEEPALIVE:
        value = sock->keepalive ? 1 : 0;
        break;
    default:
        wait_queue_unlock(&sock->wait);
        return -ENOPROTOOPT;
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
        return -EBADF;
    }
    if (level != SOL_SOCKET) {
        return -ENOPROTOOPT;
    }
    if (!optval || optlen < (__u32)sizeof(int)) {
        return -EINVAL;
    }

    memcpy(&value, optval, sizeof(value));
    wait_queue_lock(&sock->wait);
    switch (optname) {
    case SO_REUSEADDR:
        sock->reuseaddr = value != 0;
        break;
    case SO_KEEPALIVE:
        sock->keepalive = value != 0;
        break;
    case SO_SNDBUF:
        if (value <= 0) {
            wait_queue_unlock(&sock->wait);
            return -EINVAL;
        }
        sock->sendbuf = value > INT_MAX / 2 ? INT_MAX / 2 : value;
        break;
    case SO_RCVBUF:
        if (value <= 0) {
            wait_queue_unlock(&sock->wait);
            return -EINVAL;
        }
        sock->recvbuf = value > INT_MAX / 2 ? INT_MAX / 2 : value;
        break;
    default:
        wait_queue_unlock(&sock->wait);
        return -ENOPROTOOPT;
    }
    wait_queue_unlock(&sock->wait);
    return 0;
}

__kernel_ssize_t socket_sendto_impl(struct socket_state *sock,
                                    const void *buf,
                                    size_t len,
                                    bool nonblock,
                                    const unsigned char *dest_name,
                                    size_t dest_name_len,
                                    bool has_dest_name) {
    struct socket_state *peer;
    size_t to_write;
    bool sender_name_valid = false;
    unsigned char sender_name[UNIX_NAME_MAX];
    size_t sender_name_len = 0;
    unsigned char name[UNIX_NAME_MAX];
    size_t name_len = 0;

    if (!sock) {
        return -EBADF;
    }
    if (!buf && len > 0) {
        return -EFAULT;
    }
    if (len == 0) {
        return 0;
    }

    if (sock->datagram) {
        peer = NULL;

        wait_queue_lock(&sock->wait);
        if (!sock->writes_open) {
            wait_queue_unlock(&sock->wait);
            return -EPIPE;
        }
        if (sock->is_bound && sock->bound_name_len > 0) {
            sender_name_valid = true;
            sender_name_len = sock->bound_name_len;
            memcpy(sender_name, sock->bound_name, sender_name_len);
        }
        if (sock->peer) {
            peer = sock->peer;
            socket_retain_impl(peer);
        } else if (has_dest_name) {
            name_len = dest_name_len;
            memcpy(name, dest_name, dest_name_len);
        } else if (sock->connected_name_valid) {
            name_len = sock->connected_name_len;
            memcpy(name, sock->connected_name, name_len);
        } else {
            wait_queue_unlock(&sock->wait);
            return -EDESTADDRREQ;
        }
        wait_queue_unlock(&sock->wait);

        if (!peer) {
            peer = socket_lookup_named_peer(name, name_len);
            if (IS_ERR(peer)) {
                return PTR_ERR(peer);
            }
        }

        wait_queue_lock(&peer->wait);
        if (peer->type != sock->type || peer->domain != sock->domain) {
            wait_queue_unlock(&peer->wait);
            socket_release_impl(peer);
            return -EPROTOTYPE;
        }
        while (socket_datagram_space_locked(peer) < len) {
            if (!peer->writes_open) {
                wait_queue_unlock(&peer->wait);
                socket_release_impl(peer);
                return -ECONNREFUSED;
            }
            if (nonblock) {
                wait_queue_unlock(&peer->wait);
                socket_release_impl(peer);
                return -EAGAIN;
            }
            if (wait_queue_wait_locked_interruptible(&peer->wait) != 0) {
                wait_queue_unlock(&peer->wait);
                socket_release_impl(peer);
                return -EINTR;
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
        return -EPIPE;
    }
    peer = sock->peer;
    if (!peer || !sock->peer_writes_open) {
        wait_queue_unlock(&sock->wait);
        signal_generate_task(get_current(), SIGPIPE);
        return -EPIPE;
    }
    wait_queue_unlock(&sock->wait);

    wait_queue_lock(&peer->wait);
    while (socket_space_locked(peer) == 0) {
        if (!peer->peer_writes_open) {
            wait_queue_unlock(&peer->wait);
            signal_generate_task(get_current(), SIGPIPE);
            return -EPIPE;
        }
        if (nonblock) {
            wait_queue_unlock(&peer->wait);
            return -EAGAIN;
        }
        if (wait_queue_wait_locked_interruptible(&peer->wait) != 0) {
            wait_queue_unlock(&peer->wait);
            return -EINTR;
        }
    }

    to_write = len < socket_space_locked(peer) ? len : socket_space_locked(peer);
    {
        size_t tail = (peer->head + peer->len) % SOCKET_BUFFER_SIZE;
        size_t first = to_write;
        if (first > SOCKET_BUFFER_SIZE - tail) {
            first = SOCKET_BUFFER_SIZE - tail;
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
                                      bool nonblock,
                                      unsigned char *src_name_out,
                                      size_t *src_name_len_out,
                                      bool *has_src_name_out) {

    if (!sock) {
        return -EBADF;
    }
    if (!buf && len > 0) {
        return -EFAULT;
    }
    if (len == 0) {
        return 0;
    }

    wait_queue_lock(&sock->wait);
    if (sock->datagram) {
        struct socket_datagram *msg;
        size_t to_read;
        unsigned char sender_name[UNIX_NAME_MAX];
        size_t sender_name_len = 0;
        bool sender_name_valid = false;

        while (!sock->dgram_head) {
            if (!sock->peer_writes_open && !sock->connected_name_valid && !sock->peer) {
                wait_queue_unlock(&sock->wait);
                return 0;
            }
            if (nonblock) {
                wait_queue_unlock(&sock->wait);
                return -EAGAIN;
            }
            if (wait_queue_wait_locked_interruptible(&sock->wait) != 0) {
                wait_queue_unlock(&sock->wait);
                return -EINTR;
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
        kfree(msg);
        socket_wake_all_locked(sock);
        wait_queue_unlock(&sock->wait);
        if (src_name_out && sender_name_valid && sender_name_len > 0) {
            memcpy(src_name_out, sender_name, sender_name_len);
        }
        if (src_name_len_out) {
            *src_name_len_out = sender_name_valid ? sender_name_len : 0;
        }
        if (has_src_name_out) {
            *has_src_name_out = sender_name_valid;
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
            return -EAGAIN;
        }
        if (wait_queue_wait_locked_interruptible(&sock->wait) != 0) {
            wait_queue_unlock(&sock->wait);
            return -EINTR;
        }
    }

    {
        size_t to_read = len < sock->len ? len : sock->len;
        size_t first = to_read;
        if (first > SOCKET_BUFFER_SIZE - sock->head) {
            first = SOCKET_BUFFER_SIZE - sock->head;
        }
        memcpy(buf, sock->buffer + sock->head, first);
        if (first < to_read) {
            memcpy((unsigned char *)buf + first, sock->buffer, to_read - first);
        }
        sock->head = (sock->head + to_read) % SOCKET_BUFFER_SIZE;
        sock->len -= to_read;
        socket_wake_all_locked(sock);
        wait_queue_unlock(&sock->wait);
        poll_notify_readiness_impl();
        return (__kernel_ssize_t)to_read;
    }
}

__kernel_ssize_t socket_send_impl(struct socket_state *sock,
                                  const void *buf,
                                  size_t len,
                                  bool nonblock) {
    return socket_sendto_impl(sock, buf, len, nonblock, NULL, 0, false);
}

__kernel_ssize_t socket_recv_impl(struct socket_state *sock,
                                  void *buf,
                                  size_t len,
                                  bool nonblock) {
    return socket_recvfrom_impl(sock, buf, len, nonblock, NULL, NULL, NULL);
}

short socket_poll_revents_impl(struct socket_state *sock, short events) {
    short revents = 0;

    if (!sock) {
        return POLLNVAL;
    }

    wait_queue_lock(&sock->wait);
    if ((events & (POLLIN | POLLRDNORM)) &&
        ((sock->datagram && sock->dgram_head != NULL) ||
         (!sock->datagram && sock->len > 0))) {
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
