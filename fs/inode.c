/* IXLandSystem/fs/inode.c
 * Linux-shaped inode metadata operations.
 *
 * Ownership and mode changes are virtual IXLandSystem metadata. Host uid/gid
 * ownership is not the semantic source of truth.
 */

#include <errno.h>
#include <linux/fcntl.h>

#include "fdtable.h"
#include "vfs.h"
#include "internal/ios/fs/file_io_host.h"
#include "../kernel/mm.h"
#include "../kernel/task.h"

static int inode_resolve_path_at(int dirfd, const char *pathname, char *resolved_path,
                                 size_t resolved_path_len) {
    int ret;

    if (!pathname) {
        errno = EFAULT;
        return -1;
    }
    if (pathname[0] == '\0') {
        errno = ENOENT;
        return -1;
    }

    ret = vfs_resolve_virtual_path_at(dirfd, pathname, resolved_path, resolved_path_len);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }
    return 0;
}

static int inode_resolve_fd_path(int fd, char *resolved_path, size_t resolved_path_len) {
    fd_entry_t *entry;
    int ret;

    if (fd < 0 || fd >= NR_OPEN_DEFAULT) {
        errno = EBADF;
        return -1;
    }

    entry = get_fd_entry_impl(fd);
    if (!entry) {
        errno = EBADF;
        return -1;
    }

    ret = get_fd_path_impl(entry, resolved_path, resolved_path_len);
    put_fd_entry_impl(entry);
    if (ret != 0) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

static int chmod_impl(const char *pathname, linux_mode_t mode) {
    char resolved_path[MAX_PATH];
    int ret;

    if (inode_resolve_path_at(AT_FDCWD, pathname, resolved_path, sizeof(resolved_path)) != 0) {
        return -1;
    }

    ret = vfs_chmod_metadata(resolved_path, mode);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }
    return 0;
}

static int fchmod_impl(int fd, linux_mode_t mode) {
    char resolved_path[MAX_PATH];
    int ret;

    if (inode_resolve_fd_path(fd, resolved_path, sizeof(resolved_path)) != 0) {
        return -1;
    }

    ret = vfs_chmod_metadata(resolved_path, mode);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }
    return 0;
}

static int fchmodat_impl(int dirfd, const char *pathname, linux_mode_t mode, int flags) {
    char resolved_path[MAX_PATH];
    int ret;

    if (flags & ~AT_SYMLINK_NOFOLLOW) {
        errno = EINVAL;
        return -1;
    }
    if (inode_resolve_path_at(dirfd, pathname, resolved_path, sizeof(resolved_path)) != 0) {
        return -1;
    }

    ret = vfs_chmod_metadata(resolved_path, mode);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }
    return 0;
}

static int chown_impl(const char *pathname, linux_uid_t owner, linux_gid_t group) {
    char resolved_path[MAX_PATH];
    int ret;

    if (inode_resolve_path_at(AT_FDCWD, pathname, resolved_path, sizeof(resolved_path)) != 0) {
        return -1;
    }

    ret = vfs_chown_metadata(resolved_path, owner, group);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }
    return 0;
}

static int fchown_impl(int fd, linux_uid_t owner, linux_gid_t group) {
    char resolved_path[MAX_PATH];
    int ret;

    if (inode_resolve_fd_path(fd, resolved_path, sizeof(resolved_path)) != 0) {
        return -1;
    }

    ret = vfs_chown_metadata(resolved_path, owner, group);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }
    return 0;
}

static int lchown_impl(const char *pathname, linux_uid_t owner, linux_gid_t group) {
    return chown_impl(pathname, owner, group);
}

