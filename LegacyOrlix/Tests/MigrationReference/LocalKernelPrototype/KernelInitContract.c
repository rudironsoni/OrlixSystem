#include <uapi/linux/fcntl.h>
#include <uapi/linux/signal.h>
#include <uapi/asm/stat.h>
#include <uapi/linux/fs.h>
#include <uapi/linux/stat.h>
#include <uapi/linux/errno.h>
#include <linux/string.h>
#include <uapi/linux/wait.h>
#include <linux/dirent.h>

#include <stddef.h>
#include <stdint.h>

#include "fs/fdtable.h"
#include "fs/namei.h"
#include "fs/open.h"
#include "fs/readdir.h"
#include "fs/read_write.h"
#include "fs/stat.h"
#include "private/fs/fdtable_state.h"
#include "fs/vfs.h"
#include "private/fs/vfs_state.h"
#include "kernel/init.h"
#include "kernel/signal.h"
#include "kernel/task.h"
#include "private/kernel/signal_state.h"
#include "private/kernel/task_state.h"
#include "kernel/wait.h"
#include "runtime/native/registry.h"

extern int errno;

extern int kernel_exec_init(const char *preferred_path, char *const argv[], char *const envp[]);

static int wait_status_exited(int status) {
    return (status & 0x7f) == 0;
}

static int wait_status_exit_code(int status) {
    return (status >> 8) & 0xff;
}

static int buffer_contains(const char *buf, size_t len, const char *needle) {
    size_t needle_len;
    size_t i;

    if (!buf || !needle) {
        return 0;
    }

    needle_len = strlen(needle);
    if (needle_len == 0 || needle_len > len) {
        return 0;
    }

    for (i = 0; i + needle_len <= len; i++) {
        if (memcmp(buf + i, needle, needle_len) == 0) {
            return 1;
        }
    }

    return 0;
}

