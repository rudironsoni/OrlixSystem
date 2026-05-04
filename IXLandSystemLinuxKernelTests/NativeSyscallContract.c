#include "NativeSyscallContract.h"

#include <asm/unistd.h>
#include <asm/ioctls.h>
#include <asm/statfs.h>
#include <linux/close_range.h>
#include <linux/fcntl.h>
#include <linux/futex.h>
#include <linux/capability.h>
#include <linux/elf.h>
#include <linux/memfd.h>
#include <linux/mman.h>
#include <linux/pidfd.h>
#include <linux/poll.h>
#include <linux/prctl.h>
#include <linux/random.h>
#include <linux/sched.h>
#include <linux/stat.h>
#include <linux/statfs.h>
#include <linux/times.h>
#include <linux/time_types.h>
#include <linux/utsname.h>
#include <linux/xattr.h>
#include <asm-generic/siginfo.h>

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef SIGBUS
#undef SIGBUS
#endif
#ifdef SIGSEGV
#undef SIGSEGV
#endif
#ifdef SIGIOT
#undef SIGIOT
#endif
#ifdef SIGUSR1
#undef SIGUSR1
#endif
#ifdef SIGUSR2
#undef SIGUSR2
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
#ifdef SIGURG
#undef SIGURG
#endif
#ifdef SIGIO
#undef SIGIO
#endif
#ifdef SIGSYS
#undef SIGSYS
#endif
#define __ASSEMBLY__ 1
#include <asm-generic/signal.h>
#undef __ASSEMBLY__

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
#ifdef RLIMIT_NOFILE
#undef RLIMIT_NOFILE
#endif
#ifdef RLIM_NLIMITS
#undef RLIM_NLIMITS
#endif
#include <asm-generic/resource.h>
#include <asm-generic/signal-defs.h>

#include "fs/fdtable.h"
#include "fs/vfs.h"
#include "kernel/signal.h"
#include "kernel/task.h"
#ifdef WEXITED
#undef WEXITED
#endif
#ifdef WSTOPPED
#undef WSTOPPED
#endif
#ifdef WCONTINUED
#undef WCONTINUED
#endif
#ifdef WNOWAIT
#undef WNOWAIT
#endif
#include <linux/wait.h>

#ifndef F_OK
#define F_OK 0
#endif

struct linux_rusage_contract {
    struct __kernel_old_timeval ru_utime;
    struct __kernel_old_timeval ru_stime;
    __kernel_long_t ru_maxrss;
    __kernel_long_t ru_ixrss;
    __kernel_long_t ru_idrss;
    __kernel_long_t ru_isrss;
    __kernel_long_t ru_minflt;
    __kernel_long_t ru_majflt;
    __kernel_long_t ru_nswap;
    __kernel_long_t ru_inblock;
    __kernel_long_t ru_oublock;
    __kernel_long_t ru_msgsnd;
    __kernel_long_t ru_msgrcv;
    __kernel_long_t ru_nsignals;
    __kernel_long_t ru_nvcsw;
    __kernel_long_t ru_nivcsw;
};

extern int link_impl(const char *oldpath, const char *newpath);
extern int unlink_impl(const char *pathname);
extern int rmdir_impl(const char *pathname);
extern int open_impl(const char *pathname, int flags, linux_mode_t mode);
extern long read_impl(int fd, void *buf, size_t count);
extern long pread_impl(int fd, void *buf, size_t count, linux_off_t offset);
extern long readlink_impl(const char *pathname, char *buf, size_t bufsiz);
extern int symlinkat(const char *target, int newdirfd, const char *linkpath);
extern int renameat2(int olddirfd, const char *oldpath, int newdirfd, const char *newpath,
                     unsigned int flags);
#include "runtime/native/registry.h"
#include "runtime/syscall.h"

extern int execve(const char *pathname, char *const argv[], char *const envp[]);

struct native_syscall_dirent64 {
    unsigned long long d_ino;
    long long d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[];
};

static int init_entry_seen;

static int latest_signal_info_matches(struct task_struct *task, int signo, int code, uint64_t addr);
static int read_file_into_buffer(const char *path, char *buf, size_t buf_len);

static int close_if_open(int fd) {
    if (fd >= 0 && fdtable_is_used_impl(fd)) {
        long ret = syscall_dispatch_impl(__NR_close, fd, 0, 0, 0, 0, 0);
        if (ret < 0) {
            errno = (int)-ret;
            return -1;
        }
    }
    return 0;
}

static void clear_pending_signal(struct task_struct *task, int32_t sig) {
    if (!task || !task->signal || sig < 1 || sig > KERNEL_SIG_NUM) {
        return;
    }
    task->thread_pending_signals &= ~(1ULL << ((sig - 1) & 63));
    task->signal->shared_pending.sig[(sig - 1) >> 6] &= ~(1ULL << ((sig - 1) & 63));
}

static int expect_raw_errno(long ret, int expected) {
    if (ret != -(long)expected) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

static int format_outfd_env(char *buf, size_t buf_len, int fd) {
    char digits[16];
    size_t pos = 0;
    int value = fd;
    int digit_count = 0;
    const char prefix[] = "OUTFD=";

    if (!buf || buf_len < sizeof(prefix) || fd < 0) {
        errno = EINVAL;
        return -1;
    }

    memcpy(buf, prefix, sizeof(prefix) - 1);
    pos = sizeof(prefix) - 1;

    do {
        digits[digit_count++] = (char)('0' + (value % 10));
        value /= 10;
    } while (value > 0 && digit_count < (int)sizeof(digits));

    if (pos + (size_t)digit_count + 1 > buf_len) {
        errno = EOVERFLOW;
        return -1;
    }

    for (int i = digit_count - 1; i >= 0; i--) {
        buf[pos++] = digits[i];
    }
    buf[pos] = '\0';
    return 0;
}

static int format_maps_range(char *buf, size_t buf_len, uint64_t start, uint64_t end) {
    static const char hex[] = "0123456789abcdef";
    size_t pos = 0;

    if (!buf || buf_len < 27) {
        errno = EINVAL;
        return -1;
    }
    for (int shift = 44; shift >= 0; shift -= 4) {
        buf[pos++] = hex[(start >> shift) & 0xfU];
    }
    buf[pos++] = '-';
    for (int shift = 44; shift >= 0; shift -= 4) {
        buf[pos++] = hex[(end >> shift) & 0xfU];
    }
    buf[pos] = '\0';
    return 0;
}

static int proc_path_for_pid(char *buf, size_t buf_len, int32_t pid, const char *suffix);

static int format_proc_fd_path(char *buf, size_t buf_len, int fd) {
    const char prefix[] = "/proc/self/fd/";
    char digits[16];
    size_t pos = 0;
    int value = fd;
    int digit_count = 0;

    if (!buf || buf_len == 0 || fd < 0) {
        errno = EINVAL;
        return -1;
    }
    while (prefix[pos] != '\0') {
        if (pos + 1 >= buf_len) {
            errno = ENAMETOOLONG;
            return -1;
        }
        buf[pos] = prefix[pos];
        pos++;
    }
    do {
        digits[digit_count++] = (char)('0' + (value % 10));
        value /= 10;
    } while (value > 0 && digit_count < (int)sizeof(digits));
    if (value > 0 || pos + (size_t)digit_count + 1 > buf_len) {
        errno = ENAMETOOLONG;
        return -1;
    }
    while (digit_count > 0) {
        buf[pos++] = digits[--digit_count];
    }
    buf[pos] = '\0';
    return 0;
}

static int smaps_block_contains(const char *content, const char *range, const char *needle) {
    const char *block;
    const char *next;

    if (!content || !range || !needle) {
        errno = EINVAL;
        return 0;
    }
    block = strstr(content, range);
    if (!block) {
        return 0;
    }
    next = strstr(block + 1, "\n000");
    if (!next) {
        next = block + strlen(block);
    }
    return needle[0] == '\0' ||
           (strstr(block, needle) && strstr(block, needle) < next);
}

static int smaps_block_vmflags_contains(const char *content, const char *range, const char *needle) {
    const char *block;
    const char *next;
    const char *line;
    const char *line_end;
    const char *found;

    if (!content || !range || !needle) {
        errno = EINVAL;
        return 0;
    }
    block = strstr(content, range);
    if (!block) {
        return 0;
    }
    next = strstr(block + 1, "\n000");
    if (!next) {
        next = block + strlen(block);
    }
    line = strstr(block, "VmFlags:");
    if (!line || line >= next) {
        return 0;
    }
    line_end = strchr(line, '\n');
    if (!line_end || line_end > next) {
        line_end = next;
    }
    found = strstr(line, needle);
    return found && found < line_end;
}

static int smaps_block_vmflags_lacks(const char *content, const char *range, const char *needle) {
    const char *block;
    const char *next;
    const char *line;
    const char *line_end;
    const char *found;

    if (!content || !range || !needle) {
        errno = EINVAL;
        return 0;
    }
    block = strstr(content, range);
    if (!block) {
        return 0;
    }
    next = strstr(block + 1, "\n000");
    if (!next) {
        next = block + strlen(block);
    }
    line = strstr(block, "VmFlags:");
    if (!line || line >= next) {
        return 0;
    }
    line_end = strchr(line, '\n');
    if (!line_end || line_end > next) {
        line_end = next;
    }
    found = strstr(line, needle);
    return !found || found >= line_end;
}

static int contains_host_path_fragment(const char *buf) {
    return strstr(buf, "/private/") != NULL ||
           strstr(buf, "/var/mobile/") != NULL ||
           strstr(buf, "/Users/") != NULL ||
           strstr(buf, "/Volumes/") != NULL;
}

static int native_syscall_entry(int argc, char **argv, char **envp) {
    int outfd = -1;
    const char payload[] = "native-syscall-ok";

    if (argc != 2 || !argv || !argv[0] || !argv[1] ||
        strcmp(argv[0], "native-syscall") != 0 ||
        strcmp(argv[1], "arg") != 0 ||
        !envp) {
        errno = EPROTO;
        return 71;
    }

    for (int i = 0; envp[i]; i++) {
        if (strncmp(envp[i], "OUTFD=", 6) == 0) {
            outfd = (int)strtol(envp[i] + 6, NULL, 10);
        }
    }
    if (outfd < 0) {
        errno = ENOENT;
        return 72;
    }

    if (syscall_dispatch_impl(__NR_write, outfd, (long)(uintptr_t)payload,
                              sizeof(payload) - 1, 0, 0, 0) != (long)sizeof(payload) - 1) {
        return 73;
    }

    return 42;
}

static int native_init_entry(int argc, char **argv, char **envp) {
    char cwd[64];
    long ret;

    (void)envp;
    if (argc != 1 || !argv || !argv[0] || strcmp(argv[0], "/sbin/init") != 0) {
        errno = EPROTO;
        return 81;
    }

    ret = syscall_dispatch_impl(__NR_getpid, 0, 0, 0, 0, 0, 0);
    if (ret != 1) {
        errno = EPROTO;
        return 82;
    }

    memset(cwd, 0, sizeof(cwd));
    ret = syscall_dispatch_impl(__NR_getcwd, (long)(uintptr_t)cwd, sizeof(cwd), 0, 0, 0, 0);
    if (ret != 2 || strcmp(cwd, "/") != 0) {
        errno = EPROTO;
        return 83;
    }

    init_entry_seen = 1;
    return 0;
}

int native_syscall_contract_dispatches_fd_pipe_and_procfs(void) {
    int pipefd[2] = {-1, -1};
    const char payload[] = "pipe-data";
    char buf[64];
    char link_target[128];
    struct linux_stat st;
    struct pollfd pfd;
    long ret;
    int result = -1;

    ret = syscall_dispatch_impl(__NR_pipe2, (long)(uintptr_t)pipefd, O_CLOEXEC | O_NONBLOCK,
                                0, 0, 0, 0);
    if (ret != 0 || pipefd[0] < 0 || pipefd[1] < 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        return -1;
    }

    ret = syscall_dispatch_impl(__NR_fcntl, pipefd[0], F_GETFD, 0, 0, 0, 0);
    if (ret != FD_CLOEXEC) {
        errno = EPROTO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_fcntl, pipefd[0], F_GETFL, 0, 0, 0, 0);
    if (ret < 0 || (ret & O_NONBLOCK) == 0) {
        errno = EPROTO;
        goto out;
    }

    ret = syscall_dispatch_impl(__NR_write, pipefd[1], (long)(uintptr_t)payload,
                                sizeof(payload) - 1, 0, 0, 0);
    if (ret != (long)sizeof(payload) - 1) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = pipefd[0];
    pfd.events = POLLIN;
    ret = syscall_dispatch_impl(__NR_ppoll, (long)(uintptr_t)&pfd, 1, 0, 0, 0, 0);
    if (ret != 1 || (pfd.revents & POLLIN) == 0) {
        errno = EPROTO;
        goto out;
    }

    memset(buf, 0, sizeof(buf));
    ret = syscall_dispatch_impl(__NR_read, pipefd[0], (long)(uintptr_t)buf,
                                sizeof(payload) - 1, 0, 0, 0);
    if (ret != (long)sizeof(payload) - 1 || memcmp(buf, payload, sizeof(payload) - 1) != 0) {
        errno = EPROTO;
        goto out;
    }

    memset(&st, 0, sizeof(st));
    ret = syscall_dispatch_impl(__NR_fstat, pipefd[0], (long)(uintptr_t)&st, 0, 0, 0, 0);
    if (ret != 0 || (st.st_mode & S_IFMT) != S_IFIFO) {
        errno = EPROTO;
        goto out;
    }

    ret = syscall_dispatch_impl(__NR_readlinkat, AT_FDCWD, (long)(uintptr_t)"/proc/self/fd/0",
                                (long)(uintptr_t)link_target, sizeof(link_target), 0, 0);
    if (ret <= 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    result = 0;

out:
    close_if_open(pipefd[0]);
    close_if_open(pipefd[1]);
    return result;
}

int native_syscall_contract_dispatches_vm_identity_time_and_dirs(void) {
    struct task_struct *task = get_current();
    char cwd[64];
    unsigned char dirbuf[512];
    struct __kernel_timespec ts;
    void *mapped;
    int procfd = -1;
    int nullfd = -1;
    long ret;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    ret = syscall_dispatch_impl(__NR_getpid, 0, 0, 0, 0, 0, 0);
    if (ret != task->pid) {
        errno = EPROTO;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_getppid, 0, 0, 0, 0, 0, 0);
    if (ret != task->ppid) {
        errno = EPROTO;
        return -1;
    }

    memset(cwd, 0, sizeof(cwd));
    ret = syscall_dispatch_impl(__NR_getcwd, (long)(uintptr_t)cwd, sizeof(cwd), 0, 0, 0, 0);
    if (ret != 2 || strcmp(cwd, "/") != 0) {
        errno = EPROTO;
        return -1;
    }

    ret = syscall_dispatch_impl(__NR_mmap, 0, 4096, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ret < 0) {
        errno = (int)-ret;
        return -1;
    }
    mapped = (void *)(uintptr_t)ret;
    if (task_write_virtual_memory_impl(task, (uint64_t)(uintptr_t)mapped, "A", 1) != 1) {
        errno = EPROTO;
        goto out_mmap;
    }
    ret = syscall_dispatch_impl(__NR_mprotect, (long)(uintptr_t)mapped, 4096, PROT_READ, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out_mmap;
    }
    if (task_write_virtual_memory_impl(task, (uint64_t)(uintptr_t)mapped, "B", 1) >= 0 || errno != EACCES) {
        errno = EPROTO;
        goto out_mmap;
    }

    memset(&ts, 0, sizeof(ts));
    ret = syscall_dispatch_impl(__NR_clock_gettime, 0, (long)(uintptr_t)&ts, 0, 0, 0, 0);
    if (ret != 0 || ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000L) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out_mmap;
    }

    procfd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)"/proc/self/fd",
                                        O_RDONLY | O_DIRECTORY, 0, 0, 0);
    if (procfd < 0) {
        errno = -procfd;
        goto out_mmap;
    }
    memset(dirbuf, 0, sizeof(dirbuf));
    ret = syscall_dispatch_impl(__NR_getdents64, procfd, (long)(uintptr_t)dirbuf, sizeof(dirbuf), 0, 0, 0);
    if (ret <= 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out_mmap;
    }
    if (((struct native_syscall_dirent64 *)dirbuf)->d_reclen == 0) {
        errno = EPROTO;
        goto out_mmap;
    }

    nullfd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)"/dev/null",
                                        O_RDONLY, 0, 0, 0);
    if (nullfd < 0) {
        errno = -nullfd;
        goto out_mmap;
    }
    ret = syscall_dispatch_impl(__NR_ioctl, nullfd, TIOCGWINSZ, (long)(uintptr_t)&ts, 0, 0, 0);
    if (ret != -ENOTTY) {
        errno = EPROTO;
        goto out_mmap;
    }

    result = 0;

out_mmap:
    close_if_open(procfd);
    close_if_open(nullfd);
    ret = syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 4096, 0, 0, 0, 0);
    if (result == 0 && ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        result = -1;
    }
    return result;
}

int native_syscall_contract_enforces_vma_fault_policy(void) {
    struct task_struct *task = get_current();
    void *mapped;
    void *first;
    void *second;
    uint64_t base;
    char byte = 0;
    char pages[8192];
    long ret;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    ret = syscall_dispatch_impl(__NR_mmap, 0, 12288, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ret < 0) {
        errno = (int)-ret;
        return -1;
    }
    mapped = (void *)(uintptr_t)ret;
    base = (uint64_t)(uintptr_t)mapped;
    memset(pages, 'A', sizeof(pages));
    if (task_write_virtual_memory_impl(task, base, pages, 8192) != 8192 ||
        task_write_virtual_memory_impl(task, base + 8192, "Z", 1) != 1) {
        errno = EPROTO;
        goto out_mapped;
    }
    ret = syscall_dispatch_impl(__NR_mprotect, (long)(uintptr_t)(base + 4096), 4096,
                                PROT_NONE, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out_mapped;
    }
    if (task_read_virtual_memory_impl(task, base + 4096, &byte, 1) >= 0 || errno != EACCES) {
        errno = EACCES;
        goto out_mapped;
    }
    ret = syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)(base + 4096), 4096, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out_mapped;
    }
    if (task_read_virtual_memory_impl(task, base, &byte, 1) != 1 ||
        task_read_virtual_memory_impl(task, base + 8192, &byte, 1) != 1 ||
        task_read_virtual_memory_impl(task, base + 4096, &byte, 1) >= 0 || errno != EFAULT) {
        errno = ENOMSG;
        goto out_mapped;
    }
    if (task_write_virtual_memory_impl(task, base, pages, 4097) != 4096) {
        errno = ENODATA;
        goto out_mapped;
    }

    ret = syscall_dispatch_impl(__NR_mmap, 0, 4096, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ret < 0) {
        errno = (int)-ret;
        goto out_mapped;
    }
    first = (void *)(uintptr_t)ret;
    ret = syscall_dispatch_impl(__NR_mmap, (long)((uintptr_t)first + 4096), 4096,
                                PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                                -1, 0);
    if (ret < 0) {
        errno = (int)-ret;
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)first, 4096, 0, 0, 0, 0);
        goto out_mapped;
    }
    second = (void *)(uintptr_t)ret;
    if ((uintptr_t)second != (uintptr_t)first + 4096) {
        errno = ERANGE;
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)first, 4096, 0, 0, 0, 0);
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)second, 4096, 0, 0, 0, 0);
        goto out_mapped;
    }
    ret = syscall_dispatch_impl(__NR_mprotect, (long)(uintptr_t)first, 8192, PROT_READ, 0, 0, 0);
    if (ret != 0 ||
        task_write_virtual_memory_impl(task, (uint64_t)(uintptr_t)second, "Q", 1) >= 0 || errno != EACCES) {
        errno = EOWNERDEAD;
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)first, 4096, 0, 0, 0, 0);
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)second, 4096, 0, 0, 0, 0);
        goto out_mapped;
    }
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)first, 4096, 0, 0, 0, 0);
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)second, 4096, 0, 0, 0, 0);
    result = 0;

out_mapped:
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 4096, 0, 0, 0, 0);
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)(base + 8192), 4096, 0, 0, 0, 0);
    return result;
}

int native_syscall_contract_munmap_gap_and_map_fixed_replace_policy(void) {
    struct task_struct *task = get_current();
    void *mapped;
    uint64_t base;
    char byte = 0;
    long ret;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_mmap, 0, 12288, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ret < 0) {
        errno = (int)-ret;
        return -1;
    }
    mapped = (void *)(uintptr_t)ret;
    base = (uint64_t)(uintptr_t)mapped;
    if (task_write_virtual_memory_impl(task, base, "A", 1) != 1 ||
        task_write_virtual_memory_impl(task, base + 4096, "B", 1) != 1 ||
        task_write_virtual_memory_impl(task, base + 8192, "C", 1) != 1) {
        errno = EPROTO;
        goto out;
    }

    ret = syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)(base + 4096), 4096, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)(base + 4096), 4096, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_mprotect, (long)(uintptr_t)(base + 4096), 4096,
                                PROT_READ, 0, 0, 0);
    if (ret != -ENOMEM) {
        errno = EPROTO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_mmap, (long)(uintptr_t)base, 4096, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (ret != -EEXIST) {
        errno = EBUSY;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_mmap, (long)(uintptr_t)base, 4096, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (ret != (long)base) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    if (task_read_virtual_memory_impl(task, base, &byte, 1) != 1 || byte != 0 ||
        task_read_virtual_memory_impl(task, base + 8192, &byte, 1) != 1 || byte != 'C') {
        errno = ENODATA;
        goto out;
    }
    result = 0;

out:
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)base, 4096, 0, 0, 0, 0);
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)(base + 8192), 4096, 0, 0, 0, 0);
    return result;
}

int native_syscall_contract_map_fixed_noreplace_reuses_unmapped_gap_only(void) {
    struct task_struct *task = get_current();
    void *mapped = NULL;
    uint64_t base;
    char byte = 0;
    long ret;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_mmap, 0, 12288, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ret < 0) {
        errno = (int)-ret;
        return -1;
    }
    mapped = (void *)(uintptr_t)ret;
    base = (uint64_t)(uintptr_t)mapped;
    if (task_write_virtual_memory_impl(task, base, "A", 1) != 1 ||
        task_write_virtual_memory_impl(task, base + 4096, "B", 1) != 1 ||
        task_write_virtual_memory_impl(task, base + 8192, "C", 1) != 1) {
        errno = EPROTO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)(base + 4096), 4096,
                                0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_mmap, (long)(uintptr_t)(base + 4096), 4096,
                                PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                                -1, 0);
    if (ret != (long)(base + 4096)) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    if (task_read_virtual_memory_impl(task, base, &byte, 1) != 1 || byte != 'A' ||
        task_read_virtual_memory_impl(task, base + 4096, &byte, 1) != 1 || byte != 0 ||
        task_read_virtual_memory_impl(task, base + 8192, &byte, 1) != 1 || byte != 'C') {
        errno = ENODATA;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_mmap, (long)(uintptr_t)(base + 4096), 4096,
                                PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                                -1, 0);
    if (ret != -EEXIST) {
        errno = EBUSY;
        goto out;
    }

    result = 0;

out:
    if (mapped) {
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)base, 12288, 0, 0, 0, 0);
    }
    return result;
}

int native_syscall_contract_mremap_grows_and_moves_mapping(void) {
    struct task_struct *task = get_current();
    void *mapped;
    void *fixed;
    void *moved;
    uint64_t base;
    char byte = 0;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 4096, PROT_READ | PROT_WRITE,
                                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        return -1;
    }
    base = (uint64_t)(uintptr_t)mapped;
    task = get_current();
    if (task_write_virtual_memory_impl(task, base, "R", 1) != 1) {
        goto out_original;
    }
    fixed = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 8192, PROT_READ | PROT_WRITE,
                                                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)(uintptr_t)fixed < 0) {
        errno = -(int)(long)(uintptr_t)fixed;
        goto out_original;
    }
    moved = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mremap, (long)(uintptr_t)mapped, 4096, 8192,
                                                     MREMAP_MAYMOVE | MREMAP_FIXED,
                                                     (long)(uintptr_t)fixed, 0);
    if (moved != fixed) {
        errno = (long)(uintptr_t)moved < 0 ? -(int)(long)(uintptr_t)moved : EDESTADDRREQ;
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)fixed, 4096, 0, 0, 0, 0);
        return -1;
    }
    if (task_read_virtual_memory_impl(task, (uint64_t)(uintptr_t)fixed, &byte, 1) != 1 || byte != 'R') {
        errno = ENODATA;
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)fixed, 4096, 0, 0, 0, 0);
        return -1;
    }
    if (task_read_virtual_memory_impl(task, (uint64_t)(uintptr_t)fixed + 4096, &byte, 1) != 1 ||
        byte != 0) {
        errno = EALREADY;
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)fixed, 8192, 0, 0, 0, 0);
        return -1;
    }
    if (task_read_virtual_memory_impl(task, base, &byte, 1) >= 0 || errno != EFAULT) {
        errno = ENOMSG;
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)fixed, 8192, 0, 0, 0, 0);
        return -1;
    }
    result = 0;
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)fixed, 8192, 0, 0, 0, 0);
    return result;

out_original:
    {
        int saved_errno = errno;
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 8192, 0, 0, 0, 0);
        errno = saved_errno;
    }
    return result;
}

int native_syscall_contract_madvise_dontneed_discards_private_page(void) {
    struct task_struct *task = get_current();
    void *mapped;
    uint64_t base;
    char byte = 0;
    long ret;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 8192, PROT_READ | PROT_WRITE,
                                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        return -1;
    }
    base = (uint64_t)(uintptr_t)mapped;
    if (task_write_virtual_memory_impl(task, base, "A", 1) != 1 ||
        task_write_virtual_memory_impl(task, base + 4096, "B", 1) != 1) {
        errno = EPROTO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_madvise, (long)(uintptr_t)(base + 4096), 4096,
                                MADV_DONTNEED, 0, 0, 0);
    if (ret != 0 ||
        task_read_virtual_memory_impl(task, base, &byte, 1) != 1 || byte != 'A' ||
        task_read_virtual_memory_impl(task, base + 4096, &byte, 1) != 1 || byte != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_madvise, (long)(uintptr_t)(base + 1), 4096,
                                MADV_DONTNEED, 0, 0, 0);
    if (ret != -EINVAL) {
        errno = EBUSY;
        goto out;
    }
    result = 0;

