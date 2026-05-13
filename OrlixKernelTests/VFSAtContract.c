/* OrlixKernelTests/VFSAtContract.c
 * C translation unit for VFS AT_* flag Linux UAPI contract tests.
 *
 * Compiled in a Linux-UAPI-clean context.
 * Uses canonical Linux names directly.
 */

#include <uapi/asm/unistd.h>
#include <uapi/asm/stat.h>
#include <uapi/linux/fcntl.h>
#include <uapi/linux/capability.h>
#include <uapi/linux/sched.h>
#include <uapi/linux/stat.h>
#include <uapi/linux/xattr.h>
#include <uapi/linux/errno.h>
#include <linux/string.h>

#include "fs/fcntl.h"
#include "fs/open.h"
#include "fs/read_write.h"
#include "fs/stat.h"
#include "fs/vfs.h"
#include "private/fs/vfs_state.h"
#include "kernel/cred.h"
#include "private/kernel/cred_state.h"
#include "kernel/task.h"
#include "private/kernel/task_state.h"
#include "runtime/syscall.h"

extern int errno;

extern int chroot(const char *path);
extern int chdir(const char *path);
extern int fchdir(int fd);
extern char *getcwd(char *buf, size_t size);
extern int access(const char *pathname, int mode);
extern int mkdir_impl(const char *pathname, uint32_t mode);

int vfs_path_contract_open_tmp_fd_symlink_file(void) {
    return open_impl("/tmp/test_fd_symlink", O_CREAT | O_RDWR, 0644);
}
extern int close_impl(int fd);
extern long readlink_impl(const char *pathname, char *buf, size_t bufsiz);
extern int unlink_impl(const char *pathname);
extern int rmdir_impl(const char *pathname);
extern int mkdirat(int dirfd, const char *pathname, uint32_t mode);
extern int unlinkat(int dirfd, const char *pathname, int flags);
extern int linkat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, int flags);
extern int symlinkat(const char *target, int newdirfd, const char *linkpath);
extern long readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz);
extern int renameat2_impl(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, unsigned int flags);
extern void cred_reset_to_defaults(void);
extern int chmod(const char *pathname, uint32_t mode);
extern int fchmod(int fd, uint32_t mode);
extern int chown(const char *pathname, __kernel_uid32_t owner, __kernel_gid32_t group);
extern int fchown(int fd, __kernel_uid32_t owner, __kernel_gid32_t group);
extern int capget_impl(cap_user_header_t header, cap_user_data_t data);
extern int capset_impl(cap_user_header_t header, const cap_user_data_t data);
static int vfs_contract_ignore_exists(int result) {
    if (result == 0 || errno == EEXIST) {
        return 0;
    }
    return -1;
}

static void vfs_contract_restore_fs(struct fs_context *fs, const char *root, const char *pwd) {
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

    len = strlen(content);
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
    size_t expected_len = strlen(expected);

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
    if ((size_t)nread != expected_len || memcmp(buf, expected, expected_len) != 0) {
        errno = EIO;
        return -1;
    }

    return 0;
}

static int vfs_contract_content_contains(const char *content, const char *needle) {
    size_t content_len;
    size_t needle_len;

    if (!content || !needle) {
        return 0;
    }

    content_len = strlen(content);
    needle_len = strlen(needle);
    if (needle_len == 0) {
        return 1;
    }
    if (needle_len > content_len) {
        return 0;
    }

    for (size_t i = 0; i <= content_len - needle_len; i++) {
        if (memcmp(content + i, needle, needle_len) == 0) {
            return 1;
        }
    }

    return 0;
}

static int vfs_contract_proc_fd_path(int fd, char *buf, size_t buf_len) {
    const char prefix[] = "/proc/self/fd/";
    char digits[16];
    size_t pos = 0;
    int value = fd;

    if (!buf || buf_len == 0 || fd < 0) {
        errno = EINVAL;
        return -1;
    }
    while (prefix[pos] != '\0') {
        if (pos + 1 >= buf_len) {
            errno = ENAMETOOLONG;
            return -1;
        }
        buf[pos] = prefix[pos];
        pos++;
    }
    size_t digit_count = 0;
    do {
        digits[digit_count++] = (char)('0' + (value % 10));
        value /= 10;
    } while (value > 0 && digit_count < sizeof(digits));
    if (value > 0 || pos + digit_count + 1 > buf_len) {
        errno = ENAMETOOLONG;
        return -1;
    }
    while (digit_count > 0) {
        buf[pos++] = digits[--digit_count];
    }
    buf[pos] = '\0';
    return 0;
}

