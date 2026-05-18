#ifndef INTERNAL_RANDOM_H
#define INTERNAL_RANDOM_H

#include <linux/stddef.h>

void get_random_bytes(void *buf, size_t len);

#endif
