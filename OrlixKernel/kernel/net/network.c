/* Orlix - Linux-owned socket syscall surface.
 *
 * Hard rule: no host frameworks or dispatch usage in Linux-owner code.
 * Socket semantics are owned by kernel/net and participate in the virtual fd +
 * readiness infrastructure.
 */

#include <errno.h>
#include <linux/fcntl.h>
#include <linux/net.h>
#include <linux/poll.h>
#include <linux/socket.h>
#include <linux/time_types.h>
#include <linux/uio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "internal/private/kernel_socket_compat.h"
#include "socket.h"
#include "fs/fdtable.h"

#ifndef SOCK_TYPE_MASK
#define SOCK_TYPE_MASK 0xf
#endif
#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC O_CLOEXEC
#endif
#ifndef SOCK_NONBLOCK
#define SOCK_NONBLOCK O_NONBLOCK
#endif

struct linux_mmsghdr {
    struct msghdr msg_hdr;
    unsigned int msg_len;
};

static int socket_flags_from_type(int type, int *base_type_out, int *fd_flags_out) {
    int base_type;
    int fd_flags = 0;
    int type_flags;

    if (!base_type_out || !fd_flags_out) {
        errno = EINVAL;
        return -1;
    }

    base_type = type & SOCK_TYPE_MASK;
    type_flags = type & ~SOCK_TYPE_MASK;
    if (type_flags & ~(SOCK_NONBLOCK | SOCK_CLOEXEC)) {
        errno = EINVAL;
        return -1;
    }
    if (type_flags & SOCK_NONBLOCK) {
        fd_flags |= O_NONBLOCK;
    }
    if (type_flags & SOCK_CLOEXEC) {
        fd_flags |= O_CLOEXEC;
    }

    *base_type_out = base_type;
    *fd_flags_out = O_RDWR | fd_flags;
    return 0;
}

static int socket_accept_flags(int flags, int *fd_flags_out) {
    if (!fd_flags_out) {
        errno = EINVAL;
        return -1;
    }
    if (flags & ~(O_NONBLOCK | O_CLOEXEC)) {
        errno = EINVAL;
        return -1;
    }
    *fd_flags_out = O_RDWR | (flags & (O_NONBLOCK | O_CLOEXEC));
    return 0;
}

static int fd_get_socket(int fd, struct socket_state **sock_out) {
    void *entry;
    struct socket_state *sock;

    if (!sock_out) {
        errno = EFAULT;
        return -1;
    }
    *sock_out = NULL;

    if (fd < 0 || fd >= NR_OPEN_DEFAULT) {
        errno = EBADF;
        return -1;
    }

    entry = get_fd_entry_impl(fd);
    if (!entry) {
        errno = EBADF;
        return -1;
    }

    if (!get_fd_is_socket_impl(entry)) {
        put_fd_entry_impl(entry);
        errno = ENOTSOCK;
        return -1;
    }

    sock = get_fd_socket_impl(entry);
    if (!sock) {
        put_fd_entry_impl(entry);
        errno = ENOTSOCK;
        return -1;
    }

    socket_retain_impl(sock);
    put_fd_entry_impl(entry);
    *sock_out = sock;
    return 0;
}

__attribute__((visibility("default"))) int socket(int domain, int type, int protocol) {
    int base_type;
    int flags;
    struct socket_state *sock;
    int fd;

    if (socket_flags_from_type(type, &base_type, &flags) != 0) {
        return -1;
    }

    sock = socket_create_impl(domain, base_type, protocol);
    if (!sock) {
        return -1;
    }

    fd = alloc_fd_impl();
    if (fd < 0) {
        socket_release_impl(sock);
        return -1;
    }

    if (init_socket_fd_entry_impl(fd, flags, sock) != 0) {
        free_fd_impl(fd);
        socket_release_impl(sock);
        return -1;
    }

    return fd;
}

