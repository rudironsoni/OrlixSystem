/* IXLandSystemTests/VFSAtContract.c
 * C translation unit for VFS AT_* flag Linux UAPI contract tests.
 *
 * Compiled in a Linux-UAPI-clean context.
 * Uses canonical Linux names directly.
 */

#include <linux/fcntl.h>
#include <linux/capability.h>
#include <linux/magic.h>
#include <linux/mount.h>
#include <linux/stat.h>
#include <asm/statfs.h>

#include <errno.h>
#include <string.h>

#include "fs/vfs.h"
#include "kernel/cred_internal.h"
#include "kernel/task.h"

/* Access mode constants - defined locally to avoid Darwin <unistd.h> */
#ifndef X_OK
#define X_OK 1
#endif
#ifndef F_OK
#define F_OK 0
#endif

extern int chroot(const char *path);
extern int chdir(const char *path);
extern int fchdir(int fd);
extern char *getcwd(char *buf, size_t size);
extern int access(const char *pathname, int mode);
extern int mount(const char *source, const char *target, const char *filesystemtype,
                 unsigned long mountflags, const void *data);
extern int umount(const char *target);
extern int mkdir_impl(const char *pathname, linux_mode_t mode);
extern int open_impl(const char *pathname, int flags, linux_mode_t mode);

int vfs_path_contract_open_tmp_fd_symlink_file(void) {
    return open_impl("/tmp/test_fd_symlink", O_CREAT | O_RDWR, 0644);
}
extern int open_impl(const char *pathname, int flags, linux_mode_t mode);
extern int openat_impl(int dirfd, const char *pathname, int flags, linux_mode_t mode);
extern int close_impl(int fd);
extern long read_impl(int fd, void *buf, size_t count);
extern long write_impl(int fd, const void *buf, size_t count);
extern int unlink_impl(const char *pathname);
extern int rmdir_impl(const char *pathname);
extern int fstat_impl(int fd, struct linux_stat *statbuf);
extern int setuid_impl(uid_t uid);
extern int setgroups_impl(int size, const gid_t *list);
extern int mkdirat(int dirfd, const char *pathname, linux_mode_t mode);
extern int unlinkat(int dirfd, const char *pathname, int flags);
extern int linkat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, int flags);
extern int symlinkat(const char *target, int newdirfd, const char *linkpath);
extern long readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz);
extern int renameat2(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, unsigned int flags);
extern void cred_reset_to_defaults(void);
extern int chmod(const char *pathname, linux_mode_t mode);
extern int fchmod(int fd, linux_mode_t mode);
extern int chown(const char *pathname, uid_t owner, gid_t group);
extern int fchown(int fd, uid_t owner, gid_t group);
extern int capget(cap_user_header_t header, cap_user_data_t data);
extern int capset(cap_user_header_t header, const cap_user_data_t data);
extern int statfs(const char *path, struct statfs *buf);
extern int fstatfs(int fd, struct statfs *buf);

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

static int vfs_contract_read_proc_file(const char *path, char *buf, size_t buf_len) {
    int fd;
    long nread;

    if (!path || !buf || buf_len == 0) {
        errno = EINVAL;
        return -1;
    }

    fd = open_impl(path, O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }

    nread = read_impl(fd, buf, buf_len - 1);
    {
        int saved_errno = errno;
        close_impl(fd);
        errno = saved_errno;
    }
    if (nread < 0) {
        return -1;
    }
    buf[nread] = '\0';
    return 0;
}

