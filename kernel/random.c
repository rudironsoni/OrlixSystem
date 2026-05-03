/* iXland - Random Number Generator
 *
 * Canonical owner for random syscalls:
 * - getrandom()
 * - getentropy()
 *
 * Linux-shaped canonical owner - iOS mediation as implementation detail
 */

#include <errno.h>
#include <linux/fcntl.h>
#include <linux/random.h>

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "../fs/vfs.h"

extern int open_impl(const char *pathname, int flags, linux_mode_t mode);
extern ssize_t read_impl(int fd, void *buf, size_t count);
extern int close_impl(int fd);

/* ============================================================================
 * GETRANDOM - Linux-compatible random bytes
 * ============================================================================ */

ssize_t getrandom_impl(void *buf, size_t buflen, unsigned int flags) {
    ssize_t total = 0;
    char *p = buf;
    int fd;

    if (!buf) {
        errno = EFAULT;
        return -1;
    }
    if ((flags & ~(GRND_NONBLOCK | GRND_RANDOM | GRND_INSECURE)) != 0) {
        errno = EINVAL;
        return -1;
    }
    if (buflen == 0) {
        return 0;
    }

    fd = open_impl("/dev/urandom", O_RDONLY | O_CLOEXEC, 0);
    if (fd < 0) {
        return -1;
    }

    while ((size_t)total < buflen) {
        ssize_t n = read_impl(fd, p + total, buflen - (size_t)total);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            close_impl(fd);
            return -1;
        }
        if (n == 0) {
            close_impl(fd);
            errno = EIO;
            return -1;
        }
        total += n;
    }

    close_impl(fd);
    return total;
}

/* ============================================================================
 * GETENTROPY - BSD-compatible
 * ============================================================================ */

static int getentropy_impl(void *buffer, size_t length) {
    if (length > 256) {
        errno = EIO;
        return -1;
    }
    return getrandom_impl(buffer, length, 0) == (ssize_t)length ? 0 : -1;
}

/* ============================================================================
 * Public Canonical Syscalls
 * ============================================================================ */

__attribute__((visibility("default"))) ssize_t getrandom(void *buf, size_t buflen, unsigned int flags) {
    return getrandom_impl(buf, buflen, flags);
}

__attribute__((visibility("default"))) int getentropy(void *buffer, size_t length) {
    return getentropy_impl(buffer, length);
}
