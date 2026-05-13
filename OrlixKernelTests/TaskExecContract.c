#include <uapi/linux/errno.h>
#include <uapi/linux/capability.h>
#include <uapi/linux/fcntl.h>
#include <uapi/linux/mount.h>
#include <uapi/linux/prctl.h>
#include <uapi/linux/securebits.h>
#include <uapi/linux/stat.h>
#include <linux/string.h>
#include <linux/types.h>

#include "fs/fdtable.h"
#include "private/fs/fdtable_state.h"
#include "fs/vfs.h"
#include "private/fs/vfs_state.h"
#include "private/kernel/cred_state.h"
#include "kernel/cred.h"
#include "kernel/task.h"
#include "private/kernel/task_state.h"

extern int open_impl(const char *pathname, int flags, uint32_t mode);
extern int fcntl_impl(int fd, int cmd, ...);
extern long readlink_impl(const char *pathname, char *buf, size_t bufsiz);
extern int unlink_impl(const char *pathname);
extern int rmdir_impl(const char *pathname);
extern int mkdir_impl(const char *pathname, uint32_t mode);
extern int chmod(const char *pathname, uint32_t mode);
extern int chown(const char *pathname, __kernel_uid32_t owner, __kernel_gid32_t group);
extern int mount(const char *source, const char *target, const char *filesystemtype,
                 unsigned long mountflags, const void *data);
extern int umount_impl(const char *target);
extern int capget_impl(cap_user_header_t header, cap_user_data_t data);
extern int capset_impl(cap_user_header_t header, const cap_user_data_t data);
extern void cred_reset_to_defaults(void);
extern int errno;

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

static int task_exec_contract_cap_has_effective(const struct __user_cap_data_struct *data, int cap);
static int task_exec_contract_cap_has_permitted(const struct __user_cap_data_struct *data, int cap);

