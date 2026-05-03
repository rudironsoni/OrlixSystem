#include <linux/fcntl.h>

#include <errno.h>
#include <stdarg.h>

#include "sync.h"
#include "fdtable.h"

static int fcntl_get_entry_or_badf(int fd, void **entry_out) {
    void *entry;

    if (fd < 0 || fd >= NR_OPEN_DEFAULT) {
        errno = EBADF;
        return -1;
    }

    entry = get_fd_entry_impl(fd);
    if (!entry) {
        errno = EBADF;
        return -1;
    }

    *entry_out = entry;
    return 0;
}

static int fcntl_mutable_status_mask(void) {
    return O_APPEND | O_NONBLOCK | O_SYNC;
}

int dup_impl(int oldfd) {
    return clone_fd_entry_impl(oldfd, 0, false);
}

int dup2_impl(int oldfd, int newfd) {
    if (oldfd < 0 || oldfd >= NR_OPEN_DEFAULT || newfd < 0 || newfd >= NR_OPEN_DEFAULT) {
        errno = EBADF;
        return -1;
    }

    if (oldfd == newfd) {
        void *entry;
        if (fcntl_get_entry_or_badf(oldfd, &entry) != 0) {
            return -1;
        }
        put_fd_entry_impl(entry);
        return newfd;
    }

    return replace_fd_entry_impl(newfd, oldfd, false);
}

int dup3_impl(int oldfd, int newfd, int flags) {
    if (oldfd == newfd) {
        errno = EINVAL;
        return -1;
    }

    if (flags & ~O_CLOEXEC) {
        errno = EINVAL;
        return -1;
    }

    return replace_fd_entry_impl(newfd, oldfd, (flags & O_CLOEXEC) != 0);
}

int fcntl_impl(int fd, int cmd, ...) {
    va_list args;
    int arg = 0;
    void *entry;
    int result = -1;

    va_start(args, cmd);
    arg = va_arg(args, int);
    va_end(args);

    switch (cmd) {
    case F_DUPFD:
        return clone_fd_entry_impl(fd, arg, false);
    case F_DUPFD_CLOEXEC:
        return clone_fd_entry_impl(fd, arg, true);
    case F_GETFD:
        if (fcntl_get_entry_or_badf(fd, &entry) != 0) {
            return -1;
        }
        result = (get_fd_descriptor_flags_impl(entry) & FD_CLOEXEC) ? FD_CLOEXEC : 0;
        put_fd_entry_impl(entry);
        return result;
    case F_SETFD:
        if (fcntl_get_entry_or_badf(fd, &entry) != 0) {
            return -1;
        }
        set_fd_descriptor_flags_impl(entry, (arg & FD_CLOEXEC) ? FD_CLOEXEC : 0);
        put_fd_entry_impl(entry);
        fdtable_sync_current_task_fd_impl(fd);
        return 0;
    case F_GETFL:
        if (fcntl_get_entry_or_badf(fd, &entry) != 0) {
            return -1;
        }
        result = get_fd_flags_impl(entry);
        put_fd_entry_impl(entry);
        return result;
    case F_SETFL: {
        int mutable_mask = fcntl_mutable_status_mask();
        int current_flags;
        int new_flags;

        if (fcntl_get_entry_or_badf(fd, &entry) != 0) {
            return -1;
        }
        current_flags = get_fd_flags_impl(entry);
        new_flags = (current_flags & ~mutable_mask) | (arg & mutable_mask);
        set_fd_flags_impl(entry, new_flags);
        put_fd_entry_impl(entry);
        fdtable_sync_current_task_fd_impl(fd);
        return 0;
    }
    default:
        errno = EINVAL;
        return -1;
    }
}

__attribute__((visibility("default"))) int dup(int oldfd) {
    return dup_impl(oldfd);
}

__attribute__((visibility("default"))) int dup2(int oldfd, int newfd) {
    return dup2_impl(oldfd, newfd);
}

__attribute__((visibility("default"))) int dup3(int oldfd, int newfd, int flags) {
    return dup3_impl(oldfd, newfd, flags);
}

__attribute__((visibility("default"))) int fcntl(int fd, int cmd, ...) {
    va_list args;
    int arg = 0;

    va_start(args, cmd);
    arg = va_arg(args, int);
    va_end(args);

    return fcntl_impl(fd, cmd, arg);
}
