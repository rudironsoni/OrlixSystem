#include <linux/fcntl.h>
#include <linux/sched.h>
#include <linux/stat.h>

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "IXLandSystemLinuxKernelTests/ProcfsNamespaceContract.h"
#include "fs/vfs.h"
#include "kernel/cred_internal.h"
#include "kernel/task.h"
#include "kernel/uts.h"

extern int clone_impl(uint64_t flags);
extern int unshare_impl(uint64_t flags);
extern int open_impl(const char *pathname, int flags, linux_mode_t mode);
extern int close_impl(int fd);
extern long read_impl(int fd, void *buf, size_t count);
extern long readlink_impl(const char *pathname, char *buf, size_t bufsiz);
extern void cred_reset_to_defaults(void);

static void reset_procfs_namespace_state(void) {
    cred_reset_to_defaults();
    uts_reset_current_namespace();
    if (get_current()) {
        atomic_store(&get_current()->new_pid_namespace_pending, false);
    }
}

static bool contains(const char *haystack, const char *needle) {
    return strstr(haystack, needle) != NULL;
}

static int read_file_content(const char *path, char *buf, size_t buf_len) {
    int fd;
    long nread;

    if (!buf || buf_len == 0) {
        errno = EINVAL;
        return -1;
    }

    fd = open_impl(path, O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }
    memset(buf, 0, buf_len);
    nread = read_impl(fd, buf, buf_len - 1);
    close_impl(fd);
    if (nread < 0) {
        return -1;
    }
    buf[nread] = '\0';
    return 0;
}

static int read_link_content(const char *path, char *buf, size_t buf_len) {
    long nread;

    if (!buf || buf_len == 0) {
        errno = EINVAL;
        return -1;
    }

    memset(buf, 0, buf_len);
    nread = readlink_impl(path, buf, buf_len - 1);
    if (nread < 0) {
        return -1;
    }
    buf[nread] = '\0';
    return 0;
}

static void append_positive_decimal(char *buf, size_t buf_len, int value) {
    char tmp[16];
    size_t pos = 0;
    size_t out = strlen(buf);

    if (value == 0) {
        if (out + 1 < buf_len) {
            buf[out] = '0';
            buf[out + 1] = '\0';
        }
        return;
    }

    while (value > 0 && pos < sizeof(tmp)) {
        tmp[pos++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (pos > 0 && out + 1 < buf_len) {
        buf[out++] = tmp[--pos];
        buf[out] = '\0';
    }
}

static void proc_pid_status_path(char *buf, size_t buf_len, int pid) {
    memcpy(buf, "/proc/", 7);
    append_positive_decimal(buf, buf_len, pid);
    if (strlen(buf) + 8 < buf_len) {
        memcpy(buf + strlen(buf), "/status", 8);
    }
}

static void release_lookup_child(struct task_struct *parent, struct task_struct *child) {
    if (!child) {
        return;
    }
    task_unlink_child_impl(parent, child);
    free_task(child);
    free_task(child);
}

int procfs_namespace_contract_ns_directory_opens(void) {
    int fd;

    reset_procfs_namespace_state();
    fd = open_impl("/proc/self/ns", O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0) {
        return -1;
    }
    return close_impl(fd);
}

int procfs_namespace_contract_ns_links_are_linux_shaped(void) {
    char mnt[64];
    char uts[64];
    char pid[64];

    reset_procfs_namespace_state();

    if (read_link_content("/proc/self/ns/mnt", mnt, sizeof(mnt)) != 0 ||
        read_link_content("/proc/self/ns/uts", uts, sizeof(uts)) != 0 ||
        read_link_content("/proc/self/ns/pid", pid, sizeof(pid)) != 0) {
        return -1;
    }

    if (!contains(mnt, "mnt:[") || !contains(uts, "uts:[") || !contains(pid, "pid:[")) {
        errno = ENODATA;
        return -1;
    }
    if (contains(mnt, "/") || contains(uts, "/") || contains(pid, "/")) {
        errno = EXDEV;
        return -1;
    }
    return 0;
}

int procfs_namespace_contract_unshare_newuts_changes_uts_link(void) {
    char before[64];
    char after[64];

    reset_procfs_namespace_state();

    if (read_link_content("/proc/self/ns/uts", before, sizeof(before)) != 0) {
        return -1;
    }
    if (unshare_impl(CLONE_NEWUTS) != 0) {
        return -1;
    }
    if (read_link_content("/proc/self/ns/uts", after, sizeof(after)) != 0) {
        return -1;
    }
    if (strcmp(before, after) == 0) {
        errno = ENODATA;
        return -1;
    }
    return 0;
}

int procfs_namespace_contract_unshare_newns_changes_mnt_link(void) {
    char before[64];
    char after[64];

    reset_procfs_namespace_state();

    if (read_link_content("/proc/self/ns/mnt", before, sizeof(before)) != 0) {
        return -1;
    }
    if (unshare_impl(CLONE_NEWNS) != 0) {
        return -1;
    }
    if (read_link_content("/proc/self/ns/mnt", after, sizeof(after)) != 0) {
        return -1;
    }
    if (strcmp(before, after) == 0) {
        errno = ENODATA;
        return -1;
    }
    return 0;
}

int procfs_namespace_contract_proc_pid_status_aliases_current_task(void) {
    struct task_struct *task;
    char path[64] = {0};
    char content[512];
    char expected[32] = "Pid:\t";

    reset_procfs_namespace_state();

    task = get_current();
    if (!task) {
        errno = ESRCH;
        return -1;
    }
    proc_pid_status_path(path, sizeof(path), task->pid);
    append_positive_decimal(expected, sizeof(expected), task->pid);
    if (read_file_content(path, content, sizeof(content)) != 0) {
        return -1;
    }
    if (!contains(content, expected) || !contains(content, "NSpid:\t")) {
        errno = ENODATA;
        return -1;
    }
    return 0;
}

int procfs_namespace_contract_pid_namespace_status_reports_nspid(void) {
    struct task_struct *parent;
    struct task_struct *child;
    struct task_struct *saved;
    char content[512];
    int pid;
    int ret = -1;

    reset_procfs_namespace_state();

    parent = get_current();
    if (!parent) {
        errno = ESRCH;
        return -1;
    }

    pid = clone_impl(CLONE_NEWPID);
    if (pid < 0) {
        return -1;
    }
    child = task_lookup(pid);
    if (!child) {
        errno = ESRCH;
        return -1;
    }

    saved = get_current();
    set_current(child);
    if (read_file_content("/proc/self/status", content, sizeof(content)) == 0 &&
        contains(content, "NSpid:\t1\n")) {
        ret = 0;
    } else {
        errno = ENODATA;
    }
    set_current(saved);
    release_lookup_child(parent, child);
    reset_procfs_namespace_state();
    return ret;
}
