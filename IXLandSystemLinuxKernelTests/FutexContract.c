#include "FutexContract.h"

#include <asm/unistd.h>
#include <linux/futex.h>
#include <linux/capability.h>
#include <linux/mman.h>
#include <linux/sched.h>
#include <linux/time_types.h>
#ifdef SIGUSR1
#undef SIGUSR1
#endif
#define __ASSEMBLY__ 1
#include <asm-generic/signal.h>
#undef __ASSEMBLY__

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "runtime/syscall.h"
#include "kernel/cred_internal.h"
#include "kernel/signal.h"
#include "kernel/task.h"

extern int futex(int *uaddr, int futex_op, int val,
                 const struct timespec *timeout, int *uaddr2, int val3);
extern void exit_impl(int status);
extern int capget(cap_user_header_t header, cap_user_data_t data);
extern int capset(cap_user_header_t header, const cap_user_data_t data);
extern int unshare_impl(uint64_t flags);

struct futex_wait_thread {
    int *word;
    atomic_int ready;
    int rc;
    int saved_errno;
    struct task_struct *task;
};

struct futex_wait_op_thread {
    int *uaddr;
    int op;
    int val;
    struct timespec timeout;
    int *uaddr2;
    int val3;
    atomic_int ready;
    int rc;
    int saved_errno;
};

