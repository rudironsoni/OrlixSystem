#include "SignalSyscallContract.h"

#include <asm/signal.h>
#include <asm/sigcontext.h>
#include <asm/ucontext.h>
#include <asm/unistd.h>
#include <linux/mman.h>
#include <linux/sched.h>
#include <linux/signal.h>

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "runtime/syscall.h"
#include "kernel/signal.h"
#include "kernel/cred_internal.h"
#include "kernel/task.h"

extern void free(void *);
extern int nanosleep_impl(const struct timespec *req, struct timespec *rem);

static void signal_contract_disable_altstack(void) {
    stack_t disabled;

    memset(&disabled, 0, sizeof(disabled));
    disabled.ss_flags = SS_DISABLE;
    syscall_dispatch_impl(__NR_sigaltstack, (long)(uintptr_t)&disabled, 0, 0, 0, 0, 0);
}

static void signal_contract_clear_pending(struct task_struct *task, int signo) {
    int32_t dequeued = 0;

    if (!task || !task->signal || signo < 1 || signo > KERNEL_SIG_NUM) {
        return;
    }
    while (signal_dequeue(task, NULL, &dequeued) > 0) {
        if (dequeued == signo) {
            break;
        }
    }
    task->thread_pending_signals &= ~(1ULL << ((signo - 1) & 63));
    task->signal->pending.sig[(signo - 1) >> 6] &= ~(1ULL << ((signo - 1) & 63));
    task->signal->shared_pending.sig[(signo - 1) >> 6] &= ~(1ULL << ((signo - 1) & 63));
}

static void signal_contract_clear_queued_signal(struct task_struct *task, int signo) {
    struct signal_queue_entry *prev = NULL;
    struct signal_queue_entry *entry;

    if (!task || !task->signal || signo < 1 || signo > KERNEL_SIG_NUM) {
        return;
    }

    kernel_mutex_lock(&task->signal->queue.lock);
    entry = task->signal->queue.head;
    while (entry) {
        struct signal_queue_entry *next = entry->next;
        if (entry->sig == signo) {
            if (prev) {
                prev->next = next;
            } else {
                task->signal->queue.head = next;
            }
            if (task->signal->queue.tail == entry) {
                task->signal->queue.tail = prev;
            }
            task->signal->queue.count--;
            free(entry);
        } else {
            prev = entry;
        }
        entry = next;
    }
    kernel_mutex_unlock(&task->signal->queue.lock);
    signal_contract_clear_pending(task, signo);
}

static int signal_contract_queued_count(struct task_struct *task, int signo) {
    struct signal_queue_entry *entry;
    int count = 0;

    if (!task || !task->signal || signo < 1 || signo > KERNEL_SIG_NUM) {
        return 0;
    }

    kernel_mutex_lock(&task->signal->queue.lock);
    for (entry = task->signal->queue.head; entry; entry = entry->next) {
        if (entry->sig == signo) {
            count++;
        }
    }
    kernel_mutex_unlock(&task->signal->queue.lock);
    return count;
}

static struct signal_mask_bits signal_contract_mask_all_except(int signo) {
    struct signal_mask_bits mask;

    memset(&mask, 0xff, sizeof(mask));
    if (signo >= 1 && signo <= KERNEL_SIG_NUM) {
        mask.sig[(signo - 1) >> 6] &= ~(1ULL << ((signo - 1) & 63));
    }
    return mask;
}

