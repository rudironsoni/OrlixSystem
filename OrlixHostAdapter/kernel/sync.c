/*
 * OrlixKernel Kernel Sync Subsystem - Darwin Bridge
 *
 * This file includes Darwin headers and provides the implementation
 * using Darwin's pthread functions. It is the ONLY file in the kernel
 * sync subsystem that includes Darwin headers like <pthread.h>.
 * This is the private bridge behind the private kernel sync contract.
 *
 * NOTE: This file does NOT include Linux UAPI headers. It implements
 * the _impl() functions using Darwin's native types directly.
 */

#include <errno.h>
#include <stddef.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "exit.h"
#include "kthread.h"
#include "mutex.h"
#include "current.h"

static const clockid_t host_nsecs_clock_realtime = _CLOCK_REALTIME;

static pthread_mutex_t *kernel_mutex_impl(const kernel_mutex_t *mutex) {
    return mutex ? (pthread_mutex_t *)mutex->impl : NULL;
}

static pthread_cond_t *kernel_cond_impl(const kernel_cond_t *cond) {
    return cond ? (pthread_cond_t *)cond->impl : NULL;
}

static pthread_attr_t *kernel_thread_attr_impl(const kernel_thread_attr_t *attr) {
    return attr ? (pthread_attr_t *)attr->impl : NULL;
}

static pthread_t *kernel_thread_impl(const kernel_thread_t *thread) {
    return thread ? (pthread_t *)thread->impl : NULL;
}

/* ============================================================================
 * MUTEX - Darwin pthread implementation
 * ============================================================================ */

int kernel_mutex_init(kernel_mutex_t *mutex) {
    pthread_mutex_t *pmutex;

    if (!mutex) {
        return -EINVAL;
    }
    if (mutex->impl) {
        return 0;
    }
    pmutex = malloc(sizeof(*pmutex));
    if (!pmutex) {
        return -ENOMEM;
    }
    int ret = pthread_mutex_init(pmutex, NULL);
    if (ret != 0) {
        free(pmutex);
        return ret;
    }
    mutex->impl = pmutex;
    return 0;
}

int kernel_mutex_destroy(kernel_mutex_t *mutex) {
    pthread_mutex_t *pmutex;

    if (!mutex) {
        return -EINVAL;
    }
    pmutex = kernel_mutex_impl(mutex);
    if (!pmutex) {
        return 0;
    }
    int ret = pthread_mutex_destroy(pmutex);
    if (ret == 0) {
        free(pmutex);
        mutex->impl = NULL;
    }
    return ret;
}

int kernel_mutex_lock(kernel_mutex_t *mutex) {
    pthread_mutex_t *pmutex;

    if (!mutex) {
        return -EINVAL;
    }
    if (!mutex->impl) {
        int ret = kernel_mutex_init(mutex);
        if (ret != 0) {
            return ret;
        }
    }
    pmutex = kernel_mutex_impl(mutex);
    return pthread_mutex_lock(pmutex);
}

int kernel_mutex_unlock(kernel_mutex_t *mutex) {
    pthread_mutex_t *pmutex;

    if (!mutex) {
        return -EINVAL;
    }
    pmutex = kernel_mutex_impl(mutex);
    if (!pmutex) {
        return -EINVAL;
    }
    return pthread_mutex_unlock(pmutex);
}

/* ============================================================================
 * CONDITION VARIABLE - Darwin pthread implementation
 * ============================================================================ */

int kernel_cond_init(kernel_cond_t *cond) {
    pthread_cond_t *pcond;

    if (!cond) {
        return -EINVAL;
    }
    if (cond->impl) {
        return 0;
    }
    pcond = malloc(sizeof(*pcond));
    if (!pcond) {
        return -ENOMEM;
    }
    int ret = pthread_cond_init(pcond, NULL);
    if (ret != 0) {
        free(pcond);
        return ret;
    }
    cond->impl = pcond;
    return 0;
}

int kernel_cond_destroy(kernel_cond_t *cond) {
    pthread_cond_t *pcond;

    if (!cond) {
        return -EINVAL;
    }
    pcond = kernel_cond_impl(cond);
    if (!pcond) {
        return 0;
    }
    int ret = pthread_cond_destroy(pcond);
    if (ret == 0) {
        free(pcond);
        cond->impl = NULL;
    }
    return ret;
}

int kernel_cond_wait(kernel_cond_t *cond, kernel_mutex_t *mutex) {
    pthread_cond_t *pcond;
    pthread_mutex_t *pmutex;

    if (!cond || !mutex) {
        return -EINVAL;
    }
    if (!cond->impl) {
        int ret = kernel_cond_init(cond);
        if (ret != 0) {
            return ret;
        }
    }
    pcond = kernel_cond_impl(cond);
    pmutex = kernel_mutex_impl(mutex);
    if (!pmutex) {
        return -EINVAL;
    }
    return pthread_cond_wait(pcond, pmutex);
}

