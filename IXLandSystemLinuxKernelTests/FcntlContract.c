#include <linux/fcntl.h>

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "fs/fdtable.h"
#include "kernel/task.h"

extern int open_impl(const char *pathname, int flags, linux_mode_t mode);
extern int dup_impl(int oldfd);
extern int dup2_impl(int oldfd, int newfd);
extern int dup3_impl(int oldfd, int newfd, int flags);
extern int fcntl_impl(int fd, int cmd, ...);
extern long read_impl(int fd, void *buf, size_t count);
extern long pread_impl(int fd, void *buf, size_t count, linux_off_t offset);
extern long write_impl(int fd, const void *buf, size_t count);
extern linux_off_t lseek_impl(int fd, linux_off_t offset, int whence);

#ifndef SEEK_SET
#define SEEK_SET 0
#endif

#ifndef SEEK_CUR
#define SEEK_CUR 1
#endif

#ifndef SEEK_END
#define SEEK_END 2
#endif

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
    linux_off_t offset;

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
    linux_off_t offset;

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
    struct task_struct *parent;
    struct task_struct *child = NULL;
    struct task_struct *saved;
    int parent_fd = -1;
    int child_source = -1;
    int result = -1;
    char byte = 0;

    parent = get_current();
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

    saved = get_current();
    set_current(child);
    child_source = open_impl("/dev/null", O_RDONLY, 0);
    if (child_source < 0) {
        set_current(saved);
        goto out;
    }
    if (dup2_impl(child_source, parent_fd) != parent_fd) {
        set_current(saved);
        goto out;
    }
    set_current(saved);

    if (read_impl(parent_fd, &byte, 1) != 1 || byte != 0) {
        errno = ENODATA;
        goto out;
    }

    result = 0;

out:
    saved = get_current();
    if (child) {
        set_current(child);
        close_if_open(child_source);
        close_if_open(parent_fd);
        set_current(saved);
        task_unlink_child_impl(parent, child);
        free_task(child);
    }
    close_if_open(parent_fd);
    return result;
}
