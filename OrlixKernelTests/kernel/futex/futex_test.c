#include <uapi/asm/unistd.h>
#include <uapi/linux/errno.h>
#include <uapi/linux/capability.h>
#include <uapi/linux/futex.h>
#include <uapi/linux/mman.h>
#include <uapi/linux/sched.h>
#include <uapi/linux/signal.h>
#include <uapi/linux/time.h>
#include <linux/string.h>

#include "../../kunit/kunit.h"
#include "../../kunit/suite_registry.h"
#include "kernel/init.h"
#include "kernel/cred.h"
#include "kernel/futex.h"
#include "kernel/signal.h"
#include "private/kernel/futex_state.h"
#include "private/kernel/signal_state.h"
#include "kernel/task.h"
#include "private/kernel/kthread_state.h"
#include "private/kernel/task_state.h"
#include "runtime/syscall.h"

extern void exit_impl(int status);
extern int capget_impl(cap_user_header_t header, cap_user_data_t data);
extern int capset_impl(cap_user_header_t header, const cap_user_data_t data);
extern int unshare_impl(uint64_t flags);
extern int library_init(const void *config);
extern int library_is_initialized(void);
extern int errno;

struct futex_wait_thread {
    int *word;
    kernel_mutex_t lock;
    kernel_cond_t cond;
    int started;
    int done;
    int rc;
    int saved_errno;
    struct task *task;
};

struct futex_wait_op_thread {
    int *uaddr;
    int op;
    int val;
    struct __kernel_timespec timeout;
    int *uaddr2;
    int val3;
    kernel_mutex_t lock;
    kernel_cond_t cond;
    int started;
    int done;
    int rc;
    int saved_errno;
};

static void futex_wait_thread_init(struct futex_wait_thread *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    kernel_mutex_init(&ctx->lock);
    kernel_cond_init(&ctx->cond);
}

static void futex_wait_thread_destroy(struct futex_wait_thread *ctx) {
    kernel_cond_destroy(&ctx->cond);
    kernel_mutex_destroy(&ctx->lock);
}

static void futex_wait_thread_mark_started(struct futex_wait_thread *ctx) {
    kernel_mutex_lock(&ctx->lock);
    ctx->started = 1;
    kernel_cond_broadcast(&ctx->cond);
    kernel_mutex_unlock(&ctx->lock);
}

static void futex_wait_thread_mark_done(struct futex_wait_thread *ctx) {
    kernel_mutex_lock(&ctx->lock);
    ctx->done = 1;
    kernel_cond_broadcast(&ctx->cond);
    kernel_mutex_unlock(&ctx->lock);
}

static void futex_wait_thread_wait_started(struct futex_wait_thread *ctx) {
    kernel_mutex_lock(&ctx->lock);
    while (!ctx->started) {
        kernel_cond_wait(&ctx->cond, &ctx->lock);
    }
    kernel_mutex_unlock(&ctx->lock);
}

static void futex_wait_thread_wait_done(struct futex_wait_thread *ctx) {
    kernel_mutex_lock(&ctx->lock);
    while (!ctx->done) {
        kernel_cond_wait(&ctx->cond, &ctx->lock);
    }
    kernel_mutex_unlock(&ctx->lock);
}

static void futex_wait_op_thread_init(struct futex_wait_op_thread *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    kernel_mutex_init(&ctx->lock);
    kernel_cond_init(&ctx->cond);
}

static void futex_wait_op_thread_destroy(struct futex_wait_op_thread *ctx) {
    kernel_cond_destroy(&ctx->cond);
    kernel_mutex_destroy(&ctx->lock);
}

static void futex_wait_op_thread_mark_started(struct futex_wait_op_thread *ctx) {
    kernel_mutex_lock(&ctx->lock);
    ctx->started = 1;
    kernel_cond_broadcast(&ctx->cond);
    kernel_mutex_unlock(&ctx->lock);
}