out:
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 8192, 0, 0, 0, 0);
    return result;
}

int native_syscall_contract_mincore_uses_file_offset_for_truncate_residency(void) {
    struct task_struct *task = get_current();
    const char path[] = "/tmp/native-mincore-offset-truncate";
    char pages[8192];
    char byte = 0;
    unsigned char vec[1] = {0xff};
    void *mapped = (void *)-1;
    uint64_t base;
    int fd = -1;
    long ret;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    memset(pages, 'O', sizeof(pages));
    unlink_impl(path);
    fd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)path,
                                    O_CREAT | O_RDWR | O_TRUNC, 0600, 0, 0);
    if (fd < 0) {
        errno = -fd;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_write, fd, (long)(uintptr_t)pages, sizeof(pages), 0, 0, 0);
    if (ret != (long)sizeof(pages)) {
        errno = ret < 0 ? (int)-ret : EIO;
        goto out;
    }
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 4096,
                                                      PROT_READ, MAP_SHARED, fd, 4096);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        goto out;
    }
    base = (uint64_t)(uintptr_t)mapped;
    if (task_read_virtual_memory_impl(task, base, &byte, 1) != 1 || byte != 'O') {
        errno = EPROTO;
        goto out_mapped;
    }
    vec[0] = 0xff;
    if (syscall_dispatch_impl(__NR_mincore, (long)(uintptr_t)mapped, 4096,
                              (long)(uintptr_t)vec, 0, 0, 0) != 0 ||
        (vec[0] & 1) == 0) {
        errno = ENODATA;
        goto out_mapped;
    }
    ret = syscall_dispatch_impl(__NR_ftruncate, fd, 4096, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out_mapped;
    }
    vec[0] = 0xff;
    if (syscall_dispatch_impl(__NR_mincore, (long)(uintptr_t)mapped, 4096,
                              (long)(uintptr_t)vec, 0, 0, 0) != 0 ||
        (vec[0] & 1) != 0) {
        errno = ENOMSG;
        goto out_mapped;
    }
    if (task_read_virtual_memory_impl(task, base, &byte, 1) != -1) {
        errno = EPROTO;
        goto out_mapped;
    }
    if (errno != EFAULT) {
        errno = ERANGE;
        goto out_mapped;
    }
    if (task->last_fault_signal != SIGBUS) {
        errno = ENOTRECOVERABLE;
        goto out_mapped;
    }
    if (task->last_fault_code != BUS_ADRERR) {
        errno = EOWNERDEAD;
        goto out_mapped;
    }
    if (task->last_fault_addr != base) {
        errno = ESTALE;
        goto out_mapped;
    }

    result = 0;

out_mapped:
    {
        int saved_errno = errno;
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 4096, 0, 0, 0, 0);
        errno = saved_errno;
    }
out:
    {
        int saved_errno = errno;
        close_if_open(fd);
        unlink_impl(path);
        errno = saved_errno;
    }
    return result;
}

int native_syscall_contract_maps_shared_file_and_syncs(void) {
    struct task_struct *task = get_current();
    const char path[] = "/tmp/native-shared-map";
    const char initial[] = "file-backed-page";
    const char patched[] = "MAP";
    char verify[sizeof(initial)];
    void *mapped;
    int fd = -1;
    long ret;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    fd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)path,
                                    O_CREAT | O_RDWR | O_TRUNC, 0600, 0, 0);
    if (fd < 0) {
        errno = -fd;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_write, fd, (long)(uintptr_t)initial, sizeof(initial), 0, 0, 0);
    if (ret != (long)sizeof(initial)) {
        errno = ret < 0 ? (int)-ret : EIO;
        goto out;
    }
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 4096, PROT_READ | PROT_WRITE,
                                                      MAP_SHARED, fd, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        goto out;
    }
    memset(verify, 0, sizeof(verify));
    if (task_read_virtual_memory_impl(task, (uint64_t)(uintptr_t)mapped, verify, sizeof(initial)) !=
            (long)sizeof(initial) ||
        memcmp(verify, initial, sizeof(initial)) != 0) {
        errno = ENODATA;
        goto out_mapped;
    }
    if (task_write_virtual_memory_impl(task, (uint64_t)(uintptr_t)mapped, patched, sizeof(patched) - 1) !=
        (long)sizeof(patched) - 1) {
        errno = EPROTO;
        goto out_mapped;
    }
    ret = syscall_dispatch_impl(__NR_msync, (long)(uintptr_t)mapped, 4096, MS_SYNC, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out_mapped;
    }
    memset(verify, 0, sizeof(verify));
    ret = syscall_dispatch_impl(__NR_pread64, fd, (long)(uintptr_t)verify, sizeof(patched) - 1,
                                0, 0, 0);
    if (ret != (long)sizeof(patched) - 1 || memcmp(verify, patched, sizeof(patched) - 1) != 0) {
        errno = EIO;
        goto out_mapped;
    }
    result = 0;

out_mapped:
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 4096, 0, 0, 0, 0);
out:
    close_if_open(fd);
    return result;
}

int native_syscall_contract_mremap_extends_shared_mapping_writeback(void) {
    struct task_struct *task = get_current();
    const char path[] = "/tmp/native-shared-map-mremap-grow";
    char page[8192];
    char verify = 0;
    void *mapped;
    long ret;
    int fd = -1;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    memset(page, 'G', sizeof(page));
    unlink_impl(path);
    fd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)path,
                                    O_CREAT | O_RDWR | O_TRUNC, 0600, 0, 0);
    if (fd < 0) {
        errno = -fd;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_write, fd, (long)(uintptr_t)page, sizeof(page), 0, 0, 0);
    if (ret != (long)sizeof(page)) {
        errno = ret < 0 ? (int)-ret : EIO;
        goto out;
    }
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 4096, PROT_READ | PROT_WRITE,
                                                      MAP_SHARED, fd, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_mremap, (long)(uintptr_t)mapped, 4096, 8192, 0, 0, 0);
    if (ret != (long)(uintptr_t)mapped) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out_mapped;
    }
    if (task_write_virtual_memory_impl(task, (uint64_t)(uintptr_t)mapped + 4096, "Y", 1) != 1 ||
        syscall_dispatch_impl(__NR_msync, (long)(uintptr_t)mapped, 8192, MS_SYNC, 0, 0, 0) != 0) {
        errno = EIO;
        goto out_mapped;
    }
    ret = syscall_dispatch_impl(__NR_pread64, fd, (long)(uintptr_t)&verify, 1, 4096, 0, 0);
    if (ret != 1 || verify != 'Y') {
        errno = ret < 0 ? (int)-ret : ENODATA;
        goto out_mapped;
    }
    result = 0;

out_mapped:
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 8192, 0, 0, 0, 0);
out:
    close_if_open(fd);
    unlink_impl(path);
    return result;
}

int native_syscall_contract_msync_preserves_clean_shared_pages(void) {
    struct task_struct *task = get_current();
    const char path[] = "/tmp/native-shared-map-clean-pages";
    char first_page[4096];
    char second_page[4096];
    char verify[16];
    const char host_patch[] = "HOST";
    const char vm_patch[] = "DIRTY";
    void *mapped;
    int fd = -1;
    long ret;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    memset(first_page, 'A', sizeof(first_page));
    memset(second_page, 'B', sizeof(second_page));

    fd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)path,
                                    O_CREAT | O_RDWR | O_TRUNC, 0600, 0, 0);
    if (fd < 0) {
        errno = -fd;
        return -1;
    }
    if (syscall_dispatch_impl(__NR_write, fd, (long)(uintptr_t)first_page, sizeof(first_page), 0, 0, 0) !=
            (long)sizeof(first_page) ||
        syscall_dispatch_impl(__NR_write, fd, (long)(uintptr_t)second_page, sizeof(second_page), 0, 0, 0) !=
            (long)sizeof(second_page)) {
        errno = EIO;
        goto out;
    }

    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, sizeof(first_page) + sizeof(second_page),
                                                      PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        goto out;
    }

    ret = syscall_dispatch_impl(__NR_pwrite64, fd, (long)(uintptr_t)host_patch, sizeof(host_patch) - 1,
                                0, 0, 0);
    if (ret != (long)sizeof(host_patch) - 1) {
        errno = ret < 0 ? (int)-ret : EIO;
        goto out_mapped;
    }
    if (task_write_virtual_memory_impl(task, (uint64_t)(uintptr_t)mapped + sizeof(first_page),
                                       vm_patch, sizeof(vm_patch) - 1) != (long)sizeof(vm_patch) - 1) {
        errno = EPROTO;
        goto out_mapped;
    }
    ret = syscall_dispatch_impl(__NR_msync, (long)(uintptr_t)mapped,
                                sizeof(first_page) + sizeof(second_page), MS_SYNC, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out_mapped;
    }

    memset(verify, 0, sizeof(verify));
    ret = syscall_dispatch_impl(__NR_pread64, fd, (long)(uintptr_t)verify, sizeof(host_patch) - 1,
                                0, 0, 0);
    if (ret != (long)sizeof(host_patch) - 1 || memcmp(verify, host_patch, sizeof(host_patch) - 1) != 0) {
        errno = EBUSY;
        goto out_mapped;
    }
    memset(verify, 0, sizeof(verify));
    ret = syscall_dispatch_impl(__NR_pread64, fd, (long)(uintptr_t)verify, sizeof(vm_patch) - 1,
                                sizeof(first_page), 0, 0);
    if (ret != (long)sizeof(vm_patch) - 1 || memcmp(verify, vm_patch, sizeof(vm_patch) - 1) != 0) {
        errno = ENODATA;
        goto out_mapped;
    }
    result = 0;

out_mapped:
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped,
                          sizeof(first_page) + sizeof(second_page), 0, 0, 0, 0);
out:
    close_if_open(fd);
    return result;
}

int native_syscall_contract_private_file_mapping_msync_does_not_write_back_cow(void) {
    struct task_struct *task = get_current();
    const char path[] = "/tmp/native-private-file-msync-cow";
    char page[4096];
    char verify[8];
    const char cow_patch[] = "COW";
    void *mapped;
    int fd = -1;
    long ret;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    memset(page, 'A', sizeof(page));
    fd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)path,
                                    O_CREAT | O_RDWR | O_TRUNC, 0600, 0, 0);
    if (fd < 0) {
        errno = -fd;
        return -1;
    }
    if (syscall_dispatch_impl(__NR_write, fd, (long)(uintptr_t)page, sizeof(page), 0, 0, 0) !=
        (long)sizeof(page)) {
        errno = EIO;
        goto out;
    }
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, sizeof(page),
                                                      PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        goto out;
    }
    if (task_write_virtual_memory_impl(task, (uint64_t)(uintptr_t)mapped, cow_patch,
                                       sizeof(cow_patch) - 1) != (long)sizeof(cow_patch) - 1) {
        errno = EPROTO;
        goto out_mapped;
    }
    ret = syscall_dispatch_impl(__NR_msync, (long)(uintptr_t)mapped, sizeof(page),
                                MS_SYNC, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out_mapped;
    }
    memset(verify, 0, sizeof(verify));
    ret = syscall_dispatch_impl(__NR_pread64, fd, (long)(uintptr_t)verify,
                                sizeof(cow_patch) - 1, 0, 0, 0);
    if (ret != (long)sizeof(cow_patch) - 1 || memcmp(verify, "AAA", sizeof(cow_patch) - 1) != 0) {
        errno = ENODATA;
        goto out_mapped;
    }
    memset(verify, 0, sizeof(verify));
    if (task_read_virtual_memory_impl(task, (uint64_t)(uintptr_t)mapped, verify,
                                      sizeof(cow_patch) - 1) != (long)sizeof(cow_patch) - 1 ||
        memcmp(verify, cow_patch, sizeof(cow_patch) - 1) != 0) {
        errno = EBUSY;
        goto out_mapped;
    }
    result = 0;

out_mapped:
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, sizeof(page), 0, 0, 0, 0);
out:
    close_if_open(fd);
    unlink_impl(path);
    return result;
}

int native_syscall_contract_unlinked_shared_mapping_syncs_through_open_fd(void) {
    struct task_struct *task = get_current();
    const char path[] = "/tmp/native-unlinked-shared-map";
    char page[4096];
    char verify = 0;
    void *mapped;
    int fd = -1;
    long ret;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    memset(page, 'U', sizeof(page));
    unlink_impl(path);
    fd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)path,
                                    O_CREAT | O_RDWR | O_TRUNC, 0600, 0, 0);
    if (fd < 0) {
        errno = -fd;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_write, fd, (long)(uintptr_t)page, sizeof(page), 0, 0, 0);
    if (ret != (long)sizeof(page)) {
        errno = ret < 0 ? (int)-ret : EIO;
        goto out;
    }
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, sizeof(page),
                                                      PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        goto out;
    }
    if (unlink_impl(path) != 0 ||
        task_write_virtual_memory_impl(task, (uint64_t)(uintptr_t)mapped, "Z", 1) != 1 ||
        syscall_dispatch_impl(__NR_msync, (long)(uintptr_t)mapped, sizeof(page), MS_SYNC, 0, 0, 0) != 0) {
        errno = errno ? errno : EIO;
        goto out_mapped;
    }
    ret = syscall_dispatch_impl(__NR_pread64, fd, (long)(uintptr_t)&verify, 1, 0, 0, 0);
    if (ret != 1 || verify != 'Z') {
        errno = ret < 0 ? (int)-ret : ENODATA;
        goto out_mapped;
    }
    result = 0;

out_mapped:
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, sizeof(page), 0, 0, 0, 0);
out:
    close_if_open(fd);
    unlink_impl(path);
    return result;
}

int native_syscall_contract_shared_mapping_survives_fd_close_and_syncs(void) {
    struct task_struct *task = get_current();
    const char path[] = "/tmp/native-shared-map-close-sync";
    char page[4096];
    char verify = 0;
    void *mapped;
    int fd = -1;
    int reopened = -1;
    long ret;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    memset(page, 'C', sizeof(page));
    unlink_impl(path);
    fd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)path,
                                    O_CREAT | O_RDWR | O_TRUNC, 0600, 0, 0);
    if (fd < 0) {
        errno = -fd;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_write, fd, (long)(uintptr_t)page, sizeof(page), 0, 0, 0);
    if (ret != (long)sizeof(page)) {
        errno = ret < 0 ? (int)-ret : EIO;
        goto out;
    }
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, sizeof(page),
                                                      PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        goto out;
    }
    close_if_open(fd);
    fd = -1;
    if (task_write_virtual_memory_impl(task, (uint64_t)(uintptr_t)mapped, "Q", 1) != 1) {
        errno = EIO;
        goto out_mapped;
    }
    ret = syscall_dispatch_impl(__NR_msync, (long)(uintptr_t)mapped, sizeof(page), MS_SYNC, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out_mapped;
    }
    reopened = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)path,
                                          O_RDONLY, 0, 0, 0);
    if (reopened < 0) {
        errno = -reopened;
        goto out_mapped;
    }
    ret = syscall_dispatch_impl(__NR_pread64, reopened, (long)(uintptr_t)&verify, 1, 0, 0, 0);
    if (ret != 1 || verify != 'Q') {
        errno = ret < 0 ? (int)-ret : ENODATA;
        goto out_mapped;
    }
    result = 0;

out_mapped:
    close_if_open(reopened);
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, sizeof(page), 0, 0, 0, 0);
out:
    close_if_open(fd);
    unlink_impl(path);
    return result;
}

int native_syscall_contract_unlinked_shared_mapping_survives_fd_close_and_syncs(void) {
    struct task_struct *task = get_current();
    const char path[] = "/tmp/native-unlinked-shared-map-close-sync";
    char page[4096];
    char byte = 0;
    void *mapped;
    int fd = -1;
    long ret;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    memset(page, 'U', sizeof(page));
    unlink_impl(path);
    fd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)path,
                                    O_CREAT | O_RDWR | O_TRUNC, 0600, 0, 0);
    if (fd < 0) {
        errno = -fd;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_write, fd, (long)(uintptr_t)page, sizeof(page), 0, 0, 0);
    if (ret != (long)sizeof(page)) {
        errno = ret < 0 ? (int)-ret : EIO;
        goto out;
    }
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, sizeof(page),
                                                      PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        goto out;
    }
    if (unlink_impl(path) != 0) {
        goto out_mapped;
    }
    close_if_open(fd);
    fd = -1;
    if (task_write_virtual_memory_impl(task, (uint64_t)(uintptr_t)mapped, "R", 1) != 1) {
        errno = EIO;
        goto out_mapped;
    }
    ret = syscall_dispatch_impl(__NR_msync, (long)(uintptr_t)mapped, sizeof(page), MS_SYNC, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out_mapped;
    }
    if (task_read_virtual_memory_impl(task, (uint64_t)(uintptr_t)mapped, &byte, 1) != 1 ||
        byte != 'R') {
        errno = ENODATA;
        goto out_mapped;
    }
    result = 0;

out_mapped:
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, sizeof(page), 0, 0, 0, 0);
out:
    close_if_open(fd);
    unlink_impl(path);
    return result;
}

int native_syscall_contract_security_capability_xattr_round_trips(void) {
    const char path[] = "/tmp/native-security-capability-xattr";
    const char name[] = "security.capability";
    struct vfs_ns_cap_data cap = {0};
    struct vfs_ns_cap_data readback;
    int fd;
    long ret;
    int result = -1;

    unlink_impl(path);
    fd = open_impl(path, O_CREAT | O_RDWR | O_TRUNC, 0700);
    if (fd < 0) {
        return -1;
    }
    close_if_open(fd);

    cap.magic_etc = VFS_CAP_REVISION_3 | VFS_CAP_FLAGS_EFFECTIVE;
    cap.data[0].permitted = 1U << CAP_NET_BIND_SERVICE;
    cap.rootid = 0;

    ret = syscall_dispatch_impl(__NR_setxattr, (long)(uintptr_t)path, (long)(uintptr_t)name,
                                (long)(uintptr_t)&cap, sizeof(cap), XATTR_CREATE, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_setxattr, (long)(uintptr_t)path, (long)(uintptr_t)name,
                                (long)(uintptr_t)&cap, sizeof(cap), XATTR_CREATE, 0);
    if (ret != -EEXIST) {
        errno = ENODATA;
        goto out;
    }
    memset(&readback, 0, sizeof(readback));
    ret = syscall_dispatch_impl(__NR_getxattr, (long)(uintptr_t)path, (long)(uintptr_t)name,
                                (long)(uintptr_t)&readback, sizeof(readback), 0, 0);
    if (ret != (long)sizeof(readback) ||
        readback.magic_etc != cap.magic_etc ||
        readback.data[0].permitted != cap.data[0].permitted) {
        errno = ENOMSG;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_removexattr, (long)(uintptr_t)path, (long)(uintptr_t)name,
                                0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_getxattr, (long)(uintptr_t)path, (long)(uintptr_t)name,
                                (long)(uintptr_t)&readback, sizeof(readback), 0, 0);
    if (ret != -ENODATA) {
        errno = ERANGE;
        goto out;
    }

    result = 0;

out:
    unlink_impl(path);
    return result;
}

static int native_syscall_xattr_list_contains(const char *list, long list_len, const char *name) {
    long pos = 0;
    size_t name_len;

    if (!list || list_len < 0 || !name) {
        return 0;
    }
    name_len = strlen(name);
    while (pos < list_len) {
        size_t entry_len = strlen(list + pos);
        if (entry_len == name_len && memcmp(list + pos, name, name_len) == 0) {
            return 1;
        }
        pos += (long)entry_len + 1;
    }
    return 0;
}