int kernel_cond_timedwait_ms(kernel_cond_t *cond, kernel_mutex_t *mutex, int timeout_ms) {
    const int host_pthread_timedout = 60;
    int ret;
    pthread_cond_t *pcond;
    pthread_mutex_t *pmutex;

    if (!cond || !mutex || timeout_ms < 0) {
        return -EINVAL;
    }
    if (!cond->impl) {
        int ret = kernel_cond_init(cond);
        if (ret != 0) {
            return ret;
        }
    }
    pcond = kernel_cond_impl(cond);
    pmutex = kernel_mutex_impl(mutex);
    if (!pmutex) {
        return -EINVAL;
    }

    uint64_t now_ns = clock_gettime_nsec_np(host_nsecs_clock_realtime);
    struct timespec deadline;
    deadline.tv_sec = (time_t)(now_ns / 1000000000ULL) + (timeout_ms / 1000);
    deadline.tv_nsec = (long)(now_ns % 1000000000ULL) + ((long)(timeout_ms % 1000) * 1000000L);
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }

    ret = pthread_cond_timedwait(pcond, pmutex, &deadline);
    if (ret == host_pthread_timedout) {
        return KERNEL_COND_WAIT_TIMED_OUT;
    }
    return ret;
}

int kernel_cond_broadcast(kernel_cond_t *cond) {
    pthread_cond_t *pcond;

    if (!cond) {
        return -EINVAL;
    }
    pcond = kernel_cond_impl(cond);
    if (!pcond) {
        return -EINVAL;
    }
    return pthread_cond_broadcast(pcond);
}

int kernel_cond_signal(kernel_cond_t *cond) {
    pthread_cond_t *pcond;

    if (!cond) {
        return -EINVAL;
    }
    pcond = kernel_cond_impl(cond);
    if (!pcond) {
        return -EINVAL;
    }
    return pthread_cond_signal(pcond);
}

/* ============================================================================
 * THREAD - Darwin pthread implementation
 * ============================================================================ */

int kernel_thread_attr_init(kernel_thread_attr_t *attr) {
    pthread_attr_t *pattr;

    if (!attr) {
        return -EINVAL;
    }
    if (attr->impl) {
        return 0;
    }
    pattr = malloc(sizeof(*pattr));
    if (!pattr) {
        return -ENOMEM;
    }
    int ret = pthread_attr_init(pattr);
    if (ret != 0) {
        free(pattr);
        return ret;
    }
    attr->impl = pattr;
    return 0;
}

int kernel_thread_attr_destroy(kernel_thread_attr_t *attr) {
    pthread_attr_t *pattr;

    if (!attr) {
        return -EINVAL;
    }
    pattr = kernel_thread_attr_impl(attr);
    if (!pattr) {
        return 0;
    }
    int ret = pthread_attr_destroy(pattr);
    if (ret == 0) {
        free(pattr);
        attr->impl = NULL;
    }
    return ret;
}

int kernel_thread_attr_setstacksize(kernel_thread_attr_t *attr, unsigned long stacksize) {
    pthread_attr_t *pattr;

    if (!attr) {
        return -EINVAL;
    }
    pattr = kernel_thread_attr_impl(attr);
    if (!pattr) {
        return -EINVAL;
    }
    return pthread_attr_setstacksize(pattr, stacksize);
}

static void *kernel_thread_trampoline(void *arg) {
    void **args = (void **)arg;
    void *(*start_routine)(void *) = args[0];
    void *real_arg = args[1];
    free(arg);
    void *ret = start_routine(real_arg);
    /* Drop the per-thread "current task" reference if one was installed. */
    task_set_current(NULL);
    return ret;
}

int kernel_thread_create(kernel_thread_t *thread, const kernel_thread_attr_t *attr,
                         void *(*start_routine)(void *), void *arg) {
    pthread_t *pthread;

    if (!thread || !start_routine) {
        return -EINVAL;
    }
    
    void **args = malloc(2 * sizeof(void *));
    if (!args) {
        return -ENOMEM;
    }
    args[0] = start_routine;
    args[1] = arg;
    
    pthread = malloc(sizeof(*pthread));
    if (!pthread) {
        free(args);
        return -ENOMEM;
    }
    pthread_attr_t *pattr = kernel_thread_attr_impl(attr);
    
    int ret = pthread_create(pthread, pattr, kernel_thread_trampoline, args);
    if (ret == 0) {
        thread->impl = pthread;
    } else {
        free(pthread);
        free(args);
    }
    return ret;
}

int kernel_thread_detach(kernel_thread_t thread) {
    pthread_t *pthread = kernel_thread_impl(&thread);

    if (!pthread) {
        return -EINVAL;
    }
    int ret = pthread_detach(*pthread);
    if (ret == 0) {
        free(pthread);
    }
    return ret;
}

kernel_thread_t kernel_thread_self(void) {
    kernel_thread_t thread = {0};
    pthread_t *pthread = malloc(sizeof(*pthread));

    if (!pthread) {
        return thread;
    }
    *pthread = pthread_self();
    thread.impl = pthread;
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
    if (!once_control->done) {
        init_routine();
        once_control->done = 1;
    }
    pthread_mutex_unlock(&once_mutex);
    return 0;
}

int kernel_sleep_ms(int timeout_ms) {
    if (timeout_ms < 0) {
        return -EINVAL;
    }
    if (timeout_ms == 0) {
        return 0;
    }
    if (usleep((useconds_t)timeout_ms * 1000U) != 0) {
        return -errno;
    }
    return 0;
}

void process_terminate(int status) {
    _Exit(status);
}
