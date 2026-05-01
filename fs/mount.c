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

#include <linux/mount.h>

extern int vfs_mount(const char *source, const char *target,
                      const char *fstype, unsigned long flags,
                      const void *data);
extern int vfs_umount(const char *target);

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

static int mount_impl(const char *source, const char *target,
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
    if (flags != 0) {
        errno = EINVAL;
        return -1;
    }

    return umount_impl(target);
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
