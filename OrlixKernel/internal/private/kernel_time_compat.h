#ifndef ORLIX_INTERNAL_PRIVATE_KERNEL_TIME_COMPAT_H
#define ORLIX_INTERNAL_PRIVATE_KERNEL_TIME_COMPAT_H

/*
 * Kernel-private time compatibility surface for Linux-owner code that needs
 * Linux-owned time structs and Linux clock constants without importing Darwin
 * libc or package-facing OrlixMLibC headers.
 */
#include <linux/types.h>

typedef __kernel_old_time_t kernel_time_t;
typedef __kernel_suseconds_t kernel_suseconds_t;

struct kernel_timespec {
    kernel_time_t tv_sec;
    long tv_nsec;
};

struct kernel_timeval {
    kernel_time_t tv_sec;
    kernel_suseconds_t tv_usec;
};

struct kernel_timezone {
    int tz_minuteswest;
    int tz_dsttime;
};

struct kernel_itimerval {
    struct kernel_timeval it_interval;
    struct kernel_timeval it_value;
};

struct kernel_itimerspec {
    struct kernel_timespec it_interval;
    struct kernel_timespec it_value;
};

#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif

#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

#ifndef CLOCK_PROCESS_CPUTIME_ID
#define CLOCK_PROCESS_CPUTIME_ID 2
#endif

#ifndef CLOCK_THREAD_CPUTIME_ID
#define CLOCK_THREAD_CPUTIME_ID 3
#endif

#ifndef CLOCK_MONOTONIC_RAW
#define CLOCK_MONOTONIC_RAW 4
#endif

#ifndef CLOCK_REALTIME_COARSE
#define CLOCK_REALTIME_COARSE 5
#endif

#ifndef CLOCK_MONOTONIC_COARSE
#define CLOCK_MONOTONIC_COARSE 6
#endif

#ifndef CLOCK_BOOTTIME
#define CLOCK_BOOTTIME 7
#endif

#ifndef CLOCK_REALTIME_ALARM
#define CLOCK_REALTIME_ALARM 8
#endif

#ifndef CLOCK_BOOTTIME_ALARM
#define CLOCK_BOOTTIME_ALARM 9
#endif

#ifndef CLOCK_TAI
#define CLOCK_TAI 11
#endif

#ifndef ITIMER_REAL
#define ITIMER_REAL 0
#endif

#ifndef ITIMER_VIRTUAL
#define ITIMER_VIRTUAL 1
#endif

#ifndef ITIMER_PROF
#define ITIMER_PROF 2
#endif

#endif
