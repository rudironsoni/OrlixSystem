#include <uapi/linux/fcntl.h>
#include <uapi/linux/fs.h>
#include <uapi/linux/signal.h>
#include <uapi/linux/errno.h>
#include <uapi/asm/unistd.h>
#include <linux/string.h>

#include <stdbool.h>
#include <stddef.h>

#include "fs/fdtable.h"
#include "kernel/cred.h"
#include "kernel/signal.h"
#include "kernel/task.h"
#include "runtime/syscall.h"

extern int errno;

extern int open_impl(const char *pathname, int flags, uint32_t mode);
extern int dup_impl(int oldfd);
extern int dup2_impl(int oldfd, int newfd);
extern int dup3_impl(int oldfd, int newfd, int flags);
extern int fcntl_impl(int fd, int cmd, ...);
extern long read_impl(int fd, void *buf, size_t count);
extern long pread_impl(int fd, void *buf, size_t count, int64_t offset);
extern long write_impl(int fd, const void *buf, size_t count);
extern int64_t lseek_impl(int fd, int64_t offset, int whence);

static int close_if_open(int fd) {
    if (fd >= 0) {
        return close_impl(fd);
    }
    return 0;
}

static void clear_pending_signal(struct task *task, int32_t sig) {
    if (!task || !task->signal || sig < 1 || sig > KERNEL_SIG_NUM) {
        return;
    }
    task->thread_pending_signals &= ~(1ULL << ((sig - 1) & 63));
    task->signal->pending.sig[(sig - 1) >> 6] &= ~(1ULL << ((sig - 1) & 63));
    task->signal->shared_pending.sig[(sig - 1) >> 6] &= ~(1ULL << ((sig - 1) & 63));
}

static int append_decimal(char *buf, size_t buf_size, int value) {
    char digits[16];
    size_t count = 0;
    size_t i;

    if (value < 0) {
        errno = EINVAL;
        return -1;
    }

    do {
        digits[count++] = (char)('0' + (value % 10));
        value /= 10;
    } while (value > 0 && count < sizeof(digits));

    if (value > 0 || count + 1 > buf_size) {
        errno = ENAMETOOLONG;
        return -1;
    }

    for (i = 0; i < count; i++) {
        buf[i] = digits[count - 1 - i];
    }
    buf[count] = '\0';
    return 0;
}

static int read_fdinfo_flags(int fd_num, unsigned int *flags_out) {
    static const char prefix[] = "/proc/self/fdinfo/";
    char path[64];
    char buf[256];
    int infofd;
    long nread;
    char *flags_line;
    unsigned int flags;
    size_t prefix_len = sizeof(prefix) - 1;

    if (!flags_out) {
        errno = EINVAL;
        return -1;
    }

    memcpy(path, prefix, prefix_len);
    if (append_decimal(path + prefix_len, sizeof(path) - prefix_len, fd_num) != 0) {
        return -1;
    }

    infofd = open_impl(path, O_RDONLY, 0);
    if (infofd < 0) {
        return -1;
    }

    nread = pread_impl(infofd, buf, sizeof(buf) - 1, 0);
    close_impl(infofd);
    if (nread <= 0) {
        if (nread == 0) {
            errno = EIO;
        }
        return -1;
    }

    buf[nread] = '\0';
    flags_line = strstr(buf, "flags:\t0");
    if (!flags_line) {
        errno = EPROTO;
        return -1;
    }

    flags = 0;
    for (flags_line += 8; *flags_line >= '0' && *flags_line <= '7'; flags_line++) {
        flags = (flags << 3) | (unsigned int)(*flags_line - '0');
    }

    *flags_out = flags;
    return 0;
}

int fcntl_contract_dup_returns_lowest_available_fd(void) {
    int fd = -1;
    int guard = -1;
    int dupfd = -1;
    int result = -1;

    fd = open_impl("/dev/null", O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }

    guard = open_impl("/dev/zero", O_RDONLY, 0);
    if (guard < 0) {
        goto out;
    }

    if (close_impl(fd + 1) != 0) {
        goto out;
    }
    guard = -1;

    dupfd = dup_impl(fd);
    if (dupfd != fd + 1) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    close_if_open(dupfd);
    close_if_open(guard);
    close_if_open(fd);
    return result;
}

