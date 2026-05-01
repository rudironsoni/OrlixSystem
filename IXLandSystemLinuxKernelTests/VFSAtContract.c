/* IXLandSystemTests/VFSAtContract.c
 * C translation unit for VFS AT_* flag Linux UAPI contract tests.
 *
 * Compiled in a Linux-UAPI-clean context.
 * Uses canonical Linux names directly.
 */

#include <linux/fcntl.h>
#include <linux/mount.h>

#include <errno.h>

#include "fs/vfs.h"
#include "kernel/task.h"

/* Access mode constants - defined locally to avoid Darwin <unistd.h> */
#ifndef X_OK
#define X_OK 1
#endif

extern int chroot(const char *path);
extern int fchdir(int fd);
extern char *getcwd(char *buf, size_t size);
extern int mount(const char *source, const char *target, const char *filesystemtype,
                 unsigned long mountflags, const void *data);
extern int umount(const char *target);
extern int mkdir_impl(const char *pathname, linux_mode_t mode);
extern int open_impl(const char *pathname, int flags, linux_mode_t mode);
extern int close_impl(int fd);
extern long read_impl(int fd, void *buf, size_t count);
extern long write_impl(int fd, const void *buf, size_t count);
extern int unlink_impl(const char *pathname);
extern int rmdir_impl(const char *pathname);

static int vfs_contract_ignore_exists(int result) {
    if (result == 0 || errno == EEXIST) {
        return 0;
    }
    return -1;
}

static void vfs_contract_restore_fs(struct fs_struct *fs, const char *root, const char *pwd) {
    if (!fs) {
        return;
    }
    fs_set_root(fs, root);
    fs_set_pwd(fs, pwd);
}

static int vfs_contract_write_file(const char *path, const char *content) {
    int fd;
    size_t len;

    fd = open_impl(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        return -1;
    }

    len = __builtin_strlen(content);
    if (write_impl(fd, content, len) != (long)len) {
        int saved_errno = errno;
        close_impl(fd);
        errno = saved_errno;
        return -1;
    }

    return close_impl(fd);
}