static int format_proc_pid_path(char *buf, size_t buf_len, int32_t pid, const char *suffix) {
    const char prefix[] = "/proc/";
    char digits[16];
    size_t pos = 0;
    size_t suffix_pos = 0;
    int value = pid;
    int digit_count = 0;

    if (!buf || !suffix || buf_len == 0 || pid <= 0) {
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

    do {
        digits[digit_count++] = (char)('0' + (value % 10));
        value /= 10;
    } while (value > 0 && digit_count < (int)sizeof(digits));

    if (value > 0) {
        errno = ENAMETOOLONG;
        return -1;
    }
    while (digit_count > 0) {
        if (pos + 1 >= buf_len) {
            errno = ENAMETOOLONG;
            return -1;
        }
        buf[pos++] = digits[--digit_count];
    }
    while (suffix[suffix_pos] != '\0') {
        if (pos + 1 >= buf_len) {
            errno = ENAMETOOLONG;
            return -1;
        }
        buf[pos++] = suffix[suffix_pos++];
    }
    buf[pos] = '\0';
    return 0;
}

static int expect_nul_vector(const char *buf, ssize_t len, const char *const expected[]) {
    size_t pos = 0;

    if (!buf || len < 0 || !expected) {
        errno = EINVAL;
        return -1;
    }

    for (int i = 0; expected[i]; i++) {
        size_t item_len = strlen(expected[i]);
        if (pos + item_len + 1 > (size_t)len) {
            errno = EPROTO;
            return -1;
        }
        if (memcmp(buf + pos, expected[i], item_len) != 0 || buf[pos + item_len] != '\0') {
            errno = EPROTO;
            return -1;
        }
        pos += item_len + 1;
    }

    if (pos != (size_t)len) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

static int dir_contains_name(int fd, const char *needle) {
    char buf[4096];
    ssize_t nread;
    size_t offset = 0;

    if (!needle) {
        errno = EINVAL;
        return -1;
    }

    nread = getdents64_impl(fd, buf, sizeof(buf));
    if (nread < 0) {
        return -1;
    }

    while (offset < (size_t)nread) {
        struct linux_dirent64 *entry = (struct linux_dirent64 *)(buf + offset);
        if (strcmp(entry->d_name, needle) == 0) {
            return 1;
        }
        if (entry->d_reclen == 0) {
            break;
        }
        offset += entry->d_reclen;
    }

    return 0;
}

static int create_exec_file(const char *path, const char *content) {
    int fd;
    size_t len;

    fd = open_impl(path, O_WRONLY | O_CREAT | O_TRUNC, 0700);
    if (fd < 0) {
        return -1;
    }

    len = content ? strlen(content) : 0;
    if (len > 0 && write_impl(fd, content, len) != (long)len) {
        int saved_errno = errno;
        close_impl(fd);
        errno = saved_errno;
        return -1;
    }

    return close_impl(fd);
}

static int reset_boot_state(void) {
    native_registry_clear();
    if (kernel_shutdown() != 0) {
        return -1;
    }
    return start_kernel();
}

static int captured_argc;
static char captured_argv[8][128];
static char captured_env0[128];

static void clear_captured_init(void) {
    captured_argc = 0;
    memset(captured_argv, 0, sizeof(captured_argv));
    memset(captured_env0, 0, sizeof(captured_env0));
}

static int native_capture_init(int argc, char **argv, char **envp) {
    int i;

    captured_argc = argc;
    for (i = 0; i < argc && i < 8; i++) {
        if (argv && argv[i]) {
            size_t len = strlen(argv[i]);
            if (len >= sizeof(captured_argv[i])) {
                errno = ENAMETOOLONG;
                return -1;
            }
            memcpy(captured_argv[i], argv[i], len + 1);
        }
    }
    if (envp && envp[0]) {
        size_t len = strlen(envp[0]);
        if (len >= sizeof(captured_env0)) {
            errno = ENAMETOOLONG;
            return -1;
        }
        memcpy(captured_env0, envp[0], len + 1);
    }
    return 0;
}

int kernel_init_contract_start_kernel_creates_current_init_task(void) {
    struct task *task = task_current();

    if (!kernel_is_booted()) {
        errno = EPROTO;
        return -1;
    }
    if (!task || task != task_init_process) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int kernel_init_contract_init_task_identity_is_linux_shaped(void) {
    struct task *task = task_current();

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    if (task->pid != 1) {
        errno = EPROTO;
        return -1;
    }
    if (task->tgid != task->pid || task->ppid != 0) {
        errno = EPROTO;
        return -1;
    }
    if (task->pgid != task->pid || task->sid != task->pid) {
        errno = EPROTO;
        return -1;
    }
    if (atomic_read(&task->state) != RUN_STATE_RUNNING) {
        errno = EPROTO;
        return -1;
    }
    if (strcmp(task->comm, "init") != 0) {
        errno = EPROTO;
        return -1;
    }
    if (task->exe[0] != '\0') {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int kernel_init_contract_init_task_cwd_and_root_are_slash(void) {
    struct task *task = task_current();

    if (!task || !task->fs) {
        errno = ESRCH;
        return -1;
    }
    if (strcmp(task->fs->pwd_path, "/") != 0) {
        errno = EPROTO;
        return -1;
    }
    if (strcmp(task->fs->root_path, "/") != 0) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int kernel_init_contract_kernel_boot_exposes_root(void) {
    struct stat st;

    if (vfs_path_fstatat(AT_FDCWD, "/", &st, 0) != 0) {
        errno = EPROTO;
        return -1;
    }
    if ((st.st_mode & S_IFMT) != S_IFDIR) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int kernel_init_contract_kernel_boot_exposes_etc_passwd(void) {
    struct stat st;

    if (vfs_path_fstatat(AT_FDCWD, "/etc", &st, 0) != 0) {
        return -1;
    }
    if (vfs_path_fstatat(AT_FDCWD, "/etc/passwd", &st, 0) != 0) {
        return -1;
    }
    if ((st.st_mode & S_IFMT) != S_IFREG) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int kernel_init_contract_kernel_boot_exposes_dev_root(void) {
    struct stat st;

    if (!vfs_path_is_synthetic("/dev")) {
        errno = EPROTO;
        return -1;
    }
    if (vfs_path_fstatat(AT_FDCWD, "/dev", &st, 0) != 0) {
        return -1;
    }
    return 0;
}

int kernel_init_contract_kernel_boot_exposes_proc_root(void) {
    struct stat st;

    if (!vfs_path_is_synthetic("/proc")) {
        errno = EPROTO;
        return -1;
    }
    if (vfs_path_fstatat(AT_FDCWD, "/proc", &st, 0) != 0) {
        return -1;
    }
    if (vfs_path_fstatat(AT_FDCWD, "/proc/self", &st, 0) != 0) {
        return -1;
    }
    return 0;
}

int kernel_init_contract_kernel_boot_exposes_sys_root_or_documents_policy(void) {
    struct stat st;

    if (!vfs_path_is_synthetic("/sys")) {
        errno = EPROTO;
        return -1;
    }
    if (vfs_path_fstatat(AT_FDCWD, "/sys", &st, 0) != 0) {
        return -1;
    }
    return 0;
}

int kernel_init_contract_kernel_boot_exposes_tmp_and_var_cache_routes(void) {
    struct stat st;

    if (vfs_backing_class_for_path("/tmp") != VFS_BACKING_TEMP) {
        errno = EPROTO;
        return -1;
    }
    if (vfs_backing_class_for_path("/var/cache") != VFS_BACKING_CACHE) {
        errno = EPROTO;
        return -1;
    }
    if (vfs_path_fstatat(AT_FDCWD, "/tmp", &st, 0) != 0) {
        return -1;
    }
    if (vfs_path_fstatat(AT_FDCWD, "/var/cache", &st, 0) != 0) {
        return -1;
    }
    return 0;
}

int kernel_init_contract_kernel_boot_stdio_policy_is_explicit(void) {
    char buf[64];
    int link_len;
    struct stat st;

    if (!fdtable_is_used_impl(0) || !fdtable_is_used_impl(1) || !fdtable_is_used_impl(2)) {
        errno = EPROTO;
        return -1;
    }
    link_len = readlink_impl("/proc/self/fd/0", buf, sizeof(buf) - 1);
    if (link_len < 0) {
        return -1;
    }
    buf[link_len] = '\0';
    if (strcmp(buf, "/dev/null") != 0) {
        errno = EPROTO;
        return -1;
    }
    link_len = readlink_impl("/proc/self/fd/1", buf, sizeof(buf) - 1);
    if (link_len < 0) {
        return -1;
    }
    buf[link_len] = '\0';
    if (strcmp(buf, "/dev/null") != 0) {
        errno = EPROTO;
        return -1;
    }
    link_len = readlink_impl("/proc/self/fd/2", buf, sizeof(buf) - 1);
    if (link_len < 0) {
        return -1;
    }
    buf[link_len] = '\0';
    if (strcmp(buf, "/dev/null") != 0) {
        errno = EPROTO;
        return -1;
    }
    if (fstat_impl(0, &st) != 0 || fstat_impl(1, &st) != 0 || fstat_impl(2, &st) != 0) {
        return -1;
    }
    return 0;
}

int kernel_init_contract_proc_self_reflects_current_task(void) {
    char buf[512];
    ssize_t nread;
    int fd;

    nread = readlink_impl("/proc/self/cwd", buf, sizeof(buf) - 1);
    if (nread < 0) {
        return -1;
    }
    buf[nread] = '\0';
    if (strcmp(buf, "/") != 0) {
        errno = EPROTO;
        return -1;
    }

    fd = open_impl("/proc/self/comm", O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }
    nread = read_impl(fd, buf, sizeof(buf) - 1);
    close_impl(fd);
    if (nread <= 0) {
        errno = EPROTO;
        return -1;
    }
    buf[nread] = '\0';
    if (strcmp(buf, "init\n") != 0) {
        errno = EPROTO;
        return -1;
    }

    fd = open_impl("/proc/self/stat", O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }
    nread = read_impl(fd, buf, sizeof(buf) - 1);
    close_impl(fd);
    if (nread <= 0) {
        errno = EPROTO;
        return -1;
    }
    buf[nread] = '\0';
    if (!buffer_contains(buf, (size_t)nread, "1 (init) R 0 1 1 ")) {
        errno = EPROTO;
        return -1;
    }

    return 0;
}

int kernel_init_contract_proc_self_fd_reflects_boot_descriptors(void) {
    int fd;
    int found0;
    int found1;
    int found2;

    fd = open_impl("/proc/self/fd", O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0) {
        return -1;
    }

    found0 = dir_contains_name(fd, "0");
    close_impl(fd);
    if (found0 != 1) {
        errno = EPROTO;
        return -1;
    }

    fd = open_impl("/proc/self/fd", O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0) {
        return -1;
    }
    found1 = dir_contains_name(fd, "1");
    close_impl(fd);
    if (found1 != 1) {
        errno = EPROTO;
        return -1;
    }

    fd = open_impl("/proc/self/fd", O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0) {
        return -1;
    }
    found2 = dir_contains_name(fd, "2");
    close_impl(fd);
    if (found2 != 1) {
        errno = EPROTO;
        return -1;
    }

    return 0;
}

int kernel_init_contract_proc_self_fdinfo_reflects_boot_descriptors(void) {
    char buf[256];
    ssize_t nread;
    int fd;

    fd = open_impl("/proc/self/fdinfo/0", O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }
    nread = read_impl(fd, buf, sizeof(buf) - 1);
    close_impl(fd);
    if (nread <= 0) {
        errno = EPROTO;
        return -1;
    }
    buf[nread] = '\0';
    if (!buffer_contains(buf, (size_t)nread, "pos:\t0\n") || !buffer_contains(buf, (size_t)nread, "flags:\t00")) {
        errno = EPROTO;
        return -1;
    }

    fd = open_impl("/proc/self/fdinfo/1", O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }
    nread = read_impl(fd, buf, sizeof(buf) - 1);
    close_impl(fd);
    if (nread <= 0) {
        errno = EPROTO;
        return -1;
    }
    buf[nread] = '\0';
    if (!buffer_contains(buf, (size_t)nread, "flags:\t01")) {
        errno = EPROTO;
        return -1;
    }

    return 0;
}

int kernel_init_contract_proc_self_exe_policy_before_exec_is_explicit(void) {
    char buf[64];
    int ret = readlink_impl("/proc/self/exe", buf, sizeof(buf));

    if (ret >= 0) {
        errno = EPROTO;
        return -1;
    }
    if (errno != ENOENT) {
        return -1;
    }
    return 0;
}

int kernel_init_contract_kernel_shutdown_and_reboot_restores_init_state(void) {
    if (kernel_shutdown() != 0) {
        return -1;
    }
    if (start_kernel() != 0) {
        return -1;
    }
    if (kernel_init_contract_start_kernel_creates_current_init_task() != 0) {
        return -1;
    }
    if (kernel_init_contract_init_task_identity_is_linux_shaped() != 0) {
        return -1;
    }
    if (kernel_init_contract_kernel_boot_stdio_policy_is_explicit() != 0) {
        return -1;
    }
    return 0;
}

int kernel_init_contract_exec_preferred_init_launches_pid1(void) {
    struct task *task;
    char *argv[] = {"synthetic-init", "--boot", NULL};
    char *envp[] = {"INIT=preferred", NULL};
    const char *const expected_cmdline[] = {"synthetic-init", "--boot", NULL};
    const char *const expected_environ[] = {"INIT=preferred", NULL};
    char buf[256];
    ssize_t nread;
    int fd = -1;
    int result = -1;

    if (reset_boot_state() != 0) {
        return -1;
    }
    clear_captured_init();
    if (native_register("/tmp/preferred-init", native_capture_init) != 0) {
        goto out;
    }
    if (kernel_exec_init("/tmp/preferred-init", argv, envp) != 0) {
        goto out;
    }

    task = task_current();
    if (!task || task->pid != 1 || strcmp(task->exe, "/tmp/preferred-init") != 0 ||
        strcmp(task->comm, "synthetic-init") != 0 || !atomic_read(&task->execed)) {
        errno = EPROTO;
        goto out;
    }
    if (captured_argc != 2 || strcmp(captured_argv[0], "synthetic-init") != 0 ||
        strcmp(captured_argv[1], "--boot") != 0 || strcmp(captured_env0, "INIT=preferred") != 0) {
        errno = EPROTO;
        goto out;
    }
    fd = open_impl("/proc/self/cmdline", O_RDONLY, 0);
    if (fd < 0) {
        goto out;
    }
    nread = read_impl(fd, buf, sizeof(buf));
    close_impl(fd);
    fd = -1;
    if (nread < 0 || expect_nul_vector(buf, nread, expected_cmdline) != 0) {
        goto out;
    }
    fd = open_impl("/proc/self/environ", O_RDONLY, 0);
    if (fd < 0) {
        goto out;
    }
    nread = read_impl(fd, buf, sizeof(buf));
    close_impl(fd);
    fd = -1;
    if (nread < 0 || expect_nul_vector(buf, nread, expected_environ) != 0) {
        goto out;
    }

    result = 0;

out:
    {
        int saved_errno = errno;
        if (fd >= 0) {
            close_impl(fd);
        }
        reset_boot_state();
        errno = saved_errno;
    }
    return result;
}

int kernel_init_contract_exec_init_search_uses_first_existing_candidate(void) {
    struct task *task;
    int result = -1;

    if (reset_boot_state() != 0) {
        return -1;
    }
    clear_captured_init();
    if (native_register("/bin/sh", native_capture_init) != 0 ||
        native_register("/sbin/init", native_capture_init) != 0) {
        goto out;
    }
    if (kernel_exec_init(NULL, NULL, NULL) != 0) {
        goto out;
    }

    task = task_current();
    if (!task || task->pid != 1 || strcmp(task->exe, "/sbin/init") != 0 ||
        strcmp(task->comm, "init") != 0 || !atomic_read(&task->execed)) {
        errno = EPROTO;
        goto out;
    }
    if (captured_argc != 1 || strcmp(captured_argv[0], "/sbin/init") != 0 ||
        strcmp(captured_env0, "HOME=/") != 0) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    {
        int saved_errno = errno;
        reset_boot_state();
        errno = saved_errno;
    }
    return result;
}

int kernel_init_contract_exec_init_returns_enoent_when_no_candidate_exists(void) {
    int result = -1;

    if (reset_boot_state() != 0) {
        return -1;
    }
    clear_captured_init();
    errno = 0;
    if (kernel_exec_init("/tmp/missing-init", NULL, NULL) != -1 || errno != ENOENT) {
        errno = EPROTO;
        goto out;
    }
    result = 0;

out:
    {
        int saved_errno = errno;
        reset_boot_state();
        errno = saved_errno;
    }
    return result;
}

int kernel_init_contract_exec_init_preserves_pid1_identity(void) {
    struct task *task;
    int result = -1;

    if (reset_boot_state() != 0) {
        return -1;
    }
    clear_captured_init();
    if (native_register("/sbin/init", native_capture_init) != 0 ||
        kernel_exec_init(NULL, NULL, NULL) != 0) {
        goto out;
    }

    task = task_current();
    if (!task || task != task_init_process || task->pid != 1 || task->tgid != 1 ||
        task->ppid != 0 || task->pgid != 1 || task->sid != 1 ||
        atomic_read(&task->state) != RUN_STATE_RUNNING) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    {
        int saved_errno = errno;
        reset_boot_state();
        errno = saved_errno;
    }
    return result;
}

int kernel_init_contract_exec_init_updates_proc_self_exe_cmdline_comm(void) {
    const char *const expected_cmdline[] = {"/sbin/init", NULL};
    const char *const expected_environ[] = {"HOME=/", "PATH=/sbin:/bin:/usr/sbin:/usr/bin", NULL};
    char buf[512];
    ssize_t nread;
    int fd;
    int result = -1;

    if (reset_boot_state() != 0) {
        return -1;
    }
    clear_captured_init();
    if (native_register("/sbin/init", native_capture_init) != 0 ||
        kernel_exec_init(NULL, NULL, NULL) != 0) {
        goto out;
    }

    nread = readlink_impl("/proc/self/exe", buf, sizeof(buf) - 1);
    if (nread < 0) {
        goto out;
    }
    buf[nread] = '\0';
    if (strcmp(buf, "/sbin/init") != 0) {
        errno = EPROTO;
        goto out;
    }

    fd = open_impl("/proc/self/cmdline", O_RDONLY, 0);
    if (fd < 0) {
        goto out;
    }
    nread = read_impl(fd, buf, sizeof(buf));
    close_impl(fd);
    if (nread < 0 || expect_nul_vector(buf, nread, expected_cmdline) != 0) {
        goto out;
    }

    fd = open_impl("/proc/self/environ", O_RDONLY, 0);
    if (fd < 0) {
        goto out;
    }
    nread = read_impl(fd, buf, sizeof(buf));
    close_impl(fd);
    if (nread < 0 || expect_nul_vector(buf, nread, expected_environ) != 0) {
        goto out;
    }

    fd = open_impl("/proc/self/comm", O_RDONLY, 0);
    if (fd < 0) {
        goto out;
    }
    nread = read_impl(fd, buf, sizeof(buf) - 1);
    close_impl(fd);
    if (nread <= 0) {
        errno = EPROTO;
        goto out;
    }
    buf[nread] = '\0';
    if (strcmp(buf, "init\n") != 0) {
        errno = EPROTO;
        goto out;
    }

    fd = open_impl("/proc/1/status", O_RDONLY, 0);
    if (fd < 0) {
        goto out;
    }
    nread = read_impl(fd, buf, sizeof(buf) - 1);
    close_impl(fd);
    if (nread <= 0 || !buffer_contains(buf, (size_t)nread, "Name:\tinit\n") ||
        !buffer_contains(buf, (size_t)nread, "Pid:\t1\n")) {
        errno = EPROTO;
        goto out;
    }

    fd = open_impl("/proc/1/stat", O_RDONLY, 0);
    if (fd < 0) {
        goto out;
    }
    nread = read_impl(fd, buf, sizeof(buf) - 1);
    close_impl(fd);
    if (nread <= 0 || !buffer_contains(buf, (size_t)nread, "1 (init)")) {
        errno = EPROTO;
        goto out;
    }

    fd = open_impl("/proc/1/statm", O_RDONLY, 0);
    if (fd < 0) {
        goto out;
    }
    nread = read_impl(fd, buf, sizeof(buf) - 1);
    close_impl(fd);
    if (nread <= 0) {
        errno = EPROTO;
        goto out;
    }

    fd = open_impl("/proc/1/mountinfo", O_RDONLY, 0);
    if (fd < 0) {
        goto out;
    }
    nread = read_impl(fd, buf, sizeof(buf) - 1);
    close_impl(fd);
    if (nread <= 0 || !buffer_contains(buf, (size_t)nread, " / ")) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    {
        int saved_errno = errno;
        reset_boot_state();
        errno = saved_errno;
    }
    return result;
}

int kernel_init_contract_exec_init_closes_cloexec_only(void) {
    int cloexec_fd = -1;
    int keep_fd = -1;
    int result = -1;

    if (reset_boot_state() != 0) {
        return -1;
    }
    clear_captured_init();
    cloexec_fd = open_impl("/dev/null", O_RDONLY | O_CLOEXEC, 0);
    keep_fd = open_impl("/dev/null", O_RDONLY, 0);
    if (cloexec_fd < 0 || keep_fd < 0) {
        goto out;
    }
    if (native_register("/sbin/init", native_capture_init) != 0 ||
        kernel_exec_init(NULL, NULL, NULL) != 0) {
        goto out;
    }
    if (fdtable_is_used_impl(cloexec_fd) || !fdtable_is_used_impl(keep_fd)) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    {
        int saved_errno = errno;
        if (keep_fd >= 0 && fdtable_is_used_impl(keep_fd)) {
            close_impl(keep_fd);
        }
        if (cloexec_fd >= 0 && fdtable_is_used_impl(cloexec_fd)) {
            close_impl(cloexec_fd);
        }
        reset_boot_state();
        errno = saved_errno;
    }
    return result;
}

int kernel_init_contract_pid1_adopts_orphaned_children(void) {
    struct task *parent = NULL;
    struct task *child = NULL;
    struct task *saved;
    char status_path[96];
    char buf[1024];
    ssize_t nread;
    int fd = -1;
    int result = -1;

    saved = task_current();
    if (!task_init_process || saved != task_init_process) {
        errno = ESRCH;
        return -1;
    }

    parent = task_create_child_impl(task_init_process);
    if (!parent) {
        return -1;
    }
    child = task_create_child_impl(parent);
    if (!child) {
        goto out;
    }

    task_set_current(parent);
    exit_impl(0);
    task_set_current(task_init_process);

    if (child->parent != task_init_process || child->ppid != 1) {
        errno = EPROTO;
        goto out;
    }

    if (format_proc_pid_path(status_path, sizeof(status_path), child->pid, "/status") != 0) {
        goto out;
    }
    fd = open_impl(status_path, O_RDONLY, 0);
    if (fd < 0) {
        goto out;
    }
    nread = read_impl(fd, buf, sizeof(buf) - 1);
    close_impl(fd);
    fd = -1;
    if (nread <= 0 || !buffer_contains(buf, (size_t)nread, "PPid:\t1\n")) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    {
        int saved_errno = errno;
        close_impl(fd);
        task_set_current(task_init_process);
        if (child) {
            task_unlink_child_impl(task_init_process, child);
            task_unlink_child_impl(parent, child);
            task_put(child);
        }
        if (parent) {
            task_unlink_child_impl(task_init_process, parent);
            task_put(parent);
        }
        task_set_current(saved);
        errno = saved_errno;
    }
    return result;
}

int kernel_init_contract_pid1_reaps_adopted_child_exit(void) {
    struct task *parent = NULL;
    struct task *child = NULL;
    struct task *saved;
    int status = 0;
    int child_pid;
    int result = -1;

    saved = task_current();
    if (!task_init_process || saved != task_init_process) {
        errno = ESRCH;
        return -1;
    }

    parent = task_create_child_impl(task_init_process);
    if (!parent) {
        return -1;
    }
    child = task_create_child_impl(parent);
    if (!child) {
        goto out;
    }
    child_pid = child->pid;

    task_set_current(parent);
    exit_impl(0);
    task_set_current(task_init_process);

    if (child->parent != task_init_process || child->ppid != 1) {
        errno = EPROTO;
        goto out;
    }

    signal_clear_pending_markers_task(child, SIGCHLD);
    signal_clear_pending_markers_task(task_init_process, SIGCHLD);

    task_set_current(child);
    exit_impl(37);
    task_set_current(task_init_process);

    if (!signal_is_pending(task_init_process, SIGCHLD)) {
        errno = ENODATA;
        goto out;
    }
    if (waitpid_impl(child_pid, &status, 0) != child_pid ||
        !wait_status_exited(status) || wait_status_exit_code(status) != 37) {
        errno = EPROTO;
        goto out;
    }
    child = NULL;

    result = 0;

out:
    {
        int saved_errno = errno;
        task_set_current(task_init_process);
        if (child) {
            task_unlink_child_impl(task_init_process, child);
            task_unlink_child_impl(parent, child);
            task_put(child);
        }
        if (parent) {
            task_unlink_child_impl(task_init_process, parent);
            task_put(parent);
        }
        task_set_current(saved);
        errno = saved_errno;
    }
    return result;
}

int kernel_init_contract_orphaned_stopped_group_gets_hup_and_cont(void) {
    struct task *parent = NULL;
    struct task *child = NULL;
    struct task *saved;
    int result = -1;

    saved = task_current();
    if (!task_init_process || saved != task_init_process) {
        errno = ESRCH;
        return -1;
    }

    parent = task_create_child_impl(task_init_process);
    if (!parent) {
        return -1;
    }

    task_set_current(parent);
    if (setsid_impl() != parent->pid) {
        task_set_current(task_init_process);
        goto out;
    }
    child = task_create_child_impl(parent);
    if (!child) {
        task_set_current(task_init_process);
        goto out;
    }
    child->pgid = child->pid;
    task_set_current(task_init_process);

    task_mark_stopped_by_signal(child, SIGSTOP);
    signal_clear_pending_markers_task(child, SIGHUP);
    signal_clear_pending_markers_task(child, SIGCONT);

    task_set_current(parent);
    exit_impl(0);
    task_set_current(task_init_process);

    if (child->parent != task_init_process || child->ppid != 1) {
        errno = EPROTO;
        goto out;
    }
    if (!signal_is_pending(child, SIGHUP)) {
        errno = ENODATA;
        goto out;
    }
    if (!signal_is_pending(child, SIGCONT)) {
        errno = ENOMSG;
        goto out;
    }
    if (atomic_read(&child->stopped)) {
        errno = ESTALE;
        goto out;
    }

    result = 0;

out:
    {
        int saved_errno = errno;
        task_set_current(task_init_process);
        if (child) {
            task_unlink_child_impl(task_init_process, child);
            task_unlink_child_impl(parent, child);
            task_put(child);
        }
        if (parent) {
            task_unlink_child_impl(task_init_process, parent);
            task_put(parent);
        }
        task_set_current(saved);
        errno = saved_errno;
    }
    return result;
}

int kernel_init_contract_exec_script_init_uses_interpreter(void) {
    const char *const expected_cmdline[] = {"/usr/bin/init-interp", "/tmp/init-script", NULL};
    char buf[256];
    ssize_t nread;
    int fd = -1;
    int result = -1;

    if (reset_boot_state() != 0) {
        return -1;
    }
    clear_captured_init();
    unlink_impl("/tmp/init-script");
    if (create_exec_file("/tmp/init-script", "#!/usr/bin/init-interp\n") != 0) {
        goto out;
    }
    if (native_register("/usr/bin/init-interp", native_capture_init) != 0 ||
        kernel_exec_init("/tmp/init-script", NULL, NULL) != 0) {
        goto out;
    }
    if (captured_argc != 2 || strcmp(captured_argv[0], "/usr/bin/init-interp") != 0 ||
        strcmp(captured_argv[1], "/tmp/init-script") != 0) {
        errno = EPROTO;
        goto out;
    }
    if (!task_current() || strcmp(task_current()->exe, "/tmp/init-script") != 0 ||
        !task_current()->exec_image ||
        task_current()->exec_image->type != EXEC_IMAGE_SCRIPT ||
        strcmp(task_current()->exec_image->interpreter, "/usr/bin/init-interp") != 0) {
        errno = EPROTO;
        goto out;
    }
    fd = open_impl("/proc/self/cmdline", O_RDONLY, 0);
    if (fd < 0) {
        goto out;
    }
    nread = read_impl(fd, buf, sizeof(buf));
    close_impl(fd);
    fd = -1;
    if (nread < 0 || expect_nul_vector(buf, nread, expected_cmdline) != 0) {
        goto out;
    }

    result = 0;

out:
    {
        int saved_errno = errno;
        if (fd >= 0) {
            close_impl(fd);
        }
        unlink_impl("/tmp/init-script");
        reset_boot_state();
        errno = saved_errno;
    }
    return result;
}
