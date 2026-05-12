/* OrlixKernelTests/MountContract.c
 * Mount and umount2 Linux contract tests that need full linux/fs.h ownership.
 */

#include <uapi/asm/statfs.h>
#include <uapi/asm/unistd.h>
#include <uapi/linux/errno.h>
#include <uapi/linux/fcntl.h>
#include <uapi/linux/mount.h>
#include <uapi/linux/sched.h>
#include <linux/fs.h>
#include <linux/string.h>

#include "kernel/task.h"
#include "private/kernel/task_state.h"

extern int errno;

extern int mount(const char *source, const char *target, const char *filesystemtype,
                 unsigned long mountflags, const void *data);
extern int umount_impl(const char *target);
extern int umount2_impl(const char *target, int flags);
extern int clone_impl(uint64_t flags);
extern int mkdir_impl(const char *pathname, uint32_t mode);
extern int open_impl(const char *pathname, int flags, uint32_t mode);
extern int close_impl(int fd);
extern long read_impl(int fd, void *buf, size_t count);
extern long write_impl(int fd, const void *buf, size_t count);
extern int unlink_impl(const char *pathname);
extern int rmdir_impl(const char *pathname);
extern int unlinkat(int dirfd, const char *pathname, int flags);
extern int symlinkat(const char *target, int newdirfd, const char *linkpath);
extern long syscall_dispatch_impl(long number, long arg0, long arg1, long arg2,
                                  long arg3, long arg4, long arg5);
extern int statfs(const char *path, struct statfs *buf);
extern int vfs_mount_setattr(int dirfd, const char *pathname, unsigned int flags,
                             const struct mount_attr *attr, size_t size);
extern int vfs_reap_detached_mount_refs(void);
extern unsigned int vfs_detached_mount_ref_count(void);
extern int fs_set_root(struct fs_context *fs, const char *new_root);
extern int fs_set_pwd(struct fs_context *fs, const char *new_pwd);

static void mount_contract_release_lookup_child(struct task *parent, struct task *child) {
    if (!child) {
        return;
    }
    task_unlink_child_impl(parent, child);
    task_put(child);
    task_put(child);
}

static int mount_contract_ignore_exists(int result) {
    if (result == 0 || errno == EEXIST) {
        return 0;
    }
    return -1;
}

