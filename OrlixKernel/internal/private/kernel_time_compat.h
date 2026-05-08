#ifndef ORLIX_INTERNAL_PRIVATE_KERNEL_TIME_COMPAT_H
#define ORLIX_INTERNAL_PRIVATE_KERNEL_TIME_COMPAT_H

/*
 * Kernel-private time compatibility surface for Linux-owner code that needs a
 * concrete timespec and Linux clock constants without importing Darwin libc or
 * package-facing OrlixMLibC headers.
 */

#ifndef _TIME_T
#define _TIME_T
typedef __INTPTR_TYPE__ time_t;
#endif

#ifndef _SUSECONDS_T
#define _SUSECONDS_T
typedef __INT32_TYPE__ suseconds_t;
#endif

#ifndef _USECONDS_T
#define _USECONDS_T
typedef __UINT32_TYPE__ useconds_t;
#endif

#ifndef ORLIX_KERNEL_CLOCKID_T
#define ORLIX_KERNEL_CLOCKID_T
typedef int clockid_t;
#endif

#ifndef _STRUCT_TIMESPEC
#define _STRUCT_TIMESPEC struct timespec
_STRUCT_TIMESPEC {
    time_t tv_sec;
    long tv_nsec;
};
#endif

#ifndef _STRUCT_TIMEVAL
#define _STRUCT_TIMEVAL struct timeval
_STRUCT_TIMEVAL {
    time_t tv_sec;
    suseconds_t tv_usec;
};
#endif

struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
};

struct itimerval {
    struct timeval it_interval;
    struct timeval it_value;
};

struct itimerspec {
    struct timespec it_interval;
    struct timespec it_value;
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
