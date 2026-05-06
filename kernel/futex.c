#include "futex.h"

#include "task.h"
#include "wait_queue.h"

#include <errno.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <linux/futex.h>

#define KERNEL_WAIT_WORD_BUCKETS 64

struct futex_bucket {
    uintptr_t uaddr;
    int used;
    struct wait_queue_head wait;
    struct futex_waiter *waiter_list;
    int waiters;
};

struct futex_waiter {
    struct futex_waiter *next;
    struct futex_bucket *bucket;
    int *wait_uaddr;
    int expected;
    int check_expected;
    uint32_t bitset;
    int woken;
    int requeued;
};

static struct wait_queue_head futex_table_lock;
static struct futex_bucket futex_table[KERNEL_WAIT_WORD_BUCKETS];
static atomic_int futex_table_state = 0;

void futex_reset_impl(void) {
    if (atomic_load(&futex_table_state) != 2) {
        return;
    }

    wait_queue_lock(&futex_table_lock);
    for (int i = 0; i < KERNEL_WAIT_WORD_BUCKETS; i++) {
        futex_table[i].uaddr = 0;
        futex_table[i].used = 0;
        futex_table[i].waiter_list = NULL;
        futex_table[i].waiters = 0;
    }
    wait_queue_unlock(&futex_table_lock);
}

static int futex_table_init_once(void) {
    int expected = 0;

    if (atomic_load(&futex_table_state) == 2) {
        return 0;
    }
    if (atomic_compare_exchange_strong(&futex_table_state, &expected, 1)) {
        if (wait_queue_init(&futex_table_lock) != 0) {
            atomic_store(&futex_table_state, 0);
            return -ENOMEM;
        }
        memset(futex_table, 0, sizeof(futex_table));
        atomic_store(&futex_table_state, 2);
    } else {
        while (atomic_load(&futex_table_state) == 1) {
            wait_queue_sleep_ms(1);
        }
    }
    return atomic_load(&futex_table_state) == 2 ? 0 : -ENOMEM;
}

static struct futex_bucket *futex_find_bucket_locked(uintptr_t uaddr, int create) {
    struct futex_bucket *free_bucket = NULL;

    for (int i = 0; i < KERNEL_WAIT_WORD_BUCKETS; i++) {
        if (futex_table[i].used && futex_table[i].uaddr == uaddr) {
            return &futex_table[i];
        }
        if (!futex_table[i].used && !free_bucket) {
            free_bucket = &futex_table[i];
        }
    }
    if (!create || !free_bucket) {
        return NULL;
    }
    if (wait_queue_init(&free_bucket->wait) != 0) {
        return NULL;
    }
    free_bucket->uaddr = uaddr;
    free_bucket->used = 1;
    free_bucket->waiter_list = NULL;
    free_bucket->waiters = 0;
    return free_bucket;
}

static void futex_waiter_add_locked(struct futex_bucket *bucket, struct futex_waiter *w) {
    if (!bucket || !w) {
        return;
    }
    w->next = bucket->waiter_list;
    bucket->waiter_list = w;
    bucket->waiters++;
}

static void futex_waiter_remove_locked(struct futex_bucket *bucket, struct futex_waiter *w) {
    if (!bucket || !w) {
        return;
    }
    struct futex_waiter **pp = &bucket->waiter_list;
    while (*pp) {
        if (*pp == w) {
            *pp = w->next;
            w->next = NULL;
            if (bucket->waiters > 0) {
                bucket->waiters--;
            }
            return;
        }
        pp = &(*pp)->next;
    }
}