static void futex_clear_pending_signal(struct task_struct *task, int sig) {
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

static void futex_release_lookup_child(struct task_struct *parent, struct task_struct **child) {
    if (!parent || !child || !*child) {
        return;
    }
    task_unlink_child_impl(parent, *child);
    free_task(*child);
    free_task(*child);
    *child = NULL;
}

static void *futex_wait_thread_main(void *arg) {
    struct futex_wait_thread *ctx = (struct futex_wait_thread *)arg;
    struct timespec timeout = {2, 0};

    if (ctx->task) {
        set_current(ctx->task);
    }
    atomic_store(&ctx->ready, 1);
    ctx->rc = futex(ctx->word, FUTEX_WAIT_PRIVATE, 0, &timeout, NULL, 0);
    ctx->saved_errno = errno;
    return NULL;
}

static void *futex_wait_op_thread_main(void *arg) {
    struct futex_wait_op_thread *ctx = (struct futex_wait_op_thread *)arg;

    atomic_store(&ctx->ready, 1);
    ctx->rc = futex(ctx->uaddr, ctx->op, ctx->val, &ctx->timeout, ctx->uaddr2, ctx->val3);
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
    if (futex(&word, FUTEX_WAIT_PRIVATE, 0, &timeout, NULL, 0) != -1) {
        errno = EPROTO;
        return -1;
    }
    if (errno != ETIMEDOUT) {
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
    ctx.task = NULL;

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

int futex_contract_wake_bitset_only_wakes_matching_waiter(void) {
    pthread_t t1;
    pthread_t t2;
    struct futex_wait_op_thread w1;
    struct futex_wait_op_thread w2;
    int word = 0;
    long ret;

    memset(&w1, 0, sizeof(w1));
    memset(&w2, 0, sizeof(w2));

    w1.uaddr = &word;
    w1.op = FUTEX_WAIT_BITSET_PRIVATE;
    w1.val = 0;
    w1.timeout.tv_sec = 0;
    w1.timeout.tv_nsec = 200000000; /* 200ms */
    w1.uaddr2 = NULL;
    w1.val3 = 0x00000001;
    atomic_init(&w1.ready, 0);
    w1.rc = -1;
    w1.saved_errno = 0;

    w2.uaddr = &word;
    w2.op = FUTEX_WAIT_BITSET_PRIVATE;
    w2.val = 0;
    w2.timeout.tv_sec = 0;
    w2.timeout.tv_nsec = 200000000; /* 200ms */
    w2.uaddr2 = NULL;
    w2.val3 = 0x00000002;
    atomic_init(&w2.ready, 0);
    w2.rc = -1;
    w2.saved_errno = 0;

    if (pthread_create(&t1, NULL, futex_wait_op_thread_main, &w1) != 0) {
        errno = ECHILD;
        return -1;
    }
    if (pthread_create(&t2, NULL, futex_wait_op_thread_main, &w2) != 0) {
        pthread_join(t1, NULL);
        errno = ECHILD;
        return -1;
    }
    while (atomic_load(&w1.ready) == 0 || atomic_load(&w2.ready) == 0) {
        /* spin */
    }

    ret = syscall_dispatch_impl(__NR_futex, (long)(uintptr_t)&word,
                                FUTEX_WAKE_BITSET_PRIVATE, 1, 0,
                                0, 0x00000001);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    if (ret != 1) {
        errno = EPROTO;
        return -1;
    }

    /* exactly one waiter should have been woken, the other should time out */
    if (!((w1.rc == 0 && w1.saved_errno == 0 && w2.rc == -1 && w2.saved_errno == ETIMEDOUT) ||
          (w2.rc == 0 && w2.saved_errno == 0 && w1.rc == -1 && w1.saved_errno == ETIMEDOUT))) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int futex_contract_requeue_moves_waiter_to_new_uaddr(void) {
    pthread_t thread;
    struct futex_wait_thread ctx;
    int word1 = 0;
    int word2 = 0;
    long ret;

    memset(&ctx, 0, sizeof(ctx));
    ctx.word = &word1;
    atomic_init(&ctx.ready, 0);
    ctx.rc = -1;
    ctx.saved_errno = 0;
    ctx.task = NULL;

    if (pthread_create(&thread, NULL, futex_wait_thread_main, &ctx) != 0) {
        errno = ECHILD;
        return -1;
    }
    while (atomic_load(&ctx.ready) == 0) {
        /* spin */
    }

    ret = 0;
    for (int i = 0; i < 100000 && ret == 0; i++) {
        ret = syscall_dispatch_impl(__NR_futex, (long)(uintptr_t)&word1,
                                    FUTEX_REQUEUE_PRIVATE, 0,
                                    1 /* nr_requeue */, (long)(uintptr_t)&word2, 0);
    }
    if (ret != 1) {
        pthread_join(thread, NULL);
        errno = EPROTO;
        return -1;
    }

    ret = syscall_dispatch_impl(__NR_futex, (long)(uintptr_t)&word2,
                                FUTEX_WAKE_PRIVATE, 1, 0, 0, 0);
    pthread_join(thread, NULL);

    if (ret != 1 || ctx.rc != 0 || ctx.saved_errno != 0) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int futex_contract_cmp_requeue_rejects_mismatch(void) {
    int word1 = 123;
    int word2 = 0;
    long ret;

    ret = syscall_dispatch_impl(__NR_futex, (long)(uintptr_t)&word1,
                                FUTEX_CMP_REQUEUE_PRIVATE, 0,
                                1 /* nr_requeue */, (long)(uintptr_t)&word2,
                                999 /* cmpval mismatch */);
    if (ret != -EAGAIN) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int futex_contract_interrupted_wait_records_restart(void) {
    struct futex_wait_thread ctx;
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
    struct task_struct *restore;
    pthread_t thread;
    int word = 0;
    long ret;

    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    child = task_create_child_impl(parent);
    if (!child) {
        return -1;
    }

    ctx.word = &word;
    ctx.task = child;
    atomic_init(&ctx.ready, 0);
    ctx.rc = -1;
    ctx.saved_errno = 0;

    if (pthread_create(&thread, NULL, futex_wait_thread_main, &ctx) != 0) {
        task_unlink_child_impl(parent, child);
        free_task(child);
        errno = ECHILD;
        return -1;
    }
    while (atomic_load(&ctx.ready) == 0) {
        /* spin until the waiter has entered the futex path */
    }
    if (signal_generate_task(child, SIGUSR1) != 0) {
        pthread_join(thread, NULL);
        task_unlink_child_impl(parent, child);
        free_task(child);
        return -1;
    }
    pthread_join(thread, NULL);

    if (ctx.rc != -1 || ctx.saved_errno != EINTR ||
        !child->mm ||
        child->mm->signal_frame_restart_kind != TASK_RESTART_FUTEX_WAIT ||
        child->mm->signal_frame_restart_arg0 != (uint64_t)(uintptr_t)&word ||
        child->mm->signal_frame_restart_arg1 != 0 ||
        child->mm->signal_frame_restart_arg2 != 2000) {
        task_unlink_child_impl(parent, child);
        free_task(child);
        errno = ENODATA;
        return -1;
    }

    futex_clear_pending_signal(child, SIGUSR1);
    atomic_store((_Atomic int *)&word, 1);
    restore = get_current();
    set_current(child);
    ret = syscall_dispatch_impl(__NR_restart_syscall, 0, 0, 0, 0, 0, 0);
    set_current(restore);

    task_unlink_child_impl(parent, child);
    free_task(child);

    if (ret != -EAGAIN) {
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
        child->clear_child_tid != (uint64_t)(uintptr_t)&child_tid) {
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

int futex_contract_clone3_set_tid_supports_repo_pid_model(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child = NULL;
    struct clone_args args;
    struct __user_cap_header_struct header = {
        .version = _LINUX_CAPABILITY_VERSION_3,
        .pid = 0,
    };
    struct __user_cap_data_struct original_caps[_LINUX_CAPABILITY_U32S_3];
    struct __user_cap_data_struct modified_caps[_LINUX_CAPABILITY_U32S_3];
    int requested_pid = 0;
    int invalid_size_tids[2] = {0};
    int invalid_ns_pid = 2;
    int one_pid = 1;
    int pending_invalid_ns_pid = 2;
    int pending_one_pid = 1;
    long ret;
    int result = -1;
    bool caps_modified = false;

    if (!parent || !parent->cred) {
        errno = ESRCH;
        return -1;
    }

    for (int candidate = 64; candidate < 512; candidate++) {
        struct task_struct *existing = task_lookup(candidate);
        if (!existing) {
            requested_pid = candidate;
            break;
        }
        free_task(existing);
    }
    if (requested_pid == 0) {
        errno = EAGAIN;
        return -1;
    }

    memset(&args, 0, sizeof(args));
    args.set_tid = (uint64_t)(uintptr_t)&requested_pid;
    args.set_tid_size = 1;
    ret = syscall_dispatch_impl(__NR_clone3, (long)(uintptr_t)&args, sizeof(args), 0, 0, 0, 0);
    if (ret != requested_pid) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    child = task_lookup((int32_t)ret);
    if (!child) {
        errno = ESRCH;
        goto out;
    }
    if (child->pid != requested_pid ||
        child->ns_pid != requested_pid ||
        child->pid_ns_level != parent->pid_ns_level) {
        errno = EPROTO;
        goto out;
    }
    futex_release_lookup_child(parent, &child);

    memset(&args, 0, sizeof(args));
    args.set_tid = (uint64_t)(uintptr_t)&parent->pid;
    args.set_tid_size = 1;
    ret = syscall_dispatch_impl(__NR_clone3, (long)(uintptr_t)&args, sizeof(args), 0, 0, 0, 0);
    if (ret != -EEXIST) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    invalid_size_tids[0] = requested_pid + 1;
    invalid_size_tids[1] = 1;
    memset(&args, 0, sizeof(args));
    args.set_tid = (uint64_t)(uintptr_t)invalid_size_tids;
    args.set_tid_size = 2;
    ret = syscall_dispatch_impl(__NR_clone3, (long)(uintptr_t)&args, sizeof(args), 0, 0, 0, 0);
    if (ret != -EINVAL) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    invalid_size_tids[0] = 0;
    memset(&args, 0, sizeof(args));
    args.set_tid = (uint64_t)(uintptr_t)invalid_size_tids;
    args.set_tid_size = 1;
    ret = syscall_dispatch_impl(__NR_clone3, (long)(uintptr_t)&args, sizeof(args), 0, 0, 0, 0);
    if (ret != -EINVAL) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    memset(&args, 0, sizeof(args));
    args.flags = CLONE_NEWPID;
    args.set_tid = (uint64_t)(uintptr_t)&invalid_ns_pid;
    args.set_tid_size = 1;
    ret = syscall_dispatch_impl(__NR_clone3, (long)(uintptr_t)&args, sizeof(args), 0, 0, 0, 0);
    if (ret != -EINVAL) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    memset(&args, 0, sizeof(args));
    args.flags = CLONE_NEWPID;
    args.set_tid = (uint64_t)(uintptr_t)&one_pid;
    args.set_tid_size = 1;
    ret = syscall_dispatch_impl(__NR_clone3, (long)(uintptr_t)&args, sizeof(args), 0, 0, 0, 0);
    if (ret < 0) {
        errno = (int)-ret;
        goto out;
    }
    child = task_lookup((int32_t)ret);
    if (!child) {
        errno = ESRCH;
        goto out;
    }
    if (child->ns_pid != 1 || child->pid_ns_level != parent->pid_ns_level + 1) {
        errno = EPROTO;
        goto out;
    }
    futex_release_lookup_child(parent, &child);

    if (unshare_impl(CLONE_NEWPID) != 0) {
        goto out;
    }

    memset(&args, 0, sizeof(args));
    args.set_tid = (uint64_t)(uintptr_t)&pending_invalid_ns_pid;
    args.set_tid_size = 1;
    ret = syscall_dispatch_impl(__NR_clone3, (long)(uintptr_t)&args, sizeof(args), 0, 0, 0, 0);
    if (ret != -EINVAL) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }

    memset(&args, 0, sizeof(args));
    args.set_tid = (uint64_t)(uintptr_t)&pending_one_pid;
    args.set_tid_size = 1;
    ret = syscall_dispatch_impl(__NR_clone3, (long)(uintptr_t)&args, sizeof(args), 0, 0, 0, 0);
    if (ret < 0) {
        errno = (int)-ret;
        goto out;
    }
    child = task_lookup((int32_t)ret);
    if (!child) {
        errno = ESRCH;
        goto out;
    }
    if (child->ns_pid != 1 || child->pid_ns_level != parent->pid_ns_level + 1) {
        errno = EPROTO;
        goto out;
    }
    futex_release_lookup_child(parent, &child);

    if (capget(&header, original_caps) != 0) {
        goto out;
    }
    memcpy(modified_caps, original_caps, sizeof(modified_caps));
    modified_caps[CAP_SYS_ADMIN / 32].effective &= ~(1U << (CAP_SYS_ADMIN % 32));
    modified_caps[CAP_CHECKPOINT_RESTORE / 32].effective &= ~(1U << (CAP_CHECKPOINT_RESTORE % 32));
    if (capset(&header, modified_caps) != 0) {
        goto out;
    }
    caps_modified = true;

    memset(&args, 0, sizeof(args));
    args.set_tid = (uint64_t)(uintptr_t)&requested_pid;
    args.set_tid_size = 1;
    ret = syscall_dispatch_impl(__NR_clone3, (long)(uintptr_t)&args, sizeof(args), 0, 0, 0, 0);
    if (ret != -EPERM) {
        errno = ret < 0 ? (int)-ret : EPROTO;
        goto out;
    }
    result = 0;

out:
    futex_release_lookup_child(parent, &child);
    if (caps_modified) {
        int saved_errno = errno;
        if (capset(&header, original_caps) != 0) {
            return -1;
        }
        errno = saved_errno;
    }
    return result;
}

int futex_contract_clear_child_tid_is_per_thread_not_mm_shared(void) {
    struct task_struct *parent = get_current();
    struct task_struct *child;
    struct task_struct *restore;
    struct clone_args args;
    int child_tid = 0;
    int parent_clear = 11;
    long ret;
    int result = -1;

    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_set_tid_address, (long)(uintptr_t)&parent_clear, 0, 0, 0, 0, 0);
    if (ret != parent->pid || parent->clear_child_tid != (uint64_t)(uintptr_t)&parent_clear) {
        errno = EPROTO;
        return -1;
    }

    memset(&args, 0, sizeof(args));
    args.flags = CLONE_VM | CLONE_THREAD | CLONE_SIGHAND |
                 CLONE_CHILD_SETTID | CLONE_CHILD_CLEARTID;
    args.child_tid = (uint64_t)(uintptr_t)&child_tid;
    ret = syscall_dispatch_impl(__NR_clone3, (long)(uintptr_t)&args, sizeof(args), 0, 0, 0, 0);
    if (ret < 0) {
        errno = (int)-ret;
        return -1;
    }
    child = task_lookup((int32_t)ret);
    if (!child) {
        errno = ESRCH;
        return -1;
    }
    if (child->mm != parent->mm ||
        parent->clear_child_tid != (uint64_t)(uintptr_t)&parent_clear ||
        child->clear_child_tid != (uint64_t)(uintptr_t)&child_tid ||
        child_tid != child->pid) {
        errno = EPROTO;
        goto out_child;
    }

    restore = get_current();
    set_current(child);
    exit_impl(0);
    set_current(restore);
    if (child_tid != 0 ||
        parent_clear != 11 ||
        parent->clear_child_tid != (uint64_t)(uintptr_t)&parent_clear) {
        errno = ENODATA;
        goto out_child;
    }
    result = 0;

out_child:
    task_unlink_child_impl(parent, child);
    free_task(child);
    free_task(child);
    parent->clear_child_tid = 0;
    return result;
}
