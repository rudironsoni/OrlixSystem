#ifndef ORLIX_KERNEL_NET_SOCKET_H
#define ORLIX_KERNEL_NET_SOCKET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <linux/types.h>

struct sockaddr;

struct socket_state;

struct socket_state *socket_create_impl(int domain, int type, int protocol);
int socketpair_create_impl(int domain,
                           int type,
                           int protocol,
                           struct socket_state **a_out,
                           struct socket_state **b_out);
unsigned long long socket_identity_impl(const struct socket_state *sock);

void socket_retain_impl(struct socket_state *sock);
void socket_release_impl(struct socket_state *sock);

int socket_shutdown_impl(struct socket_state *sock, int how);
int socket_connect_impl(struct socket_state *sock, const struct sockaddr *addr, __u32 addrlen);
int socket_bind_impl(struct socket_state *sock, const struct sockaddr *addr, __u32 addrlen);
int socket_listen_impl(struct socket_state *sock, int backlog);
struct socket_state *socket_accept_impl(struct socket_state *sock,
                                        struct sockaddr *addr,
                                        __u32 *addrlen,
                                        int flags);
int socket_getsockname_impl(struct socket_state *sock, struct sockaddr *addr, __u32 *addrlen);
int socket_getpeername_impl(struct socket_state *sock, struct sockaddr *addr, __u32 *addrlen);
int socket_getsockopt_impl(struct socket_state *sock,
                           int level,
                           int optname,
                           void *optval,
                           __u32 *optlen);
int socket_setsockopt_impl(struct socket_state *sock,
                           int level,
                           int optname,
                           const void *optval,
                           __u32 optlen);

__kernel_ssize_t socket_send_impl(struct socket_state *sock, const void *buf, size_t len, int flags);
__kernel_ssize_t socket_recv_impl(struct socket_state *sock, void *buf, size_t len, int flags);
__kernel_ssize_t socket_sendto_impl(struct socket_state *sock,
                                    const void *buf,
                                    size_t len,
                                    int flags,
                                    const struct sockaddr *dest_addr,
                                    __u32 addrlen);
__kernel_ssize_t socket_recvfrom_impl(struct socket_state *sock,
                                      void *buf,
                                      size_t len,
                                      int flags,
                                      struct sockaddr *src_addr,
                                      __u32 *addrlen);

short socket_poll_revents_impl(struct socket_state *sock, short events);

#endif
