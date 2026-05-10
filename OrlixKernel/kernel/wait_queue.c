#include "wait_queue.h"

#include <linux/errno.h>

#include "signal.h"
#include "task.h"
#include "internal/timekeeping.h"

int wait_queue_init(struct wait_queue_head *queue) {
    int ret;

    if (!queue) {
        return -EINVAL;
    }

    ret = kernel_mutex_init(&queue->lock);
    if (ret != 0) {
        return ret;
    }

    ret = kernel_cond_init(&queue->cond);
    if (ret != 0) {
        kernel_mutex_destroy(&queue->lock);
        return ret;
    }

    return 0;
}

int wait_queue_destroy(struct wait_queue_head *queue) {
    if (!queue) {
        return -EINVAL;
    }

    kernel_cond_destroy(&queue->cond);
    kernel_mutex_destroy(&queue->lock);
    return 0;
}

int wait_queue_lock(struct wait_queue_head *queue) {
    if (!queue) {
        return -EINVAL;
    }
    return kernel_mutex_lock(&queue->lock);
}

int wait_queue_unlock(struct wait_queue_head *queue) {
    if (!queue) {
        return -EINVAL;
    }
    return kernel_mutex_unlock(&queue->lock);
}

static void wait_queue_attach_task(struct wait_queue_head *queue, struct task *task) {
    if (!task) {
        return;
    }
    kernel_mutex_lock(&task->wait_lock);
    task->current_wait_queue = queue;
    task->waiters++;
    kernel_mutex_unlock(&task->wait_lock);
}

static void wait_queue_detach_task(struct wait_queue_head *queue, struct task *task) {
    if (!task) {
        return;
    }
    kernel_mutex_lock(&task->wait_lock);
    if (task->waiters > 0) {
        task->waiters--;
    }
    if (task->current_wait_queue == queue) {
        task->current_wait_queue = NULL;
    }
    kernel_mutex_unlock(&task->wait_lock);
}

int wait_queue_wait_locked_interruptible(struct wait_queue_head *queue) {
    struct task *task;
    int ret;

    if (!queue) {
        return -EINVAL;
    }

    task = current_task();
    if (signal_has_unblocked_pending(task)) {
        return -EINTR;
    }

    wait_queue_attach_task(queue, task);
    ret = kernel_cond_wait(&queue->cond, &queue->lock);
    wait_queue_detach_task(queue, task);

    if (ret != 0) {
        return ret < 0 ? ret : -ret;
    }

    if (signal_has_unblocked_pending(task)) {
        return -EINTR;
    }

    return 0;
}

int wait_queue_wait_locked_interruptible_timeout(struct wait_queue_head *queue, int timeout_ms) {
    struct task *task;
    int ret;

    if (!queue || timeout_ms < 0) {
        return -EINVAL;
    }

    task = current_task();
    if (signal_has_unblocked_pending(task)) {
        return -EINTR;
    }

    wait_queue_attach_task(queue, task);
    ret = kernel_cond_timedwait_ms(&queue->cond, &queue->lock, timeout_ms);
    wait_queue_detach_task(queue, task);

    if (ret == KERNEL_COND_WAIT_TIMED_OUT) {
        return -ETIMEDOUT;
    }
    if (ret != 0) {
        return ret < 0 ? ret : -ret;
    }
    if (signal_has_unblocked_pending(task)) {
        return -EINTR;
    }
    return 0;
}

void wait_queue_wake_all_locked(struct wait_queue_head *queue) {
    if (!queue) {
        return;
    }
    kernel_cond_broadcast(&queue->cond);
}

void wait_queue_wake_n_locked(struct wait_queue_head *queue, int n) {
    if (!queue || n <= 0) {
        return;
    }
    for (int i = 0; i < n; i++) {
        kernel_cond_signal(&queue->cond);
    }
}

void wait_queue_wake_all(struct wait_queue_head *queue) {
    if (!queue) {
        return;
    }
    wait_queue_lock(queue);
    wait_queue_wake_all_locked(queue);
    wait_queue_unlock(queue);
}

int wait_queue_sleep_ms(int timeout_ms) {
    return kernel_sleep_ms(timeout_ms);
}
