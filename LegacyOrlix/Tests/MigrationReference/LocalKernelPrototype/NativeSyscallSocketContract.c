#include <uapi/asm/unistd.h>
#include <uapi/linux/fcntl.h>
#include <uapi/linux/errno.h>
#include <linux/string.h>
#include <linux/net.h>

#include <stddef.h>
#include <stdint.h>

#include "NativeSyscallContract.h"
#include "fs/fdtable.h"
#include "fs/namei.h"
#include "runtime/syscall.h"

extern int errno;

struct native_unix_sockaddr {
    unsigned short sun_family;
    char sun_path[108];
};

struct native_iovec {
    void *iov_base;
    size_t iov_len;
};

struct native_msghdr {
    void *msg_name;
    unsigned int msg_namelen;
    struct native_iovec *msg_iov;
    int msg_iovlen;
    void *msg_control;
    size_t msg_controllen;
    int msg_flags;
};

struct native_mmsghdr {
    struct native_msghdr msg_hdr;
    unsigned int msg_len;
};

typedef unsigned int native_socklen_t;

static int close_if_open(int fd) {
    if (fd >= 0) {
        int saved_errno = errno;
        (void)close_impl(fd);
        errno = saved_errno;
    }
    return 0;
}

static int format_proc_fd_path(char *buf, size_t buf_len, int fd) {
    const char prefix[] = "/proc/self/fd/";
    char digits[16];
    size_t pos = 0;
    int value = fd;
    int digit_count = 0;

    if (!buf || buf_len == 0 || fd < 0) {
        errno = EINVAL;
        return -1;
    }
    while (prefix[pos] != '\0') {
        if (pos + 1 >= buf_len) {
            errno = ENAMETOOLONG;
            return -1;
        }
        buf[pos] = prefix[pos];
        pos++;
    }
    do {
        digits[digit_count++] = (char)('0' + (value % 10));
        value /= 10;
    } while (value > 0 && digit_count < (int)sizeof(digits));
    if (value > 0 || pos + (size_t)digit_count + 1 > buf_len) {
        errno = ENAMETOOLONG;
        return -1;
    }
    while (digit_count > 0) {
        buf[pos++] = digits[--digit_count];
    }
    buf[pos] = '\0';
    return 0;
}

