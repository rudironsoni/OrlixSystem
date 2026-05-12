#include <uapi/linux/fcntl.h>
#include <uapi/asm/stat.h>
#include <uapi/linux/fs.h>
#include <uapi/linux/mman.h>
#include <uapi/linux/mount.h>
#include <uapi/linux/sched.h>
#include <uapi/linux/signal.h>
#include <uapi/linux/stat.h>
#include <uapi/linux/wait.h>
#include <uapi/linux/errno.h>
#include <linux/dirent.h>
#include <linux/string.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ProcfsNamespaceContract.h"
#include "fs/vfs.h"
#include "kernel/cred.h"
#include "kernel/mm.h"
#include "kernel/signal.h"
#include "private/kernel/signal_state.h"
#include "kernel/task.h"
#include "private/kernel/task_state.h"
#include "kernel/uts.h"
#include "kernel/wait.h"

extern int errno;

extern int clone_impl(uint64_t flags);
extern int unshare_impl(uint64_t flags);
extern int fcntl_impl(int fd, int cmd, ...);
extern int mkdir_impl(const char *pathname, uint32_t mode);
extern int mount(const char *source, const char *target, const char *filesystemtype,
                 unsigned long mountflags, const void *data);
extern int umount_impl(const char *target);
extern int open_impl(const char *pathname, int flags, uint32_t mode);
extern int close_impl(int fd);
extern long long lseek_impl(int fd, long long offset, int whence);
extern long read_impl(int fd, void *buf, size_t count);
extern long readlink_impl(const char *pathname, char *buf, size_t bufsiz);
extern ssize_t getdents64_impl(int fd, void *dirp, size_t count);
extern int signal_generate_process(struct task *target, int32_t sig);
extern void cred_reset_to_defaults(void);
extern void set_current_cred(struct cred *cred);

