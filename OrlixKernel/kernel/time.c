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

#include "time_internal.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include <linux/time_types.h>

#include "internal/private/kernel_time_compat.h"
#include "task.h"
#include "signal.h"
#include "wait_queue.h"

/* Forward declarations for private implementation - defined in time_darwin.c */
__kernel_old_time_t time_impl(__kernel_old_time_t *tloc);
int gettimeofday_impl(struct kernel_timeval *tv, struct kernel_timezone *tz);
int settimeofday_impl(const struct kernel_timeval *tv, const struct kernel_timezone *tz);
int clock_gettime_impl(__kernel_clockid_t clk_id, struct kernel_timespec *tp);
int clock_getres_impl(__kernel_clockid_t clk_id, struct kernel_timespec *res);
int clock_settime_impl(__kernel_clockid_t clk_id, const struct kernel_timespec *tp);
unsigned int sleep_impl(unsigned int seconds);
int usleep_impl(__u32 usec);
int setitimer_impl(int which, const struct kernel_itimerval *new_value, struct kernel_itimerval *old_value);
int getitimer_impl(int which, struct kernel_itimerval *curr_value);
unsigned int alarm_impl(unsigned int seconds);

int nanosleep_impl(const struct kernel_timespec *req, struct kernel_timespec *rem) {
    uint64_t total_ms;
    int ret;

    if (!req) {
        errno = EFAULT;
        return -1;
    }
    if (req->tv_sec < 0 || req->tv_nsec < 0 || req->tv_nsec >= 1000000000L) {
        errno = EINVAL;
        return -1;
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
        errno = EINTR;
        return -1;
    }

    ret = wait_queue_sleep_ms((int)total_ms);
    if (ret == -EINTR) {
        if (rem) {
            *rem = *req;
        }
        task_restart_record_impl(get_current(), TASK_RESTART_NANOSLEEP,
                                 (uint64_t)(uintptr_t)req, (uint64_t)(uintptr_t)rem,
                                 0, 0, 0, 0);
        errno = EINTR;
        return -1;
    }
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    if (rem) {
        rem->tv_sec = 0;
        rem->tv_nsec = 0;
    }
    return 0;
}

int linux_realtime_now_impl(struct __kernel_timespec *tp) {
    struct kernel_timespec backing_ts;

    if (!tp) {
        errno = EFAULT;
        return -1;
    }
    if (clock_gettime_impl(CLOCK_REALTIME, &backing_ts) != 0) {
        return -1;
    }
    tp->tv_sec = (__kernel_time64_t)backing_ts.tv_sec;
    tp->tv_nsec = (long long)backing_ts.tv_nsec;
    return 0;
}

int interval_timer_get_impl(int which, struct kernel_itimerval *curr_value) {
    if (which != ITIMER_REAL && which != ITIMER_VIRTUAL && which != ITIMER_PROF) {
        errno = EINVAL;
        return -1;
    }
    if (!curr_value) {
        errno = EFAULT;
        return -1;
    }
    memset(curr_value, 0, sizeof(*curr_value));
    return 0;
}

int interval_timer_set_impl(int which, const struct kernel_itimerval *new_value,
                            struct kernel_itimerval *old_value) {
    if (which != ITIMER_REAL && which != ITIMER_VIRTUAL && which != ITIMER_PROF) {
        errno = EINVAL;
        return -1;
    }
    if (!new_value) {
        errno = EFAULT;
        return -1;
    }
    if (new_value->it_interval.tv_sec < 0 || new_value->it_value.tv_sec < 0 ||
        new_value->it_interval.tv_usec < 0 || new_value->it_interval.tv_usec >= 1000000 ||
        new_value->it_value.tv_usec < 0 || new_value->it_value.tv_usec >= 1000000) {
        errno = EINVAL;
        return -1;
    }
    if (old_value && interval_timer_get_impl(which, old_value) != 0) {
        return -1;
    }
    if (new_value->it_interval.tv_sec != 0 || new_value->it_interval.tv_usec != 0 ||
        new_value->it_value.tv_sec != 0 || new_value->it_value.tv_usec != 0) {
        errno = ENOSYS;
        return -1;
    }
    return 0;
}

/* ============================================================================
 * PUBLIC SYSCALL WRAPPERS
 * These export the canonical Linux/POSIX interface
 * ============================================================================ */

__attribute__((visibility("default"))) __kernel_old_time_t time(__kernel_old_time_t *tloc) {
    return time_impl(tloc);
}

__attribute__((visibility("default"))) int gettimeofday(struct timeval *tv, void *tz) {
    struct kernel_timeval kernel_tv;
    struct kernel_timezone kernel_tz;
    struct kernel_timezone *kernel_tz_ptr = NULL;

    if (tz) {
        kernel_tz_ptr = &kernel_tz;
    }
    if (gettimeofday_impl(tv ? &kernel_tv : NULL, kernel_tz_ptr) != 0) {
        return -1;
    }
    if (tv) {
        tv->tv_sec = kernel_tv.tv_sec;
        tv->tv_usec = (__typeof__(tv->tv_usec))kernel_tv.tv_usec;
    }
    if (tz) {
        struct timezone *public_tz = (struct timezone *)tz;
        public_tz->tz_minuteswest = kernel_tz.tz_minuteswest;
        public_tz->tz_dsttime = kernel_tz.tz_dsttime;
    }
    return 0;
}

