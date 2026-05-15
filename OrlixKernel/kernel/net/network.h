#ifndef KERNEL_NET_NETWORK_H
#define KERNEL_NET_NETWORK_H

#include <linux/types.h>
#include <linux/time.h>

#ifdef __cplusplus
extern "C" {
#endif

struct user_msghdr;
struct mmsghdr;

long sys_socket(int domain, int type, int protocol);
long sys_socketpair(int domain, int type, int protocol, int *sv);
long sys_connect(int sockfd, void *addr, int addrlen);
long sys_bind(int sockfd, void *addr, int addrlen);
long sys_listen(int sockfd, int backlog);
long sys_accept(int sockfd, void *addr, int *addrlen);
long sys_accept4(int sockfd, void *addr, int *addrlen, int flags);
long sys_shutdown(int sockfd, int how);
long sys_sendto(int sockfd, void *buf, size_t len, unsigned int flags, void *dest_addr,
                int addrlen);
long sys_recvfrom(int sockfd, void *buf, size_t len, unsigned int flags, void *src_addr,
                  int *addrlen);
long sys_sendmsg(int sockfd, struct user_msghdr *msg, unsigned int flags);
long sys_recvmsg(int sockfd, struct user_msghdr *msg, unsigned int flags);
long sys_sendmmsg(int sockfd, struct mmsghdr *msgvec, unsigned int vlen, unsigned int flags);
long sys_recvmmsg(int sockfd, struct mmsghdr *msgvec, unsigned int vlen, unsigned int flags,
                  struct __kernel_timespec *timeout);
long sys_getsockname(int sockfd, void *addr, int *addrlen);
long sys_getpeername(int sockfd, void *addr, int *addrlen);
long sys_setsockopt(int sockfd, int level, int optname, char *optval, int optlen);
long sys_getsockopt(int sockfd, int level, int optname, char *optval, int *optlen);

#ifdef __cplusplus
}
#endif

#endif
