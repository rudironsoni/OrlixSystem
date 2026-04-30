#include "wait_queue.h"

#include <errno.h>

#include "signal.h"
#include "task.h"

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

int wait_queue_wait_locked_interruptible(struct wait_queue_head *queue) {
    struct task_struct *task;
    int ret;

    if (!queue) {
        return -EINVAL;
    }

    task = get_current();
    if (signal_has_unblocked_pending(task)) {
        return -EINTR;
    }

    if (task) {
        kernel_mutex_lock(&task->wait_lock);
        task->current_wait_queue = queue;
        task->waiters++;
        kernel_mutex_unlock(&task->wait_lock);
    }

    ret = kernel_cond_wait(&queue->cond, &queue->lock);

    if (task) {
        kernel_mutex_lock(&task->wait_lock);
        if (task->waiters > 0) {
            task->waiters--;
        }
        if (task->current_wait_queue == queue) {
            task->current_wait_queue = NULL;
        }
        kernel_mutex_unlock(&task->wait_lock);
    }

    if (ret != 0) {
        return -ret;
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

void wait_queue_wake_all(struct wait_queue_head *queue) {
    if (!queue) {
        return;
    }
    kernel_mutex_lock(&queue->lock);
    wait_queue_wake_all_locked(queue);
    kernel_mutex_unlock(&queue->lock);
}
