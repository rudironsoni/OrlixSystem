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
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
        return -1;
    }
    if (signal_prepare_frame_impl(task, SIGINT, 0x1234, 0x80000000, &frame_sp) != 0 ||
        frame_sp < (uint64_t)(uintptr_t)mapped ||
        frame_sp >= (uint64_t)(uintptr_t)mapped + 16384 ||
        task->mm->signal_frame_signo != SIGINT ||
        task->mm->signal_frame_return_pc != 0x1234) {
        errno = EPROTO;
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_rt_sigreturn, 0, 0, 0, 0, 0, 0);
    if (ret != 0x1234) {
        errno = EPROTO;
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
        return -1;
    }
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
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
        return -1;
    }

    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 16384, 0, 0, 0, 0);
    return 0;
}
