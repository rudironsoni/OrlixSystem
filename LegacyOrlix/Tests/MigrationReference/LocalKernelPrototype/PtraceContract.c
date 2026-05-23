#include "PtraceContract.h"

#include <uapi/asm/ptrace.h>
#include <uapi/asm/unistd.h>
#include <uapi/linux/mman.h>
#include <uapi/linux/signal.h>
#include <uapi/linux/elf.h>
#include <uapi/linux/ptrace.h>
#include <uapi/linux/sched.h>
#include <uapi/linux/uio.h>
#include <uapi/linux/wait.h>
#include <uapi/linux/errno.h>
#include <linux/string.h>

#include <stdint.h>

#include "kernel/signal.h"
#include "private/kernel/signal_state.h"
#include "kernel/task.h"
#include "kernel/wait.h"
#include "private/kernel/task_state.h"

extern int errno;

extern long ptrace_impl(long request, __kernel_pid_t pid, void *addr, void *data);
extern int task_exec_transition_impl(const char *path, const char *argv0);
extern long syscall_dispatch_impl(long number, long arg0, long arg1, long arg2,
                                  long arg3, long arg4, long arg5);

static int wait_status_is_stopped(int status) {
    return (status & 0xff) == 0x7f;
}

static int wait_status_stop_signal(int status) {
    return (status >> 8) & 0xff;
}

static void ptrace_release_child(struct task *parent, struct task *child) {
    if (!parent || !child) {
        return;
    }
    task_unlink_child_impl(parent, child);
    task_put(child);
}

static void ptrace_release_lookup_child(struct task *parent, struct task *child) {
    if (!parent || !child) {
        return;
    }
    task_unlink_child_impl(parent, child);
    task_put(child);
    task_put(child);
}

int ptrace_contract_attach_detach_child_same_user_namespace(void) {
    struct task *parent = task_current();
    struct task *child;

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
        signal_clear_pending_task(parent, SIGCHLD);
        errno = saved_errno;
        return -1;
    }
    ptrace_release_child(parent, child);
    signal_clear_pending_task(parent, SIGCHLD);
    return 0;
}

int ptrace_contract_newuser_child_cannot_attach_parent_namespace_task(void) {
    struct task *parent = task_current();
    struct task *child;
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
    task_set_current(child);
    errno = 0;
    if (ptrace_impl(PTRACE_ATTACH, parent->pid, NULL, NULL) == -1 && errno == EPERM) {
        ret = 0;
    } else {
        errno = EPROTO;
    }
    task_set_current(parent);
    ptrace_release_lookup_child(parent, child);
    return ret;
}

int ptrace_contract_newuser_child_can_attach_same_namespace_task(void) {
    struct task *parent = task_current();
    struct task *tracer;
    struct task *target;
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
    task_set_current(tracer);
    target_pid = clone_impl(0);
    target = target_pid < 0 ? NULL : task_lookup(target_pid);
    if (!target) {
        task_set_current(parent);
        ptrace_release_lookup_child(parent, tracer);
        errno = ESRCH;
        return -1;
    }
    if (ptrace_impl(PTRACE_ATTACH, target->pid, NULL, NULL) == 0 &&
        ptrace_impl(PTRACE_DETACH, target->pid, NULL, NULL) == 0) {
        ret = 0;
    }
    task_set_current(parent);
    ptrace_release_lookup_child(tracer, target);
    ptrace_release_lookup_child(parent, tracer);
    return ret;
}

int ptrace_contract_regset_round_trips_general_registers(void) {
    struct task *parent = task_current();
    struct task *child;
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
        signal_clear_pending_task(parent, SIGCHLD);
        errno = saved_errno;
    }
    return ret;
}