static int futex_wait_common_impl(int *uaddr, int expected, int timeout_ms, uint32_t bitset) {
    struct futex_bucket *bucket;
    int ret;
    struct futex_waiter waiter;

    if (!uaddr) {
        errno = EFAULT;
        return -1;
    }
    if (timeout_ms < -1) {
        errno = EINVAL;
        return -1;
    }
    if (futex_table_init_once() != 0) {
        errno = ENOMEM;
        return -1;
    }
    if (atomic_load((_Atomic int *)uaddr) != expected) {
        errno = EAGAIN;
        return -1;
    }

    wait_queue_lock(&futex_table_lock);
    bucket = futex_find_bucket_locked((uintptr_t)uaddr, 1);
    wait_queue_unlock(&futex_table_lock);
    if (!bucket) {
        errno = ENOMEM;
        return -1;
    }

    memset(&waiter, 0, sizeof(waiter));
    waiter.bucket = bucket;
    waiter.wait_uaddr = uaddr;
    waiter.expected = expected;
    waiter.check_expected = 1;
    waiter.bitset = bitset ? bitset : FUTEX_BITSET_MATCH_ANY;

    wait_queue_lock(&bucket->wait);
    if (atomic_load((_Atomic int *)uaddr) != expected) {
        wait_queue_unlock(&bucket->wait);
        errno = EAGAIN;
        return -1;
    }
    futex_waiter_add_locked(bucket, &waiter);
    while (1) {
        if (timeout_ms < 0) {
            ret = wait_queue_wait_locked_interruptible(&bucket->wait);
        } else {
            ret = wait_queue_wait_locked_interruptible_timeout(&bucket->wait, timeout_ms);
        }

        if (ret == -EINTR || ret == -ETIMEDOUT) {
            futex_waiter_remove_locked(bucket, &waiter);
            wait_queue_unlock(&bucket->wait);
            if (ret == -EINTR) {
                task_restart_record_impl(get_current(), TASK_RESTART_FUTEX_WAIT,
                                         (uint64_t)(uintptr_t)uaddr,
                                         (uint64_t)(int64_t)expected,
                                         (uint64_t)(int64_t)timeout_ms,
                                         0, 0, 0);
                errno = EINTR;
            } else {
                errno = ETIMEDOUT;
            }
            return -1;
        }

        if (waiter.woken) {
            futex_waiter_remove_locked(bucket, &waiter);
            wait_queue_unlock(&bucket->wait);
            return 0;
        }

	        if (waiter.requeued && waiter.bucket != bucket) {
	            struct futex_bucket *old_bucket = bucket;
	            struct futex_bucket *new_bucket = waiter.bucket;
	            wait_queue_unlock(&old_bucket->wait);
	            bucket = new_bucket;
	            wait_queue_lock(&bucket->wait);
	            continue;
	        }

	        /* If we were woken while not yet waiting on the new bucket, do not miss it. */
	        if (waiter.woken) {
	            futex_waiter_remove_locked(bucket, &waiter);
	            wait_queue_unlock(&bucket->wait);
	            return 0;
	        }

	        /* Spurious wakeups: keep waiting as long as the expected value matches. */
	        if (waiter.check_expected && atomic_load((_Atomic int *)waiter.wait_uaddr) != waiter.expected) {
	            futex_waiter_remove_locked(bucket, &waiter);
	            wait_queue_unlock(&bucket->wait);
            errno = EAGAIN;
            return -1;
        }
    }
}

int futex_wait_impl(int *uaddr, int expected, int timeout_ms) {
    return futex_wait_common_impl(uaddr, expected, timeout_ms, FUTEX_BITSET_MATCH_ANY);
}

static int futex_wait_bitset_impl(int *uaddr, int expected, int timeout_ms, uint32_t bitset) {
    if (bitset == 0) {
        errno = EINVAL;
        return -1;
    }
    return futex_wait_common_impl(uaddr, expected, timeout_ms, bitset);
}

