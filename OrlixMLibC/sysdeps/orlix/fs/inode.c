#include <errno.h>
#include <stdint.h>

extern int chmod_impl(const char *pathname, uint32_t mode);
extern int fchmod_impl(int fd, uint32_t mode);
extern int fchmodat_impl(int dirfd, const char *pathname, uint32_t mode, int flags);
extern int chown_impl(const char *pathname, uint32_t owner, uint32_t group);
extern int fchown_impl(int fd, uint32_t owner, uint32_t group);
extern int lchown_impl(const char *pathname, uint32_t owner, uint32_t group);
extern int fchownat_impl(int dirfd, const char *pathname, uint32_t owner, uint32_t group, int flags);
extern uint32_t umask_impl(uint32_t mask);
extern int truncate_impl(const char *path, int64_t length);
extern int ftruncate_impl(int fd, int64_t length);

static int host_int_result_from_kernel(int ret) {
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}

__attribute__((visibility("default"))) int chmod(const char *pathname, uint32_t mode) {
    return host_int_result_from_kernel(chmod_impl(pathname, mode));
}

__attribute__((visibility("default"))) int fchmod(int fd, uint32_t mode) {
    return host_int_result_from_kernel(fchmod_impl(fd, mode));
}

__attribute__((visibility("default"))) int fchmodat(int dirfd, const char *pathname, uint32_t mode, int flags) {
    return host_int_result_from_kernel(fchmodat_impl(dirfd, pathname, mode, flags));
}

__attribute__((visibility("default"))) int chown(const char *pathname, uint32_t owner, uint32_t group) {
    return host_int_result_from_kernel(chown_impl(pathname, owner, group));
}

__attribute__((visibility("default"))) int fchown(int fd, uint32_t owner, uint32_t group) {
    return host_int_result_from_kernel(fchown_impl(fd, owner, group));
}

__attribute__((visibility("default"))) int lchown(const char *pathname, uint32_t owner, uint32_t group) {
    return host_int_result_from_kernel(lchown_impl(pathname, owner, group));
}

__attribute__((visibility("default"))) int fchownat(int dirfd, const char *pathname, uint32_t owner, uint32_t group, int flags) {
    return host_int_result_from_kernel(fchownat_impl(dirfd, pathname, owner, group, flags));
}

__attribute__((visibility("default"))) uint32_t umask(uint32_t mask) {
    return umask_impl(mask);
}

__attribute__((visibility("default"))) int truncate(const char *path, int64_t length) {
    return host_int_result_from_kernel(truncate_impl(path, length));
}

__attribute__((visibility("default"))) int ftruncate(int fd, int64_t length) {
    return host_int_result_from_kernel(ftruncate_impl(fd, length));
}
