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
#include <time.h>
#include <sys/time.h>
#include <unistd.h>

#include "task.h"
#include "signal.h"
#include "wait_queue.h"

/* Forward declarations for private implementation - defined in time_darwin.c */
time_t time_impl(time_t *tloc);
int gettimeofday_impl(struct timeval *tv, void *tz);
int settimeofday_impl(const struct timeval *tv, const struct timezone *tz);
int clock_gettime_impl(clockid_t clk_id, struct timespec *tp);
int clock_getres_impl(clockid_t clk_id, struct timespec *res);
int clock_settime_impl(clockid_t clk_id, const struct timespec *tp);
unsigned int sleep_impl(unsigned int seconds);
int usleep_impl(useconds_t usec);
int setitimer_impl(int which, const struct itimerval *new_value, struct itimerval *old_value);
int getitimer_impl(int which, struct itimerval *curr_value);
unsigned int alarm_impl(unsigned int seconds);

int nanosleep_impl(const struct timespec *req, struct timespec *rem) {
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

/* ============================================================================
 * PUBLIC SYSCALL WRAPPERS
 * These export the canonical Linux/POSIX interface
 * ============================================================================ */

__attribute__((visibility("default"))) time_t time(time_t *tloc) {
    return time_impl(tloc);
}

__attribute__((visibility("default"))) int gettimeofday(struct timeval *tv, void *tz) {
    return gettimeofday_impl(tv, tz);
}

__attribute__((visibility("default"))) int settimeofday(const struct timeval *tv, const struct timezone *tz) {
    return settimeofday_impl(tv, tz);
}

__attribute__((visibility("default"))) int clock_gettime(clockid_t clk_id, struct timespec *tp) {
    return clock_gettime_impl(clk_id, tp);
}

__attribute__((visibility("default"))) int clock_getres(clockid_t clk_id, struct timespec *res) {
    return clock_getres_impl(clk_id, res);
}

__attribute__((visibility("default"))) int clock_settime(clockid_t clk_id, const struct timespec *tp) {
    return clock_settime_impl(clk_id, tp);
}

__attribute__((visibility("default"))) unsigned int sleep(unsigned int seconds) {
    return sleep_impl(seconds);
}

__attribute__((visibility("default"))) int usleep(useconds_t usec) {
    return usleep_impl(usec);
}

__attribute__((visibility("default"))) int nanosleep(const struct timespec *req, struct timespec *rem) {
    return nanosleep_impl(req, rem);
}

__attribute__((visibility("default"))) int setitimer(int which, const struct itimerval *new_value, struct itimerval *old_value) {
    return setitimer_impl(which, new_value, old_value);
}

__attribute__((visibility("default"))) int getitimer(int which, struct itimerval *curr_value) {
    return getitimer_impl(which, curr_value);
}

__attribute__((visibility("default"))) unsigned int alarm(unsigned int seconds) {
    return alarm_impl(seconds);
}
