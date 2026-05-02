#include "SignalSyscallContract.h"

#include <asm/signal.h>
#include <asm/unistd.h>
#include <linux/mman.h>
#include <linux/signal.h>

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "runtime/syscall.h"
#include "kernel/signal.h"
#include "kernel/task.h"

static void signal_contract_disable_altstack(void) {
    stack_t disabled;

    memset(&disabled, 0, sizeof(disabled));
    disabled.ss_flags = SS_DISABLE;
    syscall_dispatch_impl(__NR_sigaltstack, (long)(uintptr_t)&disabled, 0, 0, 0, 0, 0);
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
    uint64_t frame_sp = 0;
    uint64_t frame_words[2] = {0, 0};
    void *mapped;

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
        signal_prepare_frame_impl(task, SIGTERM, 0x5678, 0x80000000, &frame_sp) != 0 ||
        task_read_virtual_memory_impl(task, frame_sp, frame_words, sizeof(frame_words)) !=
            (long)sizeof(frame_words) ||
        frame_words[0] != SIGTERM ||
        frame_words[1] != 0x5678) {
        errno = EPROTO;
        signal_contract_disable_altstack();
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
        return -1;
    }

    signal_contract_disable_altstack();
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
    return 0;
}

int signal_syscall_contract_frame_records_handler_handoff(void) {
    struct task_struct *task = get_current();
    struct sigaction act;
    stack_t stack;
    uint64_t frame_sp = 0;
    void *mapped;
    long ret;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    signal_contract_disable_altstack();
    memset(&act, 0, sizeof(act));
    act.sa_handler = (__sighandler_t)(uintptr_t)0x9000;
    act.sa_flags = SA_RESTART | SA_ONSTACK;
    ret = syscall_dispatch_impl(__NR_rt_sigaction, SIGUSR1, (long)(uintptr_t)&act,
                                0, sizeof(act.sa_mask), 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        return -1;
    }

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
        signal_prepare_frame_impl(task, SIGUSR1, 0xaaaa, 0x80000000, &frame_sp) != 0 ||
        task->mm->signal_handler_pc != 0x9000 ||
        task->mm->signal_frame_flags != (SA_RESTART | SA_ONSTACK)) {
        errno = EPROTO;
        signal_contract_disable_altstack();
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
        return -1;
    }

    signal_contract_disable_altstack();
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
    return 0;
}

int signal_syscall_contract_frame_records_mask_restorer_and_context(void) {
    struct task_struct *task = get_current();
    struct sigaction act;
    stack_t stack;
    uint64_t frame_sp = 0;
    uint64_t frame_words[8] = {0};
    uint64_t block_set = 1ULL << (SIGTERM - 1);
    void *mapped;
    long ret;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    signal_contract_disable_altstack();
    memset(&act, 0, sizeof(act));
    act.sa_handler = (__sighandler_t)(uintptr_t)0x9100;
    act.sa_restorer = (__sigrestore_t)(uintptr_t)0x9200;
    act.sa_flags = SA_RESTART | SA_ONSTACK | SA_RESTORER;
    act.sa_mask.sig[0] = 1ULL << (SIGINT - 1);
    ret = syscall_dispatch_impl(__NR_rt_sigaction, SIGUSR2, (long)(uintptr_t)&act,
                                0, sizeof(act.sa_mask), 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_rt_sigprocmask, SIG_BLOCK, (long)(uintptr_t)&block_set,
                                0, sizeof(block_set), 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        return -1;
    }

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
        task->mm->signal_frame_size != sizeof(frame_words)) {
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
