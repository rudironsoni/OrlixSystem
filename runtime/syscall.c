#include "syscall.h"

#include <asm/unistd.h>
#include <linux/fcntl.h>
#include <linux/futex.h>
#include <linux/mman.h>
#include <linux/time_types.h>

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/poll.h>
#include <time.h>

#include "../fs/fdtable.h"
#include "../fs/pipe.h"
#include "../fs/poll.h"
#include "../fs/vfs.h"
#include "../kernel/mm.h"
#include "../kernel/signal.h"
#include "../kernel/task.h"

extern int openat_impl(int dirfd, const char *pathname, int flags, linux_mode_t mode);
extern ssize_t read_impl(int fd, void *buf, size_t count);
extern ssize_t write_impl(int fd, const void *buf, size_t count);
extern int fcntl_impl(int fd, int cmd, ...);
extern int fstat_impl(int fd, struct linux_stat *statbuf);
extern int fstatat_impl(int dirfd, const char *pathname, struct linux_stat *statbuf, int flags);
extern ssize_t getdents64_impl(int fd, void *dirp, size_t count);
extern char *getcwd_impl(char *buf, size_t size);
extern int ioctl_impl(int fd, unsigned long request, void *arg);
extern ssize_t readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz);
extern int execve(const char *pathname, char *const argv[], char *const envp[]);
extern int clock_gettime_impl(clockid_t clk_id, struct timespec *tp);

static int syscall_copy_sigset_to_mask(const uint64_t *sigset, size_t sigsetsize,
                                       struct signal_mask_bits *mask) {
    if (!mask || sigsetsize != sizeof(uint64_t)) {
        errno = EINVAL;
        return -1;
    }
    memset(mask, 0, sizeof(*mask));
    if (sigset) {
        mask->sig[0] = *sigset;
    }
    return 0;
}

static int syscall_copy_mask_to_sigset(const struct signal_mask_bits *mask, uint64_t *sigset,
                                       size_t sigsetsize) {
    if (!mask || !sigset || sigsetsize != sizeof(uint64_t)) {
        errno = EINVAL;
        return -1;
    }
    *sigset = mask->sig[0];
    return 0;
}

static long syscall_prlimit64(int32_t pid, int resource, const uint64_t *new_limit,
                              uint64_t *old_limit) {
    struct task_struct *task;

    if (pid != 0 && pid != getpid_impl()) {
        return -ESRCH;
    }
    if (resource < 0 || resource >= 16) {
        return -EINVAL;
    }
    task = get_current();
    if (!task) {
        return -ESRCH;
    }
    if (old_limit) {
        old_limit[0] = task->rlimits[resource].cur;
        old_limit[1] = task->rlimits[resource].max;
    }
    if (new_limit) {
        if (new_limit[0] > new_limit[1]) {
            return -EINVAL;
        }
        task->rlimits[resource].cur = new_limit[0];
        task->rlimits[resource].max = new_limit[1];
    }
    return 0;
}

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
    case __NR_brk:
        return syscall_result((long)(uintptr_t)brk_impl((void *)(uintptr_t)arg0));
    case __NR_set_tid_address: {
        struct task_struct *task = get_current();
        if (!task || !task->mm) {
            return -ESRCH;
        }
        task->mm->clear_child_tid = (uint64_t)(uintptr_t)arg0;
        return (long)task->pid;
    }
    case __NR_futex: {
        int op = (int)arg1 & FUTEX_CMD_MASK;
        if (!arg0) {
            return -EFAULT;
        }
        if (op == FUTEX_WAKE) {
            return 0;
        }
        if (op == FUTEX_WAIT) {
            return -EAGAIN;
        }
        return -ENOSYS;
    }
    case __NR_rt_sigaction: {
        struct signal_action_slot act;
        struct signal_action_slot oldact;
        const struct signal_action_slot *act_ptr = NULL;
        struct signal_action_slot *oldact_ptr = NULL;

        if (arg3 != sizeof(uint64_t)) {
            return -EINVAL;
        }
        if (arg1) {
            memcpy(&act, (const void *)(uintptr_t)arg1, sizeof(act));
            act_ptr = &act;
        }
        if (arg2) {
            oldact_ptr = &oldact;
        }
        if (do_sigaction((int32_t)arg0, act_ptr, oldact_ptr) != 0) {
            return -(long)errno;
        }
        if (arg2) {
            memcpy((void *)(uintptr_t)arg2, &oldact, sizeof(oldact));
        }
        return 0;
    }
    case __NR_rt_sigprocmask: {
        struct signal_mask_bits set;
        struct signal_mask_bits oldset;
        const struct signal_mask_bits *set_ptr = NULL;
        struct signal_mask_bits *oldset_ptr = NULL;

        if (arg3 != sizeof(uint64_t)) {
            return -EINVAL;
        }
        if (arg1) {
            if (syscall_copy_sigset_to_mask((const uint64_t *)(uintptr_t)arg1, (size_t)arg3, &set) != 0) {
                return -(long)errno;
            }
            set_ptr = &set;
        }
        if (arg2) {
            oldset_ptr = &oldset;
        }
        if (do_sigprocmask((int)arg0, set_ptr, oldset_ptr) != 0) {
            return -(long)errno;
        }
        if (arg2 && syscall_copy_mask_to_sigset(&oldset, (uint64_t *)(uintptr_t)arg2, (size_t)arg3) != 0) {
            return -(long)errno;
        }
        return 0;
    }
    case __NR_ioctl:
        return syscall_result((long)ioctl_impl((int)arg0, (unsigned long)arg1, (void *)(uintptr_t)arg2));
    case __NR_getdents64:
        return syscall_result((long)getdents64_impl((int)arg0, (void *)(uintptr_t)arg1, (size_t)arg2));
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
    case __NR_getcwd: {
        char *buf = (char *)(uintptr_t)arg0;
        size_t size = (size_t)arg1;
        if (!getcwd_impl(buf, size)) {
            return -(long)errno;
        }
        return (long)strlen(buf) + 1;
    }
    case __NR_getpid:
        return (long)getpid_impl();
    case __NR_getppid:
        return (long)getppid_impl();
    case __NR_mmap:
        return syscall_result((long)(uintptr_t)mmap_impl((void *)(uintptr_t)arg0, (size_t)arg1,
                                                         (int)arg2, (int)arg3, (int)arg4,
                                                         (int64_t)arg5));
    case __NR_mprotect:
        return syscall_result((long)mprotect_impl((void *)(uintptr_t)arg0, (size_t)arg1, (int)arg2));
    case __NR_munmap:
        return syscall_result((long)munmap_impl((void *)(uintptr_t)arg0, (size_t)arg1));
    case __NR_prlimit64:
        return syscall_prlimit64((int32_t)arg0, (int)arg1,
                                 (const uint64_t *)(uintptr_t)arg2,
                                 (uint64_t *)(uintptr_t)arg3);
    case __NR_clock_gettime: {
        struct timespec host_ts;
        struct __kernel_timespec *linux_ts = (struct __kernel_timespec *)(uintptr_t)arg1;
        long ret;

        if (!linux_ts) {
            return -EFAULT;
        }
        ret = syscall_result((long)clock_gettime_impl((clockid_t)arg0, &host_ts));
        if (ret < 0) {
            return ret;
        }
        linux_ts->tv_sec = host_ts.tv_sec;
        linux_ts->tv_nsec = host_ts.tv_nsec;
        return 0;
    }
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