static int vfs_contract_read_file_exact(const char *path, const char *expected) {
    char buf[64];
    int fd;
    long nread;
    size_t expected_len = __builtin_strlen(expected);

    if (expected_len >= sizeof(buf)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    fd = open_impl(path, O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }

    nread = read_impl(fd, buf, sizeof(buf));
    {
        int saved_errno = errno;
        close_impl(fd);
        errno = saved_errno;
    }
    if (nread < 0) {
        return -1;
    }
    if ((size_t)nread != expected_len || __builtin_memcmp(buf, expected, expected_len) != 0) {
        errno = EIO;
        return -1;
    }

    return 0;
}

static void vfs_contract_cleanup_mount_paths(void) {
    umount("/tmp/vfs-bind-target");
    unlink_impl("/tmp/vfs-bind-source/file");
    unlink_impl("/tmp/vfs-bind-target/file");
    rmdir_impl("/tmp/vfs-bind-source");
    rmdir_impl("/tmp/vfs-bind-target");
}

static void vfs_contract_cleanup_mount_namespace_paths(void) {
    umount("/tmp/vfs-mntns-target");
    unlink_impl("/tmp/vfs-mntns-parent-source/file");
    unlink_impl("/tmp/vfs-mntns-child-source/file");
    unlink_impl("/tmp/vfs-mntns-target/file");
    rmdir_impl("/tmp/vfs-mntns-parent-source");
    rmdir_impl("/tmp/vfs-mntns-child-source");
    rmdir_impl("/tmp/vfs-mntns-target");
}

/* Contract: vfs_fstatat supports AT_FDCWD */
int vfs_contract_fstatat_at_fdcwd(void) {
    struct linux_stat st;
    return vfs_fstatat(AT_FDCWD, "/etc/passwd", &st, 0);
}

/* Contract: vfs_fstatat supports AT_SYMLINK_NOFOLLOW */
int vfs_contract_fstatat_symlink_nofollow(void) {
    struct linux_stat st;
    return vfs_fstatat(AT_FDCWD, "/etc/passwd", &st, AT_SYMLINK_NOFOLLOW);
}

/* Contract: vfs_fstatat rejects unsupported synthetic paths with AT_SYMLINK_NOFOLLOW */
int vfs_contract_fstatat_synthetic_child_nofollow(void) {
    struct linux_stat st;
    return vfs_fstatat(AT_FDCWD, "/sys/kernel", &st, AT_SYMLINK_NOFOLLOW);
}

/* Contract: vfs_faccessat reports ENOTSUP for AT_EACCESS */
int vfs_contract_faccessat_eaccess_returns_enotsup(void) {
    return vfs_faccessat(AT_FDCWD, "/etc", X_OK, AT_EACCESS);
}

/* Contract: vfs_faccessat reports ENOTSUP for AT_SYMLINK_NOFOLLOW */
int vfs_contract_faccessat_symlink_nofollow_returns_enotsup(void) {
    return vfs_faccessat(AT_FDCWD, "/etc", X_OK, AT_SYMLINK_NOFOLLOW);
}

int vfs_contract_chroot_rebases_absolute_paths_and_getcwd(void) {
    struct task_struct *task = get_current();
    char old_root[MAX_PATH];
    char old_pwd[MAX_PATH];
    char cwd[MAX_PATH];
    int fd = -1;
    int ret = -1;

    if (!task || !task->fs) {
        errno = ESRCH;
        return -1;
    }

    __builtin_memcpy(old_root, task->fs->root_path, sizeof(old_root));
    __builtin_memcpy(old_pwd, task->fs->pwd_path, sizeof(old_pwd));
    fs_set_root(task->fs, "/");
    fs_set_pwd(task->fs, "/");

    unlink_impl("/tmp/vfs-chroot-root/inside");
    rmdir_impl("/tmp/vfs-chroot-root");
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-chroot-root", 0700)) != 0) {
        ret = -2;
        goto out;
    }

    if (chroot("/tmp/vfs-chroot-root") != 0) {
        ret = -4;
        goto out;
    }

    if (!getcwd(cwd, sizeof(cwd)) || __builtin_strcmp(cwd, "/") != 0) {
        ret = -5;
        goto out;
    }

    fd = open_impl("/inside", O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        ret = -6;
        goto out;
    }
    if (write_impl(fd, "rooted", 6) != 6) {
        ret = -7;
        goto out;
    }
    close_impl(fd);
    fd = -1;

    fd = open_impl("/inside", O_RDONLY, 0);
    if (fd < 0) {
        ret = -8;
        goto out;
    }
    close_impl(fd);
    fd = -1;

    ret = 0;

out:
    {
        int saved_errno = errno;
        close_impl(fd);
        errno = saved_errno;
    }
    vfs_contract_restore_fs(task->fs, old_root, old_pwd);
    unlink_impl("/tmp/vfs-chroot-root/inside");
    rmdir_impl("/tmp/vfs-chroot-root");
    return ret;
}

int vfs_contract_fchdir_updates_virtual_pwd(void) {
    struct task_struct *task = get_current();
    char old_root[MAX_PATH];
    char old_pwd[MAX_PATH];
    char cwd[MAX_PATH];
    int fd = -1;
    int ret = -1;

    if (!task || !task->fs) {
        errno = ESRCH;
        return -1;
    }

    __builtin_memcpy(old_root, task->fs->root_path, sizeof(old_root));
    __builtin_memcpy(old_pwd, task->fs->pwd_path, sizeof(old_pwd));
    fs_set_root(task->fs, "/");
    fs_set_pwd(task->fs, "/");

    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-fchdir-dir", 0700)) != 0) {
        goto out;
    }

    fd = open_impl("/tmp/vfs-fchdir-dir", O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0) {
        goto out;
    }

    if (fchdir(fd) != 0) {
        goto out;
    }
    if (!getcwd(cwd, sizeof(cwd)) || __builtin_strcmp(cwd, "/tmp/vfs-fchdir-dir") != 0) {
        goto out;
    }

    ret = 0;

out:
    {
        int saved_errno = errno;
        close_impl(fd);
        errno = saved_errno;
    }
    vfs_contract_restore_fs(task->fs, old_root, old_pwd);
    rmdir_impl("/tmp/vfs-fchdir-dir");
    return ret;
}

int vfs_contract_bind_mount_redirects_target_tree(void) {
    int ret = -1;

    vfs_contract_cleanup_mount_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-bind-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-bind-target", 0700)) != 0) {
        goto out;
    }
    if (vfs_contract_write_file("/tmp/vfs-bind-source/file", "source") != 0) {
        goto out;
    }

    if (mount("/tmp/vfs-bind-source", "/tmp/vfs-bind-target", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }
    if (vfs_contract_read_file_exact("/tmp/vfs-bind-target/file", "source") != 0) {
        goto out;
    }

    ret = 0;

out:
    vfs_contract_cleanup_mount_paths();
    return ret;
}