int ptrace_contract_syscall_trace_records_entry_and_exit(void) {
    struct task *parent = task_current();
    struct task *child;
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
    if (waitpid_impl(child->pid, &ret, WUNTRACED | WNOHANG) != child->pid) {
        errno = ENODATA;
        goto out;
    }
    task_set_current(child);
    if (syscall_dispatch_impl(__NR_getpid, 0, 0, 0, 0, 0, 0) != -EINTR) {
        task_set_current(parent);
        errno = ENODATA;
        goto out;
    }
    task_set_current(parent);
    memset(&info, 0, sizeof(info));
    if (ptrace_impl(PTRACE_GET_SYSCALL_INFO, child->pid, (void *)(uintptr_t)sizeof(info), &info) <= 0 ||
        info.op != PTRACE_SYSCALL_INFO_ENTRY ||
        info.entry.nr != __NR_getpid) {
        errno = ENODATA;
        goto out;
    }
    if (ptrace_impl(PTRACE_SYSCALL, child->pid, NULL, NULL) != 0) {
        goto out;
    }
    task_set_current(child);
    if (syscall_dispatch_impl(__NR_getpid, 0, 0, 0, 0, 0, 0) != child->pid) {
        task_set_current(parent);
        errno = ENOMSG;
        goto out;
    }
    task_set_current(parent);
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
        task_set_current(parent);
        ptrace_impl(PTRACE_DETACH, child->pid, NULL, NULL);
        ptrace_release_child(parent, child);
        signal_clear_pending_task(parent, SIGCHLD);
        errno = saved_errno;
    }
    return ret;
}

int ptrace_contract_cont_injects_pending_signal(void) {
    struct task *parent = task_current();
    struct task *child;
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
        signal_clear_pending_task(parent, SIGCHLD);
        errno = saved_errno;
    }
    return ret;
}

int ptrace_contract_attach_stop_is_waitpid_visible(void) {
    struct task *parent = task_current();
    struct task *child;
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
        !wait_status_is_stopped(status) || wait_status_stop_signal(status) != SIGSTOP) {
        errno = ENODATA;
        goto out;
    }
    ret = 0;

out:
    {
        int saved_errno = errno;
        ptrace_impl(PTRACE_DETACH, child->pid, NULL, NULL);
        ptrace_release_child(parent, child);
        signal_clear_pending_task(parent, SIGCHLD);
        errno = saved_errno;
    }
    return ret;
}

int ptrace_contract_syscall_stop_is_waitpid_visible(void) {
    struct task *parent = task_current();
    struct task *child;
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
    task_set_current(child);
    syscall_dispatch_impl(__NR_getpid, 0, 0, 0, 0, 0, 0);
    task_set_current(parent);
    status = 0;
    if (waitpid_impl(child->pid, &status, WUNTRACED | WNOHANG) != child->pid ||
        !wait_status_is_stopped(status) || wait_status_stop_signal(status) != SIGTRAP) {
        errno = ENODATA;
        goto out;
    }
    ret = 0;

out:
    {
        int saved_errno = errno;
        task_set_current(parent);
        ptrace_impl(PTRACE_DETACH, child->pid, NULL, NULL);
        ptrace_release_child(parent, child);
        signal_clear_pending_task(parent, SIGCHLD);
        errno = saved_errno;
    }
    return ret;
}

int ptrace_contract_peek_poke_data_uses_virtual_memory(void) {
    struct task *parent = task_current();
    struct task *child;
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
    task_set_current(child);
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 4096,
                                                      PROT_READ | PROT_WRITE,
                                                      MAP_PRIVATE | MAP_ANONYMOUS,
                                                      -1, 0);
    task_set_current(parent);
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
        task_set_current(child);
        if ((long)(uintptr_t)mapped >= 0) {
            syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 4096, 0, 0, 0, 0);
        }
        task_set_current(parent);
        ptrace_release_child(parent, child);
        signal_clear_pending_task(parent, SIGCHLD);
        errno = saved_errno;
    }
    return ret;
}

int ptrace_contract_traceclone_records_event_message(void) {
    struct task *parent = task_current();
    struct task *child;
    struct task *grandchild = NULL;
    unsigned long message = 0;
    int grandchild_pid;
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
        ptrace_impl(PTRACE_SETOPTIONS, child->pid, NULL, (void *)(uintptr_t)PTRACE_O_TRACEFORK) != 0) {
        goto out;
    }
    task_set_current(child);
    grandchild_pid = clone_impl(0);
    task_set_current(parent);
    if (grandchild_pid < 0) {
        goto out;
    }
    grandchild = task_lookup(grandchild_pid);
    if (waitpid_impl(child->pid, &status, WUNTRACED | WNOHANG) != child->pid ||
        !wait_status_is_stopped(status) || wait_status_stop_signal(status) != SIGTRAP ||
        ptrace_impl(PTRACE_GETEVENTMSG, child->pid, NULL, &message) != 0 ||
        message != (unsigned long)grandchild_pid) {
        errno = ENODATA;
        goto out;
    }
    ret = 0;

