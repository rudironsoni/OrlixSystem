#include "syscall.h"

#include <asm/unistd.h>
#define sigaction syscall_sigaction_frame
#define sigset_t syscall_sigset_frame
#define stack_t syscall_sigaltstack_frame
#include <asm/signal.h>
#undef sigaction
#undef sigset_t
#undef stack_t
#ifdef SIGHUP
#undef SIGHUP
#endif
#ifdef SIGINT
#undef SIGINT
#endif
#ifdef SIGQUIT
#undef SIGQUIT
#endif
#ifdef SIGILL
#undef SIGILL
#endif
#ifdef SIGTRAP
#undef SIGTRAP
#endif
#ifdef SIGABRT
#undef SIGABRT
#endif
#ifdef SIGIOT
#undef SIGIOT
#endif
#ifdef SIGBUS
#undef SIGBUS
#endif
#ifdef SIGFPE
#undef SIGFPE
#endif
#ifdef SIGKILL
#undef SIGKILL
#endif
#ifdef SIGUSR1
#undef SIGUSR1
#endif
#ifdef SIGSEGV
#undef SIGSEGV
#endif
#ifdef SIGUSR2
#undef SIGUSR2
#endif
#ifdef SIGPIPE
#undef SIGPIPE
#endif
#ifdef SIGALRM
#undef SIGALRM
#endif
#ifdef SIGTERM
#undef SIGTERM
#endif
#ifdef SIGCHLD
#undef SIGCHLD
#endif
#ifdef SIGCONT
#undef SIGCONT
#endif
#ifdef SIGSTOP
#undef SIGSTOP
#endif
#ifdef SIGTSTP
#undef SIGTSTP
#endif
#ifdef SIGTTIN
#undef SIGTTIN
#endif
#ifdef SIGTTOU
#undef SIGTTOU
#endif
#ifdef SIGURG
#undef SIGURG
#endif
#ifdef SIGXCPU
#undef SIGXCPU
#endif
#ifdef SIGXFSZ
#undef SIGXFSZ
#endif
#ifdef SIGVTALRM
#undef SIGVTALRM
#endif
#ifdef SIGPROF
#undef SIGPROF
#endif
#ifdef SIGWINCH
#undef SIGWINCH
#endif
#ifdef SIGIO
#undef SIGIO
#endif
#ifdef SIGPOLL
#undef SIGPOLL
#endif
#ifdef SIGPWR
#undef SIGPWR
#endif
#ifdef SIGSYS
#undef SIGSYS
#endif
#ifdef SIGUNUSED
#undef SIGUNUSED
#endif
#ifdef SIG_BLOCK
#undef SIG_BLOCK
#endif
#ifdef SIG_UNBLOCK
#undef SIG_UNBLOCK
#endif
#ifdef SIG_SETMASK
#undef SIG_SETMASK
#endif
#ifdef SIG_DFL
#undef SIG_DFL
#endif
#ifdef SIG_IGN
#undef SIG_IGN
#endif
#ifdef SIG_ERR
#undef SIG_ERR
#endif
#ifdef SA_NOCLDSTOP
#undef SA_NOCLDSTOP
#endif
#ifdef SA_NOCLDWAIT
#undef SA_NOCLDWAIT
#endif
#ifdef SA_SIGINFO
#undef SA_SIGINFO
#endif
#ifdef SA_ONSTACK
#undef SA_ONSTACK
#endif
#ifdef SA_RESTART
#undef SA_RESTART
#endif
#ifdef SA_NODEFER
#undef SA_NODEFER
#endif
#ifdef SA_RESETHAND
#undef SA_RESETHAND
#endif
#ifdef SA_NOMASK
#undef SA_NOMASK
#endif
#ifdef SA_ONESHOT
#undef SA_ONESHOT
#endif
#ifdef SA_RESTORER
#undef SA_RESTORER
#endif
#include <linux/fcntl.h>
#include <linux/futex.h>
#include <linux/mount.h>
#include <linux/mman.h>
#include <linux/sched.h>
#include <linux/time_types.h>
#include <linux/xattr.h>

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>

