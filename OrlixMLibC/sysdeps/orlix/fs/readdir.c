#include <errno.h>
#include <sys/types.h>

extern ssize_t getdents_impl(int fd, void *dirp, size_t count);
extern ssize_t getdents64_impl(int fd, void *dirp, size_t count);

static ssize_t host_ssize_result_from_kernel(ssize_t ret) {
    if (ret < 0) {
        errno = (int)-ret;
        return -1;
    }
    return ret;
}

__attribute__((visibility("default"))) ssize_t getdents(int fd, void *dirp, size_t count) {
    return host_ssize_result_from_kernel(getdents_impl(fd, dirp, count));
}

__attribute__((visibility("default"))) ssize_t getdents64(int fd, void *dirp, size_t count) {
    return host_ssize_result_from_kernel(getdents64_impl(fd, dirp, count));
}
