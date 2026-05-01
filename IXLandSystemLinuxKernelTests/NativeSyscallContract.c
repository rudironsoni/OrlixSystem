#include "NativeSyscallContract.h"

#include <asm/unistd.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/time_types.h>

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>

#include "fs/fdtable.h"
#include "fs/vfs.h"
#include "runtime/native/registry.h"
#include "runtime/syscall.h"

extern int execve(const char *pathname, char *const argv[], char *const envp[]);

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
