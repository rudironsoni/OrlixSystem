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
extern int vfs_umount_lazy(const char *target);
extern const char *vfs_persistent_backing_root(void);
extern int fs_set_root(struct fs_context *fs, const char *new_root);
extern int fs_set_pwd(struct fs_context *fs, const char *new_pwd);
extern int fs_unshare_mount_namespace(struct fs_context *fs);
extern unsigned int fs_mount_namespace_refs(struct fs_context *fs);
extern uint64_t fs_mount_namespace_id(struct fs_context *fs);
extern unsigned int fs_mount_namespace_active_mounts(struct fs_context *fs);

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

static int mount_contract_read_proc_file(const char *path, char *buf, size_t buf_len) {
    int fd;
    long nread;

    if (buf_len == 0) {
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

static int mount_contract_content_contains(const char *content, const char *needle) {
    size_t content_len = strlen(content);
    size_t needle_len = strlen(needle);
    size_t i;

    if (needle_len == 0) {
        return 1;
    }
    if (needle_len > content_len) {
        return 0;
    }
    for (i = 0; i + needle_len <= content_len; i++) {
        if (memcmp(content + i, needle, needle_len) == 0) {
            return 1;
        }
    }
    return 0;
}

static int mount_contract_mountinfo_ids_for_target(const char *content, const char *target,
                                                   int *mount_id_out, int *parent_id_out) {
    size_t target_len = strlen(target);
    const char *cursor = content;

    while (*cursor != '\0') {
        const char *line_end = cursor;
        int mount_id;
        int parent_id;

        while (*line_end != '\0' && *line_end != '\n') {
            line_end++;
        }

        if (sscanf(cursor, "%d %d ", &mount_id, &parent_id) == 2) {
            const char *field = cursor;
            int spaces = 0;

            while (field < line_end) {
                if (*field == ' ') {
                    spaces++;
                    if (spaces == 4) {
                        const char *mount_point = field + 1;
                        const char *mount_point_end = mount_point;
                        while (mount_point_end < line_end && *mount_point_end != ' ') {
                            mount_point_end++;
                        }
                        if ((size_t)(mount_point_end - mount_point) == target_len &&
                            memcmp(mount_point, target, target_len) == 0) {
                            *mount_id_out = mount_id;
                            *parent_id_out = parent_id;
                            return 0;
                        }
                        break;
                    }
                }
                field++;
            }
        }

        cursor = (*line_end == '\n') ? line_end + 1 : line_end;
    }

    errno = ENOENT;
    return -1;
}

static const char *mount_contract_statmount_string(const struct statmount *st, __u32 off) {
    if (off == 0 || off >= st->size) {
        return "";
    }
    return (const char *)st + off;
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

int vfs_contract_mount_namespace_shared_across_task_dup(void) {
    struct task *parent = task_current();
    struct task *child = NULL;
    int ret = -1;

    if (!parent || !parent->fs) {
        errno = ESRCH;
        return -1;
    }

    mount_contract_cleanup_mount_namespace_paths();
    if (mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0) {
        goto out;
    }
    if (mount_contract_write_file("/tmp/vfs-mntns-parent-source/file", "parent") != 0 ||
        mount_contract_write_file("/tmp/vfs-mntns-target/file", "target") != 0) {
        goto out;
    }

    child = task_create_child_impl(parent);
    if (!child) {
        goto out;
    }

    if (mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }

    task_set_current(child);
    if (mount_contract_read_file_exact("/tmp/vfs-mntns-target/file", "parent") != 0) {
        goto out;
    }

    ret = 0;

out:
    task_set_current(parent);
    if (child) {
        task_unlink_child_impl(parent, child);
        task_put(child);
    }
    mount_contract_cleanup_mount_namespace_paths();
    return ret;
}

int vfs_contract_mount_namespace_unshare_isolates_child_mounts(void) {
    struct task *parent = task_current();
    struct task *child = NULL;
    int ret = -1;

    if (!parent || !parent->fs) {
        errno = ESRCH;
        return -1;
    }

    mount_contract_cleanup_mount_namespace_paths();
    if (mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-child-source", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0) {
        goto out;
    }
    if (mount_contract_write_file("/tmp/vfs-mntns-parent-source/file", "parent") != 0 ||
        mount_contract_write_file("/tmp/vfs-mntns-child-source/file", "child") != 0 ||
        mount_contract_write_file("/tmp/vfs-mntns-target/file", "target") != 0) {
        goto out;
    }

    child = task_create_child_impl(parent);
    if (!child) {
        goto out;
    }
    if (fs_unshare_mount_namespace(child->fs) != 0) {
        goto out;
    }

    task_set_current(child);
    if (mount("/tmp/vfs-mntns-child-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }
    if (mount_contract_read_file_exact("/tmp/vfs-mntns-target/file", "child") != 0) {
        goto out;
    }

    task_set_current(parent);
    if (mount_contract_read_file_exact("/tmp/vfs-mntns-target/file", "target") != 0) {
        goto out;
    }

    ret = 0;

out:
    if (child) {
        task_set_current(child);
        umount_impl("/tmp/vfs-mntns-target");
        task_set_current(parent);
        task_unlink_child_impl(parent, child);
        task_put(child);
    } else {
        task_set_current(parent);
    }
    mount_contract_cleanup_mount_namespace_paths();
    return ret;
}

int vfs_contract_recursive_umount_propagates_nested_children_from_shared_peer(void) {
    int ret = -1;

    mount_contract_cleanup_mount_namespace_paths();
    if (mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source/child", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-child-source", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-a", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-b", 0700)) != 0) {
        goto out;
    }
    if (mount_contract_write_file("/tmp/vfs-mntns-child-source/file", "recursive-detach") != 0) {
        goto out;
    }
    if (mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-a", NULL, MS_BIND | MS_SHARED, NULL) != 0 ||
        mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-b", NULL, MS_BIND | MS_SHARED, NULL) != 0 ||
        mount("/tmp/vfs-mntns-child-source", "/tmp/vfs-mntns-peer-a/child", NULL, MS_BIND | MS_SHARED, NULL) != 0) {
        goto out;
    }
    if (mount_contract_read_file_exact("/tmp/vfs-mntns-peer-b/child/file", "recursive-detach") != 0) {
        goto out;
    }
    if (umount_impl("/tmp/vfs-mntns-peer-a") != 0) {
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
        mount_contract_cleanup_mount_namespace_paths();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_mount_namespace_refs_track_task_lifecycle(void) {
    struct task *parent = task_current();
    struct task *shared_child = NULL;
    struct task *private_child = NULL;
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
        errno = EBUSY;
        goto out;
    }

    ret = 0;

out:
    if (private_child) {
        mount_contract_release_lookup_child(parent, private_child);
    }
    if (shared_child) {
        mount_contract_release_lookup_child(parent, shared_child);
    }
    return ret;
}

int vfs_contract_lazy_umount_detaches_busy_mount_from_namespace(void) {
    int fd = -1;
    int ret = -1;

    mount_contract_cleanup_mount_namespace_paths();
    vfs_reap_detached_mount_refs();
    if (mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-source", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-source/dir", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0 ||
        mount_contract_write_file("/tmp/vfs-mntns-source/dir/file", "lazy") != 0 ||
        mount("/tmp/vfs-mntns-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }
    fd = open_impl("/tmp/vfs-mntns-target/dir/file", O_RDONLY, 0);
    if (fd < 0) {
        goto out;
    }
    if (vfs_umount_lazy("/tmp/vfs-mntns-target") != 0 ||
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

int vfs_contract_lazy_umount_removes_busy_mount_from_proc_mountinfo(void) {
    char content[8192];
    int fd = -1;
    int ret = -1;

    mount_contract_cleanup_mount_namespace_paths();
    vfs_reap_detached_mount_refs();
    if (mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-source", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0 ||
        mount_contract_write_file("/tmp/vfs-mntns-source/file", "lazy-mountinfo") != 0 ||
        mount("/tmp/vfs-mntns-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }
    fd = open_impl("/tmp/vfs-mntns-target/file", O_RDONLY, 0);
    if (fd < 0) {
        goto out;
    }
    if (mount_contract_read_proc_file("/proc/self/mountinfo", content, sizeof(content)) != 0 ||
        !mount_contract_content_contains(content, " /tmp/vfs-mntns-target ")) {
        errno = ENODATA;
        goto out;
    }
    if (vfs_umount_lazy("/tmp/vfs-mntns-target") != 0) {
        goto out;
    }
    if (mount_contract_read_proc_file("/proc/self/mountinfo", content, sizeof(content)) != 0 ||
        mount_contract_content_contains(content, " /tmp/vfs-mntns-target ")) {
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

int vfs_contract_lazy_umount_reclaims_detached_ref_after_pin_release(void) {
    int fd = -1;
    int ret = -1;

    mount_contract_cleanup_mount_namespace_paths();
    vfs_reap_detached_mount_refs();
    if (mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-source", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-source/dir", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0 ||
        mount_contract_write_file("/tmp/vfs-mntns-source/dir/file", "lazy-reap") != 0 ||
        mount("/tmp/vfs-mntns-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }
    fd = open_impl("/tmp/vfs-mntns-target/dir/file", O_RDONLY, 0);
    if (fd < 0) {
        goto out;
    }
    if (vfs_umount_lazy("/tmp/vfs-mntns-target") != 0 ||
        vfs_detached_mount_ref_count() == 0) {
        errno = ENOMSG;
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

int vfs_contract_recursive_umount_propagates_nested_shared_subtree(void) {
    int ret = -1;

    mount_contract_cleanup_mount_namespace_paths();
    if (mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source/child", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-child-source", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-child-source/grand", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-grandchild-source", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-a", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-b", 0700)) != 0 ||
        mount_contract_write_file("/tmp/vfs-mntns-grandchild-source/file", "grand") != 0) {
        goto out;
    }
    if (mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-a", NULL, MS_BIND | MS_SHARED, NULL) != 0 ||
        mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-b", NULL, MS_BIND | MS_SHARED, NULL) != 0 ||
        mount("/tmp/vfs-mntns-child-source", "/tmp/vfs-mntns-peer-a/child", NULL, MS_BIND | MS_SHARED, NULL) != 0 ||
        mount("/tmp/vfs-mntns-grandchild-source", "/tmp/vfs-mntns-peer-a/child/grand", NULL, MS_BIND | MS_SHARED, NULL) != 0) {
        goto out;
    }
    if (mount_contract_read_file_exact("/tmp/vfs-mntns-peer-b/child/grand/file", "grand") != 0) {
        goto out;
    }
    if (umount2_impl("/tmp/vfs-mntns-peer-a/child", 0) != 0) {
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
        mount_contract_cleanup_mount_namespace_paths();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_recursive_umount_updates_proc_mountinfo_for_propagated_peers(void) {
    char content[8192];
    int ret = -1;

    mount_contract_cleanup_mount_namespace_paths();
    if (mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source/child", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-child-source", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-a", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-b", 0700)) != 0 ||
        mount_contract_write_file("/tmp/vfs-mntns-child-source/file", "mountinfo-recursive") != 0) {
        goto out;
    }
    if (mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-a", NULL, MS_BIND | MS_SHARED, NULL) != 0 ||
        mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-b", NULL, MS_BIND | MS_SHARED, NULL) != 0 ||
        mount("/tmp/vfs-mntns-child-source", "/tmp/vfs-mntns-peer-a/child", NULL, MS_BIND | MS_SHARED, NULL) != 0) {
        goto out;
    }
    if (mount_contract_read_proc_file("/proc/self/mountinfo", content, sizeof(content)) != 0 ||
        !mount_contract_content_contains(content, " /tmp/vfs-mntns-peer-a/child ") ||
        !mount_contract_content_contains(content, " /tmp/vfs-mntns-peer-b/child ")) {
        errno = ENODATA;
        goto out;
    }
    if (umount2_impl("/tmp/vfs-mntns-peer-a", 0) != 0) {
        goto out;
    }
    if (mount_contract_read_proc_file("/proc/self/mountinfo", content, sizeof(content)) != 0 ||
        mount_contract_content_contains(content, " /tmp/vfs-mntns-peer-a/child ") ||
        mount_contract_content_contains(content, " /tmp/vfs-mntns-peer-b/child ")) {
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

int vfs_contract_statmount_rejects_propagated_removed_mount_id(void) {
    struct mnt_id_req req;
    char statmount_storage[sizeof(struct statmount) + 512];
    struct statmount *st = (struct statmount *)statmount_storage;
    uint64_t ids[64];
    char content[8192];
    int child_mount_id = 0;
    int child_parent_id = 0;
    int ret = -1;
    long count;

    mount_contract_cleanup_mount_namespace_paths();
    if (mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source/child", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-child-source", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-a", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-b", 0700)) != 0 ||
        mount_contract_write_file("/tmp/vfs-mntns-child-source/file", "statmount-removed") != 0) {
        goto out;
    }
    if (mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-a", NULL, MS_BIND | MS_SHARED, NULL) != 0 ||
        mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-peer-b", NULL, MS_BIND | MS_SHARED, NULL) != 0 ||
        mount("/tmp/vfs-mntns-child-source", "/tmp/vfs-mntns-peer-a/child", NULL, MS_BIND | MS_SHARED, NULL) != 0) {
        goto out;
    }
    if (mount_contract_read_file_exact("/tmp/vfs-mntns-peer-b/child/file", "statmount-removed") != 0 ||
        mount_contract_read_proc_file("/proc/self/mountinfo", content, sizeof(content)) != 0 ||
        mount_contract_mountinfo_ids_for_target(content, "/tmp/vfs-mntns-peer-b/child",
                                                &child_mount_id, &child_parent_id) != 0) {
        goto out;
    }

    memset(&req, 0, sizeof(req));
    memset(statmount_storage, 0, sizeof(statmount_storage));
    req.size = MNT_ID_REQ_SIZE_VER1;
    req.mnt_id = (uint64_t)child_mount_id;
    req.param = STATMOUNT_MNT_BASIC | STATMOUNT_MNT_POINT;
    if (syscall_dispatch_impl(__NR_statmount, (long)(uintptr_t)&req,
                              (long)(uintptr_t)st, sizeof(statmount_storage), 0, 0, 0) != 0 ||
        st->mnt_point == 0 ||
        strcmp(mount_contract_statmount_string(st, st->mnt_point),
               "/tmp/vfs-mntns-peer-b/child") != 0) {
        errno = ENODATA;
        goto out;
    }

    if (umount2_impl("/tmp/vfs-mntns-peer-a", 0) != 0) {
        goto out;
    }

    memset(&req, 0, sizeof(req));
    memset(statmount_storage, 0, sizeof(statmount_storage));
    req.size = MNT_ID_REQ_SIZE_VER1;
    req.mnt_id = (uint64_t)child_mount_id;
    req.param = STATMOUNT_MNT_BASIC;
    if (syscall_dispatch_impl(__NR_statmount, (long)(uintptr_t)&req,
                              (long)(uintptr_t)st, sizeof(statmount_storage), 0, 0, 0) != -ENOENT) {
        errno = ESTALE;
        goto out;
    }

    memset(&req, 0, sizeof(req));
    req.size = MNT_ID_REQ_SIZE_VER1;
    req.mnt_id = LSMT_ROOT;
    count = syscall_dispatch_impl(__NR_listmount, (long)(uintptr_t)&req,
                                  (long)(uintptr_t)ids,
                                  sizeof(ids) / sizeof(ids[0]), 0, 0, 0);
    if (count < 0) {
        errno = (int)-count;
        goto out;
    }
    for (long i = 0; i < count; i++) {
        if (ids[i] == (uint64_t)child_mount_id) {
            errno = EEXIST;
            goto out;
        }
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

int vfs_contract_mount_namespace_teardown_accounts_mounts_and_detached_refs(void) {
    struct task *parent = task_current();
    struct task *child = NULL;
    int fd = -1;
    unsigned int initial_active;
    unsigned int mounted_active;
    int child_pid;
    int ret = -1;

    if (!parent || !parent->fs) {
        errno = ESRCH;
        return -1;
    }

    mount_contract_cleanup_mount_namespace_paths();
    vfs_reap_detached_mount_refs();
    initial_active = fs_mount_namespace_active_mounts(parent->fs);

    if (mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-child-source", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-peer-b", 0700)) != 0 ||
        mount_contract_write_file("/tmp/vfs-mntns-parent-source/file", "pinned") != 0 ||
        mount_contract_write_file("/tmp/vfs-mntns-child-source/file", "sibling") != 0 ||
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
    mount_contract_release_lookup_child(parent, child);
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
            mount_contract_release_lookup_child(parent, child);
        }
        mount_contract_cleanup_mount_namespace_paths();
        vfs_reap_detached_mount_refs();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_lazy_umount_ref_survives_descendant_task_tree(void) {
    struct task *parent = task_current();
    struct task *child = NULL;
    struct task *grandchild = NULL;
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
        mount_contract_write_file("/tmp/vfs-mntns-parent-source/file", "tree") != 0 ||
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

    task_set_current(grandchild);
    fd = open_impl("/tmp/vfs-mntns-target/file", O_RDONLY, 0);
    task_set_current(parent);
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

    task_set_current(grandchild);
    if (close_impl(fd) != 0) {
        fd = -1;
        task_set_current(parent);
        goto out;
    }
    fd = -1;
    task_set_current(parent);

    task_unlink_child_impl(child, grandchild);
    task_put(grandchild);
    grandchild = NULL;
    task_unlink_child_impl(parent, child);
    task_put(child);
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
            task_set_current(grandchild ? grandchild : parent);
            close_impl(fd);
            fd = -1;
        }
        task_set_current(parent);
        if (grandchild) {
            task_unlink_child_impl(child ? child : parent, grandchild);
            task_put(grandchild);
        }
        if (child) {
            task_unlink_child_impl(parent, child);
            task_put(child);
        }
        mount_contract_cleanup_mount_namespace_paths();
        vfs_reap_detached_mount_refs();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_lazy_umount_ref_survives_child_root_and_pwd_pins(void) {
    struct task *parent = task_current();
    struct task *child = NULL;
    int ret = -1;

    if (!parent || !parent->fs) {
        errno = ESRCH;
        return -1;
    }

    mount_contract_cleanup_mount_namespace_paths();
    vfs_reap_detached_mount_refs();
    if (mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0 ||
        mount_contract_write_file("/tmp/vfs-mntns-parent-source/file", "rootpin") != 0 ||
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
    task_put(child);
    child = NULL;

    if (vfs_reap_detached_mount_refs() != 1 || vfs_detached_mount_ref_count() != 0) {
        errno = EBUSY;
        goto out;
    }
    ret = 0;

out:
    {
        int saved_errno = errno;
        task_set_current(parent);
        if (child) {
            task_unlink_child_impl(parent, child);
            task_put(child);
        }
        mount_contract_cleanup_mount_namespace_paths();
        vfs_reap_detached_mount_refs();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_proc_self_mountinfo_lists_bind_mount(void) {
    char content[4096];
    int ret = -1;

    mount_contract_cleanup_mount_namespace_paths();
    if (mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0) {
        goto out;
    }
    if (mount_contract_write_file("/tmp/vfs-mntns-parent-source/file", "parent") != 0 ||
        mount_contract_write_file("/tmp/vfs-mntns-target/file", "target") != 0) {
        goto out;
    }
    if (mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }
    if (mount_contract_read_proc_file("/proc/self/mountinfo", content, sizeof(content)) != 0) {
        goto out;
    }
    if (!mount_contract_content_contains(content, "/tmp/vfs-mntns-parent-source") ||
        !mount_contract_content_contains(content, "/tmp/vfs-mntns-target") ||
        !mount_contract_content_contains(content, " rw,bind\n")) {
        errno = ENODATA;
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

int vfs_contract_proc_self_mountinfo_uses_linux_shaped_optional_fields(void) {
    char content[4096];
    int ret = -1;

    mount_contract_cleanup_mount_namespace_paths();
    if (mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0) {
        goto out;
    }
    if (mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }
    if (mount_contract_read_proc_file("/proc/self/mountinfo", content, sizeof(content)) != 0) {
        goto out;
    }
    if (!mount_contract_content_contains(content, "1 0 0:1 / / rw,relatime - orlix-root orlix-root rw\n") ||
        !mount_contract_content_contains(content, " /tmp/vfs-mntns-target rw,relatime - none /tmp/vfs-mntns-parent-source rw,bind\n")) {
        errno = ENODATA;
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

int vfs_contract_proc_self_mountinfo_reports_shared_propagation(void) {
    char content[4096];
    int ret = -1;

    mount_contract_cleanup_mount_namespace_paths();
    if (mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0) {
        goto out;
    }
    if (mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0 ||
        mount(NULL, "/tmp/vfs-mntns-target", NULL, MS_BIND | MS_REMOUNT | MS_SHARED, NULL) != 0) {
        goto out;
    }
    if (mount_contract_read_proc_file("/proc/self/mountinfo", content, sizeof(content)) != 0) {
        goto out;
    }
    if (!mount_contract_content_contains(content, " /tmp/vfs-mntns-target rw,relatime shared:")) {
        errno = ENODATA;
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

int vfs_contract_proc_self_mountinfo_reports_slave_private_and_unbindable_propagation(void) {
    char content[4096];
    int ret = -1;

    mount_contract_cleanup_mount_namespace_paths();
    if (mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0) {
        goto out;
    }
    if (mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0 ||
        mount(NULL, "/tmp/vfs-mntns-target", NULL, MS_BIND | MS_REMOUNT | MS_SLAVE, NULL) != 0) {
        goto out;
    }
    if (mount_contract_read_proc_file("/proc/self/mountinfo", content, sizeof(content)) != 0 ||
        !mount_contract_content_contains(content, " /tmp/vfs-mntns-target rw,relatime master:")) {
        errno = ENODATA;
        goto out;
    }

    if (mount(NULL, "/tmp/vfs-mntns-target", NULL, MS_BIND | MS_REMOUNT | MS_PRIVATE, NULL) != 0) {
        goto out;
    }
    if (mount_contract_read_proc_file("/proc/self/mountinfo", content, sizeof(content)) != 0 ||
        !mount_contract_content_contains(content, " /tmp/vfs-mntns-target rw,relatime - none ") ||
        mount_contract_content_contains(content, " shared:") ||
        mount_contract_content_contains(content, " master:") ||
        mount_contract_content_contains(content, " unbindable")) {
        errno = ENODATA;
        goto out;
    }

    if (mount(NULL, "/tmp/vfs-mntns-target", NULL, MS_BIND | MS_REMOUNT | MS_UNBINDABLE, NULL) != 0) {
        goto out;
    }
    if (mount_contract_read_proc_file("/proc/self/mountinfo", content, sizeof(content)) != 0 ||
        !mount_contract_content_contains(content, " /tmp/vfs-mntns-target rw,relatime unbindable")) {
        errno = ENODATA;
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

int vfs_contract_mount_rejects_multiple_propagation_flags(void) {
    int ret = -1;

    mount_contract_cleanup_mount_namespace_paths();
    if (mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0) {
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
        mount_contract_cleanup_mount_namespace_paths();
        errno = saved_errno;
    }
    return ret;
}

int vfs_contract_proc_self_mounts_lists_bind_mount(void) {
    char content[4096];
    int ret = -1;

    mount_contract_cleanup_mount_namespace_paths();
    if (mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0) {
        goto out;
    }
    if (mount_contract_write_file("/tmp/vfs-mntns-parent-source/file", "parent") != 0 ||
        mount_contract_write_file("/tmp/vfs-mntns-target/file", "target") != 0) {
        goto out;
    }
    if (mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }
    if (mount_contract_read_proc_file("/proc/self/mounts", content, sizeof(content)) != 0) {
        goto out;
    }
    if (!mount_contract_content_contains(content, "/tmp/vfs-mntns-parent-source /tmp/vfs-mntns-target none rw,bind 0 0\n")) {
        errno = ENODATA;
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

int vfs_contract_proc_self_mountinfo_reports_readonly_remount(void) {
    char content[4096];
    int ret = -1;

    mount_contract_cleanup_mount_namespace_paths();
    if (mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-parent-source", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0) {
        goto out;
    }
    if (mount_contract_write_file("/tmp/vfs-mntns-parent-source/file", "parent") != 0 ||
        mount_contract_write_file("/tmp/vfs-mntns-target/file", "target") != 0) {
        goto out;
    }
    if (mount("/tmp/vfs-mntns-parent-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0 ||
        mount(NULL, "/tmp/vfs-mntns-target", NULL, MS_BIND | MS_REMOUNT | MS_RDONLY, NULL) != 0) {
        goto out;
    }
    if (mount_contract_read_proc_file("/proc/self/mountinfo", content, sizeof(content)) != 0) {
        goto out;
    }
    if (!mount_contract_content_contains(content, "/tmp/vfs-mntns-parent-source") ||
        !mount_contract_content_contains(content, "/tmp/vfs-mntns-target") ||
        !mount_contract_content_contains(content, " ro,relatime - none ") ||
        !mount_contract_content_contains(content, " ro,bind\n")) {
        errno = ENODATA;
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

int vfs_contract_proc_self_mountinfo_uses_current_mount_namespace(void) {
    struct task *parent = task_current();
    struct task *child = NULL;
    char content[4096];
    int ret = -1;

    if (!parent || !parent->fs) {
        errno = ESRCH;
        return -1;
    }

    mount_contract_cleanup_mount_namespace_paths();
    if (mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-child-source", 0700)) != 0 ||
        mount_contract_ignore_exists(mkdir_impl("/tmp/vfs-mntns-target", 0700)) != 0) {
        goto out;
    }
    if (mount_contract_write_file("/tmp/vfs-mntns-child-source/file", "child") != 0 ||
        mount_contract_write_file("/tmp/vfs-mntns-target/file", "target") != 0) {
        goto out;
    }

    child = task_create_child_impl(parent);
    if (!child) {
        goto out;
    }
    if (fs_unshare_mount_namespace(child->fs) != 0) {
        goto out;
    }

    task_set_current(child);
    if (mount("/tmp/vfs-mntns-child-source", "/tmp/vfs-mntns-target", NULL, MS_BIND, NULL) != 0) {
        goto out;
    }
    if (mount_contract_read_proc_file("/proc/self/mountinfo", content, sizeof(content)) != 0 ||
        !mount_contract_content_contains(content, "/tmp/vfs-mntns-child-source")) {
        errno = ENODATA;
        goto out;
    }

    task_set_current(parent);
    if (mount_contract_read_proc_file("/proc/self/mountinfo", content, sizeof(content)) != 0 ||
        mount_contract_content_contains(content, "/tmp/vfs-mntns-child-source")) {
        errno = ENODATA;
        goto out;
    }

    ret = 0;

out:
    if (child) {
        task_set_current(child);
        umount_impl("/tmp/vfs-mntns-target");
        task_set_current(parent);
        task_unlink_child_impl(parent, child);
        task_put(child);
    } else {
        task_set_current(parent);
    }
    mount_contract_cleanup_mount_namespace_paths();
    return ret;
}

int vfs_contract_proc_self_mount_views_do_not_expose_host_paths(void) {
    char content[4096];
    const char *persistent_root = vfs_persistent_backing_root();

    if (!persistent_root) {
        errno = ENOENT;
        return -1;
    }
    if (mount_contract_read_proc_file("/proc/self/mountinfo", content, sizeof(content)) != 0) {
        return -1;
    }
    if (mount_contract_content_contains(content, persistent_root)) {
        errno = EXDEV;
        return -1;
    }
    if (mount_contract_read_proc_file("/proc/self/mounts", content, sizeof(content)) != 0) {
        return -1;
    }
    if (mount_contract_content_contains(content, persistent_root)) {
        errno = EXDEV;
        return -1;
    }

    return 0;
}
