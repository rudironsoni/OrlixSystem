/* IXLandSystemTests/VFSAtContract.c
 * C translation unit for VFS AT_* flag Linux UAPI contract tests.
 *
 * Compiled in a Linux-UAPI-clean context.
 * Uses canonical Linux names directly.
 */

#include <asm-generic/errno.h>
#include <linux/fcntl.h>

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
extern int mkdir_impl(const char *pathname, linux_mode_t mode);
extern int open_impl(const char *pathname, int flags, linux_mode_t mode);
extern int close_impl(int fd);
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
