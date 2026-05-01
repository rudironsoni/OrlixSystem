#include <linux/fcntl.h>
#include <linux/prctl.h>
#include <linux/stat.h>

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "fs/fdtable.h"
#include "fs/vfs.h"
#include "kernel/cred_internal.h"
#include "kernel/task.h"

extern int open_impl(const char *pathname, int flags, linux_mode_t mode);
extern int fcntl_impl(int fd, int cmd, ...);
extern long readlink_impl(const char *pathname, char *buf, size_t bufsiz);
extern int unlink_impl(const char *pathname);
extern int chmod(const char *pathname, linux_mode_t mode);
extern int chown(const char *pathname, uid_t owner, gid_t group);
extern int setuid_impl(uid_t uid);
extern int setgid_impl(gid_t gid);
extern int prctl_impl(int option, unsigned long arg2, unsigned long arg3, unsigned long arg4, unsigned long arg5);
extern void cred_reset_to_defaults(void);

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

int task_exec_contract_rejects_missing_current_task(void) {
    struct task_struct *saved = get_current();
    int result;

    set_current(NULL);
    errno = 0;
    result = task_exec_transition_impl("/bin/test", "test");
    set_current(saved);

    if (result != -1) {
        errno = EPROTO;
        return -1;
    }
    return expect_errno(ESRCH);
}

int task_exec_contract_rejects_null_path(void) {
    errno = 0;
    if (task_exec_transition_impl(NULL, "test") != -1) {
        errno = EPROTO;
        return -1;
    }
    return expect_errno(EFAULT);
}

int task_exec_contract_rejects_empty_path(void) {
    errno = 0;
    if (task_exec_transition_impl("", "test") != -1) {
        errno = EPROTO;
        return -1;
    }
    return expect_errno(ENOENT);
}

int task_exec_contract_rejects_too_long_path(void) {
    char path[MAX_PATH + 32];

    memset(path, 'a', sizeof(path));
    path[0] = '/';
    path[sizeof(path) - 1] = '\0';

    errno = 0;
    if (task_exec_transition_impl(path, "test") != -1) {
        errno = EPROTO;
        return -1;
    }
    return expect_errno(ENAMETOOLONG);
}

