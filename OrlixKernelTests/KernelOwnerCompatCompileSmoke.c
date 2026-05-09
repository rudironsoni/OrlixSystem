/*
 * KernelOwnerCompatCompileSmoke.c
 *
 * Proves Linux-owner code can use kernel-private time/socket/select compat
 * surfaces without importing Darwin SDK or package-facing OrlixMLibC headers.
 */

#include "internal/private/kernel_select_compat.h"
#include "internal/private/kernel_socket_compat.h"
#include "internal/private/kernel_time_compat.h"

#ifndef AF_UNIX
#error "AF_UNIX must resolve through kernel-private socket compat"
#endif

#ifndef SOCK_STREAM
#error "SOCK_STREAM must resolve through kernel-private socket compat"
#endif

#ifndef SOL_SOCKET
#error "SOL_SOCKET must resolve through kernel-private socket compat"
#endif

#ifndef CLOCK_REALTIME
#error "CLOCK_REALTIME must resolve through kernel-private time compat"
#endif

#ifndef ITIMER_REAL
#error "ITIMER_REAL must resolve through kernel-private time compat"
#endif

static __u32 kernel_owner_probe_socklen(__u32 len) {
    return len;
}

static sa_family_t kernel_owner_probe_family(const struct sockaddr *addr) {
    return addr->sa_family;
}

static size_t kernel_owner_probe_iov_count(const struct msghdr *msg) {
    return (size_t)msg->msg_iovlen;
}

static kernel_time_t kernel_owner_probe_time(kernel_time_t value) {
    return value;
}

static long kernel_owner_probe_nsec(const struct kernel_timespec *ts) {
    return ts->tv_nsec;
}

static kernel_suseconds_t kernel_owner_probe_usec(const struct kernel_timeval *tv) {
    return tv->tv_usec;
}

static int kernel_owner_probe_select_macros(fd_set *set, int fd) {
    FD_ZERO(set);
    FD_SET(fd, set);
    return FD_ISSET(fd, set) ? CLOCK_REALTIME : ITIMER_REAL;
}

__attribute__((unused)) static void (*volatile kernel_owner_compat_refs[])(void) = {
    (void (*)(void))kernel_owner_probe_socklen,
    (void (*)(void))kernel_owner_probe_family,
    (void (*)(void))kernel_owner_probe_iov_count,
    (void (*)(void))kernel_owner_probe_time,
    (void (*)(void))kernel_owner_probe_nsec,
    (void (*)(void))kernel_owner_probe_usec,
    (void (*)(void))kernel_owner_probe_select_macros,
};
