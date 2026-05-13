#include "syscall.h"

#include <uapi/asm/unistd.h>
#include <linux/errno.h>
#include <uapi/linux/fcntl.h>
#include <uapi/linux/futex.h>
#include <uapi/linux/mount.h>
#include <uapi/linux/openat2.h>
#include <uapi/linux/sched.h>
#include <uapi/linux/signal.h>
#include <uapi/linux/stat.h>
#include <uapi/linux/time.h>
#include <linux/types.h>
#include <linux/string.h>

#include "../fs/fdtable.h"
#include "../fs/pipe.h"
#include "../fs/poll.h"
#include "../fs/vfs.h"
#include "../kernel/cred.h"
#include "../kernel/futex.h"
#include "../kernel/mm.h"
#include "../kernel/ptrace.h"
#include "../private/kernel/ptrace_state.h"
#include "../kernel/resource.h"
#include "../kernel/seccomp.h"
#include "../kernel/signal.h"
#include "../private/kernel/signal_state.h"
#include "../kernel/task.h"
#include "../private/kernel/task_state.h"
#include "../kernel/uts.h"
#include "../kernel/wait.h"

extern int mount_impl(const char *source, const char *target,
                      const char *filesystemtype, unsigned long mountflags,
                      const void *data);
extern int umount2_impl(const char *target, int flags);

struct epoll_instance;
struct epoll_event;
extern int openat_impl(int dirfd, const char *pathname, int flags, uint32_t mode);
extern int epoll_create1_impl(int flags);
extern int epoll_ctl_impl(int epfd, int op, int fd, struct epoll_event *event);
extern int epoll_wait_impl(int epfd, struct epoll_event *events, int maxevents, int timeout);
extern int epoll_pwait_impl(int epfd, struct epoll_event *events, int maxevents, int timeout);
extern long sys_socket(int domain, int type, int protocol);
extern long sys_socketpair(int domain, int type, int protocol, int *sv);
extern long sys_connect(int sockfd, void *addr, int addrlen);
extern long sys_bind(int sockfd, void *addr, int addrlen);
extern long sys_listen(int sockfd, int backlog);
extern long sys_accept(int sockfd, void *addr, int *addrlen);
extern long sys_accept4(int sockfd, void *addr, int *addrlen, int flags);
extern long sys_shutdown(int sockfd, int how);
extern long sys_sendto(int sockfd, void *buf, size_t len, unsigned int flags,
                       void *dest_addr, int addrlen);
extern long sys_recvfrom(int sockfd, void *buf, size_t len, unsigned int flags,
                         void *src_addr, int *addrlen);
extern long sys_sendmsg(int sockfd, void *msg, unsigned int flags);
extern long sys_recvmsg(int sockfd, void *msg, unsigned int flags);
extern long sys_sendmmsg(int sockfd, void *msgvec, unsigned int vlen, unsigned int flags);
extern long sys_recvmmsg(int sockfd, void *msgvec, unsigned int vlen, unsigned int flags,
                         struct __kernel_timespec *timeout);
extern long sys_getsockname(int sockfd, void *addr, int *addrlen);
extern long sys_getpeername(int sockfd, void *addr, int *addrlen);
extern long sys_setsockopt(int sockfd, int level, int optname, char *optval, int optlen);
extern long sys_getsockopt(int sockfd, int level, int optname, char *optval, int *optlen);
extern ssize_t read_impl(int fd, void *buf, size_t count);
extern ssize_t write_impl(int fd, const void *buf, size_t count);
struct iovec;
extern long readv_impl(int fd, const struct iovec *iov, int iovcnt);
extern long writev_impl(int fd, const struct iovec *iov, int iovcnt);
extern long preadv_impl(int fd, const struct iovec *iov, int iovcnt, unsigned long pos_l, unsigned long pos_h);
extern long pwritev_impl(int fd, const struct iovec *iov, int iovcnt, unsigned long pos_l, unsigned long pos_h);
extern long preadv2_impl(int fd, const struct iovec *iov, int iovcnt,
                         unsigned long pos_l, unsigned long pos_h, int flags);
extern long pwritev2_impl(int fd, const struct iovec *iov, int iovcnt,
                          unsigned long pos_l, unsigned long pos_h, int flags);
extern ssize_t pread_impl(int fd, void *buf, size_t count, long long offset);
extern ssize_t pwrite_impl(int fd, const void *buf, size_t count, long long offset);
extern int64_t lseek_impl(int fd, int64_t offset, int whence);
extern ssize_t sendfile_impl(int out_fd, int in_fd, int64_t *offset, size_t count);
extern ssize_t copy_file_range_impl(int fd_in, int64_t *off_in, int fd_out,
                                    int64_t *off_out, size_t len, unsigned int flags);
extern int fallocate_impl(int fd, int mode, int64_t offset, int64_t len);
extern int sync_file_range_impl(int fd, int64_t offset, int64_t nbytes, unsigned int flags);
extern ssize_t splice_impl(int fd_in, int64_t *off_in, int fd_out, int64_t *off_out,
                           size_t len, unsigned int flags);
extern ssize_t vmsplice_impl(int fd, const struct iovec *iov, unsigned long nr_segs, unsigned int flags);
extern ssize_t tee_impl(int fd_in, int fd_out, size_t len, unsigned int flags);
extern int fcntl_impl(int fd, int cmd, ...);
extern int flock_impl(int fd, int operation);
extern int fstat_impl(int fd, struct stat *statbuf);
extern int fstatat_impl(int dirfd, const char *pathname, struct stat *statbuf, int flags);
extern int faccessat_impl(int dirfd, const char *pathname, int mode, int flags);
extern int statx_impl(int dirfd, const char *pathname, int flags, unsigned int mask,
                      struct statx *statxbuf);
extern int truncate_impl(const char *path, int64_t length);
extern int ftruncate_impl(int fd, int64_t length);
extern ssize_t getdents64_impl(int fd, void *dirp, size_t count);
extern int getcwd_impl(char *buf, size_t size);
extern int chdir_impl(const char *path);
extern int fchdir_impl(int fd);
extern int fchmod_impl(int fd, uint32_t mode);
extern int fchmodat_impl(int dirfd, const char *pathname, uint32_t mode, int flags);
extern int fchown_impl(int fd, uint32_t owner, uint32_t group);
extern int fchownat_impl(int dirfd, const char *pathname, uint32_t owner, uint32_t group, int flags);
extern int utimensat_impl(int dirfd, const char *pathname,
                          const struct __kernel_timespec times[2], int flags);
struct statfs;
extern void sync_impl(void);
extern int fsync_impl(int fd);
extern int fdatasync_impl(int fd);
extern int syncfs_impl(int fd);
extern int statfs_impl(const char *path, struct statfs *buf);
extern int fstatfs_impl(int fd, struct statfs *buf);
extern uint32_t umask_impl(uint32_t mask);
extern int ioctl_impl(int fd, unsigned long request, void *arg);
extern ssize_t readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz);
extern int linkat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, int flags);
extern int symlinkat(const char *target, int newdirfd, const char *linkpath);
extern int chroot(const char *path);
extern int dup_impl(int oldfd);
extern int dup3_impl(int oldfd, int newfd, int flags);
extern int close_range_impl(unsigned int first, unsigned int last, unsigned int flags);
extern int eventfd2_impl(unsigned int initval, int flags);
extern int timerfd_create_impl(int clockid, int flags);
extern int timerfd_settime_impl(int fd, int flags, const struct __kernel_itimerspec *new_value,
                                struct __kernel_itimerspec *old_value);