static void reset_procfs_namespace_state(void) {
    cred_reset_to_defaults();
    uts_reset_current_namespace();
    if (task_current()) {
        atomic_set(&task_current()->new_pid_namespace_pending, 0);
        task_current()->thread_pending_signals = 0;
        if (task_current()->signal) {
            memset(&task_current()->signal->pending, 0, sizeof(task_current()->signal->pending));
            memset(&task_current()->signal->shared_pending, 0,
                   sizeof(task_current()->signal->shared_pending));
        }
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

static int append_decimal_value(char *buf, size_t buf_len, size_t *pos, int value) {
    char digits[16];
    int count = 0;

    if (!buf || !pos || value < 0 || *pos >= buf_len) {
        errno = EINVAL;
        return -1;
    }
    do {
        digits[count++] = (char)('0' + (value % 10));
        value /= 10;
    } while (value > 0 && count < (int)sizeof(digits));
    if (value > 0 || *pos + (size_t)count + 1 > buf_len) {
        errno = ENAMETOOLONG;
        return -1;
    }
    for (int i = count - 1; i >= 0; i--) {
        buf[(*pos)++] = digits[i];
    }
    buf[*pos] = '\0';
    return 0;
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

static int parse_signed_decimal_token(const char *token, long long *value_out) {
    long long value = 0;
    int sign = 1;

    if (!token || !value_out) {
        errno = EINVAL;
        return -1;
    }
    if (*token == '-') {
        sign = -1;
        token++;
    }
    if (*token < '0' || *token > '9') {
        errno = EPROTO;
        return -1;
    }
    while (*token >= '0' && *token <= '9') {
        value = (value * 10) + (*token - '0');
        token++;
    }
    *value_out = value * sign;
    return 0;
}

static int proc_stat_numeric_field(const char *stat, int field, long long *value_out) {
    const char *cursor;
    int current_field = 3;

    if (!stat || !value_out || field < 4) {
        errno = EINVAL;
        return -1;
    }
    cursor = strrchr(stat, ')');
    if (!cursor || cursor[1] != ' ' || cursor[2] == '\0' || cursor[3] != ' ') {
        errno = EPROTO;
        return -1;
    }
    cursor += 4;
    current_field = 4;
    while (current_field < field) {
        while (*cursor && *cursor != ' ') {
            cursor++;
        }
        while (*cursor == ' ') {
            cursor++;
        }
        if (*cursor == '\0') {
            errno = ENODATA;
            return -1;
        }
        current_field++;
    }
    return parse_signed_decimal_token(cursor, value_out);
}

static int proc_status_kb_field(const char *status, const char *name, long long *value_out) {
    const char *line;
    const char *cursor;
    size_t name_len;

    if (!status || !name || !value_out) {
        errno = EINVAL;
        return -1;
    }
    name_len = strlen(name);
    line = status;
    while (*line) {
        const char *next = strchr(line, '\n');
        const char *end = next ? next : line + strlen(line);

        if ((size_t)(end - line) > name_len + 1 &&
            memcmp(line, name, name_len) == 0 &&
            line[name_len] == ':' && line[name_len + 1] == '\t') {
            cursor = line + name_len + 2;
            return parse_signed_decimal_token(cursor, value_out);
        }
        if (!next) {
            break;
        }
        line = next + 1;
    }
    errno = ENODATA;
    return -1;
}

static int decimal_token_field(const char *content, int field, long long *value_out) {
    const char *cursor = content;
    int current = 1;

    if (!content || !value_out || field < 1) {
        errno = EINVAL;
        return -1;
    }
    while (current < field) {
        while (*cursor && *cursor != ' ' && *cursor != '\n' && *cursor != '\t') {
            cursor++;
        }
        while (*cursor == ' ' || *cursor == '\t') {
            cursor++;
        }
        if (*cursor == '\0' || *cursor == '\n') {
            errno = ENODATA;
            return -1;
        }
        current++;
    }
    return parse_signed_decimal_token(cursor, value_out);
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

static int procfs_read_fdinfo_pos_for_pid(int pid, int fd_num, long long *pos_out) {
    char path[96] = {0};
    char content[512];
    char *pos_line;
    long long pos = 0;

    if (!pos_out) {
        errno = EINVAL;
        return -1;
    }

    proc_pid_file_path(path, sizeof(path), pid, "/fdinfo/");
    append_positive_decimal(path, sizeof(path), fd_num);
    if (read_file_content(path, content, sizeof(content)) != 0) {
        return -1;
    }

    pos_line = strstr(content, "pos:\t");
    if (!pos_line) {
        errno = ENODATA;
        return -1;
    }

    for (pos_line += 5; *pos_line >= '0' && *pos_line <= '9'; pos_line++) {
        pos = (pos * 10) + (*pos_line - '0');
    }
    *pos_out = pos;
    return 0;
}

static void release_lookup_child(struct task *parent, struct task *child) {
    if (!child) {
        return;
    }
    task_unlink_child_impl(parent, child);
    task_put(child);
    task_put(child);
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
    struct task *task;
    char path[64] = {0};
    char content[512];
    char expected[32] = "Pid:\t";

    reset_procfs_namespace_state();

    task = task_current();
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
    struct task *parent;
    struct task *child;
    struct task *saved;
    char content[512];
    int pid;
    int ret = -1;

    reset_procfs_namespace_state();

    parent = task_current();
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

    saved = task_current();
    task_set_current(child);
    if (read_file_content("/proc/self/status", content, sizeof(content)) == 0 &&
        contains(content, "NSpid:\t1\n")) {
        ret = 0;
    } else {
        errno = ENODATA;
    }
    task_set_current(saved);
    release_lookup_child(parent, child);
    reset_procfs_namespace_state();
    return ret;
}

int procfs_namespace_contract_proc_pid_status_reports_target_credentials(void) {
    struct task *parent;
    struct task *child = NULL;
    struct task *saved;
    struct cred *parent_cred = NULL;
    struct cred *child_cred = NULL;
    char path[64] = {0};
    char content[512];
    int ret = -1;

    reset_procfs_namespace_state();

    parent = task_current();
    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    child = task_create_child_impl(parent);
    if (!child) {
        return -1;
    }

    parent_cred = dup_cred(cred_current());
    child_cred = dup_cred(cred_current());
    if (!parent_cred || !child_cred) {
        errno = ENOMEM;
        goto out;
    }
    if (cred_setuid(child_cred, 1000) != 0) {
        errno = EPERM;
        goto out;
    }

    saved = task_current();
    task_set_current(child);
    set_current_cred(child_cred);
    cred_release(child_cred);
    child_cred = NULL;
    task_set_current(parent);
    set_current_cred(parent_cred);
    cred_release(parent_cred);
    parent_cred = NULL;

    proc_pid_status_path(path, sizeof(path), child->pid);
    if (read_file_content(path, content, sizeof(content)) != 0) {
        task_set_current(saved);
        goto out;
    }
    task_set_current(saved);

    if (!contains(content, "Uid:\t1000\t1000\t1000\t1000\n")) {
        errno = ENODATA;
        goto out;
    }

    ret = 0;

out:
    if (parent_cred) {
        cred_release(parent_cred);
    }
    if (child_cred) {
        cred_release(child_cred);
    }
    if (child) {
        task_unlink_child_impl(parent, child);
        task_put(child);
    }
    reset_procfs_namespace_state();
    return ret;
}

int procfs_namespace_contract_proc_status_reports_groups_and_capabilities(void) {
    __kernel_gid32_t groups[2] = {3000, 3001};
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

int procfs_namespace_contract_proc_status_reports_thread_and_signal_queue_fields(void) {
    char content[2048];

    reset_procfs_namespace_state();

    if (read_file_content("/proc/self/status", content, sizeof(content)) != 0) {
        return -1;
    }

    if (!contains(content, "Threads:\t1\n") ||
        !contains(content, "SigQ:\t") ||
        !contains(content, "SigPnd:\t") ||
        !contains(content, "ShdPnd:\t") ||
        !contains(content, "SigBlk:\t") ||
        !contains(content, "SigIgn:\t") ||
        !contains(content, "SigCgt:\t")) {
        errno = ENODATA;
        return -1;
    }

    return 0;
}

int procfs_namespace_contract_proc_status_reports_thread_group_count(void) {
    struct task *parent;
    struct task *thread = NULL;
    char content[2048];
    int ret = -1;

    reset_procfs_namespace_state();
    parent = task_current();
    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    thread = task_create_child_with_flags_impl(parent, CLONE_THREAD);
    if (!thread) {
        return -1;
    }

    if (read_file_content("/proc/self/status", content, sizeof(content)) != 0) {
        goto out;
    }
    if (!contains(content, "Threads:\t2\n")) {
        errno = ENODATA;
        goto out;
    }
    ret = 0;

out:
    if (thread) {
        task_unlink_child_impl(parent, thread);
        task_put(thread);
    }
    reset_procfs_namespace_state();
    return ret;
}

int procfs_namespace_contract_proc_task_tid_status_aliases_thread(void) {
    struct task *parent;
    struct task *thread = NULL;
    char task_dir[96] = {0};
    char tid_dir[112] = {0};
    char status_path[128] = {0};
    char tid_name[16] = {0};
    char dirents[1024];
    char content[2048];
    int fd = -1;
    long nread;
    int ret = -1;

    reset_procfs_namespace_state();
    parent = task_current();
    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    thread = task_create_child_with_flags_impl(parent, CLONE_THREAD);
    if (!thread) {
        return -1;
    }
    thread->comm[0] = 't';
    thread->comm[1] = 'i';
    thread->comm[2] = 'd';
    thread->comm[3] = '-';
    thread->comm[4] = 'v';
    thread->comm[5] = 'i';
    thread->comm[6] = 'e';
    thread->comm[7] = 'w';
    thread->comm[8] = '\0';

    proc_pid_file_path(task_dir, sizeof(task_dir), parent->pid, "/task");
    fd = open_impl(task_dir, O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0) {
        goto out;
    }
    nread = getdents64_impl(fd, dirents, sizeof(dirents));
    if (nread < 0) {
        goto out;
    }
    append_positive_decimal(tid_name, sizeof(tid_name), thread->pid);
    if (!procfs_dirents_contain_name(dirents, nread, tid_name)) {
        errno = ENODATA;
        goto out;
    }
    close_impl(fd);
    fd = -1;

    proc_pid_file_path(tid_dir, sizeof(tid_dir), parent->pid, "/task/");
    append_positive_decimal(tid_dir, sizeof(tid_dir), thread->pid);
    fd = open_impl(tid_dir, O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0) {
        goto out;
    }
    close_impl(fd);
    fd = -1;

    proc_pid_file_path(status_path, sizeof(status_path), parent->pid, "/task/");
    append_positive_decimal(status_path, sizeof(status_path), thread->pid);
    if (strlen(status_path) + 8 >= sizeof(status_path)) {
        errno = ENAMETOOLONG;
        goto out;
    }
    memcpy(status_path + strlen(status_path), "/status", 8);
    if (read_file_content(status_path, content, sizeof(content)) != 0) {
        goto out;
    }
    if (!contains(content, "Name:\ttid-view\n") ||
        !contains(content, "Threads:\t2\n")) {
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
        if (thread) {
            task_unlink_child_impl(parent, thread);
            task_put(thread);
        }
        reset_procfs_namespace_state();
        errno = saved_errno;
    }
    return ret;
}

int procfs_namespace_contract_proc_task_tid_fd_maps_and_stat_are_thread_targeted(void) {
    struct task *parent;
    struct task *thread = NULL;
    void *mapped = (void *)-1;
    uint64_t base;
    int fd = -1;
    int info_fd = -1;
    char path[160] = {0};
    char link_target[MAX_PATH];
    char content[4096];
    char tid_name[16] = {0};
    long nread;
    int ret = -1;

    reset_procfs_namespace_state();
    parent = task_current();
    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    fd = open_impl("/dev/null", O_RDONLY | O_CLOEXEC, 0);
    if (fd < 0) {
        return -1;
    }
    thread = task_create_child_with_flags_impl(parent, CLONE_THREAD);
    if (!thread) {
        close_impl(fd);
        return -1;
    }
    memset(thread->comm, 0, sizeof(thread->comm));
    memcpy(thread->comm, "tid-deep", 9);
    task_set_current(thread);
    mapped = mmap_impl(NULL, 4096, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    task_set_current(parent);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        goto out;
    }
    base = (uint64_t)(uintptr_t)mapped;

    proc_pid_file_path(path, sizeof(path), parent->pid, "/task/");
    append_positive_decimal(path, sizeof(path), thread->pid);
    if (strlen(path) + 5 >= sizeof(path)) {
        errno = ENAMETOOLONG;
        goto out;
    }
    memcpy(path + strlen(path), "/stat", 6);
    if (read_file_content(path, content, sizeof(content)) != 0 ||
        !contains(content, "(tid-deep)")) {
        errno = ENODATA;
        goto out;
    }

    memset(path, 0, sizeof(path));
    proc_pid_file_path(path, sizeof(path), parent->pid, "/task/");
    append_positive_decimal(path, sizeof(path), thread->pid);
    if (strlen(path) + 5 >= sizeof(path)) {
        errno = ENAMETOOLONG;
        goto out;
    }
    memcpy(path + strlen(path), "/maps", 6);
    if (read_file_content(path, content, sizeof(content)) != 0) {
        goto out;
    }
    append_positive_decimal(tid_name, sizeof(tid_name), thread->pid);
    (void)tid_name;
    if (!contains(content, "[anon]")) {
        errno = ENOMSG;
        goto out;
    }

    memset(path, 0, sizeof(path));
    proc_pid_file_path(path, sizeof(path), parent->pid, "/task/");
    append_positive_decimal(path, sizeof(path), thread->pid);
    if (strlen(path) + 4 >= sizeof(path)) {
        errno = ENAMETOOLONG;
        goto out;
    }
    memcpy(path + strlen(path), "/fd/", 5);
    append_positive_decimal(path, sizeof(path), fd);
    if (read_link_content(path, link_target, sizeof(link_target)) != 0 ||
        strcmp(link_target, "/dev/null") != 0) {
        errno = ENOTCONN;
        goto out;
    }

    memset(path, 0, sizeof(path));
    proc_pid_file_path(path, sizeof(path), parent->pid, "/task/");
    append_positive_decimal(path, sizeof(path), thread->pid);
    if (strlen(path) + 8 >= sizeof(path)) {
        errno = ENAMETOOLONG;
        goto out;
    }
    memcpy(path + strlen(path), "/fdinfo/", 9);
    append_positive_decimal(path, sizeof(path), fd);
    info_fd = open_impl(path, O_RDONLY, 0);
    if (info_fd < 0) {
        goto out;
    }
    nread = read_impl(info_fd, content, sizeof(content) - 1);
    if (nread <= 0) {
        goto out;
    }
    content[nread] = '\0';
    if (!contains(content, "flags:\t")) {
        errno = ERANGE;
        goto out;
    }

    ret = 0;

out:
    {
        int saved_errno = errno;
        close_impl(info_fd);
        close_impl(fd);
        if ((long)(uintptr_t)mapped >= 0) {
            task_set_current(thread ? thread : parent);
            munmap_impl(mapped, 4096);
            task_set_current(parent);
        } else {
            task_set_current(parent);
        }
        if (thread) {
            task_unlink_child_impl(parent, thread);
            task_put(thread);
        }
        reset_procfs_namespace_state();
        errno = saved_errno;
    }
    return ret;
}

int procfs_namespace_contract_clone_files_shares_thread_fdtable(void) {
    struct task *parent;
    struct task *thread = NULL;
    int fd = -1;
    char path[160] = {0};
    char link_target[MAX_PATH];
    int ret = -1;

    reset_procfs_namespace_state();
    parent = task_current();
    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    thread = task_create_child_with_flags_impl(parent, CLONE_THREAD | CLONE_FILES);
    if (!thread) {
        return -1;
    }
    fd = open_impl("/dev/null", O_RDONLY | O_CLOEXEC, 0);
    if (fd < 0) {
        goto out;
    }

    proc_pid_file_path(path, sizeof(path), parent->pid, "/task/");
    append_positive_decimal(path, sizeof(path), thread->pid);
    if (strlen(path) + 4 >= sizeof(path)) {
        errno = ENAMETOOLONG;
        goto out;
    }
    memcpy(path + strlen(path), "/fd/", 5);
    append_positive_decimal(path, sizeof(path), fd);
    if (read_link_content(path, link_target, sizeof(link_target)) != 0 ||
        strcmp(link_target, "/dev/null") != 0) {
        errno = ENOENT;
        goto out;
    }

    task_set_current(thread);
    if (close_impl(fd) != 0) {
        task_set_current(parent);
        goto out;
    }
    task_set_current(parent);
    if (fcntl_impl(fd, F_GETFD) != -1 || errno != EBADF) {
        errno = EBUSY;
        goto out;
    }
    fd = -1;
    ret = 0;

out:
    {
        int saved_errno = errno;
        task_set_current(parent);
        close_impl(fd);
        if (thread) {
            task_unlink_child_impl(parent, thread);
            task_put(thread);
        }
        reset_procfs_namespace_state();
        errno = saved_errno;
    }
    return ret;
}

int procfs_namespace_contract_proc_task_tid_status_reports_thread_signal_state(void) {
    struct task *parent;
    struct task *thread = NULL;
    char path[160] = {0};
    char content[4096];
    int ret = -1;

    reset_procfs_namespace_state();
    parent = task_current();
    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    thread = task_create_child_with_flags_impl(parent, CLONE_THREAD);
    if (!thread) {
        return -1;
    }
    memset(thread->comm, 0, sizeof(thread->comm));
    memcpy(thread->comm, "sig-tid", 8);
    if (signal_generate_task(thread, SIGUSR1) != 0) {
        goto out;
    }

    proc_pid_file_path(path, sizeof(path), parent->pid, "/task/");
    append_positive_decimal(path, sizeof(path), thread->pid);
    if (strlen(path) + 8 >= sizeof(path)) {
        errno = ENAMETOOLONG;
        goto out;
    }
    memcpy(path + strlen(path), "/status", 8);
    if (read_file_content(path, content, sizeof(content)) != 0) {
        goto out;
    }
    if (!contains(content, "Name:\tsig-tid\n") ||
        !contains(content, "Tgid:\t") ||
        !contains(content, "Threads:\t2\n") ||
        !contains(content, "SigQ:\t1/1024\n") ||
        !contains(content, "SigPnd:\t0000000000000200\n")) {
        errno = ENODATA;
        goto out;
    }
    ret = 0;

out:
    {
        int saved_errno = errno;
        signal_clear_pending_markers_task(thread, SIGUSR1);
        if (thread) {
            task_unlink_child_impl(parent, thread);
            task_put(thread);
        }
        reset_procfs_namespace_state();
        errno = saved_errno;
    }
    return ret;
}

int procfs_namespace_contract_clone_vm_thread_shares_proc_maps(void) {
    struct task *parent;
    struct task *thread = NULL;
    void *mapped = (void *)-1;
    uint64_t base;
    char path[160] = {0};
    char parent_maps[4096];
    char thread_maps[4096];
    char needle[32] = {0};
    int ret = -1;

    reset_procfs_namespace_state();
    parent = task_current();
    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    thread = task_create_child_with_flags_impl(parent, CLONE_THREAD | CLONE_VM | CLONE_SIGHAND);
    if (!thread) {
        return -1;
    }
    mapped = mmap_impl(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        goto out;
    }
    base = (uint64_t)(uintptr_t)mapped;
    needle[0] = '\0';
    for (int shift = 60; shift >= 0; shift -= 4) {
        char c = (char)((base >> shift) & 0xf);
        size_t len = strlen(needle);
        if (len + 1 >= sizeof(needle)) {
            errno = ENAMETOOLONG;
            goto out;
        }
        needle[len] = (char)(c < 10 ? '0' + c : 'a' + (c - 10));
        needle[len + 1] = '\0';
    }
    while (needle[0] == '0' && needle[1] != '\0') {
        memmove(needle, needle + 1, strlen(needle));
    }
    if (read_file_content("/proc/self/maps", parent_maps, sizeof(parent_maps)) != 0) {
        goto out;
    }
    proc_pid_file_path(path, sizeof(path), parent->pid, "/task/");
    append_positive_decimal(path, sizeof(path), thread->pid);
    if (strlen(path) + 5 >= sizeof(path)) {
        errno = ENAMETOOLONG;
        goto out;
    }
    memcpy(path + strlen(path), "/maps", 6);
    if (read_file_content(path, thread_maps, sizeof(thread_maps)) != 0) {
        goto out;
    }
    if (!contains(parent_maps, needle) || !contains(thread_maps, needle)) {
        errno = ENODATA;
        goto out;
    }
    ret = 0;

out:
    {
        int saved_errno = errno;
        if ((long)(uintptr_t)mapped >= 0) {
            munmap_impl(mapped, 4096);
        }
        if (thread) {
            task_unlink_child_impl(parent, thread);
            task_put(thread);
        }
        reset_procfs_namespace_state();
        errno = saved_errno;
    }
    return ret;
}

int procfs_namespace_contract_process_signal_reports_shared_pending(void) {
    struct task *parent;
    struct task *thread = NULL;
    char path[160] = {0};
    char content[4096];
    int ret = -1;

    reset_procfs_namespace_state();
    parent = task_current();
    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    thread = task_create_child_with_flags_impl(parent, CLONE_THREAD | CLONE_VM | CLONE_SIGHAND);
    if (!thread) {
        return -1;
    }
    if (signal_generate_process(parent, SIGUSR2) != 0) {
        goto out;
    }
    proc_pid_file_path(path, sizeof(path), parent->pid, "/task/");
    append_positive_decimal(path, sizeof(path), thread->pid);
    if (strlen(path) + 8 >= sizeof(path)) {
        errno = ENAMETOOLONG;
        goto out;
    }
    memcpy(path + strlen(path), "/status", 8);
    if (read_file_content(path, content, sizeof(content)) != 0) {
        goto out;
    }
    if (!contains(content, "SigPnd:\t0000000000000000\n") ||
        !contains(content, "ShdPnd:\t0000000000000800\n")) {
        errno = ENODATA;
        goto out;
    }
    ret = 0;

out:
    {
        int saved_errno = errno;
        signal_clear_pending_markers_task(parent, SIGUSR2);
        if (thread) {
            task_unlink_child_impl(parent, thread);
            task_put(thread);
        }
        reset_procfs_namespace_state();
        errno = saved_errno;
    }
    return ret;
}

int procfs_namespace_contract_thread_signal_pending_is_per_tid(void) {
    struct task *parent;
    struct task *thread = NULL;
    char thread_path[160] = {0};
    char parent_status[4096];
    char thread_status[4096];
    int ret = -1;

    reset_procfs_namespace_state();
    parent = task_current();
    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    thread = task_create_child_with_flags_impl(parent, CLONE_THREAD | CLONE_VM | CLONE_SIGHAND);
    if (!thread) {
        return -1;
    }
    if (signal_generate_task(thread, SIGUSR1) != 0) {
        goto out;
    }
    if (read_file_content("/proc/self/status", parent_status, sizeof(parent_status)) != 0) {
        goto out;
    }
    proc_pid_file_path(thread_path, sizeof(thread_path), parent->pid, "/task/");
    append_positive_decimal(thread_path, sizeof(thread_path), thread->pid);
    if (strlen(thread_path) + 8 >= sizeof(thread_path)) {
        errno = ENAMETOOLONG;
        goto out;
    }
    memcpy(thread_path + strlen(thread_path), "/status", 8);
    if (read_file_content(thread_path, thread_status, sizeof(thread_status)) != 0) {
        goto out;
    }
    if (!contains(parent_status, "SigPnd:\t0000000000000000\n") ||
        !contains(thread_status, "SigPnd:\t0000000000000200\n") ||
        !contains(thread_status, "ShdPnd:\t0000000000000000\n")) {
        errno = ENODATA;
        goto out;
    }
    ret = 0;

out:
    {
        int saved_errno = errno;
        if (thread) {
            signal_clear_pending_markers_task(thread, SIGUSR1);
            task_unlink_child_impl(parent, thread);
            task_put(thread);
        }
        reset_procfs_namespace_state();
        errno = saved_errno;
    }
    return ret;
}

int procfs_namespace_contract_thread_group_stop_continue_reports_once(void) {
    struct task *parent;
    struct task *child = NULL;
    struct task *thread = NULL;
    int status = 0;
    int ret = -1;

    reset_procfs_namespace_state();
    parent = task_current();
    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    child = task_create_child_impl(parent);
    if (!child) {
        return -1;
    }
    task_set_current(child);
    thread = task_create_child_with_flags_impl(child, CLONE_THREAD | CLONE_VM | CLONE_SIGHAND);
    task_set_current(parent);
    if (!thread) {
        goto out;
    }
    if (signal_generate_task(thread, SIGSTOP) != 0 ||
        !atomic_read(&child->stopped) ||
        !atomic_read(&thread->stopped)) {
        errno = ENODATA;
        goto out;
    }
    if (waitpid_impl(child->pid, &status, WUNTRACED) != child->pid ||
        (status & 0xff) != 0x7f ||
        ((status >> 8) & 0xff) != SIGSTOP) {
        errno = ENOMSG;
        goto out;
    }
    if (waitpid_impl(child->pid, &status, WUNTRACED | WNOHANG) != 0) {
        errno = EALREADY;
        goto out;
    }
    if (signal_generate_task(child, SIGCONT) != 0 ||
        atomic_read(&child->stopped) ||
        atomic_read(&thread->stopped)) {
        errno = ERANGE;
        goto out;
    }
    if (waitpid_impl(child->pid, &status, WCONTINUED) != child->pid ||
        status != 0xffff) {
        errno = ENOTRECOVERABLE;
        goto out;
    }
    if (waitpid_impl(child->pid, &status, WCONTINUED | WNOHANG) != 0) {
        errno = EBUSY;
        goto out;
    }
    ret = 0;

out:
    {
        int saved_errno = errno;
        task_set_current(parent);
        if (thread) {
            task_unlink_child_impl(child, thread);
            task_put(thread);
        }
        if (child) {
            task_unlink_child_impl(parent, child);
            task_put(child);
        }
        reset_procfs_namespace_state();
        errno = saved_errno;
    }
    return ret;
}

int procfs_namespace_contract_proc_pid_stat_cwd_and_exe_report_target_task(void) {
    struct task *parent;
    struct task *child = NULL;
    char path[96] = {0};
    char content[1024];
    char link_target[MAX_PATH];
    int ret = -1;

    reset_procfs_namespace_state();

    parent = task_current();
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
        task_put(child);
    }
    reset_procfs_namespace_state();
    return ret;
}

int procfs_namespace_contract_proc_pid_fd_and_fdinfo_paths_are_target_aware(void) {
    struct task *parent;
    struct task *child = NULL;
    int fd = -1;
    char path[96] = {0};
    char link_target[MAX_PATH];
    char content[512];
    long nread;
    int info_fd = -1;
    int ret = -1;

    reset_procfs_namespace_state();

    parent = task_current();
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
        task_put(child);
    }
    reset_procfs_namespace_state();
    return ret;
}

int procfs_namespace_contract_proc_pid_fd_dir_lists_target_inherited_fds_after_parent_close(void) {
    struct task *parent;
    struct task *child = NULL;
    int inherited_fd = -1;
    int inherited_fd_num = -1;
    int dir_fd = -1;
    char path[96] = {0};
    char fd_name[16] = {0};
    char dirents[1024];
    long nread;
    int ret = -1;

    reset_procfs_namespace_state();

    parent = task_current();
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
    nread = getdents64_impl(dir_fd, dirents, sizeof(dirents));
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
        task_put(child);
    }
    reset_procfs_namespace_state();
    return ret;
}

int procfs_namespace_contract_fdinfo_flags_are_per_task_descriptor_state(void) {
    struct task *parent;
    struct task *child = NULL;
    struct task *saved;
    int fd = -1;
    unsigned int parent_flags = 0;
    unsigned int child_flags = 0;
    int ret = -1;

    reset_procfs_namespace_state();

    parent = task_current();
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

    saved = task_current();
    task_set_current(child);
    if (fcntl_impl(fd, F_SETFD, FD_CLOEXEC) != 0) {
        task_set_current(saved);
        goto out;
    }
    if (procfs_read_fdinfo_flags_for_pid(child->pid, fd, &child_flags) != 0 ||
        procfs_read_fdinfo_flags_for_pid(parent->pid, fd, &parent_flags) != 0) {
        task_set_current(saved);
        goto out;
    }
    task_set_current(saved);

    if ((child_flags & O_CLOEXEC) == 0 || (parent_flags & O_CLOEXEC) != 0) {
        errno = ENODATA;
        goto out;
    }

    ret = 0;

out:
    task_set_current(parent);
    if (fd >= 0) {
        close_impl(fd);
    }
    if (child) {
        task_unlink_child_impl(parent, child);
        task_put(child);
    }
    reset_procfs_namespace_state();
    return ret;
}

int procfs_namespace_contract_child_close_does_not_close_parent_descriptor(void) {
    struct task *parent;
    struct task *child = NULL;
    struct task *saved;
    int fd = -1;
    char path[96] = {0};
    char link_target[MAX_PATH];
    char byte;
    int ret = -1;

    reset_procfs_namespace_state();

    parent = task_current();
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

    saved = task_current();
    task_set_current(child);
    if (close_impl(fd) != 0) {
        task_set_current(saved);
        goto out;
    }
    task_set_current(saved);

    proc_pid_file_path(path, sizeof(path), parent->pid, "/fd/");
    append_positive_decimal(path, sizeof(path), fd);
    if (read_link_content(path, link_target, sizeof(link_target)) != 0 ||
        strcmp(link_target, "/dev/null") != 0) {
        errno = ENODATA;
        goto out;
    }
    if (read_impl(fd, &byte, sizeof(byte)) != 0) {
        errno = ENODATA;
        goto out;
    }

    memset(path, 0, sizeof(path));
    proc_pid_file_path(path, sizeof(path), child->pid, "/fd/");
    append_positive_decimal(path, sizeof(path), fd);
    if (read_link_content(path, link_target, sizeof(link_target)) == 0 || errno != ENOENT) {
        errno = ENODATA;
        goto out;
    }

    ret = 0;

out:
    task_set_current(parent);
    if (fd >= 0) {
        close_impl(fd);
    }
    if (child) {
        task_unlink_child_impl(parent, child);
        task_put(child);
    }
    reset_procfs_namespace_state();
    return ret;
}

int procfs_namespace_contract_fdinfo_offset_tracks_shared_open_file_description(void) {
    struct task *parent;
    struct task *child = NULL;
    int fd = -1;
    long long child_pos = -1;
    int ret = -1;

    reset_procfs_namespace_state();

    parent = task_current();
    if (!parent) {
        errno = ESRCH;
        return -1;
    }

    fd = open_impl("/tmp/proc-fdinfo-offset", O_CREAT | O_RDWR | O_TRUNC, 0600);
    if (fd < 0) {
        return -1;
    }

    child = task_create_child_impl(parent);
    if (!child) {
        goto out;
    }

    if (lseek_impl(fd, 7, SEEK_SET) != 7 ||
        procfs_read_fdinfo_pos_for_pid(child->pid, fd, &child_pos) != 0) {
        goto out;
    }
    if (child_pos != 7) {
        errno = ENODATA;
        goto out;
    }

    ret = 0;

out:
    task_set_current(parent);
    if (fd >= 0) {
        close_impl(fd);
    }
    if (child) {
        task_unlink_child_impl(parent, child);
        task_put(child);
    }
    reset_procfs_namespace_state();
    return ret;
}

int procfs_namespace_contract_proc_pid_cmdline_environ_and_comm_report_target_task(void) {
    struct task *parent;
    struct task *child = NULL;
    struct task *saved;
    char *argv[] = {"target-prog", "target-arg", NULL};
    char *envp[] = {"TARGET_ENV=1", NULL};
    char path[96] = {0};
    char content[512];
    long nread;
    int ret = -1;

    reset_procfs_namespace_state();

    parent = task_current();
    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    child = task_create_child_impl(parent);
    if (!child) {
        return -1;
    }

    saved = task_current();
    task_set_current(child);
    memset(child->comm, 0, sizeof(child->comm));
    memcpy(child->comm, "target-comm", 12);
    if (task_record_exec_strings_impl(argv, envp) != 0) {
        task_set_current(saved);
        goto out;
    }
    task_set_current(saved);

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
        task_put(child);
    }
    reset_procfs_namespace_state();
    return ret;
}

int procfs_namespace_contract_proc_pid_stat_status_and_maps_report_target_task(void) {
    struct task *parent;
    struct task *child = NULL;
    struct task *saved;
    void *mapping = (void *)-1;
    char path[96] = {0};
    char content[1024];
    int ret = -1;

    reset_procfs_namespace_state();

    parent = task_current();
    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    child = task_create_child_impl(parent);
    if (!child) {
        return -1;
    }

    saved = task_current();
    task_set_current(child);
    memset(child->comm, 0, sizeof(child->comm));
    memcpy(child->comm, "proc-target", 12);
    task_mark_stopped_by_signal(child, SIGSTOP);
    mapping = mmap_impl(NULL, TASK_VMA_PAGE_SIZE, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    task_set_current(saved);
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
        saved = task_current();
        task_set_current(child);
        if (mapping != (void *)-1) {
            munmap_impl(mapping, TASK_VMA_PAGE_SIZE);
        }
        task_set_current(saved);
        task_unlink_child_impl(parent, child);
        task_put(child);
    }
    reset_procfs_namespace_state();
    return ret;
}

int procfs_namespace_contract_proc_pid_status_stat_and_fdinfo_have_linux_fields(void) {
    struct task *parent;
    struct task *child = NULL;
    struct task *saved;
    char path[96] = {0};
    char content[2048];
    int child_fd = -1;
    int ret = -1;

    reset_procfs_namespace_state();
    parent = task_current();
    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    child = task_create_child_impl(parent);
    if (!child) {
        return -1;
    }

    saved = task_current();
    task_set_current(child);
    memset(child->comm, 0, sizeof(child->comm));
    memcpy(child->comm, "proc-fields", 12);
    child_fd = open_impl("/proc/self/status", O_RDONLY, 0);
    task_set_current(saved);
    if (child_fd < 0) {
        goto out;
    }

    task_mark_stopped_by_signal(child, SIGSTOP);
    proc_pid_file_path(path, sizeof(path), child->pid, "/status");
    if (read_file_content(path, content, sizeof(content)) != 0 ||
        !contains(content, "Name:\tproc-fields\n") ||
        !contains(content, "State:\tT (stopped)\n") ||
        !contains(content, "Tgid:\t") ||
        !contains(content, "Uid:\t") ||
        !contains(content, "Gid:\t") ||
        !contains(content, "NoNewPrivs:\t0\n")) {
        errno = ENODATA;
        goto out;
    }
    proc_pid_file_path(path, sizeof(path), child->pid, "/stat");
    if (read_file_content(path, content, sizeof(content)) != 0 ||
        !contains(content, "(proc-fields)") ||
        !contains(content, " T ")) {
        errno = ENOMSG;
        goto out;
    }
    {
        size_t path_pos;

        proc_pid_file_path(path, sizeof(path), child->pid, "/fdinfo/");
        path_pos = strlen(path);
        if (append_decimal_value(path, sizeof(path), &path_pos, child_fd) != 0) {
            goto out;
        }
    }
    if (read_file_content(path, content, sizeof(content)) != 0 ||
        !contains(content, "pos:\t") ||
        !contains(content, "flags:\t") ||
        !contains(content, "mnt_id:\t")) {
        errno = ENOTRECOVERABLE;
        goto out;
    }

    task_mark_exited(child, 7);
    proc_pid_file_path(path, sizeof(path), child->pid, "/status");
    if (read_file_content(path, content, sizeof(content)) != 0 ||
        !contains(content, "State:\tZ (zombie)\n")) {
        errno = ECHILD;
        goto out;
    }
    proc_pid_file_path(path, sizeof(path), child->pid, "/stat");
    if (read_file_content(path, content, sizeof(content)) != 0 ||
        !contains(content, " Z ")) {
        errno = EPROTO;
        goto out;
    }

    ret = 0;

out:
    if (child) {
        saved = task_current();
        task_set_current(child);
        if (child_fd >= 0) {
            close_impl(child_fd);
        }
        task_set_current(saved);
        task_unlink_child_impl(parent, child);
        task_put(child);
    }
    reset_procfs_namespace_state();
    return ret;
}

int procfs_namespace_contract_proc_pid_stat_reports_tty_start_rss_and_exit_signal(void) {
    struct task *parent;
    struct task *child = NULL;
    struct task *saved;
    void *mapping = (void *)-1;
    char path[96] = {0};
    char content[2048];
    long long tty_nr = -1;
    long long tpgid = 0;
    long long starttime = -1;
    long long vsize = 0;
    long long rss = 0;
    long long exit_signal = 0;
    int ret = -1;

    reset_procfs_namespace_state();
    parent = task_current();
    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    child = task_create_child_impl(parent);
    if (!child) {
        return -1;
    }

    saved = task_current();
    task_set_current(child);
    memset(child->comm, 0, sizeof(child->comm));
    memcpy(child->comm, "proc-stat", 10);
    mapping = mmap_impl(NULL, TASK_VMA_PAGE_SIZE * 2, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    task_set_current(saved);
    if (mapping == (void *)-1) {
        goto out;
    }

    proc_pid_file_path(path, sizeof(path), child->pid, "/stat");
    if (read_file_content(path, content, sizeof(content)) != 0 ||
        !contains(content, "(proc-stat)") ||
        !contains(content, " R ")) {
        errno = ENODATA;
        goto out;
    }

    if (proc_stat_numeric_field(content, 7, &tty_nr) != 0 ||
        proc_stat_numeric_field(content, 8, &tpgid) != 0 ||
        proc_stat_numeric_field(content, 22, &starttime) != 0 ||
        proc_stat_numeric_field(content, 23, &vsize) != 0 ||
        proc_stat_numeric_field(content, 24, &rss) != 0 ||
        proc_stat_numeric_field(content, 38, &exit_signal) != 0) {
        goto out;
    }
    if (tty_nr != 0) {
        errno = ENXIO;
        goto out;
    }
    if (tpgid != -1) {
        errno = ENOTTY;
        goto out;
    }
    if (starttime < 0) {
        errno = ERANGE;
        goto out;
    }
    if (vsize < (long long)(TASK_VMA_PAGE_SIZE * 2)) {
        errno = EFBIG;
        goto out;
    }
    if (rss < 2) {
        errno = ENOMEM;
        goto out;
    }
    if (exit_signal != SIGCHLD) {
        errno = ECHILD;
        goto out;
    }

    ret = 0;

out:
    if (child) {
        saved = task_current();
        task_set_current(child);
        if (mapping != (void *)-1) {
            munmap_impl(mapping, TASK_VMA_PAGE_SIZE * 2);
        }
        task_set_current(saved);
        task_unlink_child_impl(parent, child);
        task_put(child);
    }
    reset_procfs_namespace_state();
    return ret;
}

int procfs_namespace_contract_proc_pid_views_remain_consistent_across_lifecycle(void) {
    struct task *parent;
    struct task *child = NULL;
    struct task *saved;
    void *mapping = (void *)-1;
    char path[96] = {0};
    char stat_content[2048];
    char status_content[2048];
    char statm_content[128];
    long long stat_rss = 0;
    long long statm_resident = 0;
    long long status_rss_kb = 0;
    int ret = -1;

    reset_procfs_namespace_state();
    parent = task_current();
    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    child = task_create_child_impl(parent);
    if (!child) {
        return -1;
    }

    saved = task_current();
    task_set_current(child);
    memset(child->comm, 0, sizeof(child->comm));
    memcpy(child->comm, "proc-life", 10);
    mapping = mmap_impl(NULL, TASK_VMA_PAGE_SIZE * 3, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    task_set_current(saved);
    if (mapping == (void *)-1) {
        goto out;
    }

    proc_pid_file_path(path, sizeof(path), child->pid, "/stat");
    if (read_file_content(path, stat_content, sizeof(stat_content)) != 0 ||
        !contains(stat_content, "(proc-life)") ||
        !contains(stat_content, " R ") ||
        proc_stat_numeric_field(stat_content, 24, &stat_rss) != 0) {
        errno = ENODATA;
        goto out;
    }
    proc_pid_file_path(path, sizeof(path), child->pid, "/status");
    if (read_file_content(path, status_content, sizeof(status_content)) != 0 ||
        !contains(status_content, "Name:\tproc-life\n") ||
        !contains(status_content, "State:\tR (running)\n") ||
        !contains(status_content, "VmStk:\t") ||
        !contains(status_content, "VmExe:\t") ||
        proc_status_kb_field(status_content, "VmRSS", &status_rss_kb) != 0) {
        errno = ENOMSG;
        goto out;
    }
    proc_pid_file_path(path, sizeof(path), child->pid, "/statm");
    if (read_file_content(path, statm_content, sizeof(statm_content)) != 0 ||
        decimal_token_field(statm_content, 2, &statm_resident) != 0) {
        goto out;
    }
    if (stat_rss != statm_resident || status_rss_kb != statm_resident * 4) {
        errno = ERANGE;
        goto out;
    }

    task_mark_stopped_by_signal(child, SIGSTOP);
    proc_pid_file_path(path, sizeof(path), child->pid, "/stat");
    if (read_file_content(path, stat_content, sizeof(stat_content)) != 0 ||
        !contains(stat_content, " T ")) {
        errno = ENOTRECOVERABLE;
        goto out;
    }
    proc_pid_file_path(path, sizeof(path), child->pid, "/status");
    if (read_file_content(path, status_content, sizeof(status_content)) != 0 ||
        !contains(status_content, "State:\tT (stopped)\n")) {
        errno = EOPNOTSUPP;
        goto out;
    }

    saved = task_current();
    task_set_current(child);
    if (task_exec_transition_impl("/proc/self/status", "/bin/proc-exec") != 0) {
        task_set_current(saved);
        goto out;
    }
    task_set_current(saved);
    proc_pid_file_path(path, sizeof(path), child->pid, "/stat");
    if (read_file_content(path, stat_content, sizeof(stat_content)) != 0 ||
        !contains(stat_content, "(proc-exec)")) {
        errno = ENOEXEC;
        goto out;
    }
    proc_pid_file_path(path, sizeof(path), child->pid, "/status");
    if (read_file_content(path, status_content, sizeof(status_content)) != 0 ||
        !contains(status_content, "Name:\tproc-exec\n")) {
        errno = ENOENT;
        goto out;
    }

    task_mark_exited(child, 0);
    proc_pid_file_path(path, sizeof(path), child->pid, "/stat");
    if (read_file_content(path, stat_content, sizeof(stat_content)) != 0 ||
        !contains(stat_content, " Z ")) {
        errno = ECHILD;
        goto out;
    }
    proc_pid_file_path(path, sizeof(path), child->pid, "/status");
    if (read_file_content(path, status_content, sizeof(status_content)) != 0 ||
        !contains(status_content, "State:\tZ (zombie)\n")) {
        errno = ESRCH;
        goto out;
    }

    ret = 0;

out:
    if (child) {
        saved = task_current();
        task_set_current(child);
        if (mapping != (void *)-1) {
            munmap_impl(mapping, TASK_VMA_PAGE_SIZE * 3);
        }
        task_set_current(saved);
        task_unlink_child_impl(parent, child);
        task_put(child);
    }
    reset_procfs_namespace_state();
    return ret;
}

int procfs_namespace_contract_proc_pid_mountinfo_uses_target_mount_namespace(void) {
    struct task *parent;
    struct task *child = NULL;
    struct task *saved;
    char path[96] = {0};
    char content[4096];
    int ret = -1;

    reset_procfs_namespace_state();

    parent = task_current();
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

    saved = task_current();
    task_set_current(child);
    if (mount("/tmp/proc-pid-mnt-source", "/tmp/proc-pid-mnt-target", NULL, MS_BIND, NULL) != 0) {
        task_set_current(saved);
        goto out;
    }
    task_set_current(saved);

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

    proc_pid_file_path(path, sizeof(path), child->pid, "/mounts");
    if (read_file_content(path, content, sizeof(content)) != 0 ||
        !contains(content, "/tmp/proc-pid-mnt-source") ||
        !contains(content, "/tmp/proc-pid-mnt-target")) {
        errno = ENODATA;
        goto out;
    }

    if (read_file_content("/proc/self/mounts", content, sizeof(content)) != 0 ||
        contains(content, "/tmp/proc-pid-mnt-source")) {
        errno = ENODATA;
        goto out;
    }

    ret = 0;

out:
    if (child) {
        saved = task_current();
        task_set_current(child);
        umount_impl("/tmp/proc-pid-mnt-target");
        task_set_current(saved);
        task_unlink_child_impl(parent, child);
        task_put(child);
    }
    reset_procfs_namespace_state();
    return ret;
}

int procfs_namespace_contract_zombie_proc_pid_mounts_keep_target_mount_namespace(void) {
    struct task *parent;
    struct task *child = NULL;
    struct task *saved;
    char path[96] = {0};
    char content[4096];
    int ret = -1;

    reset_procfs_namespace_state();

    parent = task_current();
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
    if (mkdir_impl("/tmp/proc-zombie-mnt-source", 0700) != 0 && errno != EEXIST) {
        goto out;
    }
    if (mkdir_impl("/tmp/proc-zombie-mnt-target", 0700) != 0 && errno != EEXIST) {
        goto out;
    }

    saved = task_current();
    task_set_current(child);
    if (mount("/tmp/proc-zombie-mnt-source", "/tmp/proc-zombie-mnt-target", NULL, MS_BIND, NULL) != 0) {
        task_set_current(saved);
        goto out;
    }
    task_set_current(saved);

    task_mark_exited(child, 0);
    proc_pid_file_path(path, sizeof(path), child->pid, "/mounts");
    if (read_file_content(path, content, sizeof(content)) != 0 ||
        !contains(content, "/tmp/proc-zombie-mnt-source") ||
        !contains(content, "/tmp/proc-zombie-mnt-target")) {
        errno = ENODATA;
        goto out;
    }

    ret = 0;

out:
    if (child) {
        saved = task_current();
        task_set_current(child);
        umount_impl("/tmp/proc-zombie-mnt-target");
        task_set_current(saved);
        task_unlink_child_impl(parent, child);
        task_put(child);
    }
    reset_procfs_namespace_state();
    return ret;
}

int procfs_namespace_contract_reaped_proc_pid_mounts_disappear(void) {
    struct task *parent;
    struct task *child = NULL;
    struct task *saved;
    char path[96] = {0};
    char content[4096];
    int ret = -1;

    reset_procfs_namespace_state();

    parent = task_current();
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
    if (mkdir_impl("/tmp/proc-reaped-mnt-source", 0700) != 0 && errno != EEXIST) {
        goto out;
    }
    if (mkdir_impl("/tmp/proc-reaped-mnt-target", 0700) != 0 && errno != EEXIST) {
        goto out;
    }

    saved = task_current();
    task_set_current(child);
    if (mount("/tmp/proc-reaped-mnt-source", "/tmp/proc-reaped-mnt-target", NULL, MS_BIND, NULL) != 0) {
        task_set_current(saved);
        goto out;
    }
    task_set_current(saved);

    proc_pid_file_path(path, sizeof(path), child->pid, "/mounts");
    if (read_file_content(path, content, sizeof(content)) != 0 ||
        !contains(content, "/tmp/proc-reaped-mnt-source")) {
        errno = ENODATA;
        goto out;
    }

    task_unlink_child_impl(parent, child);
    task_put(child);
    child = NULL;

    errno = 0;
    if (read_file_content(path, content, sizeof(content)) != -1 || errno != ENOENT) {
        errno = ENODATA;
        goto out;
    }

    ret = 0;

out:
    if (child) {
        saved = task_current();
        task_set_current(child);
        umount_impl("/tmp/proc-reaped-mnt-target");
        task_set_current(saved);
        task_unlink_child_impl(parent, child);
        task_put(child);
    }
    reset_procfs_namespace_state();
    return ret;
}

int procfs_namespace_contract_reaped_proc_pid_core_views_disappear(void) {
    struct task *parent;
    struct task *child = NULL;
    char path[96] = {0};
    char content[2048];
    int fd = -1;
    int32_t reaped_pid;
    int ret = -1;

    reset_procfs_namespace_state();

    parent = task_current();
    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    child = task_create_child_impl(parent);
    if (!child) {
        return -1;
    }
    reaped_pid = child->pid;

    proc_pid_file_path(path, sizeof(path), child->pid, "/status");
    if (read_file_content(path, content, sizeof(content)) != 0) {
        goto out;
    }
    task_unlink_child_impl(parent, child);
    task_put(child);
    child = NULL;

    errno = 0;
    if (read_file_content(path, content, sizeof(content)) != -1 || errno != ENOENT) {
        errno = ENODATA;
        goto out;
    }
    proc_pid_file_path(path, sizeof(path), reaped_pid, "/stat");
    errno = 0;
    if (read_file_content(path, content, sizeof(content)) != -1 || errno != ENOENT) {
        errno = ENOMSG;
        goto out;
    }
    proc_pid_file_path(path, sizeof(path), reaped_pid, "/fd");
    errno = 0;
    fd = open_impl(path, O_RDONLY | O_DIRECTORY, 0);
    if (fd != -1 || errno != ENOENT) {
        close_impl(fd);
        fd = -1;
        errno = ENOTEMPTY;
        goto out;
    }
    proc_pid_file_path(path, sizeof(path), reaped_pid, "/task");
    errno = 0;
    fd = open_impl(path, O_RDONLY | O_DIRECTORY, 0);
    if (fd != -1 || errno != ENOENT) {
        close_impl(fd);
        fd = -1;
        errno = ENOTDIR;
        goto out;
    }

    ret = 0;

out:
    close_impl(fd);
    if (child) {
        task_unlink_child_impl(parent, child);
        task_put(child);
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

int procfs_namespace_contract_ns_directory_lists_cgroup(void) {
    char dirents[1024];
    long nread;
    int fd;

    reset_procfs_namespace_state();

    fd = open_impl("/proc/self/ns", O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0) {
        return -1;
    }
    memset(dirents, 0, sizeof(dirents));
    nread = getdents64_impl(fd, dirents, sizeof(dirents));
    close_impl(fd);
    if (nread <= 0 || !procfs_dirents_contain_name(dirents, nread, "cgroup")) {
        errno = ENODATA;
        return -1;
    }
    return 0;
}

int procfs_namespace_contract_cgroup_namespace_link_changes_after_unshare(void) {
    char before[64];
    char after[64];

    reset_procfs_namespace_state();

    if (read_link_content("/proc/self/ns/cgroup", before, sizeof(before)) != 0) {
        return -1;
    }
    if (unshare_impl(CLONE_NEWCGROUP) != 0) {
        return -1;
    }
    if (read_link_content("/proc/self/ns/cgroup", after, sizeof(after)) != 0) {
        return -1;
    }
    if (!contains(before, "cgroup:[") || !contains(after, "cgroup:[") ||
        strcmp(before, after) == 0) {
        errno = ENODATA;
        return -1;
    }
    return 0;
}
