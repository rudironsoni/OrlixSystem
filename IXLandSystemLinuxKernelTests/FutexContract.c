#include "FutexContract.h"

#include <asm/unistd.h>
#include <linux/futex.h>
#include <linux/mman.h>
#include <linux/sched.h>
#include <linux/time_types.h>

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "runtime/syscall.h"
#include "kernel/task.h"

extern int futex(int *uaddr, int futex_op, int val,
                 const struct timespec *timeout, int *uaddr2, int val3);
extern void exit_impl(int status);

struct futex_wait_thread {
    int *word;
    atomic_int ready;
    int rc;
    int saved_errno;
};

static void *futex_wait_thread_main(void *arg) {
    struct futex_wait_thread *ctx = (struct futex_wait_thread *)arg;
    struct timespec timeout = {2, 0};

    atomic_store(&ctx->ready, 1);
    ctx->rc = futex(ctx->word, FUTEX_WAIT_PRIVATE, 0, &timeout, NULL, 0);
    ctx->saved_errno = errno;
    return NULL;
}

int futex_contract_wait_mismatch_returns_again(void) {
    int word = 1;
    struct __kernel_timespec timeout = {0, 0};
    long ret;

    ret = syscall_dispatch_impl(__NR_futex, (long)(uintptr_t)&word,
                                FUTEX_WAIT_PRIVATE, 0, (long)(uintptr_t)&timeout, 0, 0);
    if (ret != -EAGAIN) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int futex_contract_wake_without_waiters_returns_zero(void) {
    int word = 0;
    long ret;

    ret = syscall_dispatch_impl(__NR_futex, (long)(uintptr_t)&word,
                                FUTEX_WAKE_PRIVATE, 1, 0, 0, 0);
    if (ret != 0) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int futex_contract_wait_timeout_returns_timedout(void) {
    int word = 0;
    struct timespec timeout = {0, 1000000};

    errno = 0;
    if (futex(&word, FUTEX_WAIT_PRIVATE, 0, &timeout, NULL, 0) != -1 || errno != ETIMEDOUT) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int futex_contract_wake_releases_waiter(void) {
    struct futex_wait_thread ctx;
    pthread_t thread;
    int word = 0;
    long ret;

    ctx.word = &word;
    atomic_init(&ctx.ready, 0);
    ctx.rc = -1;
    ctx.saved_errno = 0;

    if (pthread_create(&thread, NULL, futex_wait_thread_main, &ctx) != 0) {
        errno = ECHILD;
        return -1;
    }
    while (atomic_load(&ctx.ready) == 0) {
        /* spin until the waiter has entered the futex path */
    }
    ret = 0;
    for (int i = 0; i < 100000 && ret == 0; i++) {
        ret = syscall_dispatch_impl(__NR_futex, (long)(uintptr_t)&word,
                                    FUTEX_WAKE_PRIVATE, 1, 0, 0, 0);
    }
    pthread_join(thread, NULL);
    if (ret != 1 || ctx.rc != 0 || ctx.saved_errno != 0) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int futex_contract_sets_and_gets_robust_list(void) {
    struct robust_list_head head;
    void *returned_head = NULL;
    unsigned long returned_len = 0;
    long ret;

    ret = syscall_dispatch_impl(__NR_set_robust_list, (long)(uintptr_t)&head, sizeof(head), 0, 0, 0, 0);
    if (ret != 0) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_get_robust_list, 0, (long)(uintptr_t)&returned_head,
                                (long)(uintptr_t)&returned_len, 0, 0, 0);
    if (ret != 0 || returned_head != &head || returned_len != sizeof(head)) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        return -1;
    }
    return 0;
}

int futex_contract_rejects_missing_robust_list_outputs(void) {
    unsigned long returned_len = 0;
    long ret;

    ret = syscall_dispatch_impl(__NR_get_robust_list, 0, 0,
                                (long)(uintptr_t)&returned_len, 0, 0, 0);
    if (ret != -EFAULT) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

struct robust_test_node {
    struct robust_list list;
    int futex_word;
};

int futex_contract_exit_clears_child_tid_and_marks_robust_futex(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child;
    struct task_struct *restore;
    struct robust_list_head head;
    struct robust_test_node node;
    int clear_child_tid = 7;
    long ret;

    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    child = alloc_task();
    if (!child) {
        errno = ENOMEM;
        return -1;
    }

    memset(&head, 0, sizeof(head));
    memset(&node, 0, sizeof(node));
    head.list.next = &node.list;
    head.futex_offset = (long)((char *)&node.futex_word - (char *)&node);
    node.list.next = &head.list;
    node.futex_word = child->pid | FUTEX_WAITERS;

    restore = get_current();
    set_current(child);
    ret = syscall_dispatch_impl(__NR_set_robust_list, (long)(uintptr_t)&head, sizeof(head), 0, 0, 0, 0);
    if (ret != 0) {
        set_current(restore);
        errno = ret < 0 ? (int)-ret : EPROTO;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_set_tid_address, (long)(uintptr_t)&clear_child_tid, 0, 0, 0, 0, 0);
    if (ret != child->pid) {
        set_current(restore);
        errno = ret < 0 ? (int)-ret : EPROTO;
        return -1;
    }

    exit_impl(0);
    set_current(restore);

    if (clear_child_tid != 0 ||
        (node.futex_word & FUTEX_OWNER_DIED) == 0 ||
        (node.futex_word & FUTEX_TID_MASK) != 0) {
        errno = EPROTO;
        free_task(child);
        return -1;
    }
    free_task(child);
    return 0;
}

int futex_contract_clone_thread_shares_vm_and_thread_group(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child;
    int32_t child_pid;
    void *mapped;
    const char byte = 'T';
    char readback = 0;
    int result = -1;

    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 4096, PROT_READ | PROT_WRITE,
                                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        return -1;
    }

    child_pid = clone_impl(CLONE_VM | CLONE_THREAD | CLONE_SIGHAND);
    if (child_pid < 0) {
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 4096, 0, 0, 0, 0);
        return -1;
    }
    child = task_lookup(child_pid);
    if (!child) {
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 4096, 0, 0, 0, 0);
        errno = ESRCH;
        return -1;
    }
    if (child->tgid != parent->tgid || child->mm != parent->mm || child->signal != parent->signal) {
        errno = EPROTO;
        goto out_child;
    }
    if (task_write_virtual_memory_impl(child, (uint64_t)(uintptr_t)mapped, &byte, 1) != 1 ||
        task_read_virtual_memory_impl(parent, (uint64_t)(uintptr_t)mapped, &readback, 1) != 1 ||
        readback != byte) {
        errno = ENODATA;
        goto out_child;
    }
    result = 0;

out_child:
    task_unlink_child_impl(parent, child);
    free_task(child);
    free_task(child);
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 4096, 0, 0, 0, 0);
    return result;
}

