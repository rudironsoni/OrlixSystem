#ifndef ORLIX_INTERNAL_PRIVATE_KERNEL_SELECT_COMPAT_H
#define ORLIX_INTERNAL_PRIVATE_KERNEL_SELECT_COMPAT_H

#include "kernel_time_compat.h"

#ifndef FD_SETSIZE
#define FD_SETSIZE 1024
#endif

#define ORLIX_KERNEL_NFDBITS ((int)(8U * sizeof(unsigned long)))
#define ORLIX_KERNEL_FDSET_WORDS ((FD_SETSIZE + ORLIX_KERNEL_NFDBITS - 1) / ORLIX_KERNEL_NFDBITS)

typedef struct fd_set {
    unsigned long fds_bits[ORLIX_KERNEL_FDSET_WORDS];
} fd_set;

#ifndef FD_ZERO
#define FD_ZERO(set) orlix_kernel_fd_zero((set))
#endif
#ifndef FD_SET
#define FD_SET(fd, set) orlix_kernel_fd_set((fd), (set))
#endif
#ifndef FD_CLR
#define FD_CLR(fd, set) orlix_kernel_fd_clr((fd), (set))
#endif
#ifndef FD_ISSET
#define FD_ISSET(fd, set) orlix_kernel_fd_isset((fd), (set))
#endif

static inline void orlix_kernel_fd_zero(fd_set *set) {
    int i;

    if (!set) {
        return;
    }
    for (i = 0; i < ORLIX_KERNEL_FDSET_WORDS; i++) {
        set->fds_bits[i] = 0;
    }
}

static inline void orlix_kernel_fd_set(int fd, fd_set *set) {
    if (!set || fd < 0 || fd >= FD_SETSIZE) {
        return;
    }
    set->fds_bits[fd / ORLIX_KERNEL_NFDBITS] |= (1UL << (fd % ORLIX_KERNEL_NFDBITS));
}

static inline void orlix_kernel_fd_clr(int fd, fd_set *set) {
    if (!set || fd < 0 || fd >= FD_SETSIZE) {
        return;
    }
    set->fds_bits[fd / ORLIX_KERNEL_NFDBITS] &= ~(1UL << (fd % ORLIX_KERNEL_NFDBITS));
}

static inline int orlix_kernel_fd_isset(int fd, const fd_set *set) {
    if (!set || fd < 0 || fd >= FD_SETSIZE) {
        return 0;
    }
    return (set->fds_bits[fd / ORLIX_KERNEL_NFDBITS] & (1UL << (fd % ORLIX_KERNEL_NFDBITS))) != 0;
}

#endif
