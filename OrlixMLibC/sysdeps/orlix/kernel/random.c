#include <errno.h>

#include <sys/types.h>
#include <linux/random.h>

extern void arc4random_buf(void *buf, size_t nbytes);

void get_random_bytes(void *buf, size_t len) {
    arc4random_buf(buf, len);
}

extern __kernel_ssize_t getrandom_impl(void *buf, size_t buflen, unsigned int flags);
extern int getentropy_impl(void *buffer, size_t length);

__attribute__((visibility("default"))) ssize_t getrandom(void *buf, size_t buflen, unsigned int flags) {
    __kernel_ssize_t ret = getrandom_impl(buf, buflen, flags);
    if (ret < 0) {
        errno = (int)-ret;
        return -1;
    }
    return ret;
}

__attribute__((visibility("default"))) int getentropy(void *buffer, size_t length) {
    int ret = getentropy_impl(buffer, length);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}