out:
    {
        int saved_errno = errno;
        task_set_current(parent);
        if (grandchild) {
            task_unlink_child_impl(child, grandchild);
            task_put(grandchild);
            task_put(grandchild);
        }
        ptrace_impl(PTRACE_DETACH, child->pid, NULL, NULL);
        ptrace_release_child(parent, child);
        signal_clear_pending_task(parent, SIGCHLD);
        errno = saved_errno;
    }
    return ret;
}

int ptrace_contract_clone3_set_tid_traceclone_records_requested_pid(void) {
    struct task *parent = task_current();
    struct task *child;
    struct task *grandchild = NULL;
    struct clone_args args;
    unsigned long message = 0;
    int requested_pid = 0;
    int status = 0;
    long ret;
    int result = -1;

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
        ptrace_impl(PTRACE_SETOPTIONS, child->pid, NULL, (void *)(uintptr_t)PTRACE_O_TRACEFORK) != 0) {
        goto out;
    }

    for (int candidate = 64; candidate < 512; candidate++) {
        struct task *existing = task_lookup(candidate);
        if (!existing) {
            requested_pid = candidate;
            break;
        }
        task_put(existing);
    }
    if (requested_pid == 0) {
        errno = EAGAIN;
        goto out;
    }

    memset(&args, 0, sizeof(args));
    args.set_tid = (uint64_t)(uintptr_t)&requested_pid;
    args.set_tid_size = 1;

    task_set_current(child);
    ret = syscall_dispatch_impl(__NR_clone3, (long)(uintptr_t)&args, sizeof(args), 0, 0, 0, 0);
    task_set_current(parent);
    if (ret != requested_pid) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    grandchild = task_lookup((int32_t)ret);
    if (waitpid_impl(child->pid, &status, WUNTRACED | WNOHANG) != child->pid ||
        !wait_status_is_stopped(status) || wait_status_stop_signal(status) != SIGTRAP ||
        ptrace_impl(PTRACE_GETEVENTMSG, child->pid, NULL, &message) != 0 ||
        message != (unsigned long)requested_pid) {
        errno = ENODATA;
        goto out;
    }
    result = 0;

out:
    {
        int saved_errno = errno;
        task_set_current(parent);
        if (grandchild) {
            task_unlink_child_impl(child, grandchild);
            task_put(grandchild);
            task_put(grandchild);
        }
        ptrace_impl(PTRACE_DETACH, child->pid, NULL, NULL);
        ptrace_release_child(parent, child);
        signal_clear_pending_task(parent, SIGCHLD);
        errno = saved_errno;
    }
    return result;
}

int ptrace_contract_traceexec_records_event_message(void) {
    struct task *parent = task_current();
    struct task *child;
    unsigned long message = 0;
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
        ptrace_impl(PTRACE_SETOPTIONS, child->pid, NULL, (void *)(uintptr_t)PTRACE_O_TRACEEXEC) != 0) {
        goto out;
    }
    task_set_current(child);
    if (task_exec_transition_impl("/proc/self/status", "/proc/self/status") != 0) {
        task_set_current(parent);
        goto out;
    }
    task_set_current(parent);
    if (waitpid_impl(child->pid, &status, WUNTRACED | WNOHANG) != child->pid ||
        !wait_status_is_stopped(status) || wait_status_stop_signal(status) != SIGTRAP ||
        ptrace_impl(PTRACE_GETEVENTMSG, child->pid, NULL, &message) != 0 ||
        message != (unsigned long)child->pid) {
        errno = ENODATA;
        goto out;
    }
    ret = 0;

out:
    {
        int saved_errno = errno;
        task_set_current(parent);
        ptrace_impl(PTRACE_DETACH, child->pid, NULL, NULL);
        ptrace_release_child(parent, child);
        signal_clear_pending_task(parent, SIGCHLD);
        errno = saved_errno;
    }
    return ret;
}

