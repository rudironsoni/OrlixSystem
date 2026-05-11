#ifndef ORLIX_HOST_ADAPTER_FS_BACKING_IO_H
#define ORLIX_HOST_ADAPTER_FS_BACKING_IO_H

#include <stdint.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

struct stat;

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
    return pthread_mutex_init(mutex, (const pthread_mutexattr_t *)0);
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
    return pthread_cond_init(cond, (const pthread_condattr_t *)0);
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

static inline int ktime_get_monotonic_impl(int clock_id, struct timespec *tp) {
    return clock_gettime((clockid_t)clock_id, tp);
}

/* Alias for ktime_get_monotonic_impl */
static inline int kclock_gettime_impl(int clock_id, struct timespec *tp) {
    return clock_gettime((clockid_t)clock_id, tp);
}

/* Backing root discovery - realized privately inside OrlixHostAdapter. */
int backing_root_discover_persistent(char *path, size_t path_len);
int backing_root_discover_cache(char *path, size_t path_len);
int backing_root_discover_temp(char *path, size_t path_len);

/* Host-backed path mediation surface. Keep this Darwin-safe. */
struct stat;
int backing_stat(const char *path, struct stat *statbuf);
int backing_lstat(const char *path, struct stat *statbuf);
int backing_access(const char *path, int mode);
_Bool backing_path_is_own_sandbox(const char *path);
_Bool backing_path_is_external(const char *path);
int backing_directory_is_empty(const char *path);
int backing_rename_with_flags(int fromfd, const char *from, int tofd, const char *to,
                              unsigned int flags);
int backing_rename_exchange(const char *from, const char *to);
int backing_mkdir(const char *pathname, uint32_t mode);
int backing_rmdir(const char *pathname);
int backing_unlink(const char *pathname);
int backing_link(const char *oldpath, const char *newpath);
int backing_linkat(const char *oldpath, const char *newpath, int follow_symlink);
int backing_symlink(const char *target, const char *linkpath);
long backing_readlink(const char *pathname, char *buf, size_t bufsiz);
int backing_fchdir(int fd);

/* Backing filesystem operations via direct syscalls. */
int backing_open(const char *path, int flags, uint32_t mode);
int backing_close(int fd);
int backing_dup(int fd);
int backing_fstat(int fd, struct stat *statbuf);
ssize_t backing_read(int fd, void *buf, size_t count);
ssize_t backing_write(int fd, const void *buf, size_t count);
int64_t backing_lseek(int fd, int64_t offset, int whence);
int64_t backing_pread(int fd, void *buf, size_t count, int64_t offset);
int64_t backing_pwrite(int fd, const void *buf, size_t count, int64_t offset);
ssize_t backing_readv(int fd, const struct iovec *iov, int iovcnt);
ssize_t backing_writev(int fd, const struct iovec *iov, int iovcnt);
int backing_poll(struct pollfd *fds, nfds_t nfds, int timeout);
int backing_ioctl(int fd, unsigned long request, void *arg);
int backing_truncate(const char *path, int64_t length);
int backing_ftruncate(int fd, int64_t length);
int backing_ensure_directory(const char *path, uint32_t mode);
int backing_fcntl(int fd, int cmd, ...);

#ifdef __cplusplus
}
#endif

#endif
