#ifndef KERNEL_RESOURCE_H
#define KERNEL_RESOURCE_H

#include <linux/time_types.h>

struct linux_tms_kernel {
    __kernel_clock_t tms_utime;
    __kernel_clock_t tms_stime;
    __kernel_clock_t tms_cutime;
    __kernel_clock_t tms_cstime;
};

struct linux_rusage_kernel {
    struct __kernel_old_timeval ru_utime;
    struct __kernel_old_timeval ru_stime;
    __kernel_long_t ru_maxrss;
    __kernel_long_t ru_ixrss;
    __kernel_long_t ru_idrss;
    __kernel_long_t ru_isrss;
    __kernel_long_t ru_minflt;
    __kernel_long_t ru_majflt;
    __kernel_long_t ru_nswap;
    __kernel_long_t ru_inblock;
    __kernel_long_t ru_oublock;
    __kernel_long_t ru_msgsnd;
    __kernel_long_t ru_msgrcv;
    __kernel_long_t ru_nsignals;
    __kernel_long_t ru_nvcsw;
    __kernel_long_t ru_nivcsw;
};

long times_impl(struct linux_tms_kernel *buf);
int linux_getrusage_impl(int who, struct linux_rusage_kernel *usage);

#endif
