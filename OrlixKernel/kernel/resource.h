#ifndef KERNEL_RESOURCE_H
#define KERNEL_RESOURCE_H

#include <linux/resource.h>
#include <uapi/linux/times.h>

int getrlimit_impl(int resource, struct rlimit *rlim);
int setrlimit_impl(int resource, const struct rlimit *rlim);
long times_impl(struct tms *buf);
int getrusage_impl(int who, struct rusage *usage);
int prlimit_impl(int32_t pid, int resource, const struct rlimit *new_limit, struct rlimit *old_limit);

#endif
