/*
 * OrlixKernel Time Subsystem - Darwin Bridge
 *
 * This file includes Darwin headers and provides the implementation
 * using Darwin's time functions. It is the ONLY file in the time
 * subsystem that includes Darwin headers like <sys/time.h> and <time.h>.
 * This is the private bridge - the public interface is in kernel/time.c
 *
 * NOTE: This file does NOT include Linux UAPI headers. It implements
 * the _impl() functions using Darwin's native types directly.
 */

#include <errno.h>
#include <time.h>

#include "../fs/errno_translation.h"
#include "internal/timekeeping.h"

static const clockid_t host_clock_realtime = _CLOCK_REALTIME;
static const clockid_t host_clock_monotonic = _CLOCK_MONOTONIC;

int kernel_clock_now_ns(int clock_id, unsigned long long *ns_out) {
    unsigned long long ns;

    if (!ns_out) {
        return -linux_errno_from_darwin_errno(EFAULT);
    }

    switch (clock_id) {
    case 0:
        ns = clock_gettime_nsec_np(host_clock_realtime);
        break;
    case 1:
        ns = clock_gettime_nsec_np(host_clock_monotonic);
        break;
    default:
        return -linux_errno_from_darwin_errno(EINVAL);
    }
    *ns_out = ns;
    return 0;
}
