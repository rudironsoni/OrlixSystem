/*
 * MLibCPackageHeadersCompileSmoke.c
 *
 * PACKAGE-FACING LIBC HEADER COMPILE TEST
 *
 * This file proves that Linux-oriented package-facing libc headers resolve
 * through the OrlixMLibC bootstrap surface rather than through Darwin SDK
 * headers. It is a pure C compile-smoke test.
 */

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/mount.h>
#include <sys/resource.h>
#include <sys/times.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>

#ifndef AT_EMPTY_PATH
#error "AT_EMPTY_PATH must resolve through the package-facing fcntl.h owner"
#endif

#ifndef POLLRDHUP
#error "POLLRDHUP must resolve through the package-facing poll.h owner"
#endif

#ifndef SIG_BLOCK
#error "SIG_BLOCK must resolve through the package-facing signal.h owner"
#endif

#ifndef UIO_MAXIOV
#error "UIO_MAXIOV must resolve through the package-facing sys/uio.h owner"
#endif

#ifndef MS_BIND
#error "MS_BIND must resolve through the package-facing sys/mount.h owner"
#endif

#ifndef RLIMIT_NOFILE
#error "RLIMIT_NOFILE must resolve through the package-facing sys/resource.h owner"
#endif

#ifndef WNOWAIT
#error "WNOWAIT must resolve through the package-facing sys/wait.h owner"
#endif

static uid_t mlibc_compile_smoke_uid(uid_t uid) {
    return uid;
}

static gid_t mlibc_compile_smoke_gid(gid_t gid) {
    return gid;
}

static mode_t mlibc_compile_smoke_mode(mode_t mode) {
    return mode;
}

static ssize_t mlibc_compile_smoke_iov_len(const struct iovec *iov) {
    return (ssize_t)iov->iov_len;
}

static nfds_t mlibc_compile_smoke_nfds(nfds_t count) {
    return count;
}

static int mlibc_compile_smoke_poll_mask(short events) {
    return (events & (POLLIN | POLLOUT | POLLRDHUP));
}

static int mlibc_compile_smoke_fcntl_constants(void) {
    return O_CLOEXEC | FD_CLOEXEC | AT_EMPTY_PATH;
}

static unsigned long mlibc_compile_smoke_mount_flags(unsigned long flags) {
    return flags | MS_BIND;
}

static __typeof__(((struct rlimit64 *)0)->rlim_cur) mlibc_compile_smoke_rlimit(struct rlimit64 *rlim) {
    return rlim->rlim_cur;
}

static clock_t mlibc_compile_smoke_tms(const struct tms *times) {
    return times->tms_utime;
}

static int mlibc_compile_smoke_wait_options(int options) {
    return options | WEXITED | WNOWAIT;
}

static int mlibc_compile_smoke_wait_idtype(idtype_t type) {
    return type == P_PID;
}

__attribute__((unused)) static void (*volatile mlibc_package_smoke_refs[])(void) = {
    (void (*)(void))mlibc_compile_smoke_uid,
    (void (*)(void))mlibc_compile_smoke_gid,
    (void (*)(void))mlibc_compile_smoke_mode,
    (void (*)(void))mlibc_compile_smoke_iov_len,
    (void (*)(void))mlibc_compile_smoke_nfds,
    (void (*)(void))mlibc_compile_smoke_poll_mask,
    (void (*)(void))mlibc_compile_smoke_fcntl_constants,
    (void (*)(void))mlibc_compile_smoke_mount_flags,
    (void (*)(void))mlibc_compile_smoke_rlimit,
    (void (*)(void))mlibc_compile_smoke_tms,
    (void (*)(void))mlibc_compile_smoke_wait_options,
    (void (*)(void))mlibc_compile_smoke_wait_idtype,
};
