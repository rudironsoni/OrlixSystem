#include "errno_host.h"

#include <errno.h>

#define LINUX_EPERM         1
#define LINUX_ENOENT        2
#define LINUX_EIO           5
#define LINUX_EACCES       13
#define LINUX_EFAULT       14
#define LINUX_EEXIST       17
#define LINUX_ENODEV       19
#define LINUX_ENOTDIR      20
#define LINUX_EISDIR       21
#define LINUX_EINVAL       22
#define LINUX_ENFILE       23
#define LINUX_EMFILE       24
#define LINUX_ENOSPC       28
#define LINUX_ESPIPE       29
#define LINUX_EROFS        30
#define LINUX_ENAMETOOLONG 36
#define LINUX_ENOSYS       38
#define LINUX_ENOTEMPTY    39
#define LINUX_ELOOP        40
#define LINUX_EBADF        9
#define LINUX_EINTR        4
#define LINUX_EAGAIN       11
#define LINUX_ENOMEM       12
#define LINUX_EBUSY        16
#define LINUX_EPIPE        32
#define LINUX_ENOTSUP      95

int host_errno_to_linux_errno(int host_errno) {
    switch (host_errno) {
    case EPERM: return LINUX_EPERM;
    case ENOENT: return LINUX_ENOENT;
    case EINTR: return LINUX_EINTR;
    case EIO: return LINUX_EIO;
    case EBADF: return LINUX_EBADF;
    case EAGAIN: return LINUX_EAGAIN;
    case EACCES: return LINUX_EACCES;
    case EFAULT: return LINUX_EFAULT;
    case EBUSY: return LINUX_EBUSY;
    case EEXIST: return LINUX_EEXIST;
    case ENODEV: return LINUX_ENODEV;
    case ENOTDIR: return LINUX_ENOTDIR;
    case EISDIR: return LINUX_EISDIR;
    case EINVAL: return LINUX_EINVAL;
    case ENFILE: return LINUX_ENFILE;
    case EMFILE: return LINUX_EMFILE;
    case ENOMEM: return LINUX_ENOMEM;
    case ENOSPC: return LINUX_ENOSPC;
    case ESPIPE: return LINUX_ESPIPE;
    case EROFS: return LINUX_EROFS;
    case EPIPE: return LINUX_EPIPE;
    case ENAMETOOLONG: return LINUX_ENAMETOOLONG;
    case ENOSYS: return LINUX_ENOSYS;
    case ENOTEMPTY: return LINUX_ENOTEMPTY;
    case ELOOP: return LINUX_ELOOP;
    case EOPNOTSUPP: return LINUX_ENOTSUP;
    default: return LINUX_EIO;
    }
}
