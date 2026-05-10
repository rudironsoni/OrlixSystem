#include <errno.h>
#include <stddef.h>

#include <linux/utsname.h>

extern int uname_impl(struct new_utsname *buf);
extern int gethostname_impl(char *name, size_t len);
extern int sethostname_impl(const char *name, size_t len);
extern int getdomainname_impl(char *name, size_t len);
extern int setdomainname_impl(const char *name, size_t len);

static int wrap_int_result(int ret) {
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}

__attribute__((visibility("default"))) int uname(struct new_utsname *buf) {
    return wrap_int_result(uname_impl(buf));
}

__attribute__((visibility("default"))) int gethostname(char *name, size_t len) {
    return wrap_int_result(gethostname_impl(name, len));
}

__attribute__((visibility("default"))) int sethostname(const char *name, int len) {
    if (len < 0) {
        errno = EINVAL;
        return -1;
    }
    return wrap_int_result(sethostname_impl(name, (size_t)len));
}

__attribute__((visibility("default"))) int getdomainname(char *name, int len) {
    if (len < 0) {
        errno = EINVAL;
        return -1;
    }
    return wrap_int_result(getdomainname_impl(name, (size_t)len));
}

__attribute__((visibility("default"))) int setdomainname(const char *name, int len) {
    if (len < 0) {
        errno = EINVAL;
        return -1;
    }
    return wrap_int_result(setdomainname_impl(name, (size_t)len));
}
