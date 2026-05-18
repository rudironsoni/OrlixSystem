#include <uapi/linux/fcntl.h>

int translate_open_flags(int flags,
                         int darwin_rdonly,
                         int darwin_wronly,
                         int darwin_rdwr,
                         int darwin_creat,
                         int darwin_excl,
                         int darwin_trunc,
                         int darwin_append,
                         int darwin_nonblock,
                         int darwin_directory,
                         int darwin_nofollow) {
    int darwin_flags = 0;

    switch (flags & O_ACCMODE) {
    case O_WRONLY:
        darwin_flags |= darwin_wronly;
        break;
    case O_RDWR:
        darwin_flags |= darwin_rdwr;
        break;
    default:
        darwin_flags |= darwin_rdonly;
        break;
    }

    if ((flags & O_CREAT) != 0) {
        darwin_flags |= darwin_creat;
    }
    if ((flags & O_EXCL) != 0) {
        darwin_flags |= darwin_excl;
    }
    if ((flags & O_TRUNC) != 0) {
        darwin_flags |= darwin_trunc;
    }
    if ((flags & O_APPEND) != 0) {
        darwin_flags |= darwin_append;
    }
    if ((flags & O_NONBLOCK) != 0) {
        darwin_flags |= darwin_nonblock;
    }
    if ((flags & O_DIRECTORY) != 0) {
        darwin_flags |= darwin_directory;
    }
    if ((flags & O_NOFOLLOW) != 0) {
        darwin_flags |= darwin_nofollow;
    }

    return darwin_flags;
}