__attribute__((visibility("default"))) int socketpair(int domain, int type, int protocol, int sv[2]) {
    int base_type;
    int flags;
    struct socket_state *a = NULL;
    struct socket_state *b = NULL;
    int fd0 = -1;
    int fd1 = -1;

    if (!sv) {
        errno = EFAULT;
        return -1;
    }

    if (socket_flags_from_type(type, &base_type, &flags) != 0) {
        return -1;
    }

    if (socketpair_create_impl(domain, base_type, protocol, &a, &b) != 0) {
        return -1;
    }

    fd0 = alloc_fd_impl();
    if (fd0 < 0) {
        goto fail;
    }
    if (init_socket_fd_entry_impl(fd0, flags, a) != 0) {
        free_fd_impl(fd0);
        fd0 = -1;
        goto fail;
    }

    fd1 = alloc_fd_impl();
    if (fd1 < 0) {
        goto fail;
    }
    if (init_socket_fd_entry_impl(fd1, flags, b) != 0) {
        free_fd_impl(fd1);
        fd1 = -1;
        goto fail;
    }

    sv[0] = fd0;
    sv[1] = fd1;
    return 0;

fail:
    if (fd0 >= 0) {
        close_impl(fd0);
    }
    if (fd1 >= 0) {
        close_impl(fd1);
    }
    socket_release_impl(a);
    socket_release_impl(b);
    return -1;
}

__attribute__((visibility("default"))) int connect(int sockfd, const struct sockaddr *addr, __u32 addrlen) {
    struct socket_state *sock;
    int ret;

    if (fd_get_socket(sockfd, &sock) != 0) {
        return -1;
    }
    ret = socket_connect_impl(sock, addr, addrlen);
    socket_release_impl(sock);
    return ret;
}

__attribute__((visibility("default"))) int bind(int sockfd, const struct sockaddr *addr, __u32 addrlen) {
    struct socket_state *sock;
    int ret;

    if (fd_get_socket(sockfd, &sock) != 0) {
        return -1;
    }
    ret = socket_bind_impl(sock, addr, addrlen);
    socket_release_impl(sock);
    return ret;
}

__attribute__((visibility("default"))) int listen(int sockfd, int backlog) {
    struct socket_state *sock;
    int ret;

    if (fd_get_socket(sockfd, &sock) != 0) {
        return -1;
    }
    ret = socket_listen_impl(sock, backlog);
    socket_release_impl(sock);
    return ret;
}

__attribute__((visibility("default"))) int accept(int sockfd, struct sockaddr *addr, __u32 *addrlen) {
    struct socket_state *sock;
    struct socket_state *accepted;
    int fd;
    int fd_flags;

    if (fd_get_socket(sockfd, &sock) != 0) {
        return -1;
    }

    accepted = socket_accept_impl(sock, addr, addrlen, 0);
    socket_release_impl(sock);
    if (!accepted) {
        return -1;
    }

    fd = alloc_fd_impl();
    if (fd < 0) {
        socket_release_impl(accepted);
        return -1;
    }
    if (socket_accept_flags(0, &fd_flags) != 0) {
        free_fd_impl(fd);
        socket_release_impl(accepted);
        return -1;
    }
    if (init_socket_fd_entry_impl(fd, fd_flags, accepted) != 0) {
        free_fd_impl(fd);
        socket_release_impl(accepted);
        return -1;
    }
    return fd;
}

__attribute__((visibility("default"))) int accept4(int sockfd, struct sockaddr *addr, __u32 *addrlen, int flags) {
    struct socket_state *sock;
    struct socket_state *accepted;
    int fd;
    int fd_flags;

    if (socket_accept_flags(flags, &fd_flags) != 0) {
        return -1;
    }

    if (fd_get_socket(sockfd, &sock) != 0) {
        return -1;
    }

    accepted = socket_accept_impl(sock, addr, addrlen, flags);
    socket_release_impl(sock);
    if (!accepted) {
        return -1;
    }

    fd = alloc_fd_impl();
    if (fd < 0) {
        socket_release_impl(accepted);
        return -1;
    }
    if (init_socket_fd_entry_impl(fd, fd_flags, accepted) != 0) {
        free_fd_impl(fd);
        socket_release_impl(accepted);
        return -1;
    }
    return fd;
}

__attribute__((visibility("default"))) __kernel_ssize_t send(int sockfd, const void *buf, size_t len, int flags) {
    struct socket_state *sock;
    __kernel_ssize_t ret;

    if (fd_get_socket(sockfd, &sock) != 0) {
        return -1;
    }
    ret = socket_send_impl(sock, buf, len, flags);
    socket_release_impl(sock);
    return ret;
}