int task_exec_contract_updates_task_state_and_closes_cloexec_fds(void) {
    struct task_struct *task = get_current();
    int cloexec_fd = -1;
    int keep_fd = -1;
    char link_target[MAX_PATH];
    long link_len;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    task->exe[0] = '\0';
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

    if (task_exec_transition_impl("//usr//bin///env/", "custom-shell") != 0) {
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
    close_if_open(keep_fd);
    close_if_open(cloexec_fd);
    return result;
}

int task_exec_contract_uses_basename_of_path_when_argv0_is_empty(void) {
    struct task_struct *task = get_current();

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    atomic_store(&task->execed, false);
    if (task_exec_transition_impl("bin/echo", "") != 0) {
        return -1;
    }

    if (strcmp(task->exe, "/bin/echo") != 0) {
        errno = EPROTO;
        return -1;
    }
    if (strcmp(task->comm, "echo") != 0) {
        errno = EPROTO;
        return -1;
    }
    if (!atomic_load(&task->execed)) {
        errno = EPROTO;
        return -1;
    }

    return 0;
}

int task_exec_contract_truncates_comm_to_task_comm_len_minus_one(void) {
    struct task_struct *task = get_current();
    static const char argv0[] = "1234567890abcdefXYZ";

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    if (task_exec_transition_impl("/bin/printf", argv0) != 0) {
        return -1;
    }

    if (strcmp(task->comm, "1234567890abcde") != 0) {
        errno = EPROTO;
        return -1;
    }

    return 0;
}

int task_exec_contract_preserves_task_identity_and_non_exec_state(void) {
    struct task_struct *task = get_current();
    int32_t pid;
    int32_t tgid;
    int32_t ppid;
    int32_t pgid;
    int32_t sid;
    int state;
    char cwd[MAX_PATH];
    char root[MAX_PATH];

    if (!task || !task->fs) {
        errno = ESRCH;
        return -1;
    }

    pid = task->pid;
    tgid = task->tgid;
    ppid = task->ppid;
    pgid = task->pgid;
    sid = task->sid;
    state = atomic_load(&task->state);
    memcpy(cwd, task->fs->pwd_path, sizeof(cwd));
    memcpy(root, task->fs->root_path, sizeof(root));

    if (task_exec_transition_impl("/usr/bin/id", NULL) != 0) {
        return -1;
    }

    if (task->pid != pid || task->tgid != tgid || task->ppid != ppid || task->pgid != pgid || task->sid != sid) {
        errno = EPROTO;
        return -1;
    }
    if (atomic_load(&task->state) != state) {
        errno = EPROTO;
        return -1;
    }
    if (strcmp(task->fs->pwd_path, cwd) != 0 || strcmp(task->fs->root_path, root) != 0) {
        errno = EPROTO;
        return -1;
    }

    return 0;
}

int task_exec_contract_setuid_mode_updates_virtual_effective_uid(void) {
    struct cred *cred;
    int fd = -1;
    int ret = -1;

    cred_reset_to_defaults();
    unlink_impl("/tmp/task-exec-suid-file");

    fd = open_impl("/tmp/task-exec-suid-file", O_RDWR | O_CREAT | O_TRUNC, 0755);
    if (fd < 0) {
        goto out;
    }
    close_if_open(fd);
    fd = -1;

    if (chown("/tmp/task-exec-suid-file", 2000, 3000) != 0) {
        goto out;
    }
    if (chmod("/tmp/task-exec-suid-file", S_ISUID | 0755) != 0) {
        goto out;
    }
    if (setuid_impl(1000) != 0) {
        goto out;
    }
    if (task_exec_transition_impl("/tmp/task-exec-suid-file", "suid-file") != 0) {
        goto out;
    }

    cred = get_current_cred();
    if (!cred || cred->uid != 1000 || cred->euid != 2000 || cred->suid != 2000) {
        errno = EPROTO;
        goto out;
    }

    ret = 0;

out:
    close_if_open(fd);
    cred_reset_to_defaults();
    unlink_impl("/tmp/task-exec-suid-file");
    return ret;
}

int task_exec_contract_setgid_mode_updates_virtual_effective_gid(void) {
    struct cred *cred;
    int fd = -1;
    int ret = -1;

    cred_reset_to_defaults();
    unlink_impl("/tmp/task-exec-sgid-file");

    fd = open_impl("/tmp/task-exec-sgid-file", O_RDWR | O_CREAT | O_TRUNC, 0755);
    if (fd < 0) {
        goto out;
    }
    close_if_open(fd);
    fd = -1;

    if (chown("/tmp/task-exec-sgid-file", 2000, 3000) != 0) {
        goto out;
    }
    if (chmod("/tmp/task-exec-sgid-file", S_ISGID | 0755) != 0) {
        goto out;
    }
    if (setgid_impl(1000) != 0) {
        goto out;
    }
    if (setuid_impl(1000) != 0) {
        goto out;
    }
    if (task_exec_transition_impl("/tmp/task-exec-sgid-file", "sgid-file") != 0) {
        goto out;
    }

    cred = get_current_cred();
    if (!cred || cred->gid != 1000 || cred->egid != 3000 || cred->sgid != 3000) {
        errno = EPROTO;
        goto out;
    }

    ret = 0;

out:
    close_if_open(fd);
    cred_reset_to_defaults();
    unlink_impl("/tmp/task-exec-sgid-file");
    return ret;
}

int task_exec_contract_no_new_privs_blocks_setuid_exec_gain(void) {
    struct cred *cred;
    int fd = -1;
    int ret = -1;

    cred_reset_to_defaults();
    unlink_impl("/tmp/task-exec-nnp-suid-file");

    fd = open_impl("/tmp/task-exec-nnp-suid-file", O_RDWR | O_CREAT | O_TRUNC, 0755);
    if (fd < 0) {
        goto out;
    }
    close_if_open(fd);
    fd = -1;

    if (chown("/tmp/task-exec-nnp-suid-file", 2000, 3000) != 0) {
        goto out;
    }
    if (chmod("/tmp/task-exec-nnp-suid-file", S_ISUID | 0755) != 0) {
        goto out;
    }
    if (prctl_impl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        goto out;
    }
    if (prctl_impl(PR_GET_NO_NEW_PRIVS, 0, 0, 0, 0) != 1) {
        errno = EPROTO;
        goto out;
    }
    if (setuid_impl(1000) != 0) {
        goto out;
    }
    if (task_exec_transition_impl("/tmp/task-exec-nnp-suid-file", "nnp-suid-file") != 0) {
        goto out;
    }

    cred = get_current_cred();
    if (!cred || cred->uid != 1000 || cred->euid != 1000 || cred->suid != 1000) {
        errno = EPROTO;
        goto out;
    }

    ret = 0;

out:
    close_if_open(fd);
    cred_reset_to_defaults();
    unlink_impl("/tmp/task-exec-nnp-suid-file");
    return ret;
}

int task_exec_contract_no_new_privs_blocks_setgid_exec_gain(void) {
    struct cred *cred;
    int fd = -1;
    int ret = -1;

    cred_reset_to_defaults();
    unlink_impl("/tmp/task-exec-nnp-sgid-file");

    fd = open_impl("/tmp/task-exec-nnp-sgid-file", O_RDWR | O_CREAT | O_TRUNC, 0755);
    if (fd < 0) {
        goto out;
    }
    close_if_open(fd);
    fd = -1;

    if (chown("/tmp/task-exec-nnp-sgid-file", 2000, 3000) != 0) {
        goto out;
    }
    if (chmod("/tmp/task-exec-nnp-sgid-file", S_ISGID | 0755) != 0) {
        goto out;
    }
    if (prctl_impl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        goto out;
    }
    if (setgid_impl(1000) != 0) {
        goto out;
    }
    if (setuid_impl(1000) != 0) {
        goto out;
    }
    if (task_exec_transition_impl("/tmp/task-exec-nnp-sgid-file", "nnp-sgid-file") != 0) {
        goto out;
    }

    cred = get_current_cred();
    if (!cred || cred->gid != 1000 || cred->egid != 1000 || cred->sgid != 1000) {
        errno = EPROTO;
        goto out;
    }

    ret = 0;

out:
    close_if_open(fd);
    cred_reset_to_defaults();
    unlink_impl("/tmp/task-exec-nnp-sgid-file");
    return ret;
}

int task_exec_contract_no_new_privs_is_irreversible(void) {
    cred_reset_to_defaults();
    if (prctl_impl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        return -1;
    }
    errno = 0;
    if (prctl_impl(PR_SET_NO_NEW_PRIVS, 0, 0, 0, 0) != -1 || errno != EINVAL) {
        errno = EPROTO;
        return -1;
    }
    if (prctl_impl(PR_GET_NO_NEW_PRIVS, 0, 0, 0, 0) != 1) {
        errno = EPROTO;
        return -1;
    }
    cred_reset_to_defaults();
    return 0;
}