extern int timerfd_gettime_impl(int fd, struct __kernel_itimerspec *curr_value);
extern int memfd_create_impl(const char *name, unsigned int flags);
extern int pidfd_open_impl(int32_t pid, unsigned int flags);
extern int pidfd_getfd_impl(struct task *target, int targetfd, unsigned int flags);
extern int task_pidfd_getfd_access_impl(struct task *target);
extern int mkdirat_impl(int dirfd, const char *pathname, uint32_t mode);
extern int unlinkat_impl(int dirfd, const char *pathname, int flags);
extern int renameat2_impl(int olddirfd, const char *oldpath, int newdirfd,
                          const char *newpath, unsigned int flags);
extern int execve_impl(const char *pathname, char *const argv[], char *const envp[]);
extern int execveat_impl(int dirfd, const char *pathname, char *const argv[], char *const envp[],
                         int flags);
extern void exit_impl(int status);
extern int nanosleep_impl(const struct __kernel_timespec *req, struct __kernel_timespec *rem);
extern int gettimeofday_impl(struct __kernel_old_timeval *tv, void *tz);
extern int clock_gettime_impl(__kernel_clockid_t clk_id, struct __kernel_timespec *tp);
extern int interval_timer_set_impl(int which, const struct __kernel_old_itimerval *new_value,
                                   struct __kernel_old_itimerval *old_value);
extern int interval_timer_get_impl(int which, struct __kernel_old_itimerval *curr_value);
extern ssize_t getrandom_impl(void *buf, size_t buflen, unsigned int flags);
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

static long syscall_result(long ret);

static int syscall_copy_sigset_to_mask(const uint64_t *sigset, size_t sigsetsize,
                                       sigset_t *mask) {
    if (!mask || sigsetsize != sizeof(sigset_t)) {
        return -EINVAL;
    }
    sigemptyset(mask);
    if (sigset) {
        memcpy(mask, sigset, sizeof(*mask));
    }
    return 0;
}

static int syscall_copy_mask_to_sigset(const sigset_t *mask, uint64_t *sigset,
                                       size_t sigsetsize) {
    if (!mask || !sigset || sigsetsize != sizeof(sigset_t)) {
        return -EINVAL;
    }
    memcpy(sigset, mask, sizeof(*mask));
    return 0;
}