__attribute__((visibility("default"))) __kernel_ssize_t recv(int sockfd, void *buf, size_t len, int flags) {
    struct socket_state *sock;
    __kernel_ssize_t ret;

    if (fd_get_socket(sockfd, &sock) != 0) {
        return -1;
    }
    ret = socket_recv_impl(sock, buf, len, flags);
    socket_release_impl(sock);
    return ret;
}

__attribute__((visibility("default"))) __kernel_ssize_t sendto(int sockfd,
                                                               const void *buf,
                                                               size_t len,
                                                               int flags,
                                                               const struct sockaddr *dest_addr,
                                                               __u32 addrlen) {
    struct socket_state *sock;
    __kernel_ssize_t ret;

    if (fd_get_socket(sockfd, &sock) != 0) {
        return -1;
    }
    ret = socket_sendto_impl(sock, buf, len, flags, dest_addr, addrlen);
    socket_release_impl(sock);
    return ret;
}

__attribute__((visibility("default"))) __kernel_ssize_t recvfrom(int sockfd,
                                                                 void *buf,
                                                                 size_t len,
                                                                 int flags,
                                                                 struct sockaddr *src_addr,
                                                                 __u32 *addrlen) {
    struct socket_state *sock;
    __kernel_ssize_t ret;

    if (fd_get_socket(sockfd, &sock) != 0) {
        return -1;
    }
    ret = socket_recvfrom_impl(sock, buf, len, flags, src_addr, addrlen);
    socket_release_impl(sock);
    return ret;
}

__attribute__((visibility("default"))) __kernel_ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags) {
    size_t total = 0;
    size_t offset = 0;
    char *tmp;
    __kernel_ssize_t ret;

    if (!msg) {
        errno = EFAULT;
        return -1;
    }
    if (msg->msg_iovlen < 0) {
        errno = EINVAL;
        return -1;
    }

    for (int i = 0; i < msg->msg_iovlen; i++) {
        if (msg->msg_iov[i].iov_len > 0 && msg->msg_iov[i].iov_base) {
            total += msg->msg_iov[i].iov_len;
        }
    }

    if (total == 0) {
        return 0;
    }

    tmp = malloc(total);
    if (!tmp) {
        errno = ENOMEM;
        return -1;
    }

    for (int i = 0; i < msg->msg_iovlen; i++) {
        if (msg->msg_iov[i].iov_len > 0 && msg->msg_iov[i].iov_base) {
            memcpy(tmp + offset, msg->msg_iov[i].iov_base, msg->msg_iov[i].iov_len);
            offset += msg->msg_iov[i].iov_len;
        }
    }

    ret = sendto(sockfd, tmp, offset, flags, (const struct sockaddr *)msg->msg_name, (__u32)msg->msg_namelen);
    free(tmp);
    return ret;
}

__attribute__((visibility("default"))) __kernel_ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags) {
    size_t total = 0;
    size_t remaining;
    size_t offset = 0;
    char *tmp;
    __kernel_ssize_t nread;

    if (!msg) {
        errno = EFAULT;
        return -1;
    }
    if (msg->msg_iovlen < 0) {
        errno = EINVAL;
        return -1;
    }

    for (int i = 0; i < msg->msg_iovlen; i++) {
        if (msg->msg_iov[i].iov_len > 0 && msg->msg_iov[i].iov_base) {
            total += msg->msg_iov[i].iov_len;
        }
    }

    if (total == 0) {
        return 0;
    }

    tmp = malloc(total);
    if (!tmp) {
        errno = ENOMEM;
        return -1;
    }

    nread = recvfrom(sockfd,
                     tmp,
                     total,
                     flags,
                     (struct sockaddr *)msg->msg_name,
                     (__u32 *)&msg->msg_namelen);
    if (nread <= 0) {
        free(tmp);
        return nread;
    }

    remaining = (size_t)nread;
    for (int i = 0; i < msg->msg_iovlen && remaining > 0; i++) {
        size_t to_copy;

        if (msg->msg_iov[i].iov_len == 0 || !msg->msg_iov[i].iov_base) {
            continue;
        }

        to_copy = msg->msg_iov[i].iov_len;
        if (to_copy > remaining) {
            to_copy = remaining;
        }
        memcpy(msg->msg_iov[i].iov_base, tmp + offset, to_copy);
        offset += to_copy;
        remaining -= to_copy;
    }

    free(tmp);
    msg->msg_controllen = 0;
    msg->msg_flags = 0;
    return nread;
}

