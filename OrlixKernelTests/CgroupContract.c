#include "CgroupContract.h"

#include <asm/unistd.h>
#include <uapi/linux/capability.h>
#include <uapi/linux/fcntl.h>
#include <uapi/linux/sched.h>
#include <uapi/linux/signal.h>
#include <uapi/linux/wait.h>
#include <uapi/linux/errno.h>
#include <linux/string.h>

#include <stdint.h>

#include "kernel/cgroup.h"
#include "kernel/signal.h"
#include "kernel/task.h"
#include "runtime/syscall.h"

extern int errno;

extern int open_impl(const char *pathname, int flags, unsigned int mode);
extern long read_impl(int fd, void *buf, unsigned long count);
extern long write_impl(int fd, const void *buf, unsigned long count);
extern int close_impl(int fd);
extern int mkdir_impl(const char *pathname, unsigned int mode);
extern int rmdir_impl(const char *pathname);
extern int unshare_impl(uint64_t flags);
extern int clone_impl(uint64_t flags);
extern int mount_impl(const char *source, const char *target, const char *filesystemtype,
                      unsigned long mountflags, const void *data);
extern int capget_impl(cap_user_header_t header, cap_user_data_t data);
extern int capset_impl(cap_user_header_t header, const cap_user_data_t data);
extern void exit_impl(int status);
extern __kernel_pid_t waitpid_impl(__kernel_pid_t pid, int *wstatus, int options);

static void cgroup_contract_format_pid(int32_t pid, char *buf, unsigned long size);

static int cgroup_contract_read_file(const char *path, char *buf, unsigned long size) {
    int fd;
    long nread;

    if (!buf || size == 0) {
        errno = EINVAL;
        return -1;
    }
    fd = open_impl(path, O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }
    memset(buf, 0, size);
    nread = read_impl(fd, buf, size - 1);
    {
        int saved_errno = errno;
        close_impl(fd);
        errno = saved_errno;
    }
    if (nread < 0) {
        return -1;
    }
    return 0;
}

static int cgroup_contract_write_file(const char *path, const char *content) {
    int fd;
    unsigned long len;
    long nwritten;

    fd = open_impl(path, O_WRONLY, 0);
    if (fd < 0) {
        return -1;
    }
    len = (unsigned long)strlen(content);
    nwritten = write_impl(fd, content, len);
    {
        int saved_errno = errno;
        close_impl(fd);
        errno = saved_errno;
    }
    if (nwritten != (long)len) {
        return -1;
    }
    return 0;
}

static int cgroup_contract_write_current_pid(const char *path) {
    struct task *task = task_current();
    char pidbuf[32];

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    cgroup_contract_format_pid(task->pid, pidbuf, sizeof(pidbuf));
    return cgroup_contract_write_file(path, pidbuf);
}

static void cgroup_contract_clear_pending_signal(struct task *task, int sig) {
    struct signal_mask_bits mask;
    int delivered;

    if (!task) {
        return;
    }
    memset(&mask, 0, sizeof(mask));
    while (signal_dequeue(task, &mask, &delivered) == 1) {
        if (delivered != sig) {
            signal_generate_task(task, delivered);
            break;
        }
    }
}

