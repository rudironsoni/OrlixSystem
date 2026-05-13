#ifndef KERNEL_TIME_H
#define KERNEL_TIME_H

#include <linux/types.h>
#include <uapi/linux/time.h>

#ifdef __cplusplus
extern "C" {
#endif

__kernel_old_time_t time_impl(__kernel_old_time_t *tloc);
int gettimeofday_impl(struct __kernel_old_timeval *tv, void *tz);
int settimeofday_impl(const struct __kernel_old_timeval *tv, const void *tz);
int clock_gettime_impl(__kernel_clockid_t clk_id, struct __kernel_timespec *tp);
int nanosleep_impl(const struct __kernel_timespec *req, struct __kernel_timespec *rem);
int interval_timer_get_impl(int which, struct __kernel_old_itimerval *curr_value);
int interval_timer_set_impl(int which, const struct __kernel_old_itimerval *new_value,
                            struct __kernel_old_itimerval *old_value);

#ifdef __cplusplus
}
#endif

#endif