static void signal_contract_set_task_identity(struct task_struct *task, uint32_t uid) {
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
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
    struct task_struct *thread = NULL;
    struct signal_mask_bits saved_parent_blocked;
    long ret;
    int pidfd = -1;
    int thread_pidfd = -1;
    const int signo = SIGUSR1;
    const int idx = (signo - 1) >> 6;
    const uint64_t bit = 1ULL << ((signo - 1) & 63);

    if (!parent || !parent->signal) {
        errno = ESRCH;
        return -1;
    }

    saved_parent_blocked = parent->signal->blocked;

    child = task_create_child_impl(parent);
    if (!child || !child->signal) {
        errno = child ? EPROTO : errno;
        goto fail;
    }

    child->signal->blocked.sig[idx] |= bit;
    signal_contract_clear_queued_signal(child, signo);
    pidfd = pidfd_open_impl(child->pid, 0);
    if (pidfd < 0) {
        goto fail;
    }

    ret = syscall_dispatch_impl(__NR_pidfd_send_signal, pidfd, signo, 0, 0, 0, 0);
    if (ret != 0 ||
        child->thread_pending_signals != 0 ||
        (child->signal->shared_pending.sig[idx] & bit) == 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto fail;
    }

    ret = syscall_dispatch_impl(__NR_pidfd_send_signal, -1, signo, 0, 0, 0, 0);
    if (ret != -EBADF) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto fail;
    }

    signal_contract_clear_queued_signal(child, signo);
    signal_contract_set_task_identity(child, 2000);
    signal_contract_set_task_identity(parent, 1000);
    ret = syscall_dispatch_impl(__NR_pidfd_send_signal, pidfd, signo, 0, 0, 0, 0);
    if (ret != -EPERM) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto fail;
    }

    cred_reset_to_defaults();
    saved_parent_blocked = parent->signal->blocked;

    thread = task_create_child_with_flags_impl(parent, CLONE_THREAD);
    if (!thread || !thread->signal) {
        errno = thread ? EPROTO : errno;
        goto fail;
    }

    thread->signal->blocked.sig[idx] |= bit;
    signal_contract_clear_queued_signal(thread, signo);
    signal_contract_clear_queued_signal(parent, signo);
    thread_pidfd = pidfd_open_impl(thread->pid, 0);
    if (thread_pidfd < 0) {
        goto fail;
    }

    ret = syscall_dispatch_impl(__NR_pidfd_send_signal, thread_pidfd, signo, 0, 0, 0, 0);
    if (ret != 0 ||
        thread->thread_pending_signals != 0 ||
        (thread->signal->shared_pending.sig[idx] & bit) != 0 ||
        (parent->signal->shared_pending.sig[idx] & bit) == 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto fail;
    }

    signal_contract_clear_queued_signal(thread, signo);
    signal_contract_clear_queued_signal(parent, signo);
    parent->signal->blocked.sig[idx] |= bit;

    ret = syscall_dispatch_impl(__NR_pidfd_send_signal, thread_pidfd, signo, 0, 0, 0, 0);
    if (ret != 0 ||
        thread->thread_pending_signals != 0 ||
        (thread->signal->shared_pending.sig[idx] & bit) == 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto fail;
    }

    close_impl(thread_pidfd);
    close_impl(pidfd);
    signal_contract_clear_queued_signal(thread, signo);
    signal_contract_clear_queued_signal(child, signo);
    signal_contract_clear_queued_signal(parent, signo);
    parent->signal->blocked = saved_parent_blocked;
    free_task(thread);
    free_task(child);
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
        signal_contract_clear_queued_signal(thread, signo);
        free_task(thread);
    }
    if (child) {
        signal_contract_clear_queued_signal(child, signo);
        free_task(child);
    }
    signal_contract_clear_queued_signal(parent, signo);
    parent->signal->blocked = saved_parent_blocked;
    cred_reset_to_defaults();
    return -1;
}

