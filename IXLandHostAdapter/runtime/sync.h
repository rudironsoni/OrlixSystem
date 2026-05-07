#ifndef IXLAND_INTERNAL_IOS_RUNTIME_SYNC_H
#define IXLAND_INTERNAL_IOS_RUNTIME_SYNC_H

#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef pthread_mutex_t runtime_mutex_t;

#define RUNTIME_MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER

static inline int runtime_mutex_lock(runtime_mutex_t *mutex) {
    return pthread_mutex_lock(mutex);
}

static inline int runtime_mutex_unlock(runtime_mutex_t *mutex) {
    return pthread_mutex_unlock(mutex);
}

#ifdef __cplusplus
}
#endif

#endif
