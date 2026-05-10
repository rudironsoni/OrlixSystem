#include <errno.h>
#include <stddef.h>
#include <sys/types.h>

extern int setxattr_impl(const char *path, const char *name, const void *value, size_t size,
                         int flags);
extern int lsetxattr_impl(const char *path, const char *name, const void *value, size_t size,
                          int flags);
extern int fsetxattr_impl(int fd, const char *name, const void *value, size_t size, int flags);
extern long getxattr_impl(const char *path, const char *name, void *value, size_t size);
extern long lgetxattr_impl(const char *path, const char *name, void *value, size_t size);
extern long fgetxattr_impl(int fd, const char *name, void *value, size_t size);
extern int removexattr_impl(const char *path, const char *name);
extern int lremovexattr_impl(const char *path, const char *name);
extern int fremovexattr_impl(int fd, const char *name);
extern long listxattr_impl(const char *path, char *list, size_t size);
extern long llistxattr_impl(const char *path, char *list, size_t size);
extern long flistxattr_impl(int fd, char *list, size_t size);

static int wrap_int_result(int ret) {
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}

static ssize_t wrap_ssize_result(long ret) {
    if (ret < 0) {
        errno = (int)-ret;
        return -1;
    }
    return (ssize_t)ret;
}

__attribute__((visibility("default"))) int setxattr(const char *path, const char *name,
                                                    const void *value, size_t size, int flags) {
    return wrap_int_result(setxattr_impl(path, name, value, size, flags));
}

__attribute__((visibility("default"))) int lsetxattr(const char *path, const char *name,
                                                     const void *value, size_t size, int flags) {
    return wrap_int_result(lsetxattr_impl(path, name, value, size, flags));
}

__attribute__((visibility("default"))) int fsetxattr(int fd, const char *name, const void *value,
                                                     size_t size, int flags) {
    return wrap_int_result(fsetxattr_impl(fd, name, value, size, flags));
}

__attribute__((visibility("default"))) ssize_t getxattr(const char *path, const char *name,
                                                        void *value, size_t size) {
    return wrap_ssize_result(getxattr_impl(path, name, value, size));
}

__attribute__((visibility("default"))) ssize_t lgetxattr(const char *path, const char *name,
                                                         void *value, size_t size) {
    return wrap_ssize_result(lgetxattr_impl(path, name, value, size));
}

__attribute__((visibility("default"))) ssize_t fgetxattr(int fd, const char *name, void *value,
                                                         size_t size) {
    return wrap_ssize_result(fgetxattr_impl(fd, name, value, size));
}

__attribute__((visibility("default"))) int removexattr(const char *path, const char *name) {
    return wrap_int_result(removexattr_impl(path, name));
}

__attribute__((visibility("default"))) int lremovexattr(const char *path, const char *name) {
    return wrap_int_result(lremovexattr_impl(path, name));
}

__attribute__((visibility("default"))) int fremovexattr(int fd, const char *name) {
    return wrap_int_result(fremovexattr_impl(fd, name));
}

__attribute__((visibility("default"))) ssize_t listxattr(const char *path, char *list,
                                                         size_t size) {
    return wrap_ssize_result(listxattr_impl(path, list, size));
}

__attribute__((visibility("default"))) ssize_t llistxattr(const char *path, char *list,
                                                          size_t size) {
    return wrap_ssize_result(llistxattr_impl(path, list, size));
}

__attribute__((visibility("default"))) ssize_t flistxattr(int fd, char *list, size_t size) {
    return wrap_ssize_result(flistxattr_impl(fd, list, size));
}
