#ifndef ORLIX_MLIBC_TIME_H
#define ORLIX_MLIBC_TIME_H

#if !defined(_TIME_H_)
#define ORLIX_MLIBC_DEFINE_CLOCKID_T 1
#endif

#ifndef _TIME_H_
#define _TIME_H_
#endif

#include "orlixmlibc/bits/alltypes.h"
#include <linux/time.h>

#if defined(ORLIX_MLIBC_DEFINE_CLOCKID_T) && !defined(ORLIX_MLIBC_CLOCKID_T)
#define ORLIX_MLIBC_CLOCKID_T
typedef int clockid_t;
#endif

#endif