static int vfs_contract_content_contains(const char *content, const char *needle) {
    size_t content_len;
    size_t needle_len;

    if (!content || !needle) {
        return 0;
    }

    content_len = __builtin_strlen(content);
    needle_len = __builtin_strlen(needle);
    if (needle_len == 0) {
        return 1;
    }
    if (needle_len > content_len) {
        return 0;
    }

    for (size_t i = 0; i <= content_len - needle_len; i++) {
        if (__builtin_memcmp(content + i, needle, needle_len) == 0) {
            return 1;
        }
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
    unlink_impl("/tmp/vfs-mntns-parent-source/newfile");
    unlink_impl("/tmp/vfs-mntns-child-source/file");
    unlink_impl("/tmp/vfs-mntns-target/file");
    unlink_impl("/tmp/vfs-mntns-target/newfile");
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

int vfs_contract_faccessat_follows_absolute_symlink_as_virtual_path(void) {
    int ret = -1;

    unlink_impl("/tmp/vfs-access-absolute-link/link");
    unlink_impl("/tmp/vfs-access-absolute-link/target");
    rmdir_impl("/tmp/vfs-access-absolute-link");
    if (mkdir_impl("/tmp/vfs-access-absolute-link", 0700) != 0) {
        return -1;
    }
    if (vfs_contract_write_file("/tmp/vfs-access-absolute-link/target", "access") != 0) {
        goto out;
    }
    if (symlinkat("/tmp/vfs-access-absolute-link/target", AT_FDCWD,
                  "/tmp/vfs-access-absolute-link/link") != 0) {
        goto out;
    }
    if (vfs_faccessat(AT_FDCWD, "/tmp/vfs-access-absolute-link/link", F_OK, 0) != 0 ||
        access("/tmp/vfs-access-absolute-link/link", F_OK) != 0) {
        goto out;
    }
    ret = 0;

out:
    {
        int saved_errno = errno;
        unlink_impl("/tmp/vfs-access-absolute-link/link");
        unlink_impl("/tmp/vfs-access-absolute-link/target");
        rmdir_impl("/tmp/vfs-access-absolute-link");
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_faccessat_symlink_loop_returns_eloop(void) {
    int ret = -1;

    unlink_impl("/tmp/vfs-access-loop/a");
    unlink_impl("/tmp/vfs-access-loop/b");
    rmdir_impl("/tmp/vfs-access-loop");
    if (mkdir_impl("/tmp/vfs-access-loop", 0700) != 0) {
        return -1;
    }
    if (symlinkat("b", AT_FDCWD, "/tmp/vfs-access-loop/a") != 0 ||
        symlinkat("a", AT_FDCWD, "/tmp/vfs-access-loop/b") != 0) {
        goto out;
    }

    errno = 0;
    if (vfs_faccessat(AT_FDCWD, "/tmp/vfs-access-loop/a", F_OK, 0) != -ELOOP) {
        errno = EIO;
        goto out;
    }
    errno = 0;
    if (access("/tmp/vfs-access-loop/a", F_OK) != -1 || errno != ELOOP) {
        errno = EIO;
        goto out;
    }
    ret = 0;

out:
    {
        int saved_errno = errno;
        unlink_impl("/tmp/vfs-access-loop/a");
        unlink_impl("/tmp/vfs-access-loop/b");
        rmdir_impl("/tmp/vfs-access-loop");
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_open_nofollow_rejects_symlink_with_eloop(void) {
    int fd;

    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-open-nofollow/link");
    unlink_impl("/tmp/vfs-open-nofollow/target");
    rmdir_impl("/tmp/vfs-open-nofollow");
    if (mkdir_impl("/tmp/vfs-open-nofollow", 0700) != 0) {
        return -1;
    }
    if (vfs_contract_write_file("/tmp/vfs-open-nofollow/target", "target") != 0) {
        goto fail;
    }
    if (symlinkat("target", AT_FDCWD, "/tmp/vfs-open-nofollow/link") != 0) {
        goto fail;
    }

    errno = 0;
    fd = open_impl("/tmp/vfs-open-nofollow/link", O_RDONLY | O_NOFOLLOW, 0);
    if (fd >= 0) {
        close_impl(fd);
        errno = EIO;
        goto fail;
    }
    if (errno != ELOOP) {
        goto fail;
    }

    unlink_impl("/tmp/vfs-open-nofollow/link");
    unlink_impl("/tmp/vfs-open-nofollow/target");
    rmdir_impl("/tmp/vfs-open-nofollow");
    return 0;

fail:
    {
        int saved_errno = errno;
        unlink_impl("/tmp/vfs-open-nofollow/link");
        unlink_impl("/tmp/vfs-open-nofollow/target");
        rmdir_impl("/tmp/vfs-open-nofollow");
        errno = saved_errno;
    }
    return -1;
}

int vfs_contract_openat_nofollow_rejects_dirfd_symlink_with_eloop(void) {
    int dirfd = -1;
    int fd;

    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-openat-nofollow/link");
    unlink_impl("/tmp/vfs-openat-nofollow/target");
    rmdir_impl("/tmp/vfs-openat-nofollow");
    if (mkdir_impl("/tmp/vfs-openat-nofollow", 0700) != 0) {
        return -1;
    }
    if (vfs_contract_write_file("/tmp/vfs-openat-nofollow/target", "target") != 0) {
        goto fail;
    }
    dirfd = open_impl("/tmp/vfs-openat-nofollow", O_RDONLY | O_DIRECTORY, 0);
    if (dirfd < 0) {
        goto fail;
    }
    if (symlinkat("target", dirfd, "link") != 0) {
        goto fail;
    }

    errno = 0;
    fd = openat_impl(dirfd, "link", O_RDONLY | O_NOFOLLOW, 0);
    if (fd >= 0) {
        close_impl(fd);
        errno = EIO;
        goto fail;
    }
    if (errno != ELOOP) {
        goto fail;
    }

    close_impl(dirfd);
    unlink_impl("/tmp/vfs-openat-nofollow/link");
    unlink_impl("/tmp/vfs-openat-nofollow/target");
    rmdir_impl("/tmp/vfs-openat-nofollow");
    return 0;

fail:
    {
        int saved_errno = errno;
        close_impl(dirfd);
        unlink_impl("/tmp/vfs-openat-nofollow/link");
        unlink_impl("/tmp/vfs-openat-nofollow/target");
        rmdir_impl("/tmp/vfs-openat-nofollow");
        errno = saved_errno;
    }
    return -1;
}

int vfs_contract_open_follows_symlink_to_file(void) {
    int ret = -1;

    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-open-follow/link");
    unlink_impl("/tmp/vfs-open-follow/target");
    rmdir_impl("/tmp/vfs-open-follow");
    if (mkdir_impl("/tmp/vfs-open-follow", 0700) != 0) {
        return -1;
    }
    if (vfs_contract_write_file("/tmp/vfs-open-follow/target", "followed") != 0) {
        goto out;
    }
    if (symlinkat("target", AT_FDCWD, "/tmp/vfs-open-follow/link") != 0) {
        goto out;
    }
    ret = vfs_contract_read_file_exact("/tmp/vfs-open-follow/link", "followed");

out:
    {
        int saved_errno = errno;
        unlink_impl("/tmp/vfs-open-follow/link");
        unlink_impl("/tmp/vfs-open-follow/target");
        rmdir_impl("/tmp/vfs-open-follow");
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_open_follows_absolute_symlink_as_virtual_path(void) {
    int ret = -1;

    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-open-absolute-link/link");
    unlink_impl("/tmp/vfs-open-absolute-link/target");
    rmdir_impl("/tmp/vfs-open-absolute-link");
    if (mkdir_impl("/tmp/vfs-open-absolute-link", 0700) != 0) {
        return -1;
    }
    if (vfs_contract_write_file("/tmp/vfs-open-absolute-link/target", "absolute") != 0) {
        goto out;
    }
    if (symlinkat("/tmp/vfs-open-absolute-link/target", AT_FDCWD,
                  "/tmp/vfs-open-absolute-link/link") != 0) {
        goto out;
    }
    ret = vfs_contract_read_file_exact("/tmp/vfs-open-absolute-link/link", "absolute");

out:
    {
        int saved_errno = errno;
        unlink_impl("/tmp/vfs-open-absolute-link/link");
        unlink_impl("/tmp/vfs-open-absolute-link/target");
        rmdir_impl("/tmp/vfs-open-absolute-link");
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_open_resolves_intermediate_symlink_directory(void) {
    int ret = -1;

    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-open-intermediate/link/file");
    unlink_impl("/tmp/vfs-open-intermediate/link");
    unlink_impl("/tmp/vfs-open-intermediate/real/file");
    rmdir_impl("/tmp/vfs-open-intermediate/real");
    rmdir_impl("/tmp/vfs-open-intermediate");
    if (mkdir_impl("/tmp/vfs-open-intermediate", 0700) != 0) {
        return -1;
    }
    if (mkdir_impl("/tmp/vfs-open-intermediate/real", 0700) != 0) {
        goto out;
    }
    if (vfs_contract_write_file("/tmp/vfs-open-intermediate/real/file", "middle") != 0) {
        goto out;
    }
    if (symlinkat("real", AT_FDCWD, "/tmp/vfs-open-intermediate/link") != 0) {
        goto out;
    }
    ret = vfs_contract_read_file_exact("/tmp/vfs-open-intermediate/link/file", "middle");

out:
    {
        int saved_errno = errno;
        unlink_impl("/tmp/vfs-open-intermediate/link");
        unlink_impl("/tmp/vfs-open-intermediate/real/file");
        rmdir_impl("/tmp/vfs-open-intermediate/real");
        rmdir_impl("/tmp/vfs-open-intermediate");
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_open_symlink_loop_returns_eloop(void) {
    int fd;

    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-open-loop/a");
    unlink_impl("/tmp/vfs-open-loop/b");
    rmdir_impl("/tmp/vfs-open-loop");
    if (mkdir_impl("/tmp/vfs-open-loop", 0700) != 0) {
        return -1;
    }
    if (symlinkat("b", AT_FDCWD, "/tmp/vfs-open-loop/a") != 0) {
        goto fail;
    }
    if (symlinkat("a", AT_FDCWD, "/tmp/vfs-open-loop/b") != 0) {
        goto fail;
    }

    errno = 0;
    fd = open_impl("/tmp/vfs-open-loop/a", O_RDONLY, 0);
    if (fd >= 0) {
        close_impl(fd);
        errno = EIO;
        goto fail;
    }
    if (errno != ELOOP) {
        goto fail;
    }

    unlink_impl("/tmp/vfs-open-loop/a");
    unlink_impl("/tmp/vfs-open-loop/b");
    rmdir_impl("/tmp/vfs-open-loop");
    return 0;

fail:
    {
        int saved_errno = errno;
        unlink_impl("/tmp/vfs-open-loop/a");
        unlink_impl("/tmp/vfs-open-loop/b");
        rmdir_impl("/tmp/vfs-open-loop");
        errno = saved_errno;
    }
    return -1;
}

int vfs_contract_chdir_resolves_symlink_directory(void) {
    struct task_struct *task = get_current();
    char old_pwd[MAX_PATH];
    char cwd[MAX_PATH];
    int ret = -1;

    if (!task || !task->fs) {
        errno = ESRCH;
        return -1;
    }

    __builtin_memcpy(old_pwd, task->fs->pwd_path, sizeof(old_pwd));
    unlink_impl("/tmp/vfs-chdir-symlink/link");
    rmdir_impl("/tmp/vfs-chdir-symlink/real");
    rmdir_impl("/tmp/vfs-chdir-symlink");
    if (mkdir_impl("/tmp/vfs-chdir-symlink", 0700) != 0) {
        goto out;
    }
    if (mkdir_impl("/tmp/vfs-chdir-symlink/real", 0700) != 0) {
        goto out;
    }
    if (symlinkat("real", AT_FDCWD, "/tmp/vfs-chdir-symlink/link") != 0) {
        goto out;
    }
    if (chdir("/tmp/vfs-chdir-symlink/link") != 0) {
        goto out;
    }
    if (!getcwd(cwd, sizeof(cwd)) || __builtin_strcmp(cwd, "/tmp/vfs-chdir-symlink/real") != 0) {
        errno = EIO;
        goto out;
    }
    ret = 0;

out:
    if (task && task->fs) {
        fs_set_pwd(task->fs, old_pwd);
    }
    unlink_impl("/tmp/vfs-chdir-symlink/link");
    rmdir_impl("/tmp/vfs-chdir-symlink/real");
    rmdir_impl("/tmp/vfs-chdir-symlink");
    return ret;
}

int vfs_contract_mkdirat_resolves_intermediate_symlink_directory(void) {
    int ret = -1;

    unlink_impl("/tmp/vfs-mkdirat-symlink/link/created");
    unlink_impl("/tmp/vfs-mkdirat-symlink/link");
    rmdir_impl("/tmp/vfs-mkdirat-symlink/real/created");
    rmdir_impl("/tmp/vfs-mkdirat-symlink/real");
    rmdir_impl("/tmp/vfs-mkdirat-symlink");
    if (mkdir_impl("/tmp/vfs-mkdirat-symlink", 0700) != 0) {
        goto out;
    }
    if (mkdir_impl("/tmp/vfs-mkdirat-symlink/real", 0700) != 0) {
        goto out;
    }
    if (symlinkat("real", AT_FDCWD, "/tmp/vfs-mkdirat-symlink/link") != 0) {
        goto out;
    }
    if (mkdirat(AT_FDCWD, "/tmp/vfs-mkdirat-symlink/link/created", 0700) != 0) {
        goto out;
    }
    if (vfs_fstatat(AT_FDCWD, "/tmp/vfs-mkdirat-symlink/real/created", &(struct linux_stat){0}, 0) != 0) {
        goto out;
    }
    ret = 0;

out:
    unlink_impl("/tmp/vfs-mkdirat-symlink/link");
    rmdir_impl("/tmp/vfs-mkdirat-symlink/real/created");
    rmdir_impl("/tmp/vfs-mkdirat-symlink/real");
    rmdir_impl("/tmp/vfs-mkdirat-symlink");
    return ret;
}

int vfs_contract_unlinkat_resolves_intermediate_symlink_directory(void) {
    int ret = -1;

    unlink_impl("/tmp/vfs-unlinkat-symlink/link/file");
    unlink_impl("/tmp/vfs-unlinkat-symlink/link");
    unlink_impl("/tmp/vfs-unlinkat-symlink/real/file");
    rmdir_impl("/tmp/vfs-unlinkat-symlink/real");
    rmdir_impl("/tmp/vfs-unlinkat-symlink");
    if (mkdir_impl("/tmp/vfs-unlinkat-symlink", 0700) != 0) {
        goto out;
    }
    if (mkdir_impl("/tmp/vfs-unlinkat-symlink/real", 0700) != 0) {
        goto out;
    }
    if (vfs_contract_write_file("/tmp/vfs-unlinkat-symlink/real/file", "unlink") != 0) {
        goto out;
    }
    if (symlinkat("real", AT_FDCWD, "/tmp/vfs-unlinkat-symlink/link") != 0) {
        goto out;
    }
    if (unlinkat(AT_FDCWD, "/tmp/vfs-unlinkat-symlink/link/file", 0) != 0) {
        goto out;
    }
    errno = 0;
    if (open_impl("/tmp/vfs-unlinkat-symlink/real/file", O_RDONLY, 0) != -1 || errno != ENOENT) {
        errno = EIO;
        goto out;
    }
    ret = 0;

out:
    unlink_impl("/tmp/vfs-unlinkat-symlink/link");
    unlink_impl("/tmp/vfs-unlinkat-symlink/real/file");
    rmdir_impl("/tmp/vfs-unlinkat-symlink/real");
    rmdir_impl("/tmp/vfs-unlinkat-symlink");
    return ret;
}

int vfs_contract_renameat_resolves_intermediate_symlink_directories(void) {
    int ret = -1;

    unlink_impl("/tmp/vfs-renameat-symlink/src-link/file");
    unlink_impl("/tmp/vfs-renameat-symlink/dst-link/file");
    unlink_impl("/tmp/vfs-renameat-symlink/src-link");
    unlink_impl("/tmp/vfs-renameat-symlink/dst-link");
    unlink_impl("/tmp/vfs-renameat-symlink/src-real/file");
    unlink_impl("/tmp/vfs-renameat-symlink/dst-real/file");
    rmdir_impl("/tmp/vfs-renameat-symlink/src-real");
    rmdir_impl("/tmp/vfs-renameat-symlink/dst-real");
    rmdir_impl("/tmp/vfs-renameat-symlink");
    if (mkdir_impl("/tmp/vfs-renameat-symlink", 0700) != 0 ||
        mkdir_impl("/tmp/vfs-renameat-symlink/src-real", 0700) != 0 ||
        mkdir_impl("/tmp/vfs-renameat-symlink/dst-real", 0700) != 0) {
        goto out;
    }
    if (vfs_contract_write_file("/tmp/vfs-renameat-symlink/src-real/file", "renamed") != 0) {
        goto out;
    }
    if (symlinkat("src-real", AT_FDCWD, "/tmp/vfs-renameat-symlink/src-link") != 0 ||
        symlinkat("dst-real", AT_FDCWD, "/tmp/vfs-renameat-symlink/dst-link") != 0) {
        goto out;
    }
    if (renameat2(AT_FDCWD, "/tmp/vfs-renameat-symlink/src-link/file",
                  AT_FDCWD, "/tmp/vfs-renameat-symlink/dst-link/file", 0) != 0) {
        goto out;
    }
    if (vfs_contract_read_file_exact("/tmp/vfs-renameat-symlink/dst-real/file", "renamed") != 0) {
        goto out;
    }
    ret = 0;

out:
    unlink_impl("/tmp/vfs-renameat-symlink/src-link");
    unlink_impl("/tmp/vfs-renameat-symlink/dst-link");
    unlink_impl("/tmp/vfs-renameat-symlink/src-real/file");
    unlink_impl("/tmp/vfs-renameat-symlink/dst-real/file");
    rmdir_impl("/tmp/vfs-renameat-symlink/src-real");
    rmdir_impl("/tmp/vfs-renameat-symlink/dst-real");
    rmdir_impl("/tmp/vfs-renameat-symlink");
    return ret;
}

int vfs_contract_linkat_respects_symlink_follow_flag(void) {
    struct linux_stat st;
    int ret = -1;

    unlink_impl("/tmp/vfs-linkat-follow/hard-target");
    unlink_impl("/tmp/vfs-linkat-follow/hard-link");
    unlink_impl("/tmp/vfs-linkat-follow/link");
    unlink_impl("/tmp/vfs-linkat-follow/target");
    rmdir_impl("/tmp/vfs-linkat-follow");
    if (mkdir_impl("/tmp/vfs-linkat-follow", 0700) != 0) {
        goto out;
    }
    if (vfs_contract_write_file("/tmp/vfs-linkat-follow/target", "target") != 0) {
        goto out;
    }
    if (symlinkat("target", AT_FDCWD, "/tmp/vfs-linkat-follow/link") != 0) {
        goto out;
    }
    if (linkat(AT_FDCWD, "/tmp/vfs-linkat-follow/link", AT_FDCWD,
               "/tmp/vfs-linkat-follow/hard-link", 0) != 0) {
        goto out;
    }
    if (vfs_fstatat(AT_FDCWD, "/tmp/vfs-linkat-follow/hard-link", &st, AT_SYMLINK_NOFOLLOW) != 0 ||
        (st.st_mode & S_IFMT) != S_IFLNK) {
        errno = EIO;
        goto out;
    }
    if (linkat(AT_FDCWD, "/tmp/vfs-linkat-follow/link", AT_FDCWD,
               "/tmp/vfs-linkat-follow/hard-target", AT_SYMLINK_FOLLOW) != 0) {
        goto out;
    }
    if (vfs_contract_read_file_exact("/tmp/vfs-linkat-follow/hard-target", "target") != 0) {
        goto out;
    }
    ret = 0;

out:
    unlink_impl("/tmp/vfs-linkat-follow/hard-target");
    unlink_impl("/tmp/vfs-linkat-follow/hard-link");
    unlink_impl("/tmp/vfs-linkat-follow/link");
    unlink_impl("/tmp/vfs-linkat-follow/target");
    rmdir_impl("/tmp/vfs-linkat-follow");
    return ret;
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

int vfs_contract_nonroot_cannot_chroot(void) {
    struct task_struct *task = get_current();
    char old_root[MAX_PATH];
    char old_pwd[MAX_PATH];
    int ret = -1;

    if (!task || !task->fs) {
        errno = ESRCH;
        return -1;
    }

    cred_reset_to_defaults();
    __builtin_memcpy(old_root, task->fs->root_path, sizeof(old_root));
    __builtin_memcpy(old_pwd, task->fs->pwd_path, sizeof(old_pwd));
    fs_set_root(task->fs, "/");
    fs_set_pwd(task->fs, "/");

    rmdir_impl("/tmp/vfs-chroot-cred-root");
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-chroot-cred-root", 0700)) != 0) {
        goto out;
    }
    if (setuid_impl(1000) != 0) {
        goto out;
    }

    errno = 0;
    if (chroot("/tmp/vfs-chroot-cred-root") != -1 || errno != EPERM) {
        errno = EPERM;
        goto out;
    }

    ret = 0;

out:
    cred_reset_to_defaults();
    vfs_contract_restore_fs(task->fs, old_root, old_pwd);
    rmdir_impl("/tmp/vfs-chroot-cred-root");
    return ret;
}

int vfs_contract_root_without_sys_chroot_cannot_chroot(void) {
    struct __user_cap_header_struct header = {
        .version = _LINUX_CAPABILITY_VERSION_3,
        .pid = 0,
    };
    struct __user_cap_data_struct data[_LINUX_CAPABILITY_U32S_3];
    struct task_struct *task = get_current();
    char old_root[MAX_PATH];
    char old_pwd[MAX_PATH];
    int ret = -1;

    if (!task || !task->fs) {
        errno = ESRCH;
        return -1;
    }

    cred_reset_to_defaults();
    __builtin_memcpy(old_root, task->fs->root_path, sizeof(old_root));
    __builtin_memcpy(old_pwd, task->fs->pwd_path, sizeof(old_pwd));
    fs_set_root(task->fs, "/");
    fs_set_pwd(task->fs, "/");

    rmdir_impl("/tmp/vfs-chroot-cred-root");
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-chroot-cred-root", 0700)) != 0) {
        goto out;
    }
    if (capget(&header, data) != 0) {
        goto out;
    }
    data[CAP_SYS_CHROOT / 32].effective &= ~(1U << (CAP_SYS_CHROOT % 32));
    if (capset(&header, data) != 0) {
        goto out;
    }

    errno = 0;
    if (chroot("/tmp/vfs-chroot-cred-root") != -1 || errno != EPERM) {
        errno = EPERM;
        goto out;
    }

    ret = 0;

out:
    cred_reset_to_defaults();
    vfs_contract_restore_fs(task->fs, old_root, old_pwd);
    rmdir_impl("/tmp/vfs-chroot-cred-root");
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

int vfs_contract_proc_self_mountinfo_lists_bind_mount(void) {
    char content[4096];
    int ret = -1;

    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0) {
        goto out;
    }
    if (vfs_contract_write_file("/tmp/vfs-mntns-parent-source/file", "parent") != 0 ||
        vfs_contract_write_file("/tmp/vfs-mntns-target/file", "target") != 0) {
        goto out;
    }
    if (mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }
    if (vfs_contract_read_proc_file("/proc/self/mountinfo", content, sizeof(content)) != 0) {
        goto out;
    }
    if (!vfs_contract_content_contains(content, "/tmp/vfs-mntns-parent-source") ||
        !vfs_contract_content_contains(content, "/tmp/vfs-mntns-target") ||
        !vfs_contract_content_contains(content, " rw,bind\n")) {
        errno = ENODATA;
        goto out;
    }

    ret = 0;

out:
    {
        int saved_errno = errno;
        vfs_contract_cleanup_mount_namespace_paths();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_proc_self_mountinfo_uses_linux_shaped_optional_fields(void) {
    char content[4096];
    int ret = -1;

    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0) {
        goto out;
    }
    if (mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }
    if (vfs_contract_read_proc_file("/proc/self/mountinfo", content, sizeof(content)) != 0) {
        goto out;
    }
    if (!vfs_contract_content_contains(content, "1 0 0:1 / / rw,relatime - ixland-root ixland-root rw\n") ||
        !vfs_contract_content_contains(content, " /tmp/vfs-mntns-target rw,relatime - none /tmp/vfs-mntns-parent-source rw,bind\n")) {
        errno = ENODATA;
        goto out;
    }

    ret = 0;

out:
    {
        int saved_errno = errno;
        vfs_contract_cleanup_mount_namespace_paths();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_proc_self_mountinfo_reports_shared_propagation(void) {
    char content[4096];
    int ret = -1;

    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0) {
        goto out;
    }
    if (mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0 ||
        mount(NULL, "/tmp/vfs-mntns-target", NULL, MS_BIND | MS_REMOUNT | MS_SHARED, NULL) != 0) {
        goto out;
    }
    if (vfs_contract_read_proc_file("/proc/self/mountinfo", content, sizeof(content)) != 0) {
        goto out;
    }
    if (!vfs_contract_content_contains(content, " /tmp/vfs-mntns-target rw,relatime shared:")) {
        errno = ENODATA;
        goto out;
    }

    ret = 0;

out:
    {
        int saved_errno = errno;
        vfs_contract_cleanup_mount_namespace_paths();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_proc_self_mountinfo_reports_slave_private_and_unbindable_propagation(void) {
    char content[4096];
    int ret = -1;

    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0) {
        goto out;
    }
    if (mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0 ||
        mount(NULL, "/tmp/vfs-mntns-target", NULL, MS_BIND | MS_REMOUNT | MS_SLAVE, NULL) != 0) {
        goto out;
    }
    if (vfs_contract_read_proc_file("/proc/self/mountinfo", content, sizeof(content)) != 0 ||
        !vfs_contract_content_contains(content, " /tmp/vfs-mntns-target rw,relatime master:")) {
        errno = ENODATA;
        goto out;
    }

    if (mount(NULL, "/tmp/vfs-mntns-target", NULL, MS_BIND | MS_REMOUNT | MS_PRIVATE, NULL) != 0) {
        goto out;
    }
    if (vfs_contract_read_proc_file("/proc/self/mountinfo", content, sizeof(content)) != 0 ||
        !vfs_contract_content_contains(content, " /tmp/vfs-mntns-target rw,relatime - none ") ||
        vfs_contract_content_contains(content, " shared:") ||
        vfs_contract_content_contains(content, " master:") ||
        vfs_contract_content_contains(content, " unbindable")) {
        errno = ENODATA;
        goto out;
    }

    if (mount(NULL, "/tmp/vfs-mntns-target", NULL, MS_BIND | MS_REMOUNT | MS_UNBINDABLE, NULL) != 0) {
        goto out;
    }
    if (vfs_contract_read_proc_file("/proc/self/mountinfo", content, sizeof(content)) != 0 ||
        !vfs_contract_content_contains(content, " /tmp/vfs-mntns-target rw,relatime unbindable")) {
        errno = ENODATA;
        goto out;
    }

    ret = 0;

out:
    {
        int saved_errno = errno;
        vfs_contract_cleanup_mount_namespace_paths();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_mount_rejects_multiple_propagation_flags(void) {
    int ret = -1;

    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0) {
        goto out;
    }
    if (mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-target", NULL,
              MS_BIND | MS_SHARED | MS_PRIVATE, NULL) != -1 || errno != EINVAL) {
        errno = ENODATA;
        goto out;
    }

    ret = 0;

out:
    {
        int saved_errno = errno;
        vfs_contract_cleanup_mount_namespace_paths();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_proc_self_mounts_lists_bind_mount(void) {
    char content[4096];
    int ret = -1;

    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0) {
        goto out;
    }
    if (vfs_contract_write_file("/tmp/vfs-mntns-parent-source/file", "parent") != 0 ||
        vfs_contract_write_file("/tmp/vfs-mntns-target/file", "target") != 0) {
        goto out;
    }
    if (mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }
    if (vfs_contract_read_proc_file("/proc/self/mounts", content, sizeof(content)) != 0) {
        goto out;
    }
    if (!vfs_contract_content_contains(content, "/tmp/vfs-mntns-parent-source /tmp/vfs-mntns-target none rw,bind 0 0\n")) {
        errno = ENODATA;
        goto out;
    }

    ret = 0;

out:
    {
        int saved_errno = errno;
        vfs_contract_cleanup_mount_namespace_paths();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_bind_mount_remount_readonly_rejects_writes(void) {
    int ret = -1;

    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0) {
        goto out;
    }
    if (vfs_contract_write_file("/tmp/vfs-mntns-parent-source/file", "parent") != 0 ||
        vfs_contract_write_file("/tmp/vfs-mntns-target/file", "target") != 0) {
        goto out;
    }
    if (mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }
    if (mount(NULL, "/tmp/vfs-mntns-target", NULL, MS_BIND | MS_REMOUNT | MS_RDONLY, NULL) != 0) {
        goto out;
    }

    errno = 0;
    if (vfs_contract_write_file("/tmp/vfs-mntns-target/file", "blocked") == 0 || errno != EROFS) {
        errno = EROFS;
        goto out;
    }

    if (vfs_contract_read_file_exact("/tmp/vfs-mntns-target/file", "parent") != 0) {
        goto out;
    }

    ret = 0;

out:
    vfs_contract_cleanup_mount_namespace_paths();
    return ret;
}

int vfs_contract_bind_mount_remount_readwrite_permits_writes(void) {
    int ret = -1;

    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0) {
        goto out;
    }
    if (vfs_contract_write_file("/tmp/vfs-mntns-parent-source/file", "parent") != 0 ||
        vfs_contract_write_file("/tmp/vfs-mntns-target/file", "target") != 0) {
        goto out;
    }
    if (mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0 ||
        mount(NULL, "/tmp/vfs-mntns-target", NULL, MS_BIND | MS_REMOUNT | MS_RDONLY, NULL) != 0 ||
        mount(NULL, "/tmp/vfs-mntns-target", NULL, MS_BIND | MS_REMOUNT, NULL) != 0) {
        goto out;
    }
    if (vfs_contract_write_file("/tmp/vfs-mntns-target/newfile", "updated") != 0) {
        goto out;
    }
    if (vfs_contract_read_file_exact("/tmp/vfs-mntns-target/newfile", "updated") != 0) {
        goto out;
    }

    ret = 0;

out:
    {
        int saved_errno = errno;
        vfs_contract_cleanup_mount_namespace_paths();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_proc_self_mountinfo_reports_readonly_remount(void) {
    char content[4096];
    int ret = -1;

    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0) {
        goto out;
    }
    if (vfs_contract_write_file("/tmp/vfs-mntns-parent-source/file", "parent") != 0 ||
        vfs_contract_write_file("/tmp/vfs-mntns-target/file", "target") != 0) {
        goto out;
    }
    if (mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0 ||
        mount(NULL, "/tmp/vfs-mntns-target", NULL, MS_BIND | MS_REMOUNT | MS_RDONLY, NULL) != 0) {
        goto out;
    }
    if (vfs_contract_read_proc_file("/proc/self/mountinfo", content, sizeof(content)) != 0) {
        goto out;
    }
    if (!vfs_contract_content_contains(content, "/tmp/vfs-mntns-parent-source") ||
        !vfs_contract_content_contains(content, "/tmp/vfs-mntns-target") ||
        !vfs_contract_content_contains(content, " ro,relatime - none ") ||
        !vfs_contract_content_contains(content, " ro,bind\n")) {
        errno = ENODATA;
        goto out;
    }

    ret = 0;

out:
    vfs_contract_cleanup_mount_namespace_paths();
    return ret;
}

int vfs_contract_nonroot_cannot_create_bind_mount(void) {
    int ret = -1;

    cred_reset_to_defaults();
    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0) {
        goto out;
    }
    if (setuid_impl(1000) != 0) {
        goto out;
    }

    errno = 0;
    if (mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != -1 ||
        errno != EPERM) {
        errno = EPERM;
        goto out;
    }

    ret = 0;

out:
    cred_reset_to_defaults();
    vfs_contract_cleanup_mount_namespace_paths();
    return ret;
}

int vfs_contract_root_without_sys_admin_cannot_create_bind_mount(void) {
    struct __user_cap_header_struct header = {
        .version = _LINUX_CAPABILITY_VERSION_3,
        .pid = 0,
    };
    struct __user_cap_data_struct data[_LINUX_CAPABILITY_U32S_3];
    int ret = -1;

    cred_reset_to_defaults();
    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0) {
        goto out;
    }
    if (capget(&header, data) != 0) {
        goto out;
    }
    data[CAP_SYS_ADMIN / 32].effective &= ~(1U << (CAP_SYS_ADMIN % 32));
    if (capset(&header, data) != 0) {
        goto out;
    }

    errno = 0;
    if (mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != -1 ||
        errno != EPERM) {
        errno = EPERM;
        goto out;
    }

    ret = 0;

out:
    cred_reset_to_defaults();
    vfs_contract_cleanup_mount_namespace_paths();
    return ret;
}

int vfs_contract_nonroot_cannot_unmount_bind_mount(void) {
    int ret = -1;

    cred_reset_to_defaults();
    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0) {
        goto out;
    }
    if (mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }
    if (setuid_impl(1000) != 0) {
        goto out;
    }

    errno = 0;
    if (umount("/tmp/vfs-mntns-target") != -1 || errno != EPERM) {
        errno = EPERM;
        goto out;
    }

    ret = 0;

out:
    cred_reset_to_defaults();
    vfs_contract_cleanup_mount_namespace_paths();
    return ret;
}

int vfs_contract_proc_self_mountinfo_uses_current_mount_namespace(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
    char content[4096];
    int ret = -1;

    if (!parent || !parent->fs) {
        errno = ESRCH;
        return -1;
    }

    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-child-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0) {
        goto out;
    }
    if (vfs_contract_write_file("/tmp/vfs-mntns-child-source/file", "child") != 0 ||
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
    if (vfs_contract_read_proc_file("/proc/self/mountinfo", content, sizeof(content)) != 0 ||
        !vfs_contract_content_contains(content, "/tmp/vfs-mntns-child-source")) {
        errno = ENODATA;
        goto out;
    }

    set_current(parent);
    if (vfs_contract_read_proc_file("/proc/self/mountinfo", content, sizeof(content)) != 0 ||
        vfs_contract_content_contains(content, "/tmp/vfs-mntns-child-source")) {
        errno = ENODATA;
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

int vfs_contract_proc_self_mount_views_do_not_expose_host_paths(void) {
    char content[4096];
    const char *persistent_root = vfs_persistent_backing_root();

    if (!persistent_root) {
        errno = ENOENT;
        return -1;
    }
    if (vfs_contract_read_proc_file("/proc/self/mountinfo", content, sizeof(content)) != 0) {
        return -1;
    }
    if (vfs_contract_content_contains(content, persistent_root)) {
        errno = EXDEV;
        return -1;
    }
    if (vfs_contract_read_proc_file("/proc/self/mounts", content, sizeof(content)) != 0) {
        return -1;
    }
    if (vfs_contract_content_contains(content, persistent_root)) {
        errno = EXDEV;
        return -1;
    }

    return 0;
}

int vfs_contract_nonroot_cannot_read_root_private_file(void) {
    int fd;
    int ret = -1;

    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-cred-root-private");
    if (vfs_contract_write_file("/tmp/vfs-cred-root-private", "secret") != 0) {
        goto out;
    }

    if (setuid_impl(1000) != 0) {
        goto out;
    }
    errno = 0;
    fd = open_impl("/tmp/vfs-cred-root-private", O_RDONLY, 0);
    if (fd != -1 || errno != EACCES) {
        close_impl(fd);
        errno = EACCES;
        goto out;
    }

    ret = 0;

out:
    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-cred-root-private");
    return ret;
}

int vfs_contract_nonroot_can_read_other_readable_file(void) {
    int fd = -1;
    int ret = -1;

    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-cred-root-readable");
    fd = open_impl("/tmp/vfs-cred-root-readable", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        goto out;
    }
    if (write_impl(fd, "public", 6) != 6) {
        goto out;
    }
    close_impl(fd);
    fd = -1;

    if (setuid_impl(1000) != 0) {
        goto out;
    }
    if (vfs_contract_read_file_exact("/tmp/vfs-cred-root-readable", "public") != 0) {
        goto out;
    }

    ret = 0;

out:
    close_impl(fd);
    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-cred-root-readable");
    return ret;
}

int vfs_contract_nonroot_created_file_records_virtual_owner(void) {
    struct linux_stat st;
    int fd = -1;
    int ret = -1;

    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-cred-user-file");
    if (setuid_impl(1000) != 0) {
        goto out;
    }

    fd = open_impl("/tmp/vfs-cred-user-file", O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        goto out;
    }
    if (fstat_impl(fd, &st) != 0) {
        goto out;
    }
    if (st.st_uid != 1000 || (st.st_mode & 0777) != 0600) {
        errno = EIO;
        goto out;
    }
    if (write_impl(fd, "owned", 5) != 5) {
        goto out;
    }
    close_impl(fd);
    fd = -1;
    if (vfs_contract_read_file_exact("/tmp/vfs-cred-user-file", "owned") != 0) {
        goto out;
    }

    ret = 0;

out:
    close_impl(fd);
    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-cred-user-file");
    return ret;
}

int vfs_contract_nonroot_cannot_unlink_inside_root_private_dir(void) {
    int ret = -1;

    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-cred-private-dir/file");
    rmdir_impl("/tmp/vfs-cred-private-dir");
    if (mkdir_impl("/tmp/vfs-cred-private-dir", 0700) != 0) {
        goto out;
    }
    if (vfs_contract_write_file("/tmp/vfs-cred-private-dir/file", "owned") != 0) {
        goto out;
    }

    if (setuid_impl(1000) != 0) {
        goto out;
    }
    errno = 0;
    if (unlink_impl("/tmp/vfs-cred-private-dir/file") != -1 || errno != EACCES) {
        errno = EACCES;
        goto out;
    }

    ret = 0;

out:
    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-cred-private-dir/file");
    rmdir_impl("/tmp/vfs-cred-private-dir");
    return ret;
}

int vfs_contract_nonroot_cannot_mkdirat_inside_root_private_dir(void) {
    int dirfd = -1;
    int ret = -1;

    cred_reset_to_defaults();
    rmdir_impl("/tmp/vfs-cred-at-private-dir/child");
    rmdir_impl("/tmp/vfs-cred-at-private-dir");
    if (mkdir_impl("/tmp/vfs-cred-at-private-dir", 0700) != 0) {
        goto out;
    }
    dirfd = open_impl("/tmp/vfs-cred-at-private-dir", O_RDONLY | O_DIRECTORY, 0);
    if (dirfd < 0) {
        goto out;
    }
    if (setuid_impl(1000) != 0) {
        goto out;
    }

    errno = 0;
    if (mkdirat(dirfd, "child", 0700) != -1 || errno != EACCES) {
        errno = EACCES;
        goto out;
    }

    ret = 0;

out:
    close_impl(dirfd);
    cred_reset_to_defaults();
    rmdir_impl("/tmp/vfs-cred-at-private-dir/child");
    rmdir_impl("/tmp/vfs-cred-at-private-dir");
    return ret;
}

int vfs_contract_nonroot_cannot_unlinkat_inside_root_private_dir(void) {
    int dirfd = -1;
    int ret = -1;

    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-cred-at-private-dir/file");
    rmdir_impl("/tmp/vfs-cred-at-private-dir");
    if (mkdir_impl("/tmp/vfs-cred-at-private-dir", 0700) != 0) {
        goto out;
    }
    if (vfs_contract_write_file("/tmp/vfs-cred-at-private-dir/file", "owned") != 0) {
        goto out;
    }
    dirfd = open_impl("/tmp/vfs-cred-at-private-dir", O_RDONLY | O_DIRECTORY, 0);
    if (dirfd < 0) {
        goto out;
    }
    if (setuid_impl(1000) != 0) {
        goto out;
    }

    errno = 0;
    if (unlinkat(dirfd, "file", 0) != -1 || errno != EACCES) {
        errno = EACCES;
        goto out;
    }

    ret = 0;

out:
    close_impl(dirfd);
    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-cred-at-private-dir/file");
    rmdir_impl("/tmp/vfs-cred-at-private-dir");
    return ret;
}

int vfs_contract_linkat_uses_virtual_dirfds(void) {
    int dirfd = -1;
    int ret = -1;

    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-at-link-dir/source");
    unlink_impl("/tmp/vfs-at-link-dir/hardlink");
    rmdir_impl("/tmp/vfs-at-link-dir");
    if (mkdir_impl("/tmp/vfs-at-link-dir", 0700) != 0) {
        goto out;
    }
    if (vfs_contract_write_file("/tmp/vfs-at-link-dir/source", "linked") != 0) {
        goto out;
    }
    dirfd = open_impl("/tmp/vfs-at-link-dir", O_RDONLY | O_DIRECTORY, 0);
    if (dirfd < 0) {
        goto out;
    }
    if (linkat(dirfd, "source", dirfd, "hardlink", 0) != 0) {
        goto out;
    }
    if (vfs_contract_read_file_exact("/tmp/vfs-at-link-dir/hardlink", "linked") != 0) {
        goto out;
    }

    ret = 0;

out:
    close_impl(dirfd);
    unlink_impl("/tmp/vfs-at-link-dir/hardlink");
    unlink_impl("/tmp/vfs-at-link-dir/source");
    rmdir_impl("/tmp/vfs-at-link-dir");
    return ret;
}

int vfs_contract_symlinkat_and_readlinkat_use_virtual_dirfds(void) {
    char target[64];
    int dirfd = -1;
    long len;
    int ret = -1;

    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-at-symlink-dir/link");
    rmdir_impl("/tmp/vfs-at-symlink-dir");
    if (mkdir_impl("/tmp/vfs-at-symlink-dir", 0700) != 0) {
        goto out;
    }
    dirfd = open_impl("/tmp/vfs-at-symlink-dir", O_RDONLY | O_DIRECTORY, 0);
    if (dirfd < 0) {
        goto out;
    }
    if (symlinkat("/target/path", dirfd, "link") != 0) {
        goto out;
    }
    len = readlinkat(dirfd, "link", target, sizeof(target) - 1);
    if (len != 12) {
        goto out;
    }
    target[len] = '\0';
    if (__builtin_strcmp(target, "/target/path") != 0) {
        errno = EIO;
        goto out;
    }

    ret = 0;

out:
    close_impl(dirfd);
    unlink_impl("/tmp/vfs-at-symlink-dir/link");
    rmdir_impl("/tmp/vfs-at-symlink-dir");
    return ret;
}

int vfs_contract_renameat2_exchange_swaps_files_through_virtual_dirfds(void) {
    int dirfd = -1;
    int ret = -1;

    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-at-rename-dir/left");
    unlink_impl("/tmp/vfs-at-rename-dir/right");
    rmdir_impl("/tmp/vfs-at-rename-dir");
    if (mkdir_impl("/tmp/vfs-at-rename-dir", 0700) != 0) {
        goto out;
    }
    if (vfs_contract_write_file("/tmp/vfs-at-rename-dir/left", "left") != 0 ||
        vfs_contract_write_file("/tmp/vfs-at-rename-dir/right", "right") != 0) {
        goto out;
    }
    dirfd = open_impl("/tmp/vfs-at-rename-dir", O_RDONLY | O_DIRECTORY, 0);
    if (dirfd < 0) {
        goto out;
    }

    if (renameat2(dirfd, "left", dirfd, "right", AT_RENAME_EXCHANGE) != 0) {
        goto out;
    }
    if (vfs_contract_read_file_exact("/tmp/vfs-at-rename-dir/left", "right") != 0 ||
        vfs_contract_read_file_exact("/tmp/vfs-at-rename-dir/right", "left") != 0) {
        goto out;
    }

    ret = 0;

out:
    close_impl(dirfd);
    unlink_impl("/tmp/vfs-at-rename-dir/left");
    unlink_impl("/tmp/vfs-at-rename-dir/right");
    rmdir_impl("/tmp/vfs-at-rename-dir");
    return ret;
}

int vfs_contract_renameat2_exchange_swaps_virtual_metadata(void) {
    struct linux_stat left_st;
    struct linux_stat right_st;
    int dirfd = -1;
    int ret = -1;

    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-at-rename-dir/left");
    unlink_impl("/tmp/vfs-at-rename-dir/right");
    rmdir_impl("/tmp/vfs-at-rename-dir");
    if (mkdir_impl("/tmp/vfs-at-rename-dir", 0700) != 0) {
        goto out;
    }
    if (vfs_contract_write_file("/tmp/vfs-at-rename-dir/left", "left") != 0 ||
        vfs_contract_write_file("/tmp/vfs-at-rename-dir/right", "right") != 0) {
        goto out;
    }
    if (chown("/tmp/vfs-at-rename-dir/left", 1000, 1000) != 0 ||
        chown("/tmp/vfs-at-rename-dir/right", 2000, 2000) != 0) {
        goto out;
    }
    dirfd = open_impl("/tmp/vfs-at-rename-dir", O_RDONLY | O_DIRECTORY, 0);
    if (dirfd < 0) {
        goto out;
    }

    if (renameat2(dirfd, "left", dirfd, "right", AT_RENAME_EXCHANGE) != 0) {
        goto out;
    }
    if (vfs_fstatat(AT_FDCWD, "/tmp/vfs-at-rename-dir/left", &left_st, 0) != 0 ||
        vfs_fstatat(AT_FDCWD, "/tmp/vfs-at-rename-dir/right", &right_st, 0) != 0) {
        goto out;
    }
    if (left_st.st_uid != 2000 || right_st.st_uid != 1000) {
        errno = EIO;
        goto out;
    }

    ret = 0;

out:
    close_impl(dirfd);
    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-at-rename-dir/left");
    unlink_impl("/tmp/vfs-at-rename-dir/right");
    rmdir_impl("/tmp/vfs-at-rename-dir");
    return ret;
}

int vfs_contract_renameat2_noreplace_existing_target_returns_exist(void) {
    int dirfd = -1;
    int ret = -1;

    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-at-rename-dir/left");
    unlink_impl("/tmp/vfs-at-rename-dir/right");
    rmdir_impl("/tmp/vfs-at-rename-dir");
    if (mkdir_impl("/tmp/vfs-at-rename-dir", 0700) != 0) {
        goto out;
    }
    if (vfs_contract_write_file("/tmp/vfs-at-rename-dir/left", "left") != 0 ||
        vfs_contract_write_file("/tmp/vfs-at-rename-dir/right", "right") != 0) {
        goto out;
    }
    dirfd = open_impl("/tmp/vfs-at-rename-dir", O_RDONLY | O_DIRECTORY, 0);
    if (dirfd < 0) {
        goto out;
    }

    errno = 0;
    if (renameat2(dirfd, "left", dirfd, "right", AT_RENAME_NOREPLACE) != -1 || errno != EEXIST) {
        errno = EEXIST;
        goto out;
    }
    if (vfs_contract_read_file_exact("/tmp/vfs-at-rename-dir/left", "left") != 0 ||
        vfs_contract_read_file_exact("/tmp/vfs-at-rename-dir/right", "right") != 0) {
        goto out;
    }

    ret = 0;

out:
    close_impl(dirfd);
    unlink_impl("/tmp/vfs-at-rename-dir/left");
    unlink_impl("/tmp/vfs-at-rename-dir/right");
    rmdir_impl("/tmp/vfs-at-rename-dir");
    return ret;
}

int vfs_contract_renameat_overwrite_moves_virtual_metadata(void) {
    struct linux_stat st;
    int dirfd = -1;
    int ret = -1;

    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-at-rename-dir/left");
    unlink_impl("/tmp/vfs-at-rename-dir/right");
    rmdir_impl("/tmp/vfs-at-rename-dir");
    if (mkdir_impl("/tmp/vfs-at-rename-dir", 0700) != 0) {
        goto out;
    }
    if (vfs_contract_write_file("/tmp/vfs-at-rename-dir/left", "left") != 0 ||
        vfs_contract_write_file("/tmp/vfs-at-rename-dir/right", "right") != 0) {
        goto out;
    }
    if (chown("/tmp/vfs-at-rename-dir/left", 1000, 1000) != 0 ||
        chown("/tmp/vfs-at-rename-dir/right", 2000, 2000) != 0) {
        goto out;
    }
    dirfd = open_impl("/tmp/vfs-at-rename-dir", O_RDONLY | O_DIRECTORY, 0);
    if (dirfd < 0) {
        goto out;
    }

    if (renameat2(dirfd, "left", dirfd, "right", 0) != 0) {
        goto out;
    }
    if (vfs_contract_read_file_exact("/tmp/vfs-at-rename-dir/right", "left") != 0) {
        goto out;
    }
    if (vfs_fstatat(AT_FDCWD, "/tmp/vfs-at-rename-dir/right", &st, 0) != 0) {
        goto out;
    }
    if (st.st_uid != 1000 || st.st_gid != 1000) {
        errno = EIO;
        goto out;
    }

    ret = 0;

out:
    close_impl(dirfd);
    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-at-rename-dir/left");
    unlink_impl("/tmp/vfs-at-rename-dir/right");
    rmdir_impl("/tmp/vfs-at-rename-dir");
    return ret;
}

int vfs_contract_rename_directory_over_nonempty_directory_returns_notempty(void) {
    int ret = -1;

    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-rename-dst/child");
    rmdir_impl("/tmp/vfs-rename-src");
    rmdir_impl("/tmp/vfs-rename-dst");
    if (mkdir_impl("/tmp/vfs-rename-src", 0700) != 0 ||
        mkdir_impl("/tmp/vfs-rename-dst", 0700) != 0 ||
        vfs_contract_write_file("/tmp/vfs-rename-dst/child", "child") != 0) {
        goto out;
    }

    errno = 0;
    if (renameat2(AT_FDCWD, "/tmp/vfs-rename-src", AT_FDCWD, "/tmp/vfs-rename-dst", 0) != -1 ||
        errno != ENOTEMPTY) {
        errno = ENOTEMPTY;
        goto out;
    }
    if (vfs_contract_write_file("/tmp/vfs-rename-src/probe", "probe") != 0) {
        goto out;
    }

    ret = 0;

out:
    unlink_impl("/tmp/vfs-rename-src/probe");
    unlink_impl("/tmp/vfs-rename-dst/child");
    rmdir_impl("/tmp/vfs-rename-src");
    rmdir_impl("/tmp/vfs-rename-dst");
    return ret;
}

int vfs_contract_rename_file_over_directory_returns_isdir(void) {
    int ret = -1;

    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-rename-file");
    rmdir_impl("/tmp/vfs-rename-dir");
    if (vfs_contract_write_file("/tmp/vfs-rename-file", "file") != 0 ||
        mkdir_impl("/tmp/vfs-rename-dir", 0700) != 0) {
        goto out;
    }

    errno = 0;
    if (renameat2(AT_FDCWD, "/tmp/vfs-rename-file", AT_FDCWD, "/tmp/vfs-rename-dir", 0) != -1 ||
        errno != EISDIR) {
        errno = EISDIR;
        goto out;
    }
    if (vfs_contract_read_file_exact("/tmp/vfs-rename-file", "file") != 0) {
        goto out;
    }

    ret = 0;

out:
    unlink_impl("/tmp/vfs-rename-file");
    rmdir_impl("/tmp/vfs-rename-dir");
    return ret;
}

int vfs_contract_rename_directory_over_file_returns_notdir(void) {
    int ret = -1;

    cred_reset_to_defaults();
    rmdir_impl("/tmp/vfs-rename-dir");
    unlink_impl("/tmp/vfs-rename-file");
    if (mkdir_impl("/tmp/vfs-rename-dir", 0700) != 0 ||
        vfs_contract_write_file("/tmp/vfs-rename-file", "file") != 0) {
        goto out;
    }

    errno = 0;
    if (renameat2(AT_FDCWD, "/tmp/vfs-rename-dir", AT_FDCWD, "/tmp/vfs-rename-file", 0) != -1 ||
        errno != ENOTDIR) {
        errno = ENOTDIR;
        goto out;
    }
    if (vfs_contract_read_file_exact("/tmp/vfs-rename-file", "file") != 0) {
        goto out;
    }

    ret = 0;

out:
    rmdir_impl("/tmp/vfs-rename-dir");
    unlink_impl("/tmp/vfs-rename-file");
    return ret;
}

int vfs_contract_root_chown_updates_virtual_owner(void) {
    struct linux_stat st;
    int ret = -1;

    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-cred-chown-file");
    if (vfs_contract_write_file("/tmp/vfs-cred-chown-file", "owned") != 0) {
        goto out;
    }
    if (chown("/tmp/vfs-cred-chown-file", 1000, 1000) != 0) {
        goto out;
    }
    if (vfs_fstatat(AT_FDCWD, "/tmp/vfs-cred-chown-file", &st, 0) != 0) {
        goto out;
    }
    if (st.st_uid != 1000 || st.st_gid != 1000) {
        errno = EIO;
        goto out;
    }

    ret = 0;

out:
    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-cred-chown-file");
    return ret;
}

int vfs_contract_nonroot_cannot_chown_owned_file(void) {
    int ret = -1;

    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-cred-user-chown-file");
    if (setuid_impl(1000) != 0) {
        goto out;
    }
    if (vfs_contract_write_file("/tmp/vfs-cred-user-chown-file", "owned") != 0) {
        goto out;
    }
    errno = 0;
    if (chown("/tmp/vfs-cred-user-chown-file", 1001, 1000) != -1 || errno != EPERM) {
        errno = EPERM;
        goto out;
    }

    ret = 0;

out:
    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-cred-user-chown-file");
    return ret;
}

int vfs_contract_owner_chmod_updates_virtual_mode(void) {
    struct linux_stat st;
    int ret = -1;

    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-cred-chmod-file");
    if (setuid_impl(1000) != 0) {
        goto out;
    }
    if (vfs_contract_write_file("/tmp/vfs-cred-chmod-file", "owned") != 0) {
        goto out;
    }
    if (chmod("/tmp/vfs-cred-chmod-file", 0644) != 0) {
        goto out;
    }
    if (vfs_fstatat(AT_FDCWD, "/tmp/vfs-cred-chmod-file", &st, 0) != 0) {
        goto out;
    }
    if ((st.st_mode & 0777) != 0644) {
        errno = EIO;
        goto out;
    }

    ret = 0;

out:
    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-cred-chmod-file");
    return ret;
}

int vfs_contract_nonowner_cannot_chmod_file(void) {
    int ret = -1;

    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-cred-root-chmod-file");
    if (vfs_contract_write_file("/tmp/vfs-cred-root-chmod-file", "owned") != 0) {
        goto out;
    }
    if (setuid_impl(1000) != 0) {
        goto out;
    }
    errno = 0;
    if (chmod("/tmp/vfs-cred-root-chmod-file", 0644) != -1 || errno != EPERM) {
        errno = EPERM;
        goto out;
    }

    ret = 0;

out:
    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-cred-root-chmod-file");
    return ret;
}

int vfs_contract_fchmod_updates_virtual_mode(void) {
    struct linux_stat st;
    int fd = -1;
    int ret = -1;

    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-cred-fchmod-file");
    fd = open_impl("/tmp/vfs-cred-fchmod-file", O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        goto out;
    }
    if (fchmod(fd, 0640) != 0) {
        goto out;
    }
    if (fstat_impl(fd, &st) != 0) {
        goto out;
    }
    if ((st.st_mode & 0777) != 0640) {
        errno = EIO;
        goto out;
    }

    ret = 0;

out:
    close_impl(fd);
    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-cred-fchmod-file");
    return ret;
}

int vfs_contract_fchown_updates_virtual_owner(void) {
    struct linux_stat st;
    int fd = -1;
    int ret = -1;

    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-cred-fchown-file");
    fd = open_impl("/tmp/vfs-cred-fchown-file", O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        goto out;
    }
    if (fchown(fd, 1000, 1001) != 0) {
        goto out;
    }
    if (fstat_impl(fd, &st) != 0) {
        goto out;
    }
    if (st.st_uid != 1000 || st.st_gid != 1001) {
        errno = EIO;
        goto out;
    }

    ret = 0;

out:
    close_impl(fd);
    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-cred-fchown-file");
    return ret;
}

int vfs_contract_supplementary_group_can_read_group_file(void) {
    gid_t groups[1] = {3000};
    int ret = -1;

    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-cred-supgroup-read-file");
    if (vfs_contract_write_file("/tmp/vfs-cred-supgroup-read-file", "group") != 0) {
        goto out;
    }
    if (chown("/tmp/vfs-cred-supgroup-read-file", 2000, 3000) != 0) {
        goto out;
    }
    if (chmod("/tmp/vfs-cred-supgroup-read-file", 0640) != 0) {
        goto out;
    }
    if (setgroups_impl(1, groups) != 0) {
        goto out;
    }
    if (setuid_impl(1000) != 0) {
        goto out;
    }
    if (vfs_contract_read_file_exact("/tmp/vfs-cred-supgroup-read-file", "group") != 0) {
        goto out;
    }

    ret = 0;

out:
    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-cred-supgroup-read-file");
    return ret;
}

int vfs_contract_missing_supplementary_group_cannot_read_group_file(void) {
    gid_t groups[1] = {3001};
    int fd = -1;
    int ret = -1;

    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-cred-no-supgroup-read-file");
    if (vfs_contract_write_file("/tmp/vfs-cred-no-supgroup-read-file", "group") != 0) {
        goto out;
    }
    if (chown("/tmp/vfs-cred-no-supgroup-read-file", 2000, 3000) != 0) {
        goto out;
    }
    if (chmod("/tmp/vfs-cred-no-supgroup-read-file", 0640) != 0) {
        goto out;
    }
    if (setgroups_impl(1, groups) != 0) {
        goto out;
    }
    if (setuid_impl(1000) != 0) {
        goto out;
    }

    errno = 0;
    fd = open_impl("/tmp/vfs-cred-no-supgroup-read-file", O_RDONLY, 0);
    if (fd != -1 || errno != EACCES) {
        close_impl(fd);
        errno = EACCES;
        goto out;
    }

    ret = 0;

out:
    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-cred-no-supgroup-read-file");
    return ret;
}

int vfs_contract_root_without_dac_caps_cannot_read_private_file(void) {
    struct __user_cap_header_struct header = {
        .version = _LINUX_CAPABILITY_VERSION_3,
        .pid = 0,
    };
    struct __user_cap_data_struct data[_LINUX_CAPABILITY_U32S_3];
    int fd = -1;
    int ret = -1;

    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-cred-no-dac-cap-file");

    fd = open_impl("/tmp/vfs-cred-no-dac-cap-file", O_RDWR | O_CREAT | O_TRUNC, 0000);
    if (fd < 0) {
        goto out;
    }
    close_impl(fd);
    fd = -1;

    if (capget(&header, data) != 0) {
        goto out;
    }
    if ((data[CAP_DAC_OVERRIDE / 32].effective & (1U << (CAP_DAC_OVERRIDE % 32))) == 0) {
        errno = EPROTO;
        goto out;
    }
    data[CAP_DAC_OVERRIDE / 32].effective &= ~(1U << (CAP_DAC_OVERRIDE % 32));
    data[CAP_DAC_READ_SEARCH / 32].effective &= ~(1U << (CAP_DAC_READ_SEARCH % 32));
    if (capset(&header, data) != 0) {
        goto out;
    }
    if (capget(&header, data) != 0) {
        goto out;
    }
    if ((data[CAP_DAC_OVERRIDE / 32].effective & (1U << (CAP_DAC_OVERRIDE % 32))) != 0) {
        errno = EPROTO;
        goto out;
    }

    errno = 0;
    fd = open_impl("/tmp/vfs-cred-no-dac-cap-file", O_RDONLY, 0);
    if (fd != -1 || errno != EACCES) {
        close_impl(fd);
        errno = EACCES;
        goto out;
    }

    ret = 0;

out:
    close_impl(fd);
    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-cred-no-dac-cap-file");
    return ret;
}

int vfs_contract_statfs_reports_virtual_proc_and_tmpfs(void) {
    struct statfs st;
    int fd = -1;
    int ret = -1;

    memset(&st, 0, sizeof(st));
    if (statfs("/proc", &st) != 0 || st.f_type != PROC_SUPER_MAGIC || st.f_bsize != 4096) {
        errno = ENODATA;
        return -1;
    }

    memset(&st, 0, sizeof(st));
    if (statfs("/tmp", &st) != 0 || st.f_type != TMPFS_MAGIC || st.f_bsize != 4096) {
        errno = ENODATA;
        return -1;
    }

    fd = open_impl("/proc/self/status", O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }
    memset(&st, 0, sizeof(st));
    if (fstatfs(fd, &st) == 0 && st.f_type == PROC_SUPER_MAGIC) {
        ret = 0;
    } else {
        errno = ENODATA;
    }
    close_impl(fd);
    return ret;
}
