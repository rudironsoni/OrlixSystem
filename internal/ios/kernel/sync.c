/*
 * IXLandSystem Kernel Sync Subsystem - Darwin Bridge
 *
 * This file includes Darwin headers and provides the implementation
 * using Darwin's pthread functions. It is the ONLY file in the kernel
 * sync subsystem that includes Darwin headers like <pthread.h>.
 * This is the private bridge - the kernel-owner interface is kernel/sync.h
 *
 * NOTE: This file does NOT include Linux UAPI headers. It implements
 * the _impl() functions using Darwin's native types directly.
 */

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../../../kernel/sync.h"
#include "../../../kernel/task.h"

/* ============================================================================
 * MUTEX - Darwin pthread implementation
 * ============================================================================ */

int kernel_mutex_init(kernel_mutex_t *mutex) {
    if (!mutex) {
        return -EINVAL;
    }
    pthread_mutex_t *pmutex = (pthread_mutex_t *)mutex->storage;
    int ret = pthread_mutex_init(pmutex, NULL);
    if (ret == 0) {
        mutex->initialized = 1;
    }
    return ret;
}

int kernel_mutex_destroy(kernel_mutex_t *mutex) {
    if (!mutex) {
        return -EINVAL;
    }
    if (!mutex->initialized) {
        return 0;
    }
    pthread_mutex_t *pmutex = (pthread_mutex_t *)mutex->storage;
    int ret = pthread_mutex_destroy(pmutex);
    mutex->initialized = 0;
    return ret;
}

int kernel_mutex_lock(kernel_mutex_t *mutex) {
    if (!mutex) {
        return -EINVAL;
    }
    if (!mutex->initialized) {
        /* Auto-initialize if needed */
        int ret = kernel_mutex_init(mutex);
        if (ret != 0) {
            return ret;
        }
    }
    pthread_mutex_t *pmutex = (pthread_mutex_t *)mutex->storage;
    return pthread_mutex_lock(pmutex);
}

int kernel_mutex_unlock(kernel_mutex_t *mutex) {
    if (!mutex) {
        return -EINVAL;
    }
    if (!mutex->initialized) {
        return -EINVAL;
    }
    pthread_mutex_t *pmutex = (pthread_mutex_t *)mutex->storage;
    return pthread_mutex_unlock(pmutex);
}

/* ============================================================================
 * CONDITION VARIABLE - Darwin pthread implementation
 * ============================================================================ */

int kernel_cond_init(kernel_cond_t *cond) {
    if (!cond) {
        return -EINVAL;
    }
    pthread_cond_t *pcond = (pthread_cond_t *)cond->storage;
    int ret = pthread_cond_init(pcond, NULL);
    if (ret == 0) {
        cond->initialized = 1;
    }
    return ret;
}

int kernel_cond_destroy(kernel_cond_t *cond) {
    if (!cond) {
        return -EINVAL;
    }
    if (!cond->initialized) {
        return 0;
    }
    pthread_cond_t *pcond = (pthread_cond_t *)cond->storage;
    int ret = pthread_cond_destroy(pcond);
    cond->initialized = 0;
    return ret;
}

int kernel_cond_wait(kernel_cond_t *cond, kernel_mutex_t *mutex) {
    if (!cond || !mutex) {
        return -EINVAL;
    }
    if (!cond->initialized) {
        int ret = kernel_cond_init(cond);
        if (ret != 0) {
            return ret;
        }
    }
    if (!mutex->initialized) {
        return -EINVAL;
    }
    pthread_cond_t *pcond = (pthread_cond_t *)cond->storage;
    pthread_mutex_t *pmutex = (pthread_mutex_t *)mutex->storage;
    return pthread_cond_wait(pcond, pmutex);
}

int kernel_cond_timedwait_ms(kernel_cond_t *cond, kernel_mutex_t *mutex, int timeout_ms) {
    const int host_pthread_timedout = 60;
    int ret;

    if (!cond || !mutex || timeout_ms < 0) {
        return -EINVAL;
    }
    if (!cond->initialized) {
        int ret = kernel_cond_init(cond);
        if (ret != 0) {
            return ret;
        }
    }
    if (!mutex->initialized) {
        return -EINVAL;
    }

    uint64_t now_ns = clock_gettime_nsec_np(CLOCK_REALTIME);
    struct timespec deadline;
    deadline.tv_sec = (time_t)(now_ns / 1000000000ULL) + (timeout_ms / 1000);
    deadline.tv_nsec = (long)(now_ns % 1000000000ULL) + ((long)(timeout_ms % 1000) * 1000000L);
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }

    pthread_cond_t *pcond = (pthread_cond_t *)cond->storage;
    pthread_mutex_t *pmutex = (pthread_mutex_t *)mutex->storage;
    ret = pthread_cond_timedwait(pcond, pmutex, &deadline);
    if (ret == host_pthread_timedout) {
        return KERNEL_COND_WAIT_TIMED_OUT;
    }
    return ret;
}

int kernel_cond_broadcast(kernel_cond_t *cond) {
    if (!cond) {
        return -EINVAL;
    }
    if (!cond->initialized) {
        return -EINVAL;
    }
    pthread_cond_t *pcond = (pthread_cond_t *)cond->storage;
    return pthread_cond_broadcast(pcond);
}

