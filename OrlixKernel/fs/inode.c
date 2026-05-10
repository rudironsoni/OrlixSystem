/* OrlixKernel/fs/inode.c
 * Linux-shaped inode metadata operations.
 *
 * Ownership and mode changes are virtual OrlixKernel metadata. Host uid/gid
 * ownership is not the semantic source of truth.
 */

#include <linux/errno.h>
#include <uapi/linux/fcntl.h>
#include <uapi/linux/stat.h>
#include <uapi/linux/time_types.h>
#include <uapi/asm/stat.h>

#include "fdtable.h"
#include "vfs.h"
#include "internal/fs/file.h"
#include "../kernel/mm.h"
#include "../kernel/task.h"

#define LINUX_UTIME_NOW_VALUE  ((1L << 30) - 1L)
#define LINUX_UTIME_OMIT_VALUE ((1L << 30) - 2L)

extern int linux_realtime_now_impl(struct __kernel_timespec *tp);

static int inode_resolve_path_at(int dirfd, const char *pathname, char *resolved_path,
                                 size_t resolved_path_len) {
    int ret;

    if (!pathname) {
        return -EFAULT;
    }
    if (pathname[0] == '\0') {
        return -ENOENT;
    }

    ret = vfs_resolve_virtual_path_at(dirfd, pathname, resolved_path, resolved_path_len);
    if (ret != 0) {
        return ret;
    }
    return 0;
}

static int inode_resolve_fd_path(int fd, char *resolved_path, size_t resolved_path_len) {
    fd_entry_t *entry;
    int ret;

    if (fd < 0 || fd >= NR_OPEN_DEFAULT) {
        return -EBADF;
    }

    entry = get_fd_entry_impl(fd);
    if (!entry) {
        return -EBADF;
    }

    ret = get_fd_path_impl(entry, resolved_path, resolved_path_len);
    put_fd_entry_impl(entry);
    if (ret != 0) {
        return -ENOENT;
    }
    return 0;
}

int chmod_impl(const char *pathname, uint32_t mode) {
    char resolved_path[MAX_PATH];
    int ret;

    if (inode_resolve_path_at(AT_FDCWD, pathname, resolved_path, sizeof(resolved_path)) != 0) {
        return -1;
    }

    ret = vfs_chmod_metadata(resolved_path, mode);
    if (ret != 0) {
        return ret;
    }
    return 0;
}

int fchmod_impl(int fd, uint32_t mode) {
    char resolved_path[MAX_PATH];
    int ret;

    if (inode_resolve_fd_path(fd, resolved_path, sizeof(resolved_path)) != 0) {
        return -1;
    }

    ret = vfs_chmod_metadata(resolved_path, mode);
    if (ret != 0) {
        return ret;
    }
    return 0;
}

int fchmodat_impl(int dirfd, const char *pathname, uint32_t mode, int flags) {
    char resolved_path[MAX_PATH];
    int ret;

    if (flags & ~AT_SYMLINK_NOFOLLOW) {
        return -EINVAL;
    }
    if (inode_resolve_path_at(dirfd, pathname, resolved_path, sizeof(resolved_path)) != 0) {
        return -1;
    }

    ret = vfs_chmod_metadata(resolved_path, mode);
    if (ret != 0) {
        return ret;
    }
    return 0;
}

int chown_impl(const char *pathname, uint32_t owner, uint32_t group) {
    char resolved_path[MAX_PATH];
    int ret;

    if (inode_resolve_path_at(AT_FDCWD, pathname, resolved_path, sizeof(resolved_path)) != 0) {
        return -1;
    }

    ret = vfs_chown_metadata(resolved_path, owner, group);
    if (ret != 0) {
        return ret;
    }
    return 0;
}

int fchown_impl(int fd, uint32_t owner, uint32_t group) {
    char resolved_path[MAX_PATH];
    int ret;

    if (inode_resolve_fd_path(fd, resolved_path, sizeof(resolved_path)) != 0) {
        return -1;
    }

    ret = vfs_chown_metadata(resolved_path, owner, group);
    if (ret != 0) {
        return ret;
    }
    return 0;
}

int lchown_impl(const char *pathname, uint32_t owner, uint32_t group) {
    return chown_impl(pathname, owner, group);
}

int fchownat_impl(int dirfd, const char *pathname, uint32_t owner, uint32_t group, int flags) {
    char resolved_path[MAX_PATH];
    int ret;

    if (flags & ~(AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW)) {
        return -EINVAL;
    }
    if ((flags & AT_EMPTY_PATH) && pathname && pathname[0] == '\0') {
        return fchown_impl(dirfd, owner, group);
    }
    if (inode_resolve_path_at(dirfd, pathname, resolved_path, sizeof(resolved_path)) != 0) {
        return -1;
    }

    ret = vfs_chown_metadata(resolved_path, owner, group);
    if (ret != 0) {
        return ret;
    }
    return 0;
}

