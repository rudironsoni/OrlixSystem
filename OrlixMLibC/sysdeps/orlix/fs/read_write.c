#include <errno.h>
#include <sys/types.h>

#include <linux/uio.h>

extern __kernel_ssize_t read_impl(int fd, void *buf, size_t count);
extern __kernel_ssize_t write_impl(int fd, const void *buf, size_t count);
extern __s64 lseek_impl(int fd, __s64 offset, int whence);
extern __kernel_ssize_t pread_impl(int fd, void *buf, size_t count, __s64 offset);
extern __kernel_ssize_t pwrite_impl(int fd, const void *buf, size_t count, __s64 offset);

static long wrap_long_result(long ret) {
    if (ret < 0) {
        errno = (int)-ret;
        return -1;
    }
    return ret;
}

__attribute__((visibility("default"))) ssize_t read(int fd, void *buf, size_t count) {
    return (ssize_t)wrap_long_result((long)read_impl(fd, buf, count));
}

__attribute__((visibility("default"))) ssize_t write(int fd, const void *buf, size_t count) {
    return (ssize_t)wrap_long_result((long)write_impl(fd, buf, count));
}

__attribute__((visibility("default"))) long long lseek(int fd, long long offset, int whence) {
    return (long long)wrap_long_result((long)lseek_impl(fd, offset, whence));
}

__attribute__((visibility("default"))) ssize_t pread(int fd, void *buf, size_t count, long long offset) {
    return (ssize_t)wrap_long_result((long)pread_impl(fd, buf, count, offset));
}

__attribute__((visibility("default"))) ssize_t pwrite(int fd, const void *buf, size_t count, long long offset) {
    return (ssize_t)wrap_long_result((long)pwrite_impl(fd, buf, count, offset));
}
