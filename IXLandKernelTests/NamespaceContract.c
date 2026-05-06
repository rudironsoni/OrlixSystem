#include <linux/fcntl.h>
#include <linux/capability.h>
#include <linux/mount.h>
#include <linux/sched.h>
#include <linux/utsname.h>

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "NamespaceContract.h"
#include "fs/vfs.h"
#include "kernel/cred_internal.h"
#include "kernel/task.h"
#include "kernel/uts.h"

extern int clone_impl(uint64_t flags);
extern int unshare_impl(uint64_t flags);
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
extern int setgroups_impl(int size, const gid_t *list);
extern void cred_reset_to_defaults(void);
extern int capget(cap_user_header_t header, cap_user_data_t data);
extern int capset(cap_user_header_t header, const cap_user_data_t data);

static int expect_errno(int expected) {
    if (errno != expected) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

static int ignore_exists(int result) {
    if (result == 0 || errno == EEXIST) {
        return 0;
    }
    return -1;
}

static int write_file(const char *path, const char *content) {
    int fd;
    size_t len = strlen(content);

    fd = open_impl(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        return -1;
    }
    if (write_impl(fd, content, len) != (long)len) {
        close_impl(fd);
        return -1;
    }
    return close_impl(fd);
}

static int write_existing_file(const char *path, const char *content) {
    int fd;
    size_t len = strlen(content);

    fd = open_impl(path, O_WRONLY, 0);
    if (fd < 0) {
        return -1;
    }
    if (write_impl(fd, content, len) != (long)len) {
        close_impl(fd);
        return -1;
    }
    return close_impl(fd);
}

static int read_file_exact(const char *path, const char *expected) {
    char buf[64];
    int fd;
    long n;
    size_t len = strlen(expected);

    fd = open_impl(path, O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }
    memset(buf, 0, sizeof(buf));
    n = read_impl(fd, buf, sizeof(buf) - 1);
    close_impl(fd);
    if (n != (long)len || memcmp(buf, expected, len) != 0) {
        errno = ENODATA;
        return -1;
    }
    return 0;
}

static int read_file_contains(const char *path, const char *needle) {
    char buf[256];
    int fd;
    long n;

    fd = open_impl(path, O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }
    memset(buf, 0, sizeof(buf));
    n = read_impl(fd, buf, sizeof(buf) - 1);
    close_impl(fd);
    if (n <= 0 || !strstr(buf, needle)) {
        errno = ENODATA;
        return -1;
    }
    return 0;
}

static void cleanup_mount_paths(void) {
    umount("/tmp/ns-target");
    unlink_impl("/tmp/ns-parent-source/file");
    unlink_impl("/tmp/ns-child-source/file");
    unlink_impl("/tmp/ns-target/file");
    rmdir_impl("/tmp/ns-parent-source");
    rmdir_impl("/tmp/ns-child-source");
    rmdir_impl("/tmp/ns-target");
}

static int prepare_mount_paths(void) {
    cleanup_mount_paths();
    if (ignore_exists(mkdir_impl("/tmp/ns-parent-source", 0700)) != 0 ||
        ignore_exists(mkdir_impl("/tmp/ns-child-source", 0700)) != 0 ||
        ignore_exists(mkdir_impl("/tmp/ns-target", 0700)) != 0) {
        return -1;
    }
    if (write_file("/tmp/ns-parent-source/file", "parent") != 0 ||
        write_file("/tmp/ns-child-source/file", "child") != 0 ||
        write_file("/tmp/ns-target/file", "target") != 0) {
        return -1;
    }
    return 0;
}

static void reset_namespace_contract_state(void) {
    cred_reset_to_defaults();
    uts_reset_current_namespace();
    if (get_current()) {
        atomic_store(&get_current()->new_pid_namespace_pending, false);
    }
    cleanup_mount_paths();
}

static struct task_struct *lookup_child_from_pid(int pid) {
    struct task_struct *child = task_lookup(pid);
    if (!child) {
        errno = ESRCH;
    }
    return child;
}

static void release_lookup_child(struct task_struct *parent, struct task_struct *child) {
    if (!child) {
        return;
    }
    task_unlink_child_impl(parent, child);
    free_task(child);
    free_task(child);
}

int namespace_contract_clone_newuts_isolates_child_hostname(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
    struct task_struct *saved;
    struct new_utsname uts;
    int pid;
    int ret = -1;

    reset_namespace_contract_state();
    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    if (sethostname_impl("parent-node", 11) != 0) {
        return -1;
    }

    pid = clone_impl(CLONE_NEWUTS);
    if (pid < 0) {
        return -1;
    }
    child = lookup_child_from_pid(pid);
    if (!child) {
        return -1;
    }

    saved = get_current();
    set_current(child);
    if (sethostname_impl("child-node", 10) != 0 || uname_impl(&uts) != 0 ||
        strcmp(uts.nodename, "child-node") != 0) {
        goto out;
    }
    set_current(parent);
    if (uname_impl(&uts) != 0 || strcmp(uts.nodename, "parent-node") != 0) {
        goto out;
    }
    ret = 0;

out:
    set_current(saved);
    release_lookup_child(parent, child);
    reset_namespace_contract_state();
    return ret;
}

int namespace_contract_clone_without_newuts_shares_hostname(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
    struct task_struct *saved;
    struct new_utsname uts;
    int pid;
    int ret = -1;

    reset_namespace_contract_state();
    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    if (sethostname_impl("parent-node", 11) != 0) {
        return -1;
    }

    pid = clone_impl(0);
    if (pid < 0) {
        return -1;
    }
    child = lookup_child_from_pid(pid);
    if (!child) {
        return -1;
    }

    saved = get_current();
    set_current(child);
    if (sethostname_impl("child-node", 10) != 0) {
        goto out;
    }
    set_current(parent);
    if (uname_impl(&uts) != 0 || strcmp(uts.nodename, "child-node") != 0) {
        goto out;
    }
    ret = 0;

out:
    set_current(saved);
    release_lookup_child(parent, child);
    reset_namespace_contract_state();
    return ret;
}

int namespace_contract_unshare_newuts_isolates_current_task(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
    struct task_struct *saved;
    struct new_utsname uts;
    int pid;
    int ret = -1;

    reset_namespace_contract_state();
    if (!parent) {
        errno = ESRCH;
        return -1;
    }

    pid = clone_impl(0);
    if (pid < 0) {
        return -1;
    }
    child = lookup_child_from_pid(pid);
    if (!child) {
        return -1;
    }
    if (unshare_impl(CLONE_NEWUTS) != 0 || sethostname_impl("parent-node", 11) != 0) {
        goto out;
    }

    saved = get_current();
    set_current(child);
    if (uname_impl(&uts) != 0 || strcmp(uts.nodename, "ixland") != 0) {
        goto out_restore;
    }
    set_current(parent);
    if (uname_impl(&uts) != 0 || strcmp(uts.nodename, "parent-node") != 0) {
        goto out_restore;
    }
    ret = 0;

out_restore:
    set_current(parent);
out:
    release_lookup_child(parent, child);
    reset_namespace_contract_state();
    return ret;
}

int namespace_contract_clone_newns_isolates_child_mounts(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
    struct task_struct *saved;
    int pid;
    int ret = -1;

    reset_namespace_contract_state();
    if (!parent || prepare_mount_paths() != 0) {
        return -1;
    }

    pid = clone_impl(CLONE_NEWNS);
    if (pid < 0) {
        goto out_cleanup;
    }
    child = lookup_child_from_pid(pid);
    if (!child) {
        goto out_cleanup;
    }

    saved = get_current();
    set_current(child);
    if (mount("/tmp/ns-child-source", "/tmp/ns-target", NULL, MS_BIND, NULL) != 0 ||
        read_file_exact("/tmp/ns-target/file", "child") != 0) {
        goto out_restore;
    }

    set_current(parent);
    if (read_file_exact("/tmp/ns-target/file", "target") != 0) {
        goto out_restore;
    }
    ret = 0;

out_restore:
    set_current(child);
    umount("/tmp/ns-target");
    set_current(saved);
    release_lookup_child(parent, child);
out_cleanup:
    cleanup_mount_paths();
    reset_namespace_contract_state();
    return ret;
}

int namespace_contract_clone_without_newns_shares_mounts(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
    struct task_struct *saved;
    int pid;
    int ret = -1;

    reset_namespace_contract_state();
    if (!parent || prepare_mount_paths() != 0) {
        return -1;
    }

    pid = clone_impl(0);
    if (pid < 0) {
        goto out_cleanup;
    }
    child = lookup_child_from_pid(pid);
    if (!child) {
        goto out_cleanup;
    }

    saved = get_current();
    set_current(child);
    if (mount("/tmp/ns-child-source", "/tmp/ns-target", NULL, MS_BIND, NULL) != 0) {
        goto out_restore;
    }
    set_current(parent);
    if (read_file_exact("/tmp/ns-target/file", "child") != 0) {
        goto out_restore;
    }
    ret = 0;

out_restore:
    set_current(parent);
    umount("/tmp/ns-target");
    set_current(saved);
    release_lookup_child(parent, child);
out_cleanup:
    cleanup_mount_paths();
    reset_namespace_contract_state();
    return ret;
}

int namespace_contract_unshare_newns_isolates_current_mounts(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
    struct task_struct *saved;
    int pid;
    int ret = -1;

    reset_namespace_contract_state();
    if (!parent || prepare_mount_paths() != 0) {
        return -1;
    }

    pid = clone_impl(0);
    if (pid < 0) {
        goto out_cleanup;
    }
    child = lookup_child_from_pid(pid);
    if (!child) {
        goto out_cleanup;
    }
    if (unshare_impl(CLONE_NEWNS) != 0 ||
        mount("/tmp/ns-parent-source", "/tmp/ns-target", NULL, MS_BIND, NULL) != 0 ||
        read_file_exact("/tmp/ns-target/file", "parent") != 0) {
        goto out_release;
    }

    saved = get_current();
    set_current(child);
    if (read_file_exact("/tmp/ns-target/file", "target") != 0) {
        goto out_restore;
    }
    ret = 0;

out_restore:
    set_current(saved);
out_release:
    set_current(parent);
    umount("/tmp/ns-target");
    release_lookup_child(parent, child);
out_cleanup:
    cleanup_mount_paths();
    reset_namespace_contract_state();
    return ret;
}

int namespace_contract_unshare_clone_fs_splits_shared_fs_state(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
    struct task_struct *saved;
    struct fs_struct *original_shared_fs;
    int original_umask;
    int pid;
    int ret = -1;

    reset_namespace_contract_state();
    if (!parent || !parent->fs) {
        errno = ESRCH;
        return -1;
    }

    if (fs_set_root(parent->fs, "/") != 0 || fs_set_pwd(parent->fs, "/") != 0) {
        goto out;
    }
    original_umask = parent->fs->umask;
    pid = clone_impl(CLONE_FS);
    if (pid < 0) {
        goto out;
    }
    child = lookup_child_from_pid(pid);
    if (!child || !child->fs) {
        goto out;
    }
    if (child->fs != parent->fs) {
        errno = EPROTO;
        goto out_release;
    }
    original_shared_fs = parent->fs;

    saved = get_current();
    set_current(child);
    if (unshare_impl(CLONE_FS) != 0) {
        set_current(saved);
        goto out_release;
    }
    if (child->fs == original_shared_fs) {
        set_current(saved);
        errno = EPROTO;
        goto out_release;
    }
    if (fs_set_root(child->fs, "/tmp") != 0 || fs_set_pwd(child->fs, "/tmp") != 0) {
        set_current(saved);
        goto out_release;
    }
    child->fs->umask = 0077;
    set_current(saved);

    if (strcmp(child->fs->root_path, "/tmp") != 0 ||
        strcmp(child->fs->pwd_path, "/tmp") != 0 ||
        child->fs->umask != 0077) {
        errno = EPROTO;
        goto out_release;
    }
    if (parent->fs != original_shared_fs ||
        strcmp(parent->fs->root_path, "/") != 0 ||
        strcmp(parent->fs->pwd_path, "/") != 0 ||
        parent->fs->umask != original_umask) {
        errno = EPROTO;
        goto out_release;
    }

    saved = get_current();
    set_current(child);
    if (unshare_impl(CLONE_NEWNS) != 0) {
        set_current(saved);
        goto out_release;
    }
    if (child->fs == original_shared_fs ||
        fs_mount_namespace_id(child->fs) == fs_mount_namespace_id(parent->fs)) {
        set_current(saved);
        errno = EPROTO;
        goto out_release;
    }
    if (fs_set_pwd(child->fs, "/var") != 0) {
        set_current(saved);
        goto out_release;
    }
    child->fs->umask = 0022;
    set_current(saved);

    if (strcmp(child->fs->pwd_path, "/var") != 0 ||
        strcmp(parent->fs->pwd_path, "/") != 0 ||
        parent->fs->umask != original_umask) {
        errno = EPROTO;
        goto out_release;
    }
    ret = 0;

out_release:
    release_lookup_child(parent, child);
out:
    reset_namespace_contract_state();
    return ret;
}

int namespace_contract_clone_newns_with_clone_fs_rejected(void) {
    reset_namespace_contract_state();
    errno = 0;
    if (clone_impl(CLONE_NEWNS | CLONE_FS) != -1) {
        errno = EPROTO;
        return -1;
    }
    return expect_errno(EINVAL);
}

int namespace_contract_clone_newpid_records_child_namespace_metadata(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child;
    int pid;
    int ret = -1;

    reset_namespace_contract_state();
    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    pid = clone_impl(CLONE_NEWPID);
    if (pid < 0) {
        return -1;
    }
    child = lookup_child_from_pid(pid);
    if (!child) {
        return -1;
    }
    if (child->pid_ns_level == parent->pid_ns_level + 1 && child->ns_pid == 1) {
        ret = 0;
    } else {
        errno = ENODATA;
    }
    release_lookup_child(parent, child);
    reset_namespace_contract_state();
    return ret;
}

int namespace_contract_unshare_newpid_applies_to_next_child_metadata(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child;
    int pid;
    int ret = -1;

    reset_namespace_contract_state();
    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    if (unshare_impl(CLONE_NEWPID) != 0) {
        return -1;
    }
    pid = clone_impl(0);
    if (pid < 0) {
        return -1;
    }
    child = lookup_child_from_pid(pid);
    if (!child) {
        return -1;
    }
    if (child->pid_ns_level == parent->pid_ns_level + 1 && child->ns_pid == 1 &&
        parent->pid_ns_level == 0) {
        ret = 0;
    } else {
        errno = ENODATA;
    }
    release_lookup_child(parent, child);
    reset_namespace_contract_state();
    return ret;
}

int namespace_contract_newuser_caps_are_scoped_to_mount_namespace_owner(void) {
    struct __user_cap_header_struct header = {
        .version = _LINUX_CAPABILITY_VERSION_3,
        .pid = 0,
    };
    struct __user_cap_data_struct data[_LINUX_CAPABILITY_U32S_3];
    struct task_struct *parent = get_current();
    struct task_struct *user_child = NULL;
    struct task_struct *user_mnt_child = NULL;
    int pid;
    int ret = -1;

    reset_namespace_contract_state();
    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    if (prepare_mount_paths() != 0) {
        errno = ENOMSG;
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
    if (mount("/tmp/ns-parent-source", "/tmp/ns-target", NULL, MS_BIND, NULL) != -1 ||
        errno != EPERM) {
        errno = EPROTO;
        goto out;
    }

    pid = clone_impl(CLONE_NEWUSER);
    if (pid < 0) {
        errno = ENODATA;
        goto out;
    }
    user_child = lookup_child_from_pid(pid);
    if (!user_child) {
        errno = ESRCH;
        goto out;
    }
    set_current(user_child);
    errno = 0;
    if (mount("/tmp/ns-parent-source", "/tmp/ns-target", NULL, MS_BIND, NULL) != -1 ||
        errno != EPERM) {
        set_current(parent);
        errno = ENODATA;
        goto out;
    }
    set_current(parent);

    pid = clone_impl(CLONE_NEWUSER | CLONE_NEWNS);
    if (pid < 0) {
        errno = ENXIO;
        goto out;
    }
    user_mnt_child = lookup_child_from_pid(pid);
    if (!user_mnt_child) {
        errno = ESRCH;
        goto out;
    }
    set_current(user_mnt_child);
    if (mount("/tmp/ns-parent-source", "/tmp/ns-target", NULL, MS_BIND, NULL) != 0) {
        set_current(parent);
        goto out;
    }
    if (read_file_exact("/tmp/ns-target/file", "parent") != 0) {
        set_current(parent);
        errno = ENOMSG;
        goto out;
    }
    set_current(parent);
    if (read_file_exact("/tmp/ns-target/file", "target") != 0) {
        errno = ESRCH;
        goto out;
    }

    ret = 0;

out:
    {
        int saved_errno = errno;
        set_current(parent);
        if (user_child) {
            release_lookup_child(parent, user_child);
        }
        if (user_mnt_child) {
            release_lookup_child(parent, user_mnt_child);
        }
        cleanup_mount_paths();
        reset_namespace_contract_state();
        errno = saved_errno;
    }
    return ret;
}

int namespace_contract_proc_uid_gid_maps_are_visible(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
    int pid;
    int ret = -1;

    reset_namespace_contract_state();
    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    if (read_file_contains("/proc/self/uid_map", "         0          0 4294967295") != 0 ||
        read_file_contains("/proc/self/gid_map", "         0          0 4294967295") != 0) {
        goto out;
    }

    pid = clone_impl(CLONE_NEWUSER);
    if (pid < 0) {
        goto out;
    }
    child = lookup_child_from_pid(pid);
    if (!child) {
        goto out;
    }
    set_current(child);
    if (read_file_contains("/proc/self/uid_map", "         0          0          1") != 0 ||
        read_file_contains("/proc/self/gid_map", "         0          0          1") != 0) {
        set_current(parent);
        goto out;
    }
    set_current(parent);
    ret = 0;

out:
    {
        int saved_errno = errno;
        set_current(parent);
        if (child) {
            release_lookup_child(parent, child);
        }
        reset_namespace_contract_state();
        errno = saved_errno;
    }
    return ret;
}

int namespace_contract_proc_uid_gid_maps_are_writable_with_setgroups_policy(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
    gid_t groups[1] = {7};
    int pid;
    int ret = -1;

    reset_namespace_contract_state();
    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    pid = clone_impl(CLONE_NEWUSER);
    if (pid < 0) {
        goto out;
    }
    child = lookup_child_from_pid(pid);
    if (!child) {
        goto out;
    }
    set_current(child);
    if (read_file_contains("/proc/self/setgroups", "allow") != 0) {
        set_current(parent);
        goto out;
    }
    errno = 0;
    if (write_existing_file("/proc/self/gid_map", "0 2000 1\n") != -1 ||
        errno != EPERM) {
        set_current(parent);
        errno = EPROTO;
        goto out;
    }
    if (write_existing_file("/proc/self/setgroups", "deny\n") != 0 ||
        read_file_contains("/proc/self/setgroups", "deny") != 0) {
        set_current(parent);
        goto out;
    }
    errno = 0;
    if (setgroups_impl(1, groups) != -1 || errno != EPERM) {
        set_current(parent);
        errno = ENODATA;
        goto out;
    }
    if (write_existing_file("/proc/self/uid_map", "0 1000 1\n") != 0 ||
        write_existing_file("/proc/self/gid_map", "0 2000 1\n") != 0 ||
        read_file_contains("/proc/self/uid_map", "         0       1000          1") != 0 ||
        read_file_contains("/proc/self/gid_map", "         0       2000          1") != 0) {
        set_current(parent);
        goto out;
    }
    set_current(parent);
    ret = 0;

out:
    {
        int saved_errno = errno;
        set_current(parent);
        if (child) {
            release_lookup_child(parent, child);
        }
        reset_namespace_contract_state();
        errno = saved_errno;
    }
    return ret;
}

int namespace_contract_newuser_caps_are_scoped_to_uts_namespace_owner(void) {
    struct __user_cap_header_struct header = {
        .version = _LINUX_CAPABILITY_VERSION_3,
        .pid = 0,
    };
    struct __user_cap_data_struct data[_LINUX_CAPABILITY_U32S_3];
    struct new_utsname uts;
    struct task_struct *parent = get_current();
    struct task_struct *user_child = NULL;
    struct task_struct *user_uts_child = NULL;
    int pid;
    int ret = -1;

    reset_namespace_contract_state();
    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    if (sethostname_impl("parent-node", 11) != 0 || capget(&header, data) != 0) {
        goto out;
    }
    data[CAP_SYS_ADMIN / 32].effective &= ~(1U << (CAP_SYS_ADMIN % 32));
    if (capset(&header, data) != 0) {
        goto out;
    }
    errno = 0;
    if (sethostname_impl("denied-parent", 13) != -1 || errno != EPERM) {
        errno = EPROTO;
        goto out;
    }

    pid = clone_impl(CLONE_NEWUSER);
    if (pid < 0) {
        goto out;
    }
    user_child = lookup_child_from_pid(pid);
    if (!user_child) {
        goto out;
    }
    set_current(user_child);
    errno = 0;
    if (sethostname_impl("denied-child", 12) != -1 || errno != EPERM) {
        set_current(parent);
        errno = ENODATA;
        goto out;
    }
    set_current(parent);

    pid = clone_impl(CLONE_NEWUSER | CLONE_NEWUTS);
    if (pid < 0) {
        goto out;
    }
    user_uts_child = lookup_child_from_pid(pid);
    if (!user_uts_child) {
        goto out;
    }
    set_current(user_uts_child);
    if (sethostname_impl("child-node", 10) != 0 || uname_impl(&uts) != 0 ||
        strcmp(uts.nodename, "child-node") != 0) {
        set_current(parent);
        goto out;
    }
    set_current(parent);
    if (uname_impl(&uts) != 0 || strcmp(uts.nodename, "parent-node") != 0) {
        goto out;
    }
    ret = 0;

out:
    {
        int saved_errno = errno;
        set_current(parent);
        if (user_child) {
            release_lookup_child(parent, user_child);
        }
        if (user_uts_child) {
            release_lookup_child(parent, user_uts_child);
        }
        reset_namespace_contract_state();
        errno = saved_errno;
    }
    return ret;
}