static void futex_wait_op_thread_mark_done(struct futex_wait_op_thread *ctx) {
    kernel_mutex_lock(&ctx->lock);
    ctx->done = 1;
    kernel_cond_broadcast(&ctx->cond);
    kernel_mutex_unlock(&ctx->lock);
}

static void futex_wait_op_thread_wait_started(struct futex_wait_op_thread *ctx) {
    kernel_mutex_lock(&ctx->lock);
    while (!ctx->started) {
        kernel_cond_wait(&ctx->cond, &ctx->lock);
    }
    kernel_mutex_unlock(&ctx->lock);
}

static void futex_wait_op_thread_wait_done(struct futex_wait_op_thread *ctx) {
    kernel_mutex_lock(&ctx->lock);
    while (!ctx->done) {
        kernel_cond_wait(&ctx->cond, &ctx->lock);
    }
    kernel_mutex_unlock(&ctx->lock);
}

static void futex_release_lookup_child(struct task *parent, struct task **child) {
    if (!parent || !child || !*child) {
        return;
    }
    task_unlink_child_impl(parent, *child);
    task_put(*child);
    task_put(*child);
    *child = NULL;
}

void futex_contract_reset_test_state(void) {
    struct task *child;

    if (!library_is_initialized()) {
        library_init(NULL);
    }
    start_kernel();

    if (!kernel_is_booted() || !task_init_process) {
        return;
    }

    futex_reset_impl();

    task_set_current(task_init_process);
    task_init_process->parent = NULL;
    task_init_process->ppid = 0;
    task_init_process->exit_status = 0;
    atomic_set(&task_init_process->exited, 0);
    atomic_set(&task_init_process->signaled, 0);
    atomic_set(&task_init_process->termsig, 0);
    atomic_set(&task_init_process->stopped, 0);
    atomic_set(&task_init_process->state, RUN_STATE_RUNNING);
    atomic_set(&task_init_process->continued, 0);
    atomic_set(&task_init_process->stop_report_pending, 0);
    atomic_set(&task_init_process->continue_report_pending, 0);

    signal_reset_task_state(task_init_process);
    signal_frame_clear_task(task_init_process);

    while ((child = task_init_process->children) != NULL) {
        task_unlink_child_impl(task_init_process, child);
        child->parent = NULL;
        child->ppid = 0;
    }

    task_set_current(task_init_process);
}

static void *futex_wait_thread_main(void *arg) {
    struct futex_wait_thread *ctx = (struct futex_wait_thread *)arg;
    struct __kernel_timespec timeout = {2, 0};

    if (ctx->task) {
        task_set_current(ctx->task);
    }
    futex_wait_thread_mark_started(ctx);
    ctx->rc = (int)syscall_dispatch_impl(__NR_futex, (long)(uintptr_t)ctx->word,
                                         FUTEX_WAIT_PRIVATE, 0, (long)(uintptr_t)&timeout, 0, 0);
    if (ctx->rc < 0) {
        errno = -ctx->rc;
        ctx->rc = -1;
    }
    ctx->saved_errno = errno;
    futex_wait_thread_mark_done(ctx);
    return NULL;
}

static void *futex_wait_op_thread_main(void *arg) {
    struct futex_wait_op_thread *ctx = (struct futex_wait_op_thread *)arg;

    futex_wait_op_thread_mark_started(ctx);
    ctx->rc = (int)syscall_dispatch_impl(__NR_futex, (long)(uintptr_t)ctx->uaddr,
                                         ctx->op, ctx->val, (long)(uintptr_t)&ctx->timeout,
                                         (long)(uintptr_t)ctx->uaddr2, ctx->val3);
    if (ctx->rc < 0) {
        errno = -ctx->rc;
        ctx->rc = -1;
    }
    ctx->saved_errno = errno;
    futex_wait_op_thread_mark_done(ctx);
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
    struct __kernel_timespec timeout = {0, 1000000};
    long ret;

    errno = 0;
    ret = syscall_dispatch_impl(__NR_futex, (long)(uintptr_t)&word,
                                FUTEX_WAIT_PRIVATE, 0, (long)(uintptr_t)&timeout, 0, 0);
    if (ret != -ETIMEDOUT) {
        errno = EPROTO;
        return -1;
    }
    errno = ETIMEDOUT;
    return 0;
}

