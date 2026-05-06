#ifndef IXLAND_INTERNAL_PRIVATE_FS_SYNC_H
#define IXLAND_INTERNAL_PRIVATE_FS_SYNC_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fs_mutex {
    void *impl;
} fs_mutex_t;

typedef struct fs_cond {
    void *impl;
} fs_cond_t;

#define FS_MUTEX_INITIALIZER {NULL}
#define FS_COND_INITIALIZER {NULL}

int fs_mutex_init(fs_mutex_t *mutex);
int fs_mutex_destroy(fs_mutex_t *mutex);
int fs_mutex_lock(fs_mutex_t *mutex);
int fs_mutex_unlock(fs_mutex_t *mutex);

int fs_cond_init(fs_cond_t *cond);
int fs_cond_destroy(fs_cond_t *cond);
int fs_cond_wait(fs_cond_t *cond, fs_mutex_t *mutex);
int fs_cond_broadcast(fs_cond_t *cond);

#ifdef __cplusplus
}
#endif

#endif
