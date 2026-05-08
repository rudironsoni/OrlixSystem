#ifndef IXLAND_INTERNAL_PRIVATE_KERNEL_TIME_COMPAT_H
#define IXLAND_INTERNAL_PRIVATE_KERNEL_TIME_COMPAT_H

/*
 * Kernel-private time compatibility surface for Linux-owner code that needs a
 * concrete timespec and Linux clock constants without importing Darwin libc or
 * package-facing IXLandMLibC headers.
 */

#ifndef _STRUCT_TIMESPEC
#define _STRUCT_TIMESPEC struct timespec
_STRUCT_TIMESPEC {
    __INTPTR_TYPE__ tv_sec;
    long tv_nsec;
};
#endif

#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif

#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

#endif