int signal_syscall_contract_rt_sigaction_uses_linux_uapi_layout(void) {
    struct sigaction act;
    struct sigaction oldact;
    struct sigaction queried;
    long ret;

    memset(&act, 0, sizeof(act));
    memset(&oldact, 0, sizeof(oldact));
    memset(&queried, 0, sizeof(queried));
    act.sa_handler = (__sighandler_t)(uintptr_t)0x1000;
    act.sa_flags = SA_RESTART | SA_ONSTACK;
    act.sa_mask.sig[0] = 1ULL << (2 - 1);

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
        queried.sa_mask.sig[0] != act.sa_mask.sig[0]) {
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
    struct task_struct *task = get_current();
    stack_t stack;
    stack_t old_stack;
    uint64_t frame_sp = 0;
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

    memset(&stack, 0, sizeof(stack));
    memset(&old_stack, 0, sizeof(old_stack));
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
        task->mm->signal_frame_signo != SIGINT ||
        task->mm->signal_frame_return_pc != 0x1234) {
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
    struct task_struct *task = get_current();
    stack_t stack;
    struct signal_mask_bits old_blocked;
    uint64_t frame_sp = 0;
    uint64_t frame_words[2] = {0, 0};
    void *mapped;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    old_blocked = task->signal->blocked;
    signal_contract_disable_altstack();
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 16384, PROT_READ | PROT_WRITE,
                                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        return -1;
    }

    memset(&stack, 0, sizeof(stack));
    stack.ss_sp = mapped;
    stack.ss_size = 16384;
    if (syscall_dispatch_impl(__NR_sigaltstack, (long)(uintptr_t)&stack, 0, 0, 0, 0, 0) != 0 ||
        signal_prepare_frame_impl(task, SIGTERM, 0x5678, 0x80000000, &frame_sp) != 0 ||
        task_read_virtual_memory_impl(task, frame_sp, frame_words, sizeof(frame_words)) !=
            (long)sizeof(frame_words) ||
        frame_words[0] != SIGTERM ||
        frame_words[1] != 0x5678) {
        errno = EPROTO;
        task->signal->blocked = old_blocked;
        signal_contract_disable_altstack();
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
        return -1;
    }

    task->signal->blocked = old_blocked;
    signal_contract_disable_altstack();
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
    return 0;
}

int signal_syscall_contract_frame_records_handler_handoff(void) {
    struct task_struct *task = get_current();
    struct sigaction act;
    struct sigaction old_act;
    stack_t stack;
    struct signal_mask_bits old_blocked;
    uint64_t frame_sp = 0;
    void *mapped;
    long ret;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    old_blocked = task->signal->blocked;
    signal_contract_disable_altstack();
    memset(&act, 0, sizeof(act));
    memset(&old_act, 0, sizeof(old_act));
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
        task->signal->blocked = old_blocked;
        syscall_dispatch_impl(__NR_rt_sigaction, SIGUSR1, (long)(uintptr_t)&old_act,
                              0, sizeof(old_act.sa_mask), 0, 0);
        return -1;
    }
    memset(&stack, 0, sizeof(stack));
    stack.ss_sp = mapped;
    stack.ss_size = 16384;
    if (syscall_dispatch_impl(__NR_sigaltstack, (long)(uintptr_t)&stack, 0, 0, 0, 0, 0) != 0 ||
        signal_prepare_frame_impl(task, SIGUSR1, 0xaaaa, 0x80000000, &frame_sp) != 0 ||
        task->mm->signal_handler_pc != 0x9000 ||
        task->mm->signal_frame_flags != (SA_RESTART | SA_ONSTACK)) {
        errno = EPROTO;
        task->signal->blocked = old_blocked;
        syscall_dispatch_impl(__NR_rt_sigaction, SIGUSR1, (long)(uintptr_t)&old_act,
                              0, sizeof(old_act.sa_mask), 0, 0);
        signal_contract_disable_altstack();
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
        return -1;
    }

    task->signal->blocked = old_blocked;
    syscall_dispatch_impl(__NR_rt_sigaction, SIGUSR1, (long)(uintptr_t)&old_act,
                          0, sizeof(old_act.sa_mask), 0, 0);
    signal_contract_disable_altstack();
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
    return 0;
}