static int fchownat_impl(int dirfd, const char *pathname, linux_uid_t owner, linux_gid_t group, int flags) {
    char resolved_path[MAX_PATH];
    int ret;

    if (flags & ~(AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW)) {
        errno = EINVAL;
        return -1;
    }
    if ((flags & AT_EMPTY_PATH) && pathname && pathname[0] == '\0') {
        return fchown_impl(dirfd, owner, group);
    }
    if (inode_resolve_path_at(dirfd, pathname, resolved_path, sizeof(resolved_path)) != 0) {
        return -1;
    }

    ret = vfs_chown_metadata(resolved_path, owner, group);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }
    return 0;
}

linux_mode_t umask_impl(linux_mode_t mask) {
    struct task_struct *task = get_current();
    linux_mode_t old;

    if (!task || !task->fs) {
        return 0;
    }

    old = task->fs->umask;
    task->fs->umask = (linux_mode_t)(mask & 0777U);
    return old;
}

int truncate_impl(const char *path, linux_off_t length) {
    char resolved_path[MAX_PATH];
    char translated_path[MAX_PATH];
    int ret;

    if (length < 0) {
        errno = EINVAL;
        return -1;
    }
    if (inode_resolve_path_at(AT_FDCWD, path, resolved_path, sizeof(resolved_path)) != 0) {
        return -1;
    }
    ret = vfs_translate_path(resolved_path, translated_path, sizeof(translated_path));
    if (ret != 0) {
        errno = -ret;
        return -1;
    }
    ret = host_truncate_impl(translated_path, length);
    if (ret != 0) {
        return -1;
    }
    return 0;
}

int ftruncate_impl(int fd, linux_off_t length) {
    fd_entry_t *entry;
    int real_fd;
    int ret;

    if (length < 0) {
        errno = EINVAL;
        return -1;
    }
    if (fd < 0 || fd >= NR_OPEN_DEFAULT) {
        errno = EBADF;
        return -1;
    }
    entry = get_fd_entry_impl(fd);
    if (!entry) {
        errno = EBADF;
        return -1;
    }
    if (get_fd_is_pipe_impl(entry)) {
        put_fd_entry_impl(entry);
        errno = EINVAL;
        return -1;
    }
    real_fd = get_real_fd_impl(entry);
    put_fd_entry_impl(entry);
    if (real_fd < 0) {
        errno = EINVAL;
        return -1;
    }
    ret = host_ftruncate_impl(real_fd, length);
    if (ret != 0) {
        return -1;
    }
    mm_note_file_truncate_impl(fd, length);
    return 0;
}

__attribute__((visibility("default"))) int chmod(const char *pathname, linux_mode_t mode) {
    return chmod_impl(pathname, mode);
}

__attribute__((visibility("default"))) int fchmod(int fd, linux_mode_t mode) {
    return fchmod_impl(fd, mode);
}

__attribute__((visibility("default"))) int fchmodat(int dirfd, const char *pathname, linux_mode_t mode, int flags) {
    return fchmodat_impl(dirfd, pathname, mode, flags);
}

__attribute__((visibility("default"))) int chown(const char *pathname, linux_uid_t owner, linux_gid_t group) {
    return chown_impl(pathname, owner, group);
}

__attribute__((visibility("default"))) int fchown(int fd, linux_uid_t owner, linux_gid_t group) {
    return fchown_impl(fd, owner, group);
}

__attribute__((visibility("default"))) int lchown(const char *pathname, linux_uid_t owner, linux_gid_t group) {
    return lchown_impl(pathname, owner, group);
}

__attribute__((visibility("default"))) int fchownat(int dirfd, const char *pathname, linux_uid_t owner, linux_gid_t group, int flags) {
    return fchownat_impl(dirfd, pathname, owner, group, flags);
}

__attribute__((visibility("default"))) linux_mode_t umask(linux_mode_t mask) {
    return umask_impl(mask);
}

__attribute__((visibility("default"))) int truncate(const char *path, linux_off_t length) {
    return truncate_impl(path, length);
}

__attribute__((visibility("default"))) int ftruncate(int fd, linux_off_t length) {
    return ftruncate_impl(fd, length);
}