int native_syscall_contract_user_xattr_list_remove_round_trips(void) {
    const char path[] = "/tmp/native-user-xattr-list";
    const char name[] = "user.note";
    const char value[] = "xattr-user-value";
    char readback[32];
    char list[128];
    int fd;
    long ret;
    int result = -1;

    unlink_impl(path);
    fd = open_impl(path, O_CREAT | O_RDWR | O_TRUNC, 0600);
    if (fd < 0) {
        return -1;
    }
    close_if_open(fd);

    ret = syscall_dispatch_impl(__NR_setxattr, (long)(uintptr_t)path, (long)(uintptr_t)name,
                                (long)(uintptr_t)value, sizeof(value), XATTR_CREATE, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_getxattr, (long)(uintptr_t)path, (long)(uintptr_t)name,
                                (long)(uintptr_t)readback, sizeof(readback), 0, 0);
    if (ret != (long)sizeof(value) || memcmp(readback, value, sizeof(value)) != 0) {
        errno = ret < 0 ? (int)-ret : ENODATA;
        goto out;
    }
    memset(list, 0, sizeof(list));
    ret = syscall_dispatch_impl(__NR_listxattr, (long)(uintptr_t)path, (long)(uintptr_t)list,
                                sizeof(list), 0, 0, 0);
    if (ret <= 0 || !native_syscall_xattr_list_contains(list, ret, name)) {
        errno = ret < 0 ? (int)-ret : ENOMSG;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_removexattr, (long)(uintptr_t)path, (long)(uintptr_t)name,
                                0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    memset(list, 0, sizeof(list));
    ret = syscall_dispatch_impl(__NR_listxattr, (long)(uintptr_t)path, (long)(uintptr_t)list,
                                sizeof(list), 0, 0, 0);
    if (ret < 0 || native_syscall_xattr_list_contains(list, ret, name)) {
        errno = ret < 0 ? (int)-ret : EBUSY;
        goto out;
    }
    result = 0;

out:
    unlink_impl(path);
    return result;
}

int native_syscall_contract_flistxattr_reports_fd_user_attribute(void) {
    const char path[] = "/tmp/native-fd-user-xattr-list";
    const char name[] = "user.fdnote";
    const char value[] = "fd-xattr";
    char readback[32];
    char list[128];
    int fd = -1;
    long ret;
    int result = -1;

    unlink_impl(path);
    fd = open_impl(path, O_CREAT | O_RDWR | O_TRUNC, 0600);
    if (fd < 0) {
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_fsetxattr, fd, (long)(uintptr_t)name,
                                (long)(uintptr_t)value, sizeof(value), XATTR_CREATE, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    memset(list, 0, sizeof(list));
    ret = syscall_dispatch_impl(__NR_flistxattr, fd, (long)(uintptr_t)list, sizeof(list), 0, 0, 0);
    if (ret <= 0 || !native_syscall_xattr_list_contains(list, ret, name)) {
        errno = ret < 0 ? (int)-ret : ENOMSG;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_fgetxattr, fd, (long)(uintptr_t)name,
                                (long)(uintptr_t)readback, sizeof(readback), 0, 0);
    if (ret != (long)sizeof(value) || memcmp(readback, value, sizeof(value)) != 0) {
        errno = ret < 0 ? (int)-ret : ENODATA;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_fremovexattr, fd, (long)(uintptr_t)name, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    result = 0;

out:
    close_if_open(fd);
    unlink_impl(path);
    return result;
}

int native_syscall_contract_lxattr_targets_symlink_inode(void) {
    const char target[] = "/tmp/native-lxattr-target";
    const char link[] = "/tmp/native-lxattr-link";
    const char name[] = "user.symlink-note";
    const char target_name[] = "user.target-note";
    const char link_value[] = "link-value";
    const char target_value[] = "target-value";
    char list[128];
    char readback[32];
    int fd;
    long ret;
    int result = -1;

    unlink_impl(link);
    unlink_impl(target);
    fd = open_impl(target, O_CREAT | O_RDWR | O_TRUNC, 0600);
    if (fd < 0) {
        return -1;
    }
    close_if_open(fd);
    if (symlinkat(target, AT_FDCWD, link) != 0) {
        goto out;
    }

    ret = syscall_dispatch_impl(__NR_setxattr, (long)(uintptr_t)link, (long)(uintptr_t)target_name,
                                (long)(uintptr_t)target_value, sizeof(target_value), XATTR_CREATE, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    memset(readback, 0, sizeof(readback));
    ret = syscall_dispatch_impl(__NR_getxattr, (long)(uintptr_t)target, (long)(uintptr_t)target_name,
                                (long)(uintptr_t)readback, sizeof(readback), 0, 0);
    if (ret != (long)sizeof(target_value) || memcmp(readback, target_value, sizeof(target_value)) != 0) {
        errno = ret < 0 ? (int)-ret : ENOLINK;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_lsetxattr, (long)(uintptr_t)link, (long)(uintptr_t)name,
                                (long)(uintptr_t)link_value, sizeof(link_value), XATTR_CREATE, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    memset(readback, 0, sizeof(readback));
    ret = syscall_dispatch_impl(__NR_lgetxattr, (long)(uintptr_t)link, (long)(uintptr_t)name,
                                (long)(uintptr_t)readback, sizeof(readback), 0, 0);
    if (ret != (long)sizeof(link_value) || memcmp(readback, link_value, sizeof(link_value)) != 0) {
        errno = ret < 0 ? (int)-ret : ENODATA;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_getxattr, (long)(uintptr_t)target, (long)(uintptr_t)name,
                                (long)(uintptr_t)readback, sizeof(readback), 0, 0);
    if (ret != -ENODATA) {
        errno = ENOMSG;
        goto out;
    }
    memset(list, 0, sizeof(list));
    ret = syscall_dispatch_impl(__NR_llistxattr, (long)(uintptr_t)link, (long)(uintptr_t)list,
                                sizeof(list), 0, 0, 0);
    if (ret <= 0 || !native_syscall_xattr_list_contains(list, ret, name)) {
        errno = ret < 0 ? (int)-ret : ERANGE;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_lremovexattr, (long)(uintptr_t)link, (long)(uintptr_t)name,
                                0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_lgetxattr, (long)(uintptr_t)link, (long)(uintptr_t)name,
                                (long)(uintptr_t)readback, sizeof(readback), 0, 0);
    if (ret != -ENODATA) {
        errno = EBUSY;
        goto out;
    }
    result = 0;

out:
    unlink_impl(link);
    unlink_impl(target);
    return result;
}

int native_syscall_contract_xattr_lifetime_tracks_rename_exchange_and_symlink_unlink(void) {
    const char left[] = "/tmp/native-xattr-life-left";
    const char right[] = "/tmp/native-xattr-life-right";
    const char renamed[] = "/tmp/native-xattr-life-renamed";
    const char symlink[] = "/tmp/native-xattr-life-link";
    const char left_name[] = "user.life-left";
    const char right_name[] = "user.life-right";
    const char link_name[] = "user.life-link";
    const char left_value[] = "left-value";
    const char right_value[] = "right-value";
    const char link_value[] = "link-value";
    char readback[32];
    int fd;
    long ret;
    int result = -1;

    unlink_impl(symlink);
    unlink_impl(renamed);
    unlink_impl(right);
    unlink_impl(left);
    fd = open_impl(left, O_CREAT | O_RDWR | O_TRUNC, 0600);
    if (fd < 0) {
        return -1;
    }
    close_if_open(fd);
    fd = open_impl(right, O_CREAT | O_RDWR | O_TRUNC, 0600);
    if (fd < 0) {
        goto out;
    }
    close_if_open(fd);

    ret = syscall_dispatch_impl(__NR_setxattr, (long)(uintptr_t)left, (long)(uintptr_t)left_name,
                                (long)(uintptr_t)left_value, sizeof(left_value), XATTR_CREATE, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    if (renameat2(AT_FDCWD, left, AT_FDCWD, renamed, 0) != 0) {
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_getxattr, (long)(uintptr_t)renamed, (long)(uintptr_t)left_name,
                                (long)(uintptr_t)readback, sizeof(readback), 0, 0);
    if (ret != (long)sizeof(left_value) || memcmp(readback, left_value, sizeof(left_value)) != 0) {
        errno = ret < 0 ? (int)-ret : ENODATA;
        goto out;
    }

    ret = syscall_dispatch_impl(__NR_setxattr, (long)(uintptr_t)right, (long)(uintptr_t)right_name,
                                (long)(uintptr_t)right_value, sizeof(right_value), XATTR_CREATE, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    if (renameat2(AT_FDCWD, renamed, AT_FDCWD, right, AT_RENAME_EXCHANGE) != 0) {
        goto out;
    }
    memset(readback, 0, sizeof(readback));
    ret = syscall_dispatch_impl(__NR_getxattr, (long)(uintptr_t)right, (long)(uintptr_t)left_name,
                                (long)(uintptr_t)readback, sizeof(readback), 0, 0);
    if (ret != (long)sizeof(left_value) || memcmp(readback, left_value, sizeof(left_value)) != 0) {
        errno = ret < 0 ? (int)-ret : ENOMSG;
        goto out;
    }
    memset(readback, 0, sizeof(readback));
    ret = syscall_dispatch_impl(__NR_getxattr, (long)(uintptr_t)renamed, (long)(uintptr_t)right_name,
                                (long)(uintptr_t)readback, sizeof(readback), 0, 0);
    if (ret != (long)sizeof(right_value) || memcmp(readback, right_value, sizeof(right_value)) != 0) {
        errno = ret < 0 ? (int)-ret : ERANGE;
        goto out;
    }

    if (symlinkat(right, AT_FDCWD, symlink) != 0) {
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_lsetxattr, (long)(uintptr_t)symlink, (long)(uintptr_t)link_name,
                                (long)(uintptr_t)link_value, sizeof(link_value), XATTR_CREATE, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    if (unlink_impl(symlink) != 0) {
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_getxattr, (long)(uintptr_t)right, (long)(uintptr_t)left_name,
                                (long)(uintptr_t)readback, sizeof(readback), 0, 0);
    if (ret != (long)sizeof(left_value)) {
        errno = ret < 0 ? (int)-ret : ENODATA;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_lgetxattr, (long)(uintptr_t)symlink, (long)(uintptr_t)link_name,
                                (long)(uintptr_t)readback, sizeof(readback), 0, 0);
    if (ret != -ENOENT) {
        errno = EBUSY;
        goto out;
    }

    result = 0;

out:
    unlink_impl(symlink);
    unlink_impl(renamed);
    unlink_impl(right);
    unlink_impl(left);
    return result;
}

int native_syscall_contract_shared_mapping_fault_policy_tracks_truncate_after_fd_close(void) {
    struct task_struct *task = get_current();
    const char path[] = "/tmp/native-shared-map-truncate-after-close";
    char page[8192];
    char byte = 0;
    void *mapped;
    int fd = -1;
    int reopened = -1;
    long ret;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    memset(page, 'F', sizeof(page));
    unlink_impl(path);
    fd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)path,
                                    O_CREAT | O_RDWR | O_TRUNC, 0600, 0, 0);
    if (fd < 0) {
        errno = -fd;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_write, fd, (long)(uintptr_t)page, sizeof(page), 0, 0, 0);
    if (ret != (long)sizeof(page)) {
        errno = ret < 0 ? (int)-ret : EIO;
        goto out;
    }
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, sizeof(page),
                                                      PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        goto out;
    }
    close_if_open(fd);
    fd = -1;
    reopened = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)path,
                                          O_RDWR, 0, 0, 0);
    if (reopened < 0) {
        errno = -reopened;
        goto out_mapped;
    }
    if (syscall_dispatch_impl(__NR_ftruncate, reopened, 4096, 0, 0, 0, 0) != 0) {
        goto out_mapped;
    }
    close_if_open(reopened);
    reopened = -1;
    if (task_read_virtual_memory_impl(task, (uint64_t)(uintptr_t)mapped, &byte, 1) != 1 ||
        byte != 'F') {
        errno = ENODATA;
        goto out_mapped;
    }
    if (task_read_virtual_memory_impl(task, (uint64_t)(uintptr_t)mapped + 4096, &byte, 1) != -1 ||
        errno != EFAULT) {
        errno = ENOMSG;
        goto out_mapped;
    }
    if (!signal_is_pending(task, SIGBUS) ||
        task->last_fault_signal != SIGBUS ||
        task->last_fault_code != BUS_ADRERR ||
        task->last_fault_addr != (uint64_t)(uintptr_t)mapped + 4096) {
        errno = EPROTO;
        goto out_mapped;
    }

    result = 0;

out_mapped:
    close_if_open(reopened);
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, sizeof(page), 0, 0, 0, 0);
out:
    close_if_open(fd);
    unlink_impl(path);
    return result;
}

int native_syscall_contract_shared_file_mappings_are_coherent(void) {
    struct task_struct *task = get_current();
    const char path[] = "/tmp/native-shared-map-coherent";
    const char initial[] = "coherent-page";
    const char patch[] = "SEEN";
    char verify[sizeof(patch)];
    void *first;
    void *second;
    int fd = -1;
    long ret;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    fd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)path,
                                    O_CREAT | O_RDWR | O_TRUNC, 0600, 0, 0);
    if (fd < 0) {
        errno = -fd;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_write, fd, (long)(uintptr_t)initial, sizeof(initial), 0, 0, 0);
    if (ret != (long)sizeof(initial)) {
        errno = ret < 0 ? (int)-ret : EIO;
        goto out;
    }
    first = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 4096, PROT_READ | PROT_WRITE,
                                                     MAP_SHARED, fd, 0);
    if ((long)(uintptr_t)first < 0) {
        errno = -(int)(long)(uintptr_t)first;
        goto out;
    }
    second = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 4096, PROT_READ | PROT_WRITE,
                                                      MAP_SHARED, fd, 0);
    if ((long)(uintptr_t)second < 0) {
        errno = -(int)(long)(uintptr_t)second;
        goto out_first;
    }
    if (task_write_virtual_memory_impl(task, (uint64_t)(uintptr_t)first, patch, sizeof(patch) - 1) !=
        (long)sizeof(patch) - 1) {
        errno = EPROTO;
        goto out_second;
    }
    memset(verify, 0, sizeof(verify));
    if (task_read_virtual_memory_impl(task, (uint64_t)(uintptr_t)second, verify, sizeof(patch) - 1) !=
            (long)sizeof(patch) - 1 ||
        memcmp(verify, patch, sizeof(patch) - 1) != 0) {
        errno = ENODATA;
        goto out_second;
    }
    result = 0;

out_second:
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)second, 4096, 0, 0, 0, 0);
out_first:
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)first, 4096, 0, 0, 0, 0);
out:
    close_if_open(fd);
    return result;
}

int native_syscall_contract_shared_file_mappings_are_coherent_across_reopen(void) {
    struct task_struct *task = get_current();
    const char path[] = "/tmp/native-shared-map-coherent-reopen";
    const char initial[] = "coherent-reopened-page";
    const char patch[] = "REOPEN";
    char verify[sizeof(patch)];
    void *first;
    void *second;
    int fd1 = -1;
    int fd2 = -1;
    long ret;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    fd1 = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)path,
                                     O_CREAT | O_RDWR | O_TRUNC, 0600, 0, 0);
    if (fd1 < 0) {
        errno = -fd1;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_write, fd1, (long)(uintptr_t)initial, sizeof(initial), 0, 0, 0);
    if (ret != (long)sizeof(initial)) {
        errno = ret < 0 ? (int)-ret : EIO;
        goto out;
    }
    fd2 = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)path,
                                     O_RDWR, 0, 0, 0);
    if (fd2 < 0) {
        errno = -fd2;
        goto out;
    }
    first = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 4096, PROT_READ | PROT_WRITE,
                                                     MAP_SHARED, fd1, 0);
    if ((long)(uintptr_t)first < 0) {
        errno = -(int)(long)(uintptr_t)first;
        goto out;
    }
    second = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 4096, PROT_READ | PROT_WRITE,
                                                      MAP_SHARED, fd2, 0);
    if ((long)(uintptr_t)second < 0) {
        errno = -(int)(long)(uintptr_t)second;
        goto out_first;
    }
    if (task_write_virtual_memory_impl(task, (uint64_t)(uintptr_t)first, patch, sizeof(patch) - 1) !=
        (long)sizeof(patch) - 1) {
        errno = EPROTO;
        goto out_second;
    }
    memset(verify, 0, sizeof(verify));
    if (task_read_virtual_memory_impl(task, (uint64_t)(uintptr_t)second, verify, sizeof(patch) - 1) !=
            (long)sizeof(patch) - 1 ||
        memcmp(verify, patch, sizeof(patch) - 1) != 0) {
        errno = ENODATA;
        goto out_second;
    }
    result = 0;

out_second:
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)second, 4096, 0, 0, 0, 0);
out_first:
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)first, 4096, 0, 0, 0, 0);
out:
    close_if_open(fd2);
    close_if_open(fd1);
    return result;
}

int native_syscall_contract_shared_file_mappings_are_coherent_across_hardlink(void) {
    struct task_struct *task = get_current();
    const char path[] = "/tmp/native-shared-map-hardlink-source";
    const char alias[] = "/tmp/native-shared-map-hardlink-alias";
    const char initial[] = "coherent-hardlink-page";
    const char patch[] = "HLINK";
    char verify[sizeof(patch)];
    void *first;
    void *second;
    int fd1 = -1;
    int fd2 = -1;
    long ret;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    unlink_impl(alias);
    unlink_impl(path);
    fd1 = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)path,
                                     O_CREAT | O_RDWR | O_TRUNC, 0600, 0, 0);
    if (fd1 < 0) {
        errno = -fd1;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_write, fd1, (long)(uintptr_t)initial, sizeof(initial), 0, 0, 0);
    if (ret != (long)sizeof(initial)) {
        errno = ret < 0 ? (int)-ret : EIO;
        goto out;
    }
    if (link_impl(path, alias) != 0) {
        goto out;
    }
    fd2 = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)alias,
                                     O_RDWR, 0, 0, 0);
    if (fd2 < 0) {
        errno = -fd2;
        goto out;
    }
    first = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 4096, PROT_READ | PROT_WRITE,
                                                     MAP_SHARED, fd1, 0);
    if ((long)(uintptr_t)first < 0) {
        errno = -(int)(long)(uintptr_t)first;
        goto out;
    }
    second = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 4096, PROT_READ | PROT_WRITE,
                                                      MAP_SHARED, fd2, 0);
    if ((long)(uintptr_t)second < 0) {
        errno = -(int)(long)(uintptr_t)second;
        goto out_first;
    }
    if (task_write_virtual_memory_impl(task, (uint64_t)(uintptr_t)first, patch, sizeof(patch) - 1) !=
        (long)sizeof(patch) - 1) {
        errno = EPROTO;
        goto out_second;
    }
    memset(verify, 0, sizeof(verify));
    if (task_read_virtual_memory_impl(task, (uint64_t)(uintptr_t)second, verify, sizeof(patch) - 1) !=
            (long)sizeof(patch) - 1 ||
        memcmp(verify, patch, sizeof(patch) - 1) != 0) {
        errno = ENODATA;
        goto out_second;
    }
    result = 0;

out_second:
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)second, 4096, 0, 0, 0, 0);
out_first:
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)first, 4096, 0, 0, 0, 0);
out:
    close_if_open(fd2);
    close_if_open(fd1);
    unlink_impl(alias);
    unlink_impl(path);
    return result;
}

int native_syscall_contract_shared_hardlink_mapping_fault_policy_tracks_truncate_after_original_unlink(void) {
    struct task_struct *task = get_current();
    const char path[] = "/tmp/native-shared-map-hardlink-unlink-source";
    const char alias[] = "/tmp/native-shared-map-hardlink-unlink-alias";
    char pages[8192];
    char byte = 0;
    void *first;
    void *second;
    int fd1 = -1;
    int fd2 = -1;
    long ret;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    memset(pages, 'H', sizeof(pages));
    unlink_impl(alias);
    unlink_impl(path);
    fd1 = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)path,
                                     O_CREAT | O_RDWR | O_TRUNC, 0600, 0, 0);
    if (fd1 < 0) {
        errno = -fd1;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_write, fd1, (long)(uintptr_t)pages, sizeof(pages), 0, 0, 0);
    if (ret != (long)sizeof(pages)) {
        errno = ret < 0 ? (int)-ret : EIO;
        goto out;
    }
    if (link_impl(path, alias) != 0) {
        goto out;
    }
    first = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, sizeof(pages), PROT_READ | PROT_WRITE,
                                                     MAP_SHARED, fd1, 0);
    if ((long)(uintptr_t)first < 0) {
        errno = -(int)(long)(uintptr_t)first;
        goto out;
    }
    fd2 = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)alias,
                                     O_RDWR, 0, 0, 0);
    if (fd2 < 0) {
        errno = -fd2;
        goto out_first;
    }
    second = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, sizeof(pages), PROT_READ | PROT_WRITE,
                                                      MAP_SHARED, fd2, 0);
    if ((long)(uintptr_t)second < 0) {
        errno = -(int)(long)(uintptr_t)second;
        goto out_first;
    }
    if (unlink_impl(path) != 0) {
        goto out_second;
    }
    close_if_open(fd1);
    fd1 = -1;
    ret = syscall_dispatch_impl(__NR_ftruncate, fd2, 4096, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out_second;
    }
    close_if_open(fd2);
    fd2 = -1;

    if (task_read_virtual_memory_impl(task, (uint64_t)(uintptr_t)first, &byte, 1) != 1 ||
        byte != 'H' ||
        task_read_virtual_memory_impl(task, (uint64_t)(uintptr_t)second, &byte, 1) != 1 ||
        byte != 'H') {
        errno = ENODATA;
        goto out_second;
    }
    if (task_read_virtual_memory_impl(task, (uint64_t)(uintptr_t)first + 4096, &byte, 1) >= 0 ||
        errno != EFAULT) {
        errno = ENOMSG;
        goto out_second;
    }
    if (task_read_virtual_memory_impl(task, (uint64_t)(uintptr_t)second + 4096, &byte, 1) >= 0 ||
        errno != EFAULT) {
        errno = ERANGE;
        goto out_second;
    }
    result = 0;

out_second:
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)second, sizeof(pages), 0, 0, 0, 0);
out_first:
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)first, sizeof(pages), 0, 0, 0, 0);
out:
    close_if_open(fd2);
    close_if_open(fd1);
    unlink_impl(alias);
    unlink_impl(path);
    return result;
}

int native_syscall_contract_clone_without_vm_copies_private_vmas(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child;
    void *mapped;
    int32_t child_pid;
    char parent_before = 'P';
    char parent_after = 'p';
    char child_after = 'c';
    char byte = 0;
    int result = -1;

    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 4096, PROT_READ | PROT_WRITE,
                                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        return -1;
    }
    if (task_write_virtual_memory_impl(parent, (uint64_t)(uintptr_t)mapped, &parent_before, 1) != 1) {
        errno = EPROTO;
        goto out_map;
    }
    child_pid = clone_impl(0);
    if (child_pid < 0) {
        goto out_map;
    }
    child = task_lookup(child_pid);
    if (!child) {
        errno = ESRCH;
        goto out_map;
    }
    if (child->mm == parent->mm ||
        task_read_virtual_memory_impl(child, (uint64_t)(uintptr_t)mapped, &byte, 1) != 1 ||
        byte != parent_before) {
        errno = ENODATA;
        goto out_child;
    }
    if (task_write_virtual_memory_impl(parent, (uint64_t)(uintptr_t)mapped, &parent_after, 1) != 1 ||
        task_read_virtual_memory_impl(child, (uint64_t)(uintptr_t)mapped, &byte, 1) != 1 ||
        byte != parent_before) {
        errno = EPROTO;
        goto out_child;
    }
    if (task_write_virtual_memory_impl(child, (uint64_t)(uintptr_t)mapped, &child_after, 1) != 1 ||
        task_read_virtual_memory_impl(parent, (uint64_t)(uintptr_t)mapped, &byte, 1) != 1 ||
        byte != parent_after) {
        errno = EBUSY;
        goto out_child;
    }
    result = 0;

out_child:
    task_unlink_child_impl(parent, child);
    free_task(child);
    free_task(child);
out_map:
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 4096, 0, 0, 0, 0);
    return result;
}

int native_syscall_contract_clone_without_vm_cows_private_file_mapping(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child;
    const char path[] = "/tmp/native-clone-private-file-cow";
    char page[4096];
    char parent_patch = 'P';
    char child_patch = 'C';
    char byte = 0;
    void *mapped;
    int fd = -1;
    int32_t child_pid;
    long ret;
    int result = -1;

    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    memset(page, 'F', sizeof(page));
    unlink_impl(path);
    fd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)path,
                                    O_CREAT | O_RDWR | O_TRUNC, 0600, 0, 0);
    if (fd < 0) {
        errno = -fd;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_write, fd, (long)(uintptr_t)page, sizeof(page), 0, 0, 0);
    if (ret != (long)sizeof(page)) {
        errno = ret < 0 ? (int)-ret : EIO;
        goto out;
    }
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, sizeof(page),
                                                      PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        goto out;
    }
    child_pid = clone_impl(0);
    if (child_pid < 0) {
        goto out_mapped;
    }
    child = task_lookup(child_pid);
    if (!child) {
        errno = ESRCH;
        goto out_mapped;
    }
    if (child->mm == parent->mm ||
        task_write_virtual_memory_impl(child, (uint64_t)(uintptr_t)mapped, &child_patch, 1) != 1 ||
        task_read_virtual_memory_impl(parent, (uint64_t)(uintptr_t)mapped, &byte, 1) != 1 ||
        byte != 'F') {
        errno = ENODATA;
        goto out_child;
    }
    if (task_write_virtual_memory_impl(parent, (uint64_t)(uintptr_t)mapped, &parent_patch, 1) != 1 ||
        task_read_virtual_memory_impl(child, (uint64_t)(uintptr_t)mapped, &byte, 1) != 1 ||
        byte != child_patch) {
        errno = EBUSY;
        goto out_child;
    }
    ret = syscall_dispatch_impl(__NR_pread64, fd, (long)(uintptr_t)&byte, 1, 0, 0, 0);
    if (ret != 1 || byte != 'F') {
        errno = ret < 0 ? (int)-ret : EIO;
        goto out_child;
    }
    result = 0;

out_child:
    task_unlink_child_impl(parent, child);
    free_task(child);
    free_task(child);
out_mapped:
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, sizeof(page), 0, 0, 0, 0);
out:
    close_if_open(fd);
    unlink_impl(path);
    return result;
}

int native_syscall_contract_private_file_cow_smaps_reports_anonymous_dirty_page(void) {
    struct task_struct *task = get_current();
    const char path[] = "/tmp/native-private-file-cow-smaps";
    char pages[8192];
    char smaps[8192];
    void *mapped;
    int fd = -1;
    long ret;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    memset(pages, 'S', sizeof(pages));
    unlink_impl(path);
    fd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)path,
                                    O_CREAT | O_RDWR | O_TRUNC, 0600, 0, 0);
    if (fd < 0) {
        errno = -fd;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_write, fd, (long)(uintptr_t)pages, sizeof(pages), 0, 0, 0);
    if (ret != (long)sizeof(pages)) {
        errno = ret < 0 ? (int)-ret : EIO;
        goto out;
    }
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, sizeof(pages),
                                                      PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        goto out;
    }
    if (task_write_virtual_memory_impl(task, (uint64_t)(uintptr_t)mapped, "D", 1) != 1) {
        goto out_mapped;
    }
    if (read_file_into_buffer("/proc/self/smaps", smaps, sizeof(smaps)) != 0 ||
        !strstr(smaps, path) ||
        !strstr(smaps, "Private_Clean:         4 kB") ||
        !strstr(smaps, "Private_Dirty:         4 kB") ||
        !strstr(smaps, "Anonymous:             4 kB")) {
        errno = errno ? errno : ENODATA;
        goto out_mapped;
    }
    result = 0;

out_mapped:
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, sizeof(pages), 0, 0, 0, 0);
out:
    close_if_open(fd);
    unlink_impl(path);
    return result;
}

int native_syscall_contract_smaps_splits_mprotect_runs_and_preserves_dirty_counts(void) {
    struct task_struct *task = get_current();
    void *mapped = (void *)-1;
    uint64_t base;
    static char smaps[262144];
    char left[32];
    char middle[32];
    char right[32];
    long ret;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 12288,
                                                      PROT_READ | PROT_WRITE,
                                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        return -1;
    }
    base = (uint64_t)(uintptr_t)mapped;
    if (task_write_virtual_memory_impl(task, base, "L", 1) != 1 ||
        task_write_virtual_memory_impl(task, base + 8192, "R", 1) != 1) {
        errno = EPROTO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_mprotect, (long)(uintptr_t)(base + 4096),
                                4096, PROT_READ, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    if (format_maps_range(left, sizeof(left), base, base + 4096) != 0 ||
        format_maps_range(middle, sizeof(middle), base + 4096, base + 8192) != 0 ||
        format_maps_range(right, sizeof(right), base + 8192, base + 12288) != 0 ||
        read_file_into_buffer("/proc/self/smaps", smaps, sizeof(smaps)) != 0) {
        goto out;
    }
    if (!smaps_block_contains(smaps, left, "rw-p") ||
        !smaps_block_contains(smaps, left, "Size:                  4 kB") ||
        !smaps_block_contains(smaps, left, "Private_Dirty:         4 kB") ||
        !smaps_block_contains(smaps, middle, "r--p") ||
        !smaps_block_contains(smaps, middle, "Size:                  4 kB") ||
        !smaps_block_contains(smaps, middle, "Private_Dirty:         0 kB") ||
        !smaps_block_contains(smaps, right, "rw-p") ||
        !smaps_block_contains(smaps, right, "Size:                  4 kB") ||
        !smaps_block_contains(smaps, right, "Private_Dirty:         4 kB")) {
        errno = ENODATA;
        goto out;
    }
    result = 0;

out:
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 12288, 0, 0, 0, 0);
    return result;
}

int native_syscall_contract_map_fixed_gap_coalesces_compatible_anonymous_vmas(void) {
    struct task_struct *task = get_current();
    void *mapped = (void *)-1;
    void *middle = (void *)-1;
    uint64_t base;
    char maps[8192];
    char smaps[16384];
    char full[32];
    char split[32];
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 12288,
                                                      PROT_READ | PROT_WRITE,
                                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        return -1;
    }
    base = (uint64_t)(uintptr_t)mapped;
    if (task_write_virtual_memory_impl(task, base, "L", 1) != 1 ||
        task_write_virtual_memory_impl(task, base + 8192, "R", 1) != 1 ||
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)(base + 4096), 4096, 0, 0, 0, 0) != 0) {
        goto out;
    }

    middle = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, (long)(uintptr_t)(base + 4096),
                                                      4096, PROT_READ | PROT_WRITE,
                                                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                                                      -1, 0);
    if (middle != (void *)(uintptr_t)(base + 4096)) {
        errno = (long)(uintptr_t)middle < 0 ? -(int)(long)(uintptr_t)middle : EPROTO;
        goto out;
    }
    if (task_write_virtual_memory_impl(task, base + 4096, "M", 1) != 1) {
        errno = EPROTO;
        goto out;
    }
    if (format_maps_range(full, sizeof(full), base, base + 12288) != 0 ||
        format_maps_range(split, sizeof(split), base + 4096, base + 8192) != 0 ||
        read_file_into_buffer("/proc/self/maps", maps, sizeof(maps)) != 0 ||
        read_file_into_buffer("/proc/self/smaps", smaps, sizeof(smaps)) != 0) {
        goto out;
    }
    if (!strstr(maps, full) || !smaps_block_contains(smaps, full, "rw-p") ||
        !smaps_block_contains(smaps, full, "Size:                 12 kB") ||
        !smaps_block_contains(smaps, full, "Private_Dirty:        12 kB")) {
        errno = ENODATA;
        goto out;
    }
    if (strstr(maps, split) || smaps_block_contains(smaps, split, "")) {
        errno = EEXIST;
        goto out;
    }

    result = 0;

out:
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 12288, 0, 0, 0, 0);
    return result;
}

