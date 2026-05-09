/* Orlix - Linux-owned socket syscall surface.
 *
 * Hard rule: no host frameworks or dispatch usage in Linux-owner code.
 * Socket semantics are owned by kernel/net and participate in the virtual fd +
 * readiness infrastructure.
 */

#include <linux/socket.h>
#include <linux/net.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/un.h>
#include <stdbool.h>

#include "socket.h"
#include "fs/fdtable.h"

extern int *__error(void);
extern void *malloc(size_t);
extern void free(void *);
extern void *memcpy(void *, const void *, size_t);
extern void *memset(void *, int, size_t);

#define errno (*__error())
#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC O_CLOEXEC
#endif
#ifndef SOCK_NONBLOCK
#define SOCK_NONBLOCK O_NONBLOCK
#endif

static size_t socket_unix_name_len(size_t name_len) {
    if (name_len == 0) {
        return offsetof(struct sockaddr_un, sun_path);
    }
    return offsetof(struct sockaddr_un, sun_path) + 1 + name_len;
}

static int socket_parse_unix_name(const struct sockaddr *addr,
                                  __kernel_size_t addrlen,
                                  unsigned char *name_out,
                                  size_t *name_len_out) {
    const struct sockaddr_un *un;
    size_t sun_path_len;
    size_t name_len;

    if (!addr || !name_out || !name_len_out) {
        errno = EFAULT;
        return -1;
    }
    if (addrlen < offsetof(struct sockaddr_un, sun_path) + 1) {
        errno = EINVAL;
        return -1;
    }

    un = (const struct sockaddr_un *)addr;
    if (un->sun_family != AF_UNIX) {
        errno = EAFNOSUPPORT;
        return -1;
    }

    sun_path_len = addrlen - offsetof(struct sockaddr_un, sun_path);
    if (sun_path_len == 0 || sun_path_len > sizeof(un->sun_path)) {
        errno = EINVAL;
        return -1;
    }
    if (un->sun_path[0] != '\0') {
        errno = EOPNOTSUPP;
        return -1;
    }
    if (sun_path_len < 2) {
        errno = EINVAL;
        return -1;
    }

    name_len = sun_path_len - 1;
    if (name_len == 0 || name_len > (size_t)(UNIX_PATH_MAX - 1)) {
        errno = EINVAL;
        return -1;
    }

    memcpy(name_out, &un->sun_path[1], name_len);
    *name_len_out = name_len;
    return 0;
}

static int socket_copy_name_out(const unsigned char *name,
                                size_t name_len,
                                struct sockaddr *addr,
                                int *addrlen) {
    struct sockaddr_un unix_addr;
    size_t actual_len;
    size_t copy_len;

    if (!addrlen) {
        errno = EFAULT;
        return -1;
    }

    actual_len = socket_unix_name_len(name_len);
    if (!addr) {
        *addrlen = (int)actual_len;
        return 0;
    }

    memset(&unix_addr, 0, sizeof(unix_addr));
    unix_addr.sun_family = AF_UNIX;
    if (name_len > 0) {
        unix_addr.sun_path[0] = '\0';
        memcpy(&unix_addr.sun_path[1], name, name_len);
    }

    copy_len = (size_t)*addrlen < actual_len ? (size_t)*addrlen : actual_len;
    memcpy(addr, &unix_addr, copy_len);
    *addrlen = (int)actual_len;
    return 0;
}

