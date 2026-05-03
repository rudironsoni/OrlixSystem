#include "PtraceContract.h"

#include <linux/ptrace.h>
#include <linux/sched.h>

#include <errno.h>
#include <stdint.h>

#include "kernel/task.h"

extern int clone_impl(uint64_t flags);
extern long ptrace_impl(long request, int32_t pid, void *addr, void *data);

static void ptrace_release_child(struct task_struct *parent, struct task_struct *child) {
    if (!parent || !child) {
        return;
    }
    task_unlink_child_impl(parent, child);
    free_task(child);
}

int ptrace_contract_attach_detach_child_same_user_namespace(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child;

    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    child = task_create_child_impl(parent);
    if (!child) {
        return -1;
    }
    if (ptrace_impl(PTRACE_ATTACH, child->pid, NULL, NULL) != 0 ||
        ptrace_impl(PTRACE_DETACH, child->pid, NULL, NULL) != 0) {
        int saved_errno = errno;
        ptrace_release_child(parent, child);
        errno = saved_errno;
        return -1;
    }
    ptrace_release_child(parent, child);
    return 0;
}

int ptrace_contract_newuser_child_cannot_attach_parent_namespace_task(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child;
    int pid;
    int ret = -1;

    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    pid = clone_impl(CLONE_NEWUSER);
    if (pid < 0) {
        return -1;
    }
    child = task_lookup(pid);
    if (!child) {
        errno = ESRCH;
        return -1;
    }
    set_current(child);
    errno = 0;
    if (ptrace_impl(PTRACE_ATTACH, parent->pid, NULL, NULL) == -1 && errno == EPERM) {
        ret = 0;
    } else {
        errno = EPROTO;
    }
    set_current(parent);
    ptrace_release_child(parent, child);
    return ret;
}

int ptrace_contract_newuser_child_can_attach_same_namespace_task(void) {
    struct task_struct *parent = get_current();
    struct task_struct *tracer;
    struct task_struct *target;
    int tracer_pid;
    int target_pid;
    int ret = -1;

    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    tracer_pid = clone_impl(CLONE_NEWUSER);
    if (tracer_pid < 0) {
        return -1;
    }
    tracer = task_lookup(tracer_pid);
    if (!tracer) {
        errno = ESRCH;
        return -1;
    }
    set_current(tracer);
    target_pid = clone_impl(0);
    target = target_pid < 0 ? NULL : task_lookup(target_pid);
    if (!target) {
        set_current(parent);
        ptrace_release_child(parent, tracer);
        errno = ESRCH;
        return -1;
    }
    if (ptrace_impl(PTRACE_ATTACH, target->pid, NULL, NULL) == 0 &&
        ptrace_impl(PTRACE_DETACH, target->pid, NULL, NULL) == 0) {
        ret = 0;
    }
    set_current(parent);
    ptrace_release_child(tracer, target);
    ptrace_release_child(parent, tracer);
    return ret;
}
