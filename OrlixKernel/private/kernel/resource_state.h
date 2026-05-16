#ifndef PRIVATE_KERNEL_RESOURCE_STATE_H
#define PRIVATE_KERNEL_RESOURCE_STATE_H

#include <linux/resource.h>
#include <linux/times.h>

#ifdef __cplusplus
extern "C" {
#endif

long times_impl(struct tms *buf);
int getrusage_impl(int who, struct rusage *usage);

#ifdef __cplusplus
}
#endif

#endif