int futex_wake_impl(int *uaddr, int max_wake) {
    struct futex_bucket *bucket;
    int woken = 0;

    if (!uaddr) {
        errno = EFAULT;
        return -1;
    }
    if (max_wake < 0) {
        errno = EINVAL;
        return -1;
    }
    if (max_wake == 0) {
        return 0;
    }
    if (futex_table_init_once() != 0) {
        errno = ENOMEM;
        return -1;
    }

    wait_queue_lock(&futex_table_lock);
    bucket = futex_find_bucket_locked((uintptr_t)uaddr, 0);
    wait_queue_unlock(&futex_table_lock);
    if (!bucket) {
        return 0;
    }

    wait_queue_lock(&bucket->wait);
    for (struct futex_waiter *w = bucket->waiter_list; w && woken < max_wake; w = w->next) {
        if (w->woken) {
            continue;
        }
        w->woken = 1;
        woken++;
    }
    if (woken > 0) {
        wait_queue_wake_all_locked(&bucket->wait);
    }
    wait_queue_unlock(&bucket->wait);
    return woken;
}

static int futex_wake_bitset_impl(int *uaddr, int max_wake, uint32_t bitset) {
    struct futex_bucket *bucket;
    int woken = 0;

    if (!uaddr) {
        errno = EFAULT;
        return -1;
    }
    if (bitset == 0 || max_wake < 0) {
        errno = EINVAL;
        return -1;
    }
    if (max_wake == 0) {
        return 0;
    }
    if (futex_table_init_once() != 0) {
        errno = ENOMEM;
        return -1;
    }

    wait_queue_lock(&futex_table_lock);
    bucket = futex_find_bucket_locked((uintptr_t)uaddr, 0);
    wait_queue_unlock(&futex_table_lock);
    if (!bucket) {
        return 0;
    }

    wait_queue_lock(&bucket->wait);
    for (struct futex_waiter *w = bucket->waiter_list; w && woken < max_wake; w = w->next) {
        if (w->woken) {
            continue;
        }
        if ((w->bitset & bitset) == 0) {
            continue;
        }
        w->woken = 1;
        woken++;
    }
    if (woken > 0) {
        wait_queue_wake_all_locked(&bucket->wait);
    }
    wait_queue_unlock(&bucket->wait);
    return woken;
}

static int futex_requeue_impl(int *uaddr, int nr_wake, int nr_requeue, int *uaddr2, int use_cmp,
                              int cmpval) {
    struct futex_bucket *src;
    struct futex_bucket *dst;
    int moved = 0;
    int woken = 0;

    if (!uaddr || !uaddr2) {
        errno = EFAULT;
        return -1;
    }
    if (nr_wake < 0 || nr_requeue < 0) {
        errno = EINVAL;
        return -1;
    }
    if (futex_table_init_once() != 0) {
        errno = ENOMEM;
        return -1;
    }
    if (use_cmp && atomic_load((_Atomic int *)uaddr) != cmpval) {
        errno = EAGAIN;
        return -1;
    }

    wait_queue_lock(&futex_table_lock);
    src = futex_find_bucket_locked((uintptr_t)uaddr, 0);
    dst = futex_find_bucket_locked((uintptr_t)uaddr2, 1);
    wait_queue_unlock(&futex_table_lock);

    if (!dst) {
        errno = ENOMEM;
        return -1;
    }
    if (!src) {
        return 0;
    }

    struct futex_bucket *first = src < dst ? src : dst;
    struct futex_bucket *second = src < dst ? dst : src;
    wait_queue_lock(&first->wait);
    if (second != first) {
        wait_queue_lock(&second->wait);
    }

    for (struct futex_waiter *w = src->waiter_list; w && woken < nr_wake; w = w->next) {
        if (w->woken) {
            continue;
        }
        w->woken = 1;
        woken++;
    }

    struct futex_waiter *w = src->waiter_list;
    while (w && moved < nr_requeue) {
        struct futex_waiter *next = w->next;
        if (!w->woken) {
            futex_waiter_remove_locked(src, w);
            w->bucket = dst;
            w->wait_uaddr = uaddr2;
            w->check_expected = 0;
            w->requeued = 1;
            futex_waiter_add_locked(dst, w);
            moved++;
        }
        w = next;
    }

    if (woken > 0 || moved > 0) {
        wait_queue_wake_all_locked(&src->wait);
    }

    if (second != first) {
        wait_queue_unlock(&second->wait);
    }
    wait_queue_unlock(&first->wait);
    return woken + moved;
}

