#include <linux/fcntl.h>
#include <linux/mman.h>
#include <linux/mount.h>
#include <linux/sched.h>
#include <linux/stat.h>
#define __ASSEMBLY__ 1
#include <asm-generic/signal.h>
#undef __ASSEMBLY__

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "IXLandSystemLinuxKernelTests/ProcfsNamespaceContract.h"
#include "fs/vfs.h"
#include "kernel/cred_internal.h"
#include "kernel/mm.h"
#include "kernel/signal.h"
#include "kernel/task.h"
#include "kernel/uts.h"

extern int clone_impl(uint64_t flags);
extern int unshare_impl(uint64_t flags);
extern int fcntl_impl(int fd, int cmd, ...);
extern int mkdir_impl(const char *pathname, linux_mode_t mode);
extern int mount(const char *source, const char *target, const char *filesystemtype,
                 unsigned long mountflags, const void *data);
extern int umount(const char *target);
extern int open_impl(const char *pathname, int flags, linux_mode_t mode);
extern int close_impl(int fd);
extern long read_impl(int fd, void *buf, size_t count);
extern long readlink_impl(const char *pathname, char *buf, size_t bufsiz);
extern ssize_t getdents64(int fd, void *dirp, size_t count);
extern void cred_reset_to_defaults(void);
extern void set_current_cred(struct cred *cred);

struct linux_dirent64 {
    uint64_t d_ino;
    int64_t d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[];
};

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