int fcntl_contract_dup_shares_offset(void) {
    int fd = -1;
    int dupfd = -1;
    int result = -1;
    int64_t offset;

    fd = open_impl("/etc/passwd", O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }

    dupfd = dup_impl(fd);
    if (dupfd < 0) {
        goto out;
    }

    if (lseek_impl(fd, 5, SEEK_SET) != 5) {
        goto out;
    }

    offset = lseek_impl(dupfd, 0, SEEK_CUR);
    if (offset != 5) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    close_if_open(dupfd);
    close_if_open(fd);
    return result;
}

int fcntl_contract_dup_does_not_copy_cloexec(void) {
    int fd = -1;
    int dupfd = -1;
    int result = -1;
    int original_fd_flags;
    int dup_fd_flags;

    fd = open_impl("/etc/passwd", O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }

    if (fcntl_impl(fd, F_SETFD, FD_CLOEXEC) != 0) {
        goto out;
    }

    dupfd = dup_impl(fd);
    if (dupfd < 0) {
        goto out;
    }

    original_fd_flags = fcntl_impl(fd, F_GETFD, 0);
    dup_fd_flags = fcntl_impl(dupfd, F_GETFD, 0);
    if (original_fd_flags != FD_CLOEXEC || dup_fd_flags != 0) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    close_if_open(dupfd);
    close_if_open(fd);
    return result;
}

int fcntl_contract_dup2_same_fd_returns_fd_when_valid(void) {
    int fd = -1;
    int result = -1;
    int dup_result;

    fd = open_impl("/dev/null", O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }

    dup_result = dup2_impl(fd, fd);
    if (dup_result != fd) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    close_if_open(fd);
    return result;
}

