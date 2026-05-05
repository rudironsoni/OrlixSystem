#ifndef KERNEL_WAIT_QUEUE_H
#define KERNEL_WAIT_QUEUE_H

#define WAIT_QUEUE_LOCK_STORAGE_WORDS 10
#define WAIT_QUEUE_COND_STORAGE_WORDS 10

#ifdef __cplusplus
extern "C" {
#endif

struct wait_queue_head {
    long long lock_storage[WAIT_QUEUE_LOCK_STORAGE_WORDS];
    long long cond_storage[WAIT_QUEUE_COND_STORAGE_WORDS];
};

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