int native_syscall_contract_private_file_cow_smaps_survives_munmap_gap(void) {
    struct task_struct *task = get_current();
    const char path[] = "/tmp/native-private-cow-smaps-gap";
    char pages[12288];
    char smaps[16384];
    char left[32];
    char middle[32];
    char right[32];
    void *mapped = (void *)-1;
    uint64_t base;
    int fd = -1;
    long ret;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    memset(pages, 'F', sizeof(pages));
    unlink_impl(path);
    fd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)path,
                                    O_CREAT | O_RDWR | O_TRUNC, 0600, 0, 0);
    if (fd < 0) {
        errno = -fd;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_write, fd, (long)(uintptr_t)pages, sizeof(pages), 0, 0, 0);
    if (ret != (long)sizeof(pages)) {
        errno = ret < 0 ? (int)-ret : EIO;
        goto out;
    }
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, sizeof(pages),
                                                      PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        goto out;
    }
    base = (uint64_t)(uintptr_t)mapped;
    if (task_write_virtual_memory_impl(task, base, "A", 1) != 1 ||
        task_write_virtual_memory_impl(task, base + 8192, "C", 1) != 1) {
        errno = EPROTO;
        goto out_mapped;
    }
    ret = syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)(base + 4096), 4096, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out_mapped;
    }
    if (format_maps_range(left, sizeof(left), base, base + 4096) != 0 ||
        format_maps_range(middle, sizeof(middle), base + 4096, base + 8192) != 0 ||
        format_maps_range(right, sizeof(right), base + 8192, base + 12288) != 0) {
        errno = EDOM;
        goto out_mapped;
    }
    {
        int smaps_fd = open_impl("/proc/self/smaps", O_RDONLY, 0);
        long smaps_read;
        if (smaps_fd < 0) {
            goto out_mapped;
        }
        smaps_read = pread_impl(smaps_fd, smaps, sizeof(smaps) - 1, 0);
        close_if_open(smaps_fd);
        if (smaps_read <= 0) {
            errno = ENOMSG;
            goto out_mapped;
        }
        smaps[smaps_read] = '\0';
    }
    if (!smaps_block_contains(smaps, left, path) ||
        !smaps_block_contains(smaps, left, "Private_Dirty:         4 kB") ||
        !smaps_block_contains(smaps, left, "Anonymous:             4 kB") ||
        smaps_block_contains(smaps, middle, path) ||
        !smaps_block_contains(smaps, right, path) ||
        !smaps_block_contains(smaps, right, "Private_Dirty:         4 kB") ||
        !smaps_block_contains(smaps, right, "Anonymous:             4 kB")) {
        errno = ENODATA;
        goto out_mapped;
    }
    result = 0;

out_mapped:
    {
        int saved_errno = errno;
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)base, 4096, 0, 0, 0, 0);
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)(base + 8192), 4096, 0, 0, 0, 0);
        errno = saved_errno;
    }
out:
    {
        int saved_errno = errno;
        close_if_open(fd);
        unlink_impl(path);
        errno = saved_errno;
    }
    return result;
}

int native_syscall_contract_private_file_cow_survives_truncate_and_clean_page_faults(void) {
    struct task_struct *task = get_current();
    const char path[] = "/tmp/native-private-cow-truncate";
    char pages[8192];
    char verify[8];
    char smaps[16384];
    char range[32];
    void *mapped = (void *)-1;
    uint64_t base;
    int fd = -1;
    long ret;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    memset(pages, 'T', sizeof(pages));
    unlink_impl(path);
    fd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)path,
                                    O_CREAT | O_RDWR | O_TRUNC, 0600, 0, 0);
    if (fd < 0) {
        errno = -fd;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_write, fd, (long)(uintptr_t)pages, sizeof(pages), 0, 0, 0);
    if (ret != (long)sizeof(pages)) {
        errno = ret < 0 ? (int)-ret : EIO;
        goto out;
    }
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, sizeof(pages),
                                                      PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        goto out;
    }
    base = (uint64_t)(uintptr_t)mapped;
    if (task_write_virtual_memory_impl(task, base, "COW", 3) != 3) {
        errno = EPROTO;
        goto out_mapped;
    }
    ret = syscall_dispatch_impl(__NR_ftruncate, fd, 4096, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out_mapped;
    }
    memset(verify, 0, sizeof(verify));
    if (task_read_virtual_memory_impl(task, base, verify, 3) != 3 ||
        memcmp(verify, "COW", 3) != 0) {
        errno = ENODATA;
        goto out_mapped;
    }
    if (task_read_virtual_memory_impl(task, base + 4096, verify, 1) != -1 ||
        errno != EFAULT ||
        task->last_fault_signal != SIGBUS ||
        task->last_fault_code != BUS_ADRERR ||
        task->last_fault_addr != base + 4096) {
        errno = EPROTO;
        goto out_mapped;
    }
    if (format_maps_range(range, sizeof(range), base, base + 8192) != 0 ||
        read_file_into_buffer("/proc/self/smaps", smaps, sizeof(smaps)) != 0) {
        goto out_mapped;
    }
    if (!smaps_block_contains(smaps, range, path) ||
        !smaps_block_contains(smaps, range, "Private_Dirty:         4 kB") ||
        !smaps_block_contains(smaps, range, "Anonymous:             4 kB")) {
        errno = ENOMSG;
        goto out_mapped;
    }
    result = 0;

out_mapped:
    {
        int saved_errno = errno;
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, sizeof(pages), 0, 0, 0, 0);
        errno = saved_errno;
    }
out:
    {
        int saved_errno = errno;
        close_if_open(fd);
        unlink_impl(path);
        errno = saved_errno;
    }
    return result;
}

int native_syscall_contract_partial_truncate_zero_fills_and_mincore_tracks_pages(void) {
    struct task_struct *task = get_current();
    const char path[] = "/tmp/native-partial-truncate-map";
    char pages[8192];
    unsigned char vec[2] = {0};
    char byte = 1;
    void *mapped = (void *)-1;
    uint64_t base;
    int fd = -1;
    long ret;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    memset(pages, 'P', sizeof(pages));
    unlink_impl(path);
    fd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)path,
                                    O_CREAT | O_RDWR | O_TRUNC, 0600, 0, 0);
    if (fd < 0) {
        errno = -fd;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_write, fd, (long)(uintptr_t)pages, sizeof(pages), 0, 0, 0);
    if (ret != (long)sizeof(pages)) {
        errno = ret < 0 ? (int)-ret : EIO;
        goto out;
    }
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, sizeof(pages),
                                                      PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        goto out;
    }
    base = (uint64_t)(uintptr_t)mapped;
    ret = syscall_dispatch_impl(__NR_ftruncate, fd, 5000, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out_mapped;
    }
    byte = 1;
    if (task_read_virtual_memory_impl(task, base + 4999, &byte, 1) != 1 || byte != 'P') {
        errno = ENODATA;
        goto out_mapped;
    }
    byte = 1;
    if (task_read_virtual_memory_impl(task, base + 5000, &byte, 1) != 1 || byte != 0) {
        errno = ERANGE;
        goto out_mapped;
    }
    if (task_read_virtual_memory_impl(task, base + 8192, &byte, 1) != -1 ||
        errno != EFAULT ||
        task->last_fault_signal != SIGSEGV ||
        task->last_fault_code != SEGV_MAPERR) {
        errno = EPROTO;
        goto out_mapped;
    }
    if (syscall_dispatch_impl(__NR_mincore, (long)(uintptr_t)mapped, sizeof(pages),
                              (long)(uintptr_t)vec, 0, 0, 0) != 0 ||
        vec[0] != 1 || vec[1] != 1) {
        errno = ENOMSG;
        goto out_mapped;
    }
    ret = syscall_dispatch_impl(__NR_ftruncate, fd, 4096, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out_mapped;
    }
    if (syscall_dispatch_impl(__NR_mincore, (long)(uintptr_t)mapped, sizeof(pages),
                              (long)(uintptr_t)vec, 0, 0, 0) != 0 ||
        vec[0] != 1 || vec[1] != 0) {
        errno = ENXIO;
        goto out_mapped;
    }
    result = 0;

out_mapped:
    {
        int saved_errno = errno;
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, sizeof(pages), 0, 0, 0, 0);
        errno = saved_errno;
    }
out:
    {
        int saved_errno = errno;
        close_if_open(fd);
        unlink_impl(path);
        errno = saved_errno;
    }
    return result;
}

int native_syscall_contract_partial_page_msync_and_shared_growth_writeback(void) {
    struct task_struct *task = get_current();
    const char path[] = "/tmp/native-partial-msync-grow";
    char pages[8192];
    char verify = 0;
    void *mapped = (void *)-1;
    uint64_t base;
    int fd = -1;
    long ret;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    memset(pages, 'M', sizeof(pages));
    unlink_impl(path);
    fd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)path,
                                    O_CREAT | O_RDWR | O_TRUNC, 0600, 0, 0);
    if (fd < 0) {
        errno = -fd;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_write, fd, (long)(uintptr_t)pages, sizeof(pages), 0, 0, 0);
    if (ret != (long)sizeof(pages)) {
        errno = ret < 0 ? (int)-ret : EIO;
        goto out;
    }
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, sizeof(pages),
                                                      PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        goto out;
    }
    base = (uint64_t)(uintptr_t)mapped;
    ret = syscall_dispatch_impl(__NR_ftruncate, fd, 5000, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out_mapped;
    }
    if (task_write_virtual_memory_impl(task, base + 4999, "A", 1) != 1 ||
        task_write_virtual_memory_impl(task, base + 5000, "B", 1) != 1 ||
        syscall_dispatch_impl(__NR_msync, (long)(uintptr_t)mapped, 8192, MS_SYNC, 0, 0, 0) != 0) {
        errno = EIO;
        goto out_mapped;
    }
    if (pread_impl(fd, &verify, 1, 4999) != 1 || verify != 'A' ||
        pread_impl(fd, &verify, 1, 5000) != 0) {
        errno = ENODATA;
        goto out_mapped;
    }
    ret = syscall_dispatch_impl(__NR_ftruncate, fd, 8192, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out_mapped;
    }
    if (task_write_virtual_memory_impl(task, base + 7000, "G", 1) != 1 ||
        syscall_dispatch_impl(__NR_msync, (long)(uintptr_t)mapped, 8192, MS_SYNC, 0, 0, 0) != 0 ||
        pread_impl(fd, &verify, 1, 7000) != 1 || verify != 'G') {
        errno = ENOMSG;
        goto out_mapped;
    }
    result = 0;

out_mapped:
    {
        int saved_errno = errno;
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, sizeof(pages), 0, 0, 0, 0);
        errno = saved_errno;
    }
out:
    {
        int saved_errno = errno;
        close_if_open(fd);
        unlink_impl(path);
        errno = saved_errno;
    }
    return result;
}

int native_syscall_contract_rename_updates_open_fd_and_mapping_identity(void) {
    struct task_struct *task = get_current();
    const char old_path[] = "/tmp/native-rename-open-map-old";
    const char new_path[] = "/tmp/native-rename-open-map-new";
    char proc_fd_path[64];
    char link_target[512];
    char page[4096];
    char smaps[16384];
    char range[32];
    void *mapped = (void *)-1;
    uint64_t base;
    int fd = -1;
    long ret;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    memset(page, 'N', sizeof(page));
    unlink_impl(old_path);
    unlink_impl(new_path);
    fd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)old_path,
                                    O_CREAT | O_RDWR | O_TRUNC, 0600, 0, 0);
    if (fd < 0) {
        errno = -fd;
        return -1;
    }
    if (syscall_dispatch_impl(__NR_write, fd, (long)(uintptr_t)page, sizeof(page), 0, 0, 0) !=
        (long)sizeof(page)) {
        errno = EIO;
        goto out;
    }
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, sizeof(page),
                                                      PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        goto out;
    }
    base = (uint64_t)(uintptr_t)mapped;
    if (task_write_virtual_memory_impl(task, base, "R", 1) != 1) {
        goto out_mapped;
    }
    if (renameat2(AT_FDCWD, old_path, AT_FDCWD, new_path, 0) != 0 ||
        format_proc_fd_path(proc_fd_path, sizeof(proc_fd_path), fd) != 0) {
        goto out_mapped;
    }
    ret = readlink_impl(proc_fd_path, link_target, sizeof(link_target) - 1);
    if (ret < 0) {
        goto out_mapped;
    }
    link_target[ret] = '\0';
    if (!strstr(link_target, new_path) || strstr(link_target, old_path)) {
        errno = ENODATA;
        goto out_mapped;
    }
    if (format_maps_range(range, sizeof(range), base, base + 4096) != 0 ||
        read_file_into_buffer("/proc/self/smaps", smaps, sizeof(smaps)) != 0) {
        goto out_mapped;
    }
    if (!smaps_block_contains(smaps, range, new_path) ||
        smaps_block_contains(smaps, range, old_path) ||
        !smaps_block_contains(smaps, range, "Private_Dirty:         4 kB")) {
        errno = ENOMSG;
        goto out_mapped;
    }
    result = 0;

out_mapped:
    {
        int saved_errno = errno;
        if ((long)(uintptr_t)mapped >= 0) {
            syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, sizeof(page), 0, 0, 0, 0);
        }
        errno = saved_errno;
    }
out:
    {
        int saved_errno = errno;
        close_if_open(fd);
        unlink_impl(old_path);
        unlink_impl(new_path);
        errno = saved_errno;
    }
    return result;
}

int native_syscall_contract_mremap_fixed_preserves_private_file_cow_smaps(void) {
    struct task_struct *task = get_current();
    const char path[] = "/tmp/native-mremap-fixed-cow";
    char page[4096];
    char smaps[16384];
    char range[32];
    void *mapped = (void *)-1;
    void *target = (void *)-1;
    void *moved;
    uint64_t old_base;
    uint64_t new_base;
    char byte = 0;
    int fd = -1;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    memset(page, 'M', sizeof(page));
    unlink_impl(path);
    fd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)path,
                                    O_CREAT | O_RDWR | O_TRUNC, 0600, 0, 0);
    if (fd < 0) {
        errno = -fd;
        return -1;
    }
    if (syscall_dispatch_impl(__NR_write, fd, (long)(uintptr_t)page, sizeof(page), 0, 0, 0) !=
        (long)sizeof(page)) {
        errno = EIO;
        goto out;
    }
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, sizeof(page),
                                                      PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        goto out;
    }
    old_base = (uint64_t)(uintptr_t)mapped;
    if (task_write_virtual_memory_impl(task, old_base, "D", 1) != 1) {
        goto out_mapped;
    }
    target = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, sizeof(page),
                                                      PROT_READ | PROT_WRITE,
                                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)(uintptr_t)target < 0) {
        errno = -(int)(long)(uintptr_t)target;
        goto out_mapped;
    }
    moved = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mremap, (long)(uintptr_t)mapped,
                                                     sizeof(page), sizeof(page),
                                                     MREMAP_MAYMOVE | MREMAP_FIXED,
                                                     (long)(uintptr_t)target, 0);
    if (moved != target) {
        errno = (long)(uintptr_t)moved < 0 ? ENETDOWN : EPROTO;
        goto out_mapped;
    }
    mapped = (void *)-1;
    new_base = (uint64_t)(uintptr_t)moved;
    if (task_read_virtual_memory_impl(task, old_base, &byte, 1) >= 0 || errno != EFAULT) {
        errno = EBUSY;
        goto out_target;
    }
    if (task_read_virtual_memory_impl(task, new_base, &byte, 1) != 1 || byte != 'D') {
        errno = ENODATA;
        goto out_target;
    }
    if (format_maps_range(range, sizeof(range), new_base, new_base + 4096) != 0) {
        errno = EDOM;
        goto out_target;
    }
    {
        int smaps_fd = open_impl("/proc/self/smaps", O_RDONLY, 0);
        long smaps_read;
        if (smaps_fd < 0) {
            goto out_target;
        }
        smaps_read = pread_impl(smaps_fd, smaps, sizeof(smaps) - 1, 0);
        close_if_open(smaps_fd);
        if (smaps_read <= 0) {
            errno = ENOMSG;
            goto out_target;
        }
        smaps[smaps_read] = '\0';
    }
    if (!smaps_block_contains(smaps, range, path) ||
        !smaps_block_contains(smaps, range, "Private_Dirty:         4 kB") ||
        !smaps_block_contains(smaps, range, "Anonymous:             4 kB")) {
        errno = ENOMSG;
        goto out_target;
    }
    result = 0;

out_target:
    {
        int saved_errno = errno;
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)target, sizeof(page), 0, 0, 0, 0);
        errno = saved_errno;
    }
out_mapped:
    {
        int saved_errno = errno;
        if ((long)(uintptr_t)mapped >= 0) {
            syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, sizeof(page), 0, 0, 0, 0);
        }
        errno = saved_errno;
    }
out:
    {
        int saved_errno = errno;
        close_if_open(fd);
        unlink_impl(path);
        errno = saved_errno;
    }
    return result;
}

int native_syscall_contract_mremap_fixed_coalesces_file_backed_neighbors(void) {
    struct task_struct *task = get_current();
    const char path[] = "/tmp/native-mremap-fixed-file-coalesce";
    char pages[12288];
    char maps[8192];
    char smaps[16384];
    char range[32];
    void *left = (void *)-1;
    void *right = (void *)-1;
    void *middle_source = (void *)-1;
    void *moved = (void *)-1;
    uint64_t base;
    int fd = -1;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    memset(pages, 'F', sizeof(pages));
    unlink_impl(path);
    fd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)path,
                                    O_CREAT | O_RDWR | O_TRUNC, 0600, 0, 0);
    if (fd < 0) {
        errno = -fd;
        return -1;
    }
    if (syscall_dispatch_impl(__NR_write, fd, (long)(uintptr_t)pages, sizeof(pages), 0, 0, 0) !=
        (long)sizeof(pages)) {
        errno = EIO;
        goto out;
    }

    left = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 12288,
                                                    PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    if ((long)(uintptr_t)left < 0) {
        errno = -(int)(long)(uintptr_t)left;
        goto out;
    }
    base = (uint64_t)(uintptr_t)left;
    if (syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)(base + 4096), 4096, 0, 0, 0, 0) != 0) {
        goto out;
    }
    middle_source = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 4096,
                                                             PROT_READ | PROT_WRITE, MAP_PRIVATE,
                                                             fd, 4096);
    if ((long)(uintptr_t)middle_source < 0) {
        errno = -(int)(long)(uintptr_t)middle_source;
        goto out;
    }
    if (task_write_virtual_memory_impl(task, base, "L", 1) != 1 ||
        task_write_virtual_memory_impl(task, (uint64_t)(uintptr_t)middle_source, "M", 1) != 1 ||
        task_write_virtual_memory_impl(task, base + 8192, "R", 1) != 1) {
        goto out;
    }
    moved = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mremap,
                                                     (long)(uintptr_t)middle_source,
                                                     4096, 4096,
                                                     MREMAP_MAYMOVE | MREMAP_FIXED,
                                                     (long)(uintptr_t)(base + 4096), 0);
    if (moved != (void *)(uintptr_t)(base + 4096)) {
        errno = (long)(uintptr_t)moved < 0 ? -(int)(long)(uintptr_t)moved : EPROTO;
        goto out;
    }
    middle_source = (void *)-1;
    if (format_maps_range(range, sizeof(range), base, base + 12288) != 0 ||
        read_file_into_buffer("/proc/self/maps", maps, sizeof(maps)) != 0 ||
        read_file_into_buffer("/proc/self/smaps", smaps, sizeof(smaps)) != 0) {
        goto out;
    }
    if (!strstr(maps, range) ||
        !smaps_block_contains(smaps, range, path) ||
        !smaps_block_contains(smaps, range, "Size:                 12 kB") ||
        !smaps_block_contains(smaps, range, "Private_Dirty:        12 kB") ||
        !smaps_block_contains(smaps, range, "Anonymous:            12 kB")) {
        errno = ENODATA;
        goto out;
    }
    result = 0;

out:
    {
        int saved_errno = errno;
        if ((long)(uintptr_t)middle_source >= 0) {
            syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)middle_source, 4096, 0, 0, 0, 0);
        }
        if ((long)(uintptr_t)left >= 0) {
            syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)left, 12288, 0, 0, 0, 0);
        }
        if ((long)(uintptr_t)right >= 0) {
            syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)right, 4096, 0, 0, 0, 0);
        }
        close_if_open(fd);
        unlink_impl(path);
        errno = saved_errno;
    }
    return result;
}

int native_syscall_contract_mremap_fixed_rejects_zero_target(void) {
    void *mapped;
    void *remapped;
    int result = -1;

    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 4096,
                                                      PROT_READ | PROT_WRITE,
                                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        return -1;
    }

    remapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mremap, (long)(uintptr_t)mapped,
                                                        4096, 4096,
                                                        MREMAP_MAYMOVE | MREMAP_FIXED,
                                                        0, 0);
    if ((long)(uintptr_t)remapped != -EINVAL) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 4096, 0, 0, 0, 0);
    return result;
}

int native_syscall_contract_mremap_fixed_rejects_overlapping_target(void) {
    struct task_struct *task = get_current();
    void *mapped;
    void *remapped;
    uint64_t base;
    char byte = 0;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 8192,
                                                      PROT_READ | PROT_WRITE,
                                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        return -1;
    }
    base = (uint64_t)(uintptr_t)mapped;
    if (task_write_virtual_memory_impl(task, base, "A", 1) != 1) {
        goto out;
    }

    remapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mremap, (long)(uintptr_t)mapped,
                                                        8192, 8192,
                                                        MREMAP_MAYMOVE | MREMAP_FIXED,
                                                        (long)(uintptr_t)(base + 4096), 0);
    if ((long)(uintptr_t)remapped != -EINVAL) {
        errno = EPROTO;
        goto out;
    }
    if (task_read_virtual_memory_impl(task, base, &byte, 1) != 1 || byte != 'A') {
        errno = ENODATA;
        goto out;
    }

    result = 0;

out:
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 8192, 0, 0, 0, 0);
    return result;
}

int native_syscall_contract_mremap_fixed_grow_preserves_shared_file_mapping(void) {
    struct task_struct *task = get_current();
    const char path[] = "/tmp/native-mremap-fixed-grow-shared";
    char page[8192];
    char smaps[16384];
    char range[32];
    void *mapped = (void *)-1;
    void *target = (void *)-1;
    void *moved;
    char byte = 0;
    int fd = -1;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    memset(page, 'S', sizeof(page));
    unlink_impl(path);
    fd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)path,
                                    O_CREAT | O_RDWR | O_TRUNC, 0600, 0, 0);
    if (fd < 0) {
        errno = -fd;
        return -1;
    }
    if (syscall_dispatch_impl(__NR_write, fd, (long)(uintptr_t)page, sizeof(page), 0, 0, 0) !=
        (long)sizeof(page)) {
        errno = EIO;
        goto out;
    }
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 4096,
                                                      PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        goto out;
    }
    target = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 8192,
                                                      PROT_READ | PROT_WRITE,
                                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)(uintptr_t)target < 0) {
        errno = -(int)(long)(uintptr_t)target;
        goto out_mapped;
    }
    moved = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mremap, (long)(uintptr_t)mapped,
                                                     4096, 8192,
                                                     MREMAP_MAYMOVE | MREMAP_FIXED,
                                                     (long)(uintptr_t)target, 0);
    if (moved != target) {
        errno = (long)(uintptr_t)moved < 0 ? -(int)(long)(uintptr_t)moved : EPROTO;
        goto out_target;
    }
    mapped = (void *)-1;
    if (task_write_virtual_memory_impl(task, (uint64_t)(uintptr_t)moved + 4096, "Z", 1) != 1 ||
        syscall_dispatch_impl(__NR_msync, (long)(uintptr_t)moved, 8192, MS_SYNC, 0, 0, 0) != 0 ||
        pread_impl(fd, &byte, 1, 4096) != 1 || byte != 'Z') {
        errno = ENODATA;
        goto out_target;
    }
    if (format_maps_range(range, sizeof(range), (uint64_t)(uintptr_t)moved,
                          (uint64_t)(uintptr_t)moved + 8192) != 0 ||
        read_file_into_buffer("/proc/self/smaps", smaps, sizeof(smaps)) != 0) {
        goto out_target;
    }
    if (!smaps_block_contains(smaps, range, path) ||
        !smaps_block_contains(smaps, range, "Shared_Clean:          8 kB") ||
        !smaps_block_contains(smaps, range, "Shared_Dirty:          0 kB")) {
        errno = ENOMSG;
        goto out_target;
    }

    result = 0;

out_target:
    {
        int saved_errno = errno;
        if ((long)(uintptr_t)target >= 0) {
            syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)target, 8192, 0, 0, 0, 0);
        }
        errno = saved_errno;
    }
out_mapped:
    {
        int saved_errno = errno;
        if ((long)(uintptr_t)mapped >= 0) {
            syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 4096, 0, 0, 0, 0);
        }
        errno = saved_errno;
    }
out:
    {
        int saved_errno = errno;
        close_if_open(fd);
        unlink_impl(path);
        errno = saved_errno;
    }
    return result;
}

int native_syscall_contract_mremap_shrink_preserves_accounting_and_unmaps_tail(void) {
    struct task_struct *task = get_current();
    char smaps[16384];
    char kept_range[32];
    char tail_range[32];
    void *mapped;
    void *shrunk;
    uint64_t base;
    char byte = 0;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 8192,
                                                      PROT_READ | PROT_WRITE,
                                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        return -1;
    }
    base = (uint64_t)(uintptr_t)mapped;
    if (task_write_virtual_memory_impl(task, base, "A", 1) != 1 ||
        task_write_virtual_memory_impl(task, base + 4096, "B", 1) != 1) {
        goto out;
    }

    shrunk = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mremap, (long)(uintptr_t)mapped,
                                                      8192, 4096, 0, 0, 0);
    if (shrunk != mapped) {
        errno = (long)(uintptr_t)shrunk < 0 ? -(int)(long)(uintptr_t)shrunk : EPROTO;
        goto out;
    }
    if (task_read_virtual_memory_impl(task, base, &byte, 1) != 1 || byte != 'A') {
        errno = ENODATA;
        goto out;
    }
    errno = 0;
    if (task_read_virtual_memory_impl(task, base + 4096, &byte, 1) != -1 ||
        task->last_fault_signal != SIGSEGV ||
        task->last_fault_code != SEGV_MAPERR ||
        task->last_fault_addr != base + 4096) {
        errno = EPROTO;
        goto out;
    }
    if (format_maps_range(kept_range, sizeof(kept_range), base, base + 4096) != 0 ||
        format_maps_range(tail_range, sizeof(tail_range), base + 4096, base + 8192) != 0 ||
        read_file_into_buffer("/proc/self/smaps", smaps, sizeof(smaps)) != 0) {
        goto out;
    }
    if (!smaps_block_contains(smaps, kept_range, "rw-p") ||
        !smaps_block_contains(smaps, kept_range, "Size:                  4 kB") ||
        !smaps_block_contains(smaps, kept_range, "Private_Dirty:         4 kB") ||
        smaps_block_contains(smaps, tail_range, "")) {
        errno = ENODATA;
        goto out;
    }
    result = 0;

out:
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 4096, 0, 0, 0, 0);
    return result;
}

