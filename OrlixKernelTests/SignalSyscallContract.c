#include "SignalSyscallContract.h"

#include <uapi/asm/signal.h>
#include <uapi/asm/sigcontext.h>
#include <uapi/asm/ucontext.h>
#include <uapi/asm/unistd.h>
#include <uapi/linux/mman.h>
#include <uapi/linux/sched.h>
#include <uapi/linux/signal.h>
#include <uapi/linux/time_types.h>
#include <uapi/linux/errno.h>

#include <stdint.h>

#include "fs/fdtable.h"
#include "runtime/syscall.h"
#include "kernel/signal.h"
#include "private/kernel/signal_state.h"
#include "private/kernel/cred_state.h"
#include "kernel/cred.h"
#include "kernel/task.h"
#include "private/kernel/task_state.h"

extern int errno;

extern int nanosleep_impl(const struct __kernel_timespec *req, struct __kernel_timespec *rem);

static void signal_contract_disable_altstack(void) {
    stack_t disabled = {0};
    disabled.ss_flags = SS_DISABLE;
    syscall_dispatch_impl(__NR_sigaltstack, (long)(uintptr_t)&disabled, 0, 0, 0, 0, 0);
}

static sigset_t signal_contract_mask_all_except(int signo) {
    sigset_t mask = {0};

    sigfillset(&mask);
    if (signo >= 1 && signo <= KERNEL_SIG_NUM) {
        sigdelset(&mask, signo);
    }
    return mask;
}

static int signal_contract_sigset_contains(const sigset_t *set, int signo) {
    if (!set) {
        return 0;
    }
    return sigismember((sigset_t *)set, signo) != 0;
}

static int signal_contract_read_blocked(struct task *task, sigset_t *mask) {
    int ret = signal_blocked_get_task(task, mask);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }
    return 0;
}

static int signal_contract_write_blocked(struct task *task, const sigset_t *mask) {
    int ret = signal_blocked_set_task(task, mask);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }
    return 0;
}

static int signal_contract_clear_blocked(struct task *task) {
    int ret = signal_blocked_clear_task(task);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }
    return 0;
}

static void signal_contract_set_task_identity(struct task *task, uint32_t uid) {
    if (!task || !task->cred) {
        return;
    }

    task->cred->uid = uid;
    task->cred->euid = uid;
    task->cred->suid = uid;
    task->cred->fsuid = uid;
    task->cred->gid = uid;
    task->cred->egid = uid;
    task->cred->sgid = uid;
    task->cred->fsgid = uid;

    if (uid != 0) {
        task->cred->cap_permitted = 0;
        task->cred->cap_effective = 0;
        task->cred->cap_inheritable = 0;
        task->cred->cap_ambient = 0;
    }
}

int signal_syscall_contract_pidfd_send_signal_obeys_linux_targeting_rules(void) {
    struct task *parent = task_current();
    struct task *child = NULL;
    struct task *thread = NULL;
    sigset_t saved_parent_blocked;
    long ret;
    int32_t thread_pid;
    int pidfd = -1;
    int thread_pidfd = -1;
    const int signo = SIGUSR1;
    int32_t dequeued = 0;

    if (!parent || !parent->signal) {
        errno = ESRCH;
        return -1;
    }

    if (signal_contract_read_blocked(parent, &saved_parent_blocked) != 0) {
        goto fail;
    }

    child = task_create_child_impl(parent);
    if (!child || !child->signal) {
        errno = child ? EPROTO : errno;
        goto fail;
    }

    {
        sigset_t child_blocked = {0};
        sigaddset(&child_blocked, signo);
        if (signal_contract_write_blocked(child, &child_blocked) != 0) {
            goto fail;
        }
    }
    signal_clear_queued_task(child, signo);
    pidfd = pidfd_open_impl(child->pid, 0);
    if (pidfd < 0) {
        goto fail;
    }

    ret = syscall_dispatch_impl(__NR_pidfd_send_signal, pidfd, signo, 0, 0, 0, 0);
    if (ret != 0 ||
        signal_thread_pending(child, signo) ||
        !signal_shared_pending(child, signo)) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto fail;
    }

    ret = syscall_dispatch_impl(__NR_pidfd_send_signal, -1, signo, 0, 0, 0, 0);
    if (ret != -EBADF) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto fail;
    }

    signal_clear_queued_task(child, signo);
    signal_contract_set_task_identity(child, 2000);
    signal_contract_set_task_identity(parent, 1000);
    ret = syscall_dispatch_impl(__NR_pidfd_send_signal, pidfd, signo, 0, 0, 0, 0);
    if (ret != -EPERM) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto fail;
    }
    errno = 0;
    if (do_kill(child->pid, signo) != -1 || errno != EPERM) {
        errno = EPROTO;
        goto fail;
    }

    cred_reset_to_defaults();
    if (signal_contract_read_blocked(parent, &saved_parent_blocked) != 0) {
        goto fail;
    }

    thread_pid = clone_impl(CLONE_THREAD | CLONE_VM | CLONE_SIGHAND);
    if (thread_pid < 0) {
        goto fail;
    }
    thread = task_lookup(thread_pid);
    if (!thread || !thread->signal) {
        errno = thread ? EPROTO : ESRCH;
        goto fail;
    }

    signal_clear_queued_task(thread, signo);
    signal_clear_queued_task(parent, signo);
    thread_pidfd = pidfd_open_impl(thread->pid, 0);
    if (thread_pidfd < 0) {
        goto fail;
    }

    ret = syscall_dispatch_impl(__NR_pidfd_send_signal, thread_pidfd, signo, 0, 0, 0, 0);
    if (ret != 0 ||
        signal_thread_pending(thread, signo) ||
        signal_thread_pending(parent, signo) ||
        signal_dequeue(parent, NULL, &dequeued) != 1 ||
        dequeued != signo) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto fail;
    }

    close_impl(thread_pidfd);
    close_impl(pidfd);
    signal_clear_queued_task(thread, signo);
    signal_clear_queued_task(child, signo);
    signal_clear_queued_task(parent, signo);
    if (signal_contract_write_blocked(parent, &saved_parent_blocked) != 0) {
        return -1;
    }
    task_put(thread);
    task_put(child);
    cred_reset_to_defaults();
    return 0;