__attribute__((visibility("default"))) int sendmmsg(int sockfd,
                                                    struct linux_mmsghdr *msgvec,
                                                    unsigned int vlen,
                                                    int flags) {
    unsigned int sent = 0;

    if (!msgvec) {
        errno = EFAULT;
        return -1;
    }
    if (vlen == 0) {
        errno = EINVAL;
        return -1;
    }

    for (sent = 0; sent < vlen; sent++) {
        __kernel_ssize_t ret = sendmsg(sockfd, &msgvec[sent].msg_hdr, flags);
        if (ret < 0) {
            return sent > 0 ? (int)sent : -1;
        }
        msgvec[sent].msg_len = (unsigned int)ret;
    }
    return (int)sent;
}

__attribute__((visibility("default"))) int recvmmsg(int sockfd,
                                                    struct linux_mmsghdr *msgvec,
                                                    unsigned int vlen,
                                                    int flags,
                                                    struct __kernel_timespec *timeout) {
    unsigned int received = 0;

    if (!msgvec) {
        errno = EFAULT;
        return -1;
    }
    if (vlen == 0) {
        errno = EINVAL;
        return -1;
    }
    if (timeout) {
        errno = EOPNOTSUPP;
        return -1;
    }

    for (received = 0; received < vlen; received++) {
        int recv_flags = flags;
        __kernel_ssize_t ret;

        if (received > 0) {
            recv_flags |= MSG_DONTWAIT;
        }
        ret = recvmsg(sockfd, &msgvec[received].msg_hdr, recv_flags);
        if (ret < 0) {
            if (received > 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                return (int)received;
            }
            return received > 0 ? (int)received : -1;
        }
        msgvec[received].msg_len = (unsigned int)ret;
    }
    return (int)received;
}

__attribute__((visibility("default"))) int shutdown(int sockfd, int how) {
    struct socket_state *sock;
    int ret;

    if (fd_get_socket(sockfd, &sock) != 0) {
        return -1;
    }
    ret = socket_shutdown_impl(sock, how);
    socket_release_impl(sock);
    return ret;
}

__attribute__((visibility("default"))) int getsockopt(int sockfd,
                                                      int level,
                                                      int optname,
                                                      void *optval,
                                                      __u32 *optlen) {
    struct socket_state *sock;
    int ret;

    if (fd_get_socket(sockfd, &sock) != 0) {
        return -1;
    }
    ret = socket_getsockopt_impl(sock, level, optname, optval, optlen);
    socket_release_impl(sock);
    return ret;
}

__attribute__((visibility("default"))) int setsockopt(int sockfd,
                                                      int level,
                                                      int optname,
                                                      const void *optval,
                                                      __u32 optlen) {
    struct socket_state *sock;
    int ret;

    if (fd_get_socket(sockfd, &sock) != 0) {
        return -1;
    }
    ret = socket_setsockopt_impl(sock, level, optname, optval, optlen);
    socket_release_impl(sock);
    return ret;
}

__attribute__((visibility("default"))) int getsockname(int sockfd, struct sockaddr *addr, __u32 *addrlen) {
    struct socket_state *sock;
    int ret;

    if (fd_get_socket(sockfd, &sock) != 0) {
        return -1;
    }
    ret = socket_getsockname_impl(sock, addr, addrlen);
    socket_release_impl(sock);
    return ret;
}

__attribute__((visibility("default"))) int getpeername(int sockfd, struct sockaddr *addr, __u32 *addrlen) {
    struct socket_state *sock;
    int ret;

    if (fd_get_socket(sockfd, &sock) != 0) {
        return -1;
    }
    ret = socket_getpeername_impl(sock, addr, addrlen);
    socket_release_impl(sock);
    return ret;
}
