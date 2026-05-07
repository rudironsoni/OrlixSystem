/*
 * IXLandKernel FS Sync Subsystem - Darwin Bridge
 *
 * This file includes Darwin headers and provides the implementation
 * using Darwin's pthread functions. It is the ONLY file in the fs
 * sync subsystem that includes Darwin headers like <pthread.h>.
 * This is the private bridge behind the private fs sync contract.
 *
 * NOTE: This file does NOT include Linux UAPI headers. It implements
 * the functions using Darwin's native types directly.
 */

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "fs_sync.h"

static pthread_mutex_t *fs_mutex_impl(const fs_mutex_t *mutex) {
    return mutex ? (pthread_mutex_t *)mutex->impl : NULL;
}

static pthread_cond_t *fs_cond_impl(const fs_cond_t *cond) {
    return cond ? (pthread_cond_t *)cond->impl : NULL;
}

/* ============================================================================
 * MUTEX - Darwin pthread implementation
 * ============================================================================ */

int fs_mutex_init(fs_mutex_t *mutex) {
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
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
    int ret = pthread_mutex_init(pmutex, &attr);
    pthread_mutexattr_destroy(&attr);
    if (ret != 0) {
        free(pmutex);
        return ret;
    }
    mutex->impl = pmutex;
    return 0;
}

int fs_mutex_destroy(fs_mutex_t *mutex) {
    pthread_mutex_t *pmutex;

    if (!mutex) {
        return -EINVAL;
    }
    pmutex = fs_mutex_impl(mutex);
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

int fs_mutex_lock(fs_mutex_t *mutex) {
    pthread_mutex_t *pmutex;

    if (!mutex) {
        return -EINVAL;
    }
    if (!mutex->impl) {
        int ret = fs_mutex_init(mutex);
        if (ret != 0) {
            return ret;
        }
    }
    pmutex = fs_mutex_impl(mutex);
    return pthread_mutex_lock(pmutex);
}

int fs_mutex_unlock(fs_mutex_t *mutex) {
    pthread_mutex_t *pmutex;

    if (!mutex) {
        return -EINVAL;
    }
    pmutex = fs_mutex_impl(mutex);
    if (!pmutex) {
        return -EINVAL;
    }
    return pthread_mutex_unlock(pmutex);
}

/* ============================================================================
 * CONDITION VARIABLE - Darwin pthread implementation
 * ============================================================================ */

int fs_cond_init(fs_cond_t *cond) {
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

int fs_cond_destroy(fs_cond_t *cond) {
    pthread_cond_t *pcond;

    if (!cond) {
        return -EINVAL;
    }
    pcond = fs_cond_impl(cond);
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

int fs_cond_wait(fs_cond_t *cond, fs_mutex_t *mutex) {
    pthread_cond_t *pcond;
    pthread_mutex_t *pmutex;

    if (!cond || !mutex) {
        return -EINVAL;
    }
    pcond = fs_cond_impl(cond);
    pmutex = fs_mutex_impl(mutex);
    if (!pcond || !pmutex) {
        return -EINVAL;
    }
    return pthread_cond_wait(pcond, pmutex);
}

int fs_cond_broadcast(fs_cond_t *cond) {
    pthread_cond_t *pcond;

    if (!cond) {
        return -EINVAL;
    }
    pcond = fs_cond_impl(cond);
    if (!pcond) {
        return -EINVAL;
    }
    return pthread_cond_broadcast(pcond);
}
