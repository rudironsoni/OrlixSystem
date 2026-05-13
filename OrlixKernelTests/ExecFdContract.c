#include <uapi/linux/fcntl.h>
#include <uapi/linux/fs.h>
#include <uapi/linux/errno.h>
#include <linux/dirent.h>
#include <linux/string.h>

#include <stddef.h>
#include <stdint.h>

#include "fs/fdtable.h"
#include "private/fs/fdtable_state.h"
#include "fs/vfs.h"
#include "kernel/task.h"
#include "private/kernel/task_state.h"

extern int errno;

extern int open_impl(const char *pathname, int flags, uint32_t mode);
extern int close_impl(int fd);
extern int dup_impl(int oldfd);
extern int dup3_impl(int oldfd, int newfd, int flags);
extern int fcntl_impl(int fd, int cmd, ...);
extern long read_impl(int fd, void *buf, size_t count);
extern int64_t lseek_impl(int fd, int64_t offset, int whence);
extern ssize_t getdents64(int fd, void *dirp, size_t count);

static int close_if_open(int fd) {
    if (fd >= 0) {
        return close_impl(fd);
    }
    return 0;
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

int exec_fd_contract_close_on_exec_closes_only_cloexec_descriptor(void) {
    int cloexec_fd = -1;
    int keep_fd = -1;
    int closed;
    int result = -1;

    cloexec_fd = open_impl("/dev/null", O_RDONLY | O_CLOEXEC, 0);
    if (cloexec_fd < 0) {
        return -1;
    }

    keep_fd = open_impl("/dev/zero", O_RDONLY, 0);
    if (keep_fd < 0) {
        goto out;
    }

    closed = close_on_exec_impl();
    if (closed != 1) {
        errno = EPROTO;
        goto out;
    }
    if (fdtable_is_used_impl(cloexec_fd) || !fdtable_is_used_impl(keep_fd)) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    close_if_open(keep_fd);
    close_if_open(cloexec_fd);
    return result;
}

int exec_fd_contract_close_on_exec_preserves_descriptor_without_cloexec(void) {
    int fd = -1;
    int closed;
    int result = -1;

    fd = open_impl("/dev/null", O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }

    closed = close_on_exec_impl();
    if (closed != 0) {
        errno = EPROTO;
        goto out;
    }
    if (!fdtable_is_used_impl(fd)) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    close_if_open(fd);
    return result;
}

int exec_fd_contract_close_on_exec_does_not_close_shared_description_still_referenced(void) {
    int fd = -1;
    int dupfd = -1;
    int closed;
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

    if (fcntl_impl(dupfd, F_SETFD, FD_CLOEXEC) != 0) {
        goto out;
    }
    if (lseek_impl(fd, 7, SEEK_SET) != 7) {
        goto out;
    }

    closed = close_on_exec_impl();
    if (closed != 1) {
        errno = EPROTO;
        goto out;
    }
    if (!fdtable_is_used_impl(fd) || fdtable_is_used_impl(dupfd)) {
        errno = EPROTO;
        goto out;
    }

    offset = lseek_impl(fd, 0, SEEK_CUR);
    if (offset != 7) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    close_if_open(dupfd);
    close_if_open(fd);
    return result;
}

int exec_fd_contract_close_on_exec_closes_duplicated_descriptor_with_own_cloexec_flag(void) {
    int fd = -1;
    int dupfd = -1;
    int closed;
    int result = -1;
    int source_flags;

    fd = open_impl("/dev/null", O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }

    dupfd = dup3_impl(fd, fd + 1, O_CLOEXEC);
    if (dupfd < 0) {
        goto out;
    }

    closed = close_on_exec_impl();
    if (closed != 1) {
        errno = EPROTO;
        goto out;
    }
    if (!fdtable_is_used_impl(fd) || fdtable_is_used_impl(dupfd)) {
        errno = EPROTO;
        goto out;
    }

    source_flags = fcntl_impl(fd, F_GETFD, 0);
    if (source_flags != 0) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    close_if_open(dupfd);
    close_if_open(fd);
    return result;
}

int exec_fd_contract_close_on_exec_preserves_source_descriptor_when_duplicate_is_cloexec(void) {
    int fd = -1;
    int dupfd = -1;
    int closed;
    int result = -1;
    long nread;
    char byte;

    fd = open_impl("/dev/zero", O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }

    dupfd = dup3_impl(fd, fd + 1, O_CLOEXEC);
    if (dupfd < 0) {
        goto out;
    }

    closed = close_on_exec_impl();
    if (closed != 1) {
        errno = EPROTO;
        goto out;
    }

    nread = read_impl(fd, &byte, 1);
    if (nread != 1 || byte != 0) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    close_if_open(dupfd);
    close_if_open(fd);
    return result;
}

int exec_fd_contract_close_on_exec_removes_closed_fd_from_proc_self_fd(void) {
    static const char prefix[] = "/proc/self/fd/";
    int fd = -1;
    int closed;
    int result = -1;
    char path[64];
    char target[64];
    size_t prefix_len = sizeof(prefix) - 1;

    fd = open_impl("/dev/null", O_RDONLY | O_CLOEXEC, 0);
    if (fd < 0) {
        return -1;
    }

    memcpy(path, prefix, prefix_len);
    if (append_decimal(path + prefix_len, sizeof(path) - prefix_len, fd) != 0) {
        goto out;
    }

    closed = close_on_exec_impl();
    if (closed != 1) {
        errno = EPROTO;
        goto out;
    }
    if (vfs_proc_self_fd_link_target(path, target, sizeof(target)) != -ENOENT) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    close_if_open(fd);
    return result;
}

int exec_fd_contract_close_on_exec_removes_closed_fd_from_proc_self_fdinfo(void) {
    int fd = -1;
    int closed;

    fd = open_impl("/dev/null", O_RDONLY | O_CLOEXEC, 0);
    if (fd < 0) {
        return -1;
    }

    closed = close_on_exec_impl();
    if (closed != 1) {
        errno = EPROTO;
        return -1;
    }
    if (vfs_proc_self_fdinfo_content(fd, (char[64]){0}, 64) != -ENOENT) {
        errno = EPROTO;
        return -1;
    }

    return 0;
}

int exec_fd_contract_close_on_exec_preserves_non_cloexec_fd_in_proc_self_fd(void) {
    static const char prefix[] = "/proc/self/fd/";
    int fd = -1;
    int result = -1;
    char path[64];
    char target[64];
    size_t prefix_len = sizeof(prefix) - 1;

    fd = open_impl("/dev/null", O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }

    memcpy(path, prefix, prefix_len);
    if (append_decimal(path + prefix_len, sizeof(path) - prefix_len, fd) != 0) {
        goto out;
    }

    if (close_on_exec_impl() != 0) {
        errno = EPROTO;
        goto out;
    }
    if (vfs_proc_self_fd_link_target(path, target, sizeof(target)) != 0) {
        errno = EPROTO;
        goto out;
    }
    if (strcmp(target, "/dev/null") != 0) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    close_if_open(fd);
    return result;
}

int exec_fd_contract_close_on_exec_works_for_synthetic_dev_fd(void) {
    int fd = -1;

    fd = open_impl("/dev/null", O_RDONLY | O_CLOEXEC, 0);
    if (fd < 0) {
        return -1;
    }
    if (close_on_exec_impl() != 1) {
        errno = EPROTO;
        return -1;
    }
    if (fdtable_is_used_impl(fd)) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int exec_fd_contract_close_on_exec_works_for_synthetic_proc_directory_fd(void) {
    int fd = -1;
    int closed;
    int fd_flags;

    fd = alloc_fd_impl();
    if (fd < 0) {
        return -1;
    }
    init_synthetic_subdir_fd_entry_impl(fd, O_RDONLY, 0, "/proc/self/fd", SYNTHETIC_DIR_PROC_SELF_FD);
    if (!fdtable_is_used_impl(fd)) {
        errno = EPROTO;
        return -1;
    }
    if (fcntl_impl(fd, F_SETFD, FD_CLOEXEC) != 0) {
        return -1;
    }
    fd_flags = fcntl_impl(fd, F_GETFD, 0);
    if (fd_flags != FD_CLOEXEC) {
        errno = EPROTO;
        return -1;
    }
    closed = close_on_exec_impl();
    if (closed != 1) {
        errno = EPROTO;
        return -1;
    }
    if (fdtable_is_used_impl(fd)) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int exec_fd_contract_close_on_exec_works_for_synthetic_proc_file_fd(void) {
    static const char prefix[] = "/proc/self/fdinfo/";
    int target_fd = -1;
    int infofd = -1;
    int result = -1;
    char path[64];
    size_t prefix_len = sizeof(prefix) - 1;

    target_fd = open_impl("/dev/null", O_RDONLY, 0);
    if (target_fd < 0) {
        return -1;
    }

    memcpy(path, prefix, prefix_len);
    if (append_decimal(path + prefix_len, sizeof(path) - prefix_len, target_fd) != 0) {
        goto out;
    }

    infofd = open_impl(path, O_RDONLY | O_CLOEXEC, 0);
    if (infofd < 0) {
        goto out;
    }

    if (close_on_exec_impl() != 1) {
        errno = EPROTO;
        goto out;
    }
    if (fdtable_is_used_impl(infofd) || !fdtable_is_used_impl(target_fd)) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    close_if_open(infofd);
    close_if_open(target_fd);
    return result;
}

int exec_fd_contract_close_on_exec_is_idempotent_when_no_cloexec_fds_remain(void) {
    int fd = -1;

    fd = open_impl("/dev/null", O_RDONLY | O_CLOEXEC, 0);
    if (fd < 0) {
        return -1;
    }
    if (close_on_exec_impl() != 1) {
        errno = EPROTO;
        return -1;
    }
    if (close_on_exec_impl() != 0) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int exec_fd_contract_close_on_exec_keeps_fd_allocation_deterministic(void) {
    int fd = -1;
    int next_fd = -1;
    int result = -1;

    fd = open_impl("/dev/null", O_RDONLY | O_CLOEXEC, 0);
    if (fd < 0) {
        return -1;
    }
    if (close_on_exec_impl() != 1) {
        errno = EPROTO;
        goto out;
    }

    next_fd = open_impl("/dev/zero", O_RDONLY, 0);
    if (next_fd != fd) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    close_if_open(next_fd);
    close_if_open(fd);
    return result;
}

int exec_fd_contract_close_on_exec_does_not_mutate_status_flags_on_survivors(void) {
    int fd = -1;
    int dupfd = -1;
    int result = -1;
    unsigned int flags_before;
    unsigned int flags_after;

    fd = open_impl("/dev/null", O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }

    dupfd = dup3_impl(fd, fd + 1, O_CLOEXEC);
    if (dupfd < 0) {
        goto out;
    }

    if (fcntl_impl(fd, F_SETFL, O_NONBLOCK) != 0) {
        goto out;
    }
    flags_before = (unsigned int)fcntl_impl(fd, F_GETFL, 0);
    if ((int)flags_before < 0) {
        goto out;
    }
    if (close_on_exec_impl() != 1) {
        errno = EPROTO;
        goto out;
    }
    flags_after = (unsigned int)fcntl_impl(fd, F_GETFL, 0);
    if ((int)flags_after < 0) {
        goto out;
    }
    if (flags_before != flags_after) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    close_if_open(dupfd);
    close_if_open(fd);
    return result;
}

int exec_fd_contract_child_close_on_exec_does_not_close_parent_descriptor(void) {
    struct task *parent = task_current();
    struct task *child = NULL;
    struct task *saved = parent;
    int fd = -1;
    int child_closed = -1;
    int result = -1;
    long nread;
    char byte = 1;

    if (!parent) {
        errno = ESRCH;
        return -1;
    }

    fd = open_impl("/dev/zero", O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }
    if (fcntl_impl(fd, F_SETFD, FD_CLOEXEC) != 0) {
        goto out;
    }

    child = task_create_child_impl(parent);
    if (!child) {
        errno = ENOMEM;
        goto out;
    }

    task_set_current(child);
    child_closed = close_on_exec_impl();
    task_set_current(saved);

    if (child_closed != 1) {
        errno = EPROTO;
        goto out;
    }

    nread = read_impl(fd, &byte, 1);
    if (nread != 1 || byte != 0) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    if (child) {
        task_set_current(saved);
        task_unlink_child_impl(parent, child);
        task_put(child);
    }
    task_set_current(saved);
    close_if_open(fd);
    return result;
}