int task_exec_contract_rejects_missing_current_task(void) {
    struct task *saved = task_current();
    int result;

    task_set_current(NULL);
    errno = 0;
    result = task_exec_transition_impl("/bin/test", "test");
    task_set_current(saved);

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
    struct task *task = task_current();
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
    atomic_set(&task->execed, 0);

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

    if (!atomic_read(&task->execed)) {
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
    struct task *task = task_current();

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    atomic_set(&task->execed, 0);
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
    if (!atomic_read(&task->execed)) {
        errno = EPROTO;
        return -1;
    }

    return 0;
}

int task_exec_contract_truncates_comm_to_task_comm_len_minus_one(void) {
    struct task *task = task_current();
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
    struct task *task = task_current();
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
    state = atomic_read(&task->state);
    memcpy(cwd, task->fs->pwd_path, sizeof(cwd));
    memcpy(root, task->fs->root_path, sizeof(root));

    if (task_exec_transition_impl("/usr/bin/id", NULL) != 0) {
        return -1;
    }

    if (task->pid != pid || task->tgid != tgid || task->ppid != ppid || task->pgid != pgid || task->sid != sid) {
        errno = EPROTO;
        return -1;
    }
    if (atomic_read(&task->state) != state) {
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

    cred = cred_current();
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

    cred = cred_current();
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

int task_exec_contract_setid_exec_saved_ids_allow_drop_and_reacquire(void) {
    __kernel_uid32_t ruid = 0;
    __kernel_uid32_t euid = 0;
    __kernel_uid32_t suid = 0;
    __kernel_gid32_t rgid = 0;
    __kernel_gid32_t egid = 0;
    __kernel_gid32_t sgid = 0;
    int fd = -1;
    int ret = -1;

    cred_reset_to_defaults();
    unlink_impl("/tmp/task-exec-saved-id-file");

    fd = open_impl("/tmp/task-exec-saved-id-file", O_RDWR | O_CREAT | O_TRUNC, 0755);
    if (fd < 0) {
        goto out;
    }
    close_if_open(fd);
    fd = -1;

    if (chown("/tmp/task-exec-saved-id-file", 2000, 3000) != 0 ||
        chmod("/tmp/task-exec-saved-id-file", S_ISUID | S_ISGID | 0755) != 0 ||
        setgid_impl(1000) != 0 ||
        setuid_impl(1000) != 0 ||
        task_exec_transition_impl("/tmp/task-exec-saved-id-file", "saved-id-file") != 0 ||
        getresuid_impl(&ruid, &euid, &suid) != 0 ||
        getresgid_impl(&rgid, &egid, &sgid) != 0) {
        goto out;
    }
    if (ruid != 1000 || euid != 2000 || suid != 2000 ||
        rgid != 1000 || egid != 3000 || sgid != 3000) {
        errno = EPROTO;
        goto out;
    }

    if (seteuid_impl(1000) != 0 ||
        setegid_impl(1000) != 0 ||
        getresuid_impl(&ruid, &euid, &suid) != 0 ||
        getresgid_impl(&rgid, &egid, &sgid) != 0) {
        goto out;
    }
    if (ruid != 1000 || euid != 1000 || suid != 2000 ||
        rgid != 1000 || egid != 1000 || sgid != 3000) {
        errno = ENODATA;
        goto out;
    }

    if (seteuid_impl(2000) != 0 ||
        setegid_impl(3000) != 0 ||
        getresuid_impl(&ruid, &euid, &suid) != 0 ||
        getresgid_impl(&rgid, &egid, &sgid) != 0) {
        goto out;
    }
    if (ruid != 1000 || euid != 2000 || suid != 2000 ||
        rgid != 1000 || egid != 3000 || sgid != 3000) {
        errno = ENOMSG;
        goto out;
    }

    ret = 0;

out:
    close_if_open(fd);
    cred_reset_to_defaults();
    unlink_impl("/tmp/task-exec-saved-id-file");
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

    cred = cred_current();
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

    cred = cred_current();
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

int task_exec_contract_no_new_privs_setid_exec_cannot_reacquire_file_ids(void) {
    __kernel_uid32_t ruid = 0;
    __kernel_uid32_t euid = 0;
    __kernel_uid32_t suid = 0;
    __kernel_gid32_t rgid = 0;
    __kernel_gid32_t egid = 0;
    __kernel_gid32_t sgid = 0;
    int fd = -1;
    int ret = -1;

    cred_reset_to_defaults();
    unlink_impl("/tmp/task-exec-nnp-setid-file");

    fd = open_impl("/tmp/task-exec-nnp-setid-file", O_RDWR | O_CREAT | O_TRUNC, 0755);
    if (fd < 0) {
        goto out;
    }
    close_if_open(fd);
    fd = -1;

    if (chown("/tmp/task-exec-nnp-setid-file", 2000, 3000) != 0 ||
        chmod("/tmp/task-exec-nnp-setid-file", S_ISUID | S_ISGID | 0755) != 0 ||
        prctl_impl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0 ||
        setgid_impl(1000) != 0 ||
        setuid_impl(1000) != 0 ||
        task_exec_transition_impl("/tmp/task-exec-nnp-setid-file", "nnp-setid-file") != 0 ||
        getresuid_impl(&ruid, &euid, &suid) != 0 ||
        getresgid_impl(&rgid, &egid, &sgid) != 0) {
        goto out;
    }
    if (ruid != 1000 || euid != 1000 || suid != 1000 ||
        rgid != 1000 || egid != 1000 || sgid != 1000) {
        errno = EPROTO;
        goto out;
    }
    if (seteuid_impl(2000) != -EPERM) {
        errno = EACCES;
        goto out;
    }
    if (setegid_impl(3000) != -EPERM) {
        errno = ENODATA;
        goto out;
    }

    ret = 0;

out:
    close_if_open(fd);
    cred_reset_to_defaults();
    unlink_impl("/tmp/task-exec-nnp-setid-file");
    return ret;
}

int task_exec_contract_no_new_privs_is_irreversible(void) {
    cred_reset_to_defaults();
    if (prctl_impl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        return -1;
    }
    if (prctl_impl(PR_SET_NO_NEW_PRIVS, 0, 0, 0, 0) != -EINVAL) {
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

int task_exec_contract_file_capability_exec_grants_permitted_and_effective(void) {
    struct __user_cap_header_struct header = {
        .version = _LINUX_CAPABILITY_VERSION_3,
        .pid = 0,
    };
    struct __user_cap_data_struct data[_LINUX_CAPABILITY_U32S_3];
    uint64_t cap_setuid = 1ULL << CAP_SETUID;
    int fd = -1;
    int ret = -1;

    cred_reset_to_defaults();
    unlink_impl("/tmp/task-exec-filecap-file");

    fd = open_impl("/tmp/task-exec-filecap-file", O_RDWR | O_CREAT | O_TRUNC, 0755);
    if (fd < 0) {
        goto out;
    }
    close_if_open(fd);
    fd = -1;

    if (vfs_set_file_capabilities("/tmp/task-exec-filecap-file", cap_setuid, 0, true) != 0 ||
        setuid_impl(1000) != 0 ||
        task_exec_transition_impl("/tmp/task-exec-filecap-file", "filecap-file") != 0 ||
        capget_impl(&header, data) != 0) {
        goto out;
    }
    if (!task_exec_contract_cap_has_permitted(data, CAP_SETUID) ||
        !task_exec_contract_cap_has_effective(data, CAP_SETUID)) {
        errno = EPROTO;
        goto out;
    }
    if (!task_current() || task_current()->exec_secure != 1 || task_current()->exec_dumpable != 0) {
        errno = ENODATA;
        goto out;
    }

    ret = 0;

out:
    close_if_open(fd);
    cred_reset_to_defaults();
    vfs_remove_file_capabilities("/tmp/task-exec-filecap-file");
    unlink_impl("/tmp/task-exec-filecap-file");
    return ret;
}

int task_exec_contract_no_new_privs_blocks_file_capability_exec_gain(void) {
    struct __user_cap_header_struct header = {
        .version = _LINUX_CAPABILITY_VERSION_3,
        .pid = 0,
    };
    struct __user_cap_data_struct data[_LINUX_CAPABILITY_U32S_3];
    uint64_t cap_setuid = 1ULL << CAP_SETUID;
    int fd = -1;
    int ret = -1;

    cred_reset_to_defaults();
    unlink_impl("/tmp/task-exec-nnp-filecap-file");

    fd = open_impl("/tmp/task-exec-nnp-filecap-file", O_RDWR | O_CREAT | O_TRUNC, 0755);
    if (fd < 0) {
        goto out;
    }
    close_if_open(fd);
    fd = -1;

    if (vfs_set_file_capabilities("/tmp/task-exec-nnp-filecap-file", cap_setuid, 0, true) != 0 ||
        prctl_impl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0 ||
        setuid_impl(1000) != 0 ||
        task_exec_transition_impl("/tmp/task-exec-nnp-filecap-file", "nnp-filecap-file") != 0 ||
        capget_impl(&header, data) != 0) {
        goto out;
    }
    if (task_exec_contract_cap_has_permitted(data, CAP_SETUID) ||
        task_exec_contract_cap_has_effective(data, CAP_SETUID)) {
        errno = EPROTO;
        goto out;
    }
    if (!task_current() || task_current()->exec_secure != 0 || task_current()->exec_dumpable != 1) {
        errno = ENODATA;
        goto out;
    }

    ret = 0;

out:
    close_if_open(fd);
    cred_reset_to_defaults();
    vfs_remove_file_capabilities("/tmp/task-exec-nnp-filecap-file");
    unlink_impl("/tmp/task-exec-nnp-filecap-file");
    return ret;
}

static int task_exec_contract_cap_has_effective(const struct __user_cap_data_struct *data, int cap) {
    return (data[cap / 32].effective & (1U << (cap % 32))) != 0;
}

static int task_exec_contract_cap_has_permitted(const struct __user_cap_data_struct *data, int cap) {
    return (data[cap / 32].permitted & (1U << (cap % 32))) != 0;
}

int task_exec_contract_keepcaps_preserves_permitted_caps_after_setuid(void) {
    struct __user_cap_header_struct header = {
        .version = _LINUX_CAPABILITY_VERSION_3,
        .pid = 0,
    };
    struct __user_cap_data_struct data[_LINUX_CAPABILITY_U32S_3];

    cred_reset_to_defaults();
    if (prctl_impl(PR_SET_KEEPCAPS, 1, 0, 0, 0) != 0) {
        return -1;
    }
    if (prctl_impl(PR_GET_KEEPCAPS, 0, 0, 0, 0) != 1) {
        errno = EPROTO;
        return -1;
    }
    if (setuid_impl(1000) != 0) {
        return -1;
    }
    if (capget_impl(&header, data) != 0) {
        return -1;
    }
    if (!task_exec_contract_cap_has_permitted(data, CAP_SETUID)) {
        errno = EPROTO;
        return -1;
    }
    if (task_exec_contract_cap_has_effective(data, CAP_SETUID)) {
        errno = EPROTO;
        return -1;
    }

    data[CAP_SETUID / 32].effective |= 1U << (CAP_SETUID % 32);
    if (capset_impl(&header, data) != 0) {
        return -1;
    }
    if (setuid_impl(2000) != 0) {
        return -1;
    }
    if (getuid_impl() != 2000 || geteuid_impl() != 2000) {
        errno = EPROTO;
        return -1;
    }

    cred_reset_to_defaults();
    return 0;
}

int task_exec_contract_securebits_keepcaps_lock_is_enforced(void) {
    unsigned long locked_keepcaps = SECBIT_KEEP_CAPS | SECBIT_KEEP_CAPS_LOCKED;

    cred_reset_to_defaults();
    if (prctl_impl(PR_SET_SECUREBITS, locked_keepcaps, 0, 0, 0) != 0) {
        return -1;
    }
    if (prctl_impl(PR_GET_SECUREBITS, 0, 0, 0, 0) != (int)locked_keepcaps) {
        errno = EPROTO;
        return -1;
    }
    if (prctl_impl(PR_SET_KEEPCAPS, 0, 0, 0, 0) != -EPERM) {
        errno = EPROTO;
        return -1;
    }
    if (prctl_impl(PR_SET_SECUREBITS, SECBIT_KEEP_CAPS_LOCKED, 0, 0, 0) != -EPERM) {
        errno = EPROTO;
        return -1;
    }

    cred_reset_to_defaults();
    return 0;
}

int task_exec_contract_ambient_capability_survives_plain_exec(void) {
    struct __user_cap_header_struct header = {
        .version = _LINUX_CAPABILITY_VERSION_3,
        .pid = 0,
    };
    struct __user_cap_data_struct data[_LINUX_CAPABILITY_U32S_3];
    int fd = -1;
    int ret = -1;

    cred_reset_to_defaults();
    unlink_impl("/tmp/task-exec-ambient-file");

    fd = open_impl("/tmp/task-exec-ambient-file", O_RDWR | O_CREAT | O_TRUNC, 0755);
    if (fd < 0) {
        goto out;
    }
    close_if_open(fd);
    fd = -1;

    if (prctl_impl(PR_SET_KEEPCAPS, 1, 0, 0, 0) != 0) {
        goto out;
    }
    if (setuid_impl(1000) != 0) {
        goto out;
    }
    if (capget_impl(&header, data) != 0) {
        goto out;
    }
    data[CAP_SETUID / 32].inheritable |= 1U << (CAP_SETUID % 32);
    if (capset_impl(&header, data) != 0) {
        goto out;
    }
    if (prctl_impl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, CAP_SETUID, 0, 0) != 0) {
        goto out;
    }
    if (prctl_impl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_IS_SET, CAP_SETUID, 0, 0) != 1) {
        errno = EPROTO;
        goto out;
    }
    if (capget_impl(&header, data) != 0) {
        goto out;
    }
    data[CAP_SETUID / 32].effective &= ~(1U << (CAP_SETUID % 32));
    if (capset_impl(&header, data) != 0) {
        goto out;
    }
    if (task_exec_transition_impl("/tmp/task-exec-ambient-file", "ambient-file") != 0) {
        goto out;
    }
    if (capget_impl(&header, data) != 0) {
        goto out;
    }
    if (!task_exec_contract_cap_has_effective(data, CAP_SETUID)) {
        errno = EPROTO;
        goto out;
    }

    ret = 0;

out:
    close_if_open(fd);
    cred_reset_to_defaults();
    unlink_impl("/tmp/task-exec-ambient-file");
    return ret;
}

int task_exec_contract_no_new_privs_setuid_exec_clears_ambient_capability(void) {
    struct __user_cap_header_struct header = {
        .version = _LINUX_CAPABILITY_VERSION_3,
        .pid = 0,
    };
    struct __user_cap_data_struct data[_LINUX_CAPABILITY_U32S_3];
    int fd = -1;
    int ret = -1;

    cred_reset_to_defaults();
    unlink_impl("/tmp/task-exec-nnp-ambient-suid-file");

    fd = open_impl("/tmp/task-exec-nnp-ambient-suid-file", O_RDWR | O_CREAT | O_TRUNC, 0755);
    if (fd < 0) {
        goto out;
    }
    close_if_open(fd);
    fd = -1;

    if (chown("/tmp/task-exec-nnp-ambient-suid-file", 2000, 3000) != 0 ||
        chmod("/tmp/task-exec-nnp-ambient-suid-file", S_ISUID | 0755) != 0 ||
        prctl_impl(PR_SET_KEEPCAPS, 1, 0, 0, 0) != 0 ||
        setuid_impl(1000) != 0 ||
        capget_impl(&header, data) != 0) {
        goto out;
    }
    data[CAP_SETUID / 32].inheritable |= 1U << (CAP_SETUID % 32);
    if (capset_impl(&header, data) != 0 ||
        prctl_impl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, CAP_SETUID, 0, 0) != 0 ||
        prctl_impl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_IS_SET, CAP_SETUID, 0, 0) != 1 ||
        prctl_impl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0 ||
        task_exec_transition_impl("/tmp/task-exec-nnp-ambient-suid-file",
                                  "nnp-ambient-suid-file") != 0) {
        goto out;
    }
    if (prctl_impl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_IS_SET, CAP_SETUID, 0, 0) != 0) {
        errno = EPROTO;
        goto out;
    }
    if (capget_impl(&header, data) != 0) {
        goto out;
    }
    if (task_exec_contract_cap_has_effective(data, CAP_SETUID)) {
        errno = ENODATA;
        goto out;
    }

    ret = 0;

out:
    close_if_open(fd);
    cred_reset_to_defaults();
    unlink_impl("/tmp/task-exec-nnp-ambient-suid-file");
    return ret;
}

int task_exec_contract_nosuid_mount_blocks_setuid_exec_gain(void) {
    struct cred *cred;
    int fd = -1;
    int ret = -1;

    cred_reset_to_defaults();
    umount_impl("/tmp/task-exec-nosuid-target");
    unlink_impl("/tmp/task-exec-nosuid-source/suid-file");
    rmdir_impl("/tmp/task-exec-nosuid-source");
    rmdir_impl("/tmp/task-exec-nosuid-target");

    if (mkdir_impl("/tmp/task-exec-nosuid-source", 0700) != 0 ||
        mkdir_impl("/tmp/task-exec-nosuid-target", 0700) != 0) {
        goto out;
    }
    fd = open_impl("/tmp/task-exec-nosuid-source/suid-file", O_RDWR | O_CREAT | O_TRUNC, 0755);
    if (fd < 0) {
        goto out;
    }
    close_if_open(fd);
    fd = -1;
    if (chown("/tmp/task-exec-nosuid-source/suid-file", 2000, 3000) != 0 ||
        chmod("/tmp/task-exec-nosuid-source/suid-file", S_ISUID | 0755) != 0 ||
        mount("/tmp/task-exec-nosuid-source", "/tmp/task-exec-nosuid-target", NULL,
              MS_BIND | MS_NOSUID, NULL) != 0 ||
        setuid_impl(1000) != 0 ||
        task_exec_transition_impl("/tmp/task-exec-nosuid-target/suid-file", "nosuid-file") != 0) {
        goto out;
    }
    cred = cred_current();
    if (!cred || cred->uid != 1000 || cred->euid != 1000 || cred->suid != 1000) {
        errno = EPROTO;
        goto out;
    }

    ret = 0;

out:
    close_if_open(fd);
    cred_reset_to_defaults();
    umount_impl("/tmp/task-exec-nosuid-target");
    unlink_impl("/tmp/task-exec-nosuid-source/suid-file");
    rmdir_impl("/tmp/task-exec-nosuid-source");
    rmdir_impl("/tmp/task-exec-nosuid-target");
    return ret;
}

int task_exec_contract_nosuid_mount_blocks_file_capability_exec_gain(void) {
    struct __user_cap_header_struct header = {
        .version = _LINUX_CAPABILITY_VERSION_3,
        .pid = 0,
    };
    struct __user_cap_data_struct data[_LINUX_CAPABILITY_U32S_3];
    uint64_t cap_setuid = 1ULL << CAP_SETUID;
    int fd = -1;
    int ret = -1;

    cred_reset_to_defaults();
    umount_impl("/tmp/task-exec-nosuid-filecap-target");
    unlink_impl("/tmp/task-exec-nosuid-filecap-source/filecap-file");
    rmdir_impl("/tmp/task-exec-nosuid-filecap-source");
    rmdir_impl("/tmp/task-exec-nosuid-filecap-target");

    if (mkdir_impl("/tmp/task-exec-nosuid-filecap-source", 0700) != 0 ||
        mkdir_impl("/tmp/task-exec-nosuid-filecap-target", 0700) != 0) {
        goto out;
    }
    fd = open_impl("/tmp/task-exec-nosuid-filecap-source/filecap-file",
                   O_RDWR | O_CREAT | O_TRUNC, 0755);
    if (fd < 0) {
        goto out;
    }
    close_if_open(fd);
    fd = -1;

    if (vfs_set_file_capabilities("/tmp/task-exec-nosuid-filecap-source/filecap-file",
                                  cap_setuid, 0, true) != 0 ||
        mount("/tmp/task-exec-nosuid-filecap-source", "/tmp/task-exec-nosuid-filecap-target",
              NULL, MS_BIND | MS_NOSUID, NULL) != 0 ||
        setuid_impl(1000) != 0 ||
        task_exec_transition_impl("/tmp/task-exec-nosuid-filecap-target/filecap-file",
                                  "nosuid-filecap-file") != 0 ||
        capget_impl(&header, data) != 0) {
        goto out;
    }
    if (task_exec_contract_cap_has_permitted(data, CAP_SETUID) ||
        task_exec_contract_cap_has_effective(data, CAP_SETUID)) {
        errno = EPROTO;
        goto out;
    }
    if (!task_current() || task_current()->exec_secure != 0 || task_current()->exec_dumpable != 1) {
        errno = ENODATA;
        goto out;
    }

    ret = 0;

out:
    close_if_open(fd);
    cred_reset_to_defaults();
    vfs_remove_file_capabilities("/tmp/task-exec-nosuid-filecap-source/filecap-file");
    umount_impl("/tmp/task-exec-nosuid-filecap-target");
    unlink_impl("/tmp/task-exec-nosuid-filecap-source/filecap-file");
    rmdir_impl("/tmp/task-exec-nosuid-filecap-source");
    rmdir_impl("/tmp/task-exec-nosuid-filecap-target");
    return ret;
}

int task_exec_contract_noexec_mount_blocks_exec_transition(void) {
    int fd = -1;
    int ret = -1;

    umount_impl("/tmp/task-exec-noexec-target");
    unlink_impl("/tmp/task-exec-noexec-source/file");
    rmdir_impl("/tmp/task-exec-noexec-source");
    rmdir_impl("/tmp/task-exec-noexec-target");

    if (mkdir_impl("/tmp/task-exec-noexec-source", 0700) != 0 ||
        mkdir_impl("/tmp/task-exec-noexec-target", 0700) != 0) {
        goto out;
    }
    fd = open_impl("/tmp/task-exec-noexec-source/file", O_RDWR | O_CREAT | O_TRUNC, 0755);
    if (fd < 0) {
        goto out;
    }
    close_if_open(fd);
    fd = -1;

    if (mount("/tmp/task-exec-noexec-source", "/tmp/task-exec-noexec-target", NULL,
              MS_BIND | MS_NOEXEC, NULL) != 0) {
        goto out;
    }

    errno = 0;
    if (task_exec_transition_impl("/tmp/task-exec-noexec-target/file", "noexec-file") == 0 ||
        errno != EACCES) {
        errno = EPROTO;
        goto out;
    }

    ret = 0;

out:
    close_if_open(fd);
    umount_impl("/tmp/task-exec-noexec-target");
    unlink_impl("/tmp/task-exec-noexec-source/file");
    rmdir_impl("/tmp/task-exec-noexec-source");
    rmdir_impl("/tmp/task-exec-noexec-target");
    return ret;
}

int task_exec_contract_ambient_raise_requires_inheritable_cap(void) {
    struct __user_cap_header_struct header = {
        .version = _LINUX_CAPABILITY_VERSION_3,
        .pid = 0,
    };
    struct __user_cap_data_struct data[_LINUX_CAPABILITY_U32S_3];

    cred_reset_to_defaults();
    if (capget_impl(&header, data) != 0) {
        return -1;
    }
    data[CAP_SETUID / 32].inheritable &= ~(1U << (CAP_SETUID % 32));
    if (capset_impl(&header, data) != 0) {
        return -1;
    }
    if (prctl_impl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, CAP_SETUID, 0, 0) != -EPERM) {
        errno = EPROTO;
        return -1;
    }
    cred_reset_to_defaults();
    return 0;
}