int futex_contract_wake_releases_waiter(void) {
    struct futex_wait_thread ctx;
    kernel_thread_t thread;
    int word = 0;
    long ret;

    futex_wait_thread_init(&ctx);
    ctx.word = &word;
    ctx.rc = -1;
    ctx.saved_errno = 0;
    ctx.task = NULL;

    if (kernel_thread_create(&thread, NULL, futex_wait_thread_main, &ctx) != 0) {
        futex_wait_thread_destroy(&ctx);
        errno = ECHILD;
        return -1;
    }
    kernel_thread_detach(thread);
    futex_wait_thread_wait_started(&ctx);
    ret = 0;
    for (int i = 0; i < 100000 && ret == 0; i++) {
        ret = syscall_dispatch_impl(__NR_futex, (long)(uintptr_t)&word,
                                    FUTEX_WAKE_PRIVATE, 1, 0, 0, 0);
    }
    futex_wait_thread_wait_done(&ctx);
    futex_wait_thread_destroy(&ctx);
    if (ret != 1 || ctx.rc != 0 || ctx.saved_errno != 0) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int futex_contract_wake_bitset_only_wakes_matching_waiter(void) {
    kernel_thread_t t1;
    kernel_thread_t t2;
    struct futex_wait_op_thread w1;
    struct futex_wait_op_thread w2;
    int word = 0;
    long ret;

    futex_wait_op_thread_init(&w1);
    futex_wait_op_thread_init(&w2);

    w1.uaddr = &word;
    w1.op = FUTEX_WAIT_BITSET_PRIVATE;
    w1.val = 0;
    w1.timeout.tv_sec = 0;
    w1.timeout.tv_nsec = 200000000; /* 200ms */
    w1.uaddr2 = NULL;
    w1.val3 = 0x00000001;
    w1.rc = -1;
    w1.saved_errno = 0;

    w2.uaddr = &word;
    w2.op = FUTEX_WAIT_BITSET_PRIVATE;
    w2.val = 0;
    w2.timeout.tv_sec = 0;
    w2.timeout.tv_nsec = 200000000; /* 200ms */
    w2.uaddr2 = NULL;
    w2.val3 = 0x00000002;
    w2.rc = -1;
    w2.saved_errno = 0;

    if (kernel_thread_create(&t1, NULL, futex_wait_op_thread_main, &w1) != 0) {
        futex_wait_op_thread_destroy(&w1);
        futex_wait_op_thread_destroy(&w2);
        errno = ECHILD;
        return -1;
    }
    kernel_thread_detach(t1);
    if (kernel_thread_create(&t2, NULL, futex_wait_op_thread_main, &w2) != 0) {
        futex_wait_op_thread_wait_done(&w1);
        futex_wait_op_thread_destroy(&w1);
        futex_wait_op_thread_destroy(&w2);
        errno = ECHILD;
        return -1;
    }
    kernel_thread_detach(t2);
    futex_wait_op_thread_wait_started(&w1);
    futex_wait_op_thread_wait_started(&w2);

    ret = syscall_dispatch_impl(__NR_futex, (long)(uintptr_t)&word,
                                FUTEX_WAKE_BITSET_PRIVATE, 1, 0,
                                0, 0x00000001);

    futex_wait_op_thread_wait_done(&w1);
    futex_wait_op_thread_wait_done(&w2);
    futex_wait_op_thread_destroy(&w1);
    futex_wait_op_thread_destroy(&w2);

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
    kernel_thread_t thread;
    struct futex_wait_thread ctx;
    int word1 = 0;
    int word2 = 0;
    long ret;

    futex_wait_thread_init(&ctx);
    ctx.word = &word1;
    ctx.rc = -1;
    ctx.saved_errno = 0;
    ctx.task = NULL;

    if (kernel_thread_create(&thread, NULL, futex_wait_thread_main, &ctx) != 0) {
        futex_wait_thread_destroy(&ctx);
        errno = ECHILD;
        return -1;
    }
    kernel_thread_detach(thread);
    futex_wait_thread_wait_started(&ctx);

    ret = 0;
    for (int i = 0; i < 100000 && ret == 0; i++) {
        ret = syscall_dispatch_impl(__NR_futex, (long)(uintptr_t)&word1,
                                    FUTEX_REQUEUE_PRIVATE, 0,
                                    1 /* nr_requeue */, (long)(uintptr_t)&word2, 0);
    }
    if (ret != 1) {
        futex_wait_thread_wait_done(&ctx);
        futex_wait_thread_destroy(&ctx);
        errno = EPROTO;
        return -1;
    }

    ret = syscall_dispatch_impl(__NR_futex, (long)(uintptr_t)&word2,
                                FUTEX_WAKE_PRIVATE, 1, 0, 0, 0);
    futex_wait_thread_wait_done(&ctx);
    futex_wait_thread_destroy(&ctx);

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
    struct task *parent = task_current();
    struct task *child = NULL;
    struct task *restore;
    kernel_thread_t thread;
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

    futex_wait_thread_init(&ctx);
    ctx.word = &word;
    ctx.task = child;
    ctx.rc = -1;
    ctx.saved_errno = 0;

    if (kernel_thread_create(&thread, NULL, futex_wait_thread_main, &ctx) != 0) {
        futex_wait_thread_destroy(&ctx);
        task_unlink_child_impl(parent, child);
        task_put(child);
        errno = ECHILD;
        return -1;
    }
    kernel_thread_detach(thread);
    futex_wait_thread_wait_started(&ctx);
    if (signal_generate_task(child, SIGUSR1) != 0) {
        futex_wait_thread_wait_done(&ctx);
        futex_wait_thread_destroy(&ctx);
        task_unlink_child_impl(parent, child);
        task_put(child);
        return -1;
    }
    futex_wait_thread_wait_done(&ctx);
    futex_wait_thread_destroy(&ctx);

    {
        if (ctx.rc != -1 || ctx.saved_errno != EINTR ||
            !signal_frame_restart_matches_task(child, TASK_RESTART_FUTEX_WAIT,
                                               (uint64_t)(uintptr_t)&word,
                                               0, 2000, 0, 0, 0)) {
        task_unlink_child_impl(parent, child);
        task_put(child);
        errno = ENODATA;
        return -1;
        }
    }

    signal_clear_pending_task(child, SIGUSR1);
    word = 1;
    restore = task_current();
    task_set_current(child);
    ret = syscall_dispatch_impl(__NR_restart_syscall, 0, 0, 0, 0, 0, 0);
    task_set_current(restore);

    task_unlink_child_impl(parent, child);
    task_put(child);

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
    struct task *parent = task_current();
    struct task *child;
    struct task *restore;
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

    restore = task_current();
    task_set_current(child);
    ret = syscall_dispatch_impl(__NR_set_robust_list, (long)(uintptr_t)&head, sizeof(head), 0, 0, 0, 0);
    if (ret != 0) {
        task_set_current(restore);
        errno = ret < 0 ? (int)-ret : EPROTO;
        return -1;
    }
    ret = syscall_dispatch_impl(__NR_set_tid_address, (long)(uintptr_t)&clear_child_tid, 0, 0, 0, 0, 0);
    if (ret != child->pid) {
        task_set_current(restore);
        errno = ret < 0 ? (int)-ret : EPROTO;
        return -1;
    }

    exit_impl(0);
    task_set_current(restore);

    if (clear_child_tid != 0 ||
        (node.futex_word & FUTEX_OWNER_DIED) == 0 ||
        (node.futex_word & FUTEX_TID_MASK) != 0) {
        errno = EPROTO;
        task_put(child);
        return -1;
    }
    task_put(child);
    return 0;
}

int futex_contract_clone_thread_shares_vm_and_thread_group(void) {
    struct task *parent = task_current();
    struct task *child;
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
    task_put(child);
    task_put(child);
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 4096, 0, 0, 0, 0);
    return result;
}