int native_syscall_contract_proc_child_smaps_tracks_file_mapping_mremap_and_munmap(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
    const char private_path[] = "/tmp/native-child-private-smaps-remap";
    const char shared_path[] = "/tmp/native-child-shared-smaps-munmap";
    char pages[8192];
    char smaps[32768];
    char proc_path[64];
    char private_kept[32];
    char private_tail[32];
    char shared_kept[32];
    char shared_tail[32];
    void *private_map = (void *)-1;
    void *shared_map = (void *)-1;
    int private_fd = -1;
    int shared_fd = -1;
    int32_t child_pid;
    uint64_t private_base;
    uint64_t shared_base;
    int result = -1;

    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    memset(pages, 'P', sizeof(pages));
    unlink_impl(private_path);
    unlink_impl(shared_path);
    private_fd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)private_path,
                                            O_CREAT | O_RDWR | O_TRUNC, 0600, 0, 0);
    shared_fd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)shared_path,
                                           O_CREAT | O_RDWR | O_TRUNC, 0600, 0, 0);
    if (private_fd < 0 || shared_fd < 0) {
        errno = private_fd < 0 ? -private_fd : -shared_fd;
        goto out;
    }
    if (syscall_dispatch_impl(__NR_write, private_fd, (long)(uintptr_t)pages, sizeof(pages), 0, 0, 0) !=
            (long)sizeof(pages) ||
        syscall_dispatch_impl(__NR_write, shared_fd, (long)(uintptr_t)pages, sizeof(pages), 0, 0, 0) !=
            (long)sizeof(pages)) {
        errno = EIO;
        goto out;
    }
    private_map = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, sizeof(pages),
                                                           PROT_READ | PROT_WRITE, MAP_PRIVATE,
                                                           private_fd, 0);
    shared_map = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, sizeof(pages),
                                                          PROT_READ | PROT_WRITE, MAP_SHARED,
                                                          shared_fd, 0);
    if ((long)(uintptr_t)private_map < 0 || (long)(uintptr_t)shared_map < 0) {
        errno = (long)(uintptr_t)private_map < 0 ? -(int)(long)(uintptr_t)private_map :
                                                   -(int)(long)(uintptr_t)shared_map;
        goto out;
    }
    private_base = (uint64_t)(uintptr_t)private_map;
    shared_base = (uint64_t)(uintptr_t)shared_map;
    if (task_write_virtual_memory_impl(parent, private_base, "C", 1) != 1 ||
        task_write_virtual_memory_impl(parent, shared_base + 4096, "S", 1) != 1) {
        goto out;
    }
    child_pid = clone_impl(0);
    if (child_pid < 0) {
        goto out;
    }
    child = task_lookup(child_pid);
    if (!child) {
        errno = ESRCH;
        goto out;
    }
    set_current(child);
    if (syscall_dispatch_impl(__NR_mremap, (long)(uintptr_t)private_map, 8192, 4096, 0, 0, 0) !=
            (long)(uintptr_t)private_map ||
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)(shared_base + 4096), 4096, 0, 0, 0, 0) != 0) {
        set_current(parent);
        goto out;
    }
    set_current(parent);

    if (proc_path_for_pid(proc_path, sizeof(proc_path), child->pid, "/smaps") != 0 ||
        format_maps_range(private_kept, sizeof(private_kept), private_base, private_base + 4096) != 0 ||
        format_maps_range(private_tail, sizeof(private_tail), private_base + 4096, private_base + 8192) != 0 ||
        format_maps_range(shared_kept, sizeof(shared_kept), shared_base, shared_base + 4096) != 0 ||
        format_maps_range(shared_tail, sizeof(shared_tail), shared_base + 4096, shared_base + 8192) != 0 ||
        read_file_into_buffer(proc_path, smaps, sizeof(smaps)) != 0) {
        goto out;
    }
    if (!smaps_block_contains(smaps, private_kept, private_path) ||
        !smaps_block_contains(smaps, private_kept, "Private_Dirty:         4 kB") ||
        smaps_block_contains(smaps, private_tail, "") ||
        !smaps_block_contains(smaps, shared_kept, shared_path) ||
        !smaps_block_contains(smaps, shared_kept, "Shared_Clean:          4 kB") ||
        smaps_block_contains(smaps, shared_tail, "")) {
        errno = ENODATA;
        goto out;
    }
    result = 0;

out:
    {
        int saved_errno = errno;
        if (child) {
            set_current(child);
            if ((long)(uintptr_t)private_map >= 0) {
                syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)private_map, 4096, 0, 0, 0, 0);
            }
            if ((long)(uintptr_t)shared_map >= 0) {
                syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)shared_map, 4096, 0, 0, 0, 0);
            }
            set_current(parent);
            task_unlink_child_impl(parent, child);
            free_task(child);
            free_task(child);
        }
        if ((long)(uintptr_t)private_map >= 0) {
            syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)private_map, sizeof(pages), 0, 0, 0, 0);
        }
        if ((long)(uintptr_t)shared_map >= 0) {
            syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)shared_map, sizeof(pages), 0, 0, 0, 0);
        }
        close_if_open(private_fd);
        close_if_open(shared_fd);
        unlink_impl(private_path);
        unlink_impl(shared_path);
        errno = saved_errno;
    }
    return result;
}

int native_syscall_contract_moved_shared_mapping_truncate_updates_fault_mincore_and_smaps(void) {
    struct task_struct *task = get_current();
    const char path[] = "/tmp/native-moved-shared-truncate";
    char page[8192];
    char smaps[16384];
    char range[32];
    unsigned char vec[2] = {0, 0};
    void *mapped = (void *)-1;
    void *target = (void *)-1;
    void *moved;
    char byte = 0;
    int fd = -1;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    memset(page, 'T', sizeof(page));
    unlink_impl(path);
    fd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)path,
                                    O_CREAT | O_RDWR | O_TRUNC, 0600, 0, 0);
    if (fd < 0) {
        errno = -fd;
        return -1;
    }
    if (syscall_dispatch_impl(__NR_write, fd, (long)(uintptr_t)page, sizeof(page), 0, 0, 0) !=
        (long)sizeof(page)) {
        errno = EIO;
        goto out;
    }
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 4096,
                                                      PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        goto out;
    }
    target = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 8192,
                                                      PROT_READ | PROT_WRITE,
                                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)(uintptr_t)target < 0) {
        errno = -(int)(long)(uintptr_t)target;
        goto out_mapped;
    }
    moved = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mremap, (long)(uintptr_t)mapped,
                                                     4096, 8192,
                                                     MREMAP_MAYMOVE | MREMAP_FIXED,
                                                     (long)(uintptr_t)target, 0);
    if (moved != target) {
        errno = (long)(uintptr_t)moved < 0 ? -(int)(long)(uintptr_t)moved : EPROTO;
        goto out_target;
    }
    mapped = (void *)-1;
    if (syscall_dispatch_impl(__NR_ftruncate, fd, 4096, 0, 0, 0, 0) != 0) {
        goto out_target;
    }
    if (syscall_dispatch_impl(__NR_mincore, (long)(uintptr_t)moved, 8192,
                              (long)(uintptr_t)vec, 0, 0, 0) != 0 ||
        vec[0] != 1 || vec[1] != 0) {
        errno = ENODATA;
        goto out_target;
    }
    if (task_read_virtual_memory_impl(task, (uint64_t)(uintptr_t)moved + 4096, &byte, 1) != -1 ||
        errno != EFAULT ||
        !signal_is_pending(task, SIGBUS) ||
        task->last_fault_signal != SIGBUS ||
        task->last_fault_code != BUS_ADRERR ||
        task->last_fault_addr != (uint64_t)(uintptr_t)moved + 4096 ||
        !latest_signal_info_matches(task, SIGBUS, BUS_ADRERR, (uint64_t)(uintptr_t)moved + 4096)) {
        errno = EPROTO;
        goto out_target;
    }
    if (format_maps_range(range, sizeof(range), (uint64_t)(uintptr_t)moved,
                          (uint64_t)(uintptr_t)moved + 8192) != 0 ||
        read_file_into_buffer("/proc/self/smaps", smaps, sizeof(smaps)) != 0 ||
        !smaps_block_contains(smaps, range, path)) {
        goto out_target;
    }
    if (syscall_dispatch_impl(__NR_ftruncate, fd, 8192, 0, 0, 0, 0) != 0 ||
        syscall_dispatch_impl(__NR_mincore, (long)(uintptr_t)moved, 8192,
                              (long)(uintptr_t)vec, 0, 0, 0) != 0 ||
        vec[0] != 1 || vec[1] != 1 ||
        task_read_virtual_memory_impl(task, (uint64_t)(uintptr_t)moved + 4096, &byte, 1) != 1) {
        errno = ENODATA;
        goto out_target;
    }

    result = 0;

out_target:
    {
        int saved_errno = errno;
        if ((long)(uintptr_t)target >= 0) {
            syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)target, 8192, 0, 0, 0, 0);
        }
        errno = saved_errno;
    }
out_mapped:
    {
        int saved_errno = errno;
        if ((long)(uintptr_t)mapped >= 0) {
            syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 4096, 0, 0, 0, 0);
        }
        errno = saved_errno;
    }
out:
    {
        int saved_errno = errno;
        close_if_open(fd);
        unlink_impl(path);
        errno = saved_errno;
    }
    return result;
}

int native_syscall_contract_madvise_split_vma_clears_each_permission_run(void) {
    struct task_struct *task = get_current();
    void *mapped = (void *)-1;
    uint64_t base;
    char smaps[16384];
    char left[32];
    char middle[32];
    char right[32];
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 12288,
                                                      PROT_READ | PROT_WRITE,
                                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        return -1;
    }
    base = (uint64_t)(uintptr_t)mapped;
    if (task_write_virtual_memory_impl(task, base, "A", 1) != 1 ||
        task_write_virtual_memory_impl(task, base + 4096, "B", 1) != 1 ||
        task_write_virtual_memory_impl(task, base + 8192, "C", 1) != 1) {
        goto out;
    }
    if (syscall_dispatch_impl(__NR_mprotect, (long)(uintptr_t)(base + 4096),
                              4096, PROT_READ, 0, 0, 0) != 0 ||
        syscall_dispatch_impl(__NR_madvise, (long)(uintptr_t)base, 12288,
                              MADV_DONTNEED, 0, 0, 0) != 0) {
        goto out;
    }
    if (format_maps_range(left, sizeof(left), base, base + 4096) != 0 ||
        format_maps_range(middle, sizeof(middle), base + 4096, base + 8192) != 0 ||
        format_maps_range(right, sizeof(right), base + 8192, base + 12288) != 0 ||
        read_file_into_buffer("/proc/self/smaps", smaps, sizeof(smaps)) != 0) {
        goto out;
    }
    if (!smaps_block_contains(smaps, left, "Private_Dirty:         0 kB") ||
        !smaps_block_contains(smaps, left, "Rss:                   0 kB") ||
        !smaps_block_contains(smaps, middle, "Private_Dirty:         0 kB") ||
        !smaps_block_contains(smaps, middle, "Rss:                   0 kB") ||
        !smaps_block_contains(smaps, right, "Private_Dirty:         0 kB") ||
        !smaps_block_contains(smaps, right, "Rss:                   0 kB")) {
        errno = ENODATA;
        goto out;
    }
    result = 0;

out:
    {
        int saved_errno = errno;
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 12288, 0, 0, 0, 0);
        errno = saved_errno;
    }
    return result;
}

int native_syscall_contract_private_file_madvise_dontneed_restores_file_page_after_cow(void) {
    struct task_struct *task = get_current();
    const char path[] = "/tmp/native-private-file-madvise-cow";
    char page[4096];
    char verify[8];
    const char original[] = "FILE";
    const char cow_patch[] = "COW!";
    void *mapped;
    int fd = -1;
    int result = -1;
    long ret;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    memset(page, 'A', sizeof(page));
    memcpy(page, original, sizeof(original) - 1);
    fd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)path,
                                    O_CREAT | O_RDWR | O_TRUNC, 0600, 0, 0);
    if (fd < 0) {
        errno = -fd;
        return -1;
    }
    if (syscall_dispatch_impl(__NR_write, fd, (long)(uintptr_t)page, sizeof(page), 0, 0, 0) !=
        (long)sizeof(page)) {
        errno = EIO;
        goto out;
    }
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, sizeof(page),
                                                      PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        goto out;
    }
    if (task_write_virtual_memory_impl(task, (uint64_t)(uintptr_t)mapped, cow_patch,
                                       sizeof(cow_patch) - 1) != (long)sizeof(cow_patch) - 1) {
        errno = EPROTO;
        goto out_mapped;
    }
    ret = syscall_dispatch_impl(__NR_madvise, (long)(uintptr_t)mapped, 4096,
                                MADV_DONTNEED, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out_mapped;
    }
    memset(verify, 0, sizeof(verify));
    if (task_read_virtual_memory_impl(task, (uint64_t)(uintptr_t)mapped, verify,
                                      sizeof(original) - 1) != (long)sizeof(original) - 1 ||
        memcmp(verify, original, sizeof(original) - 1) != 0) {
        errno = ENODATA;
        goto out_mapped;
    }
    result = 0;

out_mapped:
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, sizeof(page), 0, 0, 0, 0);
out:
    close_if_open(fd);
    unlink_impl(path);
    return result;
}

int native_syscall_contract_mprotect_file_smaps_vmflags_follow_permission_runs(void) {
    struct task_struct *task = get_current();
    const char private_path[] = "/tmp/native-mprotect-private-vmflags";
    const char shared_path[] = "/tmp/native-mprotect-shared-vmflags";
    char page[12288];
    char smaps[32768];
    char private_middle[32];
    char shared_middle[32];
    void *private_map = (void *)-1;
    void *shared_map = (void *)-1;
    int private_fd = -1;
    int shared_fd = -1;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    memset(page, 'V', sizeof(page));
    private_fd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)private_path,
                                            O_CREAT | O_RDWR | O_TRUNC, 0600, 0, 0);
    shared_fd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)shared_path,
                                           O_CREAT | O_RDWR | O_TRUNC, 0600, 0, 0);
    if (private_fd < 0 || shared_fd < 0) {
        errno = private_fd < 0 ? -private_fd : -shared_fd;
        goto out;
    }
    if (syscall_dispatch_impl(__NR_write, private_fd, (long)(uintptr_t)page, sizeof(page), 0, 0, 0) !=
            (long)sizeof(page) ||
        syscall_dispatch_impl(__NR_write, shared_fd, (long)(uintptr_t)page, sizeof(page), 0, 0, 0) !=
            (long)sizeof(page)) {
        errno = EIO;
        goto out;
    }

    private_map = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, sizeof(page),
                                                           PROT_READ | PROT_WRITE,
                                                           MAP_PRIVATE, private_fd, 0);
    shared_map = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, sizeof(page),
                                                          PROT_READ | PROT_WRITE,
                                                          MAP_SHARED, shared_fd, 0);
    if ((long)(uintptr_t)private_map < 0 || (long)(uintptr_t)shared_map < 0) {
        errno = (long)(uintptr_t)private_map < 0 ? -(int)(long)(uintptr_t)private_map :
                                                   -(int)(long)(uintptr_t)shared_map;
        goto out;
    }
    if (syscall_dispatch_impl(__NR_mprotect, (long)(uintptr_t)private_map + 4096,
                              4096, PROT_READ, 0, 0, 0) != 0 ||
        syscall_dispatch_impl(__NR_mprotect, (long)(uintptr_t)shared_map + 4096,
                              4096, PROT_READ, 0, 0, 0) != 0) {
        goto out;
    }
    if (format_maps_range(private_middle, sizeof(private_middle),
                          (uint64_t)(uintptr_t)private_map + 4096,
                          (uint64_t)(uintptr_t)private_map + 8192) != 0 ||
        format_maps_range(shared_middle, sizeof(shared_middle),
                          (uint64_t)(uintptr_t)shared_map + 4096,
                          (uint64_t)(uintptr_t)shared_map + 8192) != 0 ||
        read_file_into_buffer("/proc/self/smaps", smaps, sizeof(smaps)) != 0) {
        goto out;
    }
    if (!smaps_block_contains(smaps, private_middle, "r--p") ||
        !smaps_block_vmflags_contains(smaps, private_middle, " rd")) {
        errno = ENODATA;
        goto out;
    }
    if (!smaps_block_vmflags_lacks(smaps, private_middle, " wr")) {
        errno = EPROTO;
        goto out;
    }
    if (!smaps_block_contains(smaps, shared_middle, "r--s")) {
        errno = ECHILD;
        goto out;
    }
    if (!smaps_block_vmflags_contains(smaps, shared_middle, " rd")) {
        errno = ENOMSG;
        goto out;
    }
    if (!smaps_block_vmflags_contains(smaps, shared_middle, " sh")) {
        errno = ENOTRECOVERABLE;
        goto out;
    }
    if (!smaps_block_vmflags_lacks(smaps, shared_middle, " wr")) {
        errno = ERANGE;
        goto out;
    }
    result = 0;

out:
    {
        int saved_errno = errno;
        if ((long)(uintptr_t)private_map >= 0) {
            syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)private_map, sizeof(page), 0, 0, 0, 0);
        }
        if ((long)(uintptr_t)shared_map >= 0) {
            syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)shared_map, sizeof(page), 0, 0, 0, 0);
        }
        close_if_open(private_fd);
        close_if_open(shared_fd);
        unlink_impl(private_path);
        unlink_impl(shared_path);
        errno = saved_errno;
    }
    return result;
}

int native_syscall_contract_vma_split_chain_preserves_permission_runs(void) {
    struct task_struct *task = get_current();
    void *mapped = (void *)-1;
    void *target = (void *)-1;
    void *moved = (void *)-1;
    uint64_t base;
    uint64_t new_base;
    char maps[8192];
    char first[32];
    char second[32];
    char moved_first[32];
    char moved_second[32];
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 16384,
                                                      PROT_READ | PROT_WRITE,
                                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        return -1;
    }
    base = (uint64_t)(uintptr_t)mapped;
    if (syscall_dispatch_impl(__NR_mprotect, (long)(uintptr_t)(base + 4096),
                              4096, PROT_READ, 0, 0, 0) != 0 ||
        syscall_dispatch_impl(__NR_mprotect, (long)(uintptr_t)(base + 8192),
                              4096, PROT_READ | PROT_EXEC, 0, 0, 0) != 0 ||
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)base, 4096, 0, 0, 0, 0) != 0) {
        goto out;
    }
    if (format_maps_range(first, sizeof(first), base + 4096, base + 8192) != 0 ||
        format_maps_range(second, sizeof(second), base + 8192, base + 12288) != 0 ||
        read_file_into_buffer("/proc/self/maps", maps, sizeof(maps)) != 0) {
        goto out;
    }
    if (!strstr(maps, first) || !strstr(maps, "r--p") ||
        !strstr(maps, second) || !strstr(maps, "r-xp")) {
        errno = ENODATA;
        goto out;
    }

    target = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 8192,
                                                      PROT_READ | PROT_WRITE,
                                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)(uintptr_t)target < 0) {
        errno = -(int)(long)(uintptr_t)target;
        goto out;
    }
    new_base = (uint64_t)(uintptr_t)target;
    moved = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mremap, (long)(uintptr_t)(base + 4096),
                                                     8192, 8192,
                                                     MREMAP_MAYMOVE | MREMAP_FIXED,
                                                     (long)(uintptr_t)target, 0);
    if (moved != target) {
        errno = (long)(uintptr_t)moved < 0 ? -(int)(long)(uintptr_t)moved : EPROTO;
        goto out;
    }
    target = (void *)-1;
    if (format_maps_range(moved_first, sizeof(moved_first), new_base, new_base + 4096) != 0 ||
        format_maps_range(moved_second, sizeof(moved_second), new_base + 4096, new_base + 8192) != 0 ||
        read_file_into_buffer("/proc/self/maps", maps, sizeof(maps)) != 0) {
        goto out;
    }
    if (!strstr(maps, moved_first) || !strstr(maps, "r--p") ||
        !strstr(maps, moved_second) || !strstr(maps, "r-xp")) {
        errno = ENOMSG;
        goto out;
    }
    result = 0;

out:
    {
        int saved_errno = errno;
        if ((long)(uintptr_t)moved >= 0) {
            syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)moved, 8192, 0, 0, 0, 0);
        }
        if ((long)(uintptr_t)target >= 0) {
            syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)target, 8192, 0, 0, 0, 0);
        }
        if ((long)(uintptr_t)mapped >= 0) {
            syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
        }
        errno = saved_errno;
    }
    return result;
}

int native_syscall_contract_clone_without_vm_preserves_shared_file_mappings(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child;
    const char path[] = "/tmp/native-clone-shared-map";
    const char initial[] = "clone-shared-page";
    const char patch[] = "CSHARE";
    char verify[sizeof(patch)];
    void *mapped;
    int fd = -1;
    int32_t child_pid;
    long ret;
    int result = -1;

    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    unlink_impl(path);
    fd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)path,
                                    O_CREAT | O_RDWR | O_TRUNC, 0600, 0, 0);
    if (fd < 0) {
        errno = -fd;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_write, fd, (long)(uintptr_t)initial, sizeof(initial), 0, 0, 0);
    if (ret != (long)sizeof(initial)) {
        errno = ret < 0 ? (int)-ret : EIO;
        goto out;
    }
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 4096, PROT_READ | PROT_WRITE,
                                                      MAP_SHARED, fd, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        goto out;
    }
    child_pid = clone_impl(0);
    if (child_pid < 0) {
        goto out_mapped;
    }
    child = task_lookup(child_pid);
    if (!child) {
        errno = ESRCH;
        goto out_mapped;
    }
    if (child->mm == parent->mm ||
        task_write_virtual_memory_impl(child, (uint64_t)(uintptr_t)mapped, patch, sizeof(patch) - 1) !=
            (long)sizeof(patch) - 1) {
        errno = EPROTO;
        goto out_child;
    }
    memset(verify, 0, sizeof(verify));
    if (task_read_virtual_memory_impl(parent, (uint64_t)(uintptr_t)mapped, verify, sizeof(patch) - 1) !=
            (long)sizeof(patch) - 1 ||
        memcmp(verify, patch, sizeof(patch) - 1) != 0) {
        errno = ENODATA;
        goto out_child;
    }
    result = 0;

out_child:
    task_unlink_child_impl(parent, child);
    free_task(child);
    free_task(child);
out_mapped:
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 4096, 0, 0, 0, 0);
out:
    close_if_open(fd);
    unlink_impl(path);
    return result;
}

int native_syscall_contract_ftruncate_updates_shared_mapping_fault_policy(void) {
    struct task_struct *task = get_current();
    const char path[] = "/tmp/native-shared-map-truncate";
    char page[8192];
    char byte = 0;
    struct linux_stat st;
    void *mapped;
    int fd = -1;
    long ret;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    memset(page, 'T', sizeof(page));
    unlink_impl(path);
    fd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)path,
                                    O_CREAT | O_RDWR | O_TRUNC, 0600, 0, 0);
    if (fd < 0) {
        errno = -fd;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_write, fd, (long)(uintptr_t)page, sizeof(page), 0, 0, 0);
    if (ret != (long)sizeof(page)) {
        errno = ret < 0 ? (int)-ret : EIO;
        goto out;
    }
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, sizeof(page),
                                                      PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_ftruncate, fd, 4096, 0, 0, 0, 0);
    if (ret != 0 ||
        syscall_dispatch_impl(__NR_fstat, fd, (long)(uintptr_t)&st, 0, 0, 0, 0) != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out_mapped;
    }
    if (task_read_virtual_memory_impl(task, (uint64_t)(uintptr_t)mapped, &byte, 1) != 1 ||
        byte != 'T') {
        errno = ENODATA;
        goto out_mapped;
    }
    if (task_read_virtual_memory_impl(task, (uint64_t)(uintptr_t)mapped + 4096, &byte, 1) >= 0 ||
        errno != EFAULT) {
        errno = ENOMSG;
        goto out_mapped;
    }
    result = 0;

out_mapped:
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, sizeof(page), 0, 0, 0, 0);
out:
    close_if_open(fd);
    unlink_impl(path);
    return result;
}

int native_syscall_contract_truncated_file_mapping_fault_queues_sigbus(void) {
    struct task_struct *task = get_current();
    const char path[] = "/tmp/native-shared-map-sigbus";
    char page[8192];
    char byte = 0;
    void *mapped = (void *)-1;
    int fd = -1;
    long ret;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    memset(page, 'B', sizeof(page));
    unlink_impl(path);
    fd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)path,
                                    O_CREAT | O_RDWR | O_TRUNC, 0600, 0, 0);
    if (fd < 0) {
        errno = -fd;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_write, fd, (long)(uintptr_t)page, sizeof(page), 0, 0, 0);
    if (ret != (long)sizeof(page)) {
        errno = ret < 0 ? (int)-ret : EIO;
        goto out;
    }
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, sizeof(page),
                                                      PROT_READ, MAP_SHARED, fd, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        goto out;
    }
    if (syscall_dispatch_impl(__NR_ftruncate, fd, 4096, 0, 0, 0, 0) != 0) {
        goto out_mapped;
    }
    if (task_read_virtual_memory_impl(task, (uint64_t)(uintptr_t)mapped + 4096, &byte, 1) != -1 ||
        errno != EFAULT) {
        errno = EFAULT;
        goto out_mapped;
    }
    if (!signal_is_pending(task, SIGBUS) ||
        task->last_fault_signal != SIGBUS ||
        task->last_fault_code != BUS_ADRERR ||
        task->last_fault_addr != (uint64_t)(uintptr_t)mapped + 4096 ||
        !latest_signal_info_matches(task, SIGBUS, BUS_ADRERR, (uint64_t)(uintptr_t)mapped + 4096)) {
        errno = EPROTO;
        goto out_mapped;
    }

    result = 0;

out_mapped:
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, sizeof(page), 0, 0, 0, 0);
out:
    close_if_open(fd);
    unlink_impl(path);
    return result;
}

int native_syscall_contract_truncated_file_mapping_write_fault_queues_sigbus(void) {
    struct task_struct *task = get_current();
    const char path[] = "/tmp/native-shared-map-write-sigbus";
    char page[8192];
    void *mapped = (void *)-1;
    uint64_t fault_addr;
    int fd = -1;
    long ret;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    memset(page, 'W', sizeof(page));
    unlink_impl(path);
    fd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)path,
                                    O_CREAT | O_RDWR | O_TRUNC, 0600, 0, 0);
    if (fd < 0) {
        errno = -fd;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_write, fd, (long)(uintptr_t)page, sizeof(page), 0, 0, 0);
    if (ret != (long)sizeof(page)) {
        errno = ret < 0 ? (int)-ret : EIO;
        goto out;
    }
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, sizeof(page),
                                                      PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        goto out;
    }
    if (syscall_dispatch_impl(__NR_ftruncate, fd, 4096, 0, 0, 0, 0) != 0) {
        goto out_mapped;
    }
    fault_addr = (uint64_t)(uintptr_t)mapped + 4096;
    if (task_write_virtual_memory_impl(task, fault_addr, "x", 1) != -1 ||
        errno != EFAULT) {
        errno = EFAULT;
        goto out_mapped;
    }
    if (!signal_is_pending(task, SIGBUS) ||
        task->last_fault_signal != SIGBUS ||
        task->last_fault_code != BUS_ADRERR ||
        task->last_fault_addr != fault_addr ||
        !latest_signal_info_matches(task, SIGBUS, BUS_ADRERR, fault_addr)) {
        errno = EPROTO;
        goto out_mapped;
    }

    result = 0;

