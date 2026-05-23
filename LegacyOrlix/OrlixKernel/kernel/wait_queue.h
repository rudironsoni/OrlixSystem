#ifndef KERNEL_WAIT_QUEUE_H
#define KERNEL_WAIT_QUEUE_H

#ifdef __cplusplus
extern "C" {
#endif

struct wait_queue_head;

int wait_queue_init(struct wait_queue_head *queue);
int wait_queue_destroy(struct wait_queue_head *queue);
int wait_queue_lock(struct wait_queue_head *queue);
int wait_queue_unlock(struct wait_queue_head *queue);
int wait_queue_wait_locked_interruptible(struct wait_queue_head *queue);
int wait_queue_wait_locked_interruptible_timeout(struct wait_queue_head *queue, int timeout_ms);
void wait_queue_wake_all(struct wait_queue_head *queue);
void wait_queue_wake_all_locked(struct wait_queue_head *queue);
void wait_queue_wake_n_locked(struct wait_queue_head *queue, int n);
int wait_queue_sleep_ms(int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
