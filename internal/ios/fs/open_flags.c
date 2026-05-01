#include <linux/fcntl.h>

int host_translate_open_flags_impl(int flags,
                                   int host_rdonly,
                                   int host_wronly,
                                   int host_rdwr,
                                   int host_creat,
                                   int host_excl,
                                   int host_trunc,
                                   int host_append,
                                   int host_nonblock,
                                   int host_directory,
                                   int host_nofollow) {
    int host_flags = 0;

    switch (flags & O_ACCMODE) {
    case O_WRONLY:
        host_flags |= host_wronly;
        break;
    case O_RDWR:
        host_flags |= host_rdwr;
        break;
    default:
        host_flags |= host_rdonly;
        break;
    }

    if ((flags & O_CREAT) != 0) {
        host_flags |= host_creat;
    }
    if ((flags & O_EXCL) != 0) {
        host_flags |= host_excl;
    }
    if ((flags & O_TRUNC) != 0) {
        host_flags |= host_trunc;
    }
    if ((flags & O_APPEND) != 0) {
        host_flags |= host_append;
    }
    if ((flags & O_NONBLOCK) != 0) {
        host_flags |= host_nonblock;
    }
    if ((flags & O_DIRECTORY) != 0) {
        host_flags |= host_directory;
    }
    if ((flags & O_NOFOLLOW) != 0) {
        host_flags |= host_nofollow;
    }

    return host_flags;
}