int signal_syscall_contract_frame_records_mask_restorer_and_context(void) {
    struct task_struct *task = get_current();
    struct sigaction act;
    struct sigaction old_act;
    stack_t stack;
    struct signal_mask_bits old_blocked;
    uint64_t frame_sp = 0;
    uint64_t frame_words[8] = {0};
    uint64_t block_set = 1ULL << (SIGTERM - 1);
    void *mapped;
    long ret;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    old_blocked = task->signal->blocked;
    signal_contract_disable_altstack();
    memset(&act, 0, sizeof(act));
    memset(&old_act, 0, sizeof(old_act));
    act.sa_handler = (__sighandler_t)(uintptr_t)0x9100;
    act.sa_restorer = (__sigrestore_t)(uintptr_t)0x9200;
    act.sa_flags = SA_RESTART | SA_ONSTACK | SA_RESTORER;
    act.sa_mask.sig[0] = 1ULL << (SIGINT - 1);
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
        task->signal->blocked = old_blocked;
        syscall_dispatch_impl(__NR_rt_sigaction, SIGUSR2, (long)(uintptr_t)&old_act,
                              0, sizeof(old_act.sa_mask), 0, 0);
        return -1;
    }

    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 16384, PROT_READ | PROT_WRITE,
                                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        task->signal->blocked = old_blocked;
        syscall_dispatch_impl(__NR_rt_sigaction, SIGUSR2, (long)(uintptr_t)&old_act,
                              0, sizeof(old_act.sa_mask), 0, 0);
        return -1;
    }
    memset(&stack, 0, sizeof(stack));
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
        frame_words[4] != block_set ||
        frame_words[5] != (uint64_t)(uintptr_t)mapped ||
        frame_words[6] != 16384 ||
        (frame_words[7] & 1) == 0 ||
        task->mm->signal_frame_restorer_pc != 0x9200 ||
        task->mm->signal_frame_mask != block_set) {
        errno = EPROTO;
        task->signal->blocked = old_blocked;
        syscall_dispatch_impl(__NR_rt_sigaction, SIGUSR2, (long)(uintptr_t)&old_act,
                              0, sizeof(old_act.sa_mask), 0, 0);
        signal_contract_disable_altstack();
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
        return -1;
    }

    task->signal->blocked = old_blocked;
    syscall_dispatch_impl(__NR_rt_sigaction, SIGUSR2, (long)(uintptr_t)&old_act,
                          0, sizeof(old_act.sa_mask), 0, 0);
    signal_contract_disable_altstack();
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
    return 0;
}