static void cgroup_contract_format_pid(int32_t pid, char *buf, unsigned long size) {
    char digits[16];
    unsigned long len = 0;
    unsigned long pos = 0;
    uint32_t value = (uint32_t)pid;

    if (!buf || size == 0) {
        return;
    }
    do {
        digits[len++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0 && len < sizeof(digits));
    while (len > 0 && pos + 1 < size) {
        buf[pos++] = digits[--len];
    }
    if (pos + 1 < size) {
        buf[pos++] = '\n';
    }
    buf[pos < size ? pos : size - 1] = '\0';
}

static int cgroup_contract_restore_root(void) {
    struct task *task = task_current();

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    if (task_reset_cgroup_namespace(task) != 0) {
        errno = ENOMEM;
        return -1;
    }
    return cgroup_attach_task_path(task, "/");
}

static int cgroup_contract_ignore_exists(int result) {
    if (result == 0 || errno == EEXIST) {
        return 0;
    }
    return -1;
}

int cgroup_contract_current_task_starts_in_root(void) {
    struct task *task = task_current();

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    if (strcmp(task_cgroup_path(task), "/") != 0) {
        errno = ENODATA;
        return -1;
    }
    if (task_cgroup_member_count(task) == 0) {
        errno = ENOMSG;
        return -1;
    }
    return 0;
}

int cgroup_contract_child_inherits_parent_cgroup(void) {
    struct task *parent = task_current();
    struct task *child;
    unsigned int before;
    int ret = -1;

    if (!parent) {
        errno = ESRCH;
        return -1;
    }

    before = task_cgroup_member_count(parent);
    child = task_create_child_impl(parent);
    if (!child) {
        return -1;
    }
    if (strcmp(task_cgroup_path(child), task_cgroup_path(parent)) != 0) {
        errno = ENODATA;
        goto out;
    }
    if (task_cgroup_member_count(parent) != before + 1) {
        errno = ENOMSG;
        goto out;
    }

    ret = 0;

out:
    {
        int saved_errno = errno;
        task_unlink_child_impl(parent, child);
        task_put(child);
        if (task_cgroup_member_count(parent) != before) {
            ret = -1;
            saved_errno = EBUSY;
        }
        errno = saved_errno;
    }
    return ret;
}

int cgroup_contract_proc_self_cgroup_reports_root(void) {
    char buf[32];
    int fd;
    long nread;

    fd = open_impl("/proc/self/cgroup", O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }

    memset(buf, 0, sizeof(buf));
    nread = read_impl(fd, buf, sizeof(buf) - 1);
    {
        int saved_errno = errno;
        close_impl(fd);
        errno = saved_errno;
    }
    if (nread != 5 || strcmp(buf, "0::/\n") != 0) {
        errno = ENODATA;
        return -1;
    }
    return 0;
}

int cgroup_contract_cgroupfs_creates_group_and_moves_current_task(void) {
    char buf[128];
    int ret = -1;

    if (cgroup_contract_restore_root() != 0) {
        return -1;
    }
    if (cgroup_contract_ignore_exists(mkdir_impl("/sys/fs/cgroup/workers", 0755)) != 0) {
        goto out;
    }
    if (cgroup_contract_write_current_pid("/sys/fs/cgroup/workers/cgroup.procs") != 0) {
        goto out;
    }
    if (cgroup_contract_read_file("/proc/self/cgroup", buf, sizeof(buf)) != 0 ||
        strcmp(buf, "0::/workers\n") != 0) {
        errno = ENODATA;
        goto out;
    }
    if (cgroup_contract_read_file("/sys/fs/cgroup/workers/cgroup.procs", buf, sizeof(buf)) != 0 ||
        !strstr(buf, "\n")) {
        errno = ENOMSG;
        goto out;
    }
    ret = 0;

out:
    {
        int saved_errno = errno;
        cgroup_contract_restore_root();
        errno = saved_errno;
    }
    return ret;
}

int cgroup_contract_cgroupfs_moves_child_and_proc_pid_reports_membership(void) {
    struct task *parent = task_current();
    struct task *child;
    char path[64];
    char buf[128];
    int ret = -1;

    if (!parent || cgroup_contract_restore_root() != 0) {
        return -1;
    }
    if (cgroup_contract_ignore_exists(mkdir_impl("/sys/fs/cgroup/children", 0755)) != 0) {
        goto out_no_child;
    }
    child = task_create_child_impl(parent);
    if (!child) {
        goto out_no_child;
    }
    cgroup_contract_format_pid(child->pid, buf, sizeof(buf));
    if (cgroup_contract_write_file("/sys/fs/cgroup/children/cgroup.procs", buf) != 0) {
        goto out;
    }
    memcpy(path, "/proc/", 6);
    cgroup_contract_format_pid(child->pid, path + 6, sizeof(path) - 6);
    for (char *p = path; *p; p++) {
        if (*p == '\n') {
            memcpy(p, "/cgroup", 8);
            break;
        }
    }
    if (cgroup_contract_read_file(path, buf, sizeof(buf)) != 0 ||
        strcmp(buf, "0::/children\n") != 0) {
        errno = ENODATA;
        goto out;
    }
    ret = 0;

out:
    {
        int saved_errno = errno;
        task_unlink_child_impl(parent, child);
        task_put(child);
        errno = saved_errno;
    }
out_no_child:
    {
        int saved_errno = errno;
        cgroup_contract_restore_root();
        errno = saved_errno;
    }
    return ret;
}

int cgroup_contract_cgroup_namespace_rebases_proc_and_cgroupfs_visibility(void) {
    char buf[128];
    int ret = -1;

    if (cgroup_contract_restore_root() != 0) {
        return -1;
    }
    if (cgroup_contract_ignore_exists(mkdir_impl("/sys/fs/cgroup/session", 0755)) != 0 ||
        cgroup_contract_write_current_pid("/sys/fs/cgroup/session/cgroup.procs") != 0) {
        goto out;
    }
    if (unshare_impl(CLONE_NEWCGROUP) != 0) {
        goto out;
    }
    if (cgroup_contract_read_file("/proc/self/cgroup", buf, sizeof(buf)) != 0 ||
        strcmp(buf, "0::/\n") != 0) {
        errno = ENODATA;
        goto out;
    }
    if (cgroup_contract_ignore_exists(mkdir_impl("/sys/fs/cgroup/inner", 0755)) != 0 ||
        cgroup_contract_write_current_pid("/sys/fs/cgroup/inner/cgroup.procs") != 0) {
        goto out;
    }
    if (cgroup_contract_read_file("/proc/self/cgroup", buf, sizeof(buf)) != 0 ||
        strcmp(buf, "0::/inner\n") != 0) {
        errno = ENODATA;
        goto out;
    }
    ret = 0;

out:
    {
        int saved_errno = errno;
        cgroup_contract_restore_root();
        errno = saved_errno;
    }
    return ret;
}

int cgroup_contract_pids_controller_tracks_current_and_max(void) {
    char buf[128];
    int ret = -1;

    if (cgroup_contract_restore_root() != 0) {
        return -1;
    }
    if (cgroup_contract_ignore_exists(mkdir_impl("/sys/fs/cgroup/pids-a", 0755)) != 0 ||
        cgroup_contract_write_current_pid("/sys/fs/cgroup/pids-a/cgroup.procs") != 0) {
        goto out;
    }
    if (cgroup_contract_read_file("/sys/fs/cgroup/pids-a/pids.current", buf, sizeof(buf)) != 0 ||
        strcmp(buf, "1\n") != 0) {
        errno = ENODATA;
        goto out;
    }
    if (cgroup_contract_read_file("/sys/fs/cgroup/pids-a/pids.max", buf, sizeof(buf)) != 0 ||
        strcmp(buf, "max\n") != 0) {
        errno = ENOMSG;
        goto out;
    }
    if (cgroup_contract_write_file("/sys/fs/cgroup/pids-a/pids.max", "2\n") != 0 ||
        cgroup_contract_read_file("/sys/fs/cgroup/pids-a/pids.max", buf, sizeof(buf)) != 0 ||
        strcmp(buf, "2\n") != 0) {
        errno = ERANGE;
        goto out;
    }
    ret = 0;

out:
    {
        int saved_errno = errno;
        cgroup_contract_restore_root();
        errno = saved_errno;
    }
    return ret;
}

int cgroup_contract_pids_max_rejects_extra_migration(void) {
    struct task *parent = task_current();
    struct task *child;
    char pidbuf[32];
    int ret = -1;

    if (!parent || cgroup_contract_restore_root() != 0) {
        return -1;
    }
    if (cgroup_contract_ignore_exists(mkdir_impl("/sys/fs/cgroup/pids-limit", 0755)) != 0) {
        goto out_no_child;
    }
    child = task_create_child_impl(parent);
    if (!child) {
        goto out_no_child;
    }
    if (cgroup_contract_write_file("/sys/fs/cgroup/pids-limit/pids.max", "1\n") != 0 ||
        cgroup_contract_write_current_pid("/sys/fs/cgroup/pids-limit/cgroup.procs") != 0) {
        goto out;
    }
    cgroup_contract_format_pid(child->pid, pidbuf, sizeof(pidbuf));
    if (cgroup_contract_write_file("/sys/fs/cgroup/pids-limit/cgroup.procs", pidbuf) == 0 ||
        errno != EAGAIN) {
        errno = EBUSY;
        goto out;
    }
    ret = 0;

out:
    {
        int saved_errno = errno;
        task_unlink_child_impl(parent, child);
        task_put(child);
        errno = saved_errno;
    }
out_no_child:
    {
        int saved_errno = errno;
        cgroup_contract_restore_root();
        errno = saved_errno;
    }
    return ret;
}

int cgroup_contract_freezer_blocks_and_releases_migration(void) {
    struct task *parent = task_current();
    struct task *child;
    char pidbuf[32];
    int ret = -1;

    if (!parent || cgroup_contract_restore_root() != 0) {
        return -1;
    }
    if (cgroup_contract_ignore_exists(mkdir_impl("/sys/fs/cgroup/frozen-target", 0755)) != 0 ||
        cgroup_contract_write_file("/sys/fs/cgroup/frozen-target/cgroup.freeze", "1\n") != 0) {
        goto out_no_child;
    }
    child = task_create_child_impl(parent);
    if (!child) {
        goto out_no_child;
    }
    cgroup_contract_format_pid(child->pid, pidbuf, sizeof(pidbuf));
    if (cgroup_contract_write_file("/sys/fs/cgroup/frozen-target/cgroup.procs", pidbuf) == 0 ||
        errno != EBUSY) {
        errno = ENODATA;
        goto out;
    }
    if (cgroup_contract_write_file("/sys/fs/cgroup/frozen-target/cgroup.freeze", "0\n") != 0 ||
        cgroup_contract_write_file("/sys/fs/cgroup/frozen-target/cgroup.procs", pidbuf) != 0) {
        goto out;
    }
    ret = 0;

out:
    {
        int saved_errno = errno;
        task_unlink_child_impl(parent, child);
        task_put(child);
        errno = saved_errno;
    }
out_no_child:
    {
        int saved_errno = errno;
        cgroup_contract_restore_root();
        errno = saved_errno;
    }
    return ret;
}

int cgroup_contract_subtree_control_accepts_pids_and_freezer(void) {
    char buf[128];
    int ret = -1;

    if (cgroup_contract_restore_root() != 0) {
        return -1;
    }
    if (cgroup_contract_read_file("/sys/fs/cgroup/cgroup.controllers", buf, sizeof(buf)) != 0 ||
        !strstr(buf, "pids") || !strstr(buf, "freezer")) {
        errno = ENODATA;
        goto out;
    }
    if (cgroup_contract_write_file("/sys/fs/cgroup/cgroup.subtree_control", "+pids +freezer\n") != 0 ||
        cgroup_contract_read_file("/sys/fs/cgroup/cgroup.subtree_control", buf, sizeof(buf)) != 0 ||
        !strstr(buf, "pids") || !strstr(buf, "freezer")) {
        errno = ENOMSG;
        goto out;
    }
    if (cgroup_contract_write_file("/sys/fs/cgroup/cgroup.subtree_control", "-pids\n") != 0 ||
        cgroup_contract_read_file("/sys/fs/cgroup/cgroup.subtree_control", buf, sizeof(buf)) != 0 ||
        strstr(buf, "pids") || !strstr(buf, "freezer")) {
        errno = ERANGE;
        goto out;
    }
    ret = 0;

out:
    {
        int saved_errno = errno;
        cgroup_contract_restore_root();
        errno = saved_errno;
    }
    return ret;
}

int cgroup_contract_cgroup_namespace_open_fd_survives_reset_until_closed(void) {
    char buf[128];
    int fd;
    long nread;
    int ret = -1;

    if (cgroup_contract_restore_root() != 0) {
        return -1;
    }
    if (cgroup_contract_ignore_exists(mkdir_impl("/sys/fs/cgroup/session-fd", 0755)) != 0 ||
        cgroup_contract_write_current_pid("/sys/fs/cgroup/session-fd/cgroup.procs") != 0 ||
        unshare_impl(CLONE_NEWCGROUP) != 0) {
        goto out;
    }
    fd = open_impl("/sys/fs/cgroup/cgroup.procs", O_RDONLY, 0);
    if (fd < 0) {
        goto out;
    }
    if (task_reset_cgroup_namespace(task_current()) != 0) {
        errno = ENOMEM;
        goto out_fd;
    }
    memset(buf, 0, sizeof(buf));
    nread = read_impl(fd, buf, sizeof(buf) - 1);
    if (nread <= 0 || !strstr(buf, "\n")) {
        errno = ENODATA;
        goto out_fd;
    }
    ret = 0;

out_fd:
    {
        int saved_errno = errno;
        close_impl(fd);
        errno = saved_errno;
    }
out:
    {
        int saved_errno = errno;
        cgroup_contract_restore_root();
        errno = saved_errno;
    }
    return ret;
}

int cgroup_contract_mount_cgroup2_exposes_cgroupfs_view(void) {
    char buf[128];
    int ret = -1;

    if (cgroup_contract_restore_root() != 0) {
        return -1;
    }
    cgroup_contract_ignore_exists(mkdir_impl("/tmp", 0777));
    if (cgroup_contract_ignore_exists(mkdir_impl("/tmp/cgroup2", 0755)) != 0) {
        goto out;
    }
    if (mount_impl("none", "/tmp/cgroup2", "cgroup2", 0, 0) != 0) {
        goto out;
    }
    if (cgroup_contract_ignore_exists(mkdir_impl("/tmp/cgroup2/mounted", 0755)) != 0) {
        goto out;
    }
    if (cgroup_contract_write_current_pid("/tmp/cgroup2/mounted/cgroup.procs") != 0) {
        goto out;
    }
    if (cgroup_contract_read_file("/proc/self/cgroup", buf, sizeof(buf)) != 0 ||
        strcmp(buf, "0::/mounted\n") != 0) {
        errno = ENODATA;
        goto out;
    }
    if (cgroup_contract_read_file("/tmp/cgroup2/mounted/pids.current", buf, sizeof(buf)) != 0 ||
        strcmp(buf, "1\n") != 0) {
        errno = ENOMSG;
        goto out;
    }
    ret = 0;

out:
    {
        int saved_errno = errno;
        cgroup_contract_restore_root();
        errno = saved_errno;
    }
    return ret;
}

int cgroup_contract_rmdir_empty_cgroup_removes_from_hierarchy(void) {
    char buf[128];
    int ret = -1;

    if (cgroup_contract_restore_root() != 0) {
        return -1;
    }
    if (cgroup_contract_ignore_exists(mkdir_impl("/sys/fs/cgroup/remove-empty", 0755)) != 0) {
        goto out;
    }
    if (rmdir_impl("/sys/fs/cgroup/remove-empty") != 0) {
        goto out;
    }
    if (cgroup_contract_read_file("/sys/fs/cgroup/remove-empty/cgroup.procs", buf, sizeof(buf)) == 0 ||
        errno != ENOENT) {
        errno = ENODATA;
        goto out;
    }
    ret = 0;

out:
    {
        int saved_errno = errno;
        cgroup_contract_restore_root();
        errno = saved_errno;
    }
    return ret;
}

int cgroup_contract_rmdir_busy_cgroup_fails(void) {
    int ret = -1;

    if (cgroup_contract_restore_root() != 0) {
        return -1;
    }
    if (cgroup_contract_ignore_exists(mkdir_impl("/sys/fs/cgroup/remove-busy", 0755)) != 0 ||
        cgroup_contract_write_current_pid("/sys/fs/cgroup/remove-busy/cgroup.procs") != 0) {
        goto out;
    }
    if (rmdir_impl("/sys/fs/cgroup/remove-busy") == 0 || errno != EBUSY) {
        errno = ENODATA;
        goto out;
    }
    ret = 0;

out:
    {
        int saved_errno = errno;
        cgroup_contract_restore_root();
        errno = saved_errno;
    }
    return ret;
}

int cgroup_contract_rmdir_parent_with_child_fails_notempty(void) {
    int ret = -1;

    if (cgroup_contract_restore_root() != 0) {
        return -1;
    }
    if (cgroup_contract_ignore_exists(mkdir_impl("/sys/fs/cgroup/remove-parent", 0755)) != 0 ||
        cgroup_contract_ignore_exists(mkdir_impl("/sys/fs/cgroup/remove-parent/child", 0755)) != 0) {
        goto out;
    }
    if (rmdir_impl("/sys/fs/cgroup/remove-parent") == 0 || errno != ENOTEMPTY) {
        errno = ENODATA;
        goto out;
    }
    ret = 0;

out:
    {
        int saved_errno = errno;
        cgroup_contract_restore_root();
        errno = saved_errno;
    }
    return ret;
}

int cgroup_contract_newuser_caps_are_scoped_to_cgroup_namespace_owner(void) {
    struct __user_cap_header_struct header = {
        .version = _LINUX_CAPABILITY_VERSION_3,
        .pid = 0,
    };
    struct __user_cap_data_struct data[_LINUX_CAPABILITY_U32S_3];
    struct task *parent = task_current();
    struct task *user_child = NULL;
    struct task *user_cgroup_child = NULL;
    int pid;
    int ret = -1;

    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    if (cgroup_contract_restore_root() != 0 || capget_impl(&header, data) != 0) {
        return -1;
    }
    data[CAP_SYS_ADMIN / 32].effective &= ~(1U << (CAP_SYS_ADMIN % 32));
    if (capset_impl(&header, data) != 0) {
        return -1;
    }
    errno = 0;
    if (cgroup_contract_write_file("/sys/fs/cgroup/pids.max", "16\n") != -1 ||
        errno != EPERM) {
        errno = EPROTO;
        goto out;
    }

    pid = clone_impl(CLONE_NEWUSER);
    if (pid < 0) {
        goto out;
    }
    user_child = task_lookup(pid);
    if (!user_child) {
        errno = ESRCH;
        goto out;
    }
    task_set_current(user_child);
    errno = 0;
    if (cgroup_contract_write_file("/sys/fs/cgroup/pids.max", "16\n") != -1 ||
        errno != EPERM) {
        task_set_current(parent);
        errno = ENODATA;
        goto out;
    }
    task_set_current(parent);

    pid = clone_impl(CLONE_NEWUSER | CLONE_NEWCGROUP);
    if (pid < 0) {
        goto out;
    }
    user_cgroup_child = task_lookup(pid);
    if (!user_cgroup_child) {
        errno = ESRCH;
        goto out;
    }
    task_set_current(user_cgroup_child);
    if (cgroup_contract_write_file("/sys/fs/cgroup/pids.max", "16\n") != 0 ||
        cgroup_contract_write_file("/sys/fs/cgroup/cgroup.freeze", "1\n") != 0 ||
        cgroup_contract_write_file("/sys/fs/cgroup/cgroup.freeze", "0\n") != 0) {
        task_set_current(parent);
        goto out;
    }
    task_set_current(parent);
    ret = 0;

out:
    {
        int saved_errno = errno;
        task_set_current(parent);
        if (user_child) {
            task_unlink_child_impl(parent, user_child);
            task_put(user_child);
        }
        if (user_cgroup_child) {
            task_unlink_child_impl(parent, user_cgroup_child);
            task_put(user_cgroup_child);
        }
        cgroup_contract_restore_root();
        errno = saved_errno;
    }
    return ret;
}

int cgroup_contract_cgroup_namespace_rejects_migration_of_hidden_task(void) {
    struct task *parent = task_current();
    struct task *child = NULL;
    char pidbuf[32];
    int ret = -1;

    if (!parent || cgroup_contract_restore_root() != 0) {
        return -1;
    }
    if (cgroup_contract_ignore_exists(mkdir_impl("/sys/fs/cgroup/visible-root", 0755)) != 0 ||
        cgroup_contract_ignore_exists(mkdir_impl("/sys/fs/cgroup/visible-root/target", 0755)) != 0 ||
        cgroup_contract_ignore_exists(mkdir_impl("/sys/fs/cgroup/hidden-root", 0755)) != 0) {
        goto out;
    }
    child = task_create_child_impl(parent);
    if (!child) {
        goto out;
    }
    cgroup_contract_format_pid(child->pid, pidbuf, sizeof(pidbuf));
    if (cgroup_contract_write_file("/sys/fs/cgroup/hidden-root/cgroup.procs", pidbuf) != 0 ||
        cgroup_contract_write_current_pid("/sys/fs/cgroup/visible-root/cgroup.procs") != 0 ||
        unshare_impl(CLONE_NEWCGROUP) != 0) {
        goto out;
    }
    errno = 0;
    if (cgroup_contract_write_file("/sys/fs/cgroup/target/cgroup.procs", pidbuf) == 0 ||
        errno != EPERM) {
        errno = EPROTO;
        goto out;
    }
    ret = 0;

out:
    {
        int saved_errno = errno;
        if (child) {
            task_unlink_child_impl(parent, child);
            task_put(child);
        }
        task_set_current(parent);
        cgroup_contract_restore_root();
        errno = saved_errno;
    }
    return ret;
}

int cgroup_contract_proc_pid_cgroup_hides_tasks_outside_reader_namespace(void) {
    struct task *parent = task_current();
    struct task *child = NULL;
    char pidbuf[32];
    char path[64];
    char buf[128];
    int ret = -1;

    if (!parent || cgroup_contract_restore_root() != 0) {
        return -1;
    }
    if (cgroup_contract_ignore_exists(mkdir_impl("/sys/fs/cgroup/reader-root", 0755)) != 0 ||
        cgroup_contract_ignore_exists(mkdir_impl("/sys/fs/cgroup/outside-reader", 0755)) != 0) {
        goto out;
    }
    child = task_create_child_impl(parent);
    if (!child) {
        goto out;
    }
    cgroup_contract_format_pid(child->pid, pidbuf, sizeof(pidbuf));
    if (cgroup_contract_write_file("/sys/fs/cgroup/outside-reader/cgroup.procs", pidbuf) != 0 ||
        cgroup_contract_write_current_pid("/sys/fs/cgroup/reader-root/cgroup.procs") != 0 ||
        unshare_impl(CLONE_NEWCGROUP) != 0) {
        goto out;
    }
    memcpy(path, "/proc/", 6);
    cgroup_contract_format_pid(child->pid, path + 6, sizeof(path) - 6);
    for (char *p = path; *p; p++) {
        if (*p == '\n') {
            memcpy(p, "/cgroup", 8);
            break;
        }
    }
    errno = 0;
    if (cgroup_contract_read_file(path, buf, sizeof(buf)) == 0 || errno != EACCES) {
        errno = EPROTO;
        goto out;
    }
    ret = 0;

out:
    {
        int saved_errno = errno;
        if (child) {
            task_unlink_child_impl(parent, child);
            task_put(child);
        }
        task_set_current(parent);
        cgroup_contract_restore_root();
        errno = saved_errno;
    }
    return ret;
}

int cgroup_contract_nested_cgroup_namespace_rebases_visibility(void) {
    char buf[128];
    int ret = -1;

    if (cgroup_contract_restore_root() != 0) {
        return -1;
    }
    if (cgroup_contract_ignore_exists(mkdir_impl("/sys/fs/cgroup/nested-outer", 0755)) != 0 ||
        cgroup_contract_ignore_exists(mkdir_impl("/sys/fs/cgroup/nested-outer/nested-inner", 0755)) != 0 ||
        cgroup_contract_write_current_pid("/sys/fs/cgroup/nested-outer/cgroup.procs") != 0 ||
        unshare_impl(CLONE_NEWCGROUP) != 0) {
        goto out;
    }
    if (cgroup_contract_read_file("/proc/self/cgroup", buf, sizeof(buf)) != 0 ||
        strcmp(buf, "0::/\n") != 0) {
        errno = ENODATA;
        goto out;
    }
    if (cgroup_contract_write_current_pid("/sys/fs/cgroup/nested-inner/cgroup.procs") != 0 ||
        unshare_impl(CLONE_NEWCGROUP) != 0) {
        goto out;
    }
    if (cgroup_contract_read_file("/proc/self/cgroup", buf, sizeof(buf)) != 0 ||
        strcmp(buf, "0::/\n") != 0) {
        errno = ENOMSG;
        goto out;
    }
    errno = 0;
    if (cgroup_contract_read_file("/sys/fs/cgroup/nested-inner/cgroup.procs", buf, sizeof(buf)) == 0 ||
        errno != ENOENT) {
        errno = EPROTO;
        goto out;
    }
    ret = 0;

out:
    {
        int saved_errno = errno;
        cgroup_contract_restore_root();
        errno = saved_errno;
    }
    return ret;
}

int cgroup_contract_reaped_task_releases_cgroup_membership(void) {
    struct task *parent = task_current();
    struct task *child;
    char pidbuf[32];
    char buf[128];
    int status = 0;
    int ret = -1;

    if (!parent || cgroup_contract_restore_root() != 0) {
        return -1;
    }
    if (cgroup_contract_ignore_exists(mkdir_impl("/sys/fs/cgroup/lifecycle", 0755)) != 0) {
        return -1;
    }
    child = task_create_child_impl(parent);
    if (!child) {
        return -1;
    }
    cgroup_contract_format_pid(child->pid, pidbuf, sizeof(pidbuf));
    if (cgroup_contract_write_file("/sys/fs/cgroup/lifecycle/cgroup.procs", pidbuf) != 0 ||
        cgroup_contract_read_file("/sys/fs/cgroup/lifecycle/pids.current", buf, sizeof(buf)) != 0 ||
        strcmp(buf, "1\n") != 0) {
        goto out;
    }
    task_set_current(child);
    exit_impl(7);
    task_set_current(parent);
    if (waitpid_impl(child->pid, &status, 0) != child->pid) {
        goto out;
    }
    child = NULL;
    if (cgroup_contract_read_file("/sys/fs/cgroup/lifecycle/pids.current", buf, sizeof(buf)) != 0 ||
        strcmp(buf, "0\n") != 0 ||
        cgroup_contract_read_file("/sys/fs/cgroup/lifecycle/cgroup.procs", buf, sizeof(buf)) != 0 ||
        buf[0] != '\0') {
        errno = ENODATA;
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
        cgroup_contract_clear_pending_signal(parent, SIGCHLD);
        cgroup_contract_restore_root();
        errno = saved_errno;
    }
    return ret;
}

int cgroup_contract_clone3_into_cgroup_moves_child(void) {
    struct task *parent = task_current();
    struct task *child = NULL;
    struct clone_args args;
    const char *group_path = "/sys/fs/cgroup/clone3-workers";
    int cgroup_fd = -1;
    long ret;
    int result = -1;

    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    if (cgroup_contract_restore_root() != 0) {
        return -1;
    }
    if (cgroup_contract_ignore_exists(mkdir_impl(group_path, 0755)) != 0) {
        goto out;
    }
    cgroup_fd = open_impl(group_path, O_RDONLY | O_DIRECTORY, 0);
    if (cgroup_fd < 0) {
        goto out;
    }

    memset(&args, 0, sizeof(args));
    args.flags = CLONE_INTO_CGROUP;
    args.cgroup = (uint64_t)cgroup_fd;
    ret = syscall_dispatch_impl(__NR_clone3, (long)(uintptr_t)&args, sizeof(args), 0, 0, 0, 0);
    if (ret < 0) {
        errno = (int)-ret;
        goto out;
    }
    child = task_lookup((int32_t)ret);
    if (!child) {
        errno = ESRCH;
        goto out;
    }
    if (strcmp(task_cgroup_path(child), "/clone3-workers") != 0) {
        errno = ENODATA;
        goto out;
    }
    result = 0;

out:
    {
        int saved_errno = errno;
        if (child) {
            task_unlink_child_impl(parent, child);
            task_put(child);
            task_put(child);
        }
        if (cgroup_fd >= 0) {
            close_impl(cgroup_fd);
        }
        rmdir_impl(group_path);
        errno = saved_errno;
    }
    return result;
}