int vfs_contract_bind_mount_duplicate_target_returns_busy(void) {
    int ret = -1;

    vfs_contract_cleanup_mount_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-bind-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-bind-target", 0700)) != 0) {
        goto out;
    }

    if (mount("/tmp/vfs-bind-source", "/tmp/vfs-bind-target", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }
    errno = 0;
    if (mount("/tmp/vfs-bind-source", "/tmp/vfs-bind-target", NULL, MS_BIND, NULL) != -1 ||
        errno != EBUSY) {
        goto out;
    }

    ret = 0;

out:
    vfs_contract_cleanup_mount_paths();
    return ret;
}

int vfs_contract_umount_restores_target_tree(void) {
    int ret = -1;

    vfs_contract_cleanup_mount_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-bind-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-bind-target", 0700)) != 0) {
        goto out;
    }
    if (vfs_contract_write_file("/tmp/vfs-bind-source/file", "source") != 0 ||
        vfs_contract_write_file("/tmp/vfs-bind-target/file", "target") != 0) {
        goto out;
    }

    if (mount("/tmp/vfs-bind-source", "/tmp/vfs-bind-target", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }
    if (vfs_contract_read_file_exact("/tmp/vfs-bind-target/file", "source") != 0) {
        goto out;
    }
    if (umount("/tmp/vfs-bind-target") != 0) {
        goto out;
    }
    if (vfs_contract_read_file_exact("/tmp/vfs-bind-target/file", "target") != 0) {
        goto out;
    }

    ret = 0;

out:
    vfs_contract_cleanup_mount_paths();
    return ret;
}

int vfs_contract_bind_mount_rejects_non_bind_mount(void) {
    vfs_contract_cleanup_mount_paths();
    vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-bind-source", 0700));
    vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-bind-target", 0700));

    errno = 0;
    if (mount("/tmp/vfs-bind-source", "/tmp/vfs-bind-target", "tmpfs", 0, NULL) != -1 ||
        errno != ENOSYS) {
        vfs_contract_cleanup_mount_paths();
        return -1;
    }

    vfs_contract_cleanup_mount_paths();
    return 0;
}

int vfs_contract_mount_namespace_shared_across_task_dup(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
    int ret = -1;

    if (!parent || !parent->fs) {
        errno = ESRCH;
        return -1;
    }

    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0) {
        goto out;
    }
    if (vfs_contract_write_file("/tmp/vfs-mntns-parent-source/file", "parent") != 0 ||
        vfs_contract_write_file("/tmp/vfs-mntns-target/file", "target") != 0) {
        goto out;
    }

    child = task_create_child_impl(parent);
    if (!child) {
        goto out;
    }

    if (mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }

    set_current(child);
    if (vfs_contract_read_file_exact("/tmp/vfs-mntns-target/file", "parent") != 0) {
        goto out;
    }

    ret = 0;

out:
    set_current(parent);
    if (child) {
        task_unlink_child_impl(parent, child);
        free_task(child);
    }
    vfs_contract_cleanup_mount_namespace_paths();
    return ret;
}

int vfs_contract_mount_namespace_unshare_isolates_child_mounts(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
    int ret = -1;

    if (!parent || !parent->fs) {
        errno = ESRCH;
        return -1;
    }

    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-child-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0) {
        goto out;
    }
    if (vfs_contract_write_file("/tmp/vfs-mntns-parent-source/file", "parent") != 0 ||
        vfs_contract_write_file("/tmp/vfs-mntns-child-source/file", "child") != 0 ||
        vfs_contract_write_file("/tmp/vfs-mntns-target/file", "target") != 0) {
        goto out;
    }

    child = task_create_child_impl(parent);
    if (!child) {
        goto out;
    }
    if (fs_unshare_mount_namespace(child->fs) != 0) {
        goto out;
    }

    set_current(child);
    if (mount("/tmp/vfs-mntns-child-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }
    if (vfs_contract_read_file_exact("/tmp/vfs-mntns-target/file", "child") != 0) {
        goto out;
    }

    set_current(parent);
    if (vfs_contract_read_file_exact("/tmp/vfs-mntns-target/file", "target") != 0) {
        goto out;
    }

    ret = 0;

out:
    if (child) {
        set_current(child);
        umount("/tmp/vfs-mntns-target");
        set_current(parent);
        task_unlink_child_impl(parent, child);
        free_task(child);
    } else {
        set_current(parent);
    }
    vfs_contract_cleanup_mount_namespace_paths();
    return ret;
}