int signal_syscall_contract_rt_sigreturn_restores_mask_and_altstack(void) {
    struct task_struct *task = get_current();
    stack_t stack;
    uint64_t frame_sp = 0;
    uint64_t block_set = 1ULL << (SIGTERM - 1);
    uint64_t unblock_set = block_set;
    uint64_t queried = 0;
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
    memset(&stack, 0, sizeof(stack));
    stack.ss_sp = mapped;
    stack.ss_size = 16384;
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
    if (ret != 0 || (queried & block_set) == 0 || (task->signal->altstack.ss_flags & SS_ONSTACK) != 0) {
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
    struct task_struct *task = get_current();
    stack_t stack;
    uint64_t frame_sp = 0;
    uint64_t frame_words[12] = {0};
    uint64_t block_set = 1ULL << (SIGTERM - 1);
    uint64_t queried = 0;
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
    memset(&stack, 0, sizeof(stack));
    stack.ss_sp = mapped;
    stack.ss_size = 16384;
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
        frame_words[4] != block_set ||
        frame_words[5] != (uint64_t)(uintptr_t)mapped ||
        frame_words[6] != 16384 ||
        frame_words[8] != 0x7ffff000 ||
        frame_words[9] != frame_sp ||
        frame_words[10] != block_set ||
        frame_words[11] == 0 ||
        task->mm->signal_frame_current_sp != 0x7ffff000 ||
        task->mm->signal_frame_size < sizeof(frame_words)) {
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
    if (ret != 0 || (queried & block_set) == 0) {
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
    struct task_struct *task = get_current();
    stack_t stack;
    struct signal_mask_bits old_blocked;
    uint64_t frame_sp = 0;
    uint64_t block_set = 1ULL << (SIGTERM - 1);
    struct ucontext context;
    void *mapped;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    old_blocked = task->signal->blocked;
    signal_contract_disable_altstack();
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 16384, PROT_READ | PROT_WRITE,
                                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        return -1;
    }
    memset(&stack, 0, sizeof(stack));
    stack.ss_sp = mapped;
    stack.ss_size = 16384;
    if (syscall_dispatch_impl(__NR_sigaltstack, (long)(uintptr_t)&stack, 0, 0, 0, 0, 0) != 0 ||
        syscall_dispatch_impl(__NR_rt_sigprocmask, SIG_BLOCK, (long)(uintptr_t)&block_set,
                              0, sizeof(block_set), 0, 0) != 0 ||
        signal_prepare_frame_impl(task, SIGUSR1, 0x13579bdf, 0x7fff0000, &frame_sp) != 0 ||
        task_read_virtual_memory_impl(task, frame_sp + 128, &context, sizeof(context)) !=
            (long)sizeof(context)) {
        errno = EPROTO;
        task->signal->blocked = old_blocked;
        signal_contract_disable_altstack();
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
        return -1;
    }
    if (context.uc_flags != 1 ||
        context.uc_link != 0 ||
        context.uc_stack.ss_sp != mapped ||
        context.uc_stack.ss_size != 16384 ||
        (context.uc_stack.ss_flags & SS_ONSTACK) == 0 ||
        context.uc_sigmask.sig[0] != block_set ||
        context.uc_mcontext.sp != 0x7fff0000 ||
        context.uc_mcontext.pc != 0x13579bdf ||
        task->mm->signal_frame_size < 128 + sizeof(context)) {
        errno = ENODATA;
        task->signal->blocked = old_blocked;
        signal_contract_disable_altstack();
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
        return -1;
    }

    task->signal->blocked = old_blocked;
    signal_contract_disable_altstack();
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
    return 0;
}

int signal_syscall_contract_ignored_dispositions_do_not_queue_or_terminate(void) {
    struct task_struct *task = get_current();
    struct sigaction act;
    struct sigaction dfl;
    struct sigaction old_term;
    long ret;

    if (!task || !task->signal) {
        errno = ESRCH;
        return -1;
    }

    memset(&act, 0, sizeof(act));
    memset(&dfl, 0, sizeof(dfl));
    memset(&old_term, 0, sizeof(old_term));
    dfl.sa_handler = SIG_DFL;

    signal_contract_clear_pending(task, SIGWINCH);
    ret = syscall_dispatch_impl(__NR_rt_sigaction, SIGWINCH, (long)(uintptr_t)&dfl,
                                0, sizeof(dfl.sa_mask), 0, 0);
    if (ret != 0 ||
        signal_generate_task(task, SIGWINCH) != 0 ||
        signal_is_pending(task, SIGWINCH)) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        return -1;
    }

    signal_contract_clear_pending(task, SIGTERM);
    act.sa_handler = SIG_IGN;
    ret = syscall_dispatch_impl(__NR_rt_sigaction, SIGTERM, (long)(uintptr_t)&act,
                                (long)(uintptr_t)&old_term, sizeof(act.sa_mask), 0, 0);
    if (ret != 0 ||
        signal_generate_task(task, SIGTERM) != 0 ||
        signal_is_pending(task, SIGTERM) ||
        atomic_load(&task->exited) ||
        atomic_load(&task->signaled)) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        syscall_dispatch_impl(__NR_rt_sigaction, SIGTERM, (long)(uintptr_t)&old_term,
                              0, sizeof(old_term.sa_mask), 0, 0);
        return -1;
    }

    act.sa_handler = (__sighandler_t)(uintptr_t)0x7000;
    ret = syscall_dispatch_impl(__NR_rt_sigaction, SIGTERM, (long)(uintptr_t)&act,
                                0, sizeof(act.sa_mask), 0, 0);
    signal_contract_clear_pending(task, SIGTERM);
    atomic_store(&task->exited, false);
    atomic_store(&task->signaled, false);
    if (ret != 0 ||
        signal_generate_task(task, SIGTERM) != 0 ||
        !signal_is_pending(task, SIGTERM) ||
        atomic_load(&task->exited) ||
        atomic_load(&task->signaled)) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        syscall_dispatch_impl(__NR_rt_sigaction, SIGTERM, (long)(uintptr_t)&old_term,
                              0, sizeof(old_term.sa_mask), 0, 0);
        return -1;
    }

    signal_contract_clear_pending(task, SIGTERM);
    syscall_dispatch_impl(__NR_rt_sigaction, SIGTERM, (long)(uintptr_t)&old_term,
                          0, sizeof(old_term.sa_mask), 0, 0);
    return 0;
}

