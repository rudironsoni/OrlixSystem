// SPDX-License-Identifier: GPL-2.0

#ifndef ORLIXOS_CHECKPOLICY_HOST_COMPAT_H
#define ORLIXOS_CHECKPOLICY_HOST_COMPAT_H

#include <arpa/inet.h>
#include <libkern/OSByteOrder.h>

#ifndef be32toh
#define be32toh(value) OSSwapBigToHostInt32(value)
#endif

#ifndef htobe32
#define htobe32(value) OSSwapHostToBigInt32(value)
#endif

#ifndef s6_addr32
#define s6_addr32 __u6_addr.__u6_addr32
#endif

#endif