int fcntl_contract_dup2_same_fd_invalid_returns_badf(void) {
    if (dup2_impl(-1, -1) != -1) {
        errno = EPROTO;
        return -1;
    }
    if (errno != EBADF) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int fcntl_contract_dup2_replaces_open_fd(void) {
    int source = -1;
    int target = -1;
    int result = -1;
    int dup_result;
    int64_t offset;

    source = open_impl("/etc/passwd", O_RDONLY, 0);
    if (source < 0) {
        return -1;
    }

    target = open_impl("/dev/null", O_RDONLY, 0);
    if (target < 0) {
        goto out;
    }

    if (lseek_impl(source, 7, SEEK_SET) != 7) {
        goto out;
    }

    dup_result = dup2_impl(source, target);
    if (dup_result != target) {
        errno = EPROTO;
        goto out;
    }

    offset = lseek_impl(target, 0, SEEK_CUR);
    if (offset != 7) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    close_if_open(target);
    close_if_open(source);
    return result;
}

int fcntl_contract_dup2_does_not_copy_cloexec(void) {
    int fd = -1;
    int target = -1;
    int result = -1;
    int original_fd_flags;
    int target_fd_flags;

    fd = open_impl("/dev/null", O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }

    target = open_impl("/dev/zero", O_RDONLY, 0);
    if (target < 0) {
        goto out;
    }

    if (fcntl_impl(fd, F_SETFD, FD_CLOEXEC) != 0) {
        goto out;
    }

    if (dup2_impl(fd, target) != target) {
        goto out;
    }

    original_fd_flags = fcntl_impl(fd, F_GETFD, 0);
    target_fd_flags = fcntl_impl(target, F_GETFD, 0);
    if (original_fd_flags != FD_CLOEXEC || target_fd_flags != 0) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    close_if_open(target);
    close_if_open(fd);
    return result;
}

int fcntl_contract_dup3_same_fd_returns_inval(void) {
    int fd = -1;
    int result = -1;

    fd = open_impl("/dev/null", O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }

    if (dup3_impl(fd, fd, 0) != -1) {
        errno = EPROTO;
        goto out;
    }
    if (errno != EINVAL) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    close_if_open(fd);
    return result;
}

int fcntl_contract_dup3_cloexec_sets_close_on_exec(void) {
    int fd = -1;
    int dupfd = -1;
    int result = -1;
    int original_flags;
    int dup_flags;

    fd = open_impl("/dev/null", O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }

    dupfd = dup3_impl(fd, 11, O_CLOEXEC);
    if (dupfd != 11) {
        errno = EPROTO;
        goto out;
    }

    original_flags = fcntl_impl(fd, F_GETFD, 0);
    dup_flags = fcntl_impl(dupfd, F_GETFD, 0);
    if (original_flags != 0 || dup_flags != FD_CLOEXEC) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    close_if_open(dupfd);
    close_if_open(fd);
    return result;
}

int fcntl_contract_f_dupfd_honors_minimum_fd(void) {
    int fd = -1;
    int dupfd = -1;
    int result = -1;

    fd = open_impl("/dev/null", O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }

    dupfd = fcntl_impl(fd, F_DUPFD, 10);
    if (dupfd != 10) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    close_if_open(dupfd);
    close_if_open(fd);
    return result;
}

int fcntl_contract_f_dupfd_rejects_negative_minimum(void) {
    int fd = -1;
    int result = -1;

    fd = open_impl("/dev/null", O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }

    if (fcntl_impl(fd, F_DUPFD, -1) != -1) {
        errno = EPROTO;
        goto out;
    }
    if (errno != EINVAL) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    close_if_open(fd);
    return result;
}

int fcntl_contract_f_dupfd_rejects_out_of_range_minimum(void) {
    int fd = -1;
    int result = -1;

    fd = open_impl("/dev/null", O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }

    if (fcntl_impl(fd, F_DUPFD, NR_OPEN_DEFAULT) != -1) {
        errno = EPROTO;
        goto out;
    }
    if (errno != EINVAL) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    close_if_open(fd);
    return result;
}

int fcntl_contract_f_dupfd_cloexec_sets_close_on_exec(void) {
    int fd = -1;
    int dupfd = -1;
    int result = -1;
    int original_flags;
    int dup_flags;

    fd = open_impl("/etc/passwd", O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }

    dupfd = fcntl_impl(fd, F_DUPFD_CLOEXEC, 10);
    if (dupfd != 10) {
        errno = EPROTO;
        goto out;
    }

    original_flags = fcntl_impl(fd, F_GETFD, 0);
    dup_flags = fcntl_impl(dupfd, F_GETFD, 0);
    if (original_flags != 0 || dup_flags != FD_CLOEXEC) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    close_if_open(dupfd);
    close_if_open(fd);
    return result;
}

int fcntl_contract_setfd_cloexec_is_per_descriptor(void) {
    int fd = -1;
    int dupfd = -1;
    int result = -1;
    int original_fd_flags;
    int dup_fd_flags;

    fd = open_impl("/dev/null", O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }

    dupfd = dup_impl(fd);
    if (dupfd < 0) {
        goto out;
    }

    if (fcntl_impl(dupfd, F_SETFD, FD_CLOEXEC) != 0) {
        goto out;
    }

    original_fd_flags = fcntl_impl(fd, F_GETFD, 0);
    dup_fd_flags = fcntl_impl(dupfd, F_GETFD, 0);
    if (original_fd_flags != 0 || dup_fd_flags != FD_CLOEXEC) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    close_if_open(dupfd);
    close_if_open(fd);
    return result;
}

int fcntl_contract_getfl_does_not_report_fd_cloexec(void) {
    int fd = -1;
    int result = -1;
    int status_flags;

    fd = open_impl("/dev/null", O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }

    if (fcntl_impl(fd, F_SETFD, FD_CLOEXEC) != 0) {
        goto out;
    }

    status_flags = fcntl_impl(fd, F_GETFL, 0);
    if ((status_flags & O_CLOEXEC) != 0) {
        errno = EPROTO;
        goto out;
    }
    if ((status_flags & O_ACCMODE) != O_RDONLY) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    close_if_open(fd);
    return result;
}

int fcntl_contract_setfl_does_not_mutate_access_mode(void) {
    int fd = -1;
    int result = -1;
    int status_flags;

    fd = open_impl("/etc/passwd", O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }

    if (fcntl_impl(fd, F_SETFL, O_WRONLY | O_NONBLOCK) != 0) {
        goto out;
    }

    status_flags = fcntl_impl(fd, F_GETFL, 0);
    if ((status_flags & O_ACCMODE) != O_RDONLY) {
        errno = EPROTO;
        goto out;
    }
    if ((status_flags & O_NONBLOCK) == 0) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    close_if_open(fd);
    return result;
}

int fcntl_contract_setfl_does_not_set_close_on_exec(void) {
    int fd = -1;
    int result = -1;
    int status_flags;
    int fd_flags;

    fd = open_impl("/etc/passwd", O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }

    if (fcntl_impl(fd, F_SETFL, O_CLOEXEC | O_NONBLOCK) != 0) {
        goto out;
    }

    status_flags = fcntl_impl(fd, F_GETFL, 0);
    fd_flags = fcntl_impl(fd, F_GETFD, 0);
    if ((status_flags & O_CLOEXEC) != 0 || fd_flags != 0) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    close_if_open(fd);
    return result;
}

int fcntl_contract_setfl_append_affects_duplicated_fd_status(void) {
    int fd = -1;
    int dupfd = -1;
    int result = -1;
    int original_status;
    int dup_status;

    fd = open_impl("/tmp/fcntl_append_status.txt", O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        return -1;
    }

    dupfd = dup_impl(fd);
    if (dupfd < 0) {
        goto out;
    }

    if (fcntl_impl(fd, F_SETFL, O_APPEND) != 0) {
        goto out;
    }

    original_status = fcntl_impl(fd, F_GETFL, 0);
    dup_status = fcntl_impl(dupfd, F_GETFL, 0);
    if ((original_status & O_APPEND) == 0 || (dup_status & O_APPEND) == 0) {
        errno = EPROTO;
        goto out;
    }
    if ((original_status & O_ACCMODE) != O_RDWR || (dup_status & O_ACCMODE) != O_RDWR) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    close_if_open(dupfd);
    close_if_open(fd);
    return result;
}

int fcntl_contract_proc_self_fdinfo_reflects_close_on_exec_per_descriptor(void) {
    int fd = -1;
    int dupfd = -1;
    int result = -1;
    unsigned int original_info_flags;
    unsigned int dup_info_flags;

    fd = open_impl("/etc/passwd", O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }

    dupfd = dup3_impl(fd, 11, O_CLOEXEC);
    if (dupfd != 11) {
        errno = EPROTO;
        goto out;
    }

    if (read_fdinfo_flags(fd, &original_info_flags) != 0) {
        goto out;
    }

    if (read_fdinfo_flags(dupfd, &dup_info_flags) != 0) {
        goto out;
    }

    if ((original_info_flags & O_CLOEXEC) != 0) {
        errno = ERANGE;
        goto out;
    }
    if ((dup_info_flags & O_CLOEXEC) == 0) {
        errno = ENODATA;
        goto out;
    }

    result = 0;

out:
    close_if_open(dupfd);
    close_if_open(fd);
    return result;
}

int fcntl_contract_proc_self_fdinfo_reflects_nonblock_after_setfl(void) {
    int fd = -1;
    int dupfd = -1;
    int result = -1;
    unsigned int original_info_flags;
    unsigned int dup_info_flags;

    fd = open_impl("/dev/null", O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }

    dupfd = fcntl_impl(fd, F_DUPFD_CLOEXEC, 10);
    if (dupfd != 10) {
        errno = EPROTO;
        goto out;
    }

    if (fcntl_impl(fd, F_SETFL, O_NONBLOCK) != 0) {
        goto out;
    }

    if (read_fdinfo_flags(fd, &original_info_flags) != 0) {
        goto out;
    }

    if (read_fdinfo_flags(dupfd, &dup_info_flags) != 0) {
        goto out;
    }

    if ((original_info_flags & O_NONBLOCK) == 0 || (dup_info_flags & O_NONBLOCK) == 0) {
        errno = ENODATA;
        goto out;
    }
    if ((original_info_flags & O_CLOEXEC) != 0) {
        errno = ERANGE;
        goto out;
    }
    if ((dup_info_flags & O_CLOEXEC) == 0) {
        errno = ENOMSG;
        goto out;
    }

    result = 0;

out:
    close_if_open(dupfd);
    close_if_open(fd);
    return result;
}

int fcntl_contract_child_dup2_does_not_replace_parent_descriptor(void) {
    struct task *parent;
    struct task *child = NULL;
    struct task *saved;
    int parent_fd = -1;
    int child_source = -1;
    int result = -1;
    char byte = 0;

    parent = task_current();
    if (!parent) {
        errno = ESRCH;
        return -1;
    }

    parent_fd = open_impl("/dev/zero", O_RDONLY, 0);
    if (parent_fd < 0) {
        return -1;
    }

    child = task_create_child_impl(parent);
    if (!child) {
        goto out;
    }

    saved = task_current();
    task_set_current(child);
    child_source = open_impl("/dev/null", O_RDONLY, 0);
    if (child_source < 0) {
        task_set_current(saved);
        goto out;
    }
    if (dup2_impl(child_source, parent_fd) != parent_fd) {
        task_set_current(saved);
        goto out;
    }
    task_set_current(saved);

    if (read_impl(parent_fd, &byte, 1) != 1 || byte != 0) {
        errno = ENODATA;
        goto out;
    }

    result = 0;

out:
    saved = task_current();
    if (child) {
        task_set_current(child);
        close_if_open(child_source);
        close_if_open(parent_fd);
        task_set_current(saved);
        task_unlink_child_impl(parent, child);
        task_put(child);
    }
    close_if_open(parent_fd);
    return result;
}

int fcntl_contract_pidfd_getfd_duplicates_target_descriptor(void) {
    struct task *parent;
    struct task *child = NULL;
    struct task *saved;
    int child_fd = -1;
    int pidfd = -1;
    int dupfd = -1;
    int result = -1;
    int dup_fd_flags;
    int64_t offset;

    parent = task_current();
    if (!parent) {
        errno = ESRCH;
        return -1;
    }

    child = task_create_child_impl(parent);
    if (!child) {
        return -1;
    }

    saved = task_current();
    task_set_current(child);
    child_fd = open_impl("/etc/passwd", O_RDONLY, 0);
    if (child_fd < 0) {
        task_set_current(saved);
        goto out;
    }
    if (lseek_impl(child_fd, 9, SEEK_SET) != 9) {
        task_set_current(saved);
        goto out;
    }
    task_set_current(saved);

    pidfd = (int)syscall_dispatch_impl(__NR_pidfd_open, child->pid, 0, 0, 0, 0, 0);
    if (pidfd < 0) {
        errno = -pidfd;
        goto out;
    }

    dupfd = (int)syscall_dispatch_impl(__NR_pidfd_getfd, pidfd, child_fd, 0, 0, 0, 0);
    if (dupfd < 0) {
        errno = -dupfd;
        goto out;
    }

    dup_fd_flags = fcntl_impl(dupfd, F_GETFD, 0);
    if (dup_fd_flags != FD_CLOEXEC) {
        errno = EPROTO;
        goto out;
    }

    saved = task_current();
    task_set_current(child);
    if (lseek_impl(child_fd, 17, SEEK_SET) != 17) {
        task_set_current(saved);
        goto out;
    }
    task_set_current(saved);

    offset = lseek_impl(dupfd, 0, SEEK_CUR);
    if (offset != 17) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    task_set_current(parent);
    close_if_open(dupfd);
    close_if_open(pidfd);
    if (child) {
        task_set_current(child);
        close_if_open(child_fd);
        task_set_current(parent);
        task_unlink_child_impl(parent, child);
        task_put(child);
    }
    task_set_current(parent);
    return result;
}

int fcntl_contract_pidfd_getfd_rejects_permission_mismatch(void) {
    struct task *parent;
    struct task *child = NULL;
    struct task *caller = NULL;
    struct task *saved;
    int child_fd = -1;
    int pidfd = -1;
    int result = -1;
    long ret;

    parent = task_current();
    if (!parent) {
        errno = ESRCH;
        return -1;
    }

    child = task_create_child_impl(parent);
    if (!child) {
        return -1;
    }
    caller = task_create_child_impl(parent);
    if (!caller) {
        goto out;
    }

    saved = task_current();
    task_set_current(child);
    child_fd = open_impl("/etc/passwd", O_RDONLY, 0);
    if (child_fd < 0 || setresuid_impl(1001, 1001, 1001) != 0) {
        task_set_current(saved);
        goto out;
    }
    task_set_current(caller);
    if (setresuid_impl(1000, 1000, 1000) != 0) {
        task_set_current(saved);
        goto out;
    }
    task_set_current(saved);

    task_set_current(caller);
    pidfd = (int)syscall_dispatch_impl(__NR_pidfd_open, child->pid, 0, 0, 0, 0, 0);
    if (pidfd < 0) {
        task_set_current(saved);
        errno = -pidfd;
        goto out;
    }

    ret = syscall_dispatch_impl(__NR_pidfd_getfd, pidfd, child_fd, 0, 0, 0, 0);
    task_set_current(saved);
    if (ret != -EPERM) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    result = 0;

out:
    task_set_current(parent);
    if (caller) {
        task_set_current(caller);
        close_if_open(pidfd);
        task_set_current(parent);
        task_unlink_child_impl(parent, caller);
        task_put(caller);
    } else {
        close_if_open(pidfd);
    }
    if (child) {
        task_set_current(child);
        close_if_open(child_fd);
        task_set_current(parent);
        task_unlink_child_impl(parent, child);
        task_put(child);
    }
    task_set_current(parent);
    return result;
}

int fcntl_contract_pidfd_getfd_allows_ptrace_eligible_sibling(void) {
    struct task *parent;
    struct task *target = NULL;
    struct task *caller = NULL;
    struct task *saved;
    int target_fd = -1;
    int pidfd = -1;
    int dupfd = -1;
    int result = -1;
    int64_t offset;

    parent = task_current();
    if (!parent) {
        errno = ESRCH;
        return -1;
    }

    target = task_create_child_impl(parent);
    if (!target) {
        return -1;
    }
    caller = task_create_child_impl(parent);
    if (!caller) {
        goto out;
    }

    saved = task_current();
    task_set_current(target);
    if (setresgid_impl(1000, 1000, 1000) != 0 ||
        setresuid_impl(1000, 1000, 1000) != 0) {
        task_set_current(saved);
        goto out;
    }
    target_fd = open_impl("/etc/passwd", O_RDONLY, 0);
    if (target_fd < 0 || lseek_impl(target_fd, 13, SEEK_SET) != 13) {
        task_set_current(saved);
        goto out;
    }
    task_set_current(caller);
    if (setresgid_impl(2000, 1000, 2000) != 0 ||
        setresuid_impl(2000, 1000, 2000) != 0) {
        task_set_current(saved);
        goto out;
    }
    task_set_current(saved);

    task_set_current(caller);
    pidfd = (int)syscall_dispatch_impl(__NR_pidfd_open, target->pid, 0, 0, 0, 0, 0);
    if (pidfd < 0) {
        task_set_current(saved);
        errno = -pidfd;
        goto out;
    }
    dupfd = (int)syscall_dispatch_impl(__NR_pidfd_getfd, pidfd, target_fd, 0, 0, 0, 0);
    task_set_current(saved);
    if (dupfd < 0) {
        errno = -dupfd;
        goto out;
    }

    saved = task_current();
    task_set_current(target);
    if (lseek_impl(target_fd, 21, SEEK_SET) != 21) {
        task_set_current(saved);
        goto out;
    }
    task_set_current(saved);

    task_set_current(caller);
    offset = lseek_impl(dupfd, 0, SEEK_CUR);
    task_set_current(saved);
    if (offset != 21) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    task_set_current(parent);
    if (caller) {
        task_set_current(caller);
        close_if_open(dupfd);
        close_if_open(pidfd);
        task_set_current(parent);
        task_unlink_child_impl(parent, caller);
        task_put(caller);
    } else {
        close_if_open(dupfd);
        close_if_open(pidfd);
    }
    if (target) {
        task_set_current(target);
        close_if_open(target_fd);
        task_set_current(parent);
        task_unlink_child_impl(parent, target);
        task_put(target);
    }
    task_set_current(parent);
    return result;
}

int fcntl_contract_pidfd_getfd_rejects_bad_targets(void) {
    struct task *parent;
    struct task *child = NULL;
    struct task *saved;
    int child_fd = -1;
    int pidfd = -1;
    int nullfd = -1;
    int result = -1;
    long ret;

    parent = task_current();
    if (!parent) {
        errno = ESRCH;
        return -1;
    }

    child = task_create_child_impl(parent);
    if (!child) {
        return -1;
    }

    saved = task_current();
    task_set_current(child);
    child_fd = open_impl("/etc/passwd", O_RDONLY, 0);
    task_set_current(saved);
    if (child_fd < 0) {
        goto out;
    }

    pidfd = (int)syscall_dispatch_impl(__NR_pidfd_open, child->pid, 0, 0, 0, 0, 0);
    if (pidfd < 0) {
        errno = -pidfd;
        goto out;
    }

    ret = syscall_dispatch_impl(__NR_pidfd_getfd, -1, child_fd, 0, 0, 0, 0);
    if (ret != -EBADF) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    nullfd = open_impl("/dev/null", O_RDONLY, 0);
    if (nullfd < 0) {
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_pidfd_getfd, nullfd, child_fd, 0, 0, 0, 0);
    if (ret != -EBADF) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    ret = syscall_dispatch_impl(__NR_pidfd_getfd, pidfd, child_fd + 1, 0, 0, 0, 0);
    if (ret != -EBADF) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    saved = task_current();
    task_set_current(child);
    ret = syscall_dispatch_impl(__NR_exit, 41, 0, 0, 0, 0, 0);
    task_set_current(saved);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    ret = syscall_dispatch_impl(__NR_pidfd_getfd, pidfd, child_fd, 0, 0, 0, 0);
    if (ret != -ESRCH) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    clear_pending_signal(parent, SIGCHLD);

    result = 0;

out:
    task_set_current(parent);
    close_if_open(nullfd);
    close_if_open(pidfd);
    clear_pending_signal(parent, SIGCHLD);
    if (child) {
        task_set_current(child);
        close_if_open(child_fd);
        task_set_current(parent);
        task_unlink_child_impl(parent, child);
        task_put(child);
    }
    task_set_current(parent);
    return result;
}

int fcntl_contract_pidfd_getfd_rejects_nonzero_flags(void) {
    struct task *parent;
    struct task *child = NULL;
    struct task *saved;
    int child_fd = -1;
    int pidfd = -1;
    int result = -1;
    long ret;

    parent = task_current();
    if (!parent) {
        errno = ESRCH;
        return -1;
    }

    child = task_create_child_impl(parent);
    if (!child) {
        return -1;
    }

    saved = task_current();
    task_set_current(child);
    child_fd = open_impl("/etc/passwd", O_RDONLY, 0);
    task_set_current(saved);
    if (child_fd < 0) {
        goto out;
    }

    pidfd = (int)syscall_dispatch_impl(__NR_pidfd_open, child->pid, 0, 0, 0, 0, 0);
    if (pidfd < 0) {
        errno = -pidfd;
        goto out;
    }

    ret = syscall_dispatch_impl(__NR_pidfd_getfd, pidfd, child_fd, 1, 0, 0, 0);
    if (ret != -EINVAL) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    result = 0;

out:
    task_set_current(parent);
    close_if_open(pidfd);
    if (child) {
        task_set_current(child);
        close_if_open(child_fd);
        task_set_current(parent);
        task_unlink_child_impl(parent, child);
        task_put(child);
    }
    task_set_current(parent);
    return result;
}

int fcntl_contract_pidfd_getfd_rejects_negative_targetfd(void) {
    struct task *parent;
    struct task *child = NULL;
    int pidfd = -1;
    int result = -1;
    long ret;

    parent = task_current();
    if (!parent) {
        errno = ESRCH;
        return -1;
    }

    child = task_create_child_impl(parent);
    if (!child) {
        return -1;
    }

    pidfd = (int)syscall_dispatch_impl(__NR_pidfd_open, child->pid, 0, 0, 0, 0, 0);
    if (pidfd < 0) {
        errno = -pidfd;
        goto out;
    }

    ret = syscall_dispatch_impl(__NR_pidfd_getfd, pidfd, -1, 0, 0, 0, 0);
    if (ret != -EBADF) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    result = 0;

out:
    task_set_current(parent);
    close_if_open(pidfd);
    if (child) {
        task_unlink_child_impl(parent, child);
        task_put(child);
    }
    task_set_current(parent);
    return result;
}

void fcntl_contract_reset_pidfd_test_state(void) {
    struct task *child;

    if (!task_init_process) {
        return;
    }

    task_set_current(task_init_process);
    clear_pending_signal(task_init_process, SIGCHLD);

    while ((child = task_init_process->children) != NULL) {
        task_unlink_child_impl(task_init_process, child);
        task_put(child);
    }

    task_set_current(task_init_process);
    clear_pending_signal(task_init_process, SIGCHLD);
}
