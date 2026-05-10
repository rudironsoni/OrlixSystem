#include <uapi/linux/fcntl.h>

#include <linux/errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <linux/string.h>

#include "internal/fs/lock.h"
#include "fdtable.h"

static int fcntl_get_entry_or_badf(int fd, void **entry_out) {
    void *entry;

    if (fd < 0 || fd >= NR_OPEN_DEFAULT) {
        return -EBADF;
    }

    entry = get_fd_entry_impl(fd);
    if (!entry) {
        return -EBADF;
    }

    *entry_out = entry;
    return 0;
}

static int fcntl_mutable_status_mask(void) {
    return O_APPEND | O_NONBLOCK | O_SYNC;
}

#define FLOCK_TABLE_MAX 256

struct flock_entry {
    bool active;
    uint64_t identity;
    int exclusive_fd;
    int shared_fds[NR_OPEN_DEFAULT];
};

static struct flock_entry flock_table[FLOCK_TABLE_MAX];
static fs_mutex_t flock_table_lock = FS_MUTEX_INITIALIZER;

static struct flock_entry *flock_entry_for_identity_locked(uint64_t identity, bool create) {
    struct flock_entry *free_entry = NULL;

    for (size_t i = 0; i < FLOCK_TABLE_MAX; i++) {
        if (flock_table[i].active && flock_table[i].identity == identity) {
            return &flock_table[i];
        }
        if (!flock_table[i].active && !free_entry) {
            free_entry = &flock_table[i];
        }
    }
    if (!create || !free_entry) {
        return NULL;
    }
    memset(free_entry, 0, sizeof(*free_entry));
    free_entry->active = true;
    free_entry->identity = identity;
    free_entry->exclusive_fd = -1;
    for (size_t i = 0; i < NR_OPEN_DEFAULT; i++) {
        free_entry->shared_fds[i] = -1;
    }
    return free_entry;
}

static bool flock_has_other_shared_locked(const struct flock_entry *entry, int fd) {
    if (!entry) {
        return false;
    }
    for (size_t i = 0; i < NR_OPEN_DEFAULT; i++) {
        if (entry->shared_fds[i] >= 0 && entry->shared_fds[i] != fd) {
            return true;
        }
    }
    return false;
}

static void flock_remove_fd_locked(struct flock_entry *entry, int fd) {
    bool has_shared = false;

    if (!entry) {
        return;
    }
    if (entry->exclusive_fd == fd) {
        entry->exclusive_fd = -1;
    }
    for (size_t i = 0; i < NR_OPEN_DEFAULT; i++) {
        if (entry->shared_fds[i] == fd) {
            entry->shared_fds[i] = -1;
        }
        if (entry->shared_fds[i] >= 0) {
            has_shared = true;
        }
    }
    if (entry->exclusive_fd < 0 && !has_shared) {
        memset(entry, 0, sizeof(*entry));
    }
}

int flock_impl(int fd, int operation) {
    void *entry_ref;
    struct flock_entry *entry;
    uint64_t identity;
    int op = operation & ~LOCK_NB;
    int ret = 0;

    if (fcntl_get_entry_or_badf(fd, &entry_ref) != 0) {
        return -EBADF;
    }
    identity = get_fd_file_identity_impl(entry_ref);
    put_fd_entry_impl(entry_ref);
    if (identity == 0) {
        return -EBADF;
    }
    if (op != LOCK_SH && op != LOCK_EX && op != LOCK_UN) {
        return -EINVAL;
    }

    fs_mutex_lock(&flock_table_lock);
    entry = flock_entry_for_identity_locked(identity, op != LOCK_UN);
    if (op == LOCK_UN) {
        flock_remove_fd_locked(entry, fd);
        fs_mutex_unlock(&flock_table_lock);
        return 0;
    }

    flock_remove_fd_locked(entry, fd);
    entry = flock_entry_for_identity_locked(identity, true);
    if (!entry) {
        fs_mutex_unlock(&flock_table_lock);
        return -ENOLCK;
    }

    if (op == LOCK_EX) {
        if ((entry->exclusive_fd >= 0 && entry->exclusive_fd != fd) ||
            flock_has_other_shared_locked(entry, fd)) {
            ret = -1;
        } else {
            entry->exclusive_fd = fd;
        }
    } else {
        if (entry->exclusive_fd >= 0 && entry->exclusive_fd != fd) {
            ret = -1;
        } else {
            for (size_t i = 0; i < NR_OPEN_DEFAULT; i++) {
                if (entry->shared_fds[i] < 0) {
                    entry->shared_fds[i] = fd;
                    break;
                }
            }
        }
    }
    fs_mutex_unlock(&flock_table_lock);
    if (ret != 0) {
        return -EAGAIN;
    }
    return 0;
}

int dup_impl(int oldfd) {
    return clone_fd_entry_impl(oldfd, 0, false);
}

int dup2_impl(int oldfd, int newfd) {
    if (oldfd < 0 || oldfd >= NR_OPEN_DEFAULT || newfd < 0 || newfd >= NR_OPEN_DEFAULT) {
        return -EBADF;
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
        return -EINVAL;
    }

    if (flags & ~O_CLOEXEC) {
        return -EINVAL;
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
    case F_GET_SEALS:
        if (fcntl_get_entry_or_badf(fd, &entry) != 0) {
            return -1;
        }
        result = memfd_get_seals_entry_impl(entry);
        put_fd_entry_impl(entry);
        return result;
    case F_ADD_SEALS:
        if (fcntl_get_entry_or_badf(fd, &entry) != 0) {
            return -1;
        }
        result = memfd_add_seals_entry_impl(entry, arg);
        put_fd_entry_impl(entry);
        return result;
    default:
        return -EINVAL;
    }
}
