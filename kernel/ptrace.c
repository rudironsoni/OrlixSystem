#include "ptrace.h"

#include "cred_internal.h"
#include "signal.h"
#include "task.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <linux/types.h>
#ifdef SIGSTOP
#undef SIGSTOP
#endif
#ifdef SIGTRAP
#undef SIGTRAP
#endif
#define __ASSEMBLY__ 1
#include <asm-generic/signal.h>
#undef __ASSEMBLY__
#include <asm/ptrace.h>
#include <linux/audit.h>
#include <linux/capability.h>
#include <linux/elf.h>
#include <linux/ptrace.h>
#include <linux/uio.h>

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

static bool ptrace_is_tracer(const struct task_struct *tracer, const struct task_struct *target) {
    return tracer && target && target->ptrace_attached && target->ptracer_pid == tracer->pid;
}

static int ptrace_get_target(__kernel_pid_t pid, struct task_struct **target_out) {
    struct task_struct *tracer = get_current();
    struct task_struct *target;

    if (!target_out) {
        errno = EINVAL;
        return -1;
    }
    target = task_lookup(pid);
    if (!target) {
        errno = ESRCH;
        return -1;
    }
    if (!ptrace_is_tracer(tracer, target)) {
        free_task(target);
        errno = ESRCH;
        return -1;
    }
    *target_out = target;
    return 0;
}

static int ptrace_copy_regs_to_user(struct task_struct *target, struct iovec *iov) {
    struct user_pt_regs regs;
    size_t ncopy;

    if (!target || !iov || !iov->iov_base) {
        errno = EFAULT;
        return -1;
    }
    memset(&regs, 0, sizeof(regs));
    memcpy(regs.regs, target->ptrace_regs, sizeof(regs.regs));
    regs.sp = target->ptrace_sp;
    regs.pc = target->ptrace_pc;
    regs.pstate = target->ptrace_pstate;
    ncopy = iov->iov_len < sizeof(regs) ? iov->iov_len : sizeof(regs);
    memcpy(iov->iov_base, &regs, ncopy);
    iov->iov_len = ncopy;
    return 0;
}

static int ptrace_copy_regs_from_user(struct task_struct *target, const struct iovec *iov) {
    struct user_pt_regs regs;
    size_t ncopy;

    if (!target || !iov || !iov->iov_base) {
        errno = EFAULT;
        return -1;
    }
    memset(&regs, 0, sizeof(regs));
    ncopy = iov->iov_len < sizeof(regs) ? iov->iov_len : sizeof(regs);
    memcpy(&regs, iov->iov_base, ncopy);
    memcpy(target->ptrace_regs, regs.regs, sizeof(target->ptrace_regs));
    target->ptrace_sp = regs.sp;
    target->ptrace_pc = regs.pc;
    target->ptrace_pstate = regs.pstate;
    return 0;
}

static long ptrace_copy_syscall_info(struct task_struct *target, void *addr, void *data) {
    struct ptrace_syscall_info snapshot;
    struct ptrace_syscall_info *info = (struct ptrace_syscall_info *)data;
    unsigned long size = (unsigned long)(uintptr_t)addr;
    size_t ncopy;

    if (!target || !info || size == 0) {
        errno = EFAULT;
        return -1;
    }
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.op = target->ptrace_syscall_op;
    snapshot.arch = AUDIT_ARCH_AARCH64;
    snapshot.instruction_pointer = target->ptrace_pc;
    snapshot.stack_pointer = target->ptrace_sp;
    if (target->ptrace_syscall_op == PTRACE_SYSCALL_INFO_ENTRY) {
        snapshot.entry.nr = target->ptrace_syscall_nr;
        memcpy(snapshot.entry.args, target->ptrace_syscall_args, sizeof(snapshot.entry.args));
    } else if (target->ptrace_syscall_op == PTRACE_SYSCALL_INFO_EXIT) {
        snapshot.exit.rval = target->ptrace_syscall_retval;
        snapshot.exit.is_error = target->ptrace_syscall_is_error ? 1 : 0;
    }
    ncopy = size < sizeof(snapshot) ? size : sizeof(snapshot);
    memcpy(info, &snapshot, ncopy);
    return (long)ncopy;
}

static void ptrace_report_stop(struct task_struct *target, int32_t sig) {
    task_mark_stopped_by_signal(target, sig);
    task_notify_parent_state_change(target);
}

