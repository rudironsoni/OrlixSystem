#include "PtraceContract.h"

#include <asm/ptrace.h>
#include <asm/unistd.h>
#include <linux/mman.h>
#ifdef SIGUSR1
#undef SIGUSR1
#endif
#ifdef SIGCHLD
#undef SIGCHLD
#endif
#ifdef SIGSTOP
#undef SIGSTOP
#endif
#ifdef SIGTRAP
#undef SIGTRAP
#endif
#define __ASSEMBLY__ 1
#include <asm-generic/signal.h>
#undef __ASSEMBLY__
#include <linux/elf.h>
#include <linux/ptrace.h>
#include <linux/sched.h>
#include <linux/uio.h>
#include <linux/wait.h>

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "kernel/signal.h"
#include "kernel/task.h"

extern int clone_impl(uint64_t flags);
extern long ptrace_impl(long request, __kernel_pid_t pid, void *addr, void *data);
extern __kernel_pid_t waitpid_impl(__kernel_pid_t pid, int *wstatus, int options);
extern long syscall_dispatch_impl(long number, long arg0, long arg1, long arg2,
                                  long arg3, long arg4, long arg5);

#ifndef WIFSTOPPED
#define WIFSTOPPED(status) (((status) & 0xff) == 0x7f)
#endif
#ifndef WSTOPSIG
#define WSTOPSIG(status) (((status) >> 8) & 0xff)
#endif

static void ptrace_release_child(struct task_struct *parent, struct task_struct *child) {
    if (!parent || !child) {
        return;
    }
    task_unlink_child_impl(parent, child);
    free_task(child);
}

static void ptrace_release_lookup_child(struct task_struct *parent, struct task_struct *child) {
    if (!parent || !child) {
        return;
    }
    task_unlink_child_impl(parent, child);
    free_task(child);
    free_task(child);
}

static void ptrace_clear_pending_signal(struct task_struct *task, int sig) {
    int32_t dequeued = 0;

    if (!task || !task->signal || sig < 1 || sig > KERNEL_SIG_NUM) {
        return;
    }
    while (signal_dequeue(task, NULL, &dequeued) > 0) {
        if (dequeued == sig) {
            break;
        }
    }
    task->thread_pending_signals &= ~(1ULL << ((sig - 1) & 63));
    task->signal->pending.sig[(sig - 1) >> 6] &= ~(1ULL << ((sig - 1) & 63));
    task->signal->shared_pending.sig[(sig - 1) >> 6] &= ~(1ULL << ((sig - 1) & 63));
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
        ptrace_clear_pending_signal(parent, SIGCHLD);
        errno = saved_errno;
        return -1;
    }
    ptrace_release_child(parent, child);
    ptrace_clear_pending_signal(parent, SIGCHLD);
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
    ptrace_release_lookup_child(parent, child);
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
        ptrace_release_lookup_child(parent, tracer);
        errno = ESRCH;
        return -1;
    }
    if (ptrace_impl(PTRACE_ATTACH, target->pid, NULL, NULL) == 0 &&
        ptrace_impl(PTRACE_DETACH, target->pid, NULL, NULL) == 0) {
        ret = 0;
    }
    set_current(parent);
    ptrace_release_lookup_child(tracer, target);
    ptrace_release_lookup_child(parent, tracer);
    return ret;
}

int ptrace_contract_regset_round_trips_general_registers(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child;
    struct user_pt_regs in_regs;
    struct user_pt_regs out_regs;
    struct iovec iov;
    int ret = -1;

    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    child = task_create_child_impl(parent);
    if (!child) {
        return -1;
    }
    memset(&in_regs, 0, sizeof(in_regs));
    memset(&out_regs, 0, sizeof(out_regs));
    in_regs.regs[0] = 0x1234;
    in_regs.regs[30] = 0x5678;
    in_regs.sp = 0x700000;
    in_regs.pc = 0x400000;
    in_regs.pstate = 0;
    iov.iov_base = &in_regs;
    iov.iov_len = sizeof(in_regs);
    if (ptrace_impl(PTRACE_ATTACH, child->pid, NULL, NULL) != 0 ||
        ptrace_impl(PTRACE_SETREGSET, child->pid, (void *)(uintptr_t)NT_PRSTATUS, &iov) != 0) {
        goto out;
    }
    iov.iov_base = &out_regs;
    iov.iov_len = sizeof(out_regs);
    if (ptrace_impl(PTRACE_GETREGSET, child->pid, (void *)(uintptr_t)NT_PRSTATUS, &iov) != 0 ||
        iov.iov_len != sizeof(out_regs) ||
        out_regs.regs[0] != in_regs.regs[0] ||
        out_regs.regs[30] != in_regs.regs[30] ||
        out_regs.sp != in_regs.sp ||
        out_regs.pc != in_regs.pc) {
        errno = ENODATA;
        goto out;
    }
    ret = 0;

out:
    {
        int saved_errno = errno;
        ptrace_impl(PTRACE_DETACH, child->pid, NULL, NULL);
        ptrace_release_child(parent, child);
        ptrace_clear_pending_signal(parent, SIGCHLD);
        errno = saved_errno;
    }
    return ret;
}

