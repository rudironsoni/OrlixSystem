/* iXland - Synchronization Primitives
 *
 * Canonical owner for sync syscalls:
 * - futex() - Fast Userspace muTEX
 * - set_robust_list(), get_robust_list()
 * - restart_syscall()
 *
 * Linux-shaped canonical owner - iOS mediation as implementation detail
 * Note: futex is Linux-specific; unsupported operations currently reject with ENOSYS
 */

#include "futex.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <time.h>

#include <linux/futex.h>

/* ============================================================================
 * FUTEX - Fast Userspace muTEX
 * ============================================================================ */

/* ABI truth comes from vendored Linux UAPI: <linux/futex.h> */

static int futex_timeout_ms(const struct timespec *timeout) {
    int64_t ms;

    if (!timeout) {
        return -1;
    }
    if (timeout->tv_sec < 0 || timeout->tv_nsec < 0 || timeout->tv_nsec >= 1000000000L) {
        errno = EINVAL;
        return -2;
    }
    if (timeout->tv_sec > (INT64_MAX / 1000)) {
        return INT_MAX;
    }
    ms = (int64_t)timeout->tv_sec * 1000;
    ms += (timeout->tv_nsec + 999999L) / 1000000L;
    if (ms > INT_MAX) {
        return INT_MAX;
    }
    return (int)ms;
}

__attribute__((visibility("default"))) int futex(int *uaddr, int futex_op, int val,
const struct timespec *timeout, int *uaddr2, int val3) {
int timeout_ms;

(void)uaddr2;
(void)val3;

timeout_ms = futex_timeout_ms(timeout);
if (timeout_ms == -2) {
return -1;
}
return futex_op_impl(uaddr, futex_op, val, timeout_ms);
}

__attribute__((visibility("default"))) int set_robust_list(void *head, unsigned long len) {
return set_robust_list_impl(head, len);
}

__attribute__((visibility("default"))) int get_robust_list(int pid, void **head, unsigned long *len) {
return get_robust_list_impl(pid, head, len);
}

/*
 * NOTE: the generic variadic entrypoint was removed due to host header conflicts.
 * Use specific wrappers (futex, set_robust_list, etc.) instead.

*/