long ptrace_impl(long request, __kernel_pid_t pid, void *addr, void *data) {
    struct task_struct *tracer = get_current();
    struct task_struct *target;

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
        target->ptrace_signal = 0;
        kernel_mutex_unlock(&target->lock);
        ptrace_report_stop(target, SIGSTOP);
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
        if (data) {
            target->ptrace_signal = (int32_t)(intptr_t)data;
        }
        target->ptracer_pid = 0;
        target->ptrace_attached = false;
        target->ptrace_syscall_trace = false;
        kernel_mutex_unlock(&target->lock);
        free_task(target);
        return 0;
    case PTRACE_CONT:
    case PTRACE_SYSCALL:
        if (ptrace_get_target(pid, &target) != 0) {
            return -1;
        }
        kernel_mutex_lock(&target->lock);
        target->ptrace_syscall_trace = request == PTRACE_SYSCALL;
        target->ptrace_signal = (int32_t)(intptr_t)data;
        kernel_mutex_unlock(&target->lock);
        if (data && signal_generate_task(target, (int32_t)(intptr_t)data) != 0) {
            int saved_errno = errno;
            free_task(target);
            errno = saved_errno;
            return -1;
        }
        free_task(target);
        return 0;
    case PTRACE_PEEKDATA:
    case PTRACE_PEEKTEXT:
        if (ptrace_get_target(pid, &target) != 0) {
            return -1;
        }
        {
            long word = 0;
            long nread = task_read_virtual_memory_impl(target, (uint64_t)(uintptr_t)addr,
                                                       &word, sizeof(word));
            int saved_errno = errno;
            free_task(target);
            if (nread != (long)sizeof(word)) {
                errno = nread < 0 ? saved_errno : EIO;
                return -1;
            }
            return word;
        }
    case PTRACE_POKEDATA:
    case PTRACE_POKETEXT:
        if (ptrace_get_target(pid, &target) != 0) {
            return -1;
        }
        {
            long word = (long)(intptr_t)data;
            long nwritten = task_write_virtual_memory_impl(target, (uint64_t)(uintptr_t)addr,
                                                           &word, sizeof(word));
            int saved_errno = errno;
            free_task(target);
            if (nwritten != (long)sizeof(word)) {
                errno = nwritten < 0 ? saved_errno : EIO;
                return -1;
            }
            return 0;
        }
    case PTRACE_GETREGSET:
        if ((long)(uintptr_t)addr != NT_PRSTATUS) {
            errno = EINVAL;
            return -1;
        }
        if (ptrace_get_target(pid, &target) != 0) {
            return -1;
        }
        if (ptrace_copy_regs_to_user(target, (struct iovec *)data) != 0) {
            free_task(target);
            return -1;
        }
        free_task(target);
        return 0;
    case PTRACE_SETREGSET:
        if ((long)(uintptr_t)addr != NT_PRSTATUS) {
            errno = EINVAL;
            return -1;
        }
        if (ptrace_get_target(pid, &target) != 0) {
            return -1;
        }
        if (ptrace_copy_regs_from_user(target, (const struct iovec *)data) != 0) {
            free_task(target);
            return -1;
        }
        free_task(target);
        return 0;
    case PTRACE_GET_SYSCALL_INFO:
        if (ptrace_get_target(pid, &target) != 0) {
            return -1;
        }
        {
            long ret = ptrace_copy_syscall_info(target, addr, data);
            free_task(target);
            return ret;
        }
    default:
        errno = ENOSYS;
        return -1;
    }
}

void ptrace_note_syscall_entry(long number, long arg0, long arg1, long arg2,
                               long arg3, long arg4, long arg5) {
    struct task_struct *task = get_current();

    if (!task || !task->ptrace_attached || !task->ptrace_syscall_trace) {
        return;
    }
    kernel_mutex_lock(&task->lock);
    task->ptrace_syscall_op = PTRACE_SYSCALL_INFO_ENTRY;
    task->ptrace_syscall_nr = (uint64_t)number;
    task->ptrace_syscall_args[0] = (uint64_t)arg0;
    task->ptrace_syscall_args[1] = (uint64_t)arg1;
    task->ptrace_syscall_args[2] = (uint64_t)arg2;
    task->ptrace_syscall_args[3] = (uint64_t)arg3;
    task->ptrace_syscall_args[4] = (uint64_t)arg4;
    task->ptrace_syscall_args[5] = (uint64_t)arg5;
    kernel_mutex_unlock(&task->lock);
    ptrace_report_stop(task, SIGTRAP);
}

void ptrace_note_syscall_exit(long retval) {
    struct task_struct *task = get_current();

    if (!task || !task->ptrace_attached || !task->ptrace_syscall_trace) {
        return;
    }
    kernel_mutex_lock(&task->lock);
    task->ptrace_syscall_op = PTRACE_SYSCALL_INFO_EXIT;
    task->ptrace_syscall_retval = retval;
    task->ptrace_syscall_is_error = retval < 0;
    kernel_mutex_unlock(&task->lock);
    ptrace_report_stop(task, SIGTRAP);
}