int signal_syscall_contract_realtime_queue_preserves_multiplicity_and_order(void) {
    struct task_struct *task = get_current();
    struct signal_mask_bits old_blocked;
    struct signal_mask_bits only_realtime;
    int signo = SIGRTMIN;
    int dequeued = 0;

    if (!task || !task->signal) {
        errno = ESRCH;
        return -1;
    }
    old_blocked = task->signal->blocked;
    only_realtime = signal_contract_mask_all_except(signo);
    memset(&task->signal->blocked, 0, sizeof(task->signal->blocked));
    signal_contract_clear_queued_signal(task, signo);
    signal_contract_clear_queued_signal(task, SIGUSR1);

    if (signal_generate_task_info(task, signo, 100, 0x1000) != 0 ||
        signal_generate_task_info(task, signo, 101, 0x2000) != 0 ||
        signal_contract_queued_count(task, signo) != 2 ||
        signal_dequeue(task, &only_realtime, &dequeued) != 1 ||
        dequeued != signo ||
        !signal_is_pending(task, signo) ||
        signal_contract_queued_count(task, signo) != 1 ||
        signal_dequeue(task, &only_realtime, &dequeued) != 1 ||
        dequeued != signo ||
        signal_is_pending(task, signo) ||
        signal_contract_queued_count(task, signo) != 0) {
        errno = EPROTO;
        signal_contract_clear_queued_signal(task, signo);
        task->signal->blocked = old_blocked;
        return -1;
    }

    if (signal_generate_task(task, SIGUSR1) != 0 ||
        signal_generate_task(task, SIGUSR1) != 0 ||
        signal_contract_queued_count(task, SIGUSR1) != 1) {
        errno = EALREADY;
        signal_contract_clear_queued_signal(task, SIGUSR1);
        task->signal->blocked = old_blocked;
        return -1;
    }
    signal_contract_clear_queued_signal(task, SIGUSR1);
    task->signal->blocked = old_blocked;
    return 0;
}

int signal_syscall_contract_frame_applies_handler_mask_nodefer_and_resethand(void) {
    struct task_struct *task = get_current();
    struct sigaction act;
    struct sigaction old_usr1;
    struct sigaction old_usr2;
    struct signal_mask_bits old_blocked;
    uint64_t frame_sp = 0;
    uint64_t block_term = 1ULL << (SIGTERM - 1);
    uint64_t block_usr1 = 1ULL << (SIGUSR1 - 1);
    uint64_t block_usr2 = 1ULL << (SIGUSR2 - 1);
    void *mapped;
    long ret;

    if (!task || !task->signal) {
        errno = ESRCH;
        return -1;
    }
    memset(&act, 0, sizeof(act));
    memset(&old_usr1, 0, sizeof(old_usr1));
    memset(&old_usr2, 0, sizeof(old_usr2));
    old_blocked = task->signal->blocked;
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 16384, PROT_READ | PROT_WRITE,
                                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        return -1;
    }

    task->signal->blocked.sig[0] = block_term;
    act.sa_handler = (__sighandler_t)(uintptr_t)0x7100;
    act.sa_flags = SA_RESETHAND;
    act.sa_mask.sig[0] = block_usr2;
    ret = syscall_dispatch_impl(__NR_rt_sigaction, SIGUSR1, (long)(uintptr_t)&act,
                                (long)(uintptr_t)&old_usr1, sizeof(act.sa_mask), 0, 0);
    if (ret != 0 ||
        signal_prepare_frame_impl(task, SIGUSR1, 0x1111,
                                  (uint64_t)(uintptr_t)mapped + 16384, &frame_sp) != 0 ||
        task->mm->signal_frame_mask != block_term ||
        (task->signal->blocked.sig[0] & block_term) == 0 ||
        (task->signal->blocked.sig[0] & block_usr1) == 0 ||
        (task->signal->blocked.sig[0] & block_usr2) == 0 ||
        task->signal->actions[SIGUSR1 - 1].handler != SIG_DFL) {
        errno = EPROTO;
        goto out;
    }

    task->signal->blocked.sig[0] = 0;
    memset(&act, 0, sizeof(act));
    act.sa_handler = (__sighandler_t)(uintptr_t)0x7200;
    act.sa_flags = SA_NODEFER;
    ret = syscall_dispatch_impl(__NR_rt_sigaction, SIGUSR2, (long)(uintptr_t)&act,
                                (long)(uintptr_t)&old_usr2, sizeof(act.sa_mask), 0, 0);
    if (ret != 0 ||
        signal_prepare_frame_impl(task, SIGUSR2, 0x2222,
                                  (uint64_t)(uintptr_t)mapped + 16384, &frame_sp) != 0 ||
        (task->signal->blocked.sig[0] & block_usr2) != 0) {
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
        task->signal->blocked = old_blocked;
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
        errno = saved_errno;
    }
    return ret == 0 ? 0 : -1;
}