int ptrace_contract_traceexit_records_event_message(void) {
    struct task *parent = task_current();
    struct task *child;
    unsigned long message = 0;
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
        ptrace_impl(PTRACE_SETOPTIONS, child->pid, NULL, (void *)(uintptr_t)PTRACE_O_TRACEEXIT) != 0) {
        goto out;
    }
    task_set_current(child);
    exit_impl(42);
    task_set_current(parent);
    if (waitpid_impl(child->pid, &status, WUNTRACED | WNOHANG) != child->pid) {
        errno = EBUSY;
        goto out;
    }
    if (!wait_status_is_stopped(status) || wait_status_stop_signal(status) != SIGTRAP) {
        errno = ENODATA;
        goto out;
    }
    if (ptrace_impl(PTRACE_GETEVENTMSG, child->pid, NULL, &message) != 0) {
        goto out;
    }
    if (message != 42UL) {
        errno = ENODATA;
        goto out;
    }
    ret = 0;

out:
    {
        int saved_errno = errno;
        task_set_current(parent);
        ptrace_impl(PTRACE_DETACH, child->pid, NULL, NULL);
        ptrace_release_child(parent, child);
        signal_clear_pending_task(parent, SIGCHLD);
        errno = saved_errno;
    }
    return ret;
}

int ptrace_contract_signal_delivery_stop_can_be_suppressed(void) {
    struct task *parent = task_current();
    struct task *child;
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
        signal_generate_task(child, SIGTERM) != 0 ||
        waitpid_impl(child->pid, &status, WUNTRACED | WNOHANG) != child->pid ||
        !wait_status_is_stopped(status) || wait_status_stop_signal(status) != SIGTERM) {
        errno = ENODATA;
        goto out;
    }
    if (atomic_read(&child->exited) || atomic_read(&child->signaled)) {
        errno = ENOMSG;
        goto out;
    }
    if (ptrace_impl(PTRACE_CONT, child->pid, NULL, NULL) != 0 ||
        atomic_read(&child->exited) || atomic_read(&child->signaled) ||
        signal_is_pending(child, SIGTERM)) {
        errno = EPROTO;
        goto out;
    }
    ret = 0;

out:
    {
        int saved_errno = errno;
        ptrace_impl(PTRACE_DETACH, child->pid, NULL, NULL);
        ptrace_release_child(parent, child);
        signal_clear_pending_task(parent, SIGCHLD);
        errno = saved_errno;
    }
    return ret;
}

int ptrace_contract_signal_delivery_stop_can_inject_signal(void) {
    struct task *parent = task_current();
    struct task *child;
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
        signal_generate_task(child, SIGTERM) != 0 ||
        waitpid_impl(child->pid, &status, WUNTRACED | WNOHANG) != child->pid ||
        !wait_status_is_stopped(status) || wait_status_stop_signal(status) != SIGTERM ||
        ptrace_impl(PTRACE_CONT, child->pid, NULL, (void *)(uintptr_t)SIGTERM) != 0) {
        errno = ENODATA;
        goto out;
    }
    if (!atomic_read(&child->exited) ||
        !atomic_read(&child->signaled) ||
        atomic_read(&child->termsig) != SIGTERM) {
        errno = ENOMSG;
        goto out;
    }
    ret = 0;

out:
    {
        int saved_errno = errno;
        ptrace_impl(PTRACE_DETACH, child->pid, NULL, NULL);
        ptrace_release_child(parent, child);
        signal_clear_pending_task(parent, SIGCHLD);
        errno = saved_errno;
    }
    return ret;
}

int ptrace_contract_event_stop_status_encodes_event(void) {
    struct task *parent = task_current();
    struct task *child;
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
        ptrace_impl(PTRACE_SETOPTIONS, child->pid, NULL, (void *)(uintptr_t)PTRACE_O_TRACEEXEC) != 0) {
        goto out;
    }
    task_set_current(child);
    if (task_exec_transition_impl("/proc/self/status", "/proc/self/status") != 0) {
        task_set_current(parent);
        goto out;
    }
    task_set_current(parent);
    if (waitpid_impl(child->pid, &status, WUNTRACED | WNOHANG) != child->pid ||
        !wait_status_is_stopped(status) || wait_status_stop_signal(status) != SIGTRAP ||
        (status >> 16) != PTRACE_EVENT_EXEC) {
        errno = ENODATA;
        goto out;
    }
    ret = 0;

out:
    {
        int saved_errno = errno;
        task_set_current(parent);
        ptrace_impl(PTRACE_DETACH, child->pid, NULL, NULL);
        ptrace_release_child(parent, child);
        signal_clear_pending_task(parent, SIGCHLD);
        errno = saved_errno;
    }
    return ret;
}
