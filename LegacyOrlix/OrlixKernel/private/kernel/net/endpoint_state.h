#ifndef PRIVATE_KERNEL_NET_ENDPOINT_STATE_H
#define PRIVATE_KERNEL_NET_ENDPOINT_STATE_H

#include <linux/stddef.h>
#include <linux/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct socket_state;

struct socket_state *socket_create_impl(int domain, int type, int protocol, bool datagram);
int socketpair_create_impl(int domain,
                           int type,
                           int protocol,
                           bool datagram,
                           struct socket_state **a_out,
                           struct socket_state **b_out);
unsigned long long socket_identity_impl(const struct socket_state *sock);

void socket_retain_impl(struct socket_state *sock);
void socket_release_impl(struct socket_state *sock);

int socket_shutdown_impl(struct socket_state *sock, bool shut_read, bool shut_write);
int socket_connect_impl(struct socket_state *sock, const unsigned char *name, size_t name_len);
int socket_bind_impl(struct socket_state *sock, const unsigned char *name, size_t name_len);
int socket_listen_impl(struct socket_state *sock, int backlog);
struct socket_state *socket_accept_impl(struct socket_state *sock, bool nonblock);
int socket_getsockname_impl(struct socket_state *sock, unsigned char *name_out, size_t *name_len_out);
int socket_getpeername_impl(struct socket_state *sock, unsigned char *name_out, size_t *name_len_out);
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

__kernel_ssize_t socket_send_impl(struct socket_state *sock, const void *buf, size_t len, bool nonblock);
__kernel_ssize_t socket_recv_impl(struct socket_state *sock, void *buf, size_t len, bool nonblock);
__kernel_ssize_t socket_sendto_impl(struct socket_state *sock,
                                    const void *buf,
                                    size_t len,
                                    bool nonblock,
                                    const unsigned char *dest_name,
                                    size_t dest_name_len,
                                    bool has_dest_name);
__kernel_ssize_t socket_recvfrom_impl(struct socket_state *sock,
                                      void *buf,
                                      size_t len,
                                      bool nonblock,
                                      unsigned char *src_name_out,
                                      size_t *src_name_len_out,
                                      bool *has_src_name_out);

short socket_poll_revents_impl(struct socket_state *sock, short events);

#ifdef __cplusplus
}
#endif

#endif /* PRIVATE_KERNEL_NET_ENDPOINT_STATE_H */