fail:
    if (thread_pidfd >= 0) {
        close_impl(thread_pidfd);
    }
    if (pidfd >= 0) {
        close_impl(pidfd);
    }
    if (thread) {
        signal_clear_queued_task(thread, signo);
        task_put(thread);
    }
    if (child) {
        signal_clear_queued_task(child, signo);
        task_put(child);
    }
    signal_clear_queued_task(parent, signo);
    (void)signal_contract_write_blocked(parent, &saved_parent_blocked);
    cred_reset_to_defaults();
    return -1;
}

int signal_syscall_contract_pidfd_send_signal_rejects_invalid_parameters(void) {
    struct task *parent = task_current();
    struct task *child = NULL;
    sigset_t saved_parent_blocked;
    long ret;
    int pidfd = -1;
    const int signo = SIGUSR1;

    if (!parent || !parent->signal) {
        errno = ESRCH;
        return -1;
    }

    if (signal_contract_read_blocked(parent, &saved_parent_blocked) != 0) {
        goto fail;
    }

    child = task_create_child_impl(parent);
    if (!child || !child->signal) {
        errno = child ? EPROTO : errno;
        goto fail;
    }

    {
        sigset_t child_blocked = {0};
        sigaddset(&child_blocked, signo);
        if (signal_contract_write_blocked(child, &child_blocked) != 0) {
            goto fail;
        }
    }
    signal_clear_queued_task(child, signo);
    pidfd = pidfd_open_impl(child->pid, 0);
    if (pidfd < 0) {
        goto fail;
    }

    /* Linux: sig==0 performs permission/existence checks but does not queue a signal. */
    ret = syscall_dispatch_impl(__NR_pidfd_send_signal, pidfd, 0, 0, 0, 0, 0);
    if (ret != 0 ||
        signal_thread_pending(child, signo) ||
        signal_shared_pending(child, signo) ||
        signal_queued_count_task(child, signo) != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto fail;
    }

    /* Invalid signal numbers must be rejected. */
    ret = syscall_dispatch_impl(__NR_pidfd_send_signal, pidfd, -1, 0, 0, 0, 0);
    if (ret != -EINVAL) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto fail;
    }
    ret = syscall_dispatch_impl(__NR_pidfd_send_signal, pidfd, KERNEL_SIG_NUM + 1, 0, 0, 0, 0);
    if (ret != -EINVAL) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto fail;
    }

    /* Unsupported parameters must be rejected (current kernel does not support siginfo delivery here). */
    ret = syscall_dispatch_impl(__NR_pidfd_send_signal, pidfd, signo, (long)(uintptr_t)1, 0, 0, 0);
    if (ret != -EINVAL) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto fail;
    }
    ret = syscall_dispatch_impl(__NR_pidfd_send_signal, pidfd, signo, 0, 1, 0, 0);
    if (ret != -EINVAL) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto fail;
    }

    /* sig==0 must still enforce permissions (kill(0)-style). */
    signal_contract_set_task_identity(child, 2000);
    signal_contract_set_task_identity(parent, 1000);
    ret = syscall_dispatch_impl(__NR_pidfd_send_signal, pidfd, 0, 0, 0, 0, 0);
    if (ret != -EPERM) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto fail;
    }

    close_impl(pidfd);
    signal_clear_queued_task(child, signo);
    signal_clear_queued_task(parent, signo);
    if (signal_contract_write_blocked(parent, &saved_parent_blocked) != 0) {
        return -1;
    }
    task_put(child);
    cred_reset_to_defaults();
    return 0;

fail:
    if (pidfd >= 0) {
        close_impl(pidfd);
    }
    if (child) {
        signal_clear_queued_task(child, signo);
        task_put(child);
    }
    signal_clear_queued_task(parent, signo);
    (void)signal_contract_write_blocked(parent, &saved_parent_blocked);
    cred_reset_to_defaults();
    return -1;
}

int signal_syscall_contract_rt_sigaction_uses_linux_uapi_layout(void) {
    struct sigaction act = {0};
    struct sigaction oldact = {0};
    struct sigaction queried = {0};
    long ret;
    act.sa_handler = (__sighandler_t)(uintptr_t)0x1000;
    act.sa_flags = SA_RESTART | SA_ONSTACK;
    sigaddset(&act.sa_mask, 2);

    ret = syscall_dispatch_impl(__NR_rt_sigaction, SIGINT, (long)(uintptr_t)&act,
                                (long)(uintptr_t)&oldact, sizeof(act.sa_mask), 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        return -1;
    }

    ret = syscall_dispatch_impl(__NR_rt_sigaction, SIGINT, 0,
                                (long)(uintptr_t)&queried, sizeof(queried.sa_mask), 0, 0);
    if (ret != 0 ||
        queried.sa_handler != act.sa_handler ||
        queried.sa_flags != act.sa_flags ||
        !sigequalsets(&queried.sa_mask, &act.sa_mask)) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        return -1;
    }

    ret = syscall_dispatch_impl(__NR_rt_sigaction, SIGKILL, (long)(uintptr_t)&act,
                                0, sizeof(act.sa_mask), 0, 0);
    if (ret != -EINVAL) {
        errno = EBUSY;
        return -1;
    }

    return 0;
}

