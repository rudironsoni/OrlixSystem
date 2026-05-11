/* Time and Clock Subsystem
 *
 * Canonical owner for time-related syscalls:
 * - time(), gettimeofday(), settimeofday()
 * - clock_gettime(), clock_settime(), clock_getres()
 * - nanosleep(), usleep(), sleep()
 * - alarm(), setitimer(), getitimer()
 *
 * Linux-shaped canonical owner with a narrow Darwin-backed timekeeping seam
 */

#include "time_state.h"

#include <linux/errno.h>
#include <linux/limits.h>
#include <linux/string.h>
#include <uapi/linux/time.h>

#include "task.h"
#include "signal.h"
#include "wait_queue.h"

struct timezone_payload {
    int tz_minuteswest;
    int tz_dsttime;
};

int clock_gettime_impl(__kernel_clockid_t clk_id, struct __kernel_timespec *tp);

static void time_ns_to_timespec(u64 ns, struct __kernel_timespec *tp) {
    tp->tv_sec = (__kernel_time64_t)(ns / 1000000000ULL);
    tp->tv_nsec = (long long)(ns % 1000000000ULL);
}

static void time_ns_to_timeval(u64 ns, struct __kernel_old_timeval *tv) {
    tv->tv_sec = (__kernel_old_time_t)(ns / 1000000000ULL);
    tv->tv_usec = (__kernel_suseconds_t)((ns % 1000000000ULL) / 1000ULL);
}

__kernel_old_time_t time_impl(__kernel_old_time_t *tloc) {
    struct __kernel_timespec now;
    int ret = clock_gettime_impl(CLOCK_REALTIME, &now);

    if (ret < 0) {
        return 0;
    }
    if (tloc) {
        *tloc = (__kernel_old_time_t)now.tv_sec;
    }
    return (__kernel_old_time_t)now.tv_sec;
}

int gettimeofday_impl(struct __kernel_old_timeval *tv, void *tz) {
    u64 ns;
    int ret;

    if (!tv) {
        return -EFAULT;
    }
    ret = kernel_clock_now_ns(CLOCK_REALTIME, &ns);
    if (ret < 0) {
        return ret;
    }
    time_ns_to_timeval(ns, tv);
    if (tz) {
        struct timezone_payload *payload = (struct timezone_payload *)tz;
        payload->tz_minuteswest = 0;
        payload->tz_dsttime = 0;
    }
    return 0;
}

int settimeofday_impl(const struct __kernel_old_timeval *tv, const void *tz) {
    (void)tv;
    (void)tz;
    return -EPERM;
}

int clock_gettime_impl(__kernel_clockid_t clk_id, struct __kernel_timespec *tp) {
    u64 ns;
    int ret;

    if (!tp) {
        return -EFAULT;
    }
    ret = kernel_clock_now_ns((int)clk_id, &ns);
    if (ret < 0) {
        return ret;
    }
    time_ns_to_timespec(ns, tp);
    return 0;
}

int clock_getres_impl(__kernel_clockid_t clk_id, struct __kernel_timespec *res) {
    (void)clk_id;
    (void)res;
    return -ENOSYS;
}

int clock_settime_impl(__kernel_clockid_t clk_id, const struct __kernel_timespec *tp) {
    (void)clk_id;
    (void)tp;
    return -EPERM;
}

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
    if (total_ms > (u64)S32_MAX) {
        total_ms = (u64)S32_MAX;
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
    if (!tp) {
        return -EFAULT;
    }
    return clock_gettime_impl(CLOCK_REALTIME, tp);
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

unsigned int alarm_impl(unsigned int seconds) {
    (void)seconds;
    return 0;
}
