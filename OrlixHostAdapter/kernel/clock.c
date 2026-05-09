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

#include "internal/private/kernel_time_compat.h"

#include <errno.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static const clockid_t host_clock_realtime = _CLOCK_REALTIME;
static const clockid_t host_clock_monotonic = _CLOCK_MONOTONIC;

/* ============================================================================
 * TIME - Darwin implementation using host libc
 * ============================================================================ */

kernel_time_t time_impl(kernel_time_t *tloc) {
    time_t t = time(NULL);
    if (tloc) {
        *tloc = t;
    }
    return t;
}

/* ============================================================================
 * GETTIMEOFDAY - Darwin implementation
 * ============================================================================ */

int gettimeofday_impl(struct kernel_timeval *tv, struct kernel_timezone *tz) {
    uint64_t ns;

    if (!tv) {
        errno = EFAULT;
        return -1;
    }
    ns = clock_gettime_nsec_np(host_clock_realtime);
    tv->tv_sec = (kernel_time_t)(ns / 1000000000ULL);
    tv->tv_usec = (kernel_suseconds_t)((ns % 1000000000ULL) / 1000ULL);
    if (tz) {
        tz->tz_minuteswest = 0;
        tz->tz_dsttime = 0;
    }
    return 0;
}

int settimeofday_impl(const struct kernel_timeval *tv, const struct kernel_timezone *tz) {
    (void)tv;
    (void)tz;
    errno = EPERM;
    return -1;
}

/* ============================================================================
 * CLOCK_GETTIME - Darwin implementation
 * ============================================================================ */

int clock_gettime_impl(__kernel_clockid_t clk_id, struct kernel_timespec *tp) {
    uint64_t ns;

    if (!tp) {
        errno = EFAULT;
        return -1;
    }

    switch (clk_id) {
    case 0:
        ns = clock_gettime_nsec_np(host_clock_realtime);
        tp->tv_sec = (kernel_time_t)(ns / 1000000000ULL);
        tp->tv_nsec = (long)(ns % 1000000000ULL);
        return 0;
    case 1:
        ns = clock_gettime_nsec_np(host_clock_monotonic);
        tp->tv_sec = (kernel_time_t)(ns / 1000000000ULL);
        tp->tv_nsec = (long)(ns % 1000000000ULL);
        return 0;
    default:
        errno = EINVAL;
        return -1;
    }
}

int clock_getres_impl(__kernel_clockid_t clk_id, struct kernel_timespec *res) {
    (void)clk_id;
    (void)res;
    errno = ENOSYS;
    return -1;
}

int clock_settime_impl(__kernel_clockid_t clk_id, const struct kernel_timespec *tp) {
    (void)clk_id;
    (void)tp;
    errno = EPERM;
    return -1;
}

/* ============================================================================
 * SLEEP FUNCTIONS - Darwin implementation
 * ============================================================================ */

unsigned int sleep_impl(unsigned int seconds) {
    return sleep(seconds);
}

int usleep_impl(useconds_t usec) {
    return usleep(usec);
}

/* ============================================================================
 * ITIMER - Interval timers (iOS does not support)
 * ============================================================================ */

int setitimer_impl(int which, const struct kernel_itimerval *new_value, struct kernel_itimerval *old_value) {
    (void)which;
    (void)new_value;
    (void)old_value;
    errno = ENOSYS;
    return -1;
}

int getitimer_impl(int which, struct kernel_itimerval *curr_value) {
    (void)which;
    (void)curr_value;
    errno = ENOSYS;
    return -1;
}

unsigned int alarm_impl(unsigned int seconds) {
    (void)seconds;
    return 0;
}