int futex_op_impl(int *uaddr, int futex_op, int val, int timeout_ms, int *uaddr2, int val3) {
    int op = futex_op & FUTEX_CMD_MASK;

    if (op == FUTEX_WAIT) {
        return futex_wait_impl(uaddr, val, timeout_ms);
    }
    if (op == FUTEX_WAKE) {
        return futex_wake_impl(uaddr, val);
    }
    if (op == FUTEX_WAIT_BITSET) {
        return futex_wait_bitset_impl(uaddr, val, timeout_ms, (uint32_t)val3);
    }
    if (op == FUTEX_WAKE_BITSET) {
        return futex_wake_bitset_impl(uaddr, val, (uint32_t)val3);
    }
    if (op == FUTEX_REQUEUE) {
        return futex_requeue_impl(uaddr, val, timeout_ms, uaddr2, 0, 0);
    }
    if (op == FUTEX_CMP_REQUEUE) {
        return futex_requeue_impl(uaddr, val, timeout_ms, uaddr2, 1, val3);
    }
    errno = ENOSYS;
    return -1;
}

int set_robust_list_impl(void *head, unsigned long len) {
    struct task_struct *task = get_current();

    if (!task && task_init() == 0) {
        task = get_current();
    }
    if (!task) {
        errno = ESRCH;
        return -1;
    }
    if (!head || len == 0) {
        errno = EINVAL;
        return -1;
    }
    task->robust_list_head = (uint64_t)(uintptr_t)head;
    task->robust_list_len = (uint64_t)len;
    return 0;
}

int get_robust_list_impl(int pid, void **head, unsigned long *len) {
    struct task_struct *task;

    if (!head || !len) {
        errno = EFAULT;
        return -1;
    }
    if (pid == 0) {
        task = get_current();
        if (!task) {
            task_init();
            task = get_current();
        }
    } else {
        task = task_lookup(pid);
    }
    if (!task) {
        errno = ESRCH;
        return -1;
    }
    *head = (void *)(uintptr_t)task->robust_list_head;
    *len = (unsigned long)task->robust_list_len;
    if (pid != 0) {
        free_task(task);
    }
    return 0;
}

static void futex_mark_owner_died(int *uaddr, int32_t pid) {
    int value;

    if (!uaddr || pid <= 0) {
        return;
    }
    value = atomic_load((_Atomic int *)uaddr);
    if ((value & FUTEX_TID_MASK) != pid) {
        return;
    }
    atomic_store((_Atomic int *)uaddr, (value & ~FUTEX_TID_MASK) | FUTEX_OWNER_DIED);
    futex_wake_impl(uaddr, 1);
}

static void futex_walk_robust_list(struct task_struct *task) {
    struct robust_list_head *head;
    struct robust_list *entry;

    if (!task || task->robust_list_head == 0) {
        return;
    }
    head = (struct robust_list_head *)(uintptr_t)task->robust_list_head;
    entry = head->list.next;
    for (int i = 0; entry && entry != &head->list && i < 2048; i++) {
        struct robust_list *next = entry->next;
        int *uaddr = (int *)((char *)entry + head->futex_offset);
        futex_mark_owner_died(uaddr, task->pid);
        entry = next;
    }
    if (head->list_op_pending) {
        int *pending = (int *)((char *)head->list_op_pending + head->futex_offset);
        futex_mark_owner_died(pending, task->pid);
    }
}

void futex_task_exit_impl(struct task_struct *task) {
    int *clear_child_tid;

    if (!task) {
        return;
    }
    futex_walk_robust_list(task);
    if (task->clear_child_tid == 0) {
        return;
    }
    clear_child_tid = (int *)(uintptr_t)task->clear_child_tid;
    atomic_store((_Atomic int *)clear_child_tid, 0);
    futex_wake_impl(clear_child_tid, 1);
    task->clear_child_tid = 0;
}
