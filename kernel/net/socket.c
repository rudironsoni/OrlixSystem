#include "socket.h"

#ifdef SIGPIPE
#undef SIGPIPE
#endif
#define __ASSEMBLY__ 1
#include <asm-generic/signal.h>
#undef __ASSEMBLY__

#include <errno.h>
#include <linux/poll.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "../signal.h"
#include "../task.h"
#include "../wait_queue.h"

void poll_notify_readiness_impl(void);

#define IXLAND_SOCKET_BUFFER_SIZE 65536U

struct ix_socket {
    atomic_int refs;
    int domain;
    int type;
    int protocol;

    struct ix_socket *peer;
    bool peer_writes_open;
    bool writes_open;

    unsigned char buffer[IXLAND_SOCKET_BUFFER_SIZE];
    size_t head;
    size_t len;
    struct wait_queue_head wait;
};

static size_t socket_space_locked(const struct ix_socket *sock) {
    return IXLAND_SOCKET_BUFFER_SIZE - sock->len;
}

static void socket_wake_all_locked(struct ix_socket *sock) {
    wait_queue_wake_all_locked(&sock->wait);
}

static bool socket_is_stream(int type) { return type == SOCK_STREAM; }

struct ix_socket *ix_socket_create_impl(int domain, int type, int protocol) {
    struct ix_socket *sock;

    if (domain != AF_UNIX) {
        errno = EAFNOSUPPORT;
        return NULL;
    }
    if (!socket_is_stream(type)) {
        errno = EPROTOTYPE;
        return NULL;
    }

    sock = calloc(1, sizeof(*sock));
    if (!sock) {
        errno = ENOMEM;
        return NULL;
    }

    atomic_init(&sock->refs, 1);
    sock->domain = domain;
    sock->type = type;
    sock->protocol = protocol;
    sock->peer = NULL;
    sock->peer_writes_open = false;
    sock->writes_open = true;
    sock->head = 0;
    sock->len = 0;
    wait_queue_init(&sock->wait);
    return sock;
}

int ix_socketpair_create_impl(int domain, int type, int protocol, struct ix_socket **a_out, struct ix_socket **b_out) {
    struct ix_socket *a;
    struct ix_socket *b;

    if (!a_out || !b_out) {
        errno = EFAULT;
        return -1;
    }
    *a_out = NULL;
    *b_out = NULL;

    a = ix_socket_create_impl(domain, type, protocol);
    b = ix_socket_create_impl(domain, type, protocol);
    if (!a || !b) {
        ix_socket_release_impl(a);
        ix_socket_release_impl(b);
        return -1;
    }

    a->peer = b;
    b->peer = a;
    a->peer_writes_open = true;
    b->peer_writes_open = true;

    *a_out = a;
    *b_out = b;
    return 0;
}

void ix_socket_retain_impl(struct ix_socket *sock) {
    if (sock) {
        atomic_fetch_add(&sock->refs, 1);
    }
}

void ix_socket_release_impl(struct ix_socket *sock) {
    struct ix_socket *peer;

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
    socket_wake_all_locked(sock);
    wait_queue_unlock(&sock->wait);

    if (peer) {
        wait_queue_lock(&peer->wait);
        peer->peer_writes_open = false;
        socket_wake_all_locked(peer);
        wait_queue_unlock(&peer->wait);
        poll_notify_readiness_impl();
    }

    wait_queue_destroy(&sock->wait);
    free(sock);
    poll_notify_readiness_impl();
}

int ix_socket_shutdown_impl(struct ix_socket *sock, int how) {
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

int ix_socket_connect_impl(struct ix_socket *sock, const struct sockaddr *addr, socklen_t addrlen) {
    (void)sock;
    (void)addr;
    (void)addrlen;
    errno = EOPNOTSUPP;
    return -1;
}

int ix_socket_bind_impl(struct ix_socket *sock, const struct sockaddr *addr, socklen_t addrlen) {
    (void)sock;
    (void)addr;
    (void)addrlen;
    errno = EOPNOTSUPP;
    return -1;
}

int ix_socket_listen_impl(struct ix_socket *sock, int backlog) {
    (void)sock;
    (void)backlog;
    errno = EOPNOTSUPP;
    return -1;
}

struct ix_socket *ix_socket_accept_impl(struct ix_socket *sock, struct sockaddr *addr, socklen_t *addrlen, int flags) {
    (void)sock;
    (void)addr;
    (void)addrlen;
    (void)flags;
    errno = EOPNOTSUPP;
    return NULL;
}

ssize_t ix_socket_send_impl(struct ix_socket *sock, const void *buf, size_t len, int flags) {
    struct ix_socket *peer;
    bool nonblock;
    size_t space;
    size_t to_write;
    size_t tail;
    size_t first;

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
    space = socket_space_locked(peer);
    while (space == 0) {
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
        space = socket_space_locked(peer);
    }

    to_write = len < space ? len : space;
    tail = (peer->head + peer->len) % IXLAND_SOCKET_BUFFER_SIZE;
    first = to_write;
    if (first > IXLAND_SOCKET_BUFFER_SIZE - tail) {
        first = IXLAND_SOCKET_BUFFER_SIZE - tail;
    }
    memcpy(peer->buffer + tail, buf, first);
    if (first < to_write) {
        memcpy(peer->buffer, (const unsigned char *)buf + first, to_write - first);
    }
    peer->len += to_write;
    socket_wake_all_locked(peer);
    wait_queue_unlock(&peer->wait);
    poll_notify_readiness_impl();
    return (ssize_t)to_write;
}

ssize_t ix_socket_recv_impl(struct ix_socket *sock, void *buf, size_t len, int flags) {
    bool nonblock;
    size_t to_read;
    size_t first;

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

    to_read = len < sock->len ? len : sock->len;
    first = to_read;
    if (first > IXLAND_SOCKET_BUFFER_SIZE - sock->head) {
        first = IXLAND_SOCKET_BUFFER_SIZE - sock->head;
    }
    memcpy(buf, sock->buffer + sock->head, first);
    if (first < to_read) {
        memcpy((unsigned char *)buf + first, sock->buffer, to_read - first);
    }
    sock->head = (sock->head + to_read) % IXLAND_SOCKET_BUFFER_SIZE;
    sock->len -= to_read;
    socket_wake_all_locked(sock);
    wait_queue_unlock(&sock->wait);
    poll_notify_readiness_impl();
    return (ssize_t)to_read;
}

short ix_socket_poll_revents_impl(struct ix_socket *sock, short events) {
    short revents = 0;

    if (!sock) {
        return POLLNVAL;
    }

    wait_queue_lock(&sock->wait);
    if ((events & (POLLIN | POLLRDNORM)) && sock->len > 0) {
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