int native_syscall_contract_unix_socket_listen_accept_connect_stream_transfer(void) {
    int server = -1;
    int client = -1;
    int accepted = -1;
    struct native_unix_sockaddr server_addr;
    struct native_unix_sockaddr client_addr;
    struct native_unix_sockaddr actual_addr;
    native_socklen_t addrlen;
    const char message[] = "hi";
    char buf[8];
    long ret;

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_UNIX;
    server_addr.sun_path[0] = '\0';
    memcpy(&server_addr.sun_path[1], "orlix-server", 13);

    memset(&client_addr, 0, sizeof(client_addr));
    client_addr.sun_family = AF_UNIX;
    client_addr.sun_path[0] = '\0';
    memcpy(&client_addr.sun_path[1], "orlix-client", 13);

    server = (int)syscall_dispatch_impl(__NR_socket, AF_UNIX, SOCK_STREAM, 0, 0, 0, 0);
    if (server < 0) {
        errno = (int)-server;
        return -1;
    }

    addrlen = (native_socklen_t)(offsetof(struct native_unix_sockaddr, sun_path) + 1 + 13);
    ret = syscall_dispatch_impl(__NR_bind, server, (long)(uintptr_t)&server_addr, addrlen, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    ret = syscall_dispatch_impl(__NR_listen, server, 1, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    client = (int)syscall_dispatch_impl(__NR_socket, AF_UNIX, SOCK_STREAM, 0, 0, 0, 0);
    if (client < 0) {
        errno = (int)-client;
        goto out;
    }

    ret = syscall_dispatch_impl(__NR_bind, client, (long)(uintptr_t)&client_addr, addrlen, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    ret = syscall_dispatch_impl(__NR_connect, client, (long)(uintptr_t)&server_addr, addrlen, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    accepted = (int)syscall_dispatch_impl(__NR_accept, server, 0, 0, 0, 0, 0);
    if (accepted < 0) {
        errno = (int)-accepted;
        goto out;
    }

    memset(&actual_addr, 0, sizeof(actual_addr));
    addrlen = sizeof(actual_addr);
    ret = syscall_dispatch_impl(__NR_getsockname, client, (long)(uintptr_t)&actual_addr,
                                (long)(uintptr_t)&addrlen, 0, 0, 0);
    if (ret != 0 || actual_addr.sun_family != AF_UNIX || addrlen !=
        (native_socklen_t)(offsetof(struct native_unix_sockaddr, sun_path) + 1 + 13) ||
        memcmp(actual_addr.sun_path, client_addr.sun_path, 14) != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    memset(&actual_addr, 0, sizeof(actual_addr));
    addrlen = sizeof(actual_addr);
    ret = syscall_dispatch_impl(__NR_getpeername, client, (long)(uintptr_t)&actual_addr,
                                (long)(uintptr_t)&addrlen, 0, 0, 0);
    if (ret != 0 || actual_addr.sun_family != AF_UNIX || addrlen !=
        (native_socklen_t)(offsetof(struct native_unix_sockaddr, sun_path) + 1 + 13) ||
        memcmp(actual_addr.sun_path, server_addr.sun_path, 14) != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    memset(&actual_addr, 0, sizeof(actual_addr));
    addrlen = sizeof(actual_addr);
    ret = syscall_dispatch_impl(__NR_getsockname, accepted, (long)(uintptr_t)&actual_addr,
                                (long)(uintptr_t)&addrlen, 0, 0, 0);
    if (ret != 0 || actual_addr.sun_family != AF_UNIX || addrlen !=
        (native_socklen_t)(offsetof(struct native_unix_sockaddr, sun_path) + 1 + 13) ||
        memcmp(actual_addr.sun_path, server_addr.sun_path, 14) != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    memset(&actual_addr, 0, sizeof(actual_addr));
    addrlen = sizeof(actual_addr);
    ret = syscall_dispatch_impl(__NR_getpeername, accepted, (long)(uintptr_t)&actual_addr,
                                (long)(uintptr_t)&addrlen, 0, 0, 0);
    if (ret != 0 || actual_addr.sun_family != AF_UNIX || addrlen !=
        (native_socklen_t)(offsetof(struct native_unix_sockaddr, sun_path) + 1 + 13) ||
        memcmp(actual_addr.sun_path, client_addr.sun_path, 14) != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    ret = syscall_dispatch_impl(__NR_sendto, client, (long)(uintptr_t)message, 2, 0, 0, 0);
    if (ret != 2) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    memset(buf, 0, sizeof(buf));
    ret = syscall_dispatch_impl(__NR_recvfrom, accepted, (long)(uintptr_t)buf, 2, 0, 0, 0);
    if (ret != 2 || memcmp(buf, message, 2) != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    close_if_open(server);
    close_if_open(client);
    close_if_open(accepted);
    return 0;

out:
    close_if_open(server);
    close_if_open(client);
    close_if_open(accepted);
    return -1;
}

int native_syscall_contract_unix_socket_flags_sockopts_and_proc_identity(void) {
    int listener = -1;
    int client = -1;
    int accepted = -1;
    struct native_unix_sockaddr listener_addr;
    struct native_unix_sockaddr peer_addr;
    native_socklen_t addrlen;
    int value;
    native_socklen_t optlen;
    char proc_path[64];
    char link_target[64];
    long ret;

    memset(&listener_addr, 0, sizeof(listener_addr));
    listener_addr.sun_family = AF_UNIX;
    listener_addr.sun_path[0] = '\0';
    memcpy(&listener_addr.sun_path[1], "orlix-sockopts", 15);

    memset(&peer_addr, 0, sizeof(peer_addr));
    peer_addr.sun_family = AF_UNIX;
    peer_addr.sun_path[0] = '\0';
    memcpy(&peer_addr.sun_path[1], "orlix-sock-peer", 16);

    listener = (int)syscall_dispatch_impl(__NR_socket,
                                          AF_UNIX,
                                          SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
                                          0, 0, 0, 0);
    if (listener < 0) {
        errno = -listener;
        return -1;
    }

    ret = syscall_dispatch_impl(__NR_fcntl, listener, F_GETFL, 0, 0, 0, 0);
    if (ret < 0 || (ret & O_NONBLOCK) == 0 || (ret & O_ACCMODE) != O_RDWR) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_fcntl, listener, F_GETFD, 0, 0, 0, 0);
    if (ret != FD_CLOEXEC) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    optlen = (native_socklen_t)sizeof(value);
    ret = syscall_dispatch_impl(__NR_getsockopt, listener, SOL_SOCKET, SO_TYPE,
                                (long)(uintptr_t)&value, (long)(uintptr_t)&optlen, 0);
    if (ret != 0 || optlen != (native_socklen_t)sizeof(value) || value != SOCK_STREAM) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    optlen = (native_socklen_t)sizeof(value);
    ret = syscall_dispatch_impl(__NR_getsockopt, listener, SOL_SOCKET, SO_DOMAIN,
                                (long)(uintptr_t)&value, (long)(uintptr_t)&optlen, 0);
    if (ret != 0 || value != AF_UNIX) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    optlen = (native_socklen_t)sizeof(value);
    ret = syscall_dispatch_impl(__NR_getsockopt, listener, SOL_SOCKET, SO_PROTOCOL,
                                (long)(uintptr_t)&value, (long)(uintptr_t)&optlen, 0);
    if (ret != 0 || value != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    optlen = (native_socklen_t)sizeof(value);
    ret = syscall_dispatch_impl(__NR_getsockopt, listener, SOL_SOCKET, SO_ACCEPTCONN,
                                (long)(uintptr_t)&value, (long)(uintptr_t)&optlen, 0);
    if (ret != 0 || value != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    value = 1;
    ret = syscall_dispatch_impl(__NR_setsockopt, listener, SOL_SOCKET, SO_REUSEADDR,
                                (long)(uintptr_t)&value, sizeof(value), 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_setsockopt, listener, SOL_SOCKET, SO_KEEPALIVE,
                                (long)(uintptr_t)&value, sizeof(value), 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    value = 4096;
    ret = syscall_dispatch_impl(__NR_setsockopt, listener, SOL_SOCKET, SO_SNDBUF,
                                (long)(uintptr_t)&value, sizeof(value), 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    value = 8192;
    ret = syscall_dispatch_impl(__NR_setsockopt, listener, SOL_SOCKET, SO_RCVBUF,
                                (long)(uintptr_t)&value, sizeof(value), 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    optlen = (native_socklen_t)sizeof(value);
    ret = syscall_dispatch_impl(__NR_getsockopt, listener, SOL_SOCKET, SO_REUSEADDR,
                                (long)(uintptr_t)&value, (long)(uintptr_t)&optlen, 0);
    if (ret != 0 || value != 1) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    optlen = (native_socklen_t)sizeof(value);
    ret = syscall_dispatch_impl(__NR_getsockopt, listener, SOL_SOCKET, SO_KEEPALIVE,
                                (long)(uintptr_t)&value, (long)(uintptr_t)&optlen, 0);
    if (ret != 0 || value != 1) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    optlen = (native_socklen_t)sizeof(value);
    ret = syscall_dispatch_impl(__NR_getsockopt, listener, SOL_SOCKET, SO_SNDBUF,
                                (long)(uintptr_t)&value, (long)(uintptr_t)&optlen, 0);
    if (ret != 0 || value != 4096) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    optlen = (native_socklen_t)sizeof(value);
    ret = syscall_dispatch_impl(__NR_getsockopt, listener, SOL_SOCKET, SO_RCVBUF,
                                (long)(uintptr_t)&value, (long)(uintptr_t)&optlen, 0);
    if (ret != 0 || value != 8192) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    optlen = (native_socklen_t)sizeof(value);
    ret = syscall_dispatch_impl(__NR_getsockopt, listener, SOL_SOCKET, SO_ERROR,
                                (long)(uintptr_t)&value, (long)(uintptr_t)&optlen, 0);
    if (ret != 0 || value != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    addrlen = (native_socklen_t)(offsetof(struct native_unix_sockaddr, sun_path) + 1 + 15);
    ret = syscall_dispatch_impl(__NR_bind, listener, (long)(uintptr_t)&listener_addr, addrlen, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_listen, listener, 1, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    optlen = (native_socklen_t)sizeof(value);
    ret = syscall_dispatch_impl(__NR_getsockopt, listener, SOL_SOCKET, SO_ACCEPTCONN,
                                (long)(uintptr_t)&value, (long)(uintptr_t)&optlen, 0);
    if (ret != 0 || value != 1) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    client = (int)syscall_dispatch_impl(__NR_socket, AF_UNIX, SOCK_STREAM, 0, 0, 0, 0);
    if (client < 0) {
        errno = -client;
        goto out;
    }

    addrlen = (native_socklen_t)(offsetof(struct native_unix_sockaddr, sun_path) + 1 + 16);
    ret = syscall_dispatch_impl(__NR_bind, client, (long)(uintptr_t)&peer_addr, addrlen, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    addrlen = (native_socklen_t)(offsetof(struct native_unix_sockaddr, sun_path) + 1 + 15);
    ret = syscall_dispatch_impl(__NR_connect, client, (long)(uintptr_t)&listener_addr, addrlen, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    accepted = (int)syscall_dispatch_impl(__NR_accept4, listener, 0, 0,
                                          O_NONBLOCK | O_CLOEXEC, 0, 0);
    if (accepted < 0) {
        errno = -accepted;
        goto out;
    }

    ret = syscall_dispatch_impl(__NR_fcntl, accepted, F_GETFL, 0, 0, 0, 0);
    if (ret < 0 || (ret & O_NONBLOCK) == 0 || (ret & O_ACCMODE) != O_RDWR) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_fcntl, accepted, F_GETFD, 0, 0, 0, 0);
    if (ret != FD_CLOEXEC) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    if (format_proc_fd_path(proc_path, sizeof(proc_path), accepted) != 0) {
        goto out;
    }
    ret = readlink_impl(proc_path, link_target, sizeof(link_target) - 1);
    if (ret < 0) {
        goto out;
    }
    link_target[ret] = '\0';
    if (strncmp(link_target, "socket:[", 8) != 0 || strchr(link_target + 8, ']') == NULL) {
        errno = EPROTO;
        goto out;
    }

    close_if_open(listener);
    close_if_open(client);
    close_if_open(accepted);
    return 0;

out:
    close_if_open(listener);
    close_if_open(client);
    close_if_open(accepted);
    return -1;
}

int native_syscall_contract_unix_datagram_and_mmsg_paths(void) {
    int tx = -1;
    int rx = -1;
    int pair[2] = {-1, -1};
    struct native_unix_sockaddr tx_addr;
    struct native_unix_sockaddr rx_addr;
    struct native_unix_sockaddr actual_addr;
    native_socklen_t tx_addrlen;
    native_socklen_t rx_addrlen;
    char buf[32];
    char part1[8];
    char part2[8];
    struct native_iovec send_iov[2];
    struct native_iovec recv_iov[2];
    struct native_msghdr send_hdr;
    struct native_msghdr recv_hdr;
    struct native_mmsghdr send_vec[2];
    struct native_mmsghdr recv_vec[2];
    struct native_iovec sendmmsg_iov[2];
    struct native_iovec recvmmsg_iov[2];
    struct native_unix_sockaddr recv_addrs[2];
    const char *sendmmsg_payloads[2] = {"first", "second"};
    long ret;

    memset(&tx_addr, 0, sizeof(tx_addr));
    tx_addr.sun_family = AF_UNIX;
    tx_addr.sun_path[0] = '\0';
    memcpy(&tx_addr.sun_path[1], "orlix-dgram-tx", 15);
    tx_addrlen = (native_socklen_t)(offsetof(struct native_unix_sockaddr, sun_path) + 1 + 15);

    memset(&rx_addr, 0, sizeof(rx_addr));
    rx_addr.sun_family = AF_UNIX;
    rx_addr.sun_path[0] = '\0';
    memcpy(&rx_addr.sun_path[1], "orlix-dgram-rx", 15);
    rx_addrlen = (native_socklen_t)(offsetof(struct native_unix_sockaddr, sun_path) + 1 + 15);

    tx = (int)syscall_dispatch_impl(__NR_socket, AF_UNIX, SOCK_DGRAM, 0, 0, 0, 0);
    if (tx < 0) {
        errno = -tx;
        return -1;
    }
    rx = (int)syscall_dispatch_impl(__NR_socket, AF_UNIX, SOCK_DGRAM, 0, 0, 0, 0);
    if (rx < 0) {
        errno = -rx;
        goto out;
    }

    ret = syscall_dispatch_impl(__NR_bind, tx, (long)(uintptr_t)&tx_addr, tx_addrlen, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_bind, rx, (long)(uintptr_t)&rx_addr, rx_addrlen, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    ret = syscall_dispatch_impl(__NR_sendto, tx, (long)(uintptr_t)"abc", 3, 0,
                                (long)(uintptr_t)&rx_addr, rx_addrlen);
    if (ret != 3) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    memset(&actual_addr, 0, sizeof(actual_addr));
    memset(buf, 0, sizeof(buf));
    rx_addrlen = sizeof(actual_addr);
    ret = syscall_dispatch_impl(__NR_recvfrom, rx, (long)(uintptr_t)buf, sizeof(buf), 0,
                                (long)(uintptr_t)&actual_addr, (long)(uintptr_t)&rx_addrlen);
    if (ret != 3 || memcmp(buf, "abc", 3) != 0 ||
        actual_addr.sun_family != AF_UNIX ||
        rx_addrlen != tx_addrlen ||
        memcmp(actual_addr.sun_path, tx_addr.sun_path, 16) != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    memset(send_iov, 0, sizeof(send_iov));
    memset(&send_hdr, 0, sizeof(send_hdr));
    send_iov[0].iov_base = (void *)"ab";
    send_iov[0].iov_len = 2;
    send_iov[1].iov_base = (void *)"cd";
    send_iov[1].iov_len = 2;
    send_hdr.msg_name = &rx_addr;
    send_hdr.msg_namelen = rx_addrlen;
    send_hdr.msg_iov = send_iov;
    send_hdr.msg_iovlen = 2;
    ret = syscall_dispatch_impl(__NR_sendmsg, tx, (long)(uintptr_t)&send_hdr, 0, 0, 0, 0);
    if (ret != 4) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    memset(part1, 0, sizeof(part1));
    memset(part2, 0, sizeof(part2));
    memset(&actual_addr, 0, sizeof(actual_addr));
    memset(recv_iov, 0, sizeof(recv_iov));
    memset(&recv_hdr, 0, sizeof(recv_hdr));
    recv_iov[0].iov_base = part1;
    recv_iov[0].iov_len = 2;
    recv_iov[1].iov_base = part2;
    recv_iov[1].iov_len = 2;
    recv_hdr.msg_name = &actual_addr;
    recv_hdr.msg_namelen = sizeof(actual_addr);
    recv_hdr.msg_iov = recv_iov;
    recv_hdr.msg_iovlen = 2;
    ret = syscall_dispatch_impl(__NR_recvmsg, rx, (long)(uintptr_t)&recv_hdr, 0, 0, 0, 0);
    if (ret != 4 || memcmp(part1, "ab", 2) != 0 || memcmp(part2, "cd", 2) != 0 ||
        actual_addr.sun_family != AF_UNIX || recv_hdr.msg_namelen != tx_addrlen ||
        memcmp(actual_addr.sun_path, tx_addr.sun_path, 16) != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    memset(send_vec, 0, sizeof(send_vec));
    memset(sendmmsg_iov, 0, sizeof(sendmmsg_iov));
    for (int i = 0; i < 2; i++) {
        sendmmsg_iov[i].iov_base = (void *)sendmmsg_payloads[i];
        sendmmsg_iov[i].iov_len = strlen(sendmmsg_payloads[i]);
        send_vec[i].msg_hdr.msg_name = &rx_addr;
        send_vec[i].msg_hdr.msg_namelen = tx_addrlen;
        send_vec[i].msg_hdr.msg_iov = &sendmmsg_iov[i];
        send_vec[i].msg_hdr.msg_iovlen = 1;
    }
    ret = syscall_dispatch_impl(__NR_sendmmsg, tx, (long)(uintptr_t)send_vec, 2, 0, 0, 0);
    if (ret != 2 || send_vec[0].msg_len != 5 || send_vec[1].msg_len != 6) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    memset(recv_vec, 0, sizeof(recv_vec));
    memset(recvmmsg_iov, 0, sizeof(recvmmsg_iov));
    memset(buf, 0, sizeof(buf));
    memset(part1, 0, sizeof(part1));
    for (int i = 0; i < 2; i++) {
        recv_vec[i].msg_hdr.msg_name = &recv_addrs[i];
        recv_vec[i].msg_hdr.msg_namelen = sizeof(recv_addrs[i]);
        recvmmsg_iov[i].iov_base = (i == 0) ? (void *)buf : (void *)part1;
        recvmmsg_iov[i].iov_len = (i == 0) ? 5U : 6U;
        recv_vec[i].msg_hdr.msg_iov = &recvmmsg_iov[i];
        recv_vec[i].msg_hdr.msg_iovlen = 1;
    }
    ret = syscall_dispatch_impl(__NR_recvmmsg, rx, (long)(uintptr_t)recv_vec, 2, 0, 0, 0);
    if (ret != 2 || recv_vec[0].msg_len != 5 || recv_vec[1].msg_len != 6 ||
        memcmp(buf, "first", 5) != 0 || memcmp(part1, "second", 6) != 0 ||
        recv_addrs[0].sun_family != AF_UNIX || recv_addrs[1].sun_family != AF_UNIX ||
        recv_vec[0].msg_hdr.msg_namelen != tx_addrlen || recv_vec[1].msg_hdr.msg_namelen != tx_addrlen ||
        memcmp(recv_addrs[0].sun_path, tx_addr.sun_path, 16) != 0 ||
        memcmp(recv_addrs[1].sun_path, tx_addr.sun_path, 16) != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    ret = syscall_dispatch_impl(__NR_socketpair, AF_UNIX, SOCK_DGRAM, 0, (long)(uintptr_t)pair, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_sendto, pair[0], (long)(uintptr_t)"z", 1, 0, 0, 0);
    if (ret != 1) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    memset(buf, 0, sizeof(buf));
    ret = syscall_dispatch_impl(__NR_recvfrom, pair[1], (long)(uintptr_t)buf, sizeof(buf), 0, 0, 0);
    if (ret != 1 || buf[0] != 'z') {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    close_if_open(tx);
    close_if_open(rx);
    close_if_open(pair[0]);
    close_if_open(pair[1]);
    return 0;

out:
    close_if_open(tx);
    close_if_open(rx);
    close_if_open(pair[0]);
    close_if_open(pair[1]);
    return -1;
}