static int socket_flags_from_type(int type, int *base_type_out, bool *datagram_out, int *fd_flags_out) {
    int base_type;
    int fd_flags = 0;
    int type_flags;

    if (!base_type_out || !datagram_out || !fd_flags_out) {
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
    if (base_type == SOCK_STREAM) {
        *datagram_out = false;
    } else if (base_type == SOCK_DGRAM) {
        *datagram_out = true;
    } else {
        errno = EPROTOTYPE;
        return -1;
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

long sys_socket(int domain, int type, int protocol) {
    int base_type;
    int flags;
    bool datagram;
    struct socket_state *sock;
    int fd;

    if (domain != AF_UNIX) {
        errno = EAFNOSUPPORT;
        return -1;
    }
    if (socket_flags_from_type(type, &base_type, &datagram, &flags) != 0) {
        return -1;
    }

    sock = socket_create_impl(domain, base_type, protocol, datagram);
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

long sys_socketpair(int domain, int type, int protocol, int sv[2]) {
    int base_type;
    int flags;
    bool datagram;
    struct socket_state *a = NULL;
    struct socket_state *b = NULL;
    int fd0 = -1;
    int fd1 = -1;

    if (!sv) {
        errno = EFAULT;
        return -1;
    }

    if (domain != AF_UNIX) {
        errno = EAFNOSUPPORT;
        return -1;
    }
    if (socket_flags_from_type(type, &base_type, &datagram, &flags) != 0) {
        return -1;
    }

    if (socketpair_create_impl(domain, base_type, protocol, datagram, &a, &b) != 0) {
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

long sys_connect(int sockfd, struct sockaddr *addr, int addrlen) {
    struct socket_state *sock;
    unsigned char name[UNIX_PATH_MAX - 1];
    size_t name_len;
    int ret;

    if (addrlen < 0) {
        errno = EINVAL;
        return -1;
    }

    if (fd_get_socket(sockfd, &sock) != 0) {
        return -1;
    }
    if (socket_parse_unix_name(addr, (__kernel_size_t)addrlen, name, &name_len) != 0) {
        socket_release_impl(sock);
        return -1;
    }
    ret = socket_connect_impl(sock, name, name_len);
    socket_release_impl(sock);
    return ret;
}

long sys_bind(int sockfd, struct sockaddr *addr, int addrlen) {
    struct socket_state *sock;
    unsigned char name[UNIX_PATH_MAX - 1];
    size_t name_len;
    int ret;

    if (addrlen < 0) {
        errno = EINVAL;
        return -1;
    }

    if (fd_get_socket(sockfd, &sock) != 0) {
        return -1;
    }
    if (socket_parse_unix_name(addr, (__kernel_size_t)addrlen, name, &name_len) != 0) {
        socket_release_impl(sock);
        return -1;
    }
    ret = socket_bind_impl(sock, name, name_len);
    socket_release_impl(sock);
    return ret;
}

long sys_listen(int sockfd, int backlog) {
    struct socket_state *sock;
    int ret;

    if (fd_get_socket(sockfd, &sock) != 0) {
        return -1;
    }
    ret = socket_listen_impl(sock, backlog);
    socket_release_impl(sock);
    return ret;
}

long sys_accept(int sockfd, struct sockaddr *addr, int *addrlen) {
    struct socket_state *sock;
    struct socket_state *accepted;
    int fd;
    int fd_flags;
    unsigned char peer_name[UNIX_PATH_MAX - 1];
    size_t peer_name_len = 0;

    if (addrlen) {
        if (*addrlen < 0) {
            errno = EINVAL;
            return -1;
        }
    }

    if (fd_get_socket(sockfd, &sock) != 0) {
        return -1;
    }

    accepted = socket_accept_impl(sock, false);
    socket_release_impl(sock);
    if (!accepted) {
        return -1;
    }
    if (addr || addrlen) {
        if (socket_getpeername_impl(accepted, peer_name, &peer_name_len) != 0) {
            socket_release_impl(accepted);
            return -1;
        }
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
    if (addr || addrlen) {
        if (socket_copy_name_out(peer_name, peer_name_len, addr, addrlen) != 0) {
            close_impl(fd);
            return -1;
        }
    }
    return fd;
}

long sys_accept4(int sockfd, struct sockaddr *addr, int *addrlen, int flags) {
    struct socket_state *sock;
    struct socket_state *accepted;
    int fd;
    int fd_flags;
    unsigned char peer_name[UNIX_PATH_MAX - 1];
    size_t peer_name_len = 0;

    if (socket_accept_flags(flags, &fd_flags) != 0) {
        return -1;
    }
    if (addrlen) {
        if (*addrlen < 0) {
            errno = EINVAL;
            return -1;
        }
    }

    if (fd_get_socket(sockfd, &sock) != 0) {
        return -1;
    }

    accepted = socket_accept_impl(sock, (flags & SOCK_NONBLOCK) != 0);
    socket_release_impl(sock);
    if (!accepted) {
        return -1;
    }
    if (addr || addrlen) {
        if (socket_getpeername_impl(accepted, peer_name, &peer_name_len) != 0) {
            socket_release_impl(accepted);
            return -1;
        }
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
    if (addr || addrlen) {
        if (socket_copy_name_out(peer_name, peer_name_len, addr, addrlen) != 0) {
            close_impl(fd);
            return -1;
        }
    }
    return fd;
}

long sys_sendto(int sockfd,
                void *buf,
                size_t len,
                unsigned int flags,
                struct sockaddr *dest_addr,
                int addrlen) {
    struct socket_state *sock;
    unsigned char dest_name[UNIX_PATH_MAX - 1];
    size_t dest_name_len = 0;
    bool has_dest_name = false;
    __kernel_ssize_t ret;

    if (addrlen < 0) {
        errno = EINVAL;
        return -1;
    }

    if (fd_get_socket(sockfd, &sock) != 0) {
        return -1;
    }
    if (dest_addr) {
        if (socket_parse_unix_name(dest_addr, (__kernel_size_t)addrlen, dest_name, &dest_name_len) != 0) {
            socket_release_impl(sock);
            return -1;
        }
        has_dest_name = true;
    }
    ret = socket_sendto_impl(sock, buf, len, (flags & MSG_DONTWAIT) != 0, dest_name, dest_name_len, has_dest_name);
    socket_release_impl(sock);
    return ret;
}

long sys_recvfrom(int sockfd,
                  void *buf,
                  size_t len,
                  unsigned int flags,
                  struct sockaddr *src_addr,
                  int *addrlen) {
    struct socket_state *sock;
    unsigned char src_name[UNIX_PATH_MAX - 1];
    size_t src_name_len = 0;
    bool has_src_name = false;
    __kernel_ssize_t ret;

    if (fd_get_socket(sockfd, &sock) != 0) {
        return -1;
    }
    if (addrlen) {
        if (*addrlen < 0) {
            socket_release_impl(sock);
            errno = EINVAL;
            return -1;
        }
    }
    ret = socket_recvfrom_impl(sock, buf, len, (flags & MSG_DONTWAIT) != 0, src_name, &src_name_len, &has_src_name);
    socket_release_impl(sock);
    if (ret >= 0 && (src_addr || addrlen) && has_src_name) {
        if (socket_copy_name_out(src_name, src_name_len, src_addr, addrlen) != 0) {
            return -1;
        }
    } else if (ret >= 0 && addrlen && !has_src_name) {
        *addrlen = 0;
    }
    return ret;
}

long sys_sendmsg(int sockfd, struct user_msghdr *msg, unsigned int flags) {
    size_t total = 0;
    size_t offset = 0;
    char *tmp;
    __kernel_ssize_t ret;

    if (!msg) {
        errno = EFAULT;
        return -1;
    }
    for (__kernel_size_t i = 0; i < msg->msg_iovlen; i++) {
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

    for (__kernel_size_t i = 0; i < msg->msg_iovlen; i++) {
        if (msg->msg_iov[i].iov_len > 0 && msg->msg_iov[i].iov_base) {
            memcpy(tmp + offset, msg->msg_iov[i].iov_base, msg->msg_iov[i].iov_len);
            offset += msg->msg_iov[i].iov_len;
        }
    }

    ret = sys_sendto(sockfd, tmp, offset, flags, (struct sockaddr *)msg->msg_name, msg->msg_namelen);
    free(tmp);
    return ret;
}

long sys_recvmsg(int sockfd, struct user_msghdr *msg, unsigned int flags) {
    size_t total = 0;
    size_t remaining;
    size_t offset = 0;
    char *tmp;
    __kernel_ssize_t nread;

    if (!msg) {
        errno = EFAULT;
        return -1;
    }
    for (__kernel_size_t i = 0; i < msg->msg_iovlen; i++) {
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

    nread = sys_recvfrom(sockfd,
                         tmp,
                         total,
                         flags,
                         (struct sockaddr *)msg->msg_name,
                         &msg->msg_namelen);
    if (nread <= 0) {
        free(tmp);
        return nread;
    }

    remaining = (size_t)nread;
    for (__kernel_size_t i = 0; i < msg->msg_iovlen && remaining > 0; i++) {
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

long sys_sendmmsg(int sockfd,
                  struct mmsghdr *msgvec,
                  unsigned int vlen,
                  unsigned int flags) {
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
        long ret = sys_sendmsg(sockfd, &msgvec[sent].msg_hdr, flags);
        if (ret < 0) {
            return sent > 0 ? (int)sent : -1;
        }
        msgvec[sent].msg_len = (unsigned int)ret;
    }
    return (int)sent;
}

long sys_recvmmsg(int sockfd,
                  struct mmsghdr *msgvec,
                  unsigned int vlen,
                  unsigned int flags,
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
        long ret;

        if (received > 0) {
            recv_flags |= MSG_DONTWAIT;
        }
        ret = sys_recvmsg(sockfd, &msgvec[received].msg_hdr, (unsigned int)recv_flags);
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

long sys_shutdown(int sockfd, int how) {
    struct socket_state *sock;
    int ret;
    bool shut_read;
    bool shut_write;

    if (how == SHUT_RD) {
        shut_read = true;
        shut_write = false;
    } else if (how == SHUT_WR) {
        shut_read = false;
        shut_write = true;
    } else if (how == SHUT_RDWR) {
        shut_read = true;
        shut_write = true;
    } else {
        errno = EINVAL;
        return -1;
    }

    if (fd_get_socket(sockfd, &sock) != 0) {
        return -1;
    }
    ret = socket_shutdown_impl(sock, shut_read, shut_write);
    socket_release_impl(sock);
    return ret;
}

long sys_getsockopt(int sockfd,
                    int level,
                    int optname,
                    char *optval,
                    int *optlen) {
    struct socket_state *sock;
    int ret;
    __u32 k_optlen;

    if (fd_get_socket(sockfd, &sock) != 0) {
        return -1;
    }
    if (!optlen || *optlen < 0) {
        socket_release_impl(sock);
        errno = EINVAL;
        return -1;
    }
    k_optlen = (__u32)*optlen;
    ret = socket_getsockopt_impl(sock, level, optname, optval, &k_optlen);
    socket_release_impl(sock);
    if (ret == 0) {
        *optlen = (int)k_optlen;
    }
    return ret;
}

long sys_setsockopt(int sockfd,
                    int level,
                    int optname,
                    char *optval,
                    int optlen) {
    struct socket_state *sock;
    int ret;

    if (fd_get_socket(sockfd, &sock) != 0) {
        return -1;
    }
    if (optlen < 0) {
        socket_release_impl(sock);
        errno = EINVAL;
        return -1;
    }
    ret = socket_setsockopt_impl(sock, level, optname, optval, (__u32)optlen);
    socket_release_impl(sock);
    return ret;
}

long sys_getsockname(int sockfd, struct sockaddr *addr, int *addrlen) {
    struct socket_state *sock;
    unsigned char name[UNIX_PATH_MAX - 1];
    size_t name_len = 0;
    int ret;

    if (fd_get_socket(sockfd, &sock) != 0) {
        return -1;
    }
    if (!addrlen || *addrlen < 0) {
        socket_release_impl(sock);
        errno = EINVAL;
        return -1;
    }
    ret = socket_getsockname_impl(sock, name, &name_len);
    socket_release_impl(sock);
    if (ret == 0) {
        ret = socket_copy_name_out(name, name_len, addr, addrlen);
    }
    return ret;
}

long sys_getpeername(int sockfd, struct sockaddr *addr, int *addrlen) {
    struct socket_state *sock;
    unsigned char name[UNIX_PATH_MAX - 1];
    size_t name_len = 0;
    int ret;

    if (fd_get_socket(sockfd, &sock) != 0) {
        return -1;
    }
    if (!addrlen || *addrlen < 0) {
        socket_release_impl(sock);
        errno = EINVAL;
        return -1;
    }
    ret = socket_getpeername_impl(sock, name, &name_len);
    socket_release_impl(sock);
    if (ret == 0) {
        ret = socket_copy_name_out(name, name_len, addr, addrlen);
    }
    return ret;
}
