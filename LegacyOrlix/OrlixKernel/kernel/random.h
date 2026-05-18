#ifndef KERNEL_RANDOM_H
#define KERNEL_RANDOM_H

#include <linux/types.h>

#ifdef __cplusplus
extern "C" {
#endif

__kernel_ssize_t getrandom_impl(void *buf, size_t buflen, unsigned int flags);
int getentropy_impl(void *buffer, size_t length);

#ifdef __cplusplus
}
#endif

#endif