int futex_contract_clone3_sets_parent_child_and_clear_tid(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child;
    struct clone_args args;
    int parent_tid = 0;
    int child_tid = 0;
    void *mapped;
    long ret;
    int result = -1;

    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    memset(&args, 0, sizeof(args));
    mapped = (void *)(uintptr_t)syscall_dispatch_impl(__NR_mmap, 0, 4096, PROT_READ | PROT_WRITE,
                                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)(uintptr_t)mapped < 0) {
        errno = -(int)(long)(uintptr_t)mapped;
        return -1;
    }
    args.flags = CLONE_VM | CLONE_THREAD | CLONE_SIGHAND |
                 CLONE_PARENT_SETTID | CLONE_CHILD_SETTID | CLONE_CHILD_CLEARTID;
    args.parent_tid = (uint64_t)(uintptr_t)&parent_tid;
    args.child_tid = (uint64_t)(uintptr_t)&child_tid;

    ret = syscall_dispatch_impl(__NR_clone3, (long)(uintptr_t)&args, sizeof(args), 0, 0, 0, 0);
    if (ret < 0) {
        errno = (int)-ret;
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 4096, 0, 0, 0, 0);
        return -1;
    }
    child = task_lookup((int32_t)ret);
    if (!child) {
        errno = ESRCH;
        syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 4096, 0, 0, 0, 0);
        return -1;
    }
    if (parent_tid != child->pid ||
        child_tid != child->pid ||
        !child->mm ||
        child->mm->clear_child_tid != (uint64_t)(uintptr_t)&child_tid) {
        errno = EPROTO;
        goto out_child;
    }
    result = 0;

out_child:
    task_unlink_child_impl(parent, child);
    free_task(child);
    free_task(child);
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 4096, 0, 0, 0, 0);
    return result;
}
