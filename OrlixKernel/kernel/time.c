/* Time and Clock Subsystem
 *
 * Canonical owner for time-related syscalls:
 * - time(), gettimeofday(), settimeofday()
 * - clock_gettime(), clock_settime(), clock_getres()
 * - nanosleep(), usleep(), sleep()
 * - alarm(), setitimer(), getitimer()
 *
 * Linux-shaped canonical owner - iOS mediation via time_darwin.c
 */

#include "time_state.h"

#include <stdint.h>

#include <uapi/linux/errno.h>
#include <linux/string.h>
#include <uapi/linux/time.h>
#include <uapi/linux/time_types.h>

#include "task.h"
#include "signal.h"
#include "wait_queue.h"

/* Forward declarations for private implementation - defined in time_darwin.c */
__kernel_old_time_t time_impl(__kernel_old_time_t *tloc);
int gettimeofday_impl(struct __kernel_old_timeval *tv, void *tz);
int settimeofday_impl(const struct __kernel_old_timeval *tv, const void *tz);
int clock_gettime_impl(__kernel_clockid_t clk_id, struct __kernel_timespec *tp);
int clock_getres_impl(__kernel_clockid_t clk_id, struct __kernel_timespec *res);
int clock_settime_impl(__kernel_clockid_t clk_id, const struct __kernel_timespec *tp);
unsigned int sleep_impl(unsigned int seconds);
int usleep_impl(__u32 usec);
int setitimer_impl(int which, const struct __kernel_old_itimerval *new_value, struct __kernel_old_itimerval *old_value);
int getitimer_impl(int which, struct __kernel_old_itimerval *curr_value);
unsigned int alarm_impl(unsigned int seconds);

int nanosleep_impl(const struct __kernel_timespec *req, struct __kernel_timespec *rem) {
    uint64_t total_ms;
    int ret;

    if (!req) {
        return -EFAULT;
    }
    if (req->tv_sec < 0 || req->tv_nsec < 0 || req->tv_nsec >= 1000000000L) {
        return -EINVAL;
    }

    total_ms = (uint64_t)req->tv_sec * 1000ULL + ((uint64_t)req->tv_nsec + 999999ULL) / 1000000ULL;
    if (total_ms > (uint64_t)INT32_MAX) {
        total_ms = (uint64_t)INT32_MAX;
    }

    if (signal_has_unblocked_pending(get_current())) {
        if (rem) {
            *rem = *req;
        }
        task_restart_record_impl(get_current(), TASK_RESTART_NANOSLEEP,
                                 (uint64_t)(uintptr_t)req, (uint64_t)(uintptr_t)rem,
                                 0, 0, 0, 0);
        return -EINTR;
    }

    ret = wait_queue_sleep_ms((int)total_ms);
    if (ret == -EINTR) {
        if (rem) {
            *rem = *req;
        }
        task_restart_record_impl(get_current(), TASK_RESTART_NANOSLEEP,
                                 (uint64_t)(uintptr_t)req, (uint64_t)(uintptr_t)rem,
                                 0, 0, 0, 0);
        return -EINTR;
    }
    if (ret < 0) {
        return ret;
    }
    if (rem) {
        rem->tv_sec = 0;
        rem->tv_nsec = 0;
    }
    return 0;
}

int linux_realtime_now_impl(struct __kernel_timespec *tp) {
    struct __kernel_timespec backing_ts;
    int ret;

    if (!tp) {
        return -EFAULT;
    }
    ret = clock_gettime_impl(CLOCK_REALTIME, &backing_ts);
    if (ret < 0) {
        return ret;
    }
    tp->tv_sec = (__kernel_time64_t)backing_ts.tv_sec;
    tp->tv_nsec = (long long)backing_ts.tv_nsec;
    return 0;
}

int interval_timer_get_impl(int which, struct __kernel_old_itimerval *curr_value) {
    if (which != ITIMER_REAL && which != ITIMER_VIRTUAL && which != ITIMER_PROF) {
        return -EINVAL;
    }
    if (!curr_value) {
        return -EFAULT;
    }
    memset(curr_value, 0, sizeof(*curr_value));
    return 0;
}

int interval_timer_set_impl(int which, const struct __kernel_old_itimerval *new_value,
                            struct __kernel_old_itimerval *old_value) {
    int ret;

    if (which != ITIMER_REAL && which != ITIMER_VIRTUAL && which != ITIMER_PROF) {
        return -EINVAL;
    }
    if (!new_value) {
        return -EFAULT;
    }
    if (new_value->it_interval.tv_sec < 0 || new_value->it_value.tv_sec < 0 ||
        new_value->it_interval.tv_usec < 0 || new_value->it_interval.tv_usec >= 1000000 ||
        new_value->it_value.tv_usec < 0 || new_value->it_value.tv_usec >= 1000000) {
        return -EINVAL;
    }
    if (old_value) {
        ret = interval_timer_get_impl(which, old_value);
        if (ret < 0) {
            return ret;
        }
    }
    if (new_value->it_interval.tv_sec != 0 || new_value->it_interval.tv_usec != 0 ||
        new_value->it_value.tv_sec != 0 || new_value->it_value.tv_usec != 0) {
        return -ENOSYS;
    }
    return 0;
}
