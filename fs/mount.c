/* iXland - Mount Operations
 *
 * Canonical owner for mount syscalls:
 * - mount(), umount(), umount2() - virtual mount operations
 *
 * Linux-shaped canonical owner - iOS mediation as implementation detail
 * Virtual mount behavior against IXLand's own VFS, NOT host mount(2).
 *
 */

#include <errno.h>
#include <string.h>

#include <linux/fcntl.h>
#include <linux/mount.h>
#include <linux/stat.h>
#include <linux/umount.h>

#include "vfs.h"

extern int vfs_mount(const char *source, const char *target,
                      const char *fstype, unsigned long flags,
                      const void *data);
extern int vfs_umount(const char *target);
extern int vfs_umount_lazy(const char *target);
extern int vfs_umount_expire(const char *target);
extern int vfs_umount_force(const char *target);
extern int vfs_mount_setattr(int dirfd, const char *pathname, unsigned int flags,
                             const struct mount_attr *attr, size_t size);
extern int vfs_open_tree(int dirfd, const char *pathname, unsigned int flags);
extern int vfs_move_mount(int from_dirfd, const char *from_pathname, int to_dirfd,
                          const char *to_pathname, unsigned int flags);
extern int vfs_pivot_root(const char *new_root, const char *put_old);

static int umount_target_is_symlink(const char *target, int *is_symlink) {
    struct linux_stat st;
    int ret;

    if (!target || !is_symlink) {
        return -EFAULT;
    }
    ret = vfs_fstatat(AT_FDCWD, target, &st, AT_SYMLINK_NOFOLLOW);
    if (ret < 0) {
        return ret;
    }
    *is_symlink = ((st.st_mode & S_IFMT) == S_IFLNK) ? 1 : 0;
    return 0;
}

/* ============================================================================
 * MOUNT - Virtual mount in IXLand namespace
 * ============================================================================
 *
 * This implements Linux mount semantics against IXLand's own VFS,
 * NOT the iOS host mount() entrypoint.
 *
 * source: An app-container path or user-granted directory
 * target: A path in IXLand's virtual namespace
 * filesystemtype: Interpreted by IXLand VFS
 * mountflags: Linux-style mount flags
 * data: Filesystem-specific data
 */

int mount_impl(const char *source, const char *target,
               const char *filesystemtype, unsigned long mountflags,
               const void *data) {
    if ((!source && (mountflags & MS_REMOUNT) == 0) || !target) {
        errno = EFAULT;
        return -1;
    }

    /* Validate inputs */
    if (((mountflags & MS_REMOUNT) == 0 && source[0] == '\0') || target[0] == '\0') {
        errno = ENOENT;
        return -1;
    }

    if (filesystemtype && strlen(filesystemtype) == 0) {
        errno = EINVAL;
        return -1;
    }

    int ret = vfs_mount(source, target, filesystemtype, mountflags, data);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}

/* ============================================================================
 * UMOUNT - Virtual unmount from IXLand namespace
 * ============================================================================ */

static int umount_impl(const char *target) {
    if (!target) {
        errno = EFAULT;
        return -1;
    }

    if (target[0] == '\0') {
        errno = ENOENT;
        return -1;
    }

    int ret = vfs_umount(target);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}

/* ============================================================================
 * UMOUNT2 - Virtual unmount with flags
 * ============================================================================ */

static int umount2_impl(const char *target, int flags) {
    if (!target) {
        errno = EFAULT;
        return -1;
    }

    if (target[0] == '\0') {
        errno = ENOENT;
        return -1;
    }

    if ((flags & ~(MNT_FORCE | MNT_DETACH | MNT_EXPIRE | UMOUNT_NOFOLLOW)) != 0) {
        errno = EINVAL;
        return -1;
    }

    if ((flags & MNT_EXPIRE) != 0 && (flags & (MNT_FORCE | MNT_DETACH)) != 0) {
        errno = EINVAL;
        return -1;
    }

    if ((flags & UMOUNT_NOFOLLOW) != 0) {
        int is_symlink = 0;
        int follow_ret = umount_target_is_symlink(target, &is_symlink);
        if (follow_ret < 0) {
            errno = -follow_ret;
            return -1;
        }
        if (is_symlink) {
            errno = EINVAL;
            return -1;
        }
    }

    int ret;
    if ((flags & MNT_DETACH) != 0) {
        ret = vfs_umount_lazy(target);
    } else if ((flags & MNT_EXPIRE) != 0) {
        ret = vfs_umount_expire(target);
    } else if ((flags & MNT_FORCE) != 0) {
        ret = vfs_umount_force(target);
    } else {
        ret = vfs_umount(target);
    }
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}

/* ============================================================================
 * Public Canonical Syscalls
 * ============================================================================ */

__attribute__((visibility("default"))) int mount(const char *source,
                                                   const char *target,
                                                   const char *filesystemtype,
                                                   unsigned long mountflags,
                                                   const void *data) {
    return mount_impl(source, target, filesystemtype, mountflags, data);
}

__attribute__((visibility("default"))) int umount(const char *target) {
    return umount_impl(target);
}

__attribute__((visibility("default"))) int umount2(const char *target, int flags) {
    return umount2_impl(target, flags);
}

__attribute__((visibility("default"))) int mount_setattr(int dirfd, const char *pathname,
                                                         unsigned int flags,
                                                         struct mount_attr *attr,
                                                         size_t size) {
    int ret = vfs_mount_setattr(dirfd, pathname, flags, attr, size);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}

__attribute__((visibility("default"))) int open_tree(int dirfd, const char *pathname,
                                                     unsigned int flags) {
    int ret = vfs_open_tree(dirfd, pathname, flags);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}

__attribute__((visibility("default"))) int move_mount(int from_dirfd, const char *from_pathname,
                                                      int to_dirfd, const char *to_pathname,
                                                      unsigned int flags) {
    int ret = vfs_move_mount(from_dirfd, from_pathname, to_dirfd, to_pathname, flags);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}

__attribute__((visibility("default"))) int pivot_root(const char *new_root, const char *put_old) {
    int ret = vfs_pivot_root(new_root, put_old);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}