static bool contains_bytes(const char *haystack, size_t haystack_len, const char *needle) {
    size_t needle_len;

    if (!haystack || !needle) {
        return false;
    }
    needle_len = strlen(needle);
    if (needle_len == 0 || needle_len > haystack_len) {
        return false;
    }
    for (size_t i = 0; i + needle_len <= haystack_len; i++) {
        if (memcmp(haystack + i, needle, needle_len) == 0) {
            return true;
        }
    }
    return false;
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

static long read_file_bytes(const char *path, char *buf, size_t buf_len) {
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
    nread = read_impl(fd, buf, buf_len);
    close_impl(fd);
    return nread;
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

static void proc_pid_file_path(char *buf, size_t buf_len, int pid, const char *leaf) {
    size_t len;

    memcpy(buf, "/proc/", 7);
    append_positive_decimal(buf, buf_len, pid);
    len = strlen(buf);
    if (leaf && len + strlen(leaf) + 1 < buf_len) {
        memcpy(buf + len, leaf, strlen(leaf) + 1);
    }
}

static bool procfs_dirents_contain_name(char *buf, long nread, const char *name) {
    long offset = 0;

    while (offset < nread) {
        struct linux_dirent64 *entry = (struct linux_dirent64 *)(buf + offset);
        if (entry->d_reclen == 0) {
            return false;
        }
        if (strcmp(entry->d_name, name) == 0) {
            return true;
        }
        offset += entry->d_reclen;
    }
    return false;
}

static int procfs_read_fdinfo_flags_for_pid(int pid, int fd_num, unsigned int *flags_out) {
    char path[96] = {0};
    char content[512];
    char *flags_line;
    unsigned int flags = 0;

    if (!flags_out) {
        errno = EINVAL;
        return -1;
    }

    proc_pid_file_path(path, sizeof(path), pid, "/fdinfo/");
    append_positive_decimal(path, sizeof(path), fd_num);
    if (read_file_content(path, content, sizeof(content)) != 0) {
        return -1;
    }

    flags_line = strstr(content, "flags:\t0");
    if (!flags_line) {
        errno = ENODATA;
        return -1;
    }

    for (flags_line += 8; *flags_line >= '0' && *flags_line <= '7'; flags_line++) {
        flags = (flags << 3) | (unsigned int)(*flags_line - '0');
    }
    *flags_out = flags;
    return 0;
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

int procfs_namespace_contract_proc_pid_status_reports_target_credentials(void) {
    struct task_struct *parent;
    struct task_struct *child = NULL;
    struct task_struct *saved;
    struct cred *parent_cred = NULL;
    struct cred *child_cred = NULL;
    char path[64] = {0};
    char content[512];
    int ret = -1;

    reset_procfs_namespace_state();

    parent = get_current();
    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    child = task_create_child_impl(parent);
    if (!child) {
        return -1;
    }

    parent_cred = dup_cred(get_current_cred());
    child_cred = dup_cred(get_current_cred());
    if (!parent_cred || !child_cred) {
        errno = ENOMEM;
        goto out;
    }
    if (cred_setuid(child_cred, 1000) != 0) {
        errno = EPERM;
        goto out;
    }

    saved = get_current();
    set_current(child);
    set_current_cred(child_cred);
    put_cred(child_cred);
    child_cred = NULL;
    set_current(parent);
    set_current_cred(parent_cred);
    put_cred(parent_cred);
    parent_cred = NULL;

    proc_pid_status_path(path, sizeof(path), child->pid);
    if (read_file_content(path, content, sizeof(content)) != 0) {
        set_current(saved);
        goto out;
    }
    set_current(saved);

    if (!contains(content, "Uid:\t1000\t1000\t1000\t1000\n")) {
        errno = ENODATA;
        goto out;
    }

    ret = 0;

out:
    if (parent_cred) {
        put_cred(parent_cred);
    }
    if (child_cred) {
        put_cred(child_cred);
    }
    if (child) {
        task_unlink_child_impl(parent, child);
        free_task(child);
    }
    reset_procfs_namespace_state();
    return ret;
}

int procfs_namespace_contract_proc_status_reports_groups_and_capabilities(void) {
    gid_t groups[2] = {3000, 3001};
    char content[2048];

    reset_procfs_namespace_state();

    if (setgroups_impl(2, groups) != 0) {
        return -1;
    }

    if (read_file_content("/proc/self/status", content, sizeof(content)) != 0) {
        return -1;
    }

    if (!contains(content, "Groups:\t3000 3001\n") ||
        !contains(content, "CapInh:\t") ||
        !contains(content, "CapPrm:\t") ||
        !contains(content, "CapEff:\t") ||
        !contains(content, "CapBnd:\t") ||
        !contains(content, "CapAmb:\t") ||
        !contains(content, "NoNewPrivs:\t0\n")) {
        errno = ENODATA;
        return -1;
    }

    return 0;
}

int procfs_namespace_contract_proc_pid_stat_cwd_and_exe_report_target_task(void) {
    struct task_struct *parent;
    struct task_struct *child = NULL;
    char path[96] = {0};
    char content[1024];
    char link_target[MAX_PATH];
    int ret = -1;

    reset_procfs_namespace_state();

    parent = get_current();
    if (!parent) {
        errno = ESRCH;
        return -1;
    }

    child = task_create_child_impl(parent);
    if (!child) {
        return -1;
    }

    memset(child->comm, 0, sizeof(child->comm));
    memcpy(child->comm, "pid-child", 10);
    memset(child->exe, 0, sizeof(child->exe));
    memcpy(child->exe, "/tmp/proc-pid-child-exe", 24);
    if (fs_set_pwd(child->fs, "/tmp") != 0) {
        errno = EINVAL;
        goto out;
    }

    proc_pid_file_path(path, sizeof(path), child->pid, "/stat");
    if (read_file_content(path, content, sizeof(content)) != 0 ||
        !contains(content, "(pid-child)")) {
        errno = ENODATA;
        goto out;
    }

    memset(path, 0, sizeof(path));
    proc_pid_file_path(path, sizeof(path), child->pid, "/cwd");
    if (read_link_content(path, link_target, sizeof(link_target)) != 0 ||
        strcmp(link_target, "/tmp") != 0) {
        errno = ENODATA;
        goto out;
    }

    memset(path, 0, sizeof(path));
    proc_pid_file_path(path, sizeof(path), child->pid, "/exe");
    if (read_link_content(path, link_target, sizeof(link_target)) != 0 ||
        strcmp(link_target, "/tmp/proc-pid-child-exe") != 0) {
        errno = ENODATA;
        goto out;
    }

    ret = 0;

out:
    if (child) {
        task_unlink_child_impl(parent, child);
        free_task(child);
    }
    reset_procfs_namespace_state();
    return ret;
}

int procfs_namespace_contract_proc_pid_fd_and_fdinfo_paths_are_target_aware(void) {
    struct task_struct *parent;
    struct task_struct *child = NULL;
    int fd = -1;
    char path[96] = {0};
    char link_target[MAX_PATH];
    char content[512];
    long nread;
    int info_fd = -1;
    int ret = -1;

    reset_procfs_namespace_state();

    parent = get_current();
    if (!parent) {
        errno = ESRCH;
        return -1;
    }

    fd = open_impl("/dev/null", O_RDONLY | O_CLOEXEC, 0);
    if (fd < 0) {
        return -1;
    }

    child = task_create_child_impl(parent);
    if (!child) {
        goto out;
    }

    proc_pid_file_path(path, sizeof(path), child->pid, "/fd/");
    append_positive_decimal(path, sizeof(path), fd);
    if (read_link_content(path, link_target, sizeof(link_target)) != 0 ||
        strcmp(link_target, "/dev/null") != 0) {
        errno = ENODATA;
        goto out;
    }

    memset(path, 0, sizeof(path));
    proc_pid_file_path(path, sizeof(path), child->pid, "/fdinfo/");
    append_positive_decimal(path, sizeof(path), fd);
    info_fd = open_impl(path, O_RDONLY, 0);
    if (info_fd < 0) {
        goto out;
    }
    memset(content, 0, sizeof(content));
    nread = read_impl(info_fd, content, sizeof(content) - 1);
    if (nread <= 0) {
        goto out;
    }
    content[nread] = '\0';
    if (!contains(content, "flags:\t")) {
        errno = ENODATA;
        goto out;
    }

    ret = 0;

out:
    if (info_fd >= 0) {
        close_impl(info_fd);
    }
    if (fd >= 0) {
        close_impl(fd);
    }
    if (child) {
        task_unlink_child_impl(parent, child);
        free_task(child);
    }
    reset_procfs_namespace_state();
    return ret;
}

int procfs_namespace_contract_proc_pid_fd_dir_lists_target_inherited_fds_after_parent_close(void) {
    struct task_struct *parent;
    struct task_struct *child = NULL;
    int inherited_fd = -1;
    int inherited_fd_num = -1;
    int dir_fd = -1;
    char path[96] = {0};
    char fd_name[16] = {0};
    char dirents[1024];
    long nread;
    int ret = -1;

    reset_procfs_namespace_state();

    parent = get_current();
    if (!parent) {
        errno = ESRCH;
        return -1;
    }

    inherited_fd = open_impl("/dev/null", O_RDONLY | O_CLOEXEC, 0);
    if (inherited_fd < 0) {
        return -1;
    }
    inherited_fd_num = inherited_fd;

    child = task_create_child_impl(parent);
    if (!child) {
        goto out;
    }

    if (close_impl(inherited_fd) != 0) {
        goto out;
    }
    inherited_fd = -1;

    proc_pid_file_path(path, sizeof(path), child->pid, "/fd/");
    append_positive_decimal(path, sizeof(path), inherited_fd_num);
    if (read_link_content(path, dirents, sizeof(dirents)) != 0 ||
        strcmp(dirents, "/dev/null") != 0) {
        errno = ENODATA;
        goto out;
    }

    memset(path, 0, sizeof(path));
    proc_pid_file_path(path, sizeof(path), child->pid, "/fd");
    dir_fd = open_impl(path, O_RDONLY | O_DIRECTORY, 0);
    if (dir_fd < 0) {
        goto out;
    }
    memset(dirents, 0, sizeof(dirents));
    nread = getdents64(dir_fd, dirents, sizeof(dirents));
    if (nread <= 0) {
        goto out;
    }
    append_positive_decimal(fd_name, sizeof(fd_name), inherited_fd_num);
    if (!procfs_dirents_contain_name(dirents, nread, fd_name)) {
        errno = ENODATA;
        goto out;
    }

    ret = 0;

out:
    if (dir_fd >= 0) {
        close_impl(dir_fd);
    }
    if (inherited_fd >= 0) {
        close_impl(inherited_fd);
    }
    if (child) {
        task_unlink_child_impl(parent, child);
        free_task(child);
    }
    reset_procfs_namespace_state();
    return ret;
}

int procfs_namespace_contract_fdinfo_flags_are_per_task_descriptor_state(void) {
    struct task_struct *parent;
    struct task_struct *child = NULL;
    struct task_struct *saved;
    int fd = -1;
    unsigned int parent_flags = 0;
    unsigned int child_flags = 0;
    int ret = -1;

    reset_procfs_namespace_state();

    parent = get_current();
    if (!parent) {
        errno = ESRCH;
        return -1;
    }

    fd = open_impl("/dev/null", O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }

    child = task_create_child_impl(parent);
    if (!child) {
        goto out;
    }

    saved = get_current();
    set_current(child);
    if (fcntl_impl(fd, F_SETFD, FD_CLOEXEC) != 0) {
        set_current(saved);
        goto out;
    }
    if (procfs_read_fdinfo_flags_for_pid(child->pid, fd, &child_flags) != 0 ||
        procfs_read_fdinfo_flags_for_pid(parent->pid, fd, &parent_flags) != 0) {
        set_current(saved);
        goto out;
    }
    set_current(saved);

    if ((child_flags & O_CLOEXEC) == 0 || (parent_flags & O_CLOEXEC) != 0) {
        errno = ENODATA;
        goto out;
    }

    ret = 0;

out:
    set_current(parent);
    if (fd >= 0) {
        close_impl(fd);
    }
    if (child) {
        task_unlink_child_impl(parent, child);
        free_task(child);
    }
    reset_procfs_namespace_state();
    return ret;
}

int procfs_namespace_contract_proc_pid_cmdline_environ_and_comm_report_target_task(void) {
    struct task_struct *parent;
    struct task_struct *child = NULL;
    struct task_struct *saved;
    char *argv[] = {"target-prog", "target-arg", NULL};
    char *envp[] = {"TARGET_ENV=1", NULL};
    char path[96] = {0};
    char content[512];
    long nread;
    int ret = -1;

    reset_procfs_namespace_state();

    parent = get_current();
    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    child = task_create_child_impl(parent);
    if (!child) {
        return -1;
    }

    saved = get_current();
    set_current(child);
    memset(child->comm, 0, sizeof(child->comm));
    memcpy(child->comm, "target-comm", 12);
    if (task_record_exec_strings_impl(argv, envp) != 0) {
        set_current(saved);
        goto out;
    }
    set_current(saved);

    proc_pid_file_path(path, sizeof(path), child->pid, "/cmdline");
    nread = read_file_bytes(path, content, sizeof(content));
    if (nread <= 0 ||
        !contains_bytes(content, (size_t)nread, "target-prog") ||
        !contains_bytes(content, (size_t)nread, "target-arg")) {
        errno = ENODATA;
        goto out;
    }

    memset(path, 0, sizeof(path));
    proc_pid_file_path(path, sizeof(path), child->pid, "/environ");
    nread = read_file_bytes(path, content, sizeof(content));
    if (nread <= 0 ||
        !contains_bytes(content, (size_t)nread, "TARGET_ENV=1")) {
        errno = ENODATA;
        goto out;
    }

    memset(path, 0, sizeof(path));
    proc_pid_file_path(path, sizeof(path), child->pid, "/comm");
    if (read_file_content(path, content, sizeof(content)) != 0 ||
        !contains(content, "target-comm\n")) {
        errno = ENODATA;
        goto out;
    }

    ret = 0;

out:
    if (child) {
        task_unlink_child_impl(parent, child);
        free_task(child);
    }
    reset_procfs_namespace_state();
    return ret;
}

int procfs_namespace_contract_proc_pid_stat_status_and_maps_report_target_task(void) {
    struct task_struct *parent;
    struct task_struct *child = NULL;
    struct task_struct *saved;
    void *mapping = (void *)-1;
    char path[96] = {0};
    char content[1024];
    int ret = -1;

    reset_procfs_namespace_state();

    parent = get_current();
    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    child = task_create_child_impl(parent);
    if (!child) {
        return -1;
    }

    saved = get_current();
    set_current(child);
    memset(child->comm, 0, sizeof(child->comm));
    memcpy(child->comm, "proc-target", 12);
    task_mark_stopped_by_signal(child, SIGSTOP);
    mapping = mmap_impl(NULL, TASK_VMA_PAGE_SIZE, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    set_current(saved);
    if (mapping == (void *)-1) {
        goto out;
    }

    proc_pid_file_path(path, sizeof(path), child->pid, "/stat");
    if (read_file_content(path, content, sizeof(content)) != 0 ||
        !contains(content, "(proc-target)") ||
        !contains(content, " T ")) {
        errno = ENODATA;
        goto out;
    }

    memset(path, 0, sizeof(path));
    proc_pid_file_path(path, sizeof(path), child->pid, "/status");
    if (read_file_content(path, content, sizeof(content)) != 0 ||
        !contains(content, "Name:\tproc-target\n") ||
        !contains(content, "State:\tT ")) {
        errno = ENODATA;
        goto out;
    }

    memset(path, 0, sizeof(path));
    proc_pid_file_path(path, sizeof(path), child->pid, "/maps");
    if (read_file_content(path, content, sizeof(content)) != 0 ||
        !contains(content, "rw-p") ||
        !contains(content, "[anon]")) {
        errno = ENODATA;
        goto out;
    }

    ret = 0;

out:
    if (child) {
        saved = get_current();
        set_current(child);
        if (mapping != (void *)-1) {
            munmap_impl(mapping, TASK_VMA_PAGE_SIZE);
        }
        set_current(saved);
        task_unlink_child_impl(parent, child);
        free_task(child);
    }
    reset_procfs_namespace_state();
    return ret;
}

int procfs_namespace_contract_proc_pid_mountinfo_uses_target_mount_namespace(void) {
    struct task_struct *parent;
    struct task_struct *child = NULL;
    struct task_struct *saved;
    char path[96] = {0};
    char content[4096];
    int ret = -1;

    reset_procfs_namespace_state();

    parent = get_current();
    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    child = task_create_child_impl(parent);
    if (!child) {
        return -1;
    }
    if (fs_unshare_mount_namespace(child->fs) != 0) {
        goto out;
    }
    if (mkdir_impl("/tmp/proc-pid-mnt-source", 0700) != 0 && errno != EEXIST) {
        goto out;
    }
    if (mkdir_impl("/tmp/proc-pid-mnt-target", 0700) != 0 && errno != EEXIST) {
        goto out;
    }

    saved = get_current();
    set_current(child);
    if (mount("/tmp/proc-pid-mnt-source", "/tmp/proc-pid-mnt-target", NULL, MS_BIND, NULL) != 0) {
        set_current(saved);
        goto out;
    }
    set_current(saved);

    proc_pid_file_path(path, sizeof(path), child->pid, "/mountinfo");
    if (read_file_content(path, content, sizeof(content)) != 0 ||
        !contains(content, "/tmp/proc-pid-mnt-source") ||
        !contains(content, "/tmp/proc-pid-mnt-target")) {
        errno = ENODATA;
        goto out;
    }

    if (read_file_content("/proc/self/mountinfo", content, sizeof(content)) != 0 ||
        contains(content, "/tmp/proc-pid-mnt-source")) {
        errno = ENODATA;
        goto out;
    }

    ret = 0;

out:
    if (child) {
        saved = get_current();
        set_current(child);
        umount("/tmp/proc-pid-mnt-target");
        set_current(saved);
        task_unlink_child_impl(parent, child);
        free_task(child);
    }
    reset_procfs_namespace_state();
    return ret;
}

int procfs_namespace_contract_root_files_are_readable(void) {
    char content[1024];

    reset_procfs_namespace_state();

    if (read_file_content("/proc/filesystems", content, sizeof(content)) != 0 ||
        !contains(content, "proc\n") ||
        !contains(content, "tmpfs\n")) {
        errno = ENODATA;
        return -1;
    }
    if (read_file_content("/proc/meminfo", content, sizeof(content)) != 0 ||
        !contains(content, "MemTotal:") ||
        !contains(content, "MemAvailable:")) {
        errno = ENODATA;
        return -1;
    }
    if (read_file_content("/proc/cpuinfo", content, sizeof(content)) != 0 ||
        !contains(content, "processor") ||
        !contains(content, "AArch64")) {
        errno = ENODATA;
        return -1;
    }

    return 0;
}