int futex_contract_clone3_sets_parent_child_and_clear_tid(void) {
    struct task *parent = task_current();
    struct task *child;
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
    task_put(child);
    task_put(child);
    syscall_dispatch_impl(__NR_munmap, (long)(uintptr_t)mapped, 4096, 0, 0, 0, 0);
    return result;
}

int futex_contract_clone3_set_tid_supports_repo_pid_model(void) {
    struct task *parent = task_current();
    struct task *child = NULL;
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
        struct task *existing = task_lookup(candidate);
        if (!existing) {
            requested_pid = candidate;
            break;
        }
        task_put(existing);
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

    if (capget_impl(&header, original_caps) != 0) {
        goto out;
    }
    memcpy(modified_caps, original_caps, sizeof(modified_caps));
    modified_caps[CAP_SYS_ADMIN / 32].effective &= ~(1U << (CAP_SYS_ADMIN % 32));
    modified_caps[CAP_CHECKPOINT_RESTORE / 32].effective &= ~(1U << (CAP_CHECKPOINT_RESTORE % 32));
    if (capset_impl(&header, modified_caps) != 0) {
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
        if (capset_impl(&header, original_caps) != 0) {
            return -1;
        }
        errno = saved_errno;
    }
    return result;
}

int futex_contract_clear_child_tid_is_per_thread_not_mm_shared(void) {
    struct task *parent = task_current();
    struct task *child;
    struct task *restore;
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

    restore = task_current();
    task_set_current(child);
    exit_impl(0);
    task_set_current(restore);
    if (child_tid != 0 ||
        parent_clear != 11 ||
        parent->clear_child_tid != (uint64_t)(uintptr_t)&parent_clear) {
        errno = ENODATA;
        goto out_child;
    }
    result = 0;

out_child:
    task_unlink_child_impl(parent, child);
    task_put(child);
    task_put(child);
    parent->clear_child_tid = 0;
    return result;
}

