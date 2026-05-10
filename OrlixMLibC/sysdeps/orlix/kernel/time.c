#include <errno.h>
#include <stddef.h>
#include <sys/time.h>
#include <time.h>

#include <linux/time_types.h>

extern __kernel_old_time_t time_impl(__kernel_old_time_t *tloc);
extern int gettimeofday_impl(struct __kernel_old_timeval *tv, void *tz);
extern int settimeofday_impl(const struct __kernel_old_timeval *tv, const void *tz);
extern int clock_gettime_impl(__kernel_clockid_t clk_id, struct __kernel_timespec *tp);
extern int clock_getres_impl(__kernel_clockid_t clk_id, struct __kernel_timespec *res);
extern int clock_settime_impl(__kernel_clockid_t clk_id, const struct __kernel_timespec *tp);
extern unsigned int sleep_impl(unsigned int seconds);
extern int usleep_impl(__u32 usec);
extern int nanosleep_impl(const struct __kernel_timespec *req, struct __kernel_timespec *rem);
extern int interval_timer_set_impl(int which, const struct __kernel_old_itimerval *new_value,
                                   struct __kernel_old_itimerval *old_value);
extern int interval_timer_get_impl(int which, struct __kernel_old_itimerval *curr_value);
extern unsigned int alarm_impl(unsigned int seconds);

static int wrap_int_result(int ret) {
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}

__attribute__((visibility("default"))) __kernel_old_time_t time(__kernel_old_time_t *tloc) {
    return time_impl(tloc);
}

__attribute__((visibility("default"))) int gettimeofday(struct timeval *tv, void *tz) {
    struct __kernel_old_timeval kernel_tv;
    struct timezone kernel_tz;
    struct timezone *kernel_tz_ptr = NULL;
    int ret;

    if (tz) {
        kernel_tz_ptr = &kernel_tz;
    }
    ret = gettimeofday_impl(tv ? &kernel_tv : NULL, kernel_tz_ptr);
    if (ret < 0) {
        errno = -ret;
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
    struct __kernel_old_timeval kernel_tv;
    struct timezone kernel_tz;
    const struct __kernel_old_timeval *kernel_tv_ptr = NULL;
    const struct timezone *kernel_tz_ptr = NULL;

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
    return wrap_int_result(settimeofday_impl(kernel_tv_ptr, kernel_tz_ptr));
}

__attribute__((visibility("default"))) int clock_gettime(clockid_t clk_id, struct timespec *tp) {
    struct __kernel_timespec kernel_tp;
    int ret;

    if (!tp) {
        errno = EFAULT;
        return -1;
    }
    ret = clock_gettime_impl((__kernel_clockid_t)clk_id, &kernel_tp);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    tp->tv_sec = kernel_tp.tv_sec;
    tp->tv_nsec = kernel_tp.tv_nsec;
    return 0;
}

__attribute__((visibility("default"))) int clock_getres(clockid_t clk_id, struct timespec *res) {
    struct __kernel_timespec kernel_res;
    int ret;

    if (!res) {
        errno = EFAULT;
        return -1;
    }
    ret = clock_getres_impl((__kernel_clockid_t)clk_id, &kernel_res);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    res->tv_sec = kernel_res.tv_sec;
    res->tv_nsec = kernel_res.tv_nsec;
    return 0;
}

__attribute__((visibility("default"))) int clock_settime(clockid_t clk_id, const struct timespec *tp) {
    struct __kernel_timespec kernel_tp;

    if (!tp) {
        errno = EFAULT;
        return -1;
    }
    kernel_tp.tv_sec = tp->tv_sec;
    kernel_tp.tv_nsec = tp->tv_nsec;
    return wrap_int_result(clock_settime_impl((__kernel_clockid_t)clk_id, &kernel_tp));
}

__attribute__((visibility("default"))) unsigned int sleep(unsigned int seconds) {
    return sleep_impl(seconds);
}

__attribute__((visibility("default"))) int usleep(__u32 usec) {
    return wrap_int_result(usleep_impl(usec));
}

__attribute__((visibility("default"))) int nanosleep(const struct timespec *req, struct timespec *rem) {
    struct __kernel_timespec kernel_req;
    struct __kernel_timespec kernel_rem;
    int ret;

    if (!req) {
        errno = EFAULT;
        return -1;
    }
    kernel_req.tv_sec = req->tv_sec;
    kernel_req.tv_nsec = req->tv_nsec;
    ret = nanosleep_impl(&kernel_req, rem ? &kernel_rem : NULL);
    if (ret < 0) {
        if (rem) {
            rem->tv_sec = kernel_rem.tv_sec;
            rem->tv_nsec = kernel_rem.tv_nsec;
        }
        errno = -ret;
        return -1;
    }
    if (rem) {
        rem->tv_sec = 0;
        rem->tv_nsec = 0;
    }
    return 0;
}

__attribute__((visibility("default"))) int setitimer(int which, const struct itimerval *new_value,
                                                     struct itimerval *old_value) {
    struct __kernel_old_itimerval kernel_new;
    struct __kernel_old_itimerval kernel_old;
    const struct __kernel_old_itimerval *kernel_new_ptr = NULL;
    int ret;

    if (new_value) {
        kernel_new.it_interval.tv_sec = new_value->it_interval.tv_sec;
        kernel_new.it_interval.tv_usec = new_value->it_interval.tv_usec;
        kernel_new.it_value.tv_sec = new_value->it_value.tv_sec;
        kernel_new.it_value.tv_usec = new_value->it_value.tv_usec;
        kernel_new_ptr = &kernel_new;
    }
    ret = interval_timer_set_impl(which, kernel_new_ptr, old_value ? &kernel_old : NULL);
    if (ret < 0) {
        errno = -ret;
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
    struct __kernel_old_itimerval kernel_value;
    int ret;

    if (!curr_value) {
        errno = EFAULT;
        return -1;
    }
    ret = interval_timer_get_impl(which, &kernel_value);
    if (ret < 0) {
        errno = -ret;
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