static int mount_contract_write_file(const char *path, const char *content) {
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

static int mount_contract_read_file_exact(const char *path, const char *expected) {
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

static void mount_contract_cleanup_mount_paths(void) {
    umount_impl("/tmp/vfs-bind-target");
    unlink_impl("/tmp/vfs-bind-source/file");
    unlink_impl("/tmp/vfs-bind-target/file");
    rmdir_impl("/tmp/vfs-bind-source");
    rmdir_impl("/tmp/vfs-bind-target");
}

static void mount_contract_cleanup_mount_namespace_paths(void) {
    umount_impl("/tmp/vfs-mntns-peer-a/child/grand");
    umount_impl("/tmp/vfs-mntns-peer-b/child/grand");
    umount_impl("/tmp/vfs-mntns-peer-c/child/grand");
    umount_impl("/tmp/vfs-mntns-peer-a/moved/grand");
    umount_impl("/tmp/vfs-mntns-peer-b/moved/grand");
    umount_impl("/tmp/vfs-mntns-peer-c/moved/grand");
    umount_impl("/tmp/vfs-mntns-parent-source/grand");
    umount_impl("/tmp/vfs-mntns-parent-source/child/grand");
    umount_impl("/tmp/vfs-mntns-parent-source/child");
    umount_impl("/tmp/vfs-mntns-target/child");
    umount_impl("/tmp/vfs-mntns-peer-a/child");
    umount_impl("/tmp/vfs-mntns-peer-b/child");
    umount_impl("/tmp/vfs-mntns-peer-c/child");
    umount_impl("/tmp/vfs-mntns-peer-a/moved");
    umount_impl("/tmp/vfs-mntns-peer-b/moved");
    umount_impl("/tmp/vfs-mntns-peer-c/moved");
    umount_impl("/tmp/vfs-mntns-peer-a");
    umount_impl("/tmp/vfs-mntns-peer-b");
    umount_impl("/tmp/vfs-mntns-peer-c");
    umount_impl("/tmp/vfs-mntns-target");
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
    rmdir_impl("/tmp/vfs-mntns-parent-source/moved");
    rmdir_impl("/tmp/vfs-mntns-parent-source/grand");
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

int vfs_contract_mount_syscall_bind_mount_and_umount2_work(void) {
    int ret = -1;
    long sret;

    mount_contract_cleanup_mount_paths();
    if (mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-syscall-bind-source", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-syscall-bind-target", 0700)) != 0) {
        goto out;
    }
    if (mount_contract_write_file("/tmp/vfs-syscall-bind-source/file", "source") != 0 ||
        mount_contract_write_file("/tmp/vfs-syscall-bind-target/file", "target") != 0) {
        goto out;
    }

    sret = syscall_dispatch_impl(__NR_mount,
                                 (long)(uintptr_t)"/tmp/vfs-syscall-bind-source",
                                 (long)(uintptr_t)"/tmp/vfs-syscall-bind-target",
                                 0,
                                 (long)MS_BIND,
                                 0,
                                 0);
    if (sret != 0) {
        errno = sret < 0 ? (int)-sret : EPROTO;
        goto out;
    }
    if (mount_contract_read_file_exact("/tmp/vfs-syscall-bind-target/file", "source") != 0) {
        goto out;
    }

    sret = syscall_dispatch_impl(__NR_umount2,
                                 (long)(uintptr_t)"/tmp/vfs-syscall-bind-target",
                                 0,
                                 0, 0, 0, 0);
    if (sret != 0) {
        errno = sret < 0 ? (int)-sret : EPROTO;
        goto out;
    }
    if (mount_contract_read_file_exact("/tmp/vfs-syscall-bind-target/file", "target") != 0) {
        goto out;
    }

    ret = 0;
out:
    mount_contract_cleanup_mount_paths();
    return ret;
}

int vfs_contract_umount2_detach_detaches_busy_mount_from_namespace(void) {
    int fd = -1;
    int ret = -1;

    mount_contract_cleanup_mount_namespace_paths();
    if (mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-source", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-source/dir", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0 ||
        mount_contract_write_file("/tmp/vfs-mntns-source/dir/file", "detach") != 0 ||
        mount("/tmp/vfs-mntns-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }
    fd = open_impl("/tmp/vfs-mntns-target/dir/file", O_RDONLY, 0);
    if (fd < 0) {
        goto out;
    }
    if (umount2_impl("/tmp/vfs-mntns-target", MNT_DETACH) != 0 ||
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
        mount_contract_cleanup_mount_namespace_paths();
        vfs_reap_detached_mount_refs();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_umount2_syscall_detach_detaches_busy_mount_from_namespace(void) {
    int fd = -1;
    int ret = -1;
    long sret;

    mount_contract_cleanup_mount_namespace_paths();
    if (mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-source", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-source/dir", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0 ||
        mount_contract_write_file("/tmp/vfs-mntns-source/dir/file", "detach") != 0 ||
        mount("/tmp/vfs-mntns-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }
    fd = open_impl("/tmp/vfs-mntns-target/dir/file", O_RDONLY, 0);
    if (fd < 0) {
        goto out;
    }

    sret = syscall_dispatch_impl(__NR_umount2, (long)(uintptr_t)"/tmp/vfs-mntns-target",
                                 (long)MNT_DETACH, 0, 0, 0, 0);
    if (sret != 0 ||
        open_impl("/tmp/vfs-mntns-target/dir/file", O_RDONLY, 0) != -1 ||
        errno != ENOENT) {
        errno = sret < 0 ? (int)-sret : ENODATA;
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
        mount_contract_cleanup_mount_namespace_paths();
        vfs_reap_detached_mount_refs();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_umount2_rejects_unused_linux_umount_flag(void) {
    int ret = -1;

    mount_contract_cleanup_mount_namespace_paths();
    if (mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-source", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0 ||
        mount_contract_write_file("/tmp/vfs-mntns-source/file", "unused-flag") != 0 ||
        mount("/tmp/vfs-mntns-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }
    if (umount2_impl("/tmp/vfs-mntns-target", UMOUNT_UNUSED) != -1 || errno != EINVAL) {
        errno = ENODATA;
        goto out;
    }
    if (mount_contract_read_file_exact("/tmp/vfs-mntns-target/file", "unused-flag") != 0) {
        goto out;
    }

    ret = 0;
out:
    {
        int saved_errno = errno;
        mount_contract_cleanup_mount_namespace_paths();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_umount2_force_detaches_busy_mount_and_reaps_after_pin_release(void) {
    int fd = -1;
    int ret = -1;

    mount_contract_cleanup_mount_namespace_paths();
    vfs_reap_detached_mount_refs();
    if (mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-source", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-source/dir", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0 ||
        mount_contract_write_file("/tmp/vfs-mntns-source/dir/file", "force") != 0 ||
        mount("/tmp/vfs-mntns-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }
    fd = open_impl("/tmp/vfs-mntns-target/dir/file", O_RDONLY, 0);
    if (fd < 0) {
        goto out;
    }
    if (umount_impl("/tmp/vfs-mntns-target") != -1 || errno != EBUSY) {
        errno = ENODATA;
        goto out;
    }
    if (umount2_impl("/tmp/vfs-mntns-target", MNT_FORCE) != 0 ||
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
        mount_contract_cleanup_mount_namespace_paths();
        vfs_reap_detached_mount_refs();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_force_umount_detached_refs_are_mount_namespace_scoped(void) {
    struct task *parent = task_current();
    struct task *child = NULL;
    int fd = -1;
    int pid;
    int ret = -1;

    if (!parent || !parent->fs) {
        errno = ESRCH;
        return -1;
    }

    mount_contract_cleanup_mount_namespace_paths();
    vfs_reap_detached_mount_refs();
    if (mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-source", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0 ||
        mount_contract_write_file("/tmp/vfs-mntns-source/file", "ns-detached") != 0 ||
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

    task_set_current(child);
    if (umount2_impl("/tmp/vfs-mntns-target", MNT_FORCE) != 0 ||
        vfs_detached_mount_ref_count() != 0) {
        task_set_current(parent);
        errno = ENODATA;
        goto out;
    }

    task_set_current(parent);
    if (mount_contract_read_file_exact("/tmp/vfs-mntns-target/file", "ns-detached") != 0) {
        goto out;
    }
    if (vfs_reap_detached_mount_refs() != 0 || vfs_detached_mount_ref_count() != 0) {
        errno = ENOMSG;
        goto out;
    }

    ret = 0;
out:
    task_set_current(parent);
    {
        int saved_errno = errno;
        if (fd >= 0) {
            close_impl(fd);
        }
        if (child) {
            mount_contract_release_lookup_child(parent, child);
        }
        mount_contract_cleanup_mount_namespace_paths();
        vfs_reap_detached_mount_refs();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_mount_namespace_drop_reclaims_child_detached_refs(void) {
    struct task *parent = task_current();
    struct task *child = NULL;
    int child_pid;
    int fd = -1;
    int ret = -1;

    if (!parent || !parent->fs) {
        errno = ESRCH;
        return -1;
    }

    mount_contract_cleanup_mount_namespace_paths();
    vfs_reap_detached_mount_refs();
    if (mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0 ||
        mount_contract_write_file("/tmp/vfs-mntns-parent-source/file", "child") != 0 ||
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

    task_set_current(child);
    fd = open_impl("/tmp/vfs-mntns-target/file", O_RDONLY, 0);
    if (fd < 0) {
        goto out;
    }
    if (umount2_impl("/tmp/vfs-mntns-target", MNT_DETACH) != 0 ||
        vfs_detached_mount_ref_count() == 0) {
        errno = EBUSY;
        goto out;
    }
    if (vfs_reap_detached_mount_refs() != 0 || vfs_detached_mount_ref_count() == 0) {
        errno = ENOTRECOVERABLE;
        goto out;
    }

    task_set_current(parent);
    mount_contract_release_lookup_child(parent, child);
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
                task_set_current(child);
            }
            close_impl(fd);
            fd = -1;
        }
        task_set_current(parent);
        if (child) {
            mount_contract_release_lookup_child(parent, child);
        }
        mount_contract_cleanup_mount_namespace_paths();
        vfs_reap_detached_mount_refs();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_force_umount_propagates_shared_slave_subtree_teardown(void) {
    int fd = -1;
    int ret = -1;

    mount_contract_cleanup_mount_namespace_paths();
    vfs_reap_detached_mount_refs();
    if (mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source/child", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-child-source", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-a", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-b", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-c", 0700)) != 0 ||
        mount_contract_write_file("/tmp/vfs-mntns-child-source/file", "force-propagated") != 0) {
        goto out;
    }
    if (mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-a", NULL, MS_BIND | MS_SHARED, NULL) != 0 ||
        mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-b", NULL, MS_BIND | MS_SHARED, NULL) != 0 ||
        mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-c", NULL, MS_BIND | MS_SLAVE, NULL) != 0 ||
        mount("/tmp/vfs-mntns-child-source", "/tmp/vfs-mntns-peer-a/child", NULL, MS_BIND | MS_SHARED, NULL) != 0) {
        goto out;
    }
    if (mount_contract_read_file_exact("/tmp/vfs-mntns-peer-b/child/file", "force-propagated") != 0 ||
        mount_contract_read_file_exact("/tmp/vfs-mntns-peer-c/child/file", "force-propagated") != 0) {
        goto out;
    }
    fd = open_impl("/tmp/vfs-mntns-peer-a/child/file", O_RDONLY, 0);
    if (fd < 0) {
        goto out;
    }
    if (umount2_impl("/tmp/vfs-mntns-peer-a/child", MNT_FORCE) != 0 ||
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
        mount_contract_cleanup_mount_namespace_paths();
        vfs_reap_detached_mount_refs();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_umount2_expire_requires_mark_then_unmount(void) {
    int ret = -1;

    mount_contract_cleanup_mount_namespace_paths();
    if (mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-source", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0 ||
        mount_contract_write_file("/tmp/vfs-mntns-source/file", "expire2") != 0 ||
        mount("/tmp/vfs-mntns-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }
    if (umount2_impl("/tmp/vfs-mntns-target", MNT_EXPIRE) != -1 || errno != EAGAIN) {
        errno = ENODATA;
        goto out;
    }
    if (mount_contract_read_file_exact("/tmp/vfs-mntns-target/file", "expire2") != 0) {
        goto out;
    }
    if (umount2_impl("/tmp/vfs-mntns-target", MNT_EXPIRE) != 0) {
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
        mount_contract_cleanup_mount_namespace_paths();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_umount2_rejects_expire_with_detach(void) {
    int ret = -1;

    mount_contract_cleanup_mount_namespace_paths();
    if (mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-source", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0 ||
        mount_contract_write_file("/tmp/vfs-mntns-source/file", "combo") != 0 ||
        mount("/tmp/vfs-mntns-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }
    if (umount2_impl("/tmp/vfs-mntns-target", MNT_EXPIRE | MNT_DETACH) != -1 || errno != EINVAL) {
        errno = ENODATA;
        goto out;
    }
    if (mount_contract_read_file_exact("/tmp/vfs-mntns-target/file", "combo") != 0) {
        goto out;
    }
    ret = 0;
out:
    {
        int saved_errno = errno;
        mount_contract_cleanup_mount_namespace_paths();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_umount2_nofollow_rejects_symlink_target(void) {
    int ret = -1;

    mount_contract_cleanup_mount_namespace_paths();
    unlinkat(AT_FDCWD, "/tmp/vfs-mntns-link", 0);
    if (mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-source", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0 ||
        mount_contract_write_file("/tmp/vfs-mntns-source/file", "nofollow") != 0 ||
        mount("/tmp/vfs-mntns-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0 ||
        symlinkat("/tmp/vfs-mntns-target", AT_FDCWD, "/tmp/vfs-mntns-link") != 0) {
        goto out;
    }
    if (umount2_impl("/tmp/vfs-mntns-link", UMOUNT_NOFOLLOW) != -1 || errno != EINVAL) {
        errno = ENODATA;
        goto out;
    }
    if (mount_contract_read_file_exact("/tmp/vfs-mntns-target/file", "nofollow") != 0) {
        goto out;
    }
    ret = 0;
out:
    {
        int saved_errno = errno;
        unlinkat(AT_FDCWD, "/tmp/vfs-mntns-link", 0);
        mount_contract_cleanup_mount_namespace_paths();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_child_mount_namespace_detach_survives_child_root_and_pwd_pins(void) {
    struct task *parent = task_current();
    struct task *child = NULL;
    int child_pid;
    int ret = -1;

    if (!parent || !parent->fs) {
        errno = ESRCH;
        return -1;
    }

    mount_contract_cleanup_mount_namespace_paths();
    vfs_reap_detached_mount_refs();
    if (mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0 ||
        mount_contract_write_file("/tmp/vfs-mntns-parent-source/file", "child-root-pin") != 0 ||
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

    task_set_current(child);
    if (umount2_impl(".", MNT_DETACH) != 0) {
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

    task_set_current(parent);
    mount_contract_release_lookup_child(parent, child);
    child = NULL;
    if (vfs_detached_mount_ref_count() != 0) {
        errno = EBUSY;
        goto out;
    }
    if (mount_contract_read_file_exact("/tmp/vfs-mntns-target/file", "child-root-pin") != 0) {
        goto out;
    }
    ret = 0;
out:
    {
        int saved_errno = errno;
        task_set_current(parent);
        if (child) {
            mount_contract_release_lookup_child(parent, child);
        }
        mount_contract_cleanup_mount_namespace_paths();
        vfs_reap_detached_mount_refs();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_lazy_detach_propagates_nested_shared_slave_tree(void) {
    int fd = -1;
    int peer_fd = -1;
    int ret = -1;

    mount_contract_cleanup_mount_namespace_paths();
    vfs_reap_detached_mount_refs();
    if (mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source/child", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-child-source", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-a", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-b", 0700)) != 0 ||
        mount_contract_write_file("/tmp/vfs-mntns-child-source/file", "lazy-prop") != 0) {
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
    if (umount2_impl("/tmp/vfs-mntns-peer-a/child", MNT_DETACH) != 0 ||
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
        if (fd >= 0) {
            close_impl(fd);
        }
        if (peer_fd >= 0) {
            close_impl(peer_fd);
        }
        mount_contract_cleanup_mount_namespace_paths();
        vfs_reap_detached_mount_refs();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_statfs_reports_mount_attribute_flags(void) {
    static const char source[] = "/tmp/vfs-statfs-attrs-source";
    static const char target[] = "/tmp/vfs-statfs-attrs-target";
    struct mount_attr attr;
    struct statfs st;
    int ret = -1;

    (void)umount2_impl(target, MNT_DETACH);
    (void)rmdir_impl(target);
    (void)rmdir_impl(source);

    if (mount_contract_ignore_exists(mkdir_impl(source, 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl(target, 0700)) != 0) {
        goto out;
    }

    if (mount(source, target, "bind", MS_BIND | MS_RDONLY | MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) != 0) {
        goto out;
    }

    memset(&st, 0, sizeof(st));
    if (statfs(target, &st) != 0 ||
        (st.f_flags & (MS_REMOUNT | MS_RDONLY | MS_NOSUID | MS_NODEV | MS_NOEXEC)) !=
            (MS_REMOUNT | MS_RDONLY | MS_NOSUID | MS_NODEV | MS_NOEXEC)) {
        errno = ENODATA;
        goto out;
    }

    memset(&attr, 0, sizeof(attr));
    attr.attr_clr = MOUNT_ATTR_NODEV | MOUNT_ATTR_NOEXEC;
    if (vfs_mount_setattr(AT_FDCWD, target, 0, &attr, MOUNT_ATTR_SIZE_VER0) != 0) {
        goto out;
    }

    memset(&st, 0, sizeof(st));
    if (statfs(target, &st) != 0 ||
        (st.f_flags & (MS_REMOUNT | MS_RDONLY | MS_NOSUID)) != (MS_REMOUNT | MS_RDONLY | MS_NOSUID) ||
        (st.f_flags & (MS_NODEV | MS_NOEXEC)) != 0) {
        errno = ENOMSG;
        goto out;
    }

    ret = 0;
out:
    (void)umount2_impl(target, MNT_DETACH);
    (void)rmdir_impl(target);
    (void)rmdir_impl(source);
    return ret;
}
