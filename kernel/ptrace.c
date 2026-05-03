#include "ptrace.h"

#include "cred_internal.h"
#include "task.h"

#include <errno.h>
#include <stdbool.h>

#include <linux/capability.h>
#include <linux/ptrace.h>

static bool ptrace_same_credential_domain(const struct cred *tracer, const struct cred *target) {
    if (!tracer || !target || tracer->user_ns_id != target->user_ns_id) {
        return false;
    }
    return tracer->euid == target->euid &&
           tracer->egid == target->egid &&
           tracer->fsuid == target->fsuid &&
           tracer->fsgid == target->fsgid;
}

static bool ptrace_may_attach(const struct task_struct *tracer, const struct task_struct *target) {
    const struct cred *tracer_cred;
    const struct cred *target_cred;

    if (!tracer || !target || tracer == target || !tracer->cred || !target->cred) {
        return false;
    }
    tracer_cred = tracer->cred;
    target_cred = target->cred;
    if (cred_has_cap_in_user_namespace(tracer_cred, target_cred->user_ns_id, CAP_SYS_PTRACE)) {
        return true;
    }
    return target->exec_dumpable && ptrace_same_credential_domain(tracer_cred, target_cred);
}

long ptrace_impl(long request, __kernel_pid_t pid, void *addr, void *data) {
    struct task_struct *tracer = get_current();
    struct task_struct *target;

    (void)addr;
    (void)data;

    if (!tracer) {
        errno = ESRCH;
        return -1;
    }

    switch (request) {
    case PTRACE_TRACEME:
        if (!tracer->parent) {
            errno = EPERM;
            return -1;
        }
        tracer->ptracer_pid = tracer->parent->pid;
        tracer->ptrace_attached = true;
        return 0;
    case PTRACE_ATTACH:
        target = task_lookup(pid);
        if (!target) {
            errno = ESRCH;
            return -1;
        }
        if (!ptrace_may_attach(tracer, target)) {
            free_task(target);
            errno = EPERM;
            return -1;
        }
        kernel_mutex_lock(&target->lock);
        if (target->ptrace_attached && target->ptracer_pid != tracer->pid) {
            kernel_mutex_unlock(&target->lock);
            free_task(target);
            errno = EPERM;
            return -1;
        }
        target->ptracer_pid = tracer->pid;
        target->ptrace_attached = true;
        kernel_mutex_unlock(&target->lock);
        free_task(target);
        return 0;
    case PTRACE_DETACH:
        target = task_lookup(pid);
        if (!target) {
            errno = ESRCH;
            return -1;
        }
        kernel_mutex_lock(&target->lock);
        if (!target->ptrace_attached || target->ptracer_pid != tracer->pid) {
            kernel_mutex_unlock(&target->lock);
            free_task(target);
            errno = ESRCH;
            return -1;
        }
        target->ptracer_pid = 0;
        target->ptrace_attached = false;
        kernel_mutex_unlock(&target->lock);
        free_task(target);
        return 0;
    default:
        errno = ENOSYS;
        return -1;
    }
}