__attribute__((visibility("default"))) int settimeofday(const struct timeval *tv, const struct timezone *tz) {
    struct kernel_timeval kernel_tv;
    struct kernel_timezone kernel_tz;
    const struct kernel_timeval *kernel_tv_ptr = NULL;
    const struct kernel_timezone *kernel_tz_ptr = NULL;

    if (tv) {
        kernel_tv.tv_sec = tv->tv_sec;
        kernel_tv.tv_usec = tv->tv_usec;
        kernel_tv_ptr = &kernel_tv;
    }
    if (tz) {
        kernel_tz.tz_minuteswest = tz->tz_minuteswest;
        kernel_tz.tz_dsttime = tz->tz_dsttime;
        kernel_tz_ptr = &kernel_tz;
    }
    return settimeofday_impl(kernel_tv_ptr, kernel_tz_ptr);
}

__attribute__((visibility("default"))) int clock_gettime(clockid_t clk_id, struct timespec *tp) {
    struct kernel_timespec kernel_tp;

    if (!tp) {
        errno = EFAULT;
        return -1;
    }
    if (clock_gettime_impl((__kernel_clockid_t)clk_id, &kernel_tp) != 0) {
        return -1;
    }
    tp->tv_sec = kernel_tp.tv_sec;
    tp->tv_nsec = kernel_tp.tv_nsec;
    return 0;
}

__attribute__((visibility("default"))) int clock_getres(clockid_t clk_id, struct timespec *res) {
    struct kernel_timespec kernel_res;

    if (!res) {
        errno = EFAULT;
        return -1;
    }
    if (clock_getres_impl((__kernel_clockid_t)clk_id, &kernel_res) != 0) {
        return -1;
    }
    res->tv_sec = kernel_res.tv_sec;
    res->tv_nsec = kernel_res.tv_nsec;
    return 0;
}

__attribute__((visibility("default"))) int clock_settime(clockid_t clk_id, const struct timespec *tp) {
    struct kernel_timespec kernel_tp;

    if (!tp) {
        errno = EFAULT;
        return -1;
    }
    kernel_tp.tv_sec = tp->tv_sec;
    kernel_tp.tv_nsec = tp->tv_nsec;
    return clock_settime_impl((__kernel_clockid_t)clk_id, &kernel_tp);
}

__attribute__((visibility("default"))) unsigned int sleep(unsigned int seconds) {
    return sleep_impl(seconds);
}

__attribute__((visibility("default"))) int usleep(__u32 usec) {
    return usleep_impl(usec);
}

__attribute__((visibility("default"))) int nanosleep(const struct timespec *req, struct timespec *rem) {
    struct kernel_timespec kernel_req;
    struct kernel_timespec kernel_rem;

    if (!req) {
        errno = EFAULT;
        return -1;
    }
    kernel_req.tv_sec = req->tv_sec;
    kernel_req.tv_nsec = req->tv_nsec;
    if (nanosleep_impl(&kernel_req, rem ? &kernel_rem : NULL) != 0) {
        if (rem) {
            rem->tv_sec = kernel_rem.tv_sec;
            rem->tv_nsec = kernel_rem.tv_nsec;
        }
        return -1;
    }
    if (rem) {
        rem->tv_sec = 0;
        rem->tv_nsec = 0;
    }
    return 0;
}

__attribute__((visibility("default"))) int setitimer(int which, const struct itimerval *new_value, struct itimerval *old_value) {
    struct kernel_itimerval kernel_new;
    struct kernel_itimerval kernel_old;
    const struct kernel_itimerval *kernel_new_ptr = NULL;

    if (new_value) {
        kernel_new.it_interval.tv_sec = new_value->it_interval.tv_sec;
        kernel_new.it_interval.tv_usec = new_value->it_interval.tv_usec;
        kernel_new.it_value.tv_sec = new_value->it_value.tv_sec;
        kernel_new.it_value.tv_usec = new_value->it_value.tv_usec;
        kernel_new_ptr = &kernel_new;
    }
    if (interval_timer_set_impl(which, kernel_new_ptr, old_value ? &kernel_old : NULL) != 0) {
        return -1;
    }
    if (old_value) {
        old_value->it_interval.tv_sec = kernel_old.it_interval.tv_sec;
        old_value->it_interval.tv_usec = (__typeof__(old_value->it_interval.tv_usec))kernel_old.it_interval.tv_usec;
        old_value->it_value.tv_sec = kernel_old.it_value.tv_sec;
        old_value->it_value.tv_usec = (__typeof__(old_value->it_value.tv_usec))kernel_old.it_value.tv_usec;
    }
    return 0;
}

__attribute__((visibility("default"))) int getitimer(int which, struct itimerval *curr_value) {
    struct kernel_itimerval kernel_value;

    if (!curr_value) {
        errno = EFAULT;
        return -1;
    }
    if (interval_timer_get_impl(which, &kernel_value) != 0) {
        return -1;
    }
    curr_value->it_interval.tv_sec = kernel_value.it_interval.tv_sec;
    curr_value->it_interval.tv_usec = (__typeof__(curr_value->it_interval.tv_usec))kernel_value.it_interval.tv_usec;
    curr_value->it_value.tv_sec = kernel_value.it_value.tv_sec;
    curr_value->it_value.tv_usec = (__typeof__(curr_value->it_value.tv_usec))kernel_value.it_value.tv_usec;
    return 0;
}

__attribute__((visibility("default"))) unsigned int alarm(unsigned int seconds) {
    return alarm_impl(seconds);
}