int signal_syscall_contract_sigaltstack_and_frame_policy(void) {
    struct task *task = task_current();
    stack_t stack;
    stack_t old_stack;
    uint64_t frame_sp = 0;
    uint64_t frame_signo = 0;
    uint64_t frame_return_pc = 0;
    void *mapped;
    long ret;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    signal_contract_disable_altstack();
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 16384, PROT_READ | PROT_WRITE,
                                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        return -1;
    }

    stack = (stack_t){0};
    old_stack = (stack_t){0};
    stack.ss_sp = mapped;
    stack.ss_size = 16384;
    stack.ss_flags = 0;
    ret = syscall_dispatch_impl(__NR_sigaltstack, (long)(uintptr_t)&stack,
                                (long)(uintptr_t)&old_stack, 0, 0, 0, 0);
    if (ret != 0 || (old_stack.ss_flags & SS_DISABLE) == 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        signal_contract_disable_altstack();
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
        return -1;
    }
    if (signal_prepare_frame_impl(task, SIGINT, 0x1234, 0x80000000, &frame_sp) != 0 ||
        frame_sp < (uint64_t)(uintptr_t)mapped ||
        frame_sp >= (uint64_t)(uintptr_t)mapped + 16384 ||
        signal_frame_metadata_get_task(task, &frame_signo, &frame_return_pc,
                                       NULL, NULL, NULL, NULL, NULL, NULL) != 0 ||
        frame_signo != SIGINT ||
        frame_return_pc != 0x1234) {
        errno = EPROTO;
        signal_contract_disable_altstack();
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_rt_sigreturn, 0, 0, 0, 0, 0, 0);
    if (ret != 0x1234) {
        errno = EPROTO;
        signal_contract_disable_altstack();
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
        return -1;
    }
    signal_contract_disable_altstack();
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
    return 0;
}

int signal_syscall_contract_frame_writes_virtual_record(void) {
    struct task *task = task_current();
    stack_t stack;
    sigset_t old_blocked;
    uint64_t frame_sp = 0;
    uint64_t frame_words[2] = {0, 0};
    void *mapped;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    if (signal_contract_read_blocked(task, &old_blocked) != 0) {
        return -1;
    }
    signal_contract_disable_altstack();
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 16384, PROT_READ | PROT_WRITE,
                                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        return -1;
    }

    stack = (stack_t){0};
    stack.ss_sp = mapped;
    stack.ss_size = 16384;
    if (syscall_dispatch_impl(__NR_sigaltstack, (long)(uintptr_t)&stack, 0, 0, 0, 0, 0) != 0 ||
        signal_prepare_frame_impl(task, SIGTERM, 0x5678, 0x80000000, &frame_sp) != 0 ||
        task_read_virtual_memory_impl(task, frame_sp, frame_words, sizeof(frame_words)) !=
            (long)sizeof(frame_words) ||
        frame_words[0] != SIGTERM ||
        frame_words[1] != 0x5678) {
        errno = EPROTO;
        (void)signal_contract_write_blocked(task, &old_blocked);
        signal_contract_disable_altstack();
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
        return -1;
    }

    if (signal_contract_write_blocked(task, &old_blocked) != 0) {
        signal_contract_disable_altstack();
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
        return -1;
    }
    signal_contract_disable_altstack();
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
    return 0;
}

int signal_syscall_contract_frame_records_handler_handoff(void) {
    struct task *task = task_current();
    struct sigaction act;
    struct sigaction old_act;
    stack_t stack;
    sigset_t old_blocked;
    uint64_t frame_sp = 0;
    uint64_t frame_handler_pc = 0;
    uint64_t frame_flags = 0;
    void *mapped;
    long ret;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    if (signal_contract_read_blocked(task, &old_blocked) != 0) {
        return -1;
    }
    signal_contract_disable_altstack();
    act = (struct sigaction){0};
    old_act = (struct sigaction){0};
    act.sa_handler = (__sighandler_t)(uintptr_t)0x9000;
    act.sa_flags = SA_RESTART | SA_ONSTACK;
    ret = syscall_dispatch_impl(__NR_rt_sigaction, SIGUSR1, (long)(uintptr_t)&act,
                                (long)(uintptr_t)&old_act, sizeof(act.sa_mask), 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        return -1;
    }

    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 16384, PROT_READ | PROT_WRITE,
                                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        (void)signal_contract_write_blocked(task, &old_blocked);
        syscall_dispatch_impl(__NR_rt_sigaction, SIGUSR1, (long)(uintptr_t)&old_act,
                              0, sizeof(old_act.sa_mask), 0, 0);
        return -1;
    }
    stack = (stack_t){0};
    stack.ss_sp = mapped;
    stack.ss_size = 16384;
    if (syscall_dispatch_impl(__NR_sigaltstack, (long)(uintptr_t)&stack, 0, 0, 0, 0, 0) != 0 ||
        signal_prepare_frame_impl(task, SIGUSR1, 0xaaaa, 0x80000000, &frame_sp) != 0 ||
        signal_frame_metadata_get_task(task, NULL, NULL, &frame_handler_pc,
                                       &frame_flags, NULL, NULL, NULL, NULL) != 0 ||
        frame_handler_pc != 0x9000 ||
        frame_flags != (SA_RESTART | SA_ONSTACK)) {
        errno = EPROTO;
        (void)signal_contract_write_blocked(task, &old_blocked);
        syscall_dispatch_impl(__NR_rt_sigaction, SIGUSR1, (long)(uintptr_t)&old_act,
                              0, sizeof(old_act.sa_mask), 0, 0);
        signal_contract_disable_altstack();
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
        return -1;
    }

    if (signal_contract_write_blocked(task, &old_blocked) != 0) {
        syscall_dispatch_impl(__NR_rt_sigaction, SIGUSR1, (long)(uintptr_t)&old_act,
                              0, sizeof(old_act.sa_mask), 0, 0);
        signal_contract_disable_altstack();
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
        return -1;
    }
    syscall_dispatch_impl(__NR_rt_sigaction, SIGUSR1, (long)(uintptr_t)&old_act,
                          0, sizeof(old_act.sa_mask), 0, 0);
    signal_contract_disable_altstack();
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
    return 0;
}

