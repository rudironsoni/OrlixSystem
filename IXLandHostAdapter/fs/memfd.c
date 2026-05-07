#include "backing_memfd.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "backing_io_internal.h"
#include "backing_path.h"
#include "backing_roots.h"

static int try_memfd_root(int (*discover_root)(char *path, size_t path_len),
                          pid_t pid) {
    char root[4096];
    char path[4096];

    if (discover_root(root, sizeof(root)) != 0) {
        return -1;
    }
    if (backing_ensure_directory(root, 0700) != 0) {
        return -1;
    }

    for (unsigned int attempt = 0; attempt < 64; attempt++) {
        int fd;
        int ret = snprintf(path, sizeof(path), "%s%sixland-memfd.%d.%u.XXXXXX",
                           root,
                           root[0] != '\0' && root[strlen(root) - 1] == '/' ? "" : "/",
                           (int)pid, attempt);
        if (ret < 0 || (size_t)ret >= sizeof(path)) {
            errno = ENAMETOOLONG;
            return -1;
        }

        fd = mkstemp(path);
        if (fd < 0) {
            if (errno == EEXIST) {
                continue;
            }
            return -1;
        }
        (void)backing_unlink(path);
        if (backing_fcntl(fd, F_SETFD, FD_CLOEXEC) != 0) {
            int saved_errno = errno;
            backing_close(fd);
            errno = saved_errno;
            return -1;
        }
        return fd;
    }

    errno = EEXIST;
    return -1;
}

int backing_memfd_create(void) {
    pid_t pid = getpid();
    int saved_errno = ENOENT;
    int fd;

    fd = try_memfd_root(backing_root_discover_temp, pid);
    if (fd >= 0) {
        return fd;
    }
    saved_errno = errno;

    fd = try_memfd_root(backing_root_discover_cache, pid);
    if (fd >= 0) {
        return fd;
    }
    if (errno != ENOENT) {
        saved_errno = errno;
    }

    fd = try_memfd_root(backing_root_discover_persistent, pid);
    if (fd >= 0) {
        return fd;
    }
    if (errno != ENOENT) {
        saved_errno = errno;
    }

    errno = saved_errno;
    return -1;
}
