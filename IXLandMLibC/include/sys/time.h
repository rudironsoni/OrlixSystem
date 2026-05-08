#ifndef IXLAND_MLIBC_SYS_TIME_H
#define IXLAND_MLIBC_SYS_TIME_H

#ifndef _SYS_TIME_H_
#define _SYS_TIME_H_
#endif

#include "../time.h"

#ifndef _STRUCT_TIMEVAL
#define _STRUCT_TIMEVAL struct timeval
_STRUCT_TIMEVAL {
    time_t tv_sec;
    suseconds_t tv_usec;
};
#endif

struct itimerval {
    struct timeval it_interval;
    struct timeval it_value;
};

struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
};

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
