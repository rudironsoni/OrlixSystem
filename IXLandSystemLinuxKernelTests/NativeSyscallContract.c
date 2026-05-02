#include "NativeSyscallContract.h"

#include <asm/unistd.h>
#include <asm/ioctls.h>
#include <linux/fcntl.h>
#include <linux/futex.h>
#include <linux/mman.h>
#include <linux/stat.h>
#include <linux/time_types.h>

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>

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
#include "kernel/task.h"

extern int link_impl(const char *oldpath, const char *newpath);
extern int unlink_impl(const char *pathname);
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

int native_syscall_contract_dispatches_process_startup_syscalls(void) {
    struct task_struct *task = get_current();
    uint64_t block_set = 1ULL << (2 - 1);
    uint64_t old_set = 0;
    uint64_t old_limit[2];
    uint64_t new_limit[2];
    int tid_word = 0;
    long current_brk;
    long grown_brk;
    long second_brk;
    long shrunk_brk;
    long ret;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

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