static long syscall_prlimit64(int32_t pid, int resource, const uint64_t *new_limit,
                              uint64_t *old_limit) {
    struct task *task;

    if (pid != 0 && pid != getpid_impl()) {
        return -ESRCH;
    }
    if (resource < 0 || resource >= 16) {
        return -EINVAL;
    }
    task = task_current();
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

static long syscall_clock_nanosleep(__kernel_clockid_t clk_id, int flags,
                                    const struct __kernel_timespec *req,
                                    struct __kernel_timespec *rem) {
    struct __kernel_timespec sleep_req;
    struct __kernel_timespec sleep_rem;

    if (!req) {
        return -EFAULT;
    }
    if ((flags & ~1) != 0 || req->tv_sec < 0 || req->tv_nsec < 0 || req->tv_nsec >= 1000000000LL) {
        return -EINVAL;
    }

    if ((flags & 1) != 0) {
        struct __kernel_timespec now;
        long ret = clock_gettime_impl(clk_id, &now);
        if (ret < 0) {
            return ret;
        }
        if (req->tv_sec < now.tv_sec ||
            (req->tv_sec == now.tv_sec && req->tv_nsec <= now.tv_nsec)) {
            return 0;
        }
        sleep_req.tv_sec = (__kernel_old_time_t)(req->tv_sec - now.tv_sec);
        sleep_req.tv_nsec = (long)(req->tv_nsec - now.tv_nsec);
        if (sleep_req.tv_nsec < 0) {
            sleep_req.tv_sec--;
            sleep_req.tv_nsec += 1000000000L;
        }
    } else {
        sleep_req.tv_sec = (__kernel_old_time_t)req->tv_sec;
        sleep_req.tv_nsec = (long)req->tv_nsec;
    }

    {
        long ret = nanosleep_impl(&sleep_req, rem ? &sleep_rem : NULL);
        if (ret < 0) {
            if (rem && (flags & 1) == 0) {
                rem->tv_sec = sleep_rem.tv_sec;
                rem->tv_nsec = sleep_rem.tv_nsec;
            }
            return ret;
        }
    }
    if (rem && (flags & 1) == 0) {
        rem->tv_sec = 0;
        rem->tv_nsec = 0;
    }
    return 0;
}

static long syscall_tgkill(int32_t tgid, int32_t tid, int32_t sig) {
    struct task *target;
    int result;

    if (tgid <= 0 || tid <= 0 || sig < 0 || sig > KERNEL_SIG_NUM) {
        return -EINVAL;
    }

    target = task_lookup(tid);
    if (!target) {
        return -ESRCH;
    }
    if (target->tgid != tgid) {
        task_put(target);
        return -ESRCH;
    }
    if (sig == 0) {
        task_put(target);
        return 0;
    }

    result = signal_generate_task(target, sig);
    task_put(target);
    return result < 0 ? (long)result : 0;
}

static long syscall_pidfd_send_signal(int pidfd, int32_t sig, const void *info,
                                      unsigned int flags) {
    fd_entry_t *entry;
    struct task *target;
    int result;

    if (flags != 0) {
        return -EINVAL;
    }
    if (info != NULL) {
        return -EINVAL;
    }

    entry = get_fd_entry_impl(pidfd);
    if (!entry) {
        return -EBADF;
    }
    if (!get_fd_is_pidfd_impl(entry)) {
        put_fd_entry_impl(entry);
        return -EBADF;
    }

    target = pidfd_get_task_entry_impl(entry);
    put_fd_entry_impl(entry);
    if (!target) {
        return -ESRCH;
    }

    result = signal_send_process(target, sig);
    task_put(target);
    return result < 0 ? (long)result : 0;
}

static long syscall_pidfd_getfd(int pidfd, int targetfd, unsigned int flags) {
    fd_entry_t *entry;
    struct task *target;
    int dupfd;

    if (flags != 0) {
        return -EINVAL;
    }

    entry = get_fd_entry_impl(pidfd);
    if (!entry) {
        return -EBADF;
    }
    if (!get_fd_is_pidfd_impl(entry)) {
        put_fd_entry_impl(entry);
        return -EBADF;
    }

    target = pidfd_get_task_entry_impl(entry);
    put_fd_entry_impl(entry);
    if (!target) {
        return -ESRCH;
    }
    {
        long err = task_pidfd_getfd_access_impl(target);
        if (err != 0) {
        task_put(target);
        return err < 0 ? err : -EPERM;
        }
    }

    dupfd = pidfd_getfd_impl(target, targetfd, flags);
    task_put(target);
    return syscall_result((long)dupfd);
}

static long syscall_clone(unsigned long flags, int *parent_tid, int *child_tid) {
    int32_t child_pid;
    struct task *child;

    if (((flags & CLONE_PARENT_SETTID) != 0 && !parent_tid) ||
        ((flags & CLONE_CHILD_SETTID) != 0 && !child_tid)) {
        return -EFAULT;
    }

    child_pid = clone_impl((uint64_t)flags);
    if (child_pid < 0) {
        return child_pid;
    }

    child = task_lookup(child_pid);
    if (!child) {
        return -ESRCH;
    }
    if ((flags & CLONE_PARENT_SETTID) != 0) {
        *parent_tid = child->pid;
    }
    if ((flags & CLONE_CHILD_SETTID) != 0) {
        *child_tid = child->pid;
    }
    if ((flags & CLONE_CHILD_CLEARTID) != 0) {
        child->clear_child_tid = (uint64_t)(uintptr_t)child_tid;
    }
    task_put(child);
    return child_pid;
}

static long syscall_result(long ret) {
    if (ret < 0) {
        if (ret >= -4095) {
            return ret;
        }
        return -EINVAL;
    }
    return ret;
}

static int ppoll_timeout_ms(const struct __kernel_timespec *timeout) {
    int64_t ms;

    if (!timeout) {
        return -1;
    }
    if (timeout->tv_sec < 0 || timeout->tv_nsec < 0 || timeout->tv_nsec >= 1000000000L) {
        return -EINVAL;
    }
    if (timeout->tv_sec > 9223372036854775LL) {
        return 2147483647;
    }
    ms = (int64_t)timeout->tv_sec * 1000;
    ms += (timeout->tv_nsec + 999999L) / 1000000L;
    if (ms > 2147483647LL) {
        return 2147483647;
    }
    return (int)ms;
}

struct syscall_sigmask_arg {
    const uint64_t *ss;
    size_t ss_len;
};

static int syscall_timespec_to_timeval(const struct __kernel_timespec *timeout, struct __kernel_old_timeval *timeval) {
    if (!timeout || !timeval) {
        return 0;
    }
    if (timeout->tv_sec < 0 || timeout->tv_nsec < 0 || timeout->tv_nsec >= 1000000000L) {
        return -EINVAL;
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
                                           sigset_t *old_mask,
                                           int *changed) {
    sigset_t new_mask;

    *changed = 0;
    if (!arg) {
        return 0;
    }
    if (!arg->ss) {
        return 0;
    }
    if (arg->ss_len != sizeof(uint64_t)) {
        return -EINVAL;
    }
    if (syscall_copy_sigset_to_mask(arg->ss, arg->ss_len, &new_mask) != 0) {
        return -EINVAL;
    }
    {
        int ret = do_sigsetmask(&new_mask, old_mask);
        if (ret != 0) {
            return ret;
        }
    }
    *changed = 1;
    return 0;
}

static void syscall_restore_sigmask(const sigset_t *old_mask, int changed) {
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
    case __NR_preadv:
    case __NR_pwritev:
    case __NR_preadv2:
    case __NR_pwritev2:
    case __NR_sendfile:
    case __NR_splice:
    case __NR_vmsplice:
    case __NR_tee:
    case __NR_lseek:
    case __NR_readv:
    case __NR_writev:
    case __NR_openat:
    case __NR_close:
    case __NR_dup:
    case __NR_dup3:
    case __NR_pipe2:
    case __NR_fcntl:
    case __NR_flock:
    case __NR_ioctl:
    case __NR_getdents64:
    case __NR_readlinkat:
    case __NR_newfstatat:
    case __NR_fstat:
    case __NR_faccessat:
    case __NR_faccessat2:
    case __NR_statx:
    case __NR_getcwd:
    case __NR_ftruncate:
    case __NR_mkdirat:
    case __NR_unlinkat:
    case __NR_linkat:
    case __NR_symlinkat:
    case __NR_renameat:
    case __NR_renameat2:
    case __NR_chdir:
    case __NR_fchdir:
    case __NR_chroot:
    case __NR_umask:
    case __NR_fchmod:
    case __NR_fchmodat:
    case __NR_fchmodat2:
    case __NR_fchown:
    case __NR_fchownat:
    case __NR_statfs:
    case __NR_fstatfs:
    case __NR_sync:
    case __NR_fsync:
    case __NR_fdatasync:
    case __NR_syncfs:
    case __NR_truncate:
    case __NR_fallocate:
    case __NR_sync_file_range:
    case __NR_close_range:
    case __NR_copy_file_range:
    case __NR_openat2:
    case __NR_utimensat:
    case __NR_memfd_create:
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
    case __NR_gettid:
    case __NR_clone:
    case __NR_execve:
    case __NR_execveat:
    case __NR_exit:
    case __NR_exit_group:
    case __NR_wait4:
    case __NR_waitid:
    case __NR_unshare:
    case __NR_clone3:
    case __NR_pidfd_open:
    case __NR_pidfd_getfd:
    case __NR_getpid:
    case __NR_getppid:
    case __NR_uname:
    case __NR_getuid:
    case __NR_geteuid:
    case __NR_getgid:
    case __NR_getegid:
    case __NR_setuid:
    case __NR_setgid:
    case __NR_setreuid:
    case __NR_setregid:
    case __NR_setresuid:
    case __NR_getresuid:
    case __NR_setresgid:
    case __NR_getresgid:
    case __NR_getgroups:
    case __NR_setgroups:
    case __NR_getpgid:
    case __NR_getsid:
    case __NR_setpgid:
    case __NR_setsid:
    case __NR_prctl:
        return SYSCALL_CAPABILITY_PROCESS;
    case __NR_rt_sigaction:
    case __NR_sigaltstack:
    case __NR_rt_sigreturn:
    case __NR_restart_syscall:
    case __NR_rt_sigprocmask:
    case __NR_kill:
    case __NR_tgkill:
    case __NR_pidfd_send_signal:
        return SYSCALL_CAPABILITY_SIGNAL;
    case __NR_socket:
    case __NR_socketpair:
    case __NR_connect:
    case __NR_bind:
    case __NR_listen:
    case __NR_accept:
    case __NR_accept4:
    case __NR_shutdown:
    case __NR_sendto:
    case __NR_recvfrom:
    case __NR_sendmsg:
    case __NR_recvmsg:
    case __NR_sendmmsg:
    case __NR_recvmmsg:
    case __NR_getsockname:
    case __NR_getpeername:
    case __NR_setsockopt:
    case __NR_getsockopt:
        return SYSCALL_CAPABILITY_NETWORK;
    case __NR_ppoll:
    case __NR_pselect6:
    case __NR_epoll_create1:
    case __NR_epoll_ctl:
    case __NR_epoll_pwait:
    case __NR_eventfd2:
    case __NR_timerfd_create:
    case __NR_timerfd_settime:
    case __NR_timerfd_gettime:
    case __NR_futex:
        return SYSCALL_CAPABILITY_READINESS;
    case __NR_mount_setattr:
    case __NR_open_tree:
    case __NR_move_mount:
    case __NR_pivot_root:
    case __NR_mount:
    case __NR_umount2:
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
    case __NR_nanosleep:
    case __NR_clock_nanosleep:
    case __NR_gettimeofday:
    case __NR_getitimer:
    case __NR_setitimer:
        return SYSCALL_CAPABILITY_TIME;
    case __NR_prlimit64:
    case __NR_set_robust_list:
    case __NR_get_robust_list:
    case __NR_times:
    case __NR_getrusage:
        return SYSCALL_CAPABILITY_RESOURCE;
    case __NR_getrandom:
        return SYSCALL_CAPABILITY_RANDOM;
    default:
        return SYSCALL_CAPABILITY_NONE;
    }
}

int syscall_is_implemented_impl(long number) {
    return syscall_capability_class_impl(number) != SYSCALL_CAPABILITY_NONE;
}

/*
 * Milestone-01 keeps a narrow matrix override for audited process-adjacent
 * syscalls whose repo-truth status is more specific than the coarse
 * implemented-vs-gap inventory. Keep this table scoped to audited gaps that
 * still remain outside the implemented Linux-facing surface.
 */
enum syscall_matrix_override_class syscall_matrix_override_class_impl(long number) {
    switch (number) {
    default:
        return SYSCALL_MATRIX_OVERRIDE_NONE;
    }
}

enum syscall_gap_priority syscall_gap_priority_impl(long number) {
    if (syscall_is_implemented_impl(number)) {
        return SYSCALL_GAP_NONE;
    }

    switch (number) {
    case __NR_recvmmsg:
    case __NR_sendmmsg:
        return SYSCALL_GAP_NETWORK;
    default:
        return SYSCALL_GAP_NONE;
    }
}

static long syscall_dispatch_inner_impl(long number,
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
    case __NR_readv:
        return syscall_result(readv_impl((int)arg0, (const struct iovec *)(uintptr_t)arg1, (int)arg2));
    case __NR_writev:
        return syscall_result(writev_impl((int)arg0, (const struct iovec *)(uintptr_t)arg1, (int)arg2));
    case __NR_pread64:
        return syscall_result((long)pread_impl((int)arg0, (void *)(uintptr_t)arg1, (size_t)arg2,
                                               (long long)arg3));
    case __NR_pwrite64:
        return syscall_result((long)pwrite_impl((int)arg0, (const void *)(uintptr_t)arg1, (size_t)arg2,
                                                (long long)arg3));
    case __NR_preadv:
        return syscall_result(preadv_impl((int)arg0, (const struct iovec *)(uintptr_t)arg1,
                                          (int)arg2, (unsigned long)arg3, (unsigned long)arg4));
    case __NR_pwritev:
        return syscall_result(pwritev_impl((int)arg0, (const struct iovec *)(uintptr_t)arg1,
                                           (int)arg2, (unsigned long)arg3, (unsigned long)arg4));
    case __NR_preadv2:
        return syscall_result(preadv2_impl((int)arg0, (const struct iovec *)(uintptr_t)arg1,
                                           (int)arg2, (unsigned long)arg3, (unsigned long)arg4, (int)arg5));
    case __NR_pwritev2:
        return syscall_result(pwritev2_impl((int)arg0, (const struct iovec *)(uintptr_t)arg1,
                                            (int)arg2, (unsigned long)arg3, (unsigned long)arg4, (int)arg5));
    case __NR_sendfile:
        return syscall_result((long)sendfile_impl((int)arg0, (int)arg1,
                                                  (int64_t *)(uintptr_t)arg2, (size_t)arg3));
    case __NR_splice:
        return syscall_result((long)splice_impl((int)arg0, (int64_t *)(uintptr_t)arg1,
                                                (int)arg2, (int64_t *)(uintptr_t)arg3,
                                                (size_t)arg4, (unsigned int)arg5));
    case __NR_vmsplice:
        return syscall_result((long)vmsplice_impl((int)arg0, (const struct iovec *)(uintptr_t)arg1,
                                                  (unsigned long)arg2, (unsigned int)arg3));
    case __NR_tee:
        return syscall_result((long)tee_impl((int)arg0, (int)arg1, (size_t)arg2, (unsigned int)arg3));
    case __NR_copy_file_range:
        return syscall_result((long)copy_file_range_impl((int)arg0, (int64_t *)(uintptr_t)arg1,
                                                         (int)arg2, (int64_t *)(uintptr_t)arg3,
                                                         (size_t)arg4, (unsigned int)arg5));
    case __NR_lseek:
        return syscall_result((long)lseek_impl((int)arg0, (int64_t)arg1, (int)arg2));
    case __NR_openat:
        return syscall_result((long)openat_impl((int)arg0, (const char *)(uintptr_t)arg1,
                                                (int)arg2, (uint32_t)arg3));
    case __NR_openat2: {
        const struct open_how *how = (const struct open_how *)(uintptr_t)arg2;

        if (!how) {
            return -EFAULT;
        }
        if ((size_t)arg3 < sizeof(*how)) {
            return -EINVAL;
        }
        if (how->resolve != 0) {
            return -EINVAL;
        }
        if ((how->flags & O_CREAT) == 0 && how->mode != 0) {
            return -EINVAL;
        }
        return syscall_result((long)openat_impl((int)arg0, (const char *)(uintptr_t)arg1,
                                                (int)how->flags, (uint32_t)how->mode));
    }
    case __NR_socket:
        return syscall_result(sys_socket((int)arg0, (int)arg1, (int)arg2));
    case __NR_socketpair:
        return syscall_result(sys_socketpair((int)arg0, (int)arg1, (int)arg2, (int *)(uintptr_t)arg3));
    case __NR_connect:
        return syscall_result(sys_connect((int)arg0, (void *)(uintptr_t)arg1, (int)arg2));
    case __NR_bind:
        return syscall_result(sys_bind((int)arg0, (void *)(uintptr_t)arg1, (int)arg2));
    case __NR_listen:
        return syscall_result(sys_listen((int)arg0, (int)arg1));
    case __NR_accept:
        return syscall_result(sys_accept((int)arg0, (void *)(uintptr_t)arg1,
                                         (int *)(uintptr_t)arg2));
    case __NR_accept4:
        return syscall_result(sys_accept4((int)arg0, (void *)(uintptr_t)arg1,
                                          (int *)(uintptr_t)arg2, (int)arg3));
    case __NR_shutdown:
        return syscall_result(sys_shutdown((int)arg0, (int)arg1));
    case __NR_sendto:
        return syscall_result(sys_sendto((int)arg0, (void *)(uintptr_t)arg1, (size_t)arg2, (unsigned int)arg3,
                                         (void *)(uintptr_t)arg4, (int)arg5));
    case __NR_recvfrom:
        return syscall_result(sys_recvfrom((int)arg0, (void *)(uintptr_t)arg1, (size_t)arg2, (unsigned int)arg3,
                                           (void *)(uintptr_t)arg4, (int *)(uintptr_t)arg5));
    case __NR_sendmsg:
        return syscall_result(sys_sendmsg((int)arg0, (void *)(uintptr_t)arg1, (unsigned int)arg2));
    case __NR_recvmsg:
        return syscall_result(sys_recvmsg((int)arg0, (void *)(uintptr_t)arg1, (unsigned int)arg2));
    case __NR_sendmmsg:
        return syscall_result(sys_sendmmsg((int)arg0, (void *)(uintptr_t)arg1,
                                           (unsigned int)arg2, (unsigned int)arg3));
    case __NR_recvmmsg:
        return syscall_result(sys_recvmmsg((int)arg0, (void *)(uintptr_t)arg1,
                                           (unsigned int)arg2, (unsigned int)arg3,
                                           (struct __kernel_timespec *)(uintptr_t)arg4));
    case __NR_getsockname:
        return syscall_result(sys_getsockname((int)arg0, (void *)(uintptr_t)arg1,
                                              (int *)(uintptr_t)arg2));
    case __NR_getpeername:
        return syscall_result(sys_getpeername((int)arg0, (void *)(uintptr_t)arg1,
                                              (int *)(uintptr_t)arg2));
    case __NR_setsockopt:
        return syscall_result(sys_setsockopt((int)arg0, (int)arg1, (int)arg2, (char *)(uintptr_t)arg3,
                                             (int)arg4));
    case __NR_getsockopt:
        return syscall_result(sys_getsockopt((int)arg0, (int)arg1, (int)arg2, (char *)(uintptr_t)arg3,
                                             (int *)(uintptr_t)arg4));
    case __NR_close:
        return syscall_result((long)close_impl((int)arg0));
    case __NR_close_range:
        return syscall_result((long)close_range_impl((unsigned int)arg0, (unsigned int)arg1,
                                                     (unsigned int)arg2));
    case __NR_dup:
        return syscall_result((long)dup_impl((int)arg0));
    case __NR_dup3:
        return syscall_result((long)dup3_impl((int)arg0, (int)arg1, (int)arg2));
    case __NR_pipe2:
        return syscall_result((long)pipe2_impl((int *)(uintptr_t)arg0, (int)arg1));
    case __NR_eventfd2:
        return syscall_result((long)eventfd2_impl((unsigned int)arg0, (int)arg1));
    case __NR_timerfd_create:
        return syscall_result((long)timerfd_create_impl((int)arg0, (int)arg1));
    case __NR_timerfd_settime:
        return syscall_result((long)timerfd_settime_impl((int)arg0, (int)arg1,
                                                         (const struct __kernel_itimerspec *)(uintptr_t)arg2,
                                                         (struct __kernel_itimerspec *)(uintptr_t)arg3));
    case __NR_timerfd_gettime:
        return syscall_result((long)timerfd_gettime_impl((int)arg0,
                                                         (struct __kernel_itimerspec *)(uintptr_t)arg1));
    case __NR_memfd_create:
        return syscall_result((long)memfd_create_impl((const char *)(uintptr_t)arg0,
                                                      (unsigned int)arg1));
    case __NR_pidfd_open:
        return syscall_result((long)pidfd_open_impl((int32_t)arg0, (unsigned int)arg1));
    case __NR_fcntl:
        return syscall_result((long)fcntl_impl((int)arg0, (int)arg1, (int)arg2));
    case __NR_flock:
        return syscall_result((long)flock_impl((int)arg0, (int)arg1));
    case __NR_brk:
        return syscall_result((long)(uintptr_t)brk_impl((void *)(uintptr_t)arg0));
    case __NR_set_tid_address: {
        struct task *task = task_current();
        if (!task) {
            return -ESRCH;
        }
        task->clear_child_tid = (uint64_t)(uintptr_t)arg0;
        return (long)task->pid;
    }
    case __NR_gettid: {
        struct task *task = task_current();
        return task ? (long)task->pid : -ESRCH;
    }
    case __NR_clone:
        return syscall_clone((unsigned long)arg0, (int *)(uintptr_t)arg2,
                             (int *)(uintptr_t)arg4);
    case __NR_futex: {
        int op = (int)arg1 & FUTEX_CMD_MASK;
        int timeout_ms = -1;
        if (!arg0) {
            return -EFAULT;
        }
        if ((op == FUTEX_WAIT || op == FUTEX_WAIT_BITSET) && arg3) {
            timeout_ms = ppoll_timeout_ms((const struct __kernel_timespec *)(uintptr_t)arg3);
            if (timeout_ms < 0) {
                return timeout_ms;
            }
        }
        if (op == FUTEX_WAIT) {
            return syscall_result((long)futex_wait_impl((int *)(uintptr_t)arg0, (int)arg2, timeout_ms));
        }
        if (op == FUTEX_WAKE) {
            return syscall_result((long)futex_wake_impl((int *)(uintptr_t)arg0, (int)arg2));
        }
        if (op == FUTEX_WAIT_BITSET || op == FUTEX_WAKE_BITSET || op == FUTEX_REQUEUE ||
            op == FUTEX_CMP_REQUEUE) {
            if (op == FUTEX_REQUEUE || op == FUTEX_CMP_REQUEUE) {
                timeout_ms = (int)arg3;
            }
            return syscall_result((long)futex_op_impl((int *)(uintptr_t)arg0, (int)arg1, (int)arg2,
                                                     timeout_ms, (int *)(uintptr_t)arg4, (int)arg5));
        }
        return -ENOSYS;
    }
    case __NR_set_robust_list:
        return syscall_result((long)set_robust_list_impl((void *)(uintptr_t)arg0, (unsigned long)arg1));
    case __NR_get_robust_list:
        return syscall_result((long)get_robust_list_impl((int)arg0, (void **)(uintptr_t)arg1,
                                                         (unsigned long *)(uintptr_t)arg2));
    case __NR_rt_sigaction: {
        struct sigaction act = {0};
        struct sigaction oldact = {0};
        const struct sigaction *act_ptr = NULL;
        struct sigaction *oldact_ptr = NULL;

        if (arg3 != sizeof(sigset_t)) {
            return -EINVAL;
        }
        if (arg1) {
            act = *(const struct sigaction *)(uintptr_t)arg1;
            act_ptr = &act;
        }
        if (arg2) {
            oldact_ptr = &oldact;
        }
        {
            int ret = do_sigaction((int32_t)arg0, act_ptr, oldact_ptr);
            if (ret != 0) {
                return ret;
            }
        }
        if (arg2) {
            *(struct sigaction *)(uintptr_t)arg2 = oldact;
        }
        return 0;
    }
    case __NR_sigaltstack: {
        stack_t new_stack = {0};
        stack_t old_stack = {0};
        const stack_t *new_stack_ptr = NULL;
        stack_t *old_stack_ptr = NULL;

        if (arg0) {
            new_stack = *(const stack_t *)(uintptr_t)arg0;
            new_stack_ptr = &new_stack;
        }
        if (arg1) {
            old_stack_ptr = &old_stack;
        }
        {
            int ret = do_sigaltstack(new_stack_ptr, old_stack_ptr);
            if (ret != 0) {
                return ret;
            }
        }
        if (arg1) {
            *(stack_t *)(uintptr_t)arg1 = old_stack;
        }
        return 0;
    }
    case __NR_rt_sigreturn: {
        struct task *task = task_current();
        return signal_finish_sigreturn_task(task);
    }
    case __NR_restart_syscall: {
        struct task *task = task_current();
        uint64_t restart_kind = 0;
        uint64_t restart_arg0 = 0;
        uint64_t restart_arg1 = 0;
        uint64_t restart_arg2 = 0;
        uint64_t restart_arg3 = 0;
        uint64_t restart_arg4 = 0;
        long ret;

        if (!task) {
            return -ESRCH;
        }
        if (signal_frame_restart_kind_get_task(task, &restart_kind) != 0 ||
            signal_frame_restart_arg_get_task(task, 0, &restart_arg0) != 0 ||
            signal_frame_restart_arg_get_task(task, 1, &restart_arg1) != 0 ||
            signal_frame_restart_arg_get_task(task, 2, &restart_arg2) != 0 ||
            signal_frame_restart_arg_get_task(task, 3, &restart_arg3) != 0 ||
            signal_frame_restart_arg_get_task(task, 4, &restart_arg4) != 0) {
            return -ESRCH;
        }
        if (restart_kind == TASK_RESTART_NONE) {
            return -EINTR;
        }
        signal_frame_restart_clear_task(task);

        switch (restart_kind) {
        case TASK_RESTART_NANOSLEEP:
            ret = nanosleep_impl((const struct __kernel_timespec *)(uintptr_t)restart_arg0,
                                 (struct __kernel_timespec *)(uintptr_t)restart_arg1);
            return syscall_result(ret);
        case TASK_RESTART_POLL:
            ret = poll_impl((struct pollfd *)(uintptr_t)restart_arg0,
                            (__kernel_ulong_t)restart_arg1,
                            (int)restart_arg2);
            return syscall_result(ret);
        case TASK_RESTART_PIPE_READ:
            ret = read_impl((int)restart_arg0, (void *)(uintptr_t)restart_arg1,
                            (size_t)restart_arg2);
            return syscall_result(ret);
        case TASK_RESTART_PIPE_WRITE:
            ret = write_impl((int)restart_arg0, (const void *)(uintptr_t)restart_arg1,
                             (size_t)restart_arg2);
            return syscall_result(ret);
        case TASK_RESTART_FUTEX_WAIT:
            ret = futex_wait_impl((int *)(uintptr_t)restart_arg0,
                                  (int)restart_arg1, (int)restart_arg2);
            return syscall_result(ret);
        case TASK_RESTART_WAITPID:
            ret = waitpid_impl((__kernel_pid_t)restart_arg0,
                               (int *)(uintptr_t)restart_arg1,
                               (int)restart_arg2);
            return syscall_result(ret);
        case TASK_RESTART_SELECT:
            ret = select_impl((int)restart_arg0,
                              (__kernel_fd_set *)(uintptr_t)restart_arg1,
                              (__kernel_fd_set *)(uintptr_t)restart_arg2,
                              (__kernel_fd_set *)(uintptr_t)restart_arg3,
                              (struct __kernel_old_timeval *)(uintptr_t)restart_arg4);
            return syscall_result(ret);
        case TASK_RESTART_EPOLL_WAIT:
            ret = epoll_wait_impl((int)restart_arg0,
                                  (struct epoll_event *)(uintptr_t)restart_arg1,
                                  (int)restart_arg2, (int)restart_arg3);
            return syscall_result(ret);
        default:
            return -EINTR;
        }
    }
    case __NR_rt_sigprocmask: {
        sigset_t set;
        sigset_t oldset;
        const sigset_t *set_ptr = NULL;
        sigset_t *oldset_ptr = NULL;

        if (arg3 != sizeof(sigset_t)) {
            return -EINVAL;
        }
        if (arg1) {
            int ret = syscall_copy_sigset_to_mask((const uint64_t *)(uintptr_t)arg1, (size_t)arg3, &set);
            if (ret != 0) {
                return ret;
            }
            set_ptr = &set;
        }
        if (arg2) {
            oldset_ptr = &oldset;
        }
        {
            int ret = do_sigprocmask((int)arg0, set_ptr, oldset_ptr);
            if (ret != 0) {
                return ret;
            }
        }
        if (arg2) {
            int ret = syscall_copy_mask_to_sigset(&oldset, (uint64_t *)(uintptr_t)arg2, (size_t)arg3);
            if (ret != 0) {
                return ret;
            }
        }
        return 0;
    }
    case __NR_kill:
        return syscall_result((long)do_kill((int32_t)arg0, (int32_t)arg1));
    case __NR_tgkill:
        return syscall_tgkill((int32_t)arg0, (int32_t)arg1, (int32_t)arg2);
    case __NR_pidfd_send_signal:
        return syscall_pidfd_send_signal((int)arg0, (int32_t)arg1,
                                         (const void *)(uintptr_t)arg2, (unsigned int)arg3);
    case __NR_pidfd_getfd:
        return syscall_pidfd_getfd((int)arg0, (int)arg1, (unsigned int)arg2);
    case __NR_ioctl:
        return syscall_result((long)ioctl_impl((int)arg0, (unsigned long)arg1, (void *)(uintptr_t)arg2));
    case __NR_getdents64:
        return syscall_result((long)getdents64_impl((int)arg0, (void *)(uintptr_t)arg1, (size_t)arg2));
    case __NR_ppoll: {
        int timeout_ms = ppoll_timeout_ms((const struct __kernel_timespec *)(uintptr_t)arg2);
        if (timeout_ms < 0 && timeout_ms != -1) {
            return timeout_ms;
        }
        return syscall_result((long)poll_impl((struct pollfd *)(uintptr_t)arg0, (__kernel_ulong_t)arg1, timeout_ms));
    }
    case __NR_pselect6: {
        struct __kernel_old_timeval timeout_value;
        struct __kernel_old_timeval *timeout_ptr = NULL;
        const struct syscall_sigmask_arg *sigmask_arg = (const struct syscall_sigmask_arg *)(uintptr_t)arg5;
        sigset_t old_mask;
        int mask_changed = 0;
        int ret;

        if (arg4) {
            int err = syscall_timespec_to_timeval((const struct __kernel_timespec *)(uintptr_t)arg4,
                                                  &timeout_value);
            if (err != 0) {
                return err;
            }
            timeout_ptr = &timeout_value;
        }
        {
            int err = syscall_apply_temporary_sigmask(sigmask_arg, &old_mask, &mask_changed);
            if (err != 0) {
                return err;
            }
        }
        ret = select_impl((int)arg0, (__kernel_fd_set *)(uintptr_t)arg1,
                          (__kernel_fd_set *)(uintptr_t)arg2,
                          (__kernel_fd_set *)(uintptr_t)arg3,
                          timeout_ptr);
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
        sigset_t old_mask;
        int mask_changed = 0;
        int ret;

        {
            int err = syscall_apply_temporary_sigmask(arg4 ? &sigmask_arg : NULL, &old_mask, &mask_changed);
            if (err != 0) {
                return err;
            }
        }
        ret = epoll_pwait_impl((int)arg0, (struct epoll_event *)(uintptr_t)arg1,
                               (int)arg2, (int)arg3);
        syscall_restore_sigmask(&old_mask, mask_changed);
        return syscall_result((long)ret);
    }
    case __NR_readlinkat:
        return syscall_result((long)readlinkat((int)arg0, (const char *)(uintptr_t)arg1,
                                               (char *)(uintptr_t)arg2, (size_t)arg3));
    case __NR_mkdirat:
        return syscall_result((long)mkdirat_impl((int)arg0, (const char *)(uintptr_t)arg1,
                                                 (uint32_t)arg2));
    case __NR_unlinkat:
        return syscall_result((long)unlinkat_impl((int)arg0, (const char *)(uintptr_t)arg1,
                                                  (int)arg2));
    case __NR_linkat:
        return syscall_result((long)linkat((int)arg0, (const char *)(uintptr_t)arg1,
                                           (int)arg2, (const char *)(uintptr_t)arg3,
                                           (int)arg4));
    case __NR_symlinkat:
        return syscall_result((long)symlinkat((const char *)(uintptr_t)arg0, (int)arg1,
                                              (const char *)(uintptr_t)arg2));
    case __NR_renameat:
        return syscall_result((long)renameat2_impl((int)arg0, (const char *)(uintptr_t)arg1,
                                                   (int)arg2, (const char *)(uintptr_t)arg3, 0));
    case __NR_renameat2:
        return syscall_result((long)renameat2_impl((int)arg0, (const char *)(uintptr_t)arg1,
                                                   (int)arg2, (const char *)(uintptr_t)arg3,
                                                   (unsigned int)arg4));
    case __NR_newfstatat:
        return syscall_result((long)fstatat_impl((int)arg0, (const char *)(uintptr_t)arg1,
                                                 (struct stat *)(uintptr_t)arg2, (int)arg3));
    case __NR_fstat:
        return syscall_result((long)fstat_impl((int)arg0, (struct stat *)(uintptr_t)arg1));
    case __NR_faccessat:
        return syscall_result((long)faccessat_impl((int)arg0, (const char *)(uintptr_t)arg1,
                                                   (int)arg2, (int)arg3));
    case __NR_faccessat2:
        return syscall_result((long)faccessat_impl((int)arg0, (const char *)(uintptr_t)arg1,
                                                   (int)arg2, (int)arg3));
    case __NR_statx:
        return syscall_result((long)statx_impl((int)arg0, (const char *)(uintptr_t)arg1,
                                               (int)arg2, (unsigned int)arg3,
                                               (struct statx *)(uintptr_t)arg4));
    case __NR_getcwd: {
        char *buf = (char *)(uintptr_t)arg0;
        size_t size = (size_t)arg1;
        struct task *task = task_current();
        char virtual_path[MAX_PATH];
        int ret;

        if (!buf) {
            return -EFAULT;
        }
        if (size == 0) {
            return -EINVAL;
        }
        if (!task || !task->fs) {
            return -ESRCH;
        }
        ret = vfs_getcwd_path_task(task->fs, virtual_path, sizeof(virtual_path));
        if (ret != 0) {
            return ret;
        }
        if (strlen(virtual_path) >= size) {
            return -ERANGE;
        }
        memcpy(buf, virtual_path, strlen(virtual_path) + 1);
        return (long)strlen(virtual_path) + 1;
    }
    case __NR_chdir:
        return syscall_result((long)chdir_impl((const char *)(uintptr_t)arg0));
    case __NR_fchdir:
        return syscall_result((long)fchdir_impl((int)arg0));
    case __NR_chroot:
        return syscall_result((long)chroot((const char *)(uintptr_t)arg0));
    case __NR_umask:
        return (long)umask_impl((uint32_t)arg0);
    case __NR_fchmod:
        return syscall_result((long)fchmod_impl((int)arg0, (uint32_t)arg1));
    case __NR_fchmodat:
    case __NR_fchmodat2:
        return syscall_result((long)fchmodat_impl((int)arg0, (const char *)(uintptr_t)arg1,
                                                  (uint32_t)arg2, (int)arg3));
    case __NR_fchown:
        return syscall_result((long)fchown_impl((int)arg0, (uint32_t)arg1,
                                                (uint32_t)arg2));
    case __NR_fchownat:
        return syscall_result((long)fchownat_impl((int)arg0, (const char *)(uintptr_t)arg1,
                                                  (uint32_t)arg2, (uint32_t)arg3,
                                                  (int)arg4));
    case __NR_utimensat:
        return syscall_result((long)utimensat_impl((int)arg0, (const char *)(uintptr_t)arg1,
                                                   (const struct __kernel_timespec *)(uintptr_t)arg2,
                                                   (int)arg3));
    case __NR_statfs:
        return syscall_result((long)statfs_impl((const char *)(uintptr_t)arg0,
                                                (struct statfs *)(uintptr_t)arg1));
    case __NR_fstatfs:
        return syscall_result((long)fstatfs_impl((int)arg0, (struct statfs *)(uintptr_t)arg1));
    case __NR_sync:
        sync_impl();
        return 0;
    case __NR_fsync:
        return syscall_result((long)fsync_impl((int)arg0));
    case __NR_fdatasync:
        return syscall_result((long)fdatasync_impl((int)arg0));
    case __NR_syncfs:
        return syscall_result((long)syncfs_impl((int)arg0));
    case __NR_getpid:
        return (long)getpid_impl();
    case __NR_getppid:
        return (long)getppid_impl();
    case __NR_uname:
        return syscall_result((long)uname_impl((struct new_utsname *)(uintptr_t)arg0));
    case __NR_getuid:
        return (long)getuid_impl();
    case __NR_geteuid:
        return (long)geteuid_impl();
    case __NR_getgid:
        return (long)getgid_impl();
    case __NR_getegid:
        return (long)getegid_impl();
    case __NR_setuid:
        return syscall_result((long)setuid_impl((__kernel_uid32_t)arg0));
    case __NR_setgid:
        return syscall_result((long)setgid_impl((__kernel_gid32_t)arg0));
    case __NR_setreuid:
        return syscall_result((long)setreuid_impl((__kernel_uid32_t)arg0, (__kernel_uid32_t)arg1));
    case __NR_setregid:
        return syscall_result((long)setregid_impl((__kernel_gid32_t)arg0, (__kernel_gid32_t)arg1));
    case __NR_setresuid:
        return syscall_result((long)setresuid_impl((__kernel_uid32_t)arg0,
                                                   (__kernel_uid32_t)arg1,
                                                   (__kernel_uid32_t)arg2));
    case __NR_getresuid:
        return syscall_result((long)getresuid_impl((__kernel_uid32_t *)(uintptr_t)arg0,
                                                   (__kernel_uid32_t *)(uintptr_t)arg1,
                                                   (__kernel_uid32_t *)(uintptr_t)arg2));
    case __NR_setresgid:
        return syscall_result((long)setresgid_impl((__kernel_gid32_t)arg0,
                                                   (__kernel_gid32_t)arg1,
                                                   (__kernel_gid32_t)arg2));
    case __NR_getresgid:
        return syscall_result((long)getresgid_impl((__kernel_gid32_t *)(uintptr_t)arg0,
                                                   (__kernel_gid32_t *)(uintptr_t)arg1,
                                                   (__kernel_gid32_t *)(uintptr_t)arg2));
    case __NR_getgroups:
        return syscall_result((long)getgroups_impl((int)arg0, (__kernel_gid32_t *)(uintptr_t)arg1));
    case __NR_setgroups:
        return syscall_result((long)setgroups_impl((int)arg0, (const __kernel_gid32_t *)(uintptr_t)arg1));
    case __NR_getpgid:
        return syscall_result((long)getpgid_impl((int32_t)arg0));
    case __NR_getsid:
        return syscall_result((long)getsid_impl((int32_t)arg0));
    case __NR_setpgid:
        return syscall_result((long)setpgid_impl((int32_t)arg0, (int32_t)arg1));
    case __NR_setsid:
        return syscall_result((long)setsid_impl());
    case __NR_prctl:
        return syscall_result((long)prctl_impl((int)arg0, (unsigned long)arg1,
                                               (unsigned long)arg2, (unsigned long)arg3,
                                               (unsigned long)arg4));
    case __NR_exit:
    case __NR_exit_group:
        exit_impl((int)arg0);
        return 0;
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
    case __NR_mount:
        return syscall_result((long)mount_impl((const char *)(uintptr_t)arg0,
                                              (const char *)(uintptr_t)arg1,
                                              (const char *)(uintptr_t)arg2,
                                              (unsigned long)arg3,
                                              (const void *)(uintptr_t)arg4));
    case __NR_umount2:
        return syscall_result((long)umount2_impl((const char *)(uintptr_t)arg0, (int)arg1));
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
    case __NR_truncate:
        return syscall_result((long)truncate_impl((const char *)(uintptr_t)arg0, (int64_t)arg1));
    case __NR_ftruncate:
        return syscall_result((long)ftruncate_impl((int)arg0, (int64_t)arg1));
    case __NR_fallocate:
        return syscall_result((long)fallocate_impl((int)arg0, (int)arg1, (int64_t)arg2, (int64_t)arg3));
    case __NR_sync_file_range:
        return syscall_result((long)sync_file_range_impl((int)arg0, (int64_t)arg1,
                                                         (int64_t)arg2, (unsigned int)arg3));
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
    case __NR_times:
        return syscall_result(times_impl((struct tms *)(uintptr_t)arg0));
    case __NR_getrusage:
        return syscall_result((long)getrusage_impl((int)arg0,
                                                   (struct rusage *)(uintptr_t)arg1));
    case __NR_ptrace:
        return syscall_result(ptrace_impl(arg0, (__kernel_pid_t)arg1,
                                          (void *)(uintptr_t)arg2,
                                          (void *)(uintptr_t)arg3));
    case __NR_clock_gettime: {
        struct __kernel_timespec backing_ts;
        struct __kernel_timespec *linux_ts = (struct __kernel_timespec *)(uintptr_t)arg1;
        long ret;

        if (!linux_ts) {
            return -EFAULT;
        }
        ret = syscall_result((long)clock_gettime_impl((__kernel_clockid_t)arg0, &backing_ts));
        if (ret < 0) {
            return ret;
        }
        linux_ts->tv_sec = backing_ts.tv_sec;
        linux_ts->tv_nsec = backing_ts.tv_nsec;
        return 0;
    }
    case __NR_nanosleep: {
        const struct __kernel_timespec *linux_req = (const struct __kernel_timespec *)(uintptr_t)arg0;
        struct __kernel_timespec *linux_rem = (struct __kernel_timespec *)(uintptr_t)arg1;
        struct __kernel_timespec req;
        struct __kernel_timespec rem;
        long ret;

        if (!linux_req) {
            return -EFAULT;
        }
        if (linux_req->tv_sec < 0 || linux_req->tv_nsec < 0 || linux_req->tv_nsec >= 1000000000LL) {
            return -EINVAL;
        }
        req.tv_sec = (__kernel_old_time_t)linux_req->tv_sec;
        req.tv_nsec = (long)linux_req->tv_nsec;
        ret = nanosleep_impl(&req, linux_rem ? &rem : NULL);
        if (ret < 0) {
            if (linux_rem) {
                linux_rem->tv_sec = rem.tv_sec;
                linux_rem->tv_nsec = rem.tv_nsec;
            }
            return ret;
        }
        if (linux_rem) {
            linux_rem->tv_sec = 0;
            linux_rem->tv_nsec = 0;
        }
        return 0;
    }
    case __NR_clock_nanosleep:
        return syscall_clock_nanosleep((__kernel_clockid_t)arg0, (int)arg1,
                                       (const struct __kernel_timespec *)(uintptr_t)arg2,
                                       (struct __kernel_timespec *)(uintptr_t)arg3);
    case __NR_gettimeofday: {
        struct __kernel_old_timeval *linux_tv = (struct __kernel_old_timeval *)(uintptr_t)arg0;
        struct __kernel_old_timeval backing_tv;
        long ret;

        if (!linux_tv) {
            return -EFAULT;
        }
        ret = syscall_result((long)gettimeofday_impl(&backing_tv, (void *)(uintptr_t)arg1));
        if (ret < 0) {
            return ret;
        }
        linux_tv->tv_sec = (__kernel_old_time_t)backing_tv.tv_sec;
        linux_tv->tv_usec = (__kernel_suseconds_t)backing_tv.tv_usec;
        return 0;
    }
    case __NR_getitimer: {
        struct __kernel_old_itimerval *linux_value =
            (struct __kernel_old_itimerval *)(uintptr_t)arg1;
        struct __kernel_old_itimerval backing_value;
        long ret;

        if (!linux_value) {
            return -EFAULT;
        }
        ret = syscall_result((long)interval_timer_get_impl((int)arg0, &backing_value));
        if (ret < 0) {
            return ret;
        }
        linux_value->it_interval.tv_sec = (__kernel_old_time_t)backing_value.it_interval.tv_sec;
        linux_value->it_interval.tv_usec = (__kernel_suseconds_t)backing_value.it_interval.tv_usec;
        linux_value->it_value.tv_sec = (__kernel_old_time_t)backing_value.it_value.tv_sec;
        linux_value->it_value.tv_usec = (__kernel_suseconds_t)backing_value.it_value.tv_usec;
        return 0;
    }
    case __NR_setitimer: {
        const struct __kernel_old_itimerval *linux_new =
            (const struct __kernel_old_itimerval *)(uintptr_t)arg1;
        struct __kernel_old_itimerval *linux_old =
            (struct __kernel_old_itimerval *)(uintptr_t)arg2;
        struct __kernel_old_itimerval backing_new;
        struct __kernel_old_itimerval backing_old;
        const struct __kernel_old_itimerval *backing_new_ptr = NULL;
        long ret;

        if (linux_new) {
            backing_new.it_interval.tv_sec = (__kernel_old_time_t)linux_new->it_interval.tv_sec;
            backing_new.it_interval.tv_usec = (__kernel_suseconds_t)linux_new->it_interval.tv_usec;
            backing_new.it_value.tv_sec = (__kernel_old_time_t)linux_new->it_value.tv_sec;
            backing_new.it_value.tv_usec = (__kernel_suseconds_t)linux_new->it_value.tv_usec;
            backing_new_ptr = &backing_new;
        }
        ret = syscall_result((long)interval_timer_set_impl((int)arg0, backing_new_ptr,
                                                           linux_old ? &backing_old : NULL));
        if (ret < 0) {
            return ret;
        }
        if (linux_old) {
            linux_old->it_interval.tv_sec = (__kernel_old_time_t)backing_old.it_interval.tv_sec;
            linux_old->it_interval.tv_usec = (__kernel_suseconds_t)backing_old.it_interval.tv_usec;
            linux_old->it_value.tv_sec = (__kernel_old_time_t)backing_old.it_value.tv_sec;
            linux_old->it_value.tv_usec = (__kernel_suseconds_t)backing_old.it_value.tv_usec;
        }
        return 0;
    }
    case __NR_getrandom:
        return syscall_result((long)getrandom_impl((void *)(uintptr_t)arg0, (size_t)arg1,
                                                   (unsigned int)arg2));
    case __NR_execve:
        return syscall_result((long)execve_impl((const char *)(uintptr_t)arg0,
                                                (char *const *)(uintptr_t)arg1,
                                                (char *const *)(uintptr_t)arg2));
    case __NR_execveat:
        return syscall_result((long)execveat_impl((int)arg0, (const char *)(uintptr_t)arg1,
                                                  (char *const *)(uintptr_t)arg2,
                                                  (char *const *)(uintptr_t)arg3, (int)arg4));
    case __NR_wait4:
        return syscall_result((long)wait4_impl((__kernel_pid_t)arg0, (int *)(uintptr_t)arg1,
                                               (int)arg2, (void *)(uintptr_t)arg3));
    case __NR_waitid:
        return syscall_result((long)waitid_impl((int)arg0, (__kernel_pid_t)arg1,
                                                (void *)(uintptr_t)arg2, (int)arg3,
                                                (void *)(uintptr_t)arg4));
    case __NR_unshare:
        return syscall_result((long)unshare_impl((uint64_t)arg0));
    case __NR_clone3:
        return syscall_result((long)clone3_impl((const struct clone_args *)(uintptr_t)arg0,
                                                (size_t)arg1));
    default:
        return -ENOSYS;
    }
}

long syscall_dispatch_impl(long number,
                           long arg0,
                           long arg1,
                           long arg2,
                           long arg3,
                           long arg4,
                           long arg5) {
    long ret;

    if (ptrace_note_syscall_entry(number, arg0, arg1, arg2, arg3, arg4, arg5) != 0) {
        return -EINTR;
    }
    ret = syscall_dispatch_inner_impl(number, arg0, arg1, arg2, arg3, arg4, arg5);
    ptrace_note_syscall_exit(ret);
    return ret;
}
