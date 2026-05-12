#include "ptrace.h"

#include "cred.h"
#include "signal.h"
#include "task.h"

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/stddef.h>
#include <uapi/asm/ptrace.h>
#include <uapi/linux/audit.h>
#include <uapi/linux/capability.h>
#include <uapi/linux/elf.h>
#include <uapi/linux/ptrace.h>
#include <uapi/linux/signal.h>
#include <uapi/linux/uio.h>

static bool ptrace_same_credential_domain(const struct cred *tracer, const struct cred *target) {
    if (!tracer || !target || tracer->user_ns_id != target->user_ns_id) {
        return false;
    }
    return tracer->euid == target->euid &&
           tracer->egid == target->egid &&
           tracer->fsuid == target->fsuid &&
           tracer->fsgid == target->fsgid;
}

static bool ptrace_may_attach(const struct task *tracer, const struct task *target) {
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

int ptrace_may_access_task_impl(const struct task *tracer, const struct task *target) {
    if (!tracer || !target || !tracer->cred || !target->cred) {
        return -ESRCH;
    }
    if (!ptrace_may_attach(tracer, target)) {
        return -EPERM;
    }
    return 0;
}

static bool ptrace_is_tracer(const struct task *tracer, const struct task *target) {
    return tracer && target && target->ptrace_attached && target->ptracer_pid == tracer->pid;
}

static int ptrace_get_target(__kernel_pid_t pid, struct task **target_out) {
    struct task *tracer = task_current();
    struct task *target;

    if (!target_out) {
        return -EINVAL;
    }
    target = task_lookup(pid);
    if (!target) {
        return -ESRCH;
    }
    if (!ptrace_is_tracer(tracer, target)) {
        task_put(target);
        return -ESRCH;
    }
    *target_out = target;
    return 0;
}

static int ptrace_copy_regs_to_user(struct task *target, struct iovec *iov) {
    struct user_pt_regs regs;
    size_t ncopy;

    if (!target || !iov || !iov->iov_base) {
        return -EFAULT;
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

static int ptrace_copy_regs_from_user(struct task *target, const struct iovec *iov) {
    struct user_pt_regs regs;
    size_t ncopy;

    if (!target || !iov || !iov->iov_base) {
        return -EFAULT;
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

static long ptrace_copy_syscall_info(struct task *target, void *addr, void *data) {
    struct ptrace_syscall_info snapshot;
    struct ptrace_syscall_info *info = (struct ptrace_syscall_info *)data;
    unsigned long size = (unsigned long)addr;
    size_t ncopy;

    if (!target || !info || size == 0) {
        return -EFAULT;
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

static void ptrace_report_stop(struct task *target, __s32 sig) {
    task_mark_stopped_by_signal(target, sig);
    task_notify_parent_state_change(target);
}

static void ptrace_record_event_stop(struct task *target, __u64 event, __u64 message) {
    if (!target || !target->ptrace_attached) {
        return;
    }
    kernel_mutex_lock(&target->lock);
    target->ptrace_event = event;
    target->ptrace_event_message = message;
    kernel_mutex_unlock(&target->lock);
    ptrace_report_stop(target, SIGTRAP);
}

long ptrace_impl(long request, __kernel_pid_t pid, void *addr, void *data) {
    struct task *tracer = task_current();
    struct task *target;
    int ret;

    if (!tracer) {
        return -ESRCH;
    }

    switch (request) {
    case PTRACE_TRACEME:
        if (!tracer->parent) {
            return -EPERM;
        }
        tracer->ptracer_pid = tracer->parent->pid;
        tracer->ptrace_attached = true;
        return 0;
    case PTRACE_ATTACH:
        target = task_lookup(pid);
        if (!target) {
            return -ESRCH;
        }
        if (!ptrace_may_attach(tracer, target)) {
            task_put(target);
            return -EPERM;
        }
        kernel_mutex_lock(&target->lock);
        if (target->ptrace_attached && target->ptracer_pid != tracer->pid) {
            kernel_mutex_unlock(&target->lock);
            task_put(target);
            return -EPERM;
        }
        target->ptracer_pid = tracer->pid;
        target->ptrace_attached = true;
        target->ptrace_signal = 0;
        target->ptrace_signal_stop = 0;
        target->ptrace_signal_bypass = false;
        target->ptrace_event = 0;
        target->ptrace_event_message = 0;
        kernel_mutex_unlock(&target->lock);
        ptrace_report_stop(target, SIGSTOP);
        task_put(target);
        return 0;
    case PTRACE_DETACH:
        target = task_lookup(pid);
        if (!target) {
            return -ESRCH;
        }
        kernel_mutex_lock(&target->lock);
        if (!target->ptrace_attached || target->ptracer_pid != tracer->pid) {
            kernel_mutex_unlock(&target->lock);
            task_put(target);
            return -ESRCH;
        }
        if (data) {
            target->ptrace_signal = (__s32)(long)data;
        }
        target->ptracer_pid = 0;
        target->ptrace_attached = false;
        target->ptrace_syscall_trace = false;
        target->ptrace_syscall_exit_next = false;
        target->ptrace_signal_bypass = false;
        target->ptrace_options = 0;
        target->ptrace_event = 0;
        target->ptrace_event_message = 0;
        target->ptrace_signal_stop = 0;
        kernel_mutex_unlock(&target->lock);
        task_put(target);
        return 0;
    case PTRACE_CONT:
    case PTRACE_SYSCALL:
    {
        __s32 resume_signal;
        ret = ptrace_get_target(pid, &target);
        if (ret != 0) {
            return ret;
        }
        kernel_mutex_lock(&target->lock);
        target->ptrace_syscall_trace = request == PTRACE_SYSCALL;
        target->ptrace_signal = 0;
        target->ptrace_signal_stop = 0;
        resume_signal = (__s32)(long)data;
        kernel_mutex_unlock(&target->lock);
        if (resume_signal != 0) {
            kernel_mutex_lock(&target->lock);
            target->ptrace_signal_bypass = true;
            kernel_mutex_unlock(&target->lock);
        }
        if (resume_signal != 0 && (ret = signal_generate_task(target, resume_signal)) != 0) {
            kernel_mutex_lock(&target->lock);
            target->ptrace_signal_bypass = false;
            kernel_mutex_unlock(&target->lock);
            task_put(target);
            return ret;
        }
        if (resume_signal != 0) {
            kernel_mutex_lock(&target->lock);
            target->ptrace_signal_bypass = false;
            kernel_mutex_unlock(&target->lock);
        }
        task_put(target);
        return 0;
    }
    case PTRACE_SETOPTIONS:
        ret = ptrace_get_target(pid, &target);
        if (ret != 0) {
            return ret;
        }
        kernel_mutex_lock(&target->lock);
        target->ptrace_options = (__u64)(unsigned long)data;
        kernel_mutex_unlock(&target->lock);
        task_put(target);
        return 0;
    case PTRACE_GETEVENTMSG:
        ret = ptrace_get_target(pid, &target);
        if (ret != 0) {
            return ret;
        }
        if (!data) {
            task_put(target);
            return -EFAULT;
        }
        *(unsigned long *)data = (unsigned long)target->ptrace_event_message;
        task_put(target);
        return 0;
    case PTRACE_PEEKDATA:
    case PTRACE_PEEKTEXT:
        ret = ptrace_get_target(pid, &target);
        if (ret != 0) {
            return ret;
        }
        {
            long word = 0;
            long nread = task_read_virtual_memory_impl(target, (__u64)(unsigned long)addr,
                                                       &word, sizeof(word));
            task_put(target);
            if (nread != (long)sizeof(word)) {
                return nread < 0 ? nread : -EIO;
            }
            return word;
        }
    case PTRACE_POKEDATA:
    case PTRACE_POKETEXT:
        ret = ptrace_get_target(pid, &target);
        if (ret != 0) {
            return ret;
        }
        {
            long word = (long)data;
            long nwritten = task_write_virtual_memory_impl(target, (__u64)(unsigned long)addr,
                                                           &word, sizeof(word));
            task_put(target);
            if (nwritten != (long)sizeof(word)) {
                return nwritten < 0 ? nwritten : -EIO;
            }
            return 0;
        }
    case PTRACE_GETREGSET:
        if ((unsigned long)addr != NT_PRSTATUS) {
            return -EINVAL;
        }
        ret = ptrace_get_target(pid, &target);
        if (ret != 0) {
            return ret;
        }
        if (ptrace_copy_regs_to_user(target, (struct iovec *)data) != 0) {
            task_put(target);
            return -EFAULT;
        }
        task_put(target);
        return 0;
    case PTRACE_SETREGSET:
        if ((unsigned long)addr != NT_PRSTATUS) {
            return -EINVAL;
        }
        ret = ptrace_get_target(pid, &target);
        if (ret != 0) {
            return ret;
        }
        if (ptrace_copy_regs_from_user(target, (const struct iovec *)data) != 0) {
            task_put(target);
            return -EFAULT;
        }
        task_put(target);
        return 0;
    case PTRACE_GET_SYSCALL_INFO:
        ret = ptrace_get_target(pid, &target);
        if (ret != 0) {
            return ret;
        }
        {
            long info_ret = ptrace_copy_syscall_info(target, addr, data);
            task_put(target);
            return info_ret;
        }
    default:
        return -ENOSYS;
    }
}

int ptrace_note_syscall_entry(long number, long arg0, long arg1, long arg2,
                              long arg3, long arg4, long arg5) {
    struct task *task = task_current();

    if (!task || !task->ptrace_attached || !task->ptrace_syscall_trace) {
        return 0;
    }
    kernel_mutex_lock(&task->lock);
    if (task->ptrace_syscall_exit_next) {
        kernel_mutex_unlock(&task->lock);
        return 0;
    }
    task->ptrace_syscall_op = PTRACE_SYSCALL_INFO_ENTRY;
    task->ptrace_syscall_nr = (__u64)number;
    task->ptrace_syscall_args[0] = (__u64)arg0;
    task->ptrace_syscall_args[1] = (__u64)arg1;
    task->ptrace_syscall_args[2] = (__u64)arg2;
    task->ptrace_syscall_args[3] = (__u64)arg3;
    task->ptrace_syscall_args[4] = (__u64)arg4;
    task->ptrace_syscall_args[5] = (__u64)arg5;
    task->ptrace_syscall_exit_next = true;
    kernel_mutex_unlock(&task->lock);
    ptrace_report_stop(task, SIGTRAP);
    return 1;
}

void ptrace_note_syscall_exit(long retval) {
    struct task *task = task_current();

    if (!task || !task->ptrace_attached || !task->ptrace_syscall_trace) {
        return;
    }
    kernel_mutex_lock(&task->lock);
    task->ptrace_syscall_op = PTRACE_SYSCALL_INFO_EXIT;
    task->ptrace_syscall_retval = retval;
    task->ptrace_syscall_is_error = retval < 0;
    task->ptrace_syscall_exit_next = false;
    kernel_mutex_unlock(&task->lock);
    ptrace_report_stop(task, SIGTRAP);
}

void ptrace_note_fork_event(struct task *task, __kernel_pid_t child_pid, int clone_event) {
    __u64 option;
    __u64 event;

    if (!task || !task->ptrace_attached) {
        return;
    }
    event = clone_event ? PTRACE_EVENT_CLONE : PTRACE_EVENT_FORK;
    option = clone_event ? PTRACE_O_TRACECLONE : PTRACE_O_TRACEFORK;
    if ((task->ptrace_options & option) == 0) {
        return;
    }
    ptrace_record_event_stop(task, event, (__u64)child_pid);
}

void ptrace_rewrite_fork_event_message(struct task *task, __kernel_pid_t old_child_pid,
                                       __kernel_pid_t new_child_pid, int clone_event) {
    __u64 expected_event;

    if (!task || !task->ptrace_attached) {
        return;
    }
    expected_event = clone_event ? PTRACE_EVENT_CLONE : PTRACE_EVENT_FORK;
    kernel_mutex_lock(&task->lock);
    if (task->ptrace_event == expected_event &&
        task->ptrace_event_message == (__u64)old_child_pid) {
        task->ptrace_event_message = (__u64)new_child_pid;
    }
    kernel_mutex_unlock(&task->lock);
}

void ptrace_note_exec_event(struct task *task) {
    if (!task || !task->ptrace_attached ||
        (task->ptrace_options & PTRACE_O_TRACEEXEC) == 0) {
        return;
    }
    ptrace_record_event_stop(task, PTRACE_EVENT_EXEC, (__u64)task->pid);
}

void ptrace_note_exit_event(struct task *task, int status) {
    if (!task || !task->ptrace_attached ||
        (task->ptrace_options & PTRACE_O_TRACEEXIT) == 0) {
        return;
    }
    ptrace_record_event_stop(task, PTRACE_EVENT_EXIT, (__u64)(status & 0xff));
}

int ptrace_note_signal_delivery(struct task *task, __s32 sig) {
    if (!task || !task->ptrace_attached || task->ptrace_signal_bypass || sig == SIGKILL) {
        return 0;
    }
    task->ptrace_signal_stop = sig;
    task->ptrace_event = 0;
    task->ptrace_event_message = 0;
    ptrace_report_stop(task, sig);
    return 1;
}