int signal_syscall_contract_restart_metadata_follows_sa_restart(void) {
    struct task_struct *task = get_current();
    struct sigaction act;
    struct sigaction old_usr1;
    struct signal_mask_bits old_blocked;
    void *mapped;
    uint64_t frame_sp = 0;
    long ret;

    if (!task || !task->signal) {
        errno = ESRCH;
        return -1;
    }

    memset(&act, 0, sizeof(act));
    memset(&old_usr1, 0, sizeof(old_usr1));
    old_blocked = task->signal->blocked;
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
        task->mm->signal_frame_restartable != 1 ||
        task->mm->signal_frame_restart_return_pc != 0x3333 ||
        task->mm->signal_frame_restart_sp != (uint64_t)(uintptr_t)mapped + 16384 ||
        task->mm->signal_frame_restart_signo != SIGUSR1 ||
        task->mm->signal_frame_restart_kind != TASK_RESTART_NONE ||
        syscall_dispatch_impl(__NR_restart_syscall, 0, 0, 0, 0, 0, 0) != -EINTR) {
        errno = EPROTO;
        goto out;
    }

    memset(&act, 0, sizeof(act));
    act.sa_handler = (__sighandler_t)(uintptr_t)0x7400;
    ret = syscall_dispatch_impl(__NR_rt_sigaction, SIGUSR1, (long)(uintptr_t)&act,
                                0, sizeof(act.sa_mask), 0, 0);
    if (ret != 0 ||
        signal_prepare_frame_impl(task, SIGUSR1, 0x4444,
                                  (uint64_t)(uintptr_t)mapped + 16384, &frame_sp) != 0 ||
        task->mm->signal_frame_restartable != 0 ||
        task->mm->signal_frame_restart_return_pc != 0x4444 ||
        task->mm->signal_frame_restart_kind != TASK_RESTART_NONE ||
        syscall_dispatch_impl(__NR_restart_syscall, 0, 0, 0, 0, 0, 0) != -EINTR) {
        errno = ENODATA;
        goto out;
    }

    {
        struct timespec req = {.tv_sec = 0, .tv_nsec = 1000000};
        struct timespec rem = {0};
        task->signal->blocked = old_blocked;
        signal_generate_task(task, SIGUSR1);
        if (nanosleep_impl(&req, &rem) != -1 ||
            errno != EINTR ||
            task->mm->signal_frame_restart_kind != TASK_RESTART_NANOSLEEP ||
            task->mm->signal_frame_restart_arg0 != (uint64_t)(uintptr_t)&req ||
            task->mm->signal_frame_restart_arg1 != (uint64_t)(uintptr_t)&rem) {
            errno = EBADMSG;
            goto out;
        }
        signal_contract_clear_pending(task, SIGUSR1);
        if (syscall_dispatch_impl(__NR_restart_syscall, 0, 0, 0, 0, 0, 0) != 0 ||
            task->mm->signal_frame_restart_kind != TASK_RESTART_NONE) {
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
        task->signal->blocked = old_blocked;
        signal_contract_clear_pending(task, SIGUSR1);
        task->mm->signal_frame_restartable = 0;
        task_restart_clear_impl(task);
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
        errno = saved_errno;
    }
    return ret == 0 ? 0 : -1;
}