/* Contract: vfs_path_fstatat supports AT_FDCWD */
int vfs_contract_fstatat_at_fdcwd(void) {
    struct stat st;
    return vfs_path_fstatat(AT_FDCWD, "/etc/passwd", &st, 0);
}

/* Contract: vfs_path_fstatat supports AT_SYMLINK_NOFOLLOW */
int vfs_contract_fstatat_symlink_nofollow(void) {
    struct stat st;
    return vfs_path_fstatat(AT_FDCWD, "/etc/passwd", &st, AT_SYMLINK_NOFOLLOW);
}

/* Contract: vfs_path_fstatat rejects unsupported synthetic paths with AT_SYMLINK_NOFOLLOW */
int vfs_contract_fstatat_synthetic_child_nofollow(void) {
    struct stat st;
    return vfs_path_fstatat(AT_FDCWD, "/sys/kernel", &st, AT_SYMLINK_NOFOLLOW);
}

int vfs_contract_faccessat_eaccess_uses_effective_credentials(void) {
    const char *path = "/tmp/vfs-faccessat-eaccess-effective";
    int ret = -1;

    unlink_impl(path);
    cred_reset_to_defaults();
    if (vfs_contract_write_file(path, "effective") != 0 ||
        chown(path, 2000, 3000) != 0 ||
        chmod(path, 0100) != 0 ||
        setresuid_impl(1000, 2000, 2000) != 0 ||
        setfsuid_impl(1000) != 2000) {
        goto out;
    }

    errno = 0;
    if (vfs_faccessat(AT_FDCWD, path, 1, 0) != -EACCES) {
        errno = ENODATA;
        goto out;
    }
    if (vfs_faccessat(AT_FDCWD, path, 1, AT_EACCESS) != 0) {
        goto out;
    }
    ret = 0;

out:
    {
        int saved_errno = errno;
        cred_reset_to_defaults();
        unlink_impl(path);
        errno = saved_errno;
    }
    return ret;
}