int signal_syscall_contract_frame_records_mask_restorer_and_context(void) {
    struct task *task = task_current();
    struct sigaction act;
    struct sigaction old_act;
    stack_t stack;
    sigset_t old_blocked;
    uint64_t frame_sp = 0;
    uint64_t frame_words[8] = {0};
    uint64_t block_term = 1ULL << (SIGTERM - 1);
    uint64_t frame_restorer_pc = 0;
    uint64_t frame_mask = 0;
    sigset_t block_set = {0};
    void *mapped;
    long ret;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    if (signal_contract_read_blocked(task, &old_blocked) != 0) {
        return -1;
    }
    signal_contract_disable_altstack();
    act = (struct sigaction){0};
    old_act = (struct sigaction){0};
    act.sa_handler = (__sighandler_t)(uintptr_t)0x9100;
    act.sa_restorer = (__sigrestore_t)(uintptr_t)0x9200;
    act.sa_flags = SA_RESTART | SA_ONSTACK | SA_RESTORER;
    sigaddset(&act.sa_mask, SIGINT);
    sigaddset(&block_set, SIGTERM);
    ret = syscall_dispatch_impl(__NR_rt_sigaction, SIGUSR2, (long)(uintptr_t)&act,
                                (long)(uintptr_t)&old_act, sizeof(act.sa_mask), 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_rt_sigprocmask, SIG_BLOCK, (long)(uintptr_t)&block_set,
                                0, sizeof(block_set), 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        (void)signal_contract_write_blocked(task, &old_blocked);
        syscall_dispatch_impl(__NR_rt_sigaction, SIGUSR2, (long)(uintptr_t)&old_act,
                              0, sizeof(old_act.sa_mask), 0, 0);
        return -1;
    }

    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 16384, PROT_READ | PROT_WRITE,
                                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        (void)signal_contract_write_blocked(task, &old_blocked);
        syscall_dispatch_impl(__NR_rt_sigaction, SIGUSR2, (long)(uintptr_t)&old_act,
                              0, sizeof(old_act.sa_mask), 0, 0);
        return -1;
    }
    stack = (stack_t){0};
    stack.ss_sp = mapped;
    stack.ss_size = 16384;
    if (syscall_dispatch_impl(__NR_sigaltstack, (long)(uintptr_t)&stack, 0, 0, 0, 0, 0) != 0 ||
        signal_prepare_frame_impl(task, SIGUSR2, 0xbbbb, 0x80000000, &frame_sp) != 0 ||
        task_read_virtual_memory_impl(task, frame_sp, frame_words, sizeof(frame_words)) !=
            (long)sizeof(frame_words) ||
        frame_words[0] != SIGUSR2 ||
        frame_words[1] != 0xbbbb ||
        frame_words[2] != 0x9100 ||
        frame_words[3] != (SA_RESTART | SA_ONSTACK | SA_RESTORER) ||
        frame_words[4] != block_term ||
        frame_words[5] != (uint64_t)(uintptr_t)mapped ||
        frame_words[6] != 16384 ||
        (frame_words[7] & 1) == 0 ||
        signal_frame_metadata_get_task(task, NULL, NULL, NULL, NULL,
                                       &frame_restorer_pc, &frame_mask,
                                       NULL, NULL) != 0 ||
        frame_restorer_pc != 0x9200 ||
        frame_mask != block_term) {
        errno = EPROTO;
        (void)signal_contract_write_blocked(task, &old_blocked);
        syscall_dispatch_impl(__NR_rt_sigaction, SIGUSR2, (long)(uintptr_t)&old_act,
                              0, sizeof(old_act.sa_mask), 0, 0);
        signal_contract_disable_altstack();
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
        return -1;
    }

    if (signal_contract_write_blocked(task, &old_blocked) != 0) {
        syscall_dispatch_impl(__NR_rt_sigaction, SIGUSR2, (long)(uintptr_t)&old_act,
                              0, sizeof(old_act.sa_mask), 0, 0);
        signal_contract_disable_altstack();
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
        return -1;
    }
    syscall_dispatch_impl(__NR_rt_sigaction, SIGUSR2, (long)(uintptr_t)&old_act,
                          0, sizeof(old_act.sa_mask), 0, 0);
    signal_contract_disable_altstack();
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
    return 0;
}