static void futex_suite_init(struct kunit *test) {
    (void)test;
    futex_contract_reset_test_state();
}

static void test_wait_mismatch_returns_again(struct kunit *test) {
    if (futex_contract_wait_mismatch_returns_again() != 0) {
        KUNIT_FAIL(test, "wait_mismatch_returns_again failed with errno %d", errno);
    }
}

static void test_wake_without_waiters_returns_zero(struct kunit *test) {
    if (futex_contract_wake_without_waiters_returns_zero() != 0) {
        KUNIT_FAIL(test, "wake_without_waiters_returns_zero failed with errno %d", errno);
    }
}

static void test_wait_timeout_returns_timedout(struct kunit *test) {
    if (futex_contract_wait_timeout_returns_timedout() != 0) {
        KUNIT_FAIL(test, "wait_timeout_returns_timedout failed with errno %d", errno);
    }
}

static void test_wake_releases_waiter(struct kunit *test) {
    if (futex_contract_wake_releases_waiter() != 0) {
        KUNIT_FAIL(test, "wake_releases_waiter failed with errno %d", errno);
    }
}

static void test_wake_bitset_only_wakes_matching_waiter(struct kunit *test) {
    if (futex_contract_wake_bitset_only_wakes_matching_waiter() != 0) {
        KUNIT_FAIL(test, "wake_bitset_only_wakes_matching_waiter failed with errno %d", errno);
    }
}

static void test_requeue_moves_waiter_to_new_uaddr(struct kunit *test) {
    if (futex_contract_requeue_moves_waiter_to_new_uaddr() != 0) {
        KUNIT_FAIL(test, "requeue_moves_waiter_to_new_uaddr failed with errno %d", errno);
    }
}

static void test_cmp_requeue_rejects_mismatch(struct kunit *test) {
    if (futex_contract_cmp_requeue_rejects_mismatch() != 0) {
        KUNIT_FAIL(test, "cmp_requeue_rejects_mismatch failed with errno %d", errno);
    }
}