/* Contract: vfs_faccessat reports ENOTSUP for AT_SYMLINK_NOFOLLOW */
int vfs_contract_faccessat_symlink_nofollow_returns_enotsup(void) {
    return vfs_faccessat(AT_FDCWD, "/etc", 1, AT_SYMLINK_NOFOLLOW);
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
    if (vfs_faccessat(AT_FDCWD, "/tmp/vfs-access-absolute-link/link", 0, 0) != 0 ||
        access("/tmp/vfs-access-absolute-link/link", 0) != 0) {
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
    if (vfs_faccessat(AT_FDCWD, "/tmp/vfs-access-loop/a", 0, 0) != -ELOOP) {
        errno = EIO;
        goto out;
    }
    errno = 0;
    if (access("/tmp/vfs-access-loop/a", 0) != -1 || errno != ELOOP) {
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

int vfs_contract_fsuid_controls_owner_file_access(void) {
    const char *path = "/tmp/vfs-fsuid-owner-file";
    int fd;
    int ret = -1;

    unlink_impl(path);
    cred_reset_to_defaults();
    if (vfs_contract_write_file(path, "owner") != 0 ||
        chown(path, 2000, 3000) != 0 ||
        chmod(path, 0600) != 0 ||
        setresuid_impl(1000, 1000, 2000) != 0) {
        goto out;
    }

    fd = open_impl(path, O_RDONLY, 0);
    if (fd != -1 || errno != EACCES) {
        if (fd >= 0) {
            close_impl(fd);
        }
        errno = ENODATA;
        goto out;
    }
    if (setfsuid_impl(2000) != 1000) {
        errno = EPROTO;
        goto out;
    }
    fd = open_impl(path, O_RDONLY, 0);
    if (fd < 0) {
        goto out;
    }
    close_impl(fd);
    if (setfsuid_impl(1000) != 2000) {
        errno = EPROTO;
        goto out;
    }
    fd = open_impl(path, O_RDONLY, 0);
    if (fd != -1 || errno != EACCES) {
        if (fd >= 0) {
            close_impl(fd);
        }
        errno = ENOMSG;
        goto out;
    }

    ret = 0;

out:
    {
        int saved_errno = errno;
        cred_reset_to_defaults();
        unlink_impl(path);
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_fsgid_controls_group_file_access(void) {
    const char *path = "/tmp/vfs-fsgid-group-file";
    int fd;
    int ret = -1;

    unlink_impl(path);
    cred_reset_to_defaults();
    if (vfs_contract_write_file(path, "group") != 0 ||
        chown(path, 2000, 3000) != 0 ||
        chmod(path, 0060) != 0 ||
        setresgid_impl(1000, 1000, 3000) != 0 ||
        setresuid_impl(1000, 1000, 1000) != 0) {
        goto out;
    }

    fd = open_impl(path, O_RDONLY, 0);
    if (fd != -1 || errno != EACCES) {
        if (fd >= 0) {
            close_impl(fd);
        }
        errno = ENODATA;
        goto out;
    }
    if (setfsgid_impl(3000) != 1000) {
        errno = EPROTO;
        goto out;
    }
    fd = open_impl(path, O_RDONLY, 0);
    if (fd < 0) {
        goto out;
    }
    close_impl(fd);
    if (setfsgid_impl(1000) != 3000) {
        errno = EPROTO;
        goto out;
    }
    fd = open_impl(path, O_RDONLY, 0);
    if (fd != -1 || errno != EACCES) {
        if (fd >= 0) {
            close_impl(fd);
        }
        errno = ENOMSG;
        goto out;
    }

    ret = 0;

out:
    {
        int saved_errno = errno;
        cred_reset_to_defaults();
        unlink_impl(path);
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
    struct task *task = task_current();
    char old_pwd[MAX_PATH];
    char cwd[MAX_PATH];
    int ret = -1;

    if (!task || !task->fs) {
        errno = ESRCH;
        return -1;
    }

    memcpy(old_pwd, task->fs->pwd_path, sizeof(old_pwd));
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
    if (!getcwd(cwd, sizeof(cwd)) || strcmp(cwd, "/tmp/vfs-chdir-symlink/real") != 0) {
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
    if (vfs_path_fstatat(AT_FDCWD, "/tmp/vfs-mkdirat-symlink/real/created", &(struct stat){0}, 0) != 0) {
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
    if (renameat2_impl(AT_FDCWD, "/tmp/vfs-renameat-symlink/src-link/file",
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
    struct stat st;
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
    if (vfs_path_fstatat(AT_FDCWD, "/tmp/vfs-linkat-follow/hard-link", &st, AT_SYMLINK_NOFOLLOW) != 0 ||
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
    struct task *task = task_current();
    char old_root[MAX_PATH];
    char old_pwd[MAX_PATH];
    char cwd[MAX_PATH];
    int fd = -1;
    int ret = -1;

    if (!task || !task->fs) {
        errno = ESRCH;
        return -1;
    }

    memcpy(old_root, task->fs->root_path, sizeof(old_root));
    memcpy(old_pwd, task->fs->pwd_path, sizeof(old_pwd));
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

    if (!getcwd(cwd, sizeof(cwd)) || strcmp(cwd, "/") != 0) {
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
    struct task *task = task_current();
    char old_root[MAX_PATH];
    char old_pwd[MAX_PATH];
    int ret = -1;

    if (!task || !task->fs) {
        errno = ESRCH;
        return -1;
    }

    cred_reset_to_defaults();
    memcpy(old_root, task->fs->root_path, sizeof(old_root));
    memcpy(old_pwd, task->fs->pwd_path, sizeof(old_pwd));
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
    struct task *task = task_current();
    char old_root[MAX_PATH];
    char old_pwd[MAX_PATH];
    int ret = -1;

    if (!task || !task->fs) {
        errno = ESRCH;
        return -1;
    }

    cred_reset_to_defaults();
    memcpy(old_root, task->fs->root_path, sizeof(old_root));
    memcpy(old_pwd, task->fs->pwd_path, sizeof(old_pwd));
    fs_set_root(task->fs, "/");
    fs_set_pwd(task->fs, "/");

    rmdir_impl("/tmp/vfs-chroot-cred-root");
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-chroot-cred-root", 0700)) != 0) {
        goto out;
    }
    if (capget_impl(&header, data) != 0) {
        goto out;
    }
    data[CAP_SYS_CHROOT / 32].effective &= ~(1U << (CAP_SYS_CHROOT % 32));
    if (capset_impl(&header, data) != 0) {
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
    struct task *task = task_current();
    char old_root[MAX_PATH];
    char old_pwd[MAX_PATH];
    char cwd[MAX_PATH];
    int fd = -1;
    int ret = -1;

    if (!task || !task->fs) {
        errno = ESRCH;
        return -1;
    }

    memcpy(old_root, task->fs->root_path, sizeof(old_root));
    memcpy(old_pwd, task->fs->pwd_path, sizeof(old_pwd));
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
    if (!getcwd(cwd, sizeof(cwd)) || strcmp(cwd, "/tmp/vfs-fchdir-dir") != 0) {
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


int vfs_contract_hardlink_inode_metadata_survives_unlink(void) {
    const char path[] = "/tmp/vfs-hardlink-inode-metadata";
    const char alias[] = "/tmp/vfs-hardlink-inode-metadata-alias";
    const char name[] = "user.hardlink-life";
    const char value[] = "hardlink-value";
    char readback[32];
    struct stat st;
    int fd = -1;
    long ret;
    int result = -1;

    unlink_impl(alias);
    unlink_impl(path);
    fd = open_impl(path, O_CREAT | O_RDWR | O_TRUNC, 0600);
    if (fd < 0) {
        return -1;
    }
    close_impl(fd);
    fd = -1;
    if (linkat(AT_FDCWD, path, AT_FDCWD, alias, 0) != 0) {
        goto out;
    }
    ret = syscall_dispatch_impl(__NR_setxattr, (long)(uintptr_t)path, (long)(uintptr_t)name,
                                (long)(uintptr_t)value, sizeof(value), XATTR_CREATE, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    if (chmod(path, 0640) != 0 || chown(path, 42, 84) != 0) {
        goto out;
    }
    fd = open_impl(alias, O_RDONLY, 0);
    if (fd < 0) {
        goto out;
    }
    if (fstat_impl(fd, &st) != 0) {
        goto out;
    }
    close_impl(fd);
    fd = -1;
    if ((st.st_mode & 07777U) != 0640 || st.st_uid != 42 || st.st_gid != 84) {
        errno = ENODATA;
        goto out;
    }
    if (unlink_impl(path) != 0) {
        goto out;
    }
    memset(readback, 0, sizeof(readback));
    ret = syscall_dispatch_impl(__NR_getxattr, (long)(uintptr_t)alias, (long)(uintptr_t)name,
                                (long)(uintptr_t)readback, sizeof(readback), 0, 0);
    if (ret != (long)sizeof(value) || memcmp(readback, value, sizeof(value)) != 0) {
        errno = ret < 0 ? (int)-ret : ENOMSG;
        goto out;
    }
    fd = open_impl(alias, O_RDONLY, 0);
    if (fd < 0) {
        goto out;
    }
    if (fstat_impl(fd, &st) != 0) {
        goto out;
    }
    close_impl(fd);
    fd = -1;
    if ((st.st_mode & 07777U) != 0640 || st.st_uid != 42 || st.st_gid != 84) {
        errno = ERANGE;
        goto out;
    }

    result = 0;

out:
    if (fd >= 0) {
        close_impl(fd);
    }
    unlink_impl(alias);
    unlink_impl(path);
    return result;
}

int vfs_contract_proc_fd_marks_open_unlinked_file_deleted(void) {
    const char path[] = "/tmp/vfs-open-unlinked-procfd";
    char proc_path[64];
    char target[512];
    char byte = 0;
    int fd = -1;
    long ret;
    int result = -1;

    unlink_impl(path);
    fd = open_impl(path, O_CREAT | O_RDWR | O_TRUNC, 0600);
    if (fd < 0) {
        return -1;
    }
    if (write_impl(fd, "live", 4) != 4 ||
        vfs_contract_proc_fd_path(fd, proc_path, sizeof(proc_path)) != 0) {
        goto out;
    }
    if (unlink_impl(path) != 0) {
        goto out;
    }
    if (open_impl(path, O_RDONLY, 0) != -1 || errno != ENOENT) {
        errno = EBUSY;
        goto out;
    }
    if (pread_impl(fd, &byte, 1, 0) != 1 || byte != 'l') {
        errno = ENODATA;
        goto out;
    }
    ret = readlink_impl(proc_path, target, sizeof(target) - 1);
    if (ret < 0) {
        goto out;
    }
    target[ret] = '\0';
    if (!vfs_contract_content_contains(target, path) ||
        !vfs_contract_content_contains(target, " (deleted)")) {
        errno = ENOMSG;
        goto out;
    }
    result = 0;

out:
    if (fd >= 0) {
        close_impl(fd);
    }
    unlink_impl(path);
    return result;
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
    struct stat st;
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

int vfs_contract_nonroot_cannot_open_through_unsearchable_parent_directory(void) {
    int fd = -1;
    int ret = -1;

    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-cred-no-search-dir/file");
    rmdir_impl("/tmp/vfs-cred-no-search-dir");
    if (mkdir_impl("/tmp/vfs-cred-no-search-dir", 0700) != 0) {
        goto out;
    }
    fd = open_impl("/tmp/vfs-cred-no-search-dir/file", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        goto out;
    }
    if (write_impl(fd, "visible", 7) != 7) {
        goto out;
    }
    close_impl(fd);
    fd = -1;
    if (chmod("/tmp/vfs-cred-no-search-dir", 0000) != 0) {
        goto out;
    }
    if (setuid_impl(1000) != 0) {
        goto out;
    }

    errno = 0;
    fd = open_impl("/tmp/vfs-cred-no-search-dir/file", O_RDONLY, 0);
    if (fd != -1 || errno != EACCES) {
        close_impl(fd);
        fd = -1;
        errno = EACCES;
        goto out;
    }

    ret = 0;

out:
    {
        int saved_errno = errno;
        close_impl(fd);
        cred_reset_to_defaults();
        chmod("/tmp/vfs-cred-no-search-dir", 0700);
        unlink_impl("/tmp/vfs-cred-no-search-dir/file");
        rmdir_impl("/tmp/vfs-cred-no-search-dir");
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_sticky_directory_blocks_nonowner_unlink(void) {
    int fd = -1;
    int ret = -1;

    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-cred-sticky-dir/file");
    rmdir_impl("/tmp/vfs-cred-sticky-dir");
    if (mkdir_impl("/tmp/vfs-cred-sticky-dir", 01777) != 0) {
        goto out;
    }
    fd = open_impl("/tmp/vfs-cred-sticky-dir/file", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        goto out;
    }
    close_impl(fd);
    fd = -1;
    if (chown("/tmp/vfs-cred-sticky-dir/file", 2000, 2000) != 0) {
        goto out;
    }
    if (setuid_impl(1000) != 0) {
        goto out;
    }
    errno = 0;
    if (unlink_impl("/tmp/vfs-cred-sticky-dir/file") != -1 || errno != EPERM) {
        errno = EPERM;
        goto out;
    }

    ret = 0;

out:
    {
        int saved_errno = errno;
        close_impl(fd);
        cred_reset_to_defaults();
        unlink_impl("/tmp/vfs-cred-sticky-dir/file");
        rmdir_impl("/tmp/vfs-cred-sticky-dir");
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_sticky_directory_blocks_nonowner_rename(void) {
    int fd = -1;
    int ret = -1;

    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-cred-sticky-rename-dir/file");
    unlink_impl("/tmp/vfs-cred-sticky-rename-dir/moved");
    rmdir_impl("/tmp/vfs-cred-sticky-rename-dir");
    if (mkdir_impl("/tmp/vfs-cred-sticky-rename-dir", 01777) != 0) {
        goto out;
    }
    fd = open_impl("/tmp/vfs-cred-sticky-rename-dir/file", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        goto out;
    }
    close_impl(fd);
    fd = -1;
    if (chown("/tmp/vfs-cred-sticky-rename-dir/file", 2000, 2000) != 0) {
        goto out;
    }
    if (setuid_impl(1000) != 0) {
        goto out;
    }
    errno = 0;
    if (renameat2_impl(AT_FDCWD, "/tmp/vfs-cred-sticky-rename-dir/file",
                  AT_FDCWD, "/tmp/vfs-cred-sticky-rename-dir/moved", 0) != -1 ||
        errno != EPERM) {
        errno = EPERM;
        goto out;
    }

    ret = 0;

out:
    {
        int saved_errno = errno;
        close_impl(fd);
        cred_reset_to_defaults();
        unlink_impl("/tmp/vfs-cred-sticky-rename-dir/file");
        unlink_impl("/tmp/vfs-cred-sticky-rename-dir/moved");
        rmdir_impl("/tmp/vfs-cred-sticky-rename-dir");
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_sticky_directory_blocks_nonowner_exchange_target(void) {
    int fd = -1;
    int ret = -1;

    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-cred-sticky-exchange-dir/left");
    unlink_impl("/tmp/vfs-cred-sticky-exchange-dir/right");
    rmdir_impl("/tmp/vfs-cred-sticky-exchange-dir");
    if (mkdir_impl("/tmp/vfs-cred-sticky-exchange-dir", 01777) != 0) {
        goto out;
    }
    fd = open_impl("/tmp/vfs-cred-sticky-exchange-dir/left", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        goto out;
    }
    close_impl(fd);
    fd = open_impl("/tmp/vfs-cred-sticky-exchange-dir/right", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        goto out;
    }
    close_impl(fd);
    fd = -1;
    if (chown("/tmp/vfs-cred-sticky-exchange-dir/left", 1000, 1000) != 0 ||
        chown("/tmp/vfs-cred-sticky-exchange-dir/right", 2000, 2000) != 0) {
        goto out;
    }
    if (setuid_impl(1000) != 0) {
        goto out;
    }
    errno = 0;
    if (renameat2_impl(AT_FDCWD, "/tmp/vfs-cred-sticky-exchange-dir/left",
                  AT_FDCWD, "/tmp/vfs-cred-sticky-exchange-dir/right",
                  AT_RENAME_EXCHANGE) != -1 ||
        errno != EPERM) {
        errno = EPERM;
        goto out;
    }

    ret = 0;

out:
    {
        int saved_errno = errno;
        close_impl(fd);
        cred_reset_to_defaults();
        unlink_impl("/tmp/vfs-cred-sticky-exchange-dir/left");
        unlink_impl("/tmp/vfs-cred-sticky-exchange-dir/right");
        rmdir_impl("/tmp/vfs-cred-sticky-exchange-dir");
        errno = saved_errno;
    }
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
    if (strcmp(target, "/target/path") != 0) {
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

    if (renameat2_impl(dirfd, "left", dirfd, "right", AT_RENAME_EXCHANGE) != 0) {
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
    struct stat left_st;
    struct stat right_st;
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

    if (renameat2_impl(dirfd, "left", dirfd, "right", AT_RENAME_EXCHANGE) != 0) {
        goto out;
    }
    if (vfs_path_fstatat(AT_FDCWD, "/tmp/vfs-at-rename-dir/left", &left_st, 0) != 0 ||
        vfs_path_fstatat(AT_FDCWD, "/tmp/vfs-at-rename-dir/right", &right_st, 0) != 0) {
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
    if (renameat2_impl(dirfd, "left", dirfd, "right", AT_RENAME_NOREPLACE) != -1 || errno != EEXIST) {
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
    struct stat st;
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

    if (renameat2_impl(dirfd, "left", dirfd, "right", 0) != 0) {
        goto out;
    }
    if (vfs_contract_read_file_exact("/tmp/vfs-at-rename-dir/right", "left") != 0) {
        goto out;
    }
    if (vfs_path_fstatat(AT_FDCWD, "/tmp/vfs-at-rename-dir/right", &st, 0) != 0) {
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
    if (renameat2_impl(AT_FDCWD, "/tmp/vfs-rename-src", AT_FDCWD, "/tmp/vfs-rename-dst", 0) != -1 ||
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
    if (renameat2_impl(AT_FDCWD, "/tmp/vfs-rename-file", AT_FDCWD, "/tmp/vfs-rename-dir", 0) != -1 ||
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
    if (renameat2_impl(AT_FDCWD, "/tmp/vfs-rename-dir", AT_FDCWD, "/tmp/vfs-rename-file", 0) != -1 ||
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
    struct stat st;
    int ret = -1;

    cred_reset_to_defaults();
    unlink_impl("/tmp/vfs-cred-chown-file");
    if (vfs_contract_write_file("/tmp/vfs-cred-chown-file", "owned") != 0) {
        goto out;
    }
    if (chown("/tmp/vfs-cred-chown-file", 1000, 1000) != 0) {
        goto out;
    }
    if (vfs_path_fstatat(AT_FDCWD, "/tmp/vfs-cred-chown-file", &st, 0) != 0) {
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
    struct stat st;
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
    if (vfs_path_fstatat(AT_FDCWD, "/tmp/vfs-cred-chmod-file", &st, 0) != 0) {
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
    struct stat st;
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
    struct stat st;
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
    __kernel_gid32_t groups[1] = {3000};
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
    __kernel_gid32_t groups[1] = {3001};
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

    if (capget_impl(&header, data) != 0) {
        goto out;
    }
    if ((data[CAP_DAC_OVERRIDE / 32].effective & (1U << (CAP_DAC_OVERRIDE % 32))) == 0) {
        errno = EPROTO;
        goto out;
    }
    data[CAP_DAC_OVERRIDE / 32].effective &= ~(1U << (CAP_DAC_OVERRIDE % 32));
    data[CAP_DAC_READ_SEARCH / 32].effective &= ~(1U << (CAP_DAC_READ_SEARCH % 32));
    if (capset_impl(&header, data) != 0) {
        goto out;
    }
    if (capget_impl(&header, data) != 0) {
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
