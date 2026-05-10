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
#include <linux/time_types.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#include "../fs/errno_translation.h"

static const clockid_t host_clock_realtime = _CLOCK_REALTIME;
static const clockid_t host_clock_monotonic = _CLOCK_MONOTONIC;

struct host_timezone_payload {
    int tz_minuteswest;
    int tz_dsttime;
};

/* ============================================================================
 * TIME - Darwin implementation using host libc
 * ============================================================================ */

__kernel_old_time_t time_impl(__kernel_old_time_t *tloc) {
    time_t t = time(NULL);
    if (tloc) {
        *tloc = t;
    }
    return t;
}

/* ============================================================================
 * GETTIMEOFDAY - Darwin implementation
 * ============================================================================ */

int gettimeofday_impl(struct __kernel_old_timeval *tv, void *tz) {
    uint64_t ns;

    if (!tv) {
        return -linux_errno_from_darwin_errno(EFAULT);
    }
    ns = clock_gettime_nsec_np(host_clock_realtime);
    tv->tv_sec = (__kernel_old_time_t)(ns / 1000000000ULL);
    tv->tv_usec = (__kernel_suseconds_t)((ns % 1000000000ULL) / 1000ULL);
    if (tz) {
        struct host_timezone_payload *payload = (struct host_timezone_payload *)tz;
        payload->tz_minuteswest = 0;
        payload->tz_dsttime = 0;
    }
    return 0;
}

int settimeofday_impl(const struct __kernel_old_timeval *tv, const void *tz) {
    (void)tv;
    (void)tz;
    return -linux_errno_from_darwin_errno(EPERM);
}

/* ============================================================================
 * CLOCK_GETTIME - Darwin implementation
 * ============================================================================ */

int clock_gettime_impl(__kernel_clockid_t clk_id, struct __kernel_timespec *tp) {
    uint64_t ns;

    if (!tp) {
        return -linux_errno_from_darwin_errno(EFAULT);
    }

    switch (clk_id) {
    case 0:
        ns = clock_gettime_nsec_np(host_clock_realtime);
        tp->tv_sec = (__kernel_old_time_t)(ns / 1000000000ULL);
        tp->tv_nsec = (long)(ns % 1000000000ULL);
        return 0;
    case 1:
        ns = clock_gettime_nsec_np(host_clock_monotonic);
        tp->tv_sec = (__kernel_old_time_t)(ns / 1000000000ULL);
        tp->tv_nsec = (long)(ns % 1000000000ULL);
        return 0;
    default:
        return -linux_errno_from_darwin_errno(EINVAL);
    }
}

int clock_getres_impl(__kernel_clockid_t clk_id, struct __kernel_timespec *res) {
    (void)clk_id;
    (void)res;
    return -linux_errno_from_darwin_errno(ENOSYS);
}

int clock_settime_impl(__kernel_clockid_t clk_id, const struct __kernel_timespec *tp) {
    (void)clk_id;
    (void)tp;
    return -linux_errno_from_darwin_errno(EPERM);
}

/* ============================================================================
 * SLEEP FUNCTIONS - Darwin implementation
 * ============================================================================ */

unsigned int sleep_impl(unsigned int seconds) {
    return sleep(seconds);
}

int usleep_impl(__u32 usec) {
    return usleep(usec) == 0 ? 0 : -errno;
}

/* ============================================================================
 * ITIMER - Interval timers (iOS does not support)
 * ============================================================================ */

int setitimer_impl(int which, const struct __kernel_old_itimerval *new_value, struct __kernel_old_itimerval *old_value) {
    (void)which;
    (void)new_value;
    (void)old_value;
    return -linux_errno_from_darwin_errno(ENOSYS);
}

int getitimer_impl(int which, struct __kernel_old_itimerval *curr_value) {
    (void)which;
    (void)curr_value;
    return -linux_errno_from_darwin_errno(ENOSYS);
}

unsigned int alarm_impl(unsigned int seconds) {
    (void)seconds;
    return 0;
}