int signal_syscall_contract_rt_sigreturn_restores_mask_and_altstack(void) {
    struct task *task = task_current();
    stack_t stack;
    uint64_t frame_sp = 0;
    sigset_t block_set = {0};
    sigset_t unblock_set = {0};
    sigset_t queried = {0};
    void *mapped;
    long ret;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    signal_contract_disable_altstack();
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 16384, PROT_READ | PROT_WRITE,
                                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        return -1;
    }
    stack = (stack_t){0};
    stack.ss_sp = mapped;
    stack.ss_size = 16384;
    sigaddset(&block_set, SIGTERM);
    sigaddset(&unblock_set, SIGTERM);
    if (syscall_dispatch_impl(__NR_sigaltstack, (long)(uintptr_t)&stack, 0, 0, 0, 0, 0) != 0 ||
        syscall_dispatch_impl(__NR_rt_sigprocmask, SIG_BLOCK, (long)(uintptr_t)&block_set,
                              0, sizeof(block_set), 0, 0) != 0 ||
        signal_prepare_frame_impl(task, SIGUSR1, 0x7777, 0x80000000, &frame_sp) != 0 ||
        syscall_dispatch_impl(__NR_rt_sigprocmask, SIG_UNBLOCK, (long)(uintptr_t)&unblock_set,
                              0, sizeof(unblock_set), 0, 0) != 0) {
        errno = EPROTO;
        signal_contract_disable_altstack();
        syscall_dispatch_impl(__NR_rt_sigprocmask, SIG_UNBLOCK, (long)(uintptr_t)&block_set,
                              0, sizeof(block_set), 0, 0);
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
        return -1;
    }

    ret = syscall_dispatch_impl(__NR_rt_sigreturn, 0, 0, 0, 0, 0, 0);
    if (ret != 0x7777) {
        errno = EPROTO;
        signal_contract_disable_altstack();
        syscall_dispatch_impl(__NR_rt_sigprocmask, SIG_UNBLOCK, (long)(uintptr_t)&block_set,
                              0, sizeof(block_set), 0, 0);
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_rt_sigprocmask, SIG_BLOCK, 0,
                                (long)(uintptr_t)&queried, sizeof(queried), 0, 0);
    if (ret != 0 || !signal_contract_sigset_contains(&queried, SIGTERM) ||
        signal_altstack_has_flags_task(task, SS_ONSTACK)) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        signal_contract_disable_altstack();
        syscall_dispatch_impl(__NR_rt_sigprocmask, SIG_UNBLOCK, (long)(uintptr_t)&block_set,
                              0, sizeof(block_set), 0, 0);
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
        return -1;
    }

    signal_contract_disable_altstack();
    syscall_dispatch_impl(__NR_rt_sigprocmask, SIG_UNBLOCK, (long)(uintptr_t)&block_set,
                          0, sizeof(block_set), 0, 0);
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
    return 0;
}

int signal_syscall_contract_rt_sigreturn_restores_frame_context_record(void) {
    struct task *task = task_current();
    stack_t stack;
    uint64_t frame_sp = 0;
    uint64_t frame_words[12] = {0};
    uint64_t block_term = 1ULL << (SIGTERM - 1);
    uint64_t frame_current_sp = 0;
    uint64_t frame_size = 0;
    sigset_t block_set = {0};
    sigset_t queried = {0};
    void *mapped;
    long ret;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    signal_contract_disable_altstack();
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 16384, PROT_READ | PROT_WRITE,
                                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        return -1;
    }
    stack = (stack_t){0};
    stack.ss_sp = mapped;
    stack.ss_size = 16384;
    sigaddset(&block_set, SIGTERM);
    if (syscall_dispatch_impl(__NR_sigaltstack, (long)(uintptr_t)&stack, 0, 0, 0, 0, 0) != 0 ||
        syscall_dispatch_impl(__NR_rt_sigprocmask, SIG_BLOCK, (long)(uintptr_t)&block_set,
                              0, sizeof(block_set), 0, 0) != 0 ||
        signal_prepare_frame_impl(task, SIGUSR1, 0xabcddcba, 0x7ffff000, &frame_sp) != 0 ||
        task_read_virtual_memory_impl(task, frame_sp, frame_words, sizeof(frame_words)) !=
            (long)sizeof(frame_words)) {
        errno = EPROTO;
        signal_contract_disable_altstack();
        syscall_dispatch_impl(__NR_rt_sigprocmask, SIG_UNBLOCK, (long)(uintptr_t)&block_set,
                              0, sizeof(block_set), 0, 0);
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
        return -1;
    }
    if (frame_words[0] != SIGUSR1 ||
        frame_words[1] != 0xabcddcba ||
        frame_words[4] != block_term ||
        frame_words[5] != (uint64_t)(uintptr_t)mapped ||
        frame_words[6] != 16384 ||
        frame_words[8] != 0x7ffff000 ||
        frame_words[9] != frame_sp ||
        frame_words[10] != block_term ||
        frame_words[11] == 0 ||
        signal_frame_metadata_get_task(task, NULL, NULL, NULL, NULL,
                                       NULL, NULL, &frame_current_sp,
                                       &frame_size) != 0 ||
        frame_current_sp != 0x7ffff000 ||
        frame_size < sizeof(frame_words)) {
        errno = ENODATA;
        signal_contract_disable_altstack();
        syscall_dispatch_impl(__NR_rt_sigprocmask, SIG_UNBLOCK, (long)(uintptr_t)&block_set,
                              0, sizeof(block_set), 0, 0);
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_rt_sigprocmask, SIG_UNBLOCK, (long)(uintptr_t)&block_set,
                                0, sizeof(block_set), 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        signal_contract_disable_altstack();
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_rt_sigreturn, 0, 0, 0, 0, 0, 0);
    if (ret != 0xabcddcba) {
        errno = EPROTO;
        signal_contract_disable_altstack();
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_rt_sigprocmask, SIG_BLOCK, 0,
                                (long)(uintptr_t)&queried, sizeof(queried), 0, 0);
    if (ret != 0 || !signal_contract_sigset_contains(&queried, SIGTERM)) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        signal_contract_disable_altstack();
        syscall_dispatch_impl(__NR_rt_sigprocmask, SIG_UNBLOCK, (long)(uintptr_t)&block_set,
                              0, sizeof(block_set), 0, 0);
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
        return -1;
    }

    signal_contract_disable_altstack();
    syscall_dispatch_impl(__NR_rt_sigprocmask, SIG_UNBLOCK, (long)(uintptr_t)&block_set,
                          0, sizeof(block_set), 0, 0);
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
    return 0;
}

