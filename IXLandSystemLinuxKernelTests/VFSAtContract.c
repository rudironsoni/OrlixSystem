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
#include <linux/sched.h>
#include <linux/stat.h>
#include <linux/umount.h>
#include <linux/xattr.h>
#include <asm/unistd.h>
#include <asm/statfs.h>

#include <errno.h>
#include <string.h>

#include "fs/vfs.h"
#include "kernel/cred_internal.h"
#include "kernel/task.h"
#include "runtime/syscall.h"

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
extern int umount2(const char *target, int flags);
extern int pivot_root(const char *new_root, const char *put_old);
extern int mount_setattr(int dirfd, const char *pathname, unsigned int flags,
                         struct mount_attr *attr, size_t size);
extern int open_tree(int dirfd, const char *pathname, unsigned int flags);
extern int move_mount(int from_dirfd, const char *from_pathname, int to_dirfd,
                      const char *to_pathname, unsigned int flags);
extern int clone_impl(uint64_t flags);
extern int mkdir_impl(const char *pathname, linux_mode_t mode);
extern int open_impl(const char *pathname, int flags, linux_mode_t mode);

int vfs_path_contract_open_tmp_fd_symlink_file(void) {
    return open_impl("/tmp/test_fd_symlink", O_CREAT | O_RDWR, 0644);
}
extern int open_impl(const char *pathname, int flags, linux_mode_t mode);
extern int openat_impl(int dirfd, const char *pathname, int flags, linux_mode_t mode);
extern int fcntl_impl(int fd, int cmd, ...);
extern int close_impl(int fd);
extern long read_impl(int fd, void *buf, size_t count);
extern long write_impl(int fd, const void *buf, size_t count);
extern long pread_impl(int fd, void *buf, size_t count, linux_off_t offset);
extern long readlink_impl(const char *pathname, char *buf, size_t bufsiz);
extern int unlink_impl(const char *pathname);
extern int rmdir_impl(const char *pathname);
extern int fstat_impl(int fd, struct linux_stat *statbuf);
extern int setuid_impl(uid_t uid);
extern uid_t setfsuid_impl(uid_t fsuid);
extern gid_t setfsgid_impl(gid_t fsgid);
extern int setresuid_impl(uid_t ruid, uid_t euid, uid_t suid);
extern int setresgid_impl(gid_t rgid, gid_t egid, gid_t sgid);
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

static void vfs_contract_release_lookup_child(struct task_struct *parent, struct task_struct *child) {
    if (!child) {
        return;
    }
    task_unlink_child_impl(parent, child);
    free_task(child);
    free_task(child);
}
extern int vfs_umount_lazy(const char *target);
extern int vfs_umount_expire(const char *target);
extern int vfs_umount_force(const char *target);
extern int vfs_reap_detached_mount_refs(void);
extern unsigned int vfs_detached_mount_ref_count(void);

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

static void vfs_contract_cleanup_mount_paths(void) {
    umount("/tmp/vfs-bind-target");
    unlink_impl("/tmp/vfs-bind-source/file");
    unlink_impl("/tmp/vfs-bind-target/file");
    rmdir_impl("/tmp/vfs-bind-source");
    rmdir_impl("/tmp/vfs-bind-target");
}

static void vfs_contract_cleanup_mount_namespace_paths(void) {
    umount("/tmp/vfs-mntns-peer-a/child/grand");
    umount("/tmp/vfs-mntns-peer-b/child/grand");
    umount("/tmp/vfs-mntns-peer-c/child/grand");
    umount("/tmp/vfs-mntns-peer-a/moved/grand");
    umount("/tmp/vfs-mntns-peer-b/moved/grand");
    umount("/tmp/vfs-mntns-peer-c/moved/grand");
    umount("/tmp/vfs-mntns-parent-source/child/grand");
    umount("/tmp/vfs-mntns-parent-source/child");
    umount("/tmp/vfs-mntns-target/child");
    umount("/tmp/vfs-mntns-peer-a/child");
    umount("/tmp/vfs-mntns-peer-b/child");
    umount("/tmp/vfs-mntns-peer-c/child");
    umount("/tmp/vfs-mntns-peer-a/moved");
    umount("/tmp/vfs-mntns-peer-b/moved");
    umount("/tmp/vfs-mntns-peer-c/moved");
    umount("/tmp/vfs-mntns-peer-a");
    umount("/tmp/vfs-mntns-peer-b");
    umount("/tmp/vfs-mntns-peer-c");
    umount("/tmp/vfs-mntns-target");
    unlink_impl("/tmp/vfs-mntns-grandchild-source/file");
    unlink_impl("/tmp/vfs-mntns-parent-source/file");
    unlink_impl("/tmp/vfs-mntns-parent-source/newfile");
    unlink_impl("/tmp/vfs-mntns-child-source/file");
    unlink_impl("/tmp/vfs-mntns-peer-a/child/grand/file");
    unlink_impl("/tmp/vfs-mntns-peer-b/child/grand/file");
    unlink_impl("/tmp/vfs-mntns-peer-c/child/grand/file");
    unlink_impl("/tmp/vfs-mntns-peer-a/moved/file");
    unlink_impl("/tmp/vfs-mntns-peer-b/moved/file");
    unlink_impl("/tmp/vfs-mntns-peer-c/moved/file");
    unlink_impl("/tmp/vfs-mntns-peer-a/moved/grand/file");
    unlink_impl("/tmp/vfs-mntns-peer-b/moved/grand/file");
    unlink_impl("/tmp/vfs-mntns-peer-c/moved/grand/file");
    unlink_impl("/tmp/vfs-mntns-target/file");
    unlink_impl("/tmp/vfs-mntns-target/newfile");
    unlink_impl("/tmp/vfs-mntns-target/child/file");
    unlink_impl("/tmp/vfs-mntns-source/file");
    unlink_impl("/tmp/vfs-mntns-source/dir/file");
    rmdir_impl("/tmp/vfs-mntns-parent-source/child/grand");
    rmdir_impl("/tmp/vfs-mntns-parent-source/child");
    rmdir_impl("/tmp/vfs-mntns-child-source/grand");
    rmdir_impl("/tmp/vfs-mntns-target/child");
    rmdir_impl("/tmp/vfs-mntns-peer-a/child/grand");
    rmdir_impl("/tmp/vfs-mntns-peer-a/child");
    rmdir_impl("/tmp/vfs-mntns-peer-b/child/grand");
    rmdir_impl("/tmp/vfs-mntns-peer-b/child");
    rmdir_impl("/tmp/vfs-mntns-peer-c/child/grand");
    rmdir_impl("/tmp/vfs-mntns-peer-c/child");
    rmdir_impl("/tmp/vfs-mntns-peer-a/moved/grand");
    rmdir_impl("/tmp/vfs-mntns-peer-a/moved");
    rmdir_impl("/tmp/vfs-mntns-peer-b/moved/grand");
    rmdir_impl("/tmp/vfs-mntns-peer-b/moved");
    rmdir_impl("/tmp/vfs-mntns-peer-c/moved/grand");
    rmdir_impl("/tmp/vfs-mntns-peer-c/moved");
    rmdir_impl("/tmp/vfs-mntns-peer-a");
    rmdir_impl("/tmp/vfs-mntns-peer-b");
    rmdir_impl("/tmp/vfs-mntns-peer-c");
    rmdir_impl("/tmp/vfs-mntns-parent-source");
    rmdir_impl("/tmp/vfs-mntns-child-source");
    rmdir_impl("/tmp/vfs-mntns-grandchild-source");
    rmdir_impl("/tmp/vfs-mntns-target");
    rmdir_impl("/tmp/vfs-mntns-source/dir");
    rmdir_impl("/tmp/vfs-mntns-source");
}

static void vfs_contract_cleanup_pivot_paths(void) {
    umount("/tmp/vfs-pivot-new/oldroot");
    umount("/tmp/vfs-pivot-new");
    unlink_impl("/tmp/vfs-pivot-old-marker");
    unlink_impl("/tmp/vfs-pivot-new/inside");
    rmdir_impl("/tmp/vfs-pivot-new/oldroot");
    rmdir_impl("/tmp/vfs-pivot-new");
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

int vfs_contract_pivot_root_rebases_absolute_paths_and_exposes_old_root(void) {
    struct task_struct *task = get_current();
    char old_root[MAX_PATH];
    char old_pwd[MAX_PATH];
    int ret = -1;

    if (!task || !task->fs) {
        errno = ESRCH;
        return -1;
    }
    __builtin_memcpy(old_root, task->fs->root_path, sizeof(old_root));
    __builtin_memcpy(old_pwd, task->fs->pwd_path, sizeof(old_pwd));

    fs_set_root(task->fs, "/");
    fs_set_pwd(task->fs, "/");
    vfs_contract_cleanup_pivot_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-pivot-new", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-pivot-new/oldroot", 0700)) != 0 ||
        vfs_contract_write_file("/tmp/vfs-pivot-new/inside", "new-root") != 0 ||
        vfs_contract_write_file("/tmp/vfs-pivot-old-marker", "old-root") != 0 ||
        mount("/tmp/vfs-pivot-new", "/tmp/vfs-pivot-new", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }
    if (pivot_root("/tmp/vfs-pivot-new", "/tmp/vfs-pivot-new/oldroot") != 0) {
        goto out;
    }
    if (__builtin_strcmp(task->fs->root_path, "/tmp/vfs-pivot-new") != 0) {
        errno = EPROTO;
        goto out;
    }
    if (vfs_contract_read_file_exact("/inside", "new-root") != 0) {
        goto out;
    }
    if (vfs_contract_read_file_exact("/oldroot/tmp/vfs-pivot-old-marker", "old-root") != 0) {
        errno = ENOMSG;
        goto out;
    }

    ret = 0;

out:
    {
        int saved_errno = errno;
        fs_set_root(task->fs, "/");
        fs_set_pwd(task->fs, "/");
        vfs_contract_cleanup_pivot_paths();
        fs_set_root(task->fs, old_root);
        fs_set_pwd(task->fs, old_pwd);
        errno = saved_errno;
    }
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

