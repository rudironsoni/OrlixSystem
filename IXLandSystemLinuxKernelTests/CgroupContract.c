#include "CgroupContract.h"

#include <linux/fcntl.h>
#include <linux/sched.h>

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "kernel/cgroup.h"
#include "kernel/task.h"

extern int open_impl(const char *pathname, int flags, unsigned int mode);
extern long read_impl(int fd, void *buf, unsigned long count);
extern long write_impl(int fd, const void *buf, unsigned long count);
extern int close_impl(int fd);
extern int mkdir_impl(const char *pathname, unsigned int mode);
extern int unshare_impl(uint64_t flags);

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
    struct task_struct *task = get_current();
    char pidbuf[32];

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    cgroup_contract_format_pid(task->pid, pidbuf, sizeof(pidbuf));
    return cgroup_contract_write_file(path, pidbuf);
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
    struct task_struct *task = get_current();

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
    struct task_struct *task = get_current();

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
    struct task_struct *parent = get_current();
    struct task_struct *child;
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
        free_task(child);
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
    struct task_struct *parent = get_current();
    struct task_struct *child;
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
        free_task(child);
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