/* ============================================================================
 * THREAD - Darwin pthread implementation
 * ============================================================================ */

int kernel_thread_attr_init(kernel_thread_attr_t *attr) {
    if (!attr) {
        return -EINVAL;
    }
    pthread_attr_t *pattr = (pthread_attr_t *)attr->storage;
    int ret = pthread_attr_init(pattr);
    if (ret == 0) {
        attr->initialized = 1;
    }
    return ret;
}

int kernel_thread_attr_destroy(kernel_thread_attr_t *attr) {
    if (!attr) {
        return -EINVAL;
    }
    if (!attr->initialized) {
        return 0;
    }
    pthread_attr_t *pattr = (pthread_attr_t *)attr->storage;
    int ret = pthread_attr_destroy(pattr);
    attr->initialized = 0;
    return ret;
}

int kernel_thread_attr_setstacksize(kernel_thread_attr_t *attr, size_t stacksize) {
    if (!attr) {
        return -EINVAL;
    }
    if (!attr->initialized) {
        return -EINVAL;
    }
    pthread_attr_t *pattr = (pthread_attr_t *)attr->storage;
    return pthread_attr_setstacksize(pattr, stacksize);
}

static void *kernel_thread_trampoline(void *arg) {
    void **args = (void **)arg;
    void *(*start_routine)(void *) = args[0];
    void *real_arg = args[1];
    free(arg);
    void *ret = start_routine(real_arg);
    /* Drop the per-thread "current task" reference if one was installed. */
    set_current(NULL);
    return ret;
}

int kernel_thread_create(kernel_thread_t *thread, const kernel_thread_attr_t *attr,
                         void *(*start_routine)(void *), void *arg) {
    if (!thread || !start_routine) {
        return -EINVAL;
    }
    
    void **args = malloc(2 * sizeof(void *));
    if (!args) {
        return -ENOMEM;
    }
    args[0] = start_routine;
    args[1] = arg;
    
    pthread_t *pthread = (pthread_t *)thread->storage;
    pthread_attr_t *pattr = attr && attr->initialized ? (pthread_attr_t *)attr->storage : NULL;
    
    int ret = pthread_create(pthread, pattr, kernel_thread_trampoline, args);
    if (ret == 0) {
        thread->initialized = 1;
    } else {
        free(args);
    }
    return ret;
}

int kernel_thread_detach(kernel_thread_t thread) {
    if (!thread.initialized) {
        return -EINVAL;
    }
    pthread_t *pthread = (pthread_t *)thread.storage;
    return pthread_detach(*pthread);
}

kernel_thread_t kernel_thread_self(void) {
    kernel_thread_t thread;
    pthread_t *pthread = (pthread_t *)thread.storage;
    *pthread = pthread_self();
    thread.initialized = 1;
    return thread;
}

void kernel_thread_exit(void *value_ptr) {
    pthread_exit(value_ptr);
}

/* ============================================================================
 * ONCE - Darwin pthread implementation
 * ============================================================================ */

int kernel_once(kernel_once_t *once_control, void (*init_routine)(void)) {
    if (!once_control || !init_routine) {
        return -EINVAL;
    }
    /* Simple implementation using a mutex for once semantics */
    static pthread_mutex_t once_mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&once_mutex);
    if (!once_control->initialized) {
        init_routine();
        once_control->initialized = 1;
    }
    pthread_mutex_unlock(&once_mutex);
    return 0;
}

/* ============================================================================
 * SIGNAL MASK - Darwin implementation (stub)
 * ============================================================================ */

int kernel_thread_sigmask(int how, const kernel_sigset_t *set, kernel_sigset_t *oldset) {
    (void)how;
    (void)set;
    (void)oldset;
    /* Signal masking not implemented on iOS substrate */
    return 0;
}

int kernel_sigemptyset(kernel_sigset_t *set) {
    if (!set) {
        return -EINVAL;
    }
    memset(set->storage, 0, KERNEL_SIGSET_STORAGE_SIZE);
    set->initialized = 1;
    return 0;
}

int kernel_sigaddset(kernel_sigset_t *set, int signo) {
    (void)set;
    (void)signo;
    return 0;
}

int kernel_sigismember(const kernel_sigset_t *set, int signo) {
    (void)set;
    (void)signo;
    return 0;
}

int kernel_sleep_ms(int timeout_ms) {
    if (timeout_ms < 0) {
        return -EINVAL;
    }
    if (timeout_ms == 0) {
        return 0;
    }
    return usleep((useconds_t)timeout_ms * 1000U);
}

/* ============================================================================
 * CLOCK - Darwin implementation
 * ============================================================================ */

int kernel_clock_gettime(int clock_id, struct timespec *tp) {
    if (!tp) {
        return -EINVAL;
    }
    
    /* Convert Linux clock IDs to Darwin clock IDs */
    clockid_t darwin_clock;
    switch (clock_id) {
    case 0: /* CLOCK_REALTIME */
        darwin_clock = CLOCK_REALTIME;
        break;
    case 1: /* CLOCK_MONOTONIC */
        darwin_clock = CLOCK_MONOTONIC;
        break;
    default:
        errno = EINVAL;
        return -1;
    }
    
    return clock_gettime(darwin_clock, tp);
}