int ptrace_contract_syscall_trace_records_entry_and_exit(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child;
    struct ptrace_syscall_info info;
    int ret = -1;

    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    child = task_create_child_impl(parent);
    if (!child) {
        return -1;
    }
    if (ptrace_impl(PTRACE_ATTACH, child->pid, NULL, NULL) != 0 ||
        ptrace_impl(PTRACE_SYSCALL, child->pid, NULL, NULL) != 0) {
        goto out;
    }
    set_current(child);
    syscall_dispatch_impl(__NR_getpid, 0, 0, 0, 0, 0, 0);
    set_current(parent);
    memset(&info, 0, sizeof(info));
    if (ptrace_impl(PTRACE_GET_SYSCALL_INFO, child->pid, (void *)(uintptr_t)sizeof(info), &info) <= 0 ||
        info.op != PTRACE_SYSCALL_INFO_EXIT ||
        info.exit.rval != child->pid ||
        info.exit.is_error != 0) {
        errno = ENODATA;
        goto out;
    }
    ret = 0;

out:
    {
        int saved_errno = errno;
        set_current(parent);
        ptrace_impl(PTRACE_DETACH, child->pid, NULL, NULL);
        ptrace_release_child(parent, child);
        ptrace_clear_pending_signal(parent, SIGCHLD);
        errno = saved_errno;
    }
    return ret;
}

int ptrace_contract_cont_injects_pending_signal(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child;
    int ret = -1;

    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    child = task_create_child_impl(parent);
    if (!child) {
        return -1;
    }
    if (ptrace_impl(PTRACE_ATTACH, child->pid, NULL, NULL) != 0 ||
        ptrace_impl(PTRACE_CONT, child->pid, NULL, (void *)(uintptr_t)SIGUSR1) != 0) {
        goto out;
    }
    if (!signal_is_pending(child, SIGUSR1)) {
        errno = ENODATA;
        goto out;
    }
    ret = 0;

out:
    {
        int saved_errno = errno;
        ptrace_impl(PTRACE_DETACH, child->pid, NULL, NULL);
        ptrace_release_child(parent, child);
        ptrace_clear_pending_signal(parent, SIGCHLD);
        errno = saved_errno;
    }
    return ret;
}

int ptrace_contract_attach_stop_is_waitpid_visible(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child;
    int status = 0;
    int ret = -1;

    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    child = task_create_child_impl(parent);
    if (!child) {
        return -1;
    }
    if (ptrace_impl(PTRACE_ATTACH, child->pid, NULL, NULL) != 0) {
        goto out;
    }
    if (waitpid_impl(child->pid, &status, WUNTRACED | WNOHANG) != child->pid ||
        !WIFSTOPPED(status) || WSTOPSIG(status) != SIGSTOP) {
        errno = ENODATA;
        goto out;
    }
    ret = 0;

out:
    {
        int saved_errno = errno;
        ptrace_impl(PTRACE_DETACH, child->pid, NULL, NULL);
        ptrace_release_child(parent, child);
        ptrace_clear_pending_signal(parent, SIGCHLD);
        errno = saved_errno;
    }
    return ret;
}

int ptrace_contract_syscall_stop_is_waitpid_visible(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child;
    int status = 0;
    int ret = -1;

    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    child = task_create_child_impl(parent);
    if (!child) {
        return -1;
    }
    if (ptrace_impl(PTRACE_ATTACH, child->pid, NULL, NULL) != 0 ||
        waitpid_impl(child->pid, &status, WUNTRACED | WNOHANG) != child->pid ||
        ptrace_impl(PTRACE_SYSCALL, child->pid, NULL, NULL) != 0) {
        goto out;
    }
    set_current(child);
    syscall_dispatch_impl(__NR_getpid, 0, 0, 0, 0, 0, 0);
    set_current(parent);
    status = 0;
    if (waitpid_impl(child->pid, &status, WUNTRACED | WNOHANG) != child->pid ||
        !WIFSTOPPED(status) || WSTOPSIG(status) != SIGTRAP) {
        errno = ENODATA;
        goto out;
    }
    ret = 0;

out:
    {
        int saved_errno = errno;
        set_current(parent);
        ptrace_impl(PTRACE_DETACH, child->pid, NULL, NULL);
        ptrace_release_child(parent, child);
        ptrace_clear_pending_signal(parent, SIGCHLD);
        errno = saved_errno;
    }
    return ret;
}

int ptrace_contract_peek_poke_data_uses_virtual_memory(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child;
    void *mapped;
    long value = 0x1122334455667788L;
    long readback;
    int ret = -1;

    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    child = task_create_child_impl(parent);
    if (!child) {
        return -1;
    }
    set_current(child);
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 4096,
                                                      PROT_READ | PROT_WRITE,
                                                      MAP_PRIVATE | MAP_ANONYMOUS,
                                                      -1, 0);
    set_current(parent);
    if ((long)(uintptr_t)mapped < 0) {
        errno = EFAULT;
        goto out;
    }
    if (ptrace_impl(PTRACE_ATTACH, child->pid, NULL, NULL) != 0 ||
        ptrace_impl(PTRACE_POKEDATA, child->pid, mapped, (void *)(intptr_t)value) != 0) {
        goto out;
    }
    readback = ptrace_impl(PTRACE_PEEKDATA, child->pid, mapped, NULL);
    if (readback != value) {
        errno = ENODATA;
        goto out;
    }
    ret = 0;

out:
    {
        int saved_errno = errno;
        ptrace_impl(PTRACE_DETACH, child->pid, NULL, NULL);
        set_current(child);
        if ((long)(uintptr_t)mapped >= 0) {
            syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 4096, 0, 0, 0, 0);
        }
        set_current(parent);
        ptrace_release_child(parent, child);
        ptrace_clear_pending_signal(parent, SIGCHLD);
        errno = saved_errno;
    }
    return ret;
}
