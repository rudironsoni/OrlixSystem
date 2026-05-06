#ifndef IXLAND_INTERNAL_IOS_FS_BACKING_IO_H
#define IXLAND_INTERNAL_IOS_FS_BACKING_IO_H

#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef pthread_mutex_t kmutex_t;
typedef pthread_cond_t kcond_t;
typedef pthread_t kthread_t;
typedef pthread_once_t konce_t;
typedef pthread_attr_t kthread_attr_t;
typedef sigset_t ksigset_t;

#define KMUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER
#define KCOND_INITIALIZER PTHREAD_COND_INITIALIZER
#define KONCE_INIT PTHREAD_ONCE_INIT
#define KSTDIN_FILENO STDIN_FILENO
#define KSTDOUT_FILENO STDOUT_FILENO
#define KSTDERR_FILENO STDERR_FILENO
#define KAT_FDCWD AT_FDCWD

static inline int kmutex_init_impl(kmutex_t *mutex) {
    return pthread_mutex_init(mutex, NULL);
}

static inline int kmutex_destroy_impl(kmutex_t *mutex) {
    return pthread_mutex_destroy(mutex);
}

static inline int kmutex_lock_impl(kmutex_t *mutex) {
    return pthread_mutex_lock(mutex);
}

static inline int kmutex_unlock_impl(kmutex_t *mutex) {
    return pthread_mutex_unlock(mutex);
}

static inline int kcond_init_impl(kcond_t *cond) {
    return pthread_cond_init(cond, NULL);
}

static inline int kcond_destroy_impl(kcond_t *cond) {
    return pthread_cond_destroy(cond);
}

static inline int kcond_wait_impl(kcond_t *cond, kmutex_t *mutex) {
    return pthread_cond_wait(cond, mutex);
}

static inline int kcond_broadcast_impl(kcond_t *cond) {
    return pthread_cond_broadcast(cond);
}

static inline int kthread_attr_init_impl(kthread_attr_t *attr) {
    return pthread_attr_init(attr);
}

static inline int kthread_attr_destroy_impl(kthread_attr_t *attr) {
    return pthread_attr_destroy(attr);
}

static inline int kthread_attr_setstacksize_impl(kthread_attr_t *attr, size_t stacksize) {
    return pthread_attr_setstacksize(attr, stacksize);
}

static inline int kthread_create_impl(kthread_t *thread, const kthread_attr_t *attr,
                                        void *(*start_routine)(void *), void *arg) {
    return pthread_create(thread, attr, start_routine, arg);
}

static inline int kthread_detach_impl(kthread_t thread) {
    return pthread_detach(thread);
}

static inline kthread_t kthread_self_impl(void) {
    return pthread_self();
}

static inline void kthread_exit_impl(void *value_ptr) {
    pthread_exit(value_ptr);
}

static inline int konce_impl(konce_t *once_control, void (*init_routine)(void)) {
    return pthread_once(once_control, init_routine);
}

static inline int ksigemptyset_impl(ksigset_t *set) {
    return sigemptyset(set);
}

static inline int ksigaddset_impl(ksigset_t *set, int signo) {
    return sigaddset(set, signo);
}

static inline int ksigismember_impl(const ksigset_t *set, int signo) {
    return sigismember(set, signo);
}

static inline int kthread_sigmask_impl(int how, const ksigset_t *set, ksigset_t *oldset) {
    return pthread_sigmask(how, set, oldset);
}

static inline int ktime_get_monotonic_impl(clockid_t clock_id, struct timespec *tp) {
    return clock_gettime(clock_id, tp);
}

/* Alias for ktime_get_monotonic_impl */
static inline int kclock_gettime_impl(clockid_t clock_id, struct timespec *tp) {
    return clock_gettime(clock_id, tp);
}

/* Host container path discovery - quarantined in iOS bridge */
int vfs_discover_persistent_root(char *path, size_t path_len);
int vfs_discover_cache_root(char *path, size_t path_len);
int vfs_discover_temp_root(char *path, size_t path_len);

/* Stat operations - delegated to path_host.h */
#include "path_host.h"

/* Host filesystem operations via direct syscalls */
int host_open_impl(const char *path, int flags, mode_t mode);
int host_close_impl(int fd);
int host_dup_impl(int fd);
int host_fstat_impl(int fd, struct linux_stat *statbuf);
ssize_t host_read_impl(int fd, void *buf, size_t count);
ssize_t host_write_impl(int fd, const void *buf, size_t count);
off_t host_lseek_impl(int fd, off_t offset, int whence);
ssize_t host_pread_impl(int fd, void *buf, size_t count, off_t offset);
ssize_t host_pwrite_impl(int fd, const void *buf, size_t count, off_t offset);
ssize_t host_readv_impl(int fd, const struct iovec *iov, int iovcnt);
ssize_t host_writev_impl(int fd, const struct iovec *iov, int iovcnt);
int host_poll_impl(struct pollfd *fds, nfds_t nfds, int timeout);
int host_ioctl_impl(int fd, unsigned long request, void *arg);
int host_truncate_impl(const char *path, off_t length);
int host_ftruncate_impl(int fd, off_t length);
int host_ensure_directory_impl(const char *path, mode_t mode);
int host_fcntl_impl(int fd, int cmd, ...);

#ifdef __cplusplus
}
#endif

#endif
