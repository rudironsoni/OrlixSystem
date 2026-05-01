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
    int waiters;
    struct wait_queue_head wait;
};

static struct wait_queue_head futex_table_lock;
static struct futex_bucket futex_table[KERNEL_WAIT_WORD_BUCKETS];
static atomic_int futex_table_state = 0;

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
    free_bucket->waiters = 0;
    return free_bucket;
}

int futex_wait_impl(int *uaddr, int expected, int timeout_ms) {
    struct futex_bucket *bucket;
    int ret;

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

    wait_queue_lock(&bucket->wait);
    if (atomic_load((_Atomic int *)uaddr) != expected) {
        wait_queue_unlock(&bucket->wait);
        errno = EAGAIN;
        return -1;
    }
    bucket->waiters++;
    if (timeout_ms < 0) {
        ret = wait_queue_wait_locked_interruptible(&bucket->wait);
    } else {
        ret = wait_queue_wait_locked_interruptible_timeout(&bucket->wait, timeout_ms);
    }
    if (bucket->waiters > 0) {
        bucket->waiters--;
    }
    wait_queue_unlock(&bucket->wait);

    if (ret == -EINTR) {
        errno = EINTR;
        return -1;
    }
    if (ret == -ETIMEDOUT) {
        errno = ETIMEDOUT;
        return -1;
    }
    if (ret != 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int futex_wake_impl(int *uaddr, int max_wake) {
    struct futex_bucket *bucket;
    int woken;

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
    woken = bucket->waiters < max_wake ? bucket->waiters : max_wake;
    if (woken > 0) {
        wait_queue_wake_all_locked(&bucket->wait);
    }
    wait_queue_unlock(&bucket->wait);
    return woken;
}

int futex_op_impl(int *uaddr, int futex_op, int val, int timeout_ms) {
    int op = futex_op & FUTEX_CMD_MASK;

    if (op == FUTEX_WAIT) {
        return futex_wait_impl(uaddr, val, timeout_ms);
    }
    if (op == FUTEX_WAKE) {
        return futex_wake_impl(uaddr, val);
    }
    errno = ENOSYS;
    return -1;
}

int set_robust_list_impl(void *head, unsigned long len) {
    struct task_struct *task = get_current();

    if (!task && task_init() == 0) {
        task = get_current();
    }
    if (!task || !task->mm) {
        if (!task) {
            errno = ESRCH;
            return -1;
        }
        task->mm = calloc(1, sizeof(*task->mm));
        if (!task->mm) {
            errno = ENOMEM;
            return -1;
        }
    }
    if (!head || len == 0) {
        errno = EINVAL;
        return -1;
    }
    task->mm->robust_list_head = (uint64_t)(uintptr_t)head;
    task->mm->robust_list_len = (uint64_t)len;
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
        if (!task && task_init() == 0) {
            task = get_current();
        }
    } else {
        task = task_lookup(pid);
    }
    if (!task || !task->mm) {
        errno = ESRCH;
        return -1;
    }
    *head = (void *)(uintptr_t)task->mm->robust_list_head;
    *len = (unsigned long)task->mm->robust_list_len;
    return 0;
}
