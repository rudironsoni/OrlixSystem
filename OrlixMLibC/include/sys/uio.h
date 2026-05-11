#ifndef ORLIX_MLIBC_SYS_UIO_H
#define ORLIX_MLIBC_SYS_UIO_H

#include <sys/types.h>

struct iovec {
    void *iov_base;
    size_t iov_len;
};

#define UIO_FASTIOV 8
#define UIO_MAXIOV 1024

#endif
