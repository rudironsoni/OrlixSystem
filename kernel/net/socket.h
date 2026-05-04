#ifndef IXLAND_KERNEL_NET_SOCKET_H
#define IXLAND_KERNEL_NET_SOCKET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>

struct ix_socket;

struct ix_socket *ix_socket_create_impl(int domain, int type, int protocol);
int ix_socketpair_create_impl(int domain, int type, int protocol, struct ix_socket **a_out, struct ix_socket **b_out);

void ix_socket_retain_impl(struct ix_socket *sock);
void ix_socket_release_impl(struct ix_socket *sock);

int ix_socket_shutdown_impl(struct ix_socket *sock, int how);
int ix_socket_connect_impl(struct ix_socket *sock, const struct sockaddr *addr, socklen_t addrlen);
int ix_socket_bind_impl(struct ix_socket *sock, const struct sockaddr *addr, socklen_t addrlen);
int ix_socket_listen_impl(struct ix_socket *sock, int backlog);
struct ix_socket *ix_socket_accept_impl(struct ix_socket *sock, struct sockaddr *addr, socklen_t *addrlen, int flags);

ssize_t ix_socket_send_impl(struct ix_socket *sock, const void *buf, size_t len, int flags);
ssize_t ix_socket_recv_impl(struct ix_socket *sock, void *buf, size_t len, int flags);

short ix_socket_poll_revents_impl(struct ix_socket *sock, short events);

#endif