out_mapped:
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, sizeof(page), 0, 0, 0, 0);
out:
    close_if_open(fd);
    unlink_impl(path);
    return result;
}

int native_syscall_contract_dispatches_process_startup_syscalls(void) {
    struct task_struct *task = get_current();
    struct task_struct *child = NULL;
    struct __kernel_timespec invalid_sleep = {
        .tv_sec = 0,
        .tv_nsec = 1000000000LL,
    };
    struct __kernel_old_timeval tv;
    struct __kernel_old_itimerval timer;
    struct tms tms_value;
    struct linux_rusage_contract usage;
    struct new_utsname uts;
    uint64_t block_set = 1ULL << (2 - 1);
    uint64_t old_set = 0;
    uint64_t old_limit[2];
    uint64_t new_limit[2];
    unsigned char random_buf[16];
    uint32_t ruid = 1;
    uint32_t euid = 1;
    uint32_t suid = 1;
    uint32_t rgid = 1;
    uint32_t egid = 1;
    uint32_t sgid = 1;
    int tid_word = 0;
    long child_pid;
    long current_brk;
    long grown_brk;
    long second_brk;
    long shrunk_brk;
    long ret;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    ret = syscall_dispatch_impl(__NR_gettid, 0, 0, 0, 0, 0, 0);
    if (ret != task->pid) {
        errno = ESRCH;
        return -1;
    }
    memset(&uts, 0, sizeof(uts));
    ret = syscall_dispatch_impl(__NR_uname, (long)(uintptr_t)&uts, 0, 0, 0, 0, 0);
    if (ret != 0 || strcmp(uts.sysname, "Linux") != 0 || strcmp(uts.machine, "aarch64") != 0) {
        errno = ret < 0 ? (int)-ret : ENOMSG;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_nanosleep, (long)(uintptr_t)&invalid_sleep, 0, 0, 0, 0, 0);
    if (ret != -EINVAL) {
        errno = ret < 0 ? (int)-ret : EINVAL;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_clock_nanosleep, 0, 0, (long)(uintptr_t)&invalid_sleep,
                                0, 0, 0);
    if (ret != -EINVAL) {
        errno = ret < 0 ? (int)-ret : EINVAL;
        return -1;
    }
    memset(&tv, 0, sizeof(tv));
    ret = syscall_dispatch_impl(__NR_gettimeofday, (long)(uintptr_t)&tv, 0, 0, 0, 0, 0);
    if (ret != 0 || tv.tv_sec <= 0 || tv.tv_usec < 0 || tv.tv_usec >= 1000000L) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        return -1;
    }
    memset(&timer, 0, sizeof(timer));
    ret = syscall_dispatch_impl(__NR_getitimer, 0, (long)(uintptr_t)&timer, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        return -1;
    }
    memset(&timer, 0, sizeof(timer));
    ret = syscall_dispatch_impl(__NR_setitimer, 0, (long)(uintptr_t)&timer, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        return -1;
    }
    memset(random_buf, 0, sizeof(random_buf));
    ret = syscall_dispatch_impl(__NR_getrandom, (long)(uintptr_t)random_buf,
                                sizeof(random_buf), GRND_NONBLOCK, 0, 0, 0);
    if (ret != (long)sizeof(random_buf)) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        return -1;
    }
    memset(&tms_value, 0xff, sizeof(tms_value));
    ret = syscall_dispatch_impl(__NR_times, (long)(uintptr_t)&tms_value, 0, 0, 0, 0, 0);
    if (ret < 0 || tms_value.tms_utime < 0 || tms_value.tms_stime < 0 ||
        tms_value.tms_cutime < 0 || tms_value.tms_cstime < 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        return -1;
    }
    memset(&usage, 0xff, sizeof(usage));
    ret = syscall_dispatch_impl(__NR_getrusage, 0, (long)(uintptr_t)&usage, 0, 0, 0, 0);
    if (ret != 0 || usage.ru_utime.tv_usec < 0 || usage.ru_utime.tv_usec >= 1000000L ||
        usage.ru_stime.tv_usec < 0 || usage.ru_stime.tv_usec >= 1000000L) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_kill, task->pid, 0, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : ESRCH;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_tgkill, task->tgid, task->pid, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : ESRCH;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_getuid, 0, 0, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_geteuid, 0, 0, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_getgid, 0, 0, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_getegid, 0, 0, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_getpgid, 0, 0, 0, 0, 0, 0);
    if (ret != task->pgid) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_getsid, 0, 0, 0, 0, 0, 0);
    if (ret != task->sid) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_setpgid, 0, -1, 0, 0, 0, 0);
    if (ret != -EINVAL) {
        errno = ret < 0 ? (int)-ret : EINVAL;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_setsid, 0, 0, 0, 0, 0, 0);
    if (ret != -EPERM) {
        errno = ret < 0 ? (int)-ret : EPERM;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_prctl, PR_GET_NO_NEW_PRIVS, 0, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_getresuid, (long)(uintptr_t)&ruid,
                                (long)(uintptr_t)&euid, (long)(uintptr_t)&suid, 0, 0, 0);
    if (ret != 0 || ruid != 0 || euid != 0 || suid != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_getresgid, (long)(uintptr_t)&rgid,
                                (long)(uintptr_t)&egid, (long)(uintptr_t)&sgid, 0, 0, 0);
    if (ret != 0 || rgid != 0 || egid != 0 || sgid != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_setuid, 0, 0, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_setgid, 0, 0, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_setreuid, 0, 0, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_setregid, 0, 0, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_setresuid, 0, 0, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_setresgid, 0, 0, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_getgroups, 0, 0, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_setgroups, 0, 0, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        return -1;
    }
    child_pid = syscall_dispatch_impl(__NR_clone, 0, 0, 0, 0, 0, 0);
    if (child_pid <= 0) {
        errno = child_pid < 0 ? (int)-child_pid : ECHILD;
        return -1;
    }
    child = task_lookup((int32_t)child_pid);
    if (!child) {
        errno = ESRCH;
        return -1;
    }
    if (child->ppid != task->pid || child->tgid != child->pid) {
        task_unlink_child_impl(task, child);
        free_task(child);
        free_task(child);
        errno = ECHILD;
        return -1;
    }
    task_unlink_child_impl(task, child);
    free_task(child);
    free_task(child);

    current_brk = syscall_dispatch_impl(__NR_brk, 0, 0, 0, 0, 0, 0);
    if (current_brk <= 0) {
        errno = ENODATA;
        return -1;
    }
    grown_brk = syscall_dispatch_impl(__NR_brk, current_brk + 4096, 0, 0, 0, 0, 0);
    if (grown_brk != current_brk + 4096) {
        errno = ENOMSG;
        return -1;
    }
    second_brk = syscall_dispatch_impl(__NR_brk, current_brk + 8192, 0, 0, 0, 0, 0);
    if (second_brk != current_brk + 8192 ||
        task_write_virtual_memory_impl(task, (uint64_t)(current_brk + 4096), "H", 1) != 1) {
        errno = ENOLINK;
        return -1;
    }
    shrunk_brk = syscall_dispatch_impl(__NR_brk, grown_brk, 0, 0, 0, 0, 0);
    if (shrunk_brk != grown_brk ||
        task_write_virtual_memory_impl(task, (uint64_t)grown_brk, "X", 1) >= 0 || errno != EFAULT) {
        errno = EIO;
        return -1;
    }

    ret = syscall_dispatch_impl(__NR_set_tid_address, (long)(uintptr_t)&tid_word, 0, 0, 0, 0, 0);
    if (ret != task->pid || task->clear_child_tid != (uint64_t)(uintptr_t)&tid_word) {
        errno = ESRCH;
        return -1;
    }

    ret = syscall_dispatch_impl(__NR_rt_sigprocmask, SIG_BLOCK, (long)(uintptr_t)&block_set,
                                (long)(uintptr_t)&old_set, sizeof(block_set), 0, 0);
    if (ret != 0 || (old_set & block_set) != 0) {
        errno = ret < 0 ? (int)-ret : ENOTRECOVERABLE;
        return -1;
    }
    old_set = 0;
    ret = syscall_dispatch_impl(__NR_rt_sigprocmask, SIG_UNBLOCK, (long)(uintptr_t)&block_set,
                                (long)(uintptr_t)&old_set, sizeof(block_set), 0, 0);
    if (ret != 0 || (old_set & block_set) == 0) {
        errno = ret < 0 ? (int)-ret : EOWNERDEAD;
        return -1;
    }

    ret = syscall_dispatch_impl(__NR_futex, (long)(uintptr_t)&tid_word, FUTEX_WAKE_PRIVATE, 1, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EBUSY;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_futex, (long)(uintptr_t)&tid_word, FUTEX_WAIT_PRIVATE, 1, 0, 0, 0);
    if (ret != -EAGAIN) {
        errno = ERANGE;
        return -1;
    }

    memset(&old_limit, 0, sizeof(old_limit));
    ret = syscall_dispatch_impl(__NR_prlimit64, 0, RLIMIT_NOFILE, 0,
                                (long)(uintptr_t)&old_limit, 0, 0);
    if (ret != 0 || old_limit[0] == 0 || old_limit[1] == 0) {
        errno = ret < 0 ? (int)-ret : ENOSPC;
        return -1;
    }
    new_limit[0] = old_limit[0];
    new_limit[1] = old_limit[1];
    ret = syscall_dispatch_impl(__NR_prlimit64, 0, RLIMIT_NOFILE, (long)(uintptr_t)&new_limit,
                                0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : E2BIG;
        return -1;
    }

    return 0;
}

int native_syscall_contract_dispatches_shell_fd_vfs_syscalls(void) {
    const char *dir = "/tmp/native-shell-syscalls";
    const char *file = "/tmp/native-shell-syscalls/file";
    const char *renamed = "/tmp/native-shell-syscalls/renamed";
    const char *copied = "/tmp/native-shell-syscalls/copied";
    char payload[] = "helloworld";
    char cwd[128];
    char read_buf[11];
    long long copy_in_off = 0;
    long long copy_out_off = 0;
    struct open_how open_how;
    struct __kernel_timespec file_times[2];
    struct statx stx;
    struct statfs sfs;
    struct statfs fsfs;
    struct linux_stat memfd_st;
    int fd = -1;
    int outfd = -1;
    int lockfd = -1;
    int dupfd = -1;
    int memfd = -1;
    int cloexec_fd = 77;
    long ret;

    close_if_open(cloexec_fd);
    unlink_impl(file);
    unlink_impl(renamed);
    unlink_impl(copied);
    rmdir_impl(dir);

    ret = syscall_dispatch_impl(__NR_mkdirat, AT_FDCWD, (long)(uintptr_t)dir, 0700, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_umask, 0027, 0, 0, 0, 0, 0);
    if (ret < 0) {
        errno = (int)-ret;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_umask, ret, 0, 0, 0, 0, 0);
    if (ret != 0027) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    memfd = (int)syscall_dispatch_impl(__NR_memfd_create, (long)(uintptr_t)"shell-memfd",
                                       MFD_CLOEXEC | MFD_ALLOW_SEALING, 0, 0, 0, 0);
    if (memfd < 0) {
        errno = (int)-memfd;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_fcntl, memfd, F_GETFD, 0, 0, 0, 0);
    if (ret != FD_CLOEXEC) {
        errno = 102;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_fcntl, memfd, F_GET_SEALS, 0, 0, 0, 0);
    if (ret != 0) {
        errno = 103;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_write, memfd, (long)(uintptr_t)payload, 10, 0, 0, 0);
    if (ret != 10) {
        errno = 104;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_lseek, memfd, 0, 0, 0, 0, 0);
    if (ret != 0) {
        errno = 105;
        goto out;
    }
    memset(read_buf, 0, sizeof(read_buf));
    ret = syscall_dispatch_impl(__NR_read, memfd, (long)(uintptr_t)read_buf, 5, 0, 0, 0);
    if (ret != 5 || strcmp(read_buf, "hello") != 0) {
        errno = 106;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_ftruncate, memfd, 1, 0, 0, 0, 0);
    if (ret != 0) {
        errno = 107;
        goto out;
    }
    memset(&memfd_st, 0, sizeof(memfd_st));
    ret = syscall_dispatch_impl(__NR_fstat, memfd, (long)(uintptr_t)&memfd_st, 0, 0, 0, 0);
    if (ret != 0 || (memfd_st.st_mode & S_IFMT) != S_IFREG || memfd_st.st_size != 1) {
        errno = 108;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_fcntl, memfd, F_ADD_SEALS,
                                F_SEAL_GROW | F_SEAL_SEAL, 0, 0, 0);
    if (ret != 0) {
        errno = 109;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_fcntl, memfd, F_GET_SEALS, 0, 0, 0, 0);
    if (ret != (F_SEAL_GROW | F_SEAL_SEAL)) {
        errno = 110;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_ftruncate, memfd, 8, 0, 0, 0, 0);
    if (ret != -EPERM) {
        errno = 111;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_fcntl, memfd, F_ADD_SEALS, F_SEAL_WRITE, 0, 0, 0);
    if (ret != -EPERM) {
        errno = 112;
        goto out;
    }
    close_if_open(memfd);
    memfd = -1;

    ret = syscall_dispatch_impl(__NR_chdir, (long)(uintptr_t)dir, 0, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    memset(cwd, 0, sizeof(cwd));
    ret = syscall_dispatch_impl(__NR_getcwd, (long)(uintptr_t)cwd, sizeof(cwd), 0, 0, 0, 0);
    if (ret <= 0 || strcmp(cwd, dir) != 0) {
        errno = ret < 0 ? (int)-ret : ENODATA;
        goto out;
    }

    fd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)file,
                                    O_CREAT | O_RDWR | O_TRUNC, 0600, 0, 0);
    if (fd < 0) {
        errno = (int)-fd;
        goto out;
    }
    lockfd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)file,
                                        O_RDWR, 0, 0, 0);
    if (lockfd < 0) {
        errno = (int)-lockfd;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_flock, fd, LOCK_EX, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_flock, lockfd, LOCK_SH | LOCK_NB, 0, 0, 0, 0);
    if (ret != -EAGAIN) {
        errno = ret < 0 ? (int)-ret : EAGAIN;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_flock, fd, LOCK_UN, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_flock, lockfd, LOCK_SH | LOCK_NB, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_flock, lockfd, LOCK_UN, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    close_if_open(lockfd);
    lockfd = -1;
    ret = syscall_dispatch_impl(__NR_fchdir, fd, 0, 0, 0, 0, 0);
    if (ret != -ENOTDIR) {
        errno = ret < 0 ? (int)-ret : ENOTDIR;
        goto out;
    }

    dupfd = (int)syscall_dispatch_impl(__NR_dup, fd, 0, 0, 0, 0, 0);
    if (dupfd < 0) {
        errno = (int)-dupfd;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_dup3, fd, cloexec_fd, O_CLOEXEC, 0, 0, 0);
    if (ret != cloexec_fd) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_fcntl, cloexec_fd, F_GETFD, 0, 0, 0, 0);
    if (ret != FD_CLOEXEC) {
        errno = ret < 0 ? (int)-ret : ENODATA;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_close_range, dupfd, dupfd, CLOSE_RANGE_CLOEXEC, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_fcntl, dupfd, F_GETFD, 0, 0, 0, 0);
    if (ret != FD_CLOEXEC) {
        errno = ret < 0 ? (int)-ret : ENODATA;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_close_range, cloexec_fd, cloexec_fd, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_fcntl, cloexec_fd, F_GETFD, 0, 0, 0, 0);
    if (ret != -EBADF) {
        errno = ret < 0 ? (int)-ret : EBADF;
        goto out;
    }
    cloexec_fd = -1;

    ret = syscall_dispatch_impl(__NR_write, fd, (long)(uintptr_t)payload, 10, 0, 0, 0);
    if (ret != 10) {
        errno = ret < 0 ? (int)-ret : EIO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_lseek, fd, 0, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EIO;
        goto out;
    }
    memset(read_buf, 0, sizeof(read_buf));
    ret = syscall_dispatch_impl(__NR_read, fd, (long)(uintptr_t)read_buf, 5, 0, 0, 0);
    if (ret != 5 || strcmp(read_buf, "hello") != 0) {
        errno = ret < 0 ? (int)-ret : EIO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_fchmod, fd, 0644, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_fchown, fd, 1000, 1001, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_fsync, fd, 0, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_fdatasync, fd, 0, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_syncfs, fd, 0, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    memset(&fsfs, 0, sizeof(fsfs));
    ret = syscall_dispatch_impl(__NR_fstatfs, fd, (long)(uintptr_t)&fsfs, 0, 0, 0, 0);
    if (ret != 0 || fsfs.f_type == 0 || fsfs.f_bsize != 4096 ||
        fsfs.f_namelen != 255 || (fsfs.f_flags & ST_VALID) == 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    outfd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)copied,
                                       O_CREAT | O_RDWR | O_TRUNC, 0600, 0, 0);
    if (outfd < 0) {
        errno = (int)-outfd;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_copy_file_range, fd, (long)(uintptr_t)&copy_in_off,
                                outfd, (long)(uintptr_t)&copy_out_off, 10, 0);
    if (ret != 10 || copy_in_off != 10 || copy_out_off != 10) {
        errno = ret < 0 ? (int)-ret : EIO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_lseek, outfd, 0, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EIO;
        goto out;
    }
    memset(read_buf, 0, sizeof(read_buf));
    ret = syscall_dispatch_impl(__NR_read, outfd, (long)(uintptr_t)read_buf, 10, 0, 0, 0);
    if (ret != 10 || strcmp(read_buf, "helloworld") != 0) {
        errno = ret < 0 ? (int)-ret : EIO;
        goto out;
    }
    close_if_open(outfd);
    outfd = -1;
    close_if_open(fd);
    fd = -1;
    close_if_open(dupfd);
    dupfd = -1;
    close_if_open(cloexec_fd);

    ret = syscall_dispatch_impl(__NR_renameat, AT_FDCWD, (long)(uintptr_t)file,
                                AT_FDCWD, (long)(uintptr_t)renamed, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_faccessat2, AT_FDCWD, (long)(uintptr_t)renamed,
                                F_OK, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_fchmodat, AT_FDCWD, (long)(uintptr_t)renamed,
                                0604, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_fchmodat2, AT_FDCWD, (long)(uintptr_t)renamed,
                                0660, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_fchownat, AT_FDCWD, (long)(uintptr_t)renamed,
                                1002, 1003, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    file_times[0].tv_sec = 1234;
    file_times[0].tv_nsec = 567;
    file_times[1].tv_sec = 2345;
    file_times[1].tv_nsec = 678;
    ret = syscall_dispatch_impl(__NR_utimensat, AT_FDCWD, (long)(uintptr_t)renamed,
                                (long)(uintptr_t)file_times, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_sync, 0, 0, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    memset(&sfs, 0, sizeof(sfs));
    ret = syscall_dispatch_impl(__NR_statfs, (long)(uintptr_t)renamed,
                                (long)(uintptr_t)&sfs, 0, 0, 0, 0);
    if (ret != 0 || sfs.f_type == 0 || sfs.f_bsize != 4096 ||
        sfs.f_namelen != 255 || (sfs.f_flags & ST_VALID) == 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    memset(&open_how, 0, sizeof(open_how));
    open_how.flags = O_RDONLY;
    fd = (int)syscall_dispatch_impl(__NR_openat2, AT_FDCWD, (long)(uintptr_t)renamed,
                                    (long)(uintptr_t)&open_how, sizeof(open_how), 0, 0);
    if (fd < 0) {
        errno = (int)-fd;
        goto out;
    }
    memset(read_buf, 0, sizeof(read_buf));
    ret = syscall_dispatch_impl(__NR_read, fd, (long)(uintptr_t)read_buf, 5, 0, 0, 0);
    if (ret != 5 || strcmp(read_buf, "hello") != 0) {
        errno = ret < 0 ? (int)-ret : EIO;
        goto out;
    }
    close_if_open(fd);
    fd = -1;

    fd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)renamed,
                                    O_RDONLY, 0, 0, 0);
    if (fd < 0) {
        errno = (int)-fd;
        goto out;
    }
    memset(read_buf, 0, sizeof(read_buf));
    ret = syscall_dispatch_impl(__NR_read, fd, (long)(uintptr_t)read_buf, 10, 0, 0, 0);
    if (ret != 10 || strcmp(read_buf, "helloworld") != 0) {
        errno = ret < 0 ? (int)-ret : ENODATA;
        goto out;
    }
    memset(&stx, 0, sizeof(stx));
    ret = syscall_dispatch_impl(__NR_statx, AT_FDCWD, (long)(uintptr_t)renamed,
                                AT_STATX_SYNC_AS_STAT, STATX_BASIC_STATS,
                                (long)(uintptr_t)&stx, 0);
    if (ret != 0 || (stx.stx_mode & 0777) != 0660 ||
        stx.stx_uid != 1002 || stx.stx_gid != 1003 ||
        stx.stx_atime.tv_sec != 1234 || stx.stx_atime.tv_nsec != 567 ||
        stx.stx_mtime.tv_sec != 2345 || stx.stx_mtime.tv_nsec != 678) {
        errno = ret < 0 ? (int)-ret : ENODATA;
        goto out;
    }
    close_if_open(fd);
    fd = -1;

    ret = syscall_dispatch_impl(__NR_unlinkat, AT_FDCWD, (long)(uintptr_t)renamed, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_unlinkat, AT_FDCWD, (long)(uintptr_t)copied, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_chdir, (long)(uintptr_t)"/", 0, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_unlinkat, AT_FDCWD, (long)(uintptr_t)dir,
                                AT_REMOVEDIR, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    return 0;

out:
    {
    int saved_errno = errno;
    syscall_dispatch_impl(__NR_chdir, (long)(uintptr_t)"/", 0, 0, 0, 0, 0);
    close_if_open(fd);
    close_if_open(outfd);
    close_if_open(lockfd);
    close_if_open(dupfd);
    close_if_open(memfd);
    close_if_open(cloexec_fd);
    unlink_impl(copied);
    unlink_impl(file);
    unlink_impl(renamed);
    rmdir_impl(dir);
    errno = saved_errno;
    }
    return -1;
}

int native_syscall_contract_dispatches_statx_syscall(void) {
    const char *path = "/tmp/native-statx-syscall-file";
    struct statx stx;
    int fd = -1;
    long ret;

    unlink_impl(path);
    fd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)path,
                                    O_CREAT | O_RDWR | O_TRUNC, 0640, 0, 0);
    if (fd < 0) {
        errno = (int)-fd;
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_write, fd, (long)(uintptr_t)"statx", 5, 0, 0, 0);
    if (ret != 5) {
        errno = ret < 0 ? (int)-ret : EIO;
        goto out;
    }
    close_if_open(fd);
    fd = -1;

    memset(&stx, 0, sizeof(stx));
    ret = syscall_dispatch_impl(__NR_statx, AT_FDCWD, (long)(uintptr_t)path,
                                AT_STATX_SYNC_AS_STAT,
                                STATX_BASIC_STATS | STATX_MNT_ID,
                                (long)(uintptr_t)&stx, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    if ((stx.stx_mask & STATX_BASIC_STATS) != STATX_BASIC_STATS ||
        (stx.stx_mask & STATX_MNT_ID) == 0 ||
        (stx.stx_mode & S_IFMT) != S_IFREG ||
        (stx.stx_mode & 0777) != 0640 ||
        stx.stx_size != 5 ||
        stx.stx_nlink == 0) {
        errno = ENODATA;
        goto out;
    }

    ret = syscall_dispatch_impl(__NR_statx, AT_FDCWD, (long)(uintptr_t)path,
                                AT_STATX_SYNC_AS_STAT, STATX__RESERVED,
                                (long)(uintptr_t)&stx, 0);
    if (ret != -EINVAL) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    unlink_impl(path);
    return 0;

out:
    close_if_open(fd);
    unlink_impl(path);
    return -1;
}

int native_syscall_contract_dispatches_exit_and_waitid_syscalls(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child;
    struct task_struct *restore;
    struct siginfo info;
    int child_pid;
    long ret;

    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    child = task_create_child_impl(parent);
    if (!child) {
        return -1;
    }
    child_pid = child->pid;

    restore = get_current();
    set_current(child);
    ret = syscall_dispatch_impl(__NR_exit, 23, 0, 0, 0, 0, 0);
    set_current(restore);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        task_unlink_child_impl(parent, child);
        free_task(child);
        return -1;
    }

    memset(&info, 0, sizeof(info));
    ret = syscall_dispatch_impl(__NR_waitid, P_PID, child_pid, (long)(uintptr_t)&info,
                                WEXITED | WNOWAIT, 0, 0);
    if (ret != 0 ||
        info.si_signo != SIGCHLD ||
        info.si_code != CLD_EXITED ||
        info.si_pid != child_pid ||
        info.si_status != 23) {
        errno = ret < 0 ? (int)-ret : ENODATA;
        task_unlink_child_impl(parent, child);
        free_task(child);
        return -1;
    }

    memset(&info, 0, sizeof(info));
    ret = syscall_dispatch_impl(__NR_waitid, P_PID, child_pid, (long)(uintptr_t)&info,
                                WEXITED, 0, 0);
    if (ret != 0 ||
        info.si_signo != SIGCHLD ||
        info.si_code != CLD_EXITED ||
        info.si_status != 23) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        if (ret != 0) {
            task_unlink_child_impl(parent, child);
            free_task(child);
        }
        return -1;
    }
    return 0;
}

