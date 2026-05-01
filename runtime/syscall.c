#include "syscall.h"

#include <asm/unistd.h>
#include <linux/fcntl.h>
#include <linux/time_types.h>

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/poll.h>
#include <sys/select.h>

#include "../fs/fdtable.h"
#include "../fs/pipe.h"
#include "../fs/poll.h"
#include "../fs/vfs.h"
#include "../kernel/task.h"

extern int openat_impl(int dirfd, const char *pathname, int flags, linux_mode_t mode);
extern ssize_t read_impl(int fd, void *buf, size_t count);
extern ssize_t write_impl(int fd, const void *buf, size_t count);
extern int fcntl_impl(int fd, int cmd, ...);
extern int fstat_impl(int fd, struct linux_stat *statbuf);
extern int fstatat_impl(int dirfd, const char *pathname, struct linux_stat *statbuf, int flags);
extern ssize_t readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz);
extern int execve(const char *pathname, char *const argv[], char *const envp[]);

static long syscall_result(long ret) {
    if (ret < 0) {
        int saved_errno = errno;
        if (saved_errno <= 0) {
            saved_errno = EINVAL;
        }
        return -(long)saved_errno;
    }
    return ret;
}

static int ppoll_timeout_ms(const struct __kernel_timespec *timeout) {
    int64_t ms;

    if (!timeout) {
        return -1;
    }
    if (timeout->tv_sec < 0 || timeout->tv_nsec < 0 || timeout->tv_nsec >= 1000000000L) {
        errno = EINVAL;
        return -2;
    }
    if (timeout->tv_sec > (INT64_MAX / 1000)) {
        return INT32_MAX;
    }
    ms = (int64_t)timeout->tv_sec * 1000;
    ms += (timeout->tv_nsec + 999999L) / 1000000L;
    if (ms > INT32_MAX) {
        return INT32_MAX;
    }
    return (int)ms;
}

long syscall_dispatch_impl(long number,
                           long arg0,
                           long arg1,
                           long arg2,
                           long arg3,
                           long arg4,
                           long arg5) {
    (void)arg4;
    (void)arg5;

    switch (number) {
    case __NR_read:
        return syscall_result((long)read_impl((int)arg0, (void *)(uintptr_t)arg1, (size_t)arg2));
    case __NR_write:
        return syscall_result((long)write_impl((int)arg0, (const void *)(uintptr_t)arg1, (size_t)arg2));
    case __NR_openat:
        return syscall_result((long)openat_impl((int)arg0, (const char *)(uintptr_t)arg1,
                                                (int)arg2, (linux_mode_t)arg3));
    case __NR_close:
        return syscall_result((long)close_impl((int)arg0));
    case __NR_pipe2:
        return syscall_result((long)pipe2_impl((int *)(uintptr_t)arg0, (int)arg1));
    case __NR_fcntl:
        return syscall_result((long)fcntl_impl((int)arg0, (int)arg1, (int)arg2));
    case __NR_ppoll: {
        int timeout_ms = ppoll_timeout_ms((const struct __kernel_timespec *)(uintptr_t)arg2);
        if (timeout_ms == -2) {
            return -(long)errno;
        }
        return syscall_result((long)poll_impl((struct pollfd *)(uintptr_t)arg0, (nfds_t)arg1, timeout_ms));
    }
    case __NR_readlinkat:
        return syscall_result((long)readlinkat((int)arg0, (const char *)(uintptr_t)arg1,
                                               (char *)(uintptr_t)arg2, (size_t)arg3));
    case __NR_newfstatat:
        return syscall_result((long)fstatat_impl((int)arg0, (const char *)(uintptr_t)arg1,
                                                 (struct linux_stat *)(uintptr_t)arg2, (int)arg3));
    case __NR_fstat:
        return syscall_result((long)fstat_impl((int)arg0, (struct linux_stat *)(uintptr_t)arg1));
    case __NR_execve:
        return syscall_result((long)execve((const char *)(uintptr_t)arg0,
                                           (char *const *)(uintptr_t)arg1,
                                           (char *const *)(uintptr_t)arg2));
    case __NR_wait4:
        return syscall_result((long)wait4_impl((int32_t)arg0, (int *)(uintptr_t)arg1,
                                               (int)arg2, (void *)(uintptr_t)arg3));
    default:
        return -ENOSYS;
    }
}
