/* iXland - Mount Operations
 *
 * Canonical owner for mount syscalls:
 * - mount(), umount(), umount2() - virtual mount operations
 *
 * Linux-shaped canonical owner - iOS mediation as implementation detail
 * Virtual mount behavior against Orlix's own VFS, NOT host mount(2).
 *
 */
#include <linux/errno.h>
#include <linux/string.h>

#include <uapi/linux/fcntl.h>
#include <uapi/linux/fs.h>
#include <uapi/linux/mount.h>
#include <uapi/linux/stat.h>
#include <uapi/asm/stat.h>

#include "linux_umount2_flags.h"
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
    struct stat st;
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
 * MOUNT - Virtual mount in Orlix namespace
 * ============================================================================
 *
 * This implements Linux mount semantics against Orlix's own VFS,
 * NOT the iOS host mount() entrypoint.
 *
 * source: An app-container path or user-granted directory
 * target: A path in Orlix's virtual namespace
 * filesystemtype: Interpreted by Orlix VFS
 * mountflags: Linux-style mount flags
 * data: Filesystem-specific data
 */

int mount_impl(const char *source, const char *target,
               const char *filesystemtype, unsigned long mountflags,
               const void *data) {
    if ((!source && (mountflags & MS_REMOUNT) == 0) || !target) {
        return -EFAULT;
    }

    /* Validate inputs */
    if (((mountflags & MS_REMOUNT) == 0 && source[0] == '\0') || target[0] == '\0') {
        return -ENOENT;
    }

    if (filesystemtype && strlen(filesystemtype) == 0) {
        return -EINVAL;
    }

    return vfs_mount(source, target, filesystemtype, mountflags, data);
}

/* ============================================================================
 * UMOUNT - Virtual unmount from Orlix namespace
 * ============================================================================ */

int umount_impl(const char *target) {
    if (!target) {
        return -EFAULT;
    }

    if (target[0] == '\0') {
        return -ENOENT;
    }

    return vfs_umount(target);
}

/* ============================================================================
 * UMOUNT2 - Virtual unmount with flags
 * ============================================================================ */

int umount2_impl(const char *target, int flags) {
    if (!target) {
        return -EFAULT;
    }

    if (target[0] == '\0') {
        return -ENOENT;
    }

    if ((flags & ~(MNT_FORCE | MNT_DETACH | MNT_EXPIRE | UMOUNT_NOFOLLOW)) != 0) {
        return -EINVAL;
    }

    if ((flags & MNT_EXPIRE) != 0 && (flags & (MNT_FORCE | MNT_DETACH)) != 0) {
        return -EINVAL;
    }

    if ((flags & UMOUNT_NOFOLLOW) != 0) {
        int is_symlink = 0;
        int follow_ret = umount_target_is_symlink(target, &is_symlink);
        if (follow_ret < 0) {
            return follow_ret;
        }
        if (is_symlink) {
            return -EINVAL;
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
    return ret;
}
