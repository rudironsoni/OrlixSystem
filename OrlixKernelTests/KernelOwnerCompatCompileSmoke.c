/*
 * KernelOwnerCompatCompileSmoke.c
 *
 * Proves Linux-owner code can consume vendored Linux contract truth without
 * importing Darwin SDK or package-facing OrlixMLibC headers.
 */

#include <uapi/linux/time.h>
#include <uapi/linux/socket.h>

#include <string.h>

#ifndef AF_UNIX
#error "AF_UNIX must resolve through vendored Linux kernel headers"
#endif

#ifndef SOCK_STREAM
#error "SOCK_STREAM must resolve through vendored Linux kernel headers"
#endif

#ifndef SOL_SOCKET
#error "SOL_SOCKET must resolve through vendored Linux kernel headers"
#endif

#ifndef CLOCK_REALTIME
#error "CLOCK_REALTIME must resolve through vendored Linux UAPI"
#endif

#ifndef ITIMER_REAL
#error "ITIMER_REAL must resolve through vendored Linux UAPI"
#endif

static __u32 kernel_owner_probe_socklen(__u32 len) {
    return len;
}

static sa_family_t kernel_owner_probe_family(const struct sockaddr *addr) {
    return addr->sa_family;
}

static size_t kernel_owner_probe_iov_count(const struct user_msghdr *msg) {
    return (size_t)msg->msg_iovlen;
}

static __kernel_old_time_t kernel_owner_probe_time(__kernel_old_time_t value) {
    return value;
}

static long kernel_owner_probe_nsec(const struct timespec *ts) {
    return ts->tv_nsec;
}

static __kernel_suseconds_t kernel_owner_probe_usec(const struct timeval *tv) {
    return tv->tv_usec;
}

static int kernel_owner_probe_fdset(__kernel_fd_set *set, int fd) {
    unsigned int bits_per_word = (unsigned int)(8U * sizeof(set->fds_bits[0]));
    unsigned int word = (unsigned int)fd / bits_per_word;
    unsigned int bit = (unsigned int)fd % bits_per_word;

    memset(set->fds_bits, 0, sizeof(set->fds_bits));
    set->fds_bits[word] |= (1UL << bit);
    return (set->fds_bits[word] & (1UL << bit)) != 0 ? CLOCK_REALTIME : ITIMER_REAL;
}

__attribute__((unused)) static void (*volatile kernel_owner_compat_refs[])(void) = {
    (void (*)(void))kernel_owner_probe_socklen,
    (void (*)(void))kernel_owner_probe_family,
    (void (*)(void))kernel_owner_probe_iov_count,
    (void (*)(void))kernel_owner_probe_time,
    (void (*)(void))kernel_owner_probe_nsec,
    (void (*)(void))kernel_owner_probe_usec,
    (void (*)(void))kernel_owner_probe_fdset,
};
