#ifndef IXLAND_MLIBC_TIME_H
#define IXLAND_MLIBC_TIME_H

#if !defined(_TIME_H_)
#define IXLAND_MLIBC_DEFINE_CLOCKID_T 1
#endif

#ifndef _TIME_H_
#define _TIME_H_
#endif

#include "ixlandmlibc/bits/alltypes.h"

#ifndef _STRUCT_TIMESPEC
#define _STRUCT_TIMESPEC struct timespec
_STRUCT_TIMESPEC {
    time_t tv_sec;
    long tv_nsec;
};
#endif

struct itimerspec {
    struct timespec it_interval;
    struct timespec it_value;
};

#if defined(IXLAND_MLIBC_DEFINE_CLOCKID_T) && !defined(IXLAND_MLIBC_CLOCKID_T)
#define IXLAND_MLIBC_CLOCKID_T
typedef int clockid_t;
#endif

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

#endif
