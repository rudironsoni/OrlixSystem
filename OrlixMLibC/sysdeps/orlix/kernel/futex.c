#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include <linux/futex.h>
#include <linux/time_types.h>

extern int futex_op_impl(int *uaddr, int futex_op, int val, int timeout_ms, int *uaddr2, int val3);
extern int set_robust_list_impl(void *head, unsigned long len);
extern int get_robust_list_impl(int pid, void **head, unsigned long *len);

static int futex_timeout_ms(const struct __kernel_timespec *timeout) {
    int64_t ms;

    if (!timeout) {
        return -1;
    }
    if (timeout->tv_sec < 0 || timeout->tv_nsec < 0 || timeout->tv_nsec >= 1000000000L) {
        return -EINVAL;
    }
    if (timeout->tv_sec > (INT64_MAX / 1000)) {
        return INT_MAX;
    }
    ms = (int64_t)timeout->tv_sec * 1000;
    ms += (timeout->tv_nsec + 999999L) / 1000000L;
    if (ms > INT_MAX) {
        return INT_MAX;
    }
    return (int)ms;
}

static int wrap_int_result(int ret) {
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}

__attribute__((visibility("default"))) int futex(int *uaddr, int futex_op, int val,
                                                 const struct timespec *timeout,
                                                 int *uaddr2, int val3) {
    struct __kernel_timespec kernel_timeout;
    const struct __kernel_timespec *kernel_timeout_ptr = NULL;
    int timeout_ms;

    if (timeout) {
        kernel_timeout.tv_sec = timeout->tv_sec;
        kernel_timeout.tv_nsec = timeout->tv_nsec;
        kernel_timeout_ptr = &kernel_timeout;
    }
    timeout_ms = futex_timeout_ms(kernel_timeout_ptr);
    if (timeout_ms < -1) {
        errno = -timeout_ms;
        return -1;
    }
    return wrap_int_result(futex_op_impl(uaddr, futex_op, val, timeout_ms, uaddr2, val3));
}

__attribute__((visibility("default"))) int set_robust_list(void *head, unsigned long len) {
    return wrap_int_result(set_robust_list_impl(head, len));
}

__attribute__((visibility("default"))) int get_robust_list(int pid, void **head, unsigned long *len) {
    return wrap_int_result(get_robust_list_impl(pid, head, len));
}