int vfs_contract_unprivileged_mount_operations_fail_without_namespace_mutation(void) {
    struct __user_cap_header_struct header = {
        .version = _LINUX_CAPABILITY_VERSION_3,
        .pid = 0,
    };
    struct __user_cap_data_struct data[_LINUX_CAPABILITY_U32S_3];
    struct mnt_id_req req;
    struct statmount st;
    uint64_t ids[4];
    char content[8192];
    int ret = -1;

    cred_reset_to_defaults();
    umount("/tmp/vfs-cred-mount-target");
    umount("/tmp/vfs-cred-mount-peer");
    unlink_impl("/tmp/vfs-cred-mount-source/file");
    unlink_impl("/tmp/vfs-cred-mount-denied/file");
    rmdir_impl("/tmp/vfs-cred-mount-source");
    rmdir_impl("/tmp/vfs-cred-mount-denied");
    rmdir_impl("/tmp/vfs-cred-mount-target");
    rmdir_impl("/tmp/vfs-cred-mount-peer");
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-cred-mount-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-cred-mount-denied", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-cred-mount-target", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-cred-mount-peer", 0700)) != 0 ||
        vfs_contract_write_file("/tmp/vfs-cred-mount-source/file", "privileged") != 0 ||
        vfs_contract_write_file("/tmp/vfs-cred-mount-denied/file", "denied") != 0 ||
        mount("/tmp/vfs-cred-mount-source", "/tmp/vfs-cred-mount-target", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }
    if (capget(&header, data) != 0) {
        goto out;
    }
    data[CAP_SYS_ADMIN / 32].effective &= ~(1U << (CAP_SYS_ADMIN % 32));
    if (capset(&header, data) != 0 ||
        cred_has_cap(get_current_cred(), CAP_SYS_ADMIN)) {
        goto out;
    }

    if (mount("/tmp/vfs-cred-mount-denied", "/tmp/vfs-cred-mount-peer", NULL, MS_BIND, NULL) != -1) {
        errno = ENOMSG;
        goto out;
    }
    if (errno != EPERM) {
        goto out;
    }
    if (open_impl("/tmp/vfs-cred-mount-peer/file", O_RDONLY, 0) != -1 || errno != ENOENT) {
        errno = ENODATA;
        goto out;
    }
    if (mount(NULL, "/tmp/vfs-cred-mount-target", NULL, MS_BIND | MS_REMOUNT | MS_SHARED, NULL) != -1 ||
        errno != EPERM) {
        errno = EIDRM;
        goto out;
    }
    if (vfs_contract_read_proc_file("/proc/self/mountinfo", content, sizeof(content)) != 0 ||
        vfs_contract_content_contains(content, " /tmp/vfs-cred-mount-target rw,relatime shared:")) {
        errno = ENOMSG;
        goto out;
    }
    if (move_mount(AT_FDCWD, "/tmp/vfs-cred-mount-target", AT_FDCWD,
                   "/tmp/vfs-cred-mount-peer", 0) != -1 || errno != EPERM) {
        errno = EXDEV;
        goto out;
    }
    if (vfs_contract_read_file_exact("/tmp/vfs-cred-mount-target/file", "privileged") != 0 ||
        (open_impl("/tmp/vfs-cred-mount-peer/file", O_RDONLY, 0) != -1 || errno != ENOENT)) {
        errno = ENOMSG;
        goto out;
    }
    if (open_tree(AT_FDCWD, "/tmp/vfs-cred-mount-target", OPEN_TREE_CLONE) != -1 || errno != EPERM) {
        errno = ENOTBLK;
        goto out;
    }
    memset(&req, 0, sizeof(req));
    memset(ids, 0, sizeof(ids));
    req.size = MNT_ID_REQ_SIZE_VER1;
    req.mnt_id = LSMT_ROOT;
    if (syscall_dispatch_impl(__NR_listmount, (long)(uintptr_t)&req,
                              (long)(uintptr_t)ids, sizeof(ids) / sizeof(ids[0]), 0, 0, 0) != -EPERM) {
        errno = ERANGE;
        goto out;
    }
    memset(&req, 0, sizeof(req));
    memset(&st, 0, sizeof(st));
    req.size = MNT_ID_REQ_SIZE_VER1;
    req.mnt_id = LSMT_ROOT;
    req.param = STATMOUNT_MNT_BASIC;
    if (syscall_dispatch_impl(__NR_statmount, (long)(uintptr_t)&req,
                              (long)(uintptr_t)&st, sizeof(st), 0, 0, 0) != -EPERM) {
        errno = ERANGE;
        goto out;
    }
    if (umount("/tmp/vfs-cred-mount-target") != -1 || errno != EPERM) {
        errno = EBUSY;
        goto out;
    }
    if (vfs_contract_read_file_exact("/tmp/vfs-cred-mount-target/file", "privileged") != 0) {
        goto out;
    }

    ret = 0;

out:
    {
        int saved_errno = errno;
        cred_reset_to_defaults();
        umount("/tmp/vfs-cred-mount-target");
        umount("/tmp/vfs-cred-mount-peer");
        unlink_impl("/tmp/vfs-cred-mount-source/file");
        unlink_impl("/tmp/vfs-cred-mount-denied/file");
        rmdir_impl("/tmp/vfs-cred-mount-source");
        rmdir_impl("/tmp/vfs-cred-mount-denied");
        rmdir_impl("/tmp/vfs-cred-mount-target");
        rmdir_impl("/tmp/vfs-cred-mount-peer");
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_shared_mount_propagates_child_bind_to_peer(void) {
    int ret = -1;

    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source/child", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-child-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-a", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-b", 0700)) != 0) {
        goto out;
    }
    if (vfs_contract_write_file("/tmp/vfs-mntns-child-source/file", "propagated") != 0) {
        goto out;
    }
    if (mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-a", NULL, MS_BIND | MS_SHARED, NULL) != 0 ||
        mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-b", NULL, MS_BIND | MS_SHARED, NULL) != 0 ||
        mount("/tmp/vfs-mntns-child-source", "/tmp/vfs-mntns-peer-a/child", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }
    if (vfs_contract_read_file_exact("/tmp/vfs-mntns-peer-b/child/file", "propagated") != 0) {
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

static int vfs_contract_mountinfo_shared_id_for_target(const char *content, const char *target, unsigned long long *id_out) {
    const char *line = content;
    size_t target_len;

    if (!content || !target || !id_out) {
        errno = EINVAL;
        return -1;
    }

    target_len = __builtin_strlen(target);
    while (*line) {
        const char *next = __builtin_strchr(line, '\n');
        const char *end = next ? next : line + __builtin_strlen(line);
        const char *scan = line;

        while (scan < end) {
            if ((size_t)(end - scan) >= target_len &&
                __builtin_memcmp(scan, target, target_len) == 0 &&
                (scan == line || scan[-1] == ' ') &&
                (scan + target_len == end || scan[target_len] == ' ')) {
                const char *shared = line;
                while (shared < end) {
                    static const char prefix[] = " shared:";
                    if ((size_t)(end - shared) > sizeof(prefix) - 1 &&
                        __builtin_memcmp(shared, prefix, sizeof(prefix) - 1) == 0) {
                        unsigned long long id = 0;
                        const char *digit = shared + sizeof(prefix) - 1;
                        if (digit >= end || *digit < '0' || *digit > '9') {
                            errno = EPROTO;
                            return -1;
                        }
                        while (digit < end && *digit >= '0' && *digit <= '9') {
                            id = (id * 10ULL) + (unsigned long long)(*digit - '0');
                            digit++;
                        }
                        *id_out = id;
                        return 0;
                    }
                    shared++;
                }
                errno = ENODATA;
                return -1;
            }
            scan++;
        }
        if (!next) {
            break;
        }
        line = next + 1;
    }

    errno = ENOENT;
    return -1;
}

static int vfs_contract_mountinfo_master_id_for_target(const char *content, const char *target, unsigned long long *id_out) {
    const char *line = content;
    size_t target_len;

    if (!content || !target || !id_out) {
        errno = EINVAL;
        return -1;
    }

    target_len = __builtin_strlen(target);
    while (*line) {
        const char *next = __builtin_strchr(line, '\n');
        const char *end = next ? next : line + __builtin_strlen(line);
        const char *scan = line;

        while (scan < end) {
            if ((size_t)(end - scan) >= target_len &&
                __builtin_memcmp(scan, target, target_len) == 0 &&
                (scan == line || scan[-1] == ' ') &&
                (scan + target_len == end || scan[target_len] == ' ')) {
                const char *master = line;
                while (master < end) {
                    static const char prefix[] = " master:";
                    if ((size_t)(end - master) > sizeof(prefix) - 1 &&
                        __builtin_memcmp(master, prefix, sizeof(prefix) - 1) == 0) {
                        unsigned long long id = 0;
                        const char *digit = master + sizeof(prefix) - 1;
                        if (digit >= end || *digit < '0' || *digit > '9') {
                            errno = EPROTO;
                            return -1;
                        }
                        while (digit < end && *digit >= '0' && *digit <= '9') {
                            id = (id * 10ULL) + (unsigned long long)(*digit - '0');
                            digit++;
                        }
                        *id_out = id;
                        return 0;
                    }
                    master++;
                }
                errno = ENODATA;
                return -1;
            }
            scan++;
        }
        if (!next) {
            break;
        }
        line = next + 1;
    }

    errno = ENOENT;
    return -1;
}

static int vfs_contract_mountinfo_ids_for_target(const char *content, const char *target,
                                                 int *mount_id_out, int *parent_id_out) {
    const char *line = content;
    size_t target_len;

    if (!content || !target || !mount_id_out || !parent_id_out) {
        errno = EINVAL;
        return -1;
    }

    target_len = __builtin_strlen(target);
    while (*line) {
        const char *next = __builtin_strchr(line, '\n');
        const char *end = next ? next : line + __builtin_strlen(line);
        const char *scan = line;

        while (scan < end) {
            if ((size_t)(end - scan) >= target_len &&
                __builtin_memcmp(scan, target, target_len) == 0 &&
                (scan == line || scan[-1] == ' ') &&
                (scan + target_len == end || scan[target_len] == ' ')) {
                int mount_id = 0;
                int parent_id = 0;
                const char *cursor = line;

                if (cursor >= end || *cursor < '0' || *cursor > '9') {
                    errno = EPROTO;
                    return -1;
                }
                while (cursor < end && *cursor >= '0' && *cursor <= '9') {
                    mount_id = (mount_id * 10) + (*cursor - '0');
                    cursor++;
                }
                if (cursor >= end || *cursor != ' ') {
                    errno = EPROTO;
                    return -1;
                }
                cursor++;
                if (cursor >= end || *cursor < '0' || *cursor > '9') {
                    errno = EPROTO;
                    return -1;
                }
                while (cursor < end && *cursor >= '0' && *cursor <= '9') {
                    parent_id = (parent_id * 10) + (*cursor - '0');
                    cursor++;
                }
                *mount_id_out = mount_id;
                *parent_id_out = parent_id;
                return 0;
            }
            scan++;
        }
        if (!next) {
            break;
        }
        line = next + 1;
    }

    errno = ENOENT;
    return -1;
}

int vfs_contract_shared_mountinfo_uses_peer_group_ids(void) {
    char content[4096];
    unsigned long long peer_a = 0;
    unsigned long long peer_b = 0;
    unsigned long long child_a = 0;
    int ret = -1;

    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source/child", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-child-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-a", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-b", 0700)) != 0) {
        goto out;
    }
    if (mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-a", NULL, MS_BIND | MS_SHARED, NULL) != 0 ||
        mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-b", NULL, MS_BIND | MS_SHARED, NULL) != 0 ||
        mount("/tmp/vfs-mntns-child-source", "/tmp/vfs-mntns-peer-a/child", NULL, MS_BIND | MS_SHARED, NULL) != 0) {
        goto out;
    }
    if (vfs_contract_read_proc_file("/proc/self/mountinfo", content, sizeof(content)) != 0 ||
        vfs_contract_mountinfo_shared_id_for_target(content, "/tmp/vfs-mntns-peer-a", &peer_a) != 0 ||
        vfs_contract_mountinfo_shared_id_for_target(content, "/tmp/vfs-mntns-peer-b", &peer_b) != 0 ||
        vfs_contract_mountinfo_shared_id_for_target(content, "/tmp/vfs-mntns-peer-a/child", &child_a) != 0) {
        goto out;
    }
    if (peer_a == 0 || peer_a != peer_b || child_a == peer_a) {
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

int vfs_contract_shared_mount_propagates_nested_child_bind_to_peer(void) {
    int ret = -1;

    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source/child", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-child-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-child-source/grand", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-grandchild-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-a", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-b", 0700)) != 0) {
        goto out;
    }
    if (vfs_contract_write_file("/tmp/vfs-mntns-grandchild-source/file", "nested") != 0) {
        goto out;
    }
    if (mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-a", NULL, MS_BIND | MS_SHARED, NULL) != 0 ||
        mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-b", NULL, MS_BIND | MS_SHARED, NULL) != 0 ||
        mount("/tmp/vfs-mntns-child-source", "/tmp/vfs-mntns-peer-a/child", NULL, MS_BIND | MS_SHARED, NULL) != 0 ||
        mount("/tmp/vfs-mntns-grandchild-source", "/tmp/vfs-mntns-peer-a/child/grand", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }
    if (vfs_contract_read_file_exact("/tmp/vfs-mntns-peer-b/child/grand/file", "nested") != 0) {
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

int vfs_contract_shared_mount_unmount_propagates_nested_child_from_peer(void) {
    int ret = -1;

    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source/child", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-child-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-child-source/grand", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-grandchild-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-a", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-b", 0700)) != 0) {
        goto out;
    }
    if (vfs_contract_write_file("/tmp/vfs-mntns-child-source/file", "child") != 0 ||
        vfs_contract_write_file("/tmp/vfs-mntns-grandchild-source/file", "nested") != 0) {
        goto out;
    }
    if (mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-a", NULL, MS_BIND | MS_SHARED, NULL) != 0 ||
        mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-b", NULL, MS_BIND | MS_SHARED, NULL) != 0 ||
        mount("/tmp/vfs-mntns-child-source", "/tmp/vfs-mntns-peer-a/child", NULL, MS_BIND | MS_SHARED, NULL) != 0 ||
        mount("/tmp/vfs-mntns-grandchild-source", "/tmp/vfs-mntns-peer-a/child/grand", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }
    if (umount("/tmp/vfs-mntns-peer-a/child/grand") != 0) {
        goto out;
    }
    if (vfs_contract_read_file_exact("/tmp/vfs-mntns-peer-b/child/file", "child") != 0) {
        goto out;
    }
    if (open_impl("/tmp/vfs-mntns-peer-b/child/grand/file", O_RDONLY, 0) != -1 || errno != ENOENT) {
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

int vfs_contract_recursive_umount_propagates_nested_children_from_shared_peer(void) {
    int ret = -1;

    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source/child", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-child-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-a", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-b", 0700)) != 0) {
        goto out;
    }
    if (vfs_contract_write_file("/tmp/vfs-mntns-child-source/file", "recursive-detach") != 0) {
        goto out;
    }
    if (mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-a", NULL, MS_BIND | MS_SHARED, NULL) != 0 ||
        mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-b", NULL, MS_BIND | MS_SHARED, NULL) != 0 ||
        mount("/tmp/vfs-mntns-child-source", "/tmp/vfs-mntns-peer-a/child", NULL, MS_BIND | MS_SHARED, NULL) != 0) {
        goto out;
    }
    if (vfs_contract_read_file_exact("/tmp/vfs-mntns-peer-b/child/file", "recursive-detach") != 0) {
        goto out;
    }
    if (umount("/tmp/vfs-mntns-peer-a") != 0) {
        goto out;
    }
    if (open_impl("/tmp/vfs-mntns-peer-a/child/file", O_RDONLY, 0) != -1 || errno != ENOENT) {
        errno = ENODATA;
        goto out;
    }
    if (open_impl("/tmp/vfs-mntns-peer-b/child/file", O_RDONLY, 0) != -1 || errno != ENOENT) {
        errno = ENOMSG;
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

int vfs_contract_hardlink_inode_metadata_survives_unlink(void) {
    const char path[] = "/tmp/vfs-hardlink-inode-metadata";
    const char alias[] = "/tmp/vfs-hardlink-inode-metadata-alias";
    const char name[] = "user.hardlink-life";
    const char value[] = "hardlink-value";
    char readback[32];
    struct linux_stat st;
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

int vfs_contract_clone_newns_shared_propagation_stays_inside_child_namespace(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
    struct task_struct *saved = NULL;
    int pid;
    int ret = -1;

    if (!parent || !parent->fs) {
        errno = ESRCH;
        return -1;
    }

    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source/child", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-child-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-a", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-b", 0700)) != 0) {
        goto out_cleanup;
    }
    if (vfs_contract_write_file("/tmp/vfs-mntns-child-source/file", "child-ns") != 0) {
        goto out_cleanup;
    }
    if (mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-a", NULL, MS_BIND | MS_SHARED, NULL) != 0 ||
        mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-b", NULL, MS_BIND | MS_SHARED, NULL) != 0) {
        goto out_cleanup;
    }

    pid = clone_impl(CLONE_NEWNS);
    if (pid < 0) {
        goto out_cleanup;
    }
    child = task_lookup(pid);
    if (!child) {
        errno = ESRCH;
        goto out_cleanup;
    }

    saved = get_current();
    set_current(child);
    if (mount("/tmp/vfs-mntns-child-source", "/tmp/vfs-mntns-peer-a/child", NULL, MS_BIND, NULL) != 0 ||
        vfs_contract_read_file_exact("/tmp/vfs-mntns-peer-b/child/file", "child-ns") != 0) {
        goto out_restore;
    }

    set_current(parent);
    if (open_impl("/tmp/vfs-mntns-peer-b/child/file", O_RDONLY, 0) != -1 || errno != ENOENT) {
        errno = ENODATA;
        goto out_restore;
    }
    ret = 0;

out_restore:
    if (child) {
        set_current(child);
        umount("/tmp/vfs-mntns-peer-a/child");
        umount("/tmp/vfs-mntns-peer-b/child");
    }
    set_current(saved ? saved : parent);
out_cleanup:
    if (child) {
        vfs_contract_release_lookup_child(parent, child);
    }
    vfs_contract_cleanup_mount_namespace_paths();
    return ret;
}

int vfs_contract_mount_namespace_refs_track_task_lifecycle(void) {
    struct task_struct *parent = get_current();
    struct task_struct *shared_child = NULL;
    struct task_struct *private_child = NULL;
    unsigned int initial_refs;
    uint64_t parent_ns;
    int shared_pid;
    int private_pid;
    int ret = -1;

    if (!parent || !parent->fs) {
        errno = ESRCH;
        return -1;
    }

    initial_refs = fs_mount_namespace_refs(parent->fs);
    parent_ns = fs_mount_namespace_id(parent->fs);
    if (initial_refs == 0 || parent_ns == 0) {
        errno = ENODATA;
        return -1;
    }

    shared_pid = clone_impl(0);
    if (shared_pid < 0) {
        return -1;
    }
    shared_child = task_lookup(shared_pid);
    if (!shared_child || !shared_child->fs) {
        errno = ESRCH;
        goto out;
    }
    if (fs_mount_namespace_id(shared_child->fs) != parent_ns ||
        fs_mount_namespace_refs(parent->fs) != initial_refs + 1) {
        errno = ENOMSG;
        goto out;
    }

    private_pid = clone_impl(CLONE_NEWNS);
    if (private_pid < 0) {
        goto out;
    }
    private_child = task_lookup(private_pid);
    if (!private_child || !private_child->fs) {
        errno = ESRCH;
        goto out;
    }
    if (fs_mount_namespace_id(private_child->fs) == parent_ns ||
        fs_mount_namespace_refs(private_child->fs) != 1 ||
        fs_mount_namespace_refs(parent->fs) != initial_refs + 1) {
        errno = ENODATA;
        goto out;
    }

    vfs_contract_release_lookup_child(parent, shared_child);
    shared_child = NULL;
    if (fs_mount_namespace_refs(parent->fs) != initial_refs) {
        errno = EBUSY;
        goto out;
    }
    ret = 0;

out:
    if (shared_child) {
        vfs_contract_release_lookup_child(parent, shared_child);
    }
    if (private_child) {
        vfs_contract_release_lookup_child(parent, private_child);
    }
    return ret;
}

int vfs_contract_clone_newns_rebases_shared_peer_groups(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
    char content[8192];
    unsigned long long parent_peer_a = 0;
    unsigned long long parent_peer_b = 0;
    unsigned long long child_peer_a = 0;
    unsigned long long child_peer_b = 0;
    int pid;
    int ret = -1;

    if (!parent || !parent->fs) {
        errno = ESRCH;
        return -1;
    }

    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source/child", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-child-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-a", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-b", 0700)) != 0 ||
        vfs_contract_write_file("/tmp/vfs-mntns-child-source/file", "rebased") != 0) {
        goto out;
    }
    if (mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-a", NULL, MS_BIND | MS_SHARED, NULL) != 0 ||
        mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-b", NULL, MS_BIND | MS_SHARED, NULL) != 0) {
        goto out;
    }
    if (vfs_contract_read_proc_file("/proc/self/mountinfo", content, sizeof(content)) != 0 ||
        vfs_contract_mountinfo_shared_id_for_target(content, "/tmp/vfs-mntns-peer-a", &parent_peer_a) != 0 ||
        vfs_contract_mountinfo_shared_id_for_target(content, "/tmp/vfs-mntns-peer-b", &parent_peer_b) != 0) {
        goto out;
    }
    if (parent_peer_a == 0 || parent_peer_a != parent_peer_b) {
        errno = ENODATA;
        goto out;
    }

    pid = clone_impl(CLONE_NEWNS);
    if (pid < 0) {
        goto out;
    }
    child = task_lookup(pid);
    if (!child || !child->fs) {
        errno = ESRCH;
        goto out;
    }

    set_current(child);
    if (vfs_contract_read_proc_file("/proc/self/mountinfo", content, sizeof(content)) != 0 ||
        vfs_contract_mountinfo_shared_id_for_target(content, "/tmp/vfs-mntns-peer-a", &child_peer_a) != 0 ||
        vfs_contract_mountinfo_shared_id_for_target(content, "/tmp/vfs-mntns-peer-b", &child_peer_b) != 0) {
        set_current(parent);
        goto out;
    }
    if (child_peer_a == 0 || child_peer_a != child_peer_b || child_peer_a == parent_peer_a) {
        set_current(parent);
        errno = ENOMSG;
        goto out;
    }
    if (mount("/tmp/vfs-mntns-child-source", "/tmp/vfs-mntns-peer-a/child", NULL, MS_BIND, NULL) != 0 ||
        vfs_contract_read_file_exact("/tmp/vfs-mntns-peer-b/child/file", "rebased") != 0) {
        set_current(parent);
        goto out;
    }

    set_current(parent);
    if (open_impl("/tmp/vfs-mntns-peer-b/child/file", O_RDONLY, 0) != -1 || errno != ENOENT) {
        errno = ENODATA;
        goto out;
    }

    ret = 0;

out:
    set_current(parent);
    if (child) {
        task_unlink_child_impl(parent, child);
        free_task(child);
    }
    {
        int saved_errno = errno;
        vfs_contract_cleanup_mount_namespace_paths();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_clone_newns_rebases_slave_master_to_child_peer_group(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
    char content[8192];
    unsigned long long parent_shared = 0;
    unsigned long long parent_master = 0;
    unsigned long long child_shared = 0;
    unsigned long long child_master = 0;
    int pid;
    int ret = -1;

    if (!parent || !parent->fs) {
        errno = ESRCH;
        return -1;
    }

    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source/child", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-child-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-a", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-b", 0700)) != 0 ||
        vfs_contract_write_file("/tmp/vfs-mntns-child-source/file", "slave-rebased") != 0) {
        goto out;
    }
    if (mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-a", NULL, MS_BIND | MS_SHARED, NULL) != 0 ||
        mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-b", NULL, MS_BIND | MS_SHARED, NULL) != 0 ||
        mount(NULL, "/tmp/vfs-mntns-peer-b", NULL, MS_BIND | MS_REMOUNT | MS_SLAVE, NULL) != 0) {
        goto out;
    }
    if (vfs_contract_read_proc_file("/proc/self/mountinfo", content, sizeof(content)) != 0 ||
        vfs_contract_mountinfo_shared_id_for_target(content, "/tmp/vfs-mntns-peer-a", &parent_shared) != 0 ||
        vfs_contract_mountinfo_master_id_for_target(content, "/tmp/vfs-mntns-peer-b", &parent_master) != 0) {
        goto out;
    }
    if (parent_shared == 0 || parent_master != parent_shared) {
        errno = ENODATA;
        goto out;
    }

    pid = clone_impl(CLONE_NEWNS);
    if (pid < 0) {
        goto out;
    }
    child = task_lookup(pid);
    if (!child || !child->fs) {
        errno = ESRCH;
        goto out;
    }

    set_current(child);
    if (vfs_contract_read_proc_file("/proc/self/mountinfo", content, sizeof(content)) != 0 ||
        vfs_contract_mountinfo_shared_id_for_target(content, "/tmp/vfs-mntns-peer-a", &child_shared) != 0 ||
        vfs_contract_mountinfo_master_id_for_target(content, "/tmp/vfs-mntns-peer-b", &child_master) != 0) {
        set_current(parent);
        goto out;
    }
    if (child_shared == 0 || child_shared == parent_shared || child_master != child_shared) {
        set_current(parent);
        errno = ENOMSG;
        goto out;
    }
    if (mount("/tmp/vfs-mntns-child-source", "/tmp/vfs-mntns-peer-a/child", NULL, MS_BIND, NULL) != 0 ||
        vfs_contract_read_file_exact("/tmp/vfs-mntns-peer-b/child/file", "slave-rebased") != 0) {
        set_current(parent);
        goto out;
    }

    set_current(parent);
    if (open_impl("/tmp/vfs-mntns-peer-b/child/file", O_RDONLY, 0) != -1 || errno != ENOENT) {
        errno = ENODATA;
        goto out;
    }

    ret = 0;

out:
    set_current(parent);
    if (child) {
        task_unlink_child_impl(parent, child);
        free_task(child);
    }
    {
        int saved_errno = errno;
        vfs_contract_cleanup_mount_namespace_paths();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_private_child_unmount_does_not_propagate_to_shared_peer(void) {
    int ret = -1;

    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source/child", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-child-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-a", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-b", 0700)) != 0) {
        goto out;
    }
    if (vfs_contract_write_file("/tmp/vfs-mntns-child-source/file", "private-child") != 0) {
        goto out;
    }
    if (mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-a", NULL, MS_BIND | MS_SHARED, NULL) != 0 ||
        mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-b", NULL, MS_BIND | MS_SHARED, NULL) != 0 ||
        mount("/tmp/vfs-mntns-child-source", "/tmp/vfs-mntns-peer-a/child", NULL, MS_BIND | MS_SHARED, NULL) != 0) {
        goto out;
    }
    if (vfs_contract_read_file_exact("/tmp/vfs-mntns-peer-b/child/file", "private-child") != 0) {
        goto out;
    }
    if (mount(NULL, "/tmp/vfs-mntns-peer-a/child", NULL, MS_BIND | MS_REMOUNT | MS_PRIVATE, NULL) != 0) {
        goto out;
    }
    if (umount("/tmp/vfs-mntns-peer-a/child") != 0) {
        goto out;
    }
    if (open_impl("/tmp/vfs-mntns-peer-a/child/file", O_RDONLY, 0) != -1 || errno != ENOENT) {
        errno = ENODATA;
        goto out;
    }
    if (vfs_contract_read_file_exact("/tmp/vfs-mntns-peer-b/child/file", "private-child") != 0) {
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

int vfs_contract_umount_busy_when_open_fd_pins_mount_tree(void) {
    int fd = -1;
    int ret = -1;

    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-source/dir", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0 ||
        vfs_contract_write_file("/tmp/vfs-mntns-source/dir/file", "busy") != 0 ||
        mount("/tmp/vfs-mntns-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }

    fd = open_impl("/tmp/vfs-mntns-target/dir/file", O_RDONLY, 0);
    if (fd < 0) {
        goto out;
    }
    if (umount("/tmp/vfs-mntns-target") != -1 || errno != EBUSY) {
        errno = ENODATA;
        goto out;
    }
    if (close_impl(fd) != 0) {
        fd = -1;
        goto out;
    }
    fd = -1;
    if (umount("/tmp/vfs-mntns-target") != 0) {
        goto out;
    }

    ret = 0;

out:
    {
        int saved_errno = errno;
        if (fd >= 0) {
            close_impl(fd);
        }
        vfs_contract_cleanup_mount_namespace_paths();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_lazy_umount_detaches_busy_mount_from_namespace(void) {
    int fd = -1;
    int ret = -1;

    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-source/dir", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0 ||
        vfs_contract_write_file("/tmp/vfs-mntns-source/dir/file", "lazy") != 0 ||
        mount("/tmp/vfs-mntns-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }

    fd = open_impl("/tmp/vfs-mntns-target/dir/file", O_RDONLY, 0);
    if (fd < 0) {
        goto out;
    }
    if (vfs_umount_lazy("/tmp/vfs-mntns-target") != 0) {
        goto out;
    }
    if (open_impl("/tmp/vfs-mntns-target/dir/file", O_RDONLY, 0) != -1 || errno != ENOENT) {
        errno = ENODATA;
        goto out;
    }
    if (close_impl(fd) != 0) {
        fd = -1;
        goto out;
    }
    fd = -1;
    ret = 0;

out:
    {
        int saved_errno = errno;
        if (fd >= 0) {
            close_impl(fd);
        }
        vfs_contract_cleanup_mount_namespace_paths();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_lazy_umount_removes_busy_mount_from_proc_mountinfo(void) {
    char content[8192];
    int fd = -1;
    int ret = -1;

    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-source/dir", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0 ||
        vfs_contract_write_file("/tmp/vfs-mntns-source/dir/file", "lazy-info") != 0 ||
        mount("/tmp/vfs-mntns-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }
    fd = open_impl("/tmp/vfs-mntns-target/dir/file", O_RDONLY, 0);
    if (fd < 0) {
        goto out;
    }
    if (vfs_contract_read_proc_file("/proc/self/mountinfo", content, sizeof(content)) != 0 ||
        !vfs_contract_content_contains(content, " /tmp/vfs-mntns-target ")) {
        errno = ENODATA;
        goto out;
    }
    if (vfs_umount_lazy("/tmp/vfs-mntns-target") != 0) {
        goto out;
    }
    memset(content, 0, sizeof(content));
    if (vfs_contract_read_proc_file("/proc/self/mountinfo", content, sizeof(content)) != 0 ||
        vfs_contract_content_contains(content, " /tmp/vfs-mntns-target ")) {
        errno = ENOMSG;
        goto out;
    }
    ret = 0;

out:
    {
        int saved_errno = errno;
        if (fd >= 0) {
            close_impl(fd);
        }
        vfs_contract_cleanup_mount_namespace_paths();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_umount_expire_requires_mark_then_unmount(void) {
    int ret = -1;

    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0 ||
        vfs_contract_write_file("/tmp/vfs-mntns-source/file", "expire") != 0 ||
        mount("/tmp/vfs-mntns-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }
    if (vfs_umount_expire("/tmp/vfs-mntns-target") != -EAGAIN) {
        errno = ENODATA;
        goto out;
    }
    if (vfs_contract_read_file_exact("/tmp/vfs-mntns-target/file", "expire") != 0) {
        goto out;
    }
    if (vfs_umount_expire("/tmp/vfs-mntns-target") != 0) {
        goto out;
    }
    if (open_impl("/tmp/vfs-mntns-target/file", O_RDONLY, 0) != -1 || errno != ENOENT) {
        errno = ENOMSG;
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

int vfs_contract_lazy_umount_reclaims_detached_ref_after_pin_release(void) {
    int fd = -1;
    int ret = -1;

    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-source/dir", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0 ||
        vfs_contract_write_file("/tmp/vfs-mntns-source/dir/file", "detached-ref") != 0 ||
        mount("/tmp/vfs-mntns-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }
    fd = open_impl("/tmp/vfs-mntns-target/dir/file", O_RDONLY, 0);
    if (fd < 0) {
        goto out;
    }
    if (vfs_umount_lazy("/tmp/vfs-mntns-target") != 0 ||
        vfs_detached_mount_ref_count() == 0) {
        errno = ENODATA;
        goto out;
    }
    if (vfs_reap_detached_mount_refs() != 0 || vfs_detached_mount_ref_count() == 0) {
        errno = ENOMSG;
        goto out;
    }
    if (close_impl(fd) != 0) {
        fd = -1;
        goto out;
    }
    fd = -1;
    if (vfs_reap_detached_mount_refs() != 1 || vfs_detached_mount_ref_count() != 0) {
        errno = EBUSY;
        goto out;
    }
    ret = 0;

out:
    {
        int saved_errno = errno;
        if (fd >= 0) {
            close_impl(fd);
        }
        vfs_contract_cleanup_mount_namespace_paths();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_umount2_detach_detaches_busy_mount_from_namespace(void) {
    int fd = -1;
    int ret = -1;

    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-source/dir", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0 ||
        vfs_contract_write_file("/tmp/vfs-mntns-source/dir/file", "detach") != 0 ||
        mount("/tmp/vfs-mntns-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }
    fd = open_impl("/tmp/vfs-mntns-target/dir/file", O_RDONLY, 0);
    if (fd < 0) {
        goto out;
    }
    if (umount2("/tmp/vfs-mntns-target", MNT_DETACH) != 0 ||
        open_impl("/tmp/vfs-mntns-target/dir/file", O_RDONLY, 0) != -1 ||
        errno != ENOENT) {
        errno = ENODATA;
        goto out;
    }
    if (vfs_detached_mount_ref_count() == 0) {
        errno = ENOMSG;
        goto out;
    }
    ret = 0;

out:
    {
        int saved_errno = errno;
        if (fd >= 0) {
            close_impl(fd);
        }
        vfs_contract_cleanup_mount_namespace_paths();
        vfs_reap_detached_mount_refs();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_umount2_rejects_unused_linux_umount_flag(void) {
    int ret = -1;

    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0 ||
        vfs_contract_write_file("/tmp/vfs-mntns-source/file", "unused-flag") != 0 ||
        mount("/tmp/vfs-mntns-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }
    if (umount2("/tmp/vfs-mntns-target", UMOUNT_UNUSED) != -1 || errno != EINVAL) {
        errno = ENODATA;
        goto out;
    }
    if (vfs_contract_read_file_exact("/tmp/vfs-mntns-target/file", "unused-flag") != 0) {
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

int vfs_contract_umount2_force_detaches_busy_mount_and_reaps_after_pin_release(void) {
    int fd = -1;
    int ret = -1;

    vfs_contract_cleanup_mount_namespace_paths();
    vfs_reap_detached_mount_refs();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-source/dir", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0 ||
        vfs_contract_write_file("/tmp/vfs-mntns-source/dir/file", "force") != 0 ||
        mount("/tmp/vfs-mntns-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }
    fd = open_impl("/tmp/vfs-mntns-target/dir/file", O_RDONLY, 0);
    if (fd < 0) {
        goto out;
    }
    if (umount("/tmp/vfs-mntns-target") != -1 || errno != EBUSY) {
        errno = ENODATA;
        goto out;
    }
    if (umount2("/tmp/vfs-mntns-target", MNT_FORCE) != 0 ||
        vfs_detached_mount_ref_count() == 0) {
        errno = ENOMSG;
        goto out;
    }
    if (open_impl("/tmp/vfs-mntns-target/dir/file", O_RDONLY, 0) != -1 || errno != ENOENT) {
        errno = ENOLINK;
        goto out;
    }
    if (vfs_reap_detached_mount_refs() != 0 || vfs_detached_mount_ref_count() == 0) {
        errno = EBUSY;
        goto out;
    }
    if (close_impl(fd) != 0) {
        fd = -1;
        goto out;
    }
    fd = -1;
    if (vfs_reap_detached_mount_refs() != 1 || vfs_detached_mount_ref_count() != 0) {
        errno = EBUSY;
        goto out;
    }

    ret = 0;

out:
    {
        int saved_errno = errno;
        if (fd >= 0) {
            close_impl(fd);
        }
        vfs_contract_cleanup_mount_namespace_paths();
        vfs_reap_detached_mount_refs();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_force_umount_detached_refs_are_mount_namespace_scoped(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
    int fd = -1;
    int pid;
    int ret = -1;

    if (!parent || !parent->fs) {
        errno = ESRCH;
        return -1;
    }

    vfs_contract_cleanup_mount_namespace_paths();
    vfs_reap_detached_mount_refs();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0 ||
        vfs_contract_write_file("/tmp/vfs-mntns-source/file", "ns-detached") != 0 ||
        mount("/tmp/vfs-mntns-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }
    fd = open_impl("/tmp/vfs-mntns-target/file", O_RDONLY, 0);
    if (fd < 0) {
        goto out;
    }

    pid = clone_impl(CLONE_NEWNS);
    if (pid < 0) {
        goto out;
    }
    child = task_lookup(pid);
    if (!child || !child->fs) {
        errno = ESRCH;
        goto out;
    }

    set_current(child);
    if (umount2("/tmp/vfs-mntns-target", MNT_FORCE) != 0 ||
        vfs_detached_mount_ref_count() != 0) {
        set_current(parent);
        errno = ENODATA;
        goto out;
    }

    set_current(parent);
    if (vfs_contract_read_file_exact("/tmp/vfs-mntns-target/file", "ns-detached") != 0) {
        goto out;
    }
    if (vfs_reap_detached_mount_refs() != 0 || vfs_detached_mount_ref_count() != 0) {
        errno = ENOMSG;
        goto out;
    }

    ret = 0;

out:
    set_current(parent);
    {
        int saved_errno = errno;
        if (fd >= 0) {
            close_impl(fd);
        }
        if (child) {
            vfs_contract_release_lookup_child(parent, child);
        }
        vfs_contract_cleanup_mount_namespace_paths();
        vfs_reap_detached_mount_refs();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_force_umount_propagates_shared_slave_subtree_teardown(void) {
    int fd = -1;
    int ret = -1;

    vfs_contract_cleanup_mount_namespace_paths();
    vfs_reap_detached_mount_refs();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source/child", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-child-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-a", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-b", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-c", 0700)) != 0 ||
        vfs_contract_write_file("/tmp/vfs-mntns-child-source/file", "force-propagated") != 0) {
        goto out;
    }
    if (mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-a", NULL, MS_BIND | MS_SHARED, NULL) != 0 ||
        mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-b", NULL, MS_BIND | MS_SHARED, NULL) != 0 ||
        mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-c", NULL, MS_BIND | MS_SLAVE, NULL) != 0 ||
        mount("/tmp/vfs-mntns-child-source", "/tmp/vfs-mntns-peer-a/child", NULL, MS_BIND | MS_SHARED, NULL) != 0) {
        goto out;
    }
    if (vfs_contract_read_file_exact("/tmp/vfs-mntns-peer-b/child/file", "force-propagated") != 0 ||
        vfs_contract_read_file_exact("/tmp/vfs-mntns-peer-c/child/file", "force-propagated") != 0) {
        goto out;
    }
    fd = open_impl("/tmp/vfs-mntns-peer-a/child/file", O_RDONLY, 0);
    if (fd < 0) {
        goto out;
    }
    if (umount2("/tmp/vfs-mntns-peer-a/child", MNT_FORCE) != 0 ||
        vfs_detached_mount_ref_count() == 0) {
        errno = ENODATA;
        goto out;
    }
    if (open_impl("/tmp/vfs-mntns-peer-a/child/file", O_RDONLY, 0) != -1 || errno != ENOENT) {
        errno = ENOMSG;
        goto out;
    }
    if (open_impl("/tmp/vfs-mntns-peer-b/child/file", O_RDONLY, 0) != -1 || errno != ENOENT) {
        errno = ENOMSG;
        goto out;
    }
    if (open_impl("/tmp/vfs-mntns-peer-c/child/file", O_RDONLY, 0) != -1 || errno != ENOENT) {
        errno = ENOMSG;
        goto out;
    }
    if (close_impl(fd) != 0) {
        fd = -1;
        goto out;
    }
    fd = -1;
    if (vfs_reap_detached_mount_refs() != 1 || vfs_detached_mount_ref_count() != 0) {
        errno = EBUSY;
        goto out;
    }

    ret = 0;

out:
    {
        int saved_errno = errno;
        if (fd >= 0) {
            close_impl(fd);
        }
        vfs_contract_cleanup_mount_namespace_paths();
        vfs_reap_detached_mount_refs();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_umount2_expire_requires_mark_then_unmount(void) {
    int ret = -1;

    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0 ||
        vfs_contract_write_file("/tmp/vfs-mntns-source/file", "expire2") != 0 ||
        mount("/tmp/vfs-mntns-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }
    if (umount2("/tmp/vfs-mntns-target", MNT_EXPIRE) != -1 || errno != EAGAIN) {
        errno = ENODATA;
        goto out;
    }
    if (vfs_contract_read_file_exact("/tmp/vfs-mntns-target/file", "expire2") != 0) {
        goto out;
    }
    if (umount2("/tmp/vfs-mntns-target", MNT_EXPIRE) != 0) {
        goto out;
    }
    if (open_impl("/tmp/vfs-mntns-target/file", O_RDONLY, 0) != -1 || errno != ENOENT) {
        errno = ENOMSG;
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

int vfs_contract_umount2_rejects_expire_with_detach(void) {
    int ret = -1;

    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0 ||
        vfs_contract_write_file("/tmp/vfs-mntns-source/file", "combo") != 0 ||
        mount("/tmp/vfs-mntns-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }
    if (umount2("/tmp/vfs-mntns-target", MNT_EXPIRE | MNT_DETACH) != -1 || errno != EINVAL) {
        errno = ENODATA;
        goto out;
    }
    if (vfs_contract_read_file_exact("/tmp/vfs-mntns-target/file", "combo") != 0) {
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

int vfs_contract_umount2_nofollow_rejects_symlink_target(void) {
    int ret = -1;

    vfs_contract_cleanup_mount_namespace_paths();
    unlinkat(AT_FDCWD, "/tmp/vfs-mntns-link", 0);
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0 ||
        vfs_contract_write_file("/tmp/vfs-mntns-source/file", "nofollow") != 0 ||
        mount("/tmp/vfs-mntns-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0 ||
        symlinkat("/tmp/vfs-mntns-target", AT_FDCWD, "/tmp/vfs-mntns-link") != 0) {
        goto out;
    }
    if (umount2("/tmp/vfs-mntns-link", UMOUNT_NOFOLLOW) != -1 || errno != EINVAL) {
        errno = ENODATA;
        goto out;
    }
    if (vfs_contract_read_file_exact("/tmp/vfs-mntns-target/file", "nofollow") != 0) {
        goto out;
    }
    ret = 0;

out:
    {
        int saved_errno = errno;
        unlinkat(AT_FDCWD, "/tmp/vfs-mntns-link", 0);
        vfs_contract_cleanup_mount_namespace_paths();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_umount_busy_when_pwd_pins_mount_tree(void) {
    struct task_struct *task = get_current();
    char old_root[MAX_PATH];
    char old_pwd[MAX_PATH];
    int ret = -1;

    if (!task || !task->fs) {
        errno = ESRCH;
        return -1;
    }
    fs_mutex_lock(&task->fs->lock);
    memcpy(old_root, task->fs->root_path, sizeof(old_root));
    memcpy(old_pwd, task->fs->pwd_path, sizeof(old_pwd));
    fs_mutex_unlock(&task->fs->lock);

    vfs_contract_cleanup_mount_namespace_paths();
    fs_set_root(task->fs, "/");
    fs_set_pwd(task->fs, "/");
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-source/dir", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0 ||
        mount("/tmp/vfs-mntns-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0 ||
        chdir("/tmp/vfs-mntns-target/dir") != 0) {
        goto out;
    }

    if (umount("/tmp/vfs-mntns-target") != -1 || errno != EBUSY) {
        errno = ENODATA;
        goto out;
    }
    if (chdir("/") != 0 ||
        umount("/tmp/vfs-mntns-target") != 0) {
        goto out;
    }

    ret = 0;

out:
    {
        int saved_errno = errno;
        fs_set_root(task->fs, old_root);
        fs_set_pwd(task->fs, old_pwd);
        vfs_contract_cleanup_mount_namespace_paths();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_umount_busy_when_root_pins_mount_tree(void) {
    struct task_struct *task = get_current();
    char old_root[MAX_PATH];
    char old_pwd[MAX_PATH];
    int ret = -1;

    if (!task || !task->fs) {
        errno = ESRCH;
        return -1;
    }
    fs_mutex_lock(&task->fs->lock);
    memcpy(old_root, task->fs->root_path, sizeof(old_root));
    memcpy(old_pwd, task->fs->pwd_path, sizeof(old_pwd));
    fs_mutex_unlock(&task->fs->lock);

    vfs_contract_cleanup_mount_namespace_paths();
    fs_set_root(task->fs, "/");
    fs_set_pwd(task->fs, "/");
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0 ||
        mount("/tmp/vfs-mntns-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }

    if (fs_set_root(task->fs, "/tmp/vfs-mntns-target") != 0 ||
        fs_set_pwd(task->fs, "/tmp/vfs-mntns-target") != 0) {
        goto out;
    }
    if (umount("/") != -1 || errno != EBUSY) {
        errno = ENODATA;
        goto out;
    }
    fs_set_root(task->fs, "/");
    fs_set_pwd(task->fs, "/");
    if (umount("/tmp/vfs-mntns-target") != 0) {
        goto out;
    }

    ret = 0;

out:
    {
        int saved_errno = errno;
        fs_set_root(task->fs, old_root);
        fs_set_pwd(task->fs, old_pwd);
        vfs_contract_cleanup_mount_namespace_paths();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_slave_mount_receives_nested_child_from_shared_master(void) {
    int ret = -1;

    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source/child", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-child-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-a", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-b", 0700)) != 0) {
        goto out;
    }
    if (vfs_contract_write_file("/tmp/vfs-mntns-child-source/file", "slave-propagated") != 0) {
        goto out;
    }
    if (mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-a", NULL, MS_BIND | MS_SHARED, NULL) != 0 ||
        mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-b", NULL, MS_BIND | MS_SLAVE, NULL) != 0 ||
        mount("/tmp/vfs-mntns-child-source", "/tmp/vfs-mntns-peer-a/child", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }
    if (vfs_contract_read_file_exact("/tmp/vfs-mntns-peer-b/child/file", "slave-propagated") != 0) {
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

int vfs_contract_mount_setattr_recursive_marks_child_private(void) {
    struct mount_attr attr;
    char content[4096];
    int ret = -1;

    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source/child", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-child-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-a", 0700)) != 0) {
        goto out;
    }
    if (mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-a", NULL, MS_BIND | MS_SHARED, NULL) != 0 ||
        mount("/tmp/vfs-mntns-child-source", "/tmp/vfs-mntns-peer-a/child", NULL, MS_BIND | MS_SHARED, NULL) != 0) {
        goto out;
    }
    memset(&attr, 0, sizeof(attr));
    attr.propagation = MS_PRIVATE;
    if (mount_setattr(AT_FDCWD, "/tmp/vfs-mntns-peer-a", AT_RECURSIVE,
                      &attr, sizeof(attr)) != 0) {
        goto out;
    }
    if (vfs_contract_read_proc_file("/proc/self/mountinfo", content, sizeof(content)) != 0) {
        goto out;
    }
    if (vfs_contract_content_contains(content, " /tmp/vfs-mntns-peer-a rw,relatime shared:") ||
        vfs_contract_content_contains(content, " /tmp/vfs-mntns-peer-a/child rw,relatime shared:")) {
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

int vfs_contract_recursive_remount_private_marks_child_private(void) {
    char content[4096];
    int ret = -1;

    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source/child", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-child-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-a", 0700)) != 0) {
        goto out;
    }
    if (mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-a", NULL, MS_BIND | MS_SHARED, NULL) != 0 ||
        mount("/tmp/vfs-mntns-child-source", "/tmp/vfs-mntns-peer-a/child", NULL, MS_BIND | MS_SHARED, NULL) != 0) {
        goto out;
    }
    if (mount(NULL, "/tmp/vfs-mntns-peer-a", NULL,
              MS_BIND | MS_REMOUNT | MS_REC | MS_PRIVATE, NULL) != 0) {
        goto out;
    }
    if (vfs_contract_read_proc_file("/proc/self/mountinfo", content, sizeof(content)) != 0) {
        goto out;
    }
    if (vfs_contract_content_contains(content, " /tmp/vfs-mntns-peer-a rw,relatime shared:") ||
        vfs_contract_content_contains(content, " /tmp/vfs-mntns-peer-a/child rw,relatime shared:") ||
        vfs_contract_content_contains(content, " /tmp/vfs-mntns-peer-a/child rw,relatime master:")) {
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

int vfs_contract_recursive_remount_slave_preserves_peer_group_masters(void) {
    char content[4096];
    unsigned long long parent_shared = 0;
    unsigned long long child_shared = 0;
    unsigned long long parent_master = 0;
    unsigned long long child_master = 0;
    int ret = -1;

    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source/child", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-child-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-a", 0700)) != 0) {
        goto out;
    }
    if (mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-a", NULL, MS_BIND | MS_SHARED, NULL) != 0 ||
        mount("/tmp/vfs-mntns-child-source", "/tmp/vfs-mntns-peer-a/child", NULL, MS_BIND | MS_SHARED, NULL) != 0 ||
        vfs_contract_read_proc_file("/proc/self/mountinfo", content, sizeof(content)) != 0 ||
        vfs_contract_mountinfo_shared_id_for_target(content, "/tmp/vfs-mntns-peer-a", &parent_shared) != 0 ||
        vfs_contract_mountinfo_shared_id_for_target(content, "/tmp/vfs-mntns-peer-a/child", &child_shared) != 0) {
        goto out;
    }
    if (mount(NULL, "/tmp/vfs-mntns-peer-a", NULL,
              MS_BIND | MS_REMOUNT | MS_REC | MS_SLAVE, NULL) != 0 ||
        vfs_contract_read_proc_file("/proc/self/mountinfo", content, sizeof(content)) != 0 ||
        vfs_contract_mountinfo_master_id_for_target(content, "/tmp/vfs-mntns-peer-a", &parent_master) != 0 ||
        vfs_contract_mountinfo_master_id_for_target(content, "/tmp/vfs-mntns-peer-a/child", &child_master) != 0) {
        goto out;
    }
    if (parent_master != parent_shared || child_master != child_shared ||
        vfs_contract_content_contains(content, " /tmp/vfs-mntns-peer-a rw,relatime shared:") ||
        vfs_contract_content_contains(content, " /tmp/vfs-mntns-peer-a/child rw,relatime shared:")) {
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

static const char *vfs_contract_statmount_string(const struct statmount *st, __u32 off) {
    return st && off < st->size ? st->str + off : NULL;
}

int vfs_contract_listmount_statmount_reports_slave_master(void) {
    struct mnt_id_req req;
    char statmount_storage[sizeof(struct statmount) + 512];
    struct statmount *st = (struct statmount *)statmount_storage;
    uint64_t ids[16];
    int ret = -1;
    long count;

    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-a", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-b", 0700)) != 0) {
        goto out;
    }
    if (mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-a", NULL, MS_BIND | MS_SHARED, NULL) != 0 ||
        mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-b", NULL, MS_BIND | MS_SLAVE, NULL) != 0) {
        goto out;
    }

    memset(&req, 0, sizeof(req));
    req.size = MNT_ID_REQ_SIZE_VER1;
    req.mnt_id = LSMT_ROOT;
    count = syscall_dispatch_impl(__NR_listmount, (long)(uintptr_t)&req,
                                  (long)(uintptr_t)ids,
                                  sizeof(ids) / sizeof(ids[0]), 0, 0, 0);
    if (count <= 0) {
        errno = count < 0 ? (int)-count : ENODATA;
        goto out;
    }

    for (long i = 0; i < count; i++) {
        memset(&req, 0, sizeof(req));
        memset(statmount_storage, 0, sizeof(statmount_storage));
        req.size = MNT_ID_REQ_SIZE_VER1;
        req.mnt_id = ids[i];
        req.param = STATMOUNT_MNT_BASIC | STATMOUNT_MNT_POINT | STATMOUNT_PROPAGATE_FROM;
        if (syscall_dispatch_impl(__NR_statmount, (long)(uintptr_t)&req,
                                  (long)(uintptr_t)st, sizeof(statmount_storage), 0, 0, 0) != 0) {
            continue;
        }
        if (st->mnt_point != 0 &&
            strcmp(vfs_contract_statmount_string(st, st->mnt_point), "/tmp/vfs-mntns-peer-b") == 0) {
            if (st->mnt_propagation != MS_SLAVE || st->mnt_master == 0 || st->propagate_from == 0) {
                errno = ENODATA;
                goto out;
            }
            ret = 0;
            goto out;
        }
    }
    errno = ENOENT;

out:
    {
        int saved_errno = errno;
        vfs_contract_cleanup_mount_namespace_paths();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_mountinfo_reports_nested_parent_mount_id(void) {
    char content[4096];
    int parent_mount_id = 0;
    int parent_parent_id = 0;
    int child_mount_id = 0;
    int child_parent_id = 0;
    int ret = -1;

    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source/child", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-child-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-a", 0700)) != 0) {
        goto out;
    }
    if (mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-a", NULL, MS_BIND | MS_SHARED, NULL) != 0 ||
        mount("/tmp/vfs-mntns-child-source", "/tmp/vfs-mntns-peer-a/child", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }
    if (vfs_contract_read_proc_file("/proc/self/mountinfo", content, sizeof(content)) != 0 ||
        vfs_contract_mountinfo_ids_for_target(content, "/tmp/vfs-mntns-peer-a", &parent_mount_id, &parent_parent_id) != 0 ||
        vfs_contract_mountinfo_ids_for_target(content, "/tmp/vfs-mntns-peer-a/child", &child_mount_id, &child_parent_id) != 0) {
        goto out;
    }
    if (parent_mount_id <= 1 || parent_parent_id != 1 || child_mount_id <= parent_mount_id ||
        child_parent_id != parent_mount_id) {
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

int vfs_contract_recursive_bind_clones_nested_mount_topology(void) {
    int ret = -1;

    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source/child", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-child-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0) {
        goto out;
    }
    if (vfs_contract_write_file("/tmp/vfs-mntns-parent-source/file", "parent") != 0 ||
        vfs_contract_write_file("/tmp/vfs-mntns-child-source/file", "child") != 0) {
        goto out;
    }
    if (mount("/tmp/vfs-mntns-child-source", "/tmp/vfs-mntns-parent-source/child", NULL, MS_BIND, NULL) != 0 ||
        mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-target", NULL, MS_BIND | MS_REC, NULL) != 0) {
        goto out;
    }
    if (vfs_contract_read_file_exact("/tmp/vfs-mntns-target/file", "parent") != 0 ||
        vfs_contract_read_file_exact("/tmp/vfs-mntns-target/child/file", "child") != 0) {
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

int vfs_contract_move_mount_relocates_bind_subtree(void) {
    int ret = -1;

    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-a", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0) {
        goto out;
    }
    if (vfs_contract_write_file("/tmp/vfs-mntns-parent-source/file", "moved") != 0) {
        goto out;
    }
    if (mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }
    if (mount("/tmp/vfs-mntns-target", "/tmp/vfs-mntns-peer-a", NULL, MS_MOVE, NULL) != 0) {
        goto out;
    }
    if (vfs_contract_read_file_exact("/tmp/vfs-mntns-peer-a/file", "moved") != 0) {
        goto out;
    }
    if (open_impl("/tmp/vfs-mntns-target/file", O_RDONLY, 0) != -1 || errno != ENOENT) {
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

int vfs_contract_open_tree_clone_returns_mount_fd_visible_in_proc(void) {
    char proc_path[64];
    char link_target[MAX_PATH];
    long link_len;
    int tree_fd = -1;
    int ret = -1;

    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0) {
        goto out;
    }
    if (vfs_contract_write_file("/tmp/vfs-mntns-parent-source/file", "tree") != 0 ||
        mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }

    tree_fd = (int)syscall_dispatch_impl(__NR_open_tree, AT_FDCWD,
                                         (long)(uintptr_t)"/tmp/vfs-mntns-target",
                                         OPEN_TREE_CLONE | OPEN_TREE_CLOEXEC, 0, 0, 0);
    if (tree_fd < 0) {
        errno = (int)-tree_fd;
        goto out;
    }
    if ((fcntl_impl(tree_fd, F_GETFD) & FD_CLOEXEC) == 0) {
        errno = ENODATA;
        goto out;
    }
    if (vfs_contract_proc_fd_path(tree_fd, proc_path, sizeof(proc_path)) != 0) {
        goto out;
    }
    link_len = readlink_impl(proc_path, link_target, sizeof(link_target) - 1);
    if (link_len < 0) {
        goto out;
    }
    link_target[link_len] = '\0';
    if (!vfs_contract_content_contains(link_target, "mnt:[") ||
        vfs_contract_content_contains(link_target, vfs_persistent_backing_root()) ||
        vfs_contract_content_contains(link_target, vfs_temp_backing_root())) {
        errno = ENODATA;
        goto out;
    }

    ret = 0;

out:
    {
        int saved_errno = errno;
        if (tree_fd >= 0) {
            close_impl(tree_fd);
        }
        vfs_contract_cleanup_mount_namespace_paths();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_move_mount_attaches_open_tree_clone(void) {
    int tree_fd = -1;
    int ret = -1;

    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-a", 0700)) != 0) {
        goto out;
    }
    if (vfs_contract_write_file("/tmp/vfs-mntns-parent-source/file", "tree") != 0 ||
        mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }

    tree_fd = (int)syscall_dispatch_impl(__NR_open_tree, AT_FDCWD,
                                         (long)(uintptr_t)"/tmp/vfs-mntns-target",
                                         OPEN_TREE_CLONE, 0, 0, 0);
    if (tree_fd < 0) {
        errno = (int)-tree_fd;
        goto out;
    }
    {
        long move_ret = syscall_dispatch_impl(__NR_move_mount, tree_fd, (long)(uintptr_t)"",
                                              AT_FDCWD, (long)(uintptr_t)"/tmp/vfs-mntns-peer-a",
                                              MOVE_MOUNT_F_EMPTY_PATH, 0);
        if (move_ret != 0) {
            errno = move_ret < 0 ? (int)-move_ret : EPROTO;
            goto out;
        }
    }
    if (vfs_contract_read_file_exact("/tmp/vfs-mntns-peer-a/file", "tree") != 0 ||
        vfs_contract_read_file_exact("/tmp/vfs-mntns-target/file", "tree") != 0) {
        goto out;
    }

    ret = 0;

out:
    {
        int saved_errno = errno;
        if (tree_fd >= 0) {
            close_impl(tree_fd);
        }
        vfs_contract_cleanup_mount_namespace_paths();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_open_tree_clone_survives_source_unmount_until_attached(void) {
    int tree_fd = -1;
    int ret = -1;

    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-a", 0700)) != 0 ||
        vfs_contract_write_file("/tmp/vfs-mntns-parent-source/file", "detached-tree") != 0 ||
        mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }
    tree_fd = (int)syscall_dispatch_impl(__NR_open_tree, AT_FDCWD,
                                         (long)(uintptr_t)"/tmp/vfs-mntns-target",
                                         OPEN_TREE_CLONE, 0, 0, 0);
    if (tree_fd < 0) {
        errno = (int)-tree_fd;
        goto out;
    }
    if (umount("/tmp/vfs-mntns-target") != 0) {
        goto out;
    }
    if (open_impl("/tmp/vfs-mntns-target/file", O_RDONLY, 0) != -1 || errno != ENOENT) {
        errno = ENODATA;
        goto out;
    }
    {
        long move_ret = syscall_dispatch_impl(__NR_move_mount, tree_fd, (long)(uintptr_t)"",
                                              AT_FDCWD, (long)(uintptr_t)"/tmp/vfs-mntns-peer-a",
                                              MOVE_MOUNT_F_EMPTY_PATH, 0);
        if (move_ret != 0) {
            errno = move_ret < 0 ? (int)-move_ret : EPROTO;
            goto out;
        }
    }
    if (vfs_contract_read_file_exact("/tmp/vfs-mntns-peer-a/file", "detached-tree") != 0) {
        goto out;
    }

    ret = 0;

out:
    {
        int saved_errno = errno;
        if (tree_fd >= 0) {
            close_impl(tree_fd);
        }
        vfs_contract_cleanup_mount_namespace_paths();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_open_tree_clone_nested_mount_topology_attaches_recursively(void) {
    int tree_fd = -1;
    int ret = -1;

    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source/child", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-child-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-a", 0700)) != 0) {
        goto out;
    }
    if (vfs_contract_write_file("/tmp/vfs-mntns-parent-source/file", "parent") != 0 ||
        vfs_contract_write_file("/tmp/vfs-mntns-child-source/file", "child") != 0) {
        goto out;
    }
    if (mount("/tmp/vfs-mntns-child-source", "/tmp/vfs-mntns-parent-source/child", NULL, MS_BIND, NULL) != 0 ||
        mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-target", NULL, MS_BIND | MS_REC, NULL) != 0) {
        goto out;
    }

    tree_fd = (int)syscall_dispatch_impl(__NR_open_tree, AT_FDCWD,
                                         (long)(uintptr_t)"/tmp/vfs-mntns-target",
                                         OPEN_TREE_CLONE, 0, 0, 0);
    if (tree_fd < 0) {
        errno = (int)-tree_fd;
        goto out;
    }
    {
        long move_ret = syscall_dispatch_impl(__NR_move_mount, tree_fd, (long)(uintptr_t)"",
                                              AT_FDCWD, (long)(uintptr_t)"/tmp/vfs-mntns-peer-a",
                                              MOVE_MOUNT_F_EMPTY_PATH, 0);
        if (move_ret != 0) {
            errno = move_ret < 0 ? (int)-move_ret : EPROTO;
            goto out;
        }
    }
    if (vfs_contract_read_file_exact("/tmp/vfs-mntns-peer-a/file", "parent") != 0 ||
        vfs_contract_read_file_exact("/tmp/vfs-mntns-peer-a/child/file", "child") != 0 ||
        vfs_contract_read_file_exact("/tmp/vfs-mntns-target/child/file", "child") != 0) {
        goto out;
    }

    ret = 0;

out:
    {
        int saved_errno = errno;
        if (tree_fd >= 0) {
            close_impl(tree_fd);
        }
        vfs_contract_cleanup_mount_namespace_paths();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_shared_mount_move_propagates_to_peer(void) {
    int ret = -1;

    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source/child", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-child-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-a", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-b", 0700)) != 0) {
        goto out;
    }
    if (vfs_contract_write_file("/tmp/vfs-mntns-child-source/file", "moved-child") != 0) {
        goto out;
    }
    if (mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-a", NULL, MS_BIND | MS_SHARED, NULL) != 0 ||
        mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-b", NULL, MS_BIND | MS_SHARED, NULL) != 0 ||
        mount("/tmp/vfs-mntns-child-source", "/tmp/vfs-mntns-peer-a/child", NULL, MS_BIND | MS_SHARED, NULL) != 0) {
        goto out;
    }
    if (vfs_contract_read_file_exact("/tmp/vfs-mntns-peer-b/child/file", "moved-child") != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-a/moved", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-b/moved", 0700)) != 0) {
        goto out;
    }
    {
        long move_ret = syscall_dispatch_impl(__NR_move_mount, AT_FDCWD,
                                              (long)(uintptr_t)"/tmp/vfs-mntns-peer-a/child",
                                              AT_FDCWD,
                                              (long)(uintptr_t)"/tmp/vfs-mntns-peer-a/moved",
                                              0, 0);
        if (move_ret != 0) {
            errno = move_ret < 0 ? (int)-move_ret : EPROTO;
            goto out;
        }
    }
    if (vfs_contract_read_file_exact("/tmp/vfs-mntns-peer-a/moved/file", "moved-child") != 0 ||
        vfs_contract_read_file_exact("/tmp/vfs-mntns-peer-b/moved/file", "moved-child") != 0) {
        goto out;
    }
    if (open_impl("/tmp/vfs-mntns-peer-b/child/file", O_RDONLY, 0) != -1 || errno != ENOENT) {
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

int vfs_contract_clone_newns_move_propagates_to_rebased_slave_receiver(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
    int pid;
    int ret = -1;

    if (!parent || !parent->fs) {
        errno = ESRCH;
        return -1;
    }

    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source/child", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-child-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-a", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-b", 0700)) != 0 ||
        vfs_contract_write_file("/tmp/vfs-mntns-child-source/file", "clone-moved-slave") != 0) {
        goto out;
    }
    if (mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-a", NULL, MS_BIND | MS_SHARED, NULL) != 0 ||
        mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-b", NULL, MS_BIND | MS_SHARED, NULL) != 0 ||
        mount(NULL, "/tmp/vfs-mntns-peer-b", NULL, MS_BIND | MS_REMOUNT | MS_SLAVE, NULL) != 0) {
        goto out;
    }

    pid = clone_impl(CLONE_NEWNS);
    if (pid < 0) {
        goto out;
    }
    child = task_lookup(pid);
    if (!child || !child->fs) {
        errno = ESRCH;
        goto out;
    }

    set_current(child);
    if (mount("/tmp/vfs-mntns-child-source", "/tmp/vfs-mntns-peer-a/child", NULL, MS_BIND | MS_SHARED, NULL) != 0 ||
        vfs_contract_read_file_exact("/tmp/vfs-mntns-peer-b/child/file", "clone-moved-slave") != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-a/moved", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-b/moved", 0700)) != 0) {
        set_current(parent);
        goto out;
    }
    {
        long move_ret = syscall_dispatch_impl(__NR_move_mount, AT_FDCWD,
                                              (long)(uintptr_t)"/tmp/vfs-mntns-peer-a/child",
                                              AT_FDCWD,
                                              (long)(uintptr_t)"/tmp/vfs-mntns-peer-a/moved",
                                              0, 0);
        if (move_ret != 0) {
            errno = move_ret < 0 ? (int)-move_ret : EPROTO;
            set_current(parent);
            goto out;
        }
    }
    if (vfs_contract_read_file_exact("/tmp/vfs-mntns-peer-a/moved/file", "clone-moved-slave") != 0 ||
        vfs_contract_read_file_exact("/tmp/vfs-mntns-peer-b/moved/file", "clone-moved-slave") != 0) {
        set_current(parent);
        goto out;
    }
    {
        int fd = open_impl("/tmp/vfs-mntns-peer-b/child/file", O_RDONLY, 0);
        if (fd != -1 || errno != ENOENT) {
            if (fd >= 0) {
                close_impl(fd);
            }
            set_current(parent);
            errno = ENODATA;
            goto out;
        }
    }

    set_current(parent);
    {
        int fd = open_impl("/tmp/vfs-mntns-peer-b/moved/file", O_RDONLY, 0);
        if (fd != -1 || errno != ENOENT) {
            if (fd >= 0) {
                close_impl(fd);
            }
            errno = ENOMSG;
            goto out;
        }
    }

    ret = 0;

out:
    set_current(parent);
    if (child) {
        task_unlink_child_impl(parent, child);
        free_task(child);
    }
    {
        int saved_errno = errno;
        vfs_contract_cleanup_mount_namespace_paths();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_recursive_umount_propagates_nested_shared_subtree(void) {
    int ret = -1;

    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source/child", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-child-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-child-source/grand", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-grandchild-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-a", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-b", 0700)) != 0 ||
        vfs_contract_write_file("/tmp/vfs-mntns-grandchild-source/file", "grand") != 0) {
        goto out;
    }
    if (mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-a", NULL, MS_BIND | MS_SHARED, NULL) != 0 ||
        mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-b", NULL, MS_BIND | MS_SHARED, NULL) != 0 ||
        mount("/tmp/vfs-mntns-child-source", "/tmp/vfs-mntns-peer-a/child", NULL, MS_BIND | MS_SHARED, NULL) != 0 ||
        mount("/tmp/vfs-mntns-grandchild-source", "/tmp/vfs-mntns-peer-a/child/grand", NULL, MS_BIND | MS_SHARED, NULL) != 0) {
        goto out;
    }
    if (vfs_contract_read_file_exact("/tmp/vfs-mntns-peer-b/child/grand/file", "grand") != 0) {
        goto out;
    }
    if (umount2("/tmp/vfs-mntns-peer-a/child", 0) != 0) {
        goto out;
    }
    if (open_impl("/tmp/vfs-mntns-peer-a/child/grand/file", O_RDONLY, 0) != -1 || errno != ENOENT ||
        open_impl("/tmp/vfs-mntns-peer-b/child/grand/file", O_RDONLY, 0) != -1 || errno != ENOENT) {
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

int vfs_contract_mount_ids_stable_across_move_unmount_and_namespace_clone(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
    char content[8192];
    int original_id = 0;
    int moved_id = 0;
    int after_unmount_id = 0;
    int child_id = 0;
    int parent_id = 0;
    int ignored_parent_id = 0;
    int ret = -1;

    if (!parent || !parent->fs) {
        errno = ESRCH;
        return -1;
    }

    vfs_contract_cleanup_mount_namespace_paths();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-child-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-a", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-b", 0700)) != 0 ||
        vfs_contract_write_file("/tmp/vfs-mntns-parent-source/file", "stable") != 0 ||
        vfs_contract_write_file("/tmp/vfs-mntns-child-source/file", "sibling") != 0) {
        goto out;
    }
    if (mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0 ||
        mount("/tmp/vfs-mntns-child-source", "/tmp/vfs-mntns-peer-b", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }
    if (vfs_contract_read_proc_file("/proc/self/mountinfo", content, sizeof(content)) != 0 ||
        vfs_contract_mountinfo_ids_for_target(content, "/tmp/vfs-mntns-target",
                                              &original_id, &ignored_parent_id) != 0) {
        goto out;
    }
    if (mount("/tmp/vfs-mntns-target", "/tmp/vfs-mntns-peer-a", NULL, MS_MOVE, NULL) != 0) {
        goto out;
    }
    if (vfs_contract_read_proc_file("/proc/self/mountinfo", content, sizeof(content)) != 0 ||
        vfs_contract_mountinfo_ids_for_target(content, "/tmp/vfs-mntns-peer-a",
                                              &moved_id, &parent_id) != 0) {
        goto out;
    }
    if (moved_id != original_id || parent_id != 1) {
        errno = ENODATA;
        goto out;
    }
    if (umount("/tmp/vfs-mntns-peer-b") != 0) {
        goto out;
    }
    if (vfs_contract_read_proc_file("/proc/self/mountinfo", content, sizeof(content)) != 0 ||
        vfs_contract_mountinfo_ids_for_target(content, "/tmp/vfs-mntns-peer-a",
                                              &after_unmount_id, &ignored_parent_id) != 0) {
        goto out;
    }
    if (after_unmount_id != original_id) {
        errno = ENOMSG;
        goto out;
    }

    child = task_create_child_impl(parent);
    if (!child || fs_unshare_mount_namespace(child->fs) != 0) {
        goto out;
    }
    set_current(child);
    if (vfs_contract_read_proc_file("/proc/self/mountinfo", content, sizeof(content)) != 0 ||
        vfs_contract_mountinfo_ids_for_target(content, "/tmp/vfs-mntns-peer-a",
                                              &child_id, &ignored_parent_id) != 0) {
        set_current(parent);
        goto out;
    }
    set_current(parent);
    if (child_id != original_id) {
        errno = ECHILD;
        goto out;
    }

    ret = 0;

out:
    if (child) {
        set_current(parent);
        task_unlink_child_impl(parent, child);
        free_task(child);
    } else {
        set_current(parent);
    }
    {
        int saved_errno = errno;
        vfs_contract_cleanup_mount_namespace_paths();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_mount_namespace_teardown_accounts_mounts_and_detached_refs(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
    int fd = -1;
    unsigned int initial_active;
    unsigned int mounted_active;
    int child_pid;
    int ret = -1;

    if (!parent || !parent->fs) {
        errno = ESRCH;
        return -1;
    }

    vfs_contract_cleanup_mount_namespace_paths();
    vfs_reap_detached_mount_refs();
    initial_active = fs_mount_namespace_active_mounts(parent->fs);

    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-child-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-b", 0700)) != 0 ||
        vfs_contract_write_file("/tmp/vfs-mntns-parent-source/file", "pinned") != 0 ||
        vfs_contract_write_file("/tmp/vfs-mntns-child-source/file", "sibling") != 0 ||
        mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0 ||
        mount("/tmp/vfs-mntns-child-source", "/tmp/vfs-mntns-peer-b", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }

    mounted_active = fs_mount_namespace_active_mounts(parent->fs);
    if (mounted_active != initial_active + 2) {
        errno = ENODATA;
        goto out;
    }

    child_pid = clone_impl(CLONE_NEWNS);
    if (child_pid < 0) {
        goto out;
    }
    child = task_lookup(child_pid);
    if (!child || !child->fs) {
        errno = ESRCH;
        goto out;
    }
    if (fs_mount_namespace_refs(child->fs) != 1 ||
        fs_mount_namespace_active_mounts(child->fs) != mounted_active) {
        errno = ENOMSG;
        goto out;
    }

    fd = open_impl("/tmp/vfs-mntns-target/file", O_RDONLY, 0);
    if (fd < 0) {
        goto out;
    }
    if (vfs_umount_lazy("/tmp/vfs-mntns-target") != 0 ||
        fs_mount_namespace_active_mounts(parent->fs) != initial_active + 1 ||
        vfs_detached_mount_ref_count() == 0) {
        errno = EBUSY;
        goto out;
    }
    if (vfs_reap_detached_mount_refs() != 0 || vfs_detached_mount_ref_count() == 0) {
        errno = ENOTRECOVERABLE;
        goto out;
    }
    vfs_contract_release_lookup_child(parent, child);
    child = NULL;
    if (fs_mount_namespace_active_mounts(parent->fs) != initial_active + 1) {
        errno = ESTALE;
        goto out;
    }
    if (close_impl(fd) != 0) {
        fd = -1;
        goto out;
    }
    fd = -1;
    if (vfs_reap_detached_mount_refs() != 1 ||
        vfs_detached_mount_ref_count() != 0) {
        errno = EBUSY;
        goto out;
    }

    ret = 0;

out:
    {
        int saved_errno = errno;
        if (fd >= 0) {
            close_impl(fd);
        }
        if (child) {
            vfs_contract_release_lookup_child(parent, child);
        }
        vfs_contract_cleanup_mount_namespace_paths();
        vfs_reap_detached_mount_refs();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_mount_namespace_drop_reclaims_child_detached_refs(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
    int child_pid;
    int fd = -1;
    int ret = -1;

    if (!parent || !parent->fs) {
        errno = ESRCH;
        return -1;
    }

    vfs_contract_cleanup_mount_namespace_paths();
    vfs_reap_detached_mount_refs();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0 ||
        vfs_contract_write_file("/tmp/vfs-mntns-parent-source/file", "child") != 0 ||
        mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }

    child_pid = clone_impl(CLONE_NEWNS);
    if (child_pid < 0) {
        goto out;
    }
    child = task_lookup(child_pid);
    if (!child || !child->fs) {
        errno = ESRCH;
        goto out;
    }

    set_current(child);
    fd = open_impl("/tmp/vfs-mntns-target/file", O_RDONLY, 0);
    if (fd < 0) {
        goto out;
    }
    if (umount2("/tmp/vfs-mntns-target", MNT_DETACH) != 0 ||
        vfs_detached_mount_ref_count() == 0) {
        errno = EBUSY;
        goto out;
    }
    if (vfs_reap_detached_mount_refs() != 0 || vfs_detached_mount_ref_count() == 0) {
        errno = ENOTRECOVERABLE;
        goto out;
    }

    set_current(parent);
    vfs_contract_release_lookup_child(parent, child);
    child = NULL;
    fd = -1;

    if (vfs_detached_mount_ref_count() != 0) {
        errno = EBUSY;
        goto out;
    }

    ret = 0;

out:
    {
        int saved_errno = errno;
        if (fd >= 0) {
            if (child) {
                set_current(child);
            }
            close_impl(fd);
            fd = -1;
        }
        set_current(parent);
        if (child) {
            vfs_contract_release_lookup_child(parent, child);
        }
        vfs_contract_cleanup_mount_namespace_paths();
        vfs_reap_detached_mount_refs();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_lazy_umount_ref_survives_descendant_task_tree(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
    struct task_struct *grandchild = NULL;
    int fd = -1;
    int ret = -1;

    if (!parent || !parent->fs) {
        errno = ESRCH;
        return -1;
    }

    vfs_contract_cleanup_mount_namespace_paths();
    vfs_reap_detached_mount_refs();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0 ||
        vfs_contract_write_file("/tmp/vfs-mntns-parent-source/file", "tree") != 0 ||
        mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }

    child = task_create_child_impl(parent);
    if (!child) {
        goto out;
    }
    grandchild = task_create_child_impl(child);
    if (!grandchild) {
        goto out;
    }

    set_current(grandchild);
    fd = open_impl("/tmp/vfs-mntns-target/file", O_RDONLY, 0);
    set_current(parent);
    if (fd < 0) {
        goto out;
    }

    if (vfs_umount_lazy("/tmp/vfs-mntns-target") != 0 ||
        vfs_detached_mount_ref_count() == 0) {
        errno = EBUSY;
        goto out;
    }
    if (vfs_reap_detached_mount_refs() != 0 || vfs_detached_mount_ref_count() == 0) {
        errno = ENOTRECOVERABLE;
        goto out;
    }

    set_current(grandchild);
    if (close_impl(fd) != 0) {
        fd = -1;
        set_current(parent);
        goto out;
    }
    fd = -1;
    set_current(parent);

    task_unlink_child_impl(child, grandchild);
    free_task(grandchild);
    grandchild = NULL;
    task_unlink_child_impl(parent, child);
    free_task(child);
    child = NULL;

    if (vfs_reap_detached_mount_refs() != 1 || vfs_detached_mount_ref_count() != 0) {
        errno = EBUSY;
        goto out;
    }

    ret = 0;

out:
    {
        int saved_errno = errno;
        if (fd >= 0) {
            set_current(grandchild ? grandchild : parent);
            close_impl(fd);
            fd = -1;
        }
        set_current(parent);
        if (grandchild) {
            task_unlink_child_impl(child ? child : parent, grandchild);
            free_task(grandchild);
        }
        if (child) {
            task_unlink_child_impl(parent, child);
            free_task(child);
        }
        vfs_contract_cleanup_mount_namespace_paths();
        vfs_reap_detached_mount_refs();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_lazy_umount_ref_survives_child_root_and_pwd_pins(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
    int ret = -1;

    if (!parent || !parent->fs) {
        errno = ESRCH;
        return -1;
    }

    vfs_contract_cleanup_mount_namespace_paths();
    vfs_reap_detached_mount_refs();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0 ||
        vfs_contract_write_file("/tmp/vfs-mntns-parent-source/file", "rootpin") != 0 ||
        mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }

    child = task_create_child_impl(parent);
    if (!child ||
        fs_set_root(child->fs, "/tmp/vfs-mntns-target") != 0 ||
        fs_set_pwd(child->fs, "/tmp/vfs-mntns-target") != 0) {
        goto out;
    }

    if (vfs_umount_lazy("/tmp/vfs-mntns-target") != 0 ||
        vfs_detached_mount_ref_count() == 0) {
        errno = EBUSY;
        goto out;
    }
    if (vfs_reap_detached_mount_refs() != 0 || vfs_detached_mount_ref_count() == 0) {
        errno = ENOTRECOVERABLE;
        goto out;
    }

    task_unlink_child_impl(parent, child);
    free_task(child);
    child = NULL;

    if (vfs_reap_detached_mount_refs() != 1 || vfs_detached_mount_ref_count() != 0) {
        errno = EBUSY;
        goto out;
    }
    ret = 0;

out:
    {
        int saved_errno = errno;
        set_current(parent);
        if (child) {
            task_unlink_child_impl(parent, child);
            free_task(child);
        }
        vfs_contract_cleanup_mount_namespace_paths();
        vfs_reap_detached_mount_refs();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_child_mount_namespace_detach_survives_child_root_and_pwd_pins(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
    int child_pid;
    int ret = -1;

    if (!parent || !parent->fs) {
        errno = ESRCH;
        return -1;
    }

    vfs_contract_cleanup_mount_namespace_paths();
    vfs_reap_detached_mount_refs();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0 ||
        vfs_contract_write_file("/tmp/vfs-mntns-parent-source/file", "child-root-pin") != 0 ||
        mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }

    child_pid = clone_impl(CLONE_NEWNS);
    if (child_pid < 0) {
        goto out;
    }
    child = task_lookup(child_pid);
    if (!child || !child->fs) {
        errno = ESRCH;
        goto out;
    }
    if (fs_set_root(child->fs, "/tmp/vfs-mntns-target") != 0 ||
        fs_set_pwd(child->fs, "/tmp/vfs-mntns-target") != 0) {
        goto out;
    }

    set_current(child);
    if (umount2(".", MNT_DETACH) != 0) {
        goto out;
    }
    if (vfs_detached_mount_ref_count() == 0) {
        errno = EBUSY;
        goto out;
    }
    if (vfs_reap_detached_mount_refs() != 0 || vfs_detached_mount_ref_count() == 0) {
        errno = ENOTRECOVERABLE;
        goto out;
    }

    set_current(parent);
    vfs_contract_release_lookup_child(parent, child);
    child = NULL;
    if (vfs_detached_mount_ref_count() != 0) {
        errno = EBUSY;
        goto out;
    }
    if (vfs_contract_read_file_exact("/tmp/vfs-mntns-target/file", "child-root-pin") != 0) {
        goto out;
    }
    ret = 0;

out:
    {
        int saved_errno = errno;
        set_current(parent);
        if (child) {
            vfs_contract_release_lookup_child(parent, child);
        }
        vfs_contract_cleanup_mount_namespace_paths();
        vfs_reap_detached_mount_refs();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_lazy_detach_propagates_nested_shared_slave_tree(void) {
    int fd = -1;
    int peer_fd = -1;
    int ret = -1;

    vfs_contract_cleanup_mount_namespace_paths();
    vfs_reap_detached_mount_refs();
    if (vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source/child", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-child-source", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-a", 0700)) != 0 ||
        vfs_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-b", 0700)) != 0 ||
        vfs_contract_write_file("/tmp/vfs-mntns-child-source/file", "lazy-prop") != 0) {
        goto out;
    }
    if (mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-a", NULL,
              MS_BIND | MS_SHARED, NULL) != 0 ||
        mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-b", NULL,
              MS_BIND | MS_SHARED, NULL) != 0 ||
        mount(NULL, "/tmp/vfs-mntns-peer-b", NULL, MS_BIND | MS_REMOUNT | MS_SLAVE, NULL) != 0 ||
        mount("/tmp/vfs-mntns-child-source", "/tmp/vfs-mntns-peer-a/child", NULL,
              MS_BIND | MS_SHARED, NULL) != 0) {
        goto out;
    }
    peer_fd = open_impl("/tmp/vfs-mntns-peer-b/child/file", O_RDONLY, 0);
    if (peer_fd < 0) {
        goto out;
    }
    close_impl(peer_fd);
    peer_fd = -1;

    fd = open_impl("/tmp/vfs-mntns-peer-a/child/file", O_RDONLY, 0);
    if (fd < 0) {
        goto out;
    }
    if (umount2("/tmp/vfs-mntns-peer-a/child", MNT_DETACH) != 0 ||
        vfs_detached_mount_ref_count() == 0) {
        errno = EBUSY;
        goto out;
    }
    peer_fd = open_impl("/tmp/vfs-mntns-peer-b/child/file", O_RDONLY, 0);
    if (peer_fd >= 0 || errno != ENOENT) {
        if (peer_fd >= 0) {
            close_impl(peer_fd);
            peer_fd = -1;
        }
        errno = ENODATA;
        goto out;
    }
    if (vfs_reap_detached_mount_refs() != 0 || vfs_detached_mount_ref_count() == 0) {
        errno = ENOMSG;
        goto out;
    }
    if (close_impl(fd) != 0) {
        fd = -1;
        goto out;
    }
    fd = -1;
    if (vfs_reap_detached_mount_refs() != 1 || vfs_detached_mount_ref_count() != 0) {
        errno = ERANGE;
        goto out;
    }
    ret = 0;

out:
    {
        int saved_errno = errno;
        if (peer_fd >= 0) {
            close_impl(peer_fd);
        }
        if (fd >= 0) {
            close_impl(fd);
        }
        vfs_contract_cleanup_mount_namespace_paths();
        vfs_reap_detached_mount_refs();
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