#include "../fs/fdtable.h"
#include "../fs/eventpoll.h"
#include "../fs/pipe.h"
#include "../fs/poll.h"
#include "../fs/vfs.h"
#include "../kernel/futex.h"
#include "../kernel/mm.h"
#include "../kernel/seccomp.h"
#include "../kernel/signal.h"
#include "../kernel/task.h"

extern int openat_impl(int dirfd, const char *pathname, int flags, linux_mode_t mode);
extern ssize_t read_impl(int fd, void *buf, size_t count);
extern ssize_t write_impl(int fd, const void *buf, size_t count);
extern ssize_t pread_impl(int fd, void *buf, size_t count, long long offset);
extern ssize_t pwrite_impl(int fd, const void *buf, size_t count, long long offset);
extern int fcntl_impl(int fd, int cmd, ...);
extern int fstat_impl(int fd, struct linux_stat *statbuf);
extern int fstatat_impl(int dirfd, const char *pathname, struct linux_stat *statbuf, int flags);
extern int ftruncate_impl(int fd, linux_off_t length);
extern ssize_t getdents64_impl(int fd, void *dirp, size_t count);
extern char *getcwd_impl(char *buf, size_t size);
extern int ioctl_impl(int fd, unsigned long request, void *arg);
extern ssize_t readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz);
extern int execve(const char *pathname, char *const argv[], char *const envp[]);
extern int nanosleep_impl(const struct timespec *req, struct timespec *rem);
extern int clock_gettime_impl(clockid_t clk_id, struct timespec *tp);
extern int mount_setattr(int dirfd, const char *pathname, unsigned int flags,
                         struct mount_attr *attr, size_t size);
extern int open_tree(int dirfd, const char *pathname, unsigned int flags);
extern int move_mount(int from_dirfd, const char *from_pathname, int to_dirfd,
                      const char *to_pathname, unsigned int flags);
extern int pivot_root(const char *new_root, const char *put_old);
extern int setxattr_impl(const char *path, const char *name, const void *value, size_t size, int flags);
extern int lsetxattr_impl(const char *path, const char *name, const void *value, size_t size, int flags);
extern int fsetxattr_impl(int fd, const char *name, const void *value, size_t size, int flags);
extern long getxattr_impl(const char *path, const char *name, void *value, size_t size);
extern long lgetxattr_impl(const char *path, const char *name, void *value, size_t size);
extern long fgetxattr_impl(int fd, const char *name, void *value, size_t size);
extern long listxattr_impl(const char *path, char *list, size_t size);
extern long llistxattr_impl(const char *path, char *list, size_t size);
extern long flistxattr_impl(int fd, char *list, size_t size);
extern int removexattr_impl(const char *path, const char *name);
extern int lremovexattr_impl(const char *path, const char *name);
extern int fremovexattr_impl(int fd, const char *name);

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

static void syscall_sigaction_from_linux(const struct syscall_sigaction_frame *linux_act,
                                         struct signal_action_slot *act) {
    memset(act, 0, sizeof(*act));
    if (!linux_act) {
        return;
    }
    act->handler = (sighandler_t)linux_act->sa_handler;
    act->flags = (int32_t)linux_act->sa_flags;
    act->restorer = (uint64_t)(uintptr_t)linux_act->sa_restorer;
    act->mask.sig[0] = linux_act->sa_mask.sig[0];
}

static void syscall_sigaction_to_linux(const struct signal_action_slot *act,
                                       struct syscall_sigaction_frame *linux_act) {
    memset(linux_act, 0, sizeof(*linux_act));
    if (!act) {
        return;
    }
    linux_act->sa_handler = (__sighandler_t)act->handler;
    linux_act->sa_flags = (unsigned long)act->flags;
    linux_act->sa_restorer = (__sigrestore_t)(uintptr_t)act->restorer;
    linux_act->sa_mask.sig[0] = act->mask.sig[0];
}

