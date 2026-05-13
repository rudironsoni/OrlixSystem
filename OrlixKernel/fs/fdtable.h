#ifndef FDTABLE_H
#define FDTABLE_H

#include <linux/types.h>

#define NR_OPEN_DEFAULT 256

#ifdef __cplusplus
extern "C" {
#endif

struct __kernel_itimerspec;
int eventfd2_impl(unsigned int initval, int flags);
int timerfd_create_impl(int clockid, int flags);
int timerfd_settime_impl(int fd, int flags, const struct __kernel_itimerspec *new_value,
                         struct __kernel_itimerspec *old_value);
int timerfd_gettime_impl(int fd, struct __kernel_itimerspec *curr_value);
int memfd_create_impl(const char *name, unsigned int flags);
int pidfd_open_impl(int32_t pid, unsigned int flags);

/* Close implementation using static fd table */
int close_impl(int fd);
int close_range_impl(unsigned int first, unsigned int last, unsigned int flags);

#ifdef __cplusplus
}
#endif

#endif
