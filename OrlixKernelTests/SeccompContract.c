#include "SeccompContract.h"

#include <asm/unistd.h>
#include <uapi/linux/sched.h>

#include <errno.h>
#include <stdatomic.h>

#include "kernel/seccomp.h"
#include "kernel/task.h"
#include "runtime/syscall.h"

int seccomp_contract_task_errno_policy_denies_syscall_dispatch(void) {
    struct task_struct *task = get_current();
    long ret;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    if (seccomp_set_task_errno_policy(task, __NR_getpid, EPERM) != 0) {
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_getpid, 0, 0, 0, 0, 0, 0);
    seccomp_clear_task_policy(task);
    if (ret != -EPERM) {
        errno = ENODATA;
        return -1;
    }
    return 0;
}

int seccomp_contract_unmentioned_syscall_remains_allowed(void) {
    struct task_struct *task = get_current();
    long ret;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    if (seccomp_set_task_errno_policy(task, __NR_write, EPERM) != 0) {
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_getpid, 0, 0, 0, 0, 0, 0);
    seccomp_clear_task_policy(task);
    if (ret != task->pid) {
        errno = ENODATA;
        return -1;
    }
    return 0;
}

int seccomp_contract_thread_group_policy_applies_to_thread_peer(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child;
    struct task_struct *saved;
    long ret;
    int result = -1;

    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    child = task_create_child_with_flags_impl(parent, CLONE_THREAD);
    if (!child) {
        return -1;
    }
    if (seccomp_set_thread_group_errno_policy(parent, __NR_getpid, EACCES) != 0) {
        goto out;
    }
    saved = get_current();
    set_current(child);
    ret = syscall_dispatch_impl(__NR_getpid, 0, 0, 0, 0, 0, 0);
    set_current(saved);
    if (ret != -EACCES) {
        errno = ENODATA;
        goto out;
    }
    result = 0;

out:
    {
        int saved_errno = errno;
        seccomp_clear_task_policy(parent);
        seccomp_clear_task_policy(child);
        task_unlink_child_impl(parent, child);
        free_task(child);
        errno = saved_errno;
    }
    return result;
}