static void syscall_sigaltstack_from_linux(const syscall_sigaltstack_frame *linux_stack,
                                           struct signal_altstack *stack) {
    memset(stack, 0, sizeof(*stack));
    if (!linux_stack) {
        return;
    }
    stack->ss_sp = linux_stack->ss_sp;
    stack->ss_size = linux_stack->ss_size;
    stack->ss_flags = linux_stack->ss_flags;
}

static void syscall_sigaltstack_to_linux(const struct signal_altstack *stack,
                                         syscall_sigaltstack_frame *linux_stack) {
    memset(linux_stack, 0, sizeof(*linux_stack));
    if (!stack) {
        return;
    }
    linux_stack->ss_sp = stack->ss_sp;
    linux_stack->ss_size = stack->ss_size;
    linux_stack->ss_flags = stack->ss_flags;
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

struct syscall_sigmask_arg {
    const uint64_t *ss;
    size_t ss_len;
};

static int syscall_timespec_to_timeval(const struct __kernel_timespec *timeout, struct timeval *timeval) {
    if (!timeout || !timeval) {
        return 0;
    }
    if (timeout->tv_sec < 0 || timeout->tv_nsec < 0 || timeout->tv_nsec >= 1000000000L) {
        errno = EINVAL;
        return -1;
    }
    timeval->tv_sec = timeout->tv_sec;
    timeval->tv_usec = (int)((timeout->tv_nsec + 999L) / 1000L);
    if (timeval->tv_usec >= 1000000L) {
        timeval->tv_sec++;
        timeval->tv_usec -= 1000000L;
    }
    return 0;
}

static int syscall_apply_temporary_sigmask(const struct syscall_sigmask_arg *arg,
                                           struct signal_mask_bits *old_mask,
                                           int *changed) {
    struct signal_mask_bits new_mask;

    *changed = 0;
    if (!arg) {
        return 0;
    }
    if (!arg->ss) {
        return 0;
    }
    if (arg->ss_len != sizeof(uint64_t)) {
        errno = EINVAL;
        return -1;
    }
    if (syscall_copy_sigset_to_mask(arg->ss, arg->ss_len, &new_mask) != 0) {
        return -1;
    }
    if (do_sigsetmask(&new_mask, old_mask) != 0) {
        return -1;
    }
    *changed = 1;
    return 0;
}

static void syscall_restore_sigmask(const struct signal_mask_bits *old_mask, int changed) {
    if (changed) {
        do_sigsetmask(old_mask, NULL);
    }
}

enum syscall_capability_class syscall_capability_class_impl(long number) {
    switch (number) {
    case __NR_read:
    case __NR_write:
    case __NR_pread64:
    case __NR_pwrite64:
    case __NR_openat:
    case __NR_close:
    case __NR_pipe2:
    case __NR_fcntl:
    case __NR_ioctl:
    case __NR_getdents64:
    case __NR_readlinkat:
    case __NR_newfstatat:
    case __NR_fstat:
    case __NR_getcwd:
    case __NR_ftruncate:
        return SYSCALL_CAPABILITY_FD;
    case __NR_brk:
    case __NR_mmap:
    case __NR_mprotect:
    case __NR_munmap:
    case __NR_mremap:
    case __NR_madvise:
    case __NR_mincore:
    case __NR_msync:
        return SYSCALL_CAPABILITY_VM;
    case __NR_set_tid_address:
    case __NR_execve:
    case __NR_wait4:
    case __NR_clone3:
    case __NR_getpid:
    case __NR_getppid:
        return SYSCALL_CAPABILITY_PROCESS;
    case __NR_rt_sigaction:
    case __NR_sigaltstack:
    case __NR_rt_sigreturn:
    case __NR_restart_syscall:
    case __NR_rt_sigprocmask:
        return SYSCALL_CAPABILITY_SIGNAL;
    case __NR_ppoll:
    case __NR_pselect6:
    case __NR_epoll_create1:
    case __NR_epoll_ctl:
    case __NR_epoll_pwait:
    case __NR_futex:
        return SYSCALL_CAPABILITY_READINESS;
    case __NR_mount_setattr:
    case __NR_open_tree:
    case __NR_move_mount:
    case __NR_pivot_root:
    case __NR_listmount:
    case __NR_statmount:
        return SYSCALL_CAPABILITY_MOUNT;
    case __NR_setxattr:
    case __NR_lsetxattr:
    case __NR_fsetxattr:
    case __NR_getxattr:
    case __NR_lgetxattr:
    case __NR_fgetxattr:
    case __NR_listxattr:
    case __NR_llistxattr:
    case __NR_flistxattr:
    case __NR_removexattr:
    case __NR_lremovexattr:
    case __NR_fremovexattr:
        return SYSCALL_CAPABILITY_XATTR;
    case __NR_clock_gettime:
        return SYSCALL_CAPABILITY_TIME;
    case __NR_prlimit64:
    case __NR_set_robust_list:
    case __NR_get_robust_list:
        return SYSCALL_CAPABILITY_RESOURCE;
    default:
        return SYSCALL_CAPABILITY_NONE;
    }
}

int syscall_is_implemented_impl(long number) {
    return syscall_capability_class_impl(number) != SYSCALL_CAPABILITY_NONE;
}

long syscall_dispatch_impl(long number,
                           long arg0,
                           long arg1,
                           long arg2,
                           long arg3,
                           long arg4,
                           long arg5) {
    long seccomp_ret = seccomp_check_current_syscall(number);

    if (seccomp_ret < 0) {
        return seccomp_ret;
    }

    switch (number) {
    case __NR_read:
        return syscall_result((long)read_impl((int)arg0, (void *)(uintptr_t)arg1, (size_t)arg2));
    case __NR_write:
        return syscall_result((long)write_impl((int)arg0, (const void *)(uintptr_t)arg1, (size_t)arg2));
    case __NR_pread64:
        return syscall_result((long)pread_impl((int)arg0, (void *)(uintptr_t)arg1, (size_t)arg2,
                                               (long long)arg3));
    case __NR_pwrite64:
        return syscall_result((long)pwrite_impl((int)arg0, (const void *)(uintptr_t)arg1, (size_t)arg2,
                                                (long long)arg3));
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
        if (!task) {
            return -ESRCH;
        }
        task->clear_child_tid = (uint64_t)(uintptr_t)arg0;
        return (long)task->pid;
    }
    case __NR_futex: {
        int op = (int)arg1 & FUTEX_CMD_MASK;
        int timeout_ms = -1;
        if (!arg0) {
            return -EFAULT;
        }
        if (op == FUTEX_WAIT && arg3) {
            timeout_ms = ppoll_timeout_ms((const struct __kernel_timespec *)(uintptr_t)arg3);
            if (timeout_ms == -2) {
                return -(long)errno;
            }
        }
        if (op == FUTEX_WAIT) {
            return syscall_result((long)futex_wait_impl((int *)(uintptr_t)arg0, (int)arg2, timeout_ms));
        }
        if (op == FUTEX_WAKE) {
            return syscall_result((long)futex_wake_impl((int *)(uintptr_t)arg0, (int)arg2));
        }
        return -ENOSYS;
    }
    case __NR_set_robust_list:
        return syscall_result((long)set_robust_list_impl((void *)(uintptr_t)arg0, (unsigned long)arg1));
    case __NR_get_robust_list:
        return syscall_result((long)get_robust_list_impl((int)arg0, (void **)(uintptr_t)arg1,
                                                         (unsigned long *)(uintptr_t)arg2));
    case __NR_rt_sigaction: {
        struct signal_action_slot act;
        struct signal_action_slot oldact;
        const struct signal_action_slot *act_ptr = NULL;
        struct signal_action_slot *oldact_ptr = NULL;

        if (arg3 != sizeof(uint64_t)) {
            return -EINVAL;
        }
        if (arg1) {
            syscall_sigaction_from_linux((const struct syscall_sigaction_frame *)(uintptr_t)arg1, &act);
            act_ptr = &act;
        }
        if (arg2) {
            oldact_ptr = &oldact;
        }
        if (do_sigaction((int32_t)arg0, act_ptr, oldact_ptr) != 0) {
            return -(long)errno;
        }
        if (arg2) {
            syscall_sigaction_to_linux(&oldact, (struct syscall_sigaction_frame *)(uintptr_t)arg2);
        }
        return 0;
    }
    case __NR_sigaltstack: {
        struct signal_altstack new_stack;
        struct signal_altstack old_stack;
        const struct signal_altstack *new_stack_ptr = NULL;
        struct signal_altstack *old_stack_ptr = NULL;

        if (arg0) {
            syscall_sigaltstack_from_linux((const syscall_sigaltstack_frame *)(uintptr_t)arg0, &new_stack);
            new_stack_ptr = &new_stack;
        }
        if (arg1) {
            old_stack_ptr = &old_stack;
        }
        if (do_sigaltstack(new_stack_ptr, old_stack_ptr) != 0) {
            return -(long)errno;
        }
        if (arg1) {
            syscall_sigaltstack_to_linux(&old_stack, (syscall_sigaltstack_frame *)(uintptr_t)arg1);
        }
        return 0;
    }
    case __NR_rt_sigreturn: {
        struct task_struct *task = get_current();
        if (!task || !task->mm) {
            return -ESRCH;
        }
        if (task->signal) {
            task->signal->blocked.sig[0] = task->mm->signal_frame_mask;
            task->signal->altstack.ss_flags &= ~1;
        }
        return (long)task->mm->signal_frame_return_pc;
    }
    case __NR_restart_syscall: {
        struct task_struct *task = get_current();
        enum task_restart_kind kind;
        uint64_t arg0;
        uint64_t arg1;
        uint64_t arg2;
        uint64_t arg3;
        uint64_t arg4;
        uint64_t arg5;
        long ret;

        if (!task || !task->mm) {
            return -ESRCH;
        }
        kind = (enum task_restart_kind)task->mm->signal_frame_restart_kind;
        if (kind == TASK_RESTART_NONE) {
            return -EINTR;
        }
        arg0 = task->mm->signal_frame_restart_arg0;
        arg1 = task->mm->signal_frame_restart_arg1;
        arg2 = task->mm->signal_frame_restart_arg2;
        arg3 = task->mm->signal_frame_restart_arg3;
        arg4 = task->mm->signal_frame_restart_arg4;
        arg5 = task->mm->signal_frame_restart_arg5;
        task_restart_clear_impl(task);

        switch (kind) {
        case TASK_RESTART_NANOSLEEP:
            ret = nanosleep_impl((const struct timespec *)(uintptr_t)arg0,
                                 (struct timespec *)(uintptr_t)arg1);
            return ret < 0 ? -(long)errno : ret;
        case TASK_RESTART_POLL:
            ret = poll_impl((struct pollfd *)(uintptr_t)arg0, (nfds_t)arg1, (int)arg2);
            return ret < 0 ? -(long)errno : ret;
        case TASK_RESTART_PIPE_READ:
            ret = read_impl((int)arg0, (void *)(uintptr_t)arg1, (size_t)arg2);
            return ret < 0 ? -(long)errno : ret;
        case TASK_RESTART_PIPE_WRITE:
            ret = write_impl((int)arg0, (const void *)(uintptr_t)arg1, (size_t)arg2);
            return ret < 0 ? -(long)errno : ret;
        case TASK_RESTART_FUTEX_WAIT:
            ret = futex_wait_impl((int *)(uintptr_t)arg0, (int)arg1, (int)arg2);
            return ret < 0 ? -(long)errno : ret;
        case TASK_RESTART_WAITPID:
            ret = waitpid_impl((int32_t)arg0, (int *)(uintptr_t)arg1, (int)arg2);
            return ret < 0 ? -(long)errno : ret;
        case TASK_RESTART_SELECT:
            ret = select_impl((int)arg0, (fd_set *)(uintptr_t)arg1,
                              (fd_set *)(uintptr_t)arg2, (fd_set *)(uintptr_t)arg3,
                              (struct timeval *)(uintptr_t)arg4);
            return ret < 0 ? -(long)errno : ret;
        case TASK_RESTART_EPOLL_WAIT:
            ret = epoll_wait_impl((int)arg0, (struct epoll_event *)(uintptr_t)arg1,
                                  (int)arg2, (int)arg3);
            return ret < 0 ? -(long)errno : ret;
        default:
            (void)arg4;
            (void)arg5;
            return -EINTR;
        }
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
    case __NR_pselect6: {
        struct timeval timeout_value;
        struct timeval *timeout_ptr = NULL;
        const struct syscall_sigmask_arg *sigmask_arg = (const struct syscall_sigmask_arg *)(uintptr_t)arg5;
        struct signal_mask_bits old_mask;
        int mask_changed = 0;
        int ret;

        if (arg4) {
            if (syscall_timespec_to_timeval((const struct __kernel_timespec *)(uintptr_t)arg4,
                                            &timeout_value) != 0) {
                return -(long)errno;
            }
            timeout_ptr = &timeout_value;
        }
        if (syscall_apply_temporary_sigmask(sigmask_arg, &old_mask, &mask_changed) != 0) {
            return -(long)errno;
        }
        ret = select_impl((int)arg0, (fd_set *)(uintptr_t)arg1, (fd_set *)(uintptr_t)arg2,
                          (fd_set *)(uintptr_t)arg3, timeout_ptr);
        syscall_restore_sigmask(&old_mask, mask_changed);
        return syscall_result((long)ret);
    }
    case __NR_epoll_create1:
        return syscall_result((long)epoll_create1_impl((int)arg0));
    case __NR_epoll_ctl:
        return syscall_result((long)epoll_ctl_impl((int)arg0, (int)arg1, (int)arg2,
                                                   (struct epoll_event *)(uintptr_t)arg3));
    case __NR_epoll_pwait: {
        const struct syscall_sigmask_arg sigmask_arg = {
            .ss = (const uint64_t *)(uintptr_t)arg4,
            .ss_len = (size_t)arg5,
        };
        struct signal_mask_bits old_mask;
        int mask_changed = 0;
        int ret;

        if (syscall_apply_temporary_sigmask(arg4 ? &sigmask_arg : NULL, &old_mask, &mask_changed) != 0) {
            return -(long)errno;
        }
        ret = epoll_pwait_impl((int)arg0, (struct epoll_event *)(uintptr_t)arg1,
                               (int)arg2, (int)arg3);
        syscall_restore_sigmask(&old_mask, mask_changed);
        return syscall_result((long)ret);
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
    case __NR_mremap:
        return syscall_result((long)(uintptr_t)mremap_impl((void *)(uintptr_t)arg0, (size_t)arg1,
                                                           (size_t)arg2, (int)arg3,
                                                           (void *)(uintptr_t)arg4));
    case __NR_madvise:
        return syscall_result((long)madvise_impl((void *)(uintptr_t)arg0, (size_t)arg1, (int)arg2));
    case __NR_mincore:
        return syscall_result((long)mincore_impl((void *)(uintptr_t)arg0, (size_t)arg1,
                                                 (unsigned char *)(uintptr_t)arg2));
    case __NR_mount_setattr:
        return syscall_result((long)mount_setattr((int)arg0, (const char *)(uintptr_t)arg1,
                                                  (unsigned int)arg2,
                                                  (struct mount_attr *)(uintptr_t)arg3,
                                                  (size_t)arg4));
    case __NR_open_tree:
        return syscall_result((long)open_tree((int)arg0, (const char *)(uintptr_t)arg1,
                                              (unsigned int)arg2));
    case __NR_move_mount:
        return syscall_result((long)move_mount((int)arg0, (const char *)(uintptr_t)arg1,
                                               (int)arg2, (const char *)(uintptr_t)arg3,
                                               (unsigned int)arg4));
    case __NR_pivot_root:
        return syscall_result((long)pivot_root((const char *)(uintptr_t)arg0,
                                               (const char *)(uintptr_t)arg1));
    case __NR_listmount:
        return syscall_result(vfs_listmount((const struct mnt_id_req *)(uintptr_t)arg0,
                                            (uint64_t *)(uintptr_t)arg1,
                                            (size_t)arg2, (unsigned int)arg3));
    case __NR_statmount:
        return syscall_result((long)vfs_statmount((const struct mnt_id_req *)(uintptr_t)arg0,
                                                  (struct statmount *)(uintptr_t)arg1,
                                                  (size_t)arg2, (unsigned int)arg3));
    case __NR_msync:
        return syscall_result((long)msync_impl((void *)(uintptr_t)arg0, (size_t)arg1, (int)arg2));
    case __NR_ftruncate:
        return syscall_result((long)ftruncate_impl((int)arg0, (linux_off_t)arg1));
    case __NR_setxattr:
        return syscall_result((long)setxattr_impl((const char *)(uintptr_t)arg0,
                                                  (const char *)(uintptr_t)arg1,
                                                  (const void *)(uintptr_t)arg2,
                                                  (size_t)arg3, (int)arg4));
    case __NR_lsetxattr:
        return syscall_result((long)lsetxattr_impl((const char *)(uintptr_t)arg0,
                                                   (const char *)(uintptr_t)arg1,
                                                   (const void *)(uintptr_t)arg2,
                                                   (size_t)arg3, (int)arg4));
    case __NR_fsetxattr:
        return syscall_result((long)fsetxattr_impl((int)arg0, (const char *)(uintptr_t)arg1,
                                                   (const void *)(uintptr_t)arg2,
                                                   (size_t)arg3, (int)arg4));
    case __NR_getxattr:
        return syscall_result(getxattr_impl((const char *)(uintptr_t)arg0,
                                            (const char *)(uintptr_t)arg1,
                                            (void *)(uintptr_t)arg2, (size_t)arg3));
    case __NR_lgetxattr:
        return syscall_result(lgetxattr_impl((const char *)(uintptr_t)arg0,
                                             (const char *)(uintptr_t)arg1,
                                             (void *)(uintptr_t)arg2, (size_t)arg3));
    case __NR_fgetxattr:
        return syscall_result(fgetxattr_impl((int)arg0, (const char *)(uintptr_t)arg1,
                                             (void *)(uintptr_t)arg2, (size_t)arg3));
    case __NR_listxattr:
        return syscall_result(listxattr_impl((const char *)(uintptr_t)arg0,
                                             (char *)(uintptr_t)arg1, (size_t)arg2));
    case __NR_llistxattr:
        return syscall_result(llistxattr_impl((const char *)(uintptr_t)arg0,
                                              (char *)(uintptr_t)arg1, (size_t)arg2));
    case __NR_flistxattr:
        return syscall_result(flistxattr_impl((int)arg0, (char *)(uintptr_t)arg1,
                                              (size_t)arg2));
    case __NR_removexattr:
        return syscall_result((long)removexattr_impl((const char *)(uintptr_t)arg0,
                                                     (const char *)(uintptr_t)arg1));
    case __NR_lremovexattr:
        return syscall_result((long)lremovexattr_impl((const char *)(uintptr_t)arg0,
                                                      (const char *)(uintptr_t)arg1));
    case __NR_fremovexattr:
        return syscall_result((long)fremovexattr_impl((int)arg0, (const char *)(uintptr_t)arg1));
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
    case __NR_clone3:
        return syscall_result((long)clone3_impl((const struct clone_args *)(uintptr_t)arg0,
                                                (size_t)arg1));
    default:
        return -ENOSYS;
    }
}