int task_exec_contract_securebits_block_ambient_raise(void) {
    struct __user_cap_header_struct header = {
        .version = _LINUX_CAPABILITY_VERSION_3,
        .pid = 0,
    };
    struct __user_cap_data_struct data[_LINUX_CAPABILITY_U32S_3];

    cred_reset_to_defaults();
    if (capget_impl(&header, data) != 0) {
        return -1;
    }
    data[CAP_SETUID / 32].inheritable |= 1U << (CAP_SETUID % 32);
    if (capset_impl(&header, data) != 0) {
        return -1;
    }
    if (prctl_impl(PR_SET_SECUREBITS, SECBIT_NO_CAP_AMBIENT_RAISE, 0, 0, 0) != 0) {
        return -1;
    }
    if (prctl_impl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, CAP_SETUID, 0, 0) != -EPERM) {
        errno = EPROTO;
        return -1;
    }
    cred_reset_to_defaults();
    return 0;
}

int task_exec_contract_capability_bounding_drop_blocks_inheritable_and_clears_ambient(void) {
    struct __user_cap_header_struct header = {
        .version = _LINUX_CAPABILITY_VERSION_3,
        .pid = 0,
    };
    struct __user_cap_data_struct data[_LINUX_CAPABILITY_U32S_3];

    cred_reset_to_defaults();
    if (capget_impl(&header, data) != 0) {
        return -1;
    }
    data[CAP_SETUID / 32].inheritable |= 1U << (CAP_SETUID % 32);
    if (capset_impl(&header, data) != 0 ||
        prctl_impl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, CAP_SETUID, 0, 0) != 0 ||
        prctl_impl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_IS_SET, CAP_SETUID, 0, 0) != 1) {
        cred_reset_to_defaults();
        return -1;
    }
    if (prctl_impl(PR_CAPBSET_DROP, CAP_SETUID, 0, 0, 0) != 0 ||
        prctl_impl(PR_CAPBSET_READ, CAP_SETUID, 0, 0, 0) != 0 ||
        prctl_impl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_IS_SET, CAP_SETUID, 0, 0) != 0) {
        cred_reset_to_defaults();
        return -1;
    }
    if (capget_impl(&header, data) != 0) {
        cred_reset_to_defaults();
        return -1;
    }
    data[CAP_SETUID / 32].inheritable &= ~(1U << (CAP_SETUID % 32));
    if (capset_impl(&header, data) != 0) {
        cred_reset_to_defaults();
        return -1;
    }
    data[CAP_SETUID / 32].inheritable |= 1U << (CAP_SETUID % 32);
    errno = 0;
    if (capset_impl(&header, data) != -1 || errno != EPERM) {
        cred_reset_to_defaults();
        errno = EPROTO;
        return -1;
    }
    cred_reset_to_defaults();
    return 0;
}
