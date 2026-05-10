#include <asm/statfs.h>
#include <errno.h>
#include <linux/types.h>

extern void sync_impl(void);
extern int fsync_impl(int fd);
extern int fdatasync_impl(int fd);
extern int syncfs_impl(int fd);
extern int statfs_impl(const char *path, struct statfs *buf);
extern int fstatfs_impl(int fd, struct statfs *buf);
extern int posix_fadvise_impl(int fd, __kernel_off_t offset, __kernel_off_t len, int advice);
extern int posix_fallocate_impl(int fd, __kernel_off_t offset, __kernel_off_t len);

static int host_int_result_from_kernel(int ret) {
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}

__attribute__((visibility("default"))) void sync(void) {
    sync_impl();
}

__attribute__((visibility("default"))) int fsync(int fd) {
    return host_int_result_from_kernel(fsync_impl(fd));
}

__attribute__((visibility("default"))) int fdatasync(int fd) {
    return host_int_result_from_kernel(fdatasync_impl(fd));
}

__attribute__((visibility("default"))) int syncfs(int fd) {
    return host_int_result_from_kernel(syncfs_impl(fd));
}

__attribute__((visibility("default"))) int statfs(const char *path, struct statfs *buf) {
    return host_int_result_from_kernel(statfs_impl(path, buf));
}

__attribute__((visibility("default"))) int fstatfs(int fd, struct statfs *buf) {
    return host_int_result_from_kernel(fstatfs_impl(fd, buf));
}

__attribute__((visibility("default"))) int posix_fadvise(int fd, __kernel_off_t offset,
                                                         __kernel_off_t len, int advice) {
    return host_int_result_from_kernel(posix_fadvise_impl(fd, offset, len, advice));
}

__attribute__((visibility("default"))) int posix_fallocate(int fd, __kernel_off_t offset,
                                                           __kernel_off_t len) {
    return host_int_result_from_kernel(posix_fallocate_impl(fd, offset, len));
}