static void test_interrupted_wait_records_restart(struct kunit *test) {
    if (futex_contract_interrupted_wait_records_restart() != 0) {
        KUNIT_FAIL(test, "interrupted_wait_records_restart failed with errno %d", errno);
    }
}

static void test_sets_and_gets_robust_list(struct kunit *test) {
    if (futex_contract_sets_and_gets_robust_list() != 0) {
        KUNIT_FAIL(test, "sets_and_gets_robust_list failed with errno %d", errno);
    }
}

static void test_rejects_missing_robust_list_outputs(struct kunit *test) {
    if (futex_contract_rejects_missing_robust_list_outputs() != 0) {
        KUNIT_FAIL(test, "rejects_missing_robust_list_outputs failed with errno %d", errno);
    }
}

static void test_exit_clears_child_tid_and_marks_robust_futex(struct kunit *test) {
    if (futex_contract_exit_clears_child_tid_and_marks_robust_futex() != 0) {
        KUNIT_FAIL(test, "exit_clears_child_tid_and_marks_robust_futex failed with errno %d", errno);
    }
}

static void test_clone_thread_shares_vm_and_thread_group(struct kunit *test) {
    if (futex_contract_clone_thread_shares_vm_and_thread_group() != 0) {
        KUNIT_FAIL(test, "clone_thread_shares_vm_and_thread_group failed with errno %d", errno);
    }
}

static void test_clone3_sets_parent_child_and_clear_tid(struct kunit *test) {
    if (futex_contract_clone3_sets_parent_child_and_clear_tid() != 0) {
        KUNIT_FAIL(test, "clone3_sets_parent_child_and_clear_tid failed with errno %d", errno);
    }
}

static void test_clone3_set_tid_supports_repo_pid_model(struct kunit *test) {
    if (futex_contract_clone3_set_tid_supports_repo_pid_model() != 0) {
        KUNIT_FAIL(test, "clone3_set_tid_supports_repo_pid_model failed with errno %d", errno);
    }
}

static void test_clear_child_tid_is_per_thread_not_mm_shared(struct kunit *test) {
    if (futex_contract_clear_child_tid_is_per_thread_not_mm_shared() != 0) {
        KUNIT_FAIL(test, "clear_child_tid_is_per_thread_not_mm_shared failed with errno %d", errno);
    }
}

static void futex_suite_exit(struct kunit *test) {
    (void)test;
    futex_contract_reset_test_state();
}

static const struct kunit_case futex_cases[] = {
    KUNIT_CASE(test_wait_mismatch_returns_again),
    KUNIT_CASE(test_wake_without_waiters_returns_zero),
    KUNIT_CASE(test_wait_timeout_returns_timedout),
    KUNIT_CASE(test_wake_releases_waiter),
    KUNIT_CASE(test_wake_bitset_only_wakes_matching_waiter),
    KUNIT_CASE(test_requeue_moves_waiter_to_new_uaddr),
    KUNIT_CASE(test_cmp_requeue_rejects_mismatch),
    KUNIT_CASE(test_interrupted_wait_records_restart),
    KUNIT_CASE(test_sets_and_gets_robust_list),
    KUNIT_CASE(test_rejects_missing_robust_list_outputs),
    KUNIT_CASE(test_exit_clears_child_tid_and_marks_robust_futex),
    KUNIT_CASE(test_clone_thread_shares_vm_and_thread_group),
    KUNIT_CASE(test_clone3_sets_parent_child_and_clear_tid),
    KUNIT_CASE(test_clone3_set_tid_supports_repo_pid_model),
    KUNIT_CASE(test_clear_child_tid_is_per_thread_not_mm_shared),
};

static const struct kunit_suite futex_suite = {
    .name = "futex",
    .cases = futex_cases,
    .case_count = sizeof(futex_cases) / sizeof(futex_cases[0]),
    .init = futex_suite_init,
    .exit = futex_suite_exit,
};

const struct kunit_suite *kernel_futex_suite(void) {
    return &futex_suite;
}
