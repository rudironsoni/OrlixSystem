#ifndef PRIVATE_KERNEL_MUTEX_STATE_H
#define PRIVATE_KERNEL_MUTEX_STATE_H

#ifdef __cplusplus
extern "C" {
#endif

#define KERNEL_COND_WAIT_TIMED_OUT 1

typedef struct kernel_mutex {
    void *impl;
} kernel_mutex_t;

typedef struct kernel_cond {
    void *impl;
} kernel_cond_t;

typedef struct kernel_once {
    int done;
} kernel_once_t;

#define KERNEL_MUTEX_INITIALIZER {NULL}
#define KERNEL_COND_INITIALIZER {NULL}
#define KERNEL_ONCE_INIT {0}

int kernel_mutex_init(kernel_mutex_t *mutex);
int kernel_mutex_destroy(kernel_mutex_t *mutex);
int kernel_mutex_lock(kernel_mutex_t *mutex);
int kernel_mutex_unlock(kernel_mutex_t *mutex);

int kernel_cond_init(kernel_cond_t *cond);
int kernel_cond_destroy(kernel_cond_t *cond);
int kernel_cond_wait(kernel_cond_t *cond, kernel_mutex_t *mutex);
int kernel_cond_timedwait_ms(kernel_cond_t *cond, kernel_mutex_t *mutex, int timeout_ms);
int kernel_cond_signal(kernel_cond_t *cond);
int kernel_cond_broadcast(kernel_cond_t *cond);

int kernel_once(kernel_once_t *once_control, void (*init_routine)(void));

#ifdef __cplusplus
}
#endif

#endif