int signal_syscall_contract_frame_contains_linux_ucontext(void) {
    struct task *task = task_current();
    stack_t stack;
    sigset_t old_blocked;
    uint64_t frame_sp = 0;
    uint64_t block_term = 1ULL << (SIGTERM - 1);
    uint64_t frame_size = 0;
    sigset_t block_set = {0};
    struct ucontext context;
    void *mapped;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    if (signal_contract_read_blocked(task, &old_blocked) != 0) {
        return -1;
    }
    signal_contract_disable_altstack();
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 16384, PROT_READ | PROT_WRITE,
                                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        return -1;
    }
    stack = (stack_t){0};
    stack.ss_sp = mapped;
    stack.ss_size = 16384;
    sigaddset(&block_set, SIGTERM);
    if (syscall_dispatch_impl(__NR_sigaltstack, (long)(uintptr_t)&stack, 0, 0, 0, 0, 0) != 0 ||
        syscall_dispatch_impl(__NR_rt_sigprocmask, SIG_BLOCK, (long)(uintptr_t)&block_set,
                              0, sizeof(block_set), 0, 0) != 0 ||
        signal_prepare_frame_impl(task, SIGUSR1, 0x13579bdf, 0x7fff0000, &frame_sp) != 0 ||
        task_read_virtual_memory_impl(task, frame_sp + 128, &context, sizeof(context)) !=
            (long)sizeof(context)) {
        errno = EPROTO;
        (void)signal_contract_write_blocked(task, &old_blocked);
        signal_contract_disable_altstack();
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
        return -1;
    }
    if (context.uc_flags != 1 ||
        context.uc_link != 0 ||
        context.uc_stack.ss_sp != mapped ||
        context.uc_stack.ss_size != 16384 ||
        (context.uc_stack.ss_flags & SS_ONSTACK) == 0 ||
        context.uc_sigmask.sig[0] != block_term ||
        context.uc_mcontext.sp != 0x7fff0000 ||
        context.uc_mcontext.pc != 0x13579bdf ||
        signal_frame_metadata_get_task(task, NULL, NULL, NULL, NULL,
                                       NULL, NULL, NULL, &frame_size) != 0 ||
        frame_size < 128 + sizeof(context)) {
        errno = ENODATA;
        (void)signal_contract_write_blocked(task, &old_blocked);
        signal_contract_disable_altstack();
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
        return -1;
    }

    if (signal_contract_write_blocked(task, &old_blocked) != 0) {
        signal_contract_disable_altstack();
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
        return -1;
    }
    signal_contract_disable_altstack();
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
    return 0;
}

int signal_syscall_contract_ignored_dispositions_do_not_queue_or_terminate(void) {
    struct task *task = task_current();
    struct sigaction act;
    struct sigaction dfl;
    struct sigaction old_term;
    long ret;

    if (!task || !task->signal) {
        errno = ESRCH;
        return -1;
    }

    act = (struct sigaction){0};
    dfl = (struct sigaction){0};
    old_term = (struct sigaction){0};
    dfl.sa_handler = SIG_DFL;

    signal_clear_pending_task(task, SIGWINCH);
    ret = syscall_dispatch_impl(__NR_rt_sigaction, SIGWINCH, (long)(uintptr_t)&dfl,
                                0, sizeof(dfl.sa_mask), 0, 0);
    if (ret != 0 ||
        signal_generate_task(task, SIGWINCH) != 0 ||
        signal_is_pending(task, SIGWINCH)) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        return -1;
    }

    signal_clear_pending_task(task, SIGTERM);
    act.sa_handler = SIG_IGN;
    ret = syscall_dispatch_impl(__NR_rt_sigaction, SIGTERM, (long)(uintptr_t)&act,
                                (long)(uintptr_t)&old_term, sizeof(act.sa_mask), 0, 0);
    if (ret != 0 ||
        signal_generate_task(task, SIGTERM) != 0 ||
        signal_is_pending(task, SIGTERM) ||
        atomic_read(&task->exited) ||
        atomic_read(&task->signaled)) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        syscall_dispatch_impl(__NR_rt_sigaction, SIGTERM, (long)(uintptr_t)&old_term,
                              0, sizeof(old_term.sa_mask), 0, 0);
        return -1;
    }

    act.sa_handler = (__sighandler_t)(uintptr_t)0x7000;
    ret = syscall_dispatch_impl(__NR_rt_sigaction, SIGTERM, (long)(uintptr_t)&act,
                                0, sizeof(act.sa_mask), 0, 0);
    signal_clear_pending_task(task, SIGTERM);
    atomic_set(&task->exited, 0);
    atomic_set(&task->signaled, 0);
    if (ret != 0 ||
        signal_generate_task(task, SIGTERM) != 0 ||
        !signal_is_pending(task, SIGTERM) ||
        atomic_read(&task->exited) ||
        atomic_read(&task->signaled)) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        syscall_dispatch_impl(__NR_rt_sigaction, SIGTERM, (long)(uintptr_t)&old_term,
                              0, sizeof(old_term.sa_mask), 0, 0);
        return -1;
    }

    signal_clear_pending_task(task, SIGTERM);
    syscall_dispatch_impl(__NR_rt_sigaction, SIGTERM, (long)(uintptr_t)&old_term,
                          0, sizeof(old_term.sa_mask), 0, 0);
    return 0;
}

