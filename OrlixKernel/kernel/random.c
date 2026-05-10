/* iXland - Random Number Generator
 *
 * Canonical owner for random syscalls:
 * - getrandom()
 * - getentropy()
 *
 * Linux-shaped canonical owner - iOS mediation as implementation detail
 */

#include <linux/errno.h>
#include <linux/random.h>

#include <stddef.h>
#include <stdint.h>

#include <linux/types.h>

extern void get_random_bytes(void *buf, size_t len);

/* ============================================================================
 * GETRANDOM - Linux-compatible random bytes
 * ============================================================================ */

__kernel_ssize_t getrandom_impl(void *buf, size_t buflen, unsigned int flags) {
    if (!buf) {
        return -EFAULT;
    }
    if ((flags & ~(GRND_NONBLOCK | GRND_RANDOM | GRND_INSECURE)) != 0) {
        return -EINVAL;
    }
    if (buflen == 0) {
        return 0;
    }
    get_random_bytes(buf, buflen);
    return (__kernel_ssize_t)buflen;
}

/* ============================================================================
 * GETENTROPY - BSD-compatible
 * ============================================================================ */

int getentropy_impl(void *buffer, size_t length) {
    if (length > 256) {
        return -EIO;
    }
    return getrandom_impl(buffer, length, 0) == (__kernel_ssize_t)length ? 0 : -EIO;
}
