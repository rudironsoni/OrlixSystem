#ifndef FS_SYNC_H
#define FS_SYNC_H

#ifdef __cplusplus
extern "C" {
#endif

#define FS_MUTEX_STORAGE_SIZE 64
#define FS_COND_STORAGE_SIZE 64

typedef struct fs_mutex {
    char storage[FS_MUTEX_STORAGE_SIZE];
    int initialized;
} fs_mutex_t;

typedef struct fs_cond {
    char storage[FS_COND_STORAGE_SIZE];
    int initialized;
} fs_cond_t;

#define FS_MUTEX_INITIALIZER {{0}, 0}
#define FS_COND_INITIALIZER {{0}, 0}

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
