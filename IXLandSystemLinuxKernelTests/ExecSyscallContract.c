#include <linux/fcntl.h>

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>
#include <string.h>

#include "fs/fdtable.h"
#include "fs/vfs.h"
#include "kernel/task.h"
#include "runtime/native/registry.h"

extern int execve(const char *pathname, char *const argv[], char *const envp[]);
extern int fexecve(int fd, char *const argv[], char *const envp[]);
extern int open_impl(const char *pathname, int flags, linux_mode_t mode);
extern long write_impl(int fd, const void *buf, size_t count);
extern long readlink_impl(const char *pathname, char *buf, size_t bufsiz);
extern int unlink_impl(const char *pathname);

static int close_if_open(int fd) {
    if (fd >= 0 && fdtable_is_used_impl(fd)) {
        return close_impl(fd);
    }
    return 0;
}

static int expect_errno(int expected) {
    if (errno != expected) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

static int native_exec_status(int argc, char **argv, char **envp) {
    (void)argc;
    (void)argv;
    (void)envp;
    return 23;
}

static int captured_argc;
static char captured_argv[8][MAX_PATH];
static char captured_env0[MAX_PATH];

static void clear_captured_exec(void) {
    captured_argc = 0;
    memset(captured_argv, 0, sizeof(captured_argv));
    memset(captured_env0, 0, sizeof(captured_env0));
}

static int native_capture_exec(int argc, char **argv, char **envp) {
    captured_argc = argc;
    for (int i = 0; i < argc && i < 8; i++) {
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
    return 37;
}

static int create_exec_file(const char *path, const char *content) {
    int fd = open_impl(path, O_WRONLY | O_CREAT | O_TRUNC, 0700);
    if (fd < 0) {
        return -1;
    }
    if (content) {
        size_t len = strlen(content);
        if (write_impl(fd, content, len) != (long)len) {
            int saved_errno = errno;
            close_impl(fd);
            errno = saved_errno;
            return -1;
        }
    }
    return close_impl(fd);
}

static int verify_state_unchanged(struct task_struct *task,
                                  const char *expected_exe,
                                  const char *expected_comm,
                                  bool expected_execed,
                                  int cloexec_fd,
                                  int keep_fd) {
    if (strcmp(task->exe, expected_exe) != 0) {
        errno = EPROTO;
        return -1;
    }
    if (strcmp(task->comm, expected_comm) != 0) {
        errno = EPROTO;
        return -1;
    }
    if (atomic_load(&task->execed) != expected_execed) {
        errno = EPROTO;
        return -1;
    }
    if ((cloexec_fd >= 0 && !fdtable_is_used_impl(cloexec_fd)) ||
        (keep_fd >= 0 && !fdtable_is_used_impl(keep_fd))) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int exec_syscall_contract_rejects_null_path_without_transition(void) {
    struct task_struct *task = get_current();

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    memcpy(task->exe, "/before", 8);
    memset(task->comm, 0, sizeof(task->comm));
    memcpy(task->comm, "before", 7);
    atomic_store(&task->execed, false);

    errno = 0;
    if (execve(NULL, NULL, NULL) != -1) {
        errno = EPROTO;
        return -1;
    }
    if (expect_errno(EFAULT) != 0) {
        return -1;
    }
    return verify_state_unchanged(task, "/before", "before", false, -1, -1);
}

int exec_syscall_contract_rejects_empty_path_without_transition(void) {
    struct task_struct *task = get_current();

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    memcpy(task->exe, "/before", 8);
    memset(task->comm, 0, sizeof(task->comm));
    memcpy(task->comm, "before", 7);
    atomic_store(&task->execed, false);

    errno = 0;
    if (execve("", NULL, NULL) != -1) {
        errno = EPROTO;
        return -1;
    }
    if (expect_errno(ENOENT) != 0) {
        return -1;
    }
    return verify_state_unchanged(task, "/before", "before", false, -1, -1);
}

int exec_syscall_contract_missing_path_preserves_state_and_cloexec_fds(void) {
    struct task_struct *task = get_current();
    int cloexec_fd = -1;
    int keep_fd = -1;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    memcpy(task->exe, "/before", 8);
    memset(task->comm, 0, sizeof(task->comm));
    memcpy(task->comm, "before", 7);
    atomic_store(&task->execed, false);

    cloexec_fd = open_impl("/dev/null", O_RDONLY | O_CLOEXEC, 0);
    if (cloexec_fd < 0) {
        return -1;
    }

    keep_fd = open_impl("/dev/zero", O_RDONLY, 0);
    if (keep_fd < 0) {
        goto out;
    }

    errno = 0;
    if (execve("/missing", NULL, NULL) != -1) {
        errno = EPROTO;
        goto out;
    }
    if (expect_errno(ENOENT) != 0) {
        goto out;
    }
    if (verify_state_unchanged(task, "/before", "before", false, cloexec_fd, keep_fd) != 0) {
        goto out;
    }

    result = 0;

out:
    close_if_open(keep_fd);
    close_if_open(cloexec_fd);
    return result;
}

int exec_syscall_contract_native_success_applies_transition_and_returns_entry_status(void) {
    struct task_struct *task = get_current();
    char *argv[] = {"custom-shell", "arg1", NULL};
    char *envp[] = {"A=B", NULL};
    int cloexec_fd = -1;
    int keep_fd = -1;
    char link_target[MAX_PATH];
    long link_len;
    int status;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    native_registry_clear();
    if (native_register("//usr//bin///env/", native_exec_status) != 0) {
        return -1;
    }

    memcpy(task->exe, "/before", 8);
    memset(task->comm, 0, sizeof(task->comm));
    memcpy(task->comm, "before", 7);
    atomic_store(&task->execed, false);

    cloexec_fd = open_impl("/dev/null", O_RDONLY | O_CLOEXEC, 0);
    if (cloexec_fd < 0) {
        goto out;
    }

    keep_fd = open_impl("/dev/zero", O_RDONLY, 0);
    if (keep_fd < 0) {
        goto out;
    }

    status = execve("//usr//bin///env/", argv, envp);
    if (status != 23) {
        errno = EPROTO;
        goto out;
    }
    if (!atomic_load(&task->execed)) {
        errno = EPROTO;
        goto out;
    }
    if (strcmp(task->exe, "/usr/bin/env") != 0) {
        errno = EPROTO;
        goto out;
    }
    if (strcmp(task->comm, "custom-shell") != 0) {
        errno = EPROTO;
        goto out;
    }
    if (fdtable_is_used_impl(cloexec_fd) || !fdtable_is_used_impl(keep_fd)) {
        errno = EPROTO;
        goto out;
    }
    if (!task->exec_image) {
        errno = EPROTO;
        goto out;
    }
    if (strcmp(task->exec_image->path, "/usr/bin/env") != 0) {
        errno = EPROTO;
        goto out;
    }

    link_len = readlink_impl("/proc/self/exe", link_target, sizeof(link_target));
    if (link_len < 0) {
        goto out;
    }
    if ((size_t)link_len >= sizeof(link_target)) {
        errno = EPROTO;
        goto out;
    }
    link_target[link_len] = '\0';
    if (strcmp(link_target, "/usr/bin/env") != 0) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    native_registry_clear();
    close_if_open(keep_fd);
    close_if_open(cloexec_fd);
    return result;
}

int exec_syscall_contract_script_uses_virtual_path_and_native_interpreter(void) {
    struct task_struct *task = get_current();
    char *argv[] = {"script-name", "arg1", NULL};
    char *envp[] = {"KEY=VALUE", NULL};
    int status;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    native_registry_clear();
    clear_captured_exec();
    unlink_impl("/tmp/exec-script-launch");

    if (create_exec_file("/tmp/exec-script-launch", "#!//usr//bin///interp/\n") != 0) {
        goto out;
    }
    if (native_register("/usr/bin/interp", native_capture_exec) != 0) {
        goto out;
    }

    status = execve("//tmp///exec-script-launch", argv, envp);
    if (status != 37) {
        errno = EPROTO;
        goto out;
    }
    if (!atomic_load(&task->execed) ||
        strcmp(task->exe, "/tmp/exec-script-launch") != 0 ||
        strcmp(task->comm, "script-name") != 0) {
        errno = EPROTO;
        goto out;
    }
    if (!task->exec_image ||
        strcmp(task->exec_image->path, "/tmp/exec-script-launch") != 0 ||
        strcmp(task->exec_image->interpreter, "/usr/bin/interp") != 0 ||
        task->exec_image->type != EXEC_IMAGE_SCRIPT) {
        errno = EPROTO;
        goto out;
    }
    if (captured_argc != 3 ||
        strcmp(captured_argv[0], "//usr//bin///interp/") != 0 ||
        strcmp(captured_argv[1], "/tmp/exec-script-launch") != 0 ||
        strcmp(captured_argv[2], "arg1") != 0 ||
        strcmp(captured_env0, "KEY=VALUE") != 0) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    native_registry_clear();
    unlink_impl("/tmp/exec-script-launch");
    return result;
}

int exec_syscall_contract_missing_script_interpreter_preserves_state(void) {
    struct task_struct *task = get_current();
    char *argv[] = {"script-name", NULL};
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    native_registry_clear();
    unlink_impl("/tmp/exec-script-missing-interpreter");
    if (create_exec_file("/tmp/exec-script-missing-interpreter", "#!/usr/bin/not-there\n") != 0) {
        goto out;
    }

    memcpy(task->exe, "/before", 8);
    memset(task->comm, 0, sizeof(task->comm));
    memcpy(task->comm, "before", 7);
    atomic_store(&task->execed, false);

    errno = 0;
    if (execve("/tmp/exec-script-missing-interpreter", argv, NULL) != -1) {
        errno = EPROTO;
        goto out;
    }
    if (expect_errno(ENOENT) != 0) {
        goto out;
    }
    if (verify_state_unchanged(task, "/before", "before", false, -1, -1) != 0) {
        goto out;
    }

    result = 0;

out:
    unlink_impl("/tmp/exec-script-missing-interpreter");
    return result;
}

int exec_syscall_contract_fexecve_uses_fd_path(void) {
    struct task_struct *task = get_current();
    char *argv[] = {"fd-native", NULL};
    char *envp[] = {"FD=1", NULL};
    int fd = -1;
    int status;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    native_registry_clear();
    clear_captured_exec();
    unlink_impl("/tmp/exec-native-fd");
    if (create_exec_file("/tmp/exec-native-fd", "") != 0) {
        goto out;
    }
    fd = open_impl("//tmp///exec-native-fd", O_RDONLY, 0);
    if (fd < 0) {
        goto out;
    }
    if (native_register("/tmp/exec-native-fd", native_capture_exec) != 0) {
        goto out;
    }

    status = fexecve(fd, argv, envp);
    if (status != 37) {
        errno = EPROTO;
        goto out;
    }
    if (!atomic_load(&task->execed) ||
        strcmp(task->exe, "/tmp/exec-native-fd") != 0 ||
        strcmp(task->comm, "fd-native") != 0 ||
        captured_argc != 1 ||
        strcmp(captured_argv[0], "fd-native") != 0 ||
        strcmp(captured_env0, "FD=1") != 0) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    native_registry_clear();
    close_if_open(fd);
    unlink_impl("/tmp/exec-native-fd");
    return result;
}

int exec_syscall_contract_fexecve_rejects_invalid_fd(void) {
    errno = 0;
    if (fexecve(240, NULL, NULL) != -1) {
        errno = EPROTO;
        return -1;
    }
    return expect_errno(EBADF);
}
