/* fs/super.c
 * Linux-shaped superblock operations over IXLand virtual filesystems.
 *
 * Host filesystem statistics do not define the Linux-facing contract here.
 */

#include <errno.h>
#include <string.h>

#include <asm/statfs.h>
#include <linux/fcntl.h>
#include <linux/magic.h>
#include <linux/mount.h>
#include "fdtable.h"
#include "vfs.h"

typedef __INT64_TYPE__ super_off_t;

static long vfs_statfs_magic_for_path(const char *path) {
    enum vfs_backing_class backing_class;
    enum vfs_route_identity route_id;

    if (vfs_describe_route_for_path(path, &route_id, &backing_class, NULL) != 0) {
        return TMPFS_MAGIC;
    }

    switch (route_id) {
    case VFS_ROUTE_PROC:
        return PROC_SUPER_MAGIC;
    case VFS_ROUTE_SYS:
        return SYSFS_MAGIC;
    case VFS_ROUTE_DEV:
        return TMPFS_MAGIC;
    default:
        break;
    }

    switch (backing_class) {
    case VFS_BACKING_TEMP:
    case VFS_BACKING_CACHE:
    case VFS_BACKING_PERSISTENT:
    case VFS_BACKING_EXTERNAL:
        return TMPFS_MAGIC;
    case VFS_BACKING_SYNTHETIC:
        return TMPFS_MAGIC;
    default:
        return TMPFS_MAGIC;
    }
}

static int vfs_fill_statfs(const char *resolved_path, struct statfs *buf) {
    unsigned long mount_flags;

    if (!buf) {
        errno = EFAULT;
        return -1;
    }

    memset(buf, 0, sizeof(*buf));
    buf->f_type = vfs_statfs_magic_for_path(resolved_path);
    buf->f_bsize = 4096;
    buf->f_blocks = 65536;
    buf->f_bfree = 32768;
    buf->f_bavail = 32768;
    buf->f_files = 65536;
    buf->f_ffree = 32768;
    buf->f_namelen = 255;
    buf->f_frsize = 4096;
    /* Linux statfs(2) f_flags are defined by libc; values match MS_* in Linux headers. */
    buf->f_flags = MS_REMOUNT;
    mount_flags = vfs_mount_flags_for_path(resolved_path);
    if ((mount_flags & MS_RDONLY) != 0) {
        buf->f_flags |= MS_RDONLY;
    }
    if ((mount_flags & MS_NOSUID) != 0) {
        buf->f_flags |= MS_NOSUID;
    }
    if ((mount_flags & MS_NODEV) != 0) {
        buf->f_flags |= MS_NODEV;
    }
    if ((mount_flags & MS_NOEXEC) != 0) {
        buf->f_flags |= MS_NOEXEC;
    }
    return 0;
}

void sync_impl(void) {
}

int fsync_impl(int fd) {
    void *entry = get_fd_entry_impl(fd);
    if (!entry) {
        errno = EBADF;
        return -1;
    }
    put_fd_entry_impl(entry);
    return 0;
}

int fdatasync_impl(int fd) {
    return fsync_impl(fd);
}

int syncfs_impl(int fd) {
    return fsync_impl(fd);
}

int statfs_impl(const char *path, struct statfs *buf) {
    char resolved_path[MAX_PATH];
    int ret;

    if (!path) {
        errno = EFAULT;
        return -1;
    }

    ret = vfs_resolve_virtual_path_at(AT_FDCWD, path, resolved_path, sizeof(resolved_path));
    if (ret != 0) {
        errno = -ret;
        return -1;
    }

    return vfs_fill_statfs(resolved_path, buf);
}

int fstatfs_impl(int fd, struct statfs *buf) {
    char path[MAX_PATH];
    void *entry;
    int ret;

    entry = get_fd_entry_impl(fd);
    if (!entry) {
        errno = EBADF;
        return -1;
    }
    ret = get_fd_path_impl(entry, path, sizeof(path));
    put_fd_entry_impl(entry);
    if (ret != 0) {
        errno = EBADF;
        return -1;
    }

    return vfs_fill_statfs(path, buf);
}

static int posix_fadvise_impl(int fd, super_off_t offset, super_off_t len, int advice) {
    (void)offset;
    (void)len;
    (void)advice;
    return fsync_impl(fd);
}

static int posix_fallocate_impl(int fd, super_off_t offset, super_off_t len) {
    (void)offset;
    (void)len;
    return fsync_impl(fd);
}

__attribute__((visibility("default"))) void sync(void) {
    sync_impl();
}

__attribute__((visibility("default"))) int fsync(int fd) {
    return fsync_impl(fd);
}

__attribute__((visibility("default"))) int fdatasync(int fd) {
    return fdatasync_impl(fd);
}

__attribute__((visibility("default"))) int syncfs(int fd) {
    return syncfs_impl(fd);
}

__attribute__((visibility("default"))) int statfs(const char *path, struct statfs *buf) {
    return statfs_impl(path, buf);
}

__attribute__((visibility("default"))) int fstatfs(int fd, struct statfs *buf) {
    return fstatfs_impl(fd, buf);
}

__attribute__((visibility("default"))) int posix_fadvise(int fd, super_off_t offset,
                                                         super_off_t len, int advice) {
    return posix_fadvise_impl(fd, offset, len, advice);
}

__attribute__((visibility("default"))) int posix_fallocate(int fd, super_off_t offset,
                                                           super_off_t len) {
    return posix_fallocate_impl(fd, offset, len);
}
