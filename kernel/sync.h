#ifndef KERNEL_SYNC_H
#define KERNEL_SYNC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KERNEL_MUTEX_STORAGE_SIZE 64
#define KERNEL_COND_STORAGE_SIZE 64
#define KERNEL_THREAD_STORAGE_SIZE 64
#define KERNEL_THREAD_ATTR_STORAGE_SIZE 64
#define KERNEL_ONCE_STORAGE_SIZE 64
#define KERNEL_SIGSET_STORAGE_SIZE 128

#define KERNEL_COND_WAIT_TIMED_OUT 1

typedef struct kernel_mutex {
    char storage[KERNEL_MUTEX_STORAGE_SIZE];
    int initialized;
} kernel_mutex_t;

typedef struct kernel_cond {
    char storage[KERNEL_COND_STORAGE_SIZE];
    int initialized;
} kernel_cond_t;

typedef struct kernel_thread {
    char storage[KERNEL_THREAD_STORAGE_SIZE];
    int initialized;
} kernel_thread_t;

typedef struct kernel_thread_attr {
    char storage[KERNEL_THREAD_ATTR_STORAGE_SIZE];
    int initialized;
} kernel_thread_attr_t;

typedef struct kernel_once {
    char storage[KERNEL_ONCE_STORAGE_SIZE];
    int initialized;
} kernel_once_t;

typedef struct kernel_sigset {
    char storage[KERNEL_SIGSET_STORAGE_SIZE];
    int initialized;
} kernel_sigset_t;

#define KERNEL_MUTEX_INITIALIZER {{0}, 0}
#define KERNEL_COND_INITIALIZER {{0}, 0}
#define KERNEL_ONCE_INIT {{0}, 0}
#define KERNEL_SIGSET_INITIALIZER {{0}, 0}

int kernel_mutex_init(kernel_mutex_t *mutex);
int kernel_mutex_destroy(kernel_mutex_t *mutex);
int kernel_mutex_lock(kernel_mutex_t *mutex);
int kernel_mutex_unlock(kernel_mutex_t *mutex);

int kernel_cond_init(kernel_cond_t *cond);
int kernel_cond_destroy(kernel_cond_t *cond);
int kernel_cond_wait(kernel_cond_t *cond, kernel_mutex_t *mutex);
int kernel_cond_timedwait_ms(kernel_cond_t *cond, kernel_mutex_t *mutex, int timeout_ms);
int kernel_cond_broadcast(kernel_cond_t *cond);

int kernel_thread_attr_init(kernel_thread_attr_t *attr);
int kernel_thread_attr_destroy(kernel_thread_attr_t *attr);
int kernel_thread_attr_setstacksize(kernel_thread_attr_t *attr, size_t stacksize);
int kernel_thread_create(kernel_thread_t *thread, const kernel_thread_attr_t *attr,
                         void *(*start_routine)(void *), void *arg);
int kernel_thread_detach(kernel_thread_t thread);
kernel_thread_t kernel_thread_self(void);
void kernel_thread_exit(void *value_ptr);

int kernel_once(kernel_once_t *once_control, void (*init_routine)(void));

int kernel_thread_sigmask(int how, const kernel_sigset_t *set, kernel_sigset_t *oldset);
int kernel_sigemptyset(kernel_sigset_t *set);
int kernel_sigaddset(kernel_sigset_t *set, int signo);
int kernel_sigismember(const kernel_sigset_t *set, int signo);
int kernel_sleep_ms(int timeout_ms);

struct timespec;
int kernel_clock_gettime(int clock_id, struct timespec *tp);

#ifdef __cplusplus
}
#endif

#endif