int utimensat_impl(int dirfd, const char *pathname, const struct __kernel_timespec times[2],
                   int flags) {
    char resolved_path[MAX_PATH];
    struct stat st;
    struct __kernel_timespec now;
    long atime_sec;
    unsigned long atime_nsec;
    long mtime_sec;
    unsigned long mtime_nsec;
    int stat_flags = flags & AT_SYMLINK_NOFOLLOW;
    int ret;

    if ((flags & ~AT_SYMLINK_NOFOLLOW) != 0) {
        return -EINVAL;
    }
    ret = inode_resolve_path_at(dirfd, pathname, resolved_path, sizeof(resolved_path));
    if (ret != 0) {
        return -1;
    }
    ret = vfs_fstatat(AT_FDCWD, resolved_path, &st, stat_flags);
    if (ret != 0) {
        return ret;
    }

    atime_sec = st.st_atime;
    atime_nsec = st.st_atime_nsec;
    mtime_sec = st.st_mtime;
    mtime_nsec = st.st_mtime_nsec;

    if (!times || times[0].tv_nsec == LINUX_UTIME_NOW_VALUE ||
        times[1].tv_nsec == LINUX_UTIME_NOW_VALUE) {
        if (linux_realtime_now_impl(&now) != 0) {
            return -1;
        }
    }

    if (!times) {
        atime_sec = (long)now.tv_sec;
        atime_nsec = (unsigned long)now.tv_nsec;
        mtime_sec = (long)now.tv_sec;
        mtime_nsec = (unsigned long)now.tv_nsec;
    } else {
        if (times[0].tv_nsec != LINUX_UTIME_OMIT_VALUE) {
            if (times[0].tv_nsec == LINUX_UTIME_NOW_VALUE) {
                atime_sec = (long)now.tv_sec;
                atime_nsec = (unsigned long)now.tv_nsec;
            } else if (times[0].tv_nsec < 0 || times[0].tv_nsec >= 1000000000LL ||
                       times[0].tv_sec < 0) {
                return -EINVAL;
            } else {
                atime_sec = (long)times[0].tv_sec;
                atime_nsec = (unsigned long)times[0].tv_nsec;
            }
        }
        if (times[1].tv_nsec != LINUX_UTIME_OMIT_VALUE) {
            if (times[1].tv_nsec == LINUX_UTIME_NOW_VALUE) {
                mtime_sec = (long)now.tv_sec;
                mtime_nsec = (unsigned long)now.tv_nsec;
            } else if (times[1].tv_nsec < 0 || times[1].tv_nsec >= 1000000000LL ||
                       times[1].tv_sec < 0) {
                return -EINVAL;
            } else {
                mtime_sec = (long)times[1].tv_sec;
                mtime_nsec = (unsigned long)times[1].tv_nsec;
            }
        }
    }

    ret = vfs_utimens_metadata(resolved_path, atime_sec, atime_nsec, mtime_sec, mtime_nsec);
    if (ret != 0) {
        return ret;
    }
    return 0;
}

uint32_t umask_impl(uint32_t mask) {
    struct task_struct *task = get_current();
    uint32_t old;

    if (!task || !task->fs) {
        return 0;
    }

    old = task->fs->umask;
    task->fs->umask = (uint32_t)(mask & 0777U);
    return old;
}

int truncate_impl(const char *path, int64_t length) {
    char resolved_path[MAX_PATH];
    char translated_path[MAX_PATH];
    int ret;

    if (length < 0) {
        return -EINVAL;
    }
    if (inode_resolve_path_at(AT_FDCWD, path, resolved_path, sizeof(resolved_path)) != 0) {
        return -1;
    }
    ret = vfs_translate_path(resolved_path, translated_path, sizeof(translated_path));
    if (ret != 0) {
        return ret;
    }
    ret = backing_truncate(translated_path, length);
    if (ret != 0) {
        return ret;
    }
    return 0;
}

int ftruncate_impl(int fd, int64_t length) {
    fd_entry_t *entry;
    int real_fd;
    int ret;

    if (length < 0) {
        return -EINVAL;
    }
    if (fd < 0 || fd >= NR_OPEN_DEFAULT) {
        return -EBADF;
    }
    entry = get_fd_entry_impl(fd);
    if (!entry) {
        return -EBADF;
    }
    if (get_fd_is_pipe_impl(entry)) {
        put_fd_entry_impl(entry);
        return -EINVAL;
    }
    if (get_fd_is_memfd_impl(entry) && memfd_truncate_allowed_entry_impl(entry, length) != 0) {
        put_fd_entry_impl(entry);
        return -1;
    }
    real_fd = get_real_fd_impl(entry);
    put_fd_entry_impl(entry);
    if (real_fd < 0) {
        return -EINVAL;
    }
    ret = backing_ftruncate(real_fd, length);
    if (ret != 0) {
        return ret;
    }
    mm_note_file_truncate_impl(fd, length);
    return 0;
}