int native_syscall_contract_dispatches_pidfd_syscalls(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child;
    struct task_struct *clone_child;
    struct task_struct *restore;
    struct clone_args args;
    struct siginfo info;
    struct pollfd pfd;
    struct __kernel_timespec zero_timeout = {0, 0};
    uint64_t block_sigchld = 1ULL << (SIGCHLD - 1);
    uint64_t old_sigmask = 0;
    long ret;
    int pidfd = -1;
    int clone_pidfd = -1;
    int child_pid;
    int clone_child_pid;

    if (!parent) {
        errno = ESRCH;
        return -1;
    }

    child = task_create_child_impl(parent);
    if (!child) {
        return -1;
    }
    child_pid = child->pid;

    pidfd = (int)syscall_dispatch_impl(__NR_pidfd_open, child_pid, 0, 0, 0, 0, 0);
    if (pidfd < 0) {
        errno = pidfd < 0 ? (int)-pidfd : EPROTO;
        goto out;
    }

    clear_pending_signal(parent, SIGCHLD);
    ret = syscall_dispatch_impl(__NR_rt_sigprocmask, SIG_BLOCK,
                                (long)(uintptr_t)&block_sigchld,
                                (long)(uintptr_t)&old_sigmask,
                                sizeof(block_sigchld), 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    pfd.fd = pidfd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    ret = syscall_dispatch_impl(__NR_ppoll, (long)(uintptr_t)&pfd, 1,
                                (long)(uintptr_t)&zero_timeout, 0, 0, 0);
    if (ret != 0 || pfd.revents != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    restore = get_current();
    set_current(child);
    ret = syscall_dispatch_impl(__NR_exit, 29, 0, 0, 0, 0, 0);
    set_current(restore);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    pfd.revents = 0;
    ret = syscall_dispatch_impl(__NR_ppoll, (long)(uintptr_t)&pfd, 1,
                                (long)(uintptr_t)&zero_timeout, 0, 0, 0);
    if (ret != 1 || (pfd.revents & POLLIN) == 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    memset(&info, 0, sizeof(info));
    ret = syscall_dispatch_impl(__NR_waitid, P_PIDFD, pidfd, (long)(uintptr_t)&info,
                                WEXITED | WNOWAIT, 0, 0);
    if (ret != 0 ||
        info.si_signo != SIGCHLD ||
        info.si_code != CLD_EXITED ||
        info.si_pid != child_pid ||
        info.si_status != 29) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    memset(&info, 0, sizeof(info));
    ret = syscall_dispatch_impl(__NR_waitid, P_PIDFD, pidfd, (long)(uintptr_t)&info,
                                WEXITED, 0, 0);
    if (ret != 0 ||
        info.si_signo != SIGCHLD ||
        info.si_code != CLD_EXITED ||
        info.si_pid != child_pid ||
        info.si_status != 29) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    clear_pending_signal(parent, SIGCHLD);

    memset(&args, 0, sizeof(args));
    args.flags = CLONE_PIDFD;
    args.pidfd = (uintptr_t)&clone_pidfd;
    ret = syscall_dispatch_impl(__NR_clone3, (long)(uintptr_t)&args, sizeof(args), 0, 0, 0, 0);
    if (ret <= 0 || clone_pidfd < 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    clone_child_pid = (int)ret;
    clone_child = task_lookup(clone_child_pid);
    if (!clone_child) {
        errno = ESRCH;
        goto out;
    }

    pfd.fd = clone_pidfd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    ret = syscall_dispatch_impl(__NR_ppoll, (long)(uintptr_t)&pfd, 1,
                                (long)(uintptr_t)&zero_timeout, 0, 0, 0);
    if (ret != 0 || pfd.revents != 0) {
        free_task(clone_child);
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    restore = get_current();
    set_current(clone_child);
    ret = syscall_dispatch_impl(__NR_exit, 31, 0, 0, 0, 0, 0);
    set_current(restore);
    if (ret != 0) {
        free_task(clone_child);
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    free_task(clone_child);

    memset(&info, 0, sizeof(info));
    ret = syscall_dispatch_impl(__NR_waitid, P_PIDFD, clone_pidfd, (long)(uintptr_t)&info,
                                WEXITED, 0, 0);
    if (ret != 0 ||
        info.si_signo != SIGCHLD ||
        info.si_code != CLD_EXITED ||
        info.si_pid != clone_child_pid ||
        info.si_status != 31) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    clear_pending_signal(parent, SIGCHLD);

    ret = syscall_dispatch_impl(__NR_rt_sigprocmask, SIG_SETMASK,
                                (long)(uintptr_t)&old_sigmask, 0,
                                sizeof(old_sigmask), 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    old_sigmask = 0;

    close_if_open(pidfd);
    close_if_open(clone_pidfd);
    return 0;

out:
    if (old_sigmask != 0 || (parent && parent->signal &&
        (parent->signal->blocked.sig[0] & block_sigchld) != 0)) {
        syscall_dispatch_impl(__NR_rt_sigprocmask, SIG_UNBLOCK,
                              (long)(uintptr_t)&block_sigchld, 0,
                              sizeof(block_sigchld), 0, 0);
    }
    clear_pending_signal(parent, SIGCHLD);
    close_if_open(pidfd);
    close_if_open(clone_pidfd);
    return -1;
}

int native_syscall_contract_classifies_milestone_01_process_surface(void) {
    struct required_syscall_class {
        long number;
        enum syscall_capability_class capability_class;
    };
    struct audited_syscall_repo_truth {
        long number;
        enum syscall_matrix_override_class override_class;
    };
    #define NATIVE_SYSCALL_MILESTONE_01_IMPLEMENTED_LIST(X) \
        X(__NR_unshare, SYSCALL_CAPABILITY_PROCESS)
    #define NATIVE_SYSCALL_MILESTONE_01_NEXT_LIST(X) \
        X(__NR_pidfd_send_signal, SYSCALL_MATRIX_OVERRIDE_KERNEL_OWNED_NEXT_PROCESS) \
        X(__NR_pidfd_getfd, SYSCALL_MATRIX_OVERRIDE_KERNEL_OWNED_NEXT_PROCESS)
    enum {
        NATIVE_SYSCALL_MILESTONE_01_IMPLEMENTED_COUNT =
    #define NATIVE_SYSCALL_MILESTONE_01_COUNT_IMPLEMENTED(number, capability_class) + 1
            0 NATIVE_SYSCALL_MILESTONE_01_IMPLEMENTED_LIST(NATIVE_SYSCALL_MILESTONE_01_COUNT_IMPLEMENTED)
    #undef NATIVE_SYSCALL_MILESTONE_01_COUNT_IMPLEMENTED
    };
    enum {
        NATIVE_SYSCALL_MILESTONE_01_NEXT_COUNT =
    #define NATIVE_SYSCALL_MILESTONE_01_COUNT_NEXT(number, override_class) + 1
            0 NATIVE_SYSCALL_MILESTONE_01_NEXT_LIST(NATIVE_SYSCALL_MILESTONE_01_COUNT_NEXT)
    #undef NATIVE_SYSCALL_MILESTONE_01_COUNT_NEXT
    };
    enum {
        NATIVE_SYSCALL_MILESTONE_01_AUDITED_COUNT =
            NATIVE_SYSCALL_MILESTONE_01_IMPLEMENTED_COUNT +
            NATIVE_SYSCALL_MILESTONE_01_NEXT_COUNT,
    };
    _Static_assert(NATIVE_SYSCALL_MILESTONE_01_IMPLEMENTED_COUNT == 1,
                   "milestone 01 must keep one implemented syscall in the audited process surface");
    _Static_assert(NATIVE_SYSCALL_MILESTONE_01_NEXT_COUNT == 2,
                   "milestone 01 must keep two kernel-owned-next pidfd syscalls in the audited process surface");
    _Static_assert(NATIVE_SYSCALL_MILESTONE_01_AUDITED_COUNT == 3,
                   "milestone 01 audited process surface must stay scoped to the reviewed three-syscall set");
    static const struct required_syscall_class required_classes[] = {
    #define NATIVE_SYSCALL_MILESTONE_01_EMIT_IMPLEMENTED(number, capability_class) {number, capability_class},
        NATIVE_SYSCALL_MILESTONE_01_IMPLEMENTED_LIST(NATIVE_SYSCALL_MILESTONE_01_EMIT_IMPLEMENTED)
    #undef NATIVE_SYSCALL_MILESTONE_01_EMIT_IMPLEMENTED
    };
    static const struct audited_syscall_repo_truth next_syscalls[] = {
    #define NATIVE_SYSCALL_MILESTONE_01_EMIT_NEXT(number, override_class) {number, override_class},
        NATIVE_SYSCALL_MILESTONE_01_NEXT_LIST(NATIVE_SYSCALL_MILESTONE_01_EMIT_NEXT)
    #undef NATIVE_SYSCALL_MILESTONE_01_EMIT_NEXT
    };
    #undef NATIVE_SYSCALL_MILESTONE_01_NEXT_LIST
    #undef NATIVE_SYSCALL_MILESTONE_01_IMPLEMENTED_LIST
    long ret;

    for (size_t i = 0; i < sizeof(required_classes) / sizeof(required_classes[0]); i++) {
        if (syscall_capability_class_impl(required_classes[i].number) != required_classes[i].capability_class ||
            syscall_matrix_override_class_impl(required_classes[i].number) != SYSCALL_MATRIX_OVERRIDE_NONE ||
            !syscall_is_implemented_impl(required_classes[i].number)) {
            errno = ENOMSG;
            return -1;
        }
    }

    ret = syscall_dispatch_impl(__NR_unshare, 0, 0, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        return -1;
    }

    ret = syscall_dispatch_impl(__NR_unshare, CLONE_FS, 0, 0, 0, 0, 0);
    if (ret >= 0) {
        errno = EPROTO;
        return -1;
    }

    for (size_t i = 0; i < sizeof(next_syscalls) / sizeof(next_syscalls[0]); i++) {
        if (syscall_is_implemented_impl(next_syscalls[i].number) ||
            syscall_matrix_override_class_impl(next_syscalls[i].number) != next_syscalls[i].override_class) {
            errno = ENOTSUP;
            return -1;
        }
        ret = syscall_dispatch_impl(next_syscalls[i].number, 0, 0, 0, 0, 0, 0);
        if (ret != -ENOSYS) {
            errno = ret < 0 ? (int)-ret : EPROTO;
            return -1;
        }
    }

    return 0;
}

int native_syscall_contract_mlibc_linux_sysdeps_inventory_is_kernel_owned(void) {
    struct required_syscall_class {
        long number;
        enum syscall_capability_class capability_class;
    };
    struct planned_syscall_gap {
        long number;
        enum syscall_gap_priority priority;
    };
    static const long required_syscalls[] = {
        __NR_read,
        __NR_write,
        __NR_readv,
        __NR_writev,
        __NR_pread64,
        __NR_pwrite64,
        __NR_lseek,
        __NR_openat,
        __NR_close,
        __NR_dup,
        __NR_dup3,
        __NR_pipe2,
        __NR_fcntl,
        __NR_brk,
        __NR_set_tid_address,
        __NR_gettid,
        __NR_clone,
        __NR_futex,
        __NR_set_robust_list,
        __NR_get_robust_list,
        __NR_rt_sigaction,
        __NR_sigaltstack,
        __NR_rt_sigreturn,
        __NR_restart_syscall,
        __NR_rt_sigprocmask,
        __NR_kill,
        __NR_tgkill,
        __NR_ioctl,
        __NR_getdents64,
        __NR_ppoll,
        __NR_pselect6,
        __NR_epoll_create1,
        __NR_epoll_ctl,
        __NR_epoll_pwait,
        __NR_readlinkat,
        __NR_mkdirat,
        __NR_unlinkat,
        __NR_renameat,
        __NR_renameat2,
        __NR_chdir,
        __NR_fchdir,
        __NR_umask,
        __NR_fchmod,
        __NR_fchmodat,
        __NR_fchmodat2,
        __NR_fchown,
        __NR_fchownat,
        __NR_statfs,
        __NR_fstatfs,
        __NR_sync,
        __NR_fsync,
        __NR_fdatasync,
        __NR_syncfs,
        __NR_newfstatat,
        __NR_fstat,
        __NR_faccessat,
        __NR_faccessat2,
        __NR_statx,
        __NR_getcwd,
        __NR_getpid,
        __NR_getppid,
        __NR_uname,
        __NR_getuid,
        __NR_geteuid,
        __NR_getgid,
        __NR_getegid,
        __NR_setuid,
        __NR_setgid,
        __NR_setreuid,
        __NR_setregid,
        __NR_setresuid,
        __NR_getresuid,
        __NR_setresgid,
        __NR_getresgid,
        __NR_getgroups,
        __NR_setgroups,
        __NR_getpgid,
        __NR_getsid,
        __NR_setpgid,
        __NR_setsid,
        __NR_prctl,
        __NR_exit,
        __NR_exit_group,
        __NR_mmap,
        __NR_mprotect,
        __NR_munmap,
        __NR_mremap,
        __NR_madvise,
        __NR_mincore,
        __NR_mount_setattr,
        __NR_open_tree,
        __NR_move_mount,
        __NR_pivot_root,
        __NR_listmount,
        __NR_statmount,
        __NR_msync,
        __NR_ftruncate,
        __NR_setxattr,
        __NR_lsetxattr,
        __NR_fsetxattr,
        __NR_getxattr,
        __NR_lgetxattr,
        __NR_fgetxattr,
        __NR_listxattr,
        __NR_llistxattr,
        __NR_flistxattr,
        __NR_removexattr,
        __NR_lremovexattr,
        __NR_fremovexattr,
        __NR_prlimit64,
        __NR_clock_gettime,
        __NR_nanosleep,
        __NR_clock_nanosleep,
        __NR_gettimeofday,
        __NR_getitimer,
        __NR_setitimer,
        __NR_getrandom,
        __NR_times,
        __NR_getrusage,
        __NR_close_range,
        __NR_copy_file_range,
        __NR_openat2,
        __NR_utimensat,
        __NR_flock,
        __NR_eventfd2,
        __NR_timerfd_create,
        __NR_timerfd_settime,
        __NR_timerfd_gettime,
        __NR_memfd_create,
        __NR_pidfd_open,
        __NR_execve,
        __NR_execveat,
        __NR_wait4,
        __NR_waitid,
        __NR_clone3,
    };
    static const struct required_syscall_class required_classes[] = {
        {__NR_readv, SYSCALL_CAPABILITY_FD},
        {__NR_writev, SYSCALL_CAPABILITY_FD},
        {__NR_lseek, SYSCALL_CAPABILITY_FD},
        {__NR_openat, SYSCALL_CAPABILITY_FD},
        {__NR_dup, SYSCALL_CAPABILITY_FD},
        {__NR_dup3, SYSCALL_CAPABILITY_FD},
        {__NR_pipe2, SYSCALL_CAPABILITY_FD},
        {__NR_mkdirat, SYSCALL_CAPABILITY_FD},
        {__NR_unlinkat, SYSCALL_CAPABILITY_FD},
        {__NR_renameat, SYSCALL_CAPABILITY_FD},
        {__NR_renameat2, SYSCALL_CAPABILITY_FD},
        {__NR_chdir, SYSCALL_CAPABILITY_FD},
        {__NR_fchdir, SYSCALL_CAPABILITY_FD},
        {__NR_umask, SYSCALL_CAPABILITY_FD},
        {__NR_fchmod, SYSCALL_CAPABILITY_FD},
        {__NR_fchmodat, SYSCALL_CAPABILITY_FD},
        {__NR_fchmodat2, SYSCALL_CAPABILITY_FD},
        {__NR_fchown, SYSCALL_CAPABILITY_FD},
        {__NR_fchownat, SYSCALL_CAPABILITY_FD},
        {__NR_statfs, SYSCALL_CAPABILITY_FD},
        {__NR_fstatfs, SYSCALL_CAPABILITY_FD},
        {__NR_sync, SYSCALL_CAPABILITY_FD},
        {__NR_fsync, SYSCALL_CAPABILITY_FD},
        {__NR_fdatasync, SYSCALL_CAPABILITY_FD},
        {__NR_syncfs, SYSCALL_CAPABILITY_FD},
        {__NR_faccessat, SYSCALL_CAPABILITY_FD},
        {__NR_faccessat2, SYSCALL_CAPABILITY_FD},
        {__NR_statx, SYSCALL_CAPABILITY_FD},
        {__NR_getpid, SYSCALL_CAPABILITY_PROCESS},
        {__NR_gettid, SYSCALL_CAPABILITY_PROCESS},
        {__NR_clone, SYSCALL_CAPABILITY_PROCESS},
        {__NR_uname, SYSCALL_CAPABILITY_PROCESS},
        {__NR_getuid, SYSCALL_CAPABILITY_PROCESS},
        {__NR_geteuid, SYSCALL_CAPABILITY_PROCESS},
        {__NR_getgid, SYSCALL_CAPABILITY_PROCESS},
        {__NR_getegid, SYSCALL_CAPABILITY_PROCESS},
        {__NR_setuid, SYSCALL_CAPABILITY_PROCESS},
        {__NR_setgid, SYSCALL_CAPABILITY_PROCESS},
        {__NR_setreuid, SYSCALL_CAPABILITY_PROCESS},
        {__NR_setregid, SYSCALL_CAPABILITY_PROCESS},
        {__NR_setresuid, SYSCALL_CAPABILITY_PROCESS},
        {__NR_getresuid, SYSCALL_CAPABILITY_PROCESS},
        {__NR_setresgid, SYSCALL_CAPABILITY_PROCESS},
        {__NR_getresgid, SYSCALL_CAPABILITY_PROCESS},
        {__NR_getgroups, SYSCALL_CAPABILITY_PROCESS},
        {__NR_setgroups, SYSCALL_CAPABILITY_PROCESS},
        {__NR_getpgid, SYSCALL_CAPABILITY_PROCESS},
        {__NR_getsid, SYSCALL_CAPABILITY_PROCESS},
        {__NR_setpgid, SYSCALL_CAPABILITY_PROCESS},
        {__NR_setsid, SYSCALL_CAPABILITY_PROCESS},
        {__NR_prctl, SYSCALL_CAPABILITY_PROCESS},
        {__NR_exit, SYSCALL_CAPABILITY_PROCESS},
        {__NR_exit_group, SYSCALL_CAPABILITY_PROCESS},
        {__NR_execve, SYSCALL_CAPABILITY_PROCESS},
        {__NR_execveat, SYSCALL_CAPABILITY_PROCESS},
        {__NR_waitid, SYSCALL_CAPABILITY_PROCESS},
        {__NR_rt_sigaction, SYSCALL_CAPABILITY_SIGNAL},
        {__NR_kill, SYSCALL_CAPABILITY_SIGNAL},
        {__NR_tgkill, SYSCALL_CAPABILITY_SIGNAL},
        {__NR_mmap, SYSCALL_CAPABILITY_VM},
        {__NR_ppoll, SYSCALL_CAPABILITY_READINESS},
        {__NR_epoll_pwait, SYSCALL_CAPABILITY_READINESS},
        {__NR_mount_setattr, SYSCALL_CAPABILITY_MOUNT},
        {__NR_statmount, SYSCALL_CAPABILITY_MOUNT},
        {__NR_setxattr, SYSCALL_CAPABILITY_XATTR},
        {__NR_clock_gettime, SYSCALL_CAPABILITY_TIME},
        {__NR_nanosleep, SYSCALL_CAPABILITY_TIME},
        {__NR_clock_nanosleep, SYSCALL_CAPABILITY_TIME},
        {__NR_gettimeofday, SYSCALL_CAPABILITY_TIME},
        {__NR_getitimer, SYSCALL_CAPABILITY_TIME},
        {__NR_setitimer, SYSCALL_CAPABILITY_TIME},
        {__NR_getrandom, SYSCALL_CAPABILITY_RANDOM},
        {__NR_times, SYSCALL_CAPABILITY_RESOURCE},
        {__NR_getrusage, SYSCALL_CAPABILITY_RESOURCE},
        {__NR_close_range, SYSCALL_CAPABILITY_FD},
        {__NR_copy_file_range, SYSCALL_CAPABILITY_FD},
        {__NR_openat2, SYSCALL_CAPABILITY_FD},
        {__NR_utimensat, SYSCALL_CAPABILITY_FD},
        {__NR_flock, SYSCALL_CAPABILITY_FD},
        {__NR_eventfd2, SYSCALL_CAPABILITY_READINESS},
        {__NR_timerfd_create, SYSCALL_CAPABILITY_READINESS},
        {__NR_timerfd_settime, SYSCALL_CAPABILITY_READINESS},
        {__NR_timerfd_gettime, SYSCALL_CAPABILITY_READINESS},
        {__NR_memfd_create, SYSCALL_CAPABILITY_FD},
        {__NR_pidfd_open, SYSCALL_CAPABILITY_PROCESS},
        {__NR_prlimit64, SYSCALL_CAPABILITY_RESOURCE},
    };
    static const struct planned_syscall_gap planned_gaps[] = {
        {__NR_socket, SYSCALL_GAP_NETWORK},
        {__NR_socketpair, SYSCALL_GAP_NETWORK},
        {__NR_connect, SYSCALL_GAP_NETWORK},
        {__NR_sendto, SYSCALL_GAP_NETWORK},
        {__NR_recvfrom, SYSCALL_GAP_NETWORK},
        {__NR_sendmsg, SYSCALL_GAP_NETWORK},
        {__NR_recvmsg, SYSCALL_GAP_NETWORK},
        {__NR_recvmmsg, SYSCALL_GAP_NETWORK},
        {__NR_sendmmsg, SYSCALL_GAP_NETWORK},
    };

    for (size_t i = 0; i < sizeof(required_syscalls) / sizeof(required_syscalls[0]); i++) {
        if (!syscall_is_implemented_impl(required_syscalls[i])) {
            errno = ENOSYS;
            return -1;
        }
    }

    for (size_t i = 0; i < sizeof(required_classes) / sizeof(required_classes[0]); i++) {
        if (syscall_capability_class_impl(required_classes[i].number) != required_classes[i].capability_class) {
            errno = ENOMSG;
            return -1;
        }
    }

    for (size_t i = 0; i < sizeof(planned_gaps) / sizeof(planned_gaps[0]); i++) {
        if (syscall_is_implemented_impl(planned_gaps[i].number) ||
            syscall_gap_priority_impl(planned_gaps[i].number) != planned_gaps[i].priority) {
            errno = ENOTSUP;
            return -1;
        }
    }

    return 0;
}

int native_syscall_contract_registers_native_artifact_descriptor(void) {
    native_program_t program;

    native_registry_clear();
    if (native_register_artifact("//sbin///init", "/ixland/packages/core/init.xcframework",
                                 "native-ios-arm64", native_init_entry) != 0) {
        return -1;
    }
    if (native_lookup_program("/sbin/init", &program) != 0) {
        native_registry_clear();
        return -1;
    }
    if (strcmp(program.path, "/sbin/init") != 0 ||
        strcmp(program.artifact_path, "/ixland/packages/core/init.xcframework") != 0 ||
        strcmp(program.abi, "native-ios-arm64") != 0 ||
        program.entry != native_init_entry) {
        native_registry_clear();
        errno = EPROTO;
        return -1;
    }
    native_registry_clear();
    return 0;
}

int native_syscall_contract_execs_sbin_init_through_syscall_surface(void) {
    struct task_struct *task = get_current();
    char *argv[] = {"/sbin/init", NULL};
    char *envp[] = {"PATH=/bin:/usr/bin", NULL};
    long ret;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    native_registry_clear();
    init_entry_seen = 0;
    if (native_register_artifact("/sbin/init", "/ixland/packages/core/init.xcframework",
                                 "native-ios-arm64", native_init_entry) != 0) {
        return -1;
    }

    ret = syscall_dispatch_impl(__NR_execve, (long)(uintptr_t)"/sbin/init",
                                (long)(uintptr_t)argv, (long)(uintptr_t)envp, 0, 0, 0);
    if (ret != 0 || !init_entry_seen || strcmp(task->exe, "/sbin/init") != 0 ||
        strcmp(task->comm, "init") != 0) {
        native_registry_clear();
        errno = ret < 0 ? (int)-ret : EPROTO;
        return -1;
    }

    native_registry_clear();
    return 0;
}

int native_syscall_contract_returns_raw_negative_errno(void) {
    long ret;

    ret = syscall_dispatch_impl(__NR_read, -1, 0, 1, 0, 0, 0);
    if (expect_raw_errno(ret, EFAULT) != 0) {
        return -1;
    }

    ret = syscall_dispatch_impl(__NR_close, -1, 0, 0, 0, 0, 0);
    if (expect_raw_errno(ret, EBADF) != 0) {
        return -1;
    }

    ret = syscall_dispatch_impl(-1, 0, 0, 0, 0, 0, 0);
    return expect_raw_errno(ret, ENOSYS);
}

int native_syscall_contract_registered_program_uses_syscall_surface(void) {
    int pipefd[2] = {-1, -1};
    char env_storage[32];
    char *argv[] = {"native-syscall", "arg", NULL};
    char *envp[] = {env_storage, NULL};
    char buf[64];
    long ret;
    int status;
    int result = -1;

    ret = syscall_dispatch_impl(__NR_pipe2, (long)(uintptr_t)pipefd, O_CLOEXEC, 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        return -1;
    }

    if (syscall_dispatch_impl(__NR_fcntl, pipefd[0], F_SETFD, 0, 0, 0, 0) != 0 ||
        syscall_dispatch_impl(__NR_fcntl, pipefd[1], F_SETFD, 0, 0, 0, 0) != 0) {
        errno = EPROTO;
        goto out;
    }

    if (format_outfd_env(env_storage, sizeof(env_storage), pipefd[1]) != 0) {
        goto out;
    }

    native_registry_clear();
    if (native_register("/usr/bin/native-syscall", native_syscall_entry) != 0) {
        goto out;
    }

    status = (int)syscall_dispatch_impl(__NR_execve, (long)(uintptr_t)"/usr/bin/native-syscall",
                                        (long)(uintptr_t)argv, (long)(uintptr_t)envp, 0, 0, 0);
    if (status != 42) {
        errno = status < 0 ? -status : EPROTO;
        goto out;
    }

    memset(buf, 0, sizeof(buf));
    ret = syscall_dispatch_impl(__NR_read, pipefd[0], (long)(uintptr_t)buf, sizeof(buf), 0, 0, 0);
    if (ret != 17 || memcmp(buf, "native-syscall-ok", 17) != 0) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    native_registry_clear();
    close_if_open(pipefd[0]);
    close_if_open(pipefd[1]);
    return result;
}

int native_syscall_contract_proc_self_maps_reports_permission_runs(void) {
    struct task_struct *task = get_current();
    void *mapped = (void *)-1;
    int fd = -1;
    char maps[2048];
    long ret;
    char needle[32];
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 12288,
                                                      PROT_READ | PROT_WRITE,
                                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_mprotect, (long)(uintptr_t)mapped + 4096, 4096,
                                PROT_READ, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    fd = open_impl("/proc/self/maps", O_RDONLY, 0);
    if (fd < 0) {
        result = errno ? errno : ENOENT;
        goto out;
    }
    ret = read_impl(fd, maps, sizeof(maps) - 1);
    if (ret <= 0) {
        result = errno ? errno : ENODATA;
        goto out;
    }
    maps[ret] = '\0';
    if (format_maps_range(needle, sizeof(needle), (uint64_t)(uintptr_t)mapped,
                          (uint64_t)((uintptr_t)mapped + 4096)) != 0 ||
        !strstr(maps, needle) ||
        !strstr(maps, "rw-p") ||
        !strstr(maps, "r--p")) {
        result = ENODATA;
        goto out;
    }
    result = 0;

out:
    close_if_open(fd);
    if ((long)(uintptr_t)mapped >= 0) {
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 12288, 0, 0, 0, 0);
    }
    return result;
}

int native_syscall_contract_dev_zero_mmap_is_virtual_zero_memory(void) {
    struct task_struct *task = get_current();
    int fd = -1;
    void *mapped = (void *)-1;
    unsigned char bytes[4] = {1, 1, 1, 1};
    char maps[2048];
    long ret;
    int maps_fd = -1;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    fd = open_impl("/dev/zero", O_RDWR, 0);
    if (fd < 0) {
        return errno ? errno : ENOENT;
    }
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 4096,
                                                      PROT_READ | PROT_WRITE,
                                                      MAP_PRIVATE, fd, 0);
    if ((long)(uintptr_t)mapped < 0) {
        result = -(int)(long)(uintptr_t)mapped;
        goto out;
    }
    if (task_read_virtual_memory_impl(task, (uint64_t)(uintptr_t)mapped, bytes, sizeof(bytes)) !=
            (long)sizeof(bytes) ||
        bytes[0] != 0 || bytes[1] != 0 || bytes[2] != 0 || bytes[3] != 0 ||
        task_write_virtual_memory_impl(task, (uint64_t)(uintptr_t)mapped, "D", 1) != 1) {
        result = errno ? errno : EIO;
        goto out;
    }
    maps_fd = open_impl("/proc/self/maps", O_RDONLY, 0);
    if (maps_fd < 0) {
        result = errno ? errno : ENOENT;
        goto out;
    }
    ret = read_impl(maps_fd, maps, sizeof(maps) - 1);
    if (ret <= 0) {
        result = errno ? errno : ENODATA;
        goto out;
    }
    maps[ret] = '\0';
    if (!strstr(maps, "/dev/zero") || contains_host_path_fragment(maps)) {
        result = ENODATA;
        goto out;
    }
    result = 0;

out:
    close_if_open(maps_fd);
    if ((long)(uintptr_t)mapped >= 0) {
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 4096, 0, 0, 0, 0);
    }
    close_if_open(fd);
    return result;
}

static int append_decimal_value(char *buf, size_t buf_len, size_t *pos, int value) {
    char digits[16];
    int count = 0;

    if (!buf || !pos || value < 0 || *pos >= buf_len) {
        errno = EINVAL;
        return -1;
    }
    do {
        digits[count++] = (char)('0' + (value % 10));
        value /= 10;
    } while (value > 0 && count < (int)sizeof(digits));
    if (value > 0 || *pos + (size_t)count + 1 > buf_len) {
        errno = ENAMETOOLONG;
        return -1;
    }
    for (int i = count - 1; i >= 0; i--) {
        buf[(*pos)++] = digits[i];
    }
    buf[*pos] = '\0';
    return 0;
}

static int proc_path_for_pid(char *buf, size_t buf_len, int pid, const char *suffix) {
    size_t pos = 0;
    const char prefix[] = "/proc/";
    size_t prefix_len = sizeof(prefix) - 1;
    size_t suffix_len;

    if (!buf || !suffix || buf_len <= prefix_len) {
        errno = EINVAL;
        return -1;
    }
    memcpy(buf, prefix, prefix_len);
    pos = prefix_len;
    if (append_decimal_value(buf, buf_len, &pos, pid) != 0) {
        return -1;
    }
    suffix_len = strlen(suffix);
    if (pos + suffix_len + 1 > buf_len) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(buf + pos, suffix, suffix_len + 1);
    return 0;
}

static int read_file_into_buffer(const char *path, char *buf, size_t buf_len) {
    int fd;
    long nread;

    fd = open_impl(path, O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }
    nread = read_impl(fd, buf, buf_len - 1);
    close_if_open(fd);
    if (nread <= 0) {
        errno = errno ? errno : ENODATA;
        return -1;
    }
    buf[nread] = '\0';
    return 0;
}

static int status_value_kb(const char *content, const char *name, uint64_t *out_value) {
    const char *line;
    const char *cursor;
    uint64_t value = 0;

    if (!content || !name || !out_value) {
        errno = EINVAL;
        return -1;
    }
    line = strstr(content, name);
    if (!line) {
        errno = ENOENT;
        return -1;
    }
    cursor = line + strlen(name);
    if (*cursor != '\t') {
        errno = EPROTO;
        return -1;
    }
    cursor++;
    if (*cursor < '0' || *cursor > '9') {
        errno = EPROTO;
        return -1;
    }
    while (*cursor >= '0' && *cursor <= '9') {
        value = (value * 10ULL) + (uint64_t)(*cursor - '0');
        cursor++;
    }
    if (strncmp(cursor, " kB", 3) != 0) {
        errno = EPROTO;
        return -1;
    }
    *out_value = value;
    return 0;
}

static int latest_signal_info_matches(struct task_struct *task, int signo, int code, uint64_t addr) {
    struct signal_queue_entry *entry;

    if (!task || !task->signal) {
        return 0;
    }

    entry = task->signal->queue.tail;
    return entry &&
           entry->sig == signo &&
           entry->si_signo == signo &&
           entry->si_code == code &&
           entry->fault_addr == addr;
}

static void clear_pending_task_signal(struct task_struct *task, int signo) {
    int bit;

    if (!task || signo <= 0) {
        return;
    }
    bit = (signo - 1) & 63;
    task->thread_pending_signals &= ~(1ULL << bit);
    if (task->signal) {
        task->signal->shared_pending.sig[0] &= ~(1ULL << bit);
    }
}

int native_syscall_contract_virtual_memory_faults_queue_sigsegv_codes(void) {
    struct task_struct *task = get_current();
    void *mapped;
    char byte = 0;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 4096,
                                                      PROT_READ | PROT_WRITE,
                                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        return -1;
    }
    if (syscall_dispatch_impl(__NR_mprotect, (long)(uintptr_t)mapped, 4096,
                              PROT_READ, 0, 0, 0) != 0 ||
        task_write_virtual_memory_impl(task, (uint64_t)(uintptr_t)mapped, "x", 1) != -1 ||
        errno != EACCES ||
        !signal_is_pending(task, SIGSEGV) ||
        task->last_fault_signal != SIGSEGV ||
        task->last_fault_code != SEGV_ACCERR ||
        task->last_fault_addr != (uint64_t)(uintptr_t)mapped ||
        !latest_signal_info_matches(task, SIGSEGV, SEGV_ACCERR, (uint64_t)(uintptr_t)mapped)) {
        result = errno ? errno : EPROTO;
        goto out;
    }
    if (task_read_virtual_memory_impl(task, (uint64_t)(uintptr_t)mapped + 8192, &byte, 1) != -1 ||
        errno != EFAULT ||
        !signal_is_pending(task, SIGSEGV) ||
        task->last_fault_signal != SIGSEGV ||
        task->last_fault_code != SEGV_MAPERR ||
        task->last_fault_addr != (uint64_t)(uintptr_t)mapped + 8192 ||
        !latest_signal_info_matches(task, SIGSEGV, SEGV_MAPERR, (uint64_t)(uintptr_t)mapped + 8192)) {
        result = errno ? errno : EPROTO;
        goto out;
    }
    result = 0;

out:
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 4096, 0, 0, 0, 0);
    return result;
}

int native_syscall_contract_prot_none_read_fault_queues_sigsegv_accerr(void) {
    struct task_struct *task = get_current();
    void *mapped;
    char byte = 0;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 4096,
                                                      PROT_READ | PROT_WRITE,
                                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        return -1;
    }
    clear_pending_task_signal(task, SIGSEGV);
    if (syscall_dispatch_impl(__NR_mprotect, (long)(uintptr_t)mapped, 4096,
                              PROT_NONE, 0, 0, 0) != 0 ||
        task_read_virtual_memory_impl(task, (uint64_t)(uintptr_t)mapped, &byte, 1) != -1 ||
        errno != EACCES ||
        !signal_is_pending(task, SIGSEGV) ||
        task->last_fault_signal != SIGSEGV ||
        task->last_fault_code != SEGV_ACCERR ||
        task->last_fault_addr != (uint64_t)(uintptr_t)mapped ||
        !latest_signal_info_matches(task, SIGSEGV, SEGV_ACCERR, (uint64_t)(uintptr_t)mapped)) {
        result = errno ? errno : EPROTO;
        goto out;
    }
    result = 0;

out:
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 4096, 0, 0, 0, 0);
    return result;
}

