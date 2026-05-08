/*
 * MLibCConfigureProbeCompileSmoke.c
 *
 * PACKAGE-STYLE CONFIGURE PROBE COMPILE TEST
 *
 * This file proves that representative package-facing configure probes for
 * time, socket, and select ownership resolve through the IXLandMLibC bootstrap
 * surface instead of through Darwin SDK headers.
 */

#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>

#ifndef AF_UNIX
#error "AF_UNIX must resolve through the package-facing sys/socket.h owner"
#endif

#ifndef SOCK_STREAM
#error "SOCK_STREAM must resolve through the package-facing sys/socket.h owner"
#endif

#ifndef SOL_SOCKET
#error "SOL_SOCKET must resolve through the package-facing sys/socket.h owner"
#endif

#ifndef ITIMER_REAL
#error "ITIMER_REAL must resolve through the package-facing sys/time.h owner"
#endif

#ifndef CLOCK_REALTIME
#error "CLOCK_REALTIME must resolve through the package-facing time.h owner"
#endif

static socklen_t mlibc_configure_probe_socklen(socklen_t len) {
    return len;
}

static sa_family_t mlibc_configure_probe_family(struct sockaddr *addr) {
    return addr->sa_family;
}

static size_t mlibc_configure_probe_iov_count(const struct msghdr *msg) {
    return (size_t)msg->msg_iovlen;
}

static time_t mlibc_configure_probe_time(time_t value) {
    return value;
}

static long mlibc_configure_probe_nsec(const struct timespec *ts) {
    return ts->tv_nsec;
}

static suseconds_t mlibc_configure_probe_usec(const struct timeval *tv) {
    return tv->tv_usec;
}

static int mlibc_configure_probe_select_macros(fd_set *set, int fd) {
    FD_ZERO(set);
    FD_SET(fd, set);
    return FD_ISSET(fd, set) ? CLOCK_REALTIME : ITIMER_REAL;
}

__attribute__((unused)) static void (*volatile mlibc_configure_probe_refs[])(void) = {
    (void (*)(void))mlibc_configure_probe_socklen,
    (void (*)(void))mlibc_configure_probe_family,
    (void (*)(void))mlibc_configure_probe_iov_count,
    (void (*)(void))mlibc_configure_probe_time,
    (void (*)(void))mlibc_configure_probe_nsec,
    (void (*)(void))mlibc_configure_probe_usec,
    (void (*)(void))mlibc_configure_probe_select_macros,
};
