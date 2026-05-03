/*
 * IXLandSystem FS Sync Subsystem - Darwin Bridge
 *
 * This file includes Darwin headers and provides the implementation
 * using Darwin's pthread functions. It is the ONLY file in the fs
 * sync subsystem that includes Darwin headers like <pthread.h>.
 * This is the private bridge - the fs-owner interface is fs/sync.h
 *
 * NOTE: This file does NOT include Linux UAPI headers. It implements
 * the functions using Darwin's native types directly.
 */

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "../../../fs/sync.h"

/* ============================================================================
 * MUTEX - Darwin pthread implementation
 * ============================================================================ */

int fs_mutex_init(fs_mutex_t *mutex) {
    if (!mutex) {
        return -EINVAL;
    }
    pthread_mutex_t *pmutex = (pthread_mutex_t *)mutex->storage;
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
    int ret = pthread_mutex_init(pmutex, &attr);
    pthread_mutexattr_destroy(&attr);
    if (ret == 0) {
        mutex->initialized = 1;
    }
    return ret;
}

int fs_mutex_destroy(fs_mutex_t *mutex) {
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

int fs_mutex_lock(fs_mutex_t *mutex) {
    if (!mutex) {
        return -EINVAL;
    }
    if (!mutex->initialized) {
        /* Auto-initialize if needed */
        int ret = fs_mutex_init(mutex);
        if (ret != 0) {
            return ret;
        }
    }
    pthread_mutex_t *pmutex = (pthread_mutex_t *)mutex->storage;
    return pthread_mutex_lock(pmutex);
}

int fs_mutex_unlock(fs_mutex_t *mutex) {
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

int fs_cond_init(fs_cond_t *cond) {
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

int fs_cond_destroy(fs_cond_t *cond) {
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

int fs_cond_wait(fs_cond_t *cond, fs_mutex_t *mutex) {
    if (!cond || !mutex) {
        return -EINVAL;
    }
    if (!cond->initialized || !mutex->initialized) {
        return -EINVAL;
    }
    pthread_cond_t *pcond = (pthread_cond_t *)cond->storage;
    pthread_mutex_t *pmutex = (pthread_mutex_t *)mutex->storage;
    return pthread_cond_wait(pcond, pmutex);
}

int fs_cond_broadcast(fs_cond_t *cond) {
    if (!cond) {
        return -EINVAL;
    }
    if (!cond->initialized) {
        return -EINVAL;
    }
    pthread_cond_t *pcond = (pthread_cond_t *)cond->storage;
    return pthread_cond_broadcast(pcond);
}