int native_syscall_contract_stack_guard_write_grows_and_below_guard_faults(void) {
    struct task_struct *task = get_current();
    struct mm_struct *old_mm;
    struct mm_struct *mm = NULL;
    uint64_t stack_base = 0x700000000000ULL;
    uint64_t stack_size = 4096;
    uint64_t guard_addr = stack_base - 1;
    uint64_t below_guard_addr = stack_base - (3 * 4096);
    char byte = 0;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    old_mm = task->mm;
    mm = calloc(1, sizeof(*mm));
    if (!mm) {
        errno = ENOMEM;
        return -1;
    }
    atomic_init(&mm->refs, 1);
    mm->initial_stack_base = stack_base;
    mm->initial_stack_size = stack_size;
    mm->initial_stack_image_size = stack_size;
    mm->initial_stack_image = calloc(1, (size_t)stack_size);
    if (!mm->initial_stack_image) {
        errno = ENOMEM;
        goto out;
    }
    mm->vmas[0].start = stack_base - 4096;
    mm->vmas[0].end = stack_base;
    mm->vmas[0].flags = PF_R | PF_W;
    mm->vmas[0].kind = TASK_VMA_GUARD;
    mm->vmas[0].page_count = 1;
    mm->vmas[1].start = stack_base;
    mm->vmas[1].end = stack_base + stack_size;
    mm->vmas[1].flags = PF_R | PF_W;
    mm->vmas[1].kind = TASK_VMA_STACK;
    mm->vmas[1].image = mm->initial_stack_image;
    mm->vmas[1].image_size = stack_size;
    mm->vmas[1].page_count = 1;
    mm->vmas[1].page_flags = calloc(1, sizeof(*mm->vmas[1].page_flags));
    mm->vmas[1].resident_pages = calloc(1, sizeof(*mm->vmas[1].resident_pages));
    mm->vmas[1].dirty_pages = calloc(1, sizeof(*mm->vmas[1].dirty_pages));
    if (!mm->vmas[1].page_flags || !mm->vmas[1].resident_pages || !mm->vmas[1].dirty_pages) {
        errno = ENOMEM;
        goto out;
    }
    mm->vmas[1].page_flags[0] = PF_R | PF_W;
    mm->vma_count = 2;

    task->mm = mm;
    clear_pending_task_signal(task, SIGSEGV);
    if (task_write_virtual_memory_impl(task, guard_addr, "g", 1) != 1 ||
        task_read_virtual_memory_impl(task, guard_addr, &byte, 1) != 1 ||
        byte != 'g' ||
        task->mm->initial_stack_base != stack_base - 4096 ||
        task->mm->initial_stack_size != stack_size + 4096 ||
        task->mm->vmas[0].start != stack_base - 8192 ||
        task->mm->vmas[0].end != stack_base - 4096 ||
        task->mm->vmas[1].start != stack_base - 4096 ||
        task->mm->vmas[1].end != stack_base + stack_size) {
        result = errno ? errno : EPROTO;
        goto out;
    }
    if (task_write_virtual_memory_impl(task, below_guard_addr, "x", 1) != -1 ||
        errno != EFAULT ||
        !signal_is_pending(task, SIGSEGV) ||
        task->last_fault_signal != SIGSEGV ||
        task->last_fault_code != SEGV_MAPERR ||
        task->last_fault_addr != below_guard_addr ||
        !latest_signal_info_matches(task, SIGSEGV, SEGV_MAPERR, below_guard_addr)) {
        result = errno ? errno : EPROTO;
        goto out;
    }

    result = 0;

out:
    task->mm = old_mm;
    if (mm) {
        task_mm_put_impl(mm);
    }
    return result;
}

int native_syscall_contract_partial_copy_records_sigbus_fault_address(void) {
    struct task_struct *task = get_current();
    const char path[] = "/tmp/native-partial-sigbus-copy";
    char pages[8192];
    char bytes[8192];
    void *mapped = (void *)-1;
    uint64_t base;
    int fd = -1;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    memset(pages, 'B', sizeof(pages));
    unlink_impl(path);
    fd = (int)syscall_dispatch_impl(__NR_openat, AT_FDCWD, (long)(uintptr_t)path,
                                    O_CREAT | O_RDWR | O_TRUNC, 0600, 0, 0);
    if (fd < 0) {
        errno = -fd;
        return -1;
    }
    if (syscall_dispatch_impl(__NR_write, fd, (long)(uintptr_t)pages, sizeof(pages), 0, 0, 0) !=
        (long)sizeof(pages)) {
        errno = EIO;
        goto out;
    }
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, sizeof(pages),
                                                      PROT_READ, MAP_SHARED, fd, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        goto out;
    }
    base = (uint64_t)(uintptr_t)mapped;
    if (syscall_dispatch_impl(__NR_ftruncate, fd, 4096, 0, 0, 0, 0) != 0) {
        goto out;
    }
    memset(bytes, 0, sizeof(bytes));
    if (task_read_virtual_memory_impl(task, base, bytes, sizeof(bytes)) != 4096 ||
        !signal_is_pending(task, SIGBUS) ||
        task->last_fault_signal != SIGBUS ||
        task->last_fault_code != BUS_ADRERR ||
        task->last_fault_addr != base + 4096 ||
        !latest_signal_info_matches(task, SIGBUS, BUS_ADRERR, base + 4096)) {
        errno = ENODATA;
        goto out;
    }

    result = 0;

out:
    {
        int saved_errno = errno;
        if ((long)(uintptr_t)mapped >= 0) {
            syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, sizeof(pages), 0, 0, 0, 0);
        }
        close_if_open(fd);
        unlink_impl(path);
        clear_pending_task_signal(task, SIGBUS);
        errno = saved_errno;
    }
    return result;
}

int native_syscall_contract_partial_copy_records_fault_address(void) {
    struct task_struct *task = get_current();
    void *mapped;
    char bytes[8192];
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 8192,
                                                      PROT_READ | PROT_WRITE,
                                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        return -1;
    }
    memset(bytes, 'p', sizeof(bytes));
    if (syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped + 4096, 4096, 0, 0, 0, 0) != 0) {
        goto out;
    }
    if (task_write_virtual_memory_impl(task, (uint64_t)(uintptr_t)mapped, bytes, sizeof(bytes)) != 4096 ||
        task->last_fault_signal != SIGSEGV ||
        task->last_fault_code != SEGV_MAPERR ||
        task->last_fault_addr != (uint64_t)(uintptr_t)mapped + 4096 ||
        !latest_signal_info_matches(task, SIGSEGV, SEGV_MAPERR, (uint64_t)(uintptr_t)mapped + 4096)) {
        errno = ENODATA;
        goto out;
    }

    result = 0;

out:
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 4096, 0, 0, 0, 0);
    return result;
}

int native_syscall_contract_proc_pid_maps_and_status_reflect_child_task(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
    void *mapped = (void *)-1;
    char path[64];
    char content[2048];
    int result = -1;

    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    child = task_create_child_impl(parent);
    if (!child) {
        return errno ? errno : ENOMEM;
    }
    set_current(child);
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 4096,
                                                      PROT_READ | PROT_WRITE,
                                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    set_current(parent);
    if ((long)(uintptr_t)mapped < 0) {
        result = -(int)(long)(uintptr_t)mapped;
        goto out;
    }

    if (proc_path_for_pid(path, sizeof(path), child->pid, "/status") != 0 ||
        read_file_into_buffer(path, content, sizeof(content)) != 0 ||
        !strstr(content, "Pid:\t") ||
        !strstr(content, "VmSize:\t")) {
        result = errno ? errno : ENODATA;
        goto out;
    }
    if (proc_path_for_pid(path, sizeof(path), child->pid, "/maps") != 0 ||
        read_file_into_buffer(path, content, sizeof(content)) != 0 ||
        !strstr(content, "[anon]")) {
        result = errno ? errno : ENODATA;
        goto out;
    }
    result = 0;

out:
    set_current(child);
    if ((long)(uintptr_t)mapped >= 0) {
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 4096, 0, 0, 0, 0);
    }
    set_current(parent);
    task_unlink_child_impl(parent, child);
    free_task(child);
    return result;
}

int native_syscall_contract_proc_vm_accounting_reports_mapped_pages(void) {
    void *mapped = (void *)-1;
    char statm[256];
    char status[1024];
    int result = -1;

    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 8192,
                                                      PROT_READ | PROT_WRITE,
                                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        return -1;
    }
    if (read_file_into_buffer("/proc/self/statm", statm, sizeof(statm)) != 0 ||
        statm[0] == '0' ||
        read_file_into_buffer("/proc/self/status", status, sizeof(status)) != 0 ||
        !strstr(status, "VmSize:\t") ||
        !strstr(status, "VmRSS:\t") ||
        !strstr(status, "RssAnon:\t") ||
        strstr(status, "VmSize:\t0 kB")) {
        result = errno ? errno : ENODATA;
        goto out;
    }
    result = 0;

out:
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 8192, 0, 0, 0, 0);
    return result;
}

int native_syscall_contract_proc_status_reports_vm_high_water_fields(void) {
    void *mapped = (void *)-1;
    uint64_t base = 0;
    char status[2048];
    uint64_t vm_peak = 0;
    uint64_t vm_hwm = 0;
    uint64_t vm_size = 0;
    uint64_t vm_rss = 0;
    uint64_t vm_lck = 1;
    uint64_t vm_pin = 1;
    uint64_t vm_swap = 1;
    int result = -1;

    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 8192,
                                                      PROT_READ | PROT_WRITE,
                                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        return -1;
    }
    base = (uint64_t)(uintptr_t)mapped;
    if (task_write_virtual_memory_impl(get_current(), (uint64_t)(uintptr_t)mapped, "H", 1) != 1 ||
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 4096, 0, 0, 0, 0) != 0) {
        goto out;
    }
    mapped = (void *)-1;
    if (read_file_into_buffer("/proc/self/status", status, sizeof(status)) != 0 ||
        status_value_kb(status, "VmPeak:", &vm_peak) != 0 ||
        status_value_kb(status, "VmHWM:", &vm_hwm) != 0 ||
        status_value_kb(status, "VmSize:", &vm_size) != 0 ||
        status_value_kb(status, "VmRSS:", &vm_rss) != 0 ||
        status_value_kb(status, "VmLck:", &vm_lck) != 0 ||
        status_value_kb(status, "VmPin:", &vm_pin) != 0 ||
        status_value_kb(status, "VmSwap:", &vm_swap) != 0 ||
        vm_peak < vm_size ||
        vm_hwm < vm_rss ||
        vm_peak < 8 ||
        vm_hwm < 8 ||
        vm_lck != 0 ||
        vm_pin != 0 ||
        vm_swap != 0) {
        errno = ENODATA;
        goto out;
    }

    result = 0;

out:
    if ((long)(uintptr_t)mapped >= 0) {
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 8192, 0, 0, 0, 0);
    } else if (base != 0) {
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)(base + 4096), 4096, 0, 0, 0, 0);
    }
    return result;
}

int native_syscall_contract_proc_self_smaps_reports_vma_accounting(void) {
    void *mapped = (void *)-1;
    char smaps[4096];
    int result = -1;

    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 8192,
                                                      PROT_READ | PROT_WRITE,
                                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        return -1;
    }

    if (task_write_virtual_memory_impl(get_current(), (uint64_t)(uintptr_t)mapped, "D", 1) != 1) {
        goto out;
    }

    if (read_file_into_buffer("/proc/self/smaps", smaps, sizeof(smaps)) != 0 ||
        !strstr(smaps, "[anon]") ||
        !strstr(smaps, "Size:                  8 kB") ||
        !strstr(smaps, "Rss:                   8 kB") ||
        !strstr(smaps, "Private_Dirty:         4 kB") ||
        !strstr(smaps, "Shared_Dirty:          0 kB")) {
        result = errno ? errno : ENODATA;
        goto out;
    }
    result = 0;

out:
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 8192, 0, 0, 0, 0);
    return result;
}

int native_syscall_contract_proc_self_smaps_dirty_clears_after_madvise(void) {
    void *mapped = (void *)-1;
    char smaps[8192];
    int result = -1;

    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 8192,
                                                      PROT_READ | PROT_WRITE,
                                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        return -1;
    }

    if (task_write_virtual_memory_impl(get_current(), (uint64_t)(uintptr_t)mapped, "D", 1) != 1) {
        goto out;
    }
    if (read_file_into_buffer("/proc/self/smaps", smaps, sizeof(smaps)) != 0 ||
        !strstr(smaps, "Private_Dirty:         4 kB") ||
        !strstr(smaps, "Referenced:            8 kB") ||
        !strstr(smaps, "Anonymous:             8 kB") ||
        !strstr(smaps, "VmFlags:")) {
        result = errno ? errno : ENODATA;
        goto out;
    }

    if (syscall_dispatch_impl(__NR_madvise, (long)(uintptr_t)mapped, 4096, MADV_DONTNEED,
                              0, 0, 0) != 0) {
        goto out;
    }
    if (read_file_into_buffer("/proc/self/smaps", smaps, sizeof(smaps)) != 0 ||
        !strstr(smaps, "Private_Dirty:         0 kB")) {
        result = errno ? errno : ENODATA;
        goto out;
    }

    result = 0;

out:
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 8192, 0, 0, 0, 0);
    return result;
}

int native_syscall_contract_proc_self_smaps_reclaims_dontneed_residency(void) {
    void *mapped = (void *)-1;
    char smaps[8192];
    int result = -1;

    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 8192,
                                                      PROT_READ | PROT_WRITE,
                                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        return -1;
    }

    if (task_write_virtual_memory_impl(get_current(), (uint64_t)(uintptr_t)mapped, "A", 1) != 1 ||
        task_write_virtual_memory_impl(get_current(), (uint64_t)(uintptr_t)mapped + 4096, "B", 1) != 1) {
        goto out;
    }
    if (read_file_into_buffer("/proc/self/smaps", smaps, sizeof(smaps)) != 0 ||
        !strstr(smaps, "Rss:                   8 kB") ||
        !strstr(smaps, "Referenced:            8 kB")) {
        result = errno ? errno : ENODATA;
        goto out;
    }
    if (syscall_dispatch_impl(__NR_madvise, (long)(uintptr_t)mapped, 4096, MADV_DONTNEED,
                              0, 0, 0) != 0) {
        goto out;
    }
    if (read_file_into_buffer("/proc/self/smaps", smaps, sizeof(smaps)) != 0 ||
        !strstr(smaps, "Rss:                   4 kB") ||
        !strstr(smaps, "Referenced:            4 kB")) {
        result = errno ? errno : ENODATA;
        goto out;
    }

    result = 0;

out:
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 8192, 0, 0, 0, 0);
    return result;
}

int native_syscall_contract_mincore_reports_virtual_residency(void) {
    void *mapped = (void *)-1;
    unsigned char vec[2] = {0xff, 0xff};
    int result = -1;

    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 8192,
                                                      PROT_READ | PROT_WRITE,
                                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        return -1;
    }
    if (syscall_dispatch_impl(__NR_mincore, (long)(uintptr_t)mapped, 8192,
                              (long)(uintptr_t)vec, 0, 0, 0) != 0) {
        goto out;
    }
    if ((vec[0] & 1) == 0 || (vec[1] & 1) == 0) {
        errno = ENODATA;
        goto out;
    }
    if (syscall_dispatch_impl(__NR_madvise, (long)(uintptr_t)mapped, 4096, MADV_DONTNEED,
                              0, 0, 0) != 0) {
        goto out;
    }
    vec[0] = 0xff;
    vec[1] = 0xff;
    if (syscall_dispatch_impl(__NR_mincore, (long)(uintptr_t)mapped, 8192,
                              (long)(uintptr_t)vec, 0, 0, 0) != 0) {
        goto out;
    }
    if ((vec[0] & 1) != 0 || (vec[1] & 1) == 0) {
        errno = ENODATA;
        goto out;
    }

    result = 0;

out:
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 8192, 0, 0, 0, 0);
    return result;
}

int native_syscall_contract_read_fault_restores_mincore_residency(void) {
    struct task_struct *task = get_current();
    void *mapped = (void *)-1;
    unsigned char vec[2] = {0xff, 0xff};
    char byte = 0;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 8192,
                                                      PROT_READ | PROT_WRITE,
                                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        return -1;
    }
    if (syscall_dispatch_impl(__NR_madvise, (long)(uintptr_t)mapped, 4096, MADV_DONTNEED,
                              0, 0, 0) != 0) {
        goto out;
    }
    if (syscall_dispatch_impl(__NR_mincore, (long)(uintptr_t)mapped, 8192,
                              (long)(uintptr_t)vec, 0, 0, 0) != 0 ||
        (vec[0] & 1) != 0 ||
        (vec[1] & 1) == 0) {
        errno = ENODATA;
        goto out;
    }
    if (task_read_virtual_memory_impl(task, (uint64_t)(uintptr_t)mapped, &byte, 1) != 1 ||
        byte != 0) {
        errno = EPROTO;
        goto out;
    }
    vec[0] = 0xff;
    vec[1] = 0xff;
    if (syscall_dispatch_impl(__NR_mincore, (long)(uintptr_t)mapped, 8192,
                              (long)(uintptr_t)vec, 0, 0, 0) != 0 ||
        (vec[0] & 1) == 0 ||
        (vec[1] & 1) == 0) {
        errno = ENODATA;
        goto out;
    }

    result = 0;

out:
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 8192, 0, 0, 0, 0);
    return result;
}