int signal_syscall_contract_realtime_queue_preserves_multiplicity_and_order(void) {
    struct task *task = task_current();
    sigset_t old_blocked;
    sigset_t only_realtime;
    int signo = SIGRTMIN;
    int dequeued = 0;

    if (!task || !task->signal) {
        errno = ESRCH;
        return -1;
    }
    if (signal_contract_read_blocked(task, &old_blocked) != 0) {
        return -1;
    }
    only_realtime = signal_contract_mask_all_except(signo);
    if (signal_contract_clear_blocked(task) != 0) {
        return -1;
    }
    signal_clear_queued_task(task, signo);
    signal_clear_queued_task(task, SIGUSR1);

    if (signal_generate_task_info(task, signo, 100, 0x1000) != 0 ||
        signal_generate_task_info(task, signo, 101, 0x2000) != 0 ||
        signal_queued_count_task(task, signo) != 2 ||
        signal_dequeue(task, &only_realtime, &dequeued) != 1 ||
        dequeued != signo ||
        !signal_is_pending(task, signo) ||
        signal_queued_count_task(task, signo) != 1 ||
        signal_dequeue(task, &only_realtime, &dequeued) != 1 ||
        dequeued != signo ||
        signal_is_pending(task, signo) ||
        signal_queued_count_task(task, signo) != 0) {
        errno = EPROTO;
        signal_clear_queued_task(task, signo);
        (void)signal_contract_write_blocked(task, &old_blocked);
        return -1;
    }

    if (signal_generate_task(task, SIGUSR1) != 0 ||
        signal_generate_task(task, SIGUSR1) != 0 ||
        signal_queued_count_task(task, SIGUSR1) != 1) {
        errno = EALREADY;
        signal_clear_queued_task(task, SIGUSR1);
        (void)signal_contract_write_blocked(task, &old_blocked);
        return -1;
    }
    signal_clear_queued_task(task, SIGUSR1);
    if (signal_contract_write_blocked(task, &old_blocked) != 0) {
        signal_clear_queued_task(task, SIGUSR1);
        return -1;
    }
    return 0;
}

int signal_syscall_contract_frame_applies_handler_mask_nodefer_and_resethand(void) {
    struct task *task = task_current();
    struct sigaction act;
    struct sigaction old_usr1;
    struct sigaction old_usr2;
    sigset_t old_blocked;
    uint64_t frame_sp = 0;
    uint64_t block_term = 1ULL << (SIGTERM - 1);
    uint64_t frame_mask = 0;
    void *mapped;
    long ret;

    if (!task || !task->signal) {
        errno = ESRCH;
        return -1;
    }
    act = (struct sigaction){0};
    old_usr1 = (struct sigaction){0};
    old_usr2 = (struct sigaction){0};
    if (signal_contract_read_blocked(task, &old_blocked) != 0) {
        return -1;
    }
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 16384, PROT_READ | PROT_WRITE,
                                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        return -1;
    }

    {
        sigset_t blocked = {0};
        sigaddset(&blocked, SIGTERM);
        if (signal_contract_write_blocked(task, &blocked) != 0) {
            syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
            return -1;
        }
    }
    act.sa_handler = (__sighandler_t)(uintptr_t)0x7100;
    act.sa_flags = SA_RESETHAND;
    sigaddset(&act.sa_mask, SIGUSR2);
    ret = syscall_dispatch_impl(__NR_rt_sigaction, SIGUSR1, (long)(uintptr_t)&act,
                                (long)(uintptr_t)&old_usr1, sizeof(act.sa_mask), 0, 0);
    if (ret != 0 ||
        signal_prepare_frame_impl(task, SIGUSR1, 0x1111,
                                  (uint64_t)(uintptr_t)mapped + 16384, &frame_sp) != 0 ||
        signal_frame_metadata_get_task(task, NULL, NULL, NULL, NULL,
                                       NULL, &frame_mask, NULL, NULL) != 0 ||
        frame_mask != block_term ||
        !signal_is_blocked(task, SIGTERM) ||
        !signal_is_blocked(task, SIGUSR1) ||
        !signal_is_blocked(task, SIGUSR2) ||
        !signal_action_default_task(task, SIGUSR1)) {
        errno = EPROTO;
        goto out;
    }

    if (signal_contract_clear_blocked(task) != 0) {
        goto out;
    }
    act = (struct sigaction){0};
    act.sa_handler = (__sighandler_t)(uintptr_t)0x7200;
    act.sa_flags = SA_NODEFER;
    ret = syscall_dispatch_impl(__NR_rt_sigaction, SIGUSR2, (long)(uintptr_t)&act,
                                (long)(uintptr_t)&old_usr2, sizeof(act.sa_mask), 0, 0);
    if (ret != 0 ||
        signal_prepare_frame_impl(task, SIGUSR2, 0x2222,
                                  (uint64_t)(uintptr_t)mapped + 16384, &frame_sp) != 0 ||
        signal_is_blocked(task, SIGUSR2)) {
        errno = ENOTRECOVERABLE;
        goto out;
    }

    ret = 0;

out:
    {
        int saved_errno = errno;
        syscall_dispatch_impl(__NR_rt_sigaction, SIGUSR1, (long)(uintptr_t)&old_usr1,
                              0, sizeof(old_usr1.sa_mask), 0, 0);
        syscall_dispatch_impl(__NR_rt_sigaction, SIGUSR2, (long)(uintptr_t)&old_usr2,
                              0, sizeof(old_usr2.sa_mask), 0, 0);
        (void)signal_contract_write_blocked(task, &old_blocked);
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
        errno = saved_errno;
    }
    return ret == 0 ? 0 : -1;
}

int signal_syscall_contract_restart_metadata_follows_sa_restart(void) {
    struct task *task = task_current();
    struct sigaction act;
    struct sigaction old_usr1;
    sigset_t old_blocked;
    void *mapped;
    uint64_t frame_sp = 0;
    uint64_t restartable = 0;
    uint64_t restart_return_pc = 0;
    uint64_t restart_sp = 0;
    uint64_t restart_signo = 0;
    long ret;

    if (!task || !task->signal) {
        errno = ESRCH;
        return -1;
    }

    act = (struct sigaction){0};
    old_usr1 = (struct sigaction){0};
    if (signal_contract_read_blocked(task, &old_blocked) != 0) {
        return -1;
    }
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 16384, PROT_READ | PROT_WRITE,
                                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        return -1;
    }
    if (!task->mm) {
        errno = ESRCH;
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
        return -1;
    }

    act.sa_handler = (__sighandler_t)(uintptr_t)0x7300;
    act.sa_flags = SA_RESTART;
    ret = syscall_dispatch_impl(__NR_rt_sigaction, SIGUSR1, (long)(uintptr_t)&act,
                                (long)(uintptr_t)&old_usr1, sizeof(act.sa_mask), 0, 0);
    if (ret != 0 ||
        signal_prepare_frame_impl(task, SIGUSR1, 0x3333,
                                  (uint64_t)(uintptr_t)mapped + 16384, &frame_sp) != 0 ||
        signal_frame_restart_status_get_task(task, &restartable, &restart_return_pc,
                                             &restart_sp, &restart_signo) != 0 ||
        restartable != 1 ||
        restart_return_pc != 0x3333 ||
        restart_sp != (uint64_t)(uintptr_t)mapped + 16384 ||
        restart_signo != SIGUSR1 ||
        !signal_frame_restart_is_task(task, TASK_RESTART_NONE) ||
        syscall_dispatch_impl(__NR_restart_syscall, 0, 0, 0, 0, 0, 0) != -EINTR) {
        errno = EPROTO;
        goto out;
    }

    act = (struct sigaction){0};
    act.sa_handler = (__sighandler_t)(uintptr_t)0x7400;
    ret = syscall_dispatch_impl(__NR_rt_sigaction, SIGUSR1, (long)(uintptr_t)&act,
                                0, sizeof(act.sa_mask), 0, 0);
    if (ret != 0 ||
        signal_prepare_frame_impl(task, SIGUSR1, 0x4444,
                                  (uint64_t)(uintptr_t)mapped + 16384, &frame_sp) != 0 ||
        signal_frame_restart_status_get_task(task, &restartable, &restart_return_pc,
                                             NULL, NULL) != 0 ||
        restartable != 0 ||
        restart_return_pc != 0x4444 ||
        !signal_frame_restart_is_task(task, TASK_RESTART_NONE) ||
        syscall_dispatch_impl(__NR_restart_syscall, 0, 0, 0, 0, 0, 0) != -EINTR) {
        errno = ENODATA;
        goto out;
    }

    {
        struct __kernel_timespec req = {.tv_sec = 0, .tv_nsec = 1000000};
        struct __kernel_timespec rem = {0};
        if (signal_contract_write_blocked(task, &old_blocked) != 0) {
            errno = EPROTO;
            goto out;
        }
        signal_generate_task(task, SIGUSR1);
        if (nanosleep_impl(&req, &rem) != -1 ||
            errno != EINTR ||
            !signal_frame_restart_matches_task(task, TASK_RESTART_NANOSLEEP,
                                               (uint64_t)(uintptr_t)&req,
                                               (uint64_t)(uintptr_t)&rem,
                                               0, 0, 0, 0)) {
            errno = EBADMSG;
            goto out;
        }
        signal_clear_pending_task(task, SIGUSR1);
        if (syscall_dispatch_impl(__NR_restart_syscall, 0, 0, 0, 0, 0, 0) != 0 ||
            !signal_frame_restart_is_task(task, TASK_RESTART_NONE)) {
            errno = EALREADY;
            goto out;
        }
    }

    ret = 0;

out:
    {
        int saved_errno = errno;
        syscall_dispatch_impl(__NR_rt_sigaction, SIGUSR1, (long)(uintptr_t)&old_usr1,
                              0, sizeof(old_usr1.sa_mask), 0, 0);
        (void)signal_contract_write_blocked(task, &old_blocked);
        signal_clear_pending_task(task, SIGUSR1);
        signal_frame_clear_task(task);
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
        errno = saved_errno;
    }
    return ret == 0 ? 0 : -1;
}
