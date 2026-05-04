#include "memfd_host.h"

#include <linux/fcntl.h>

#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "file_io_host.h"
#include "path_host.h"
#include "path_discovery_host.h"

int host_memfd_create_backing_impl(void) {
    char temp_root[4096];
    char path[4096];
    pid_t pid = getpid();

    if (vfs_discover_temp_root(temp_root, sizeof(temp_root)) != 0) {
        return -1;
    }

    for (unsigned int attempt = 0; attempt < 64; attempt++) {
        int fd;
        int ret = snprintf(path, sizeof(path), "%s%sixland-memfd.%d.%u",
                           temp_root,
                           temp_root[0] != '\0' && temp_root[strlen(temp_root) - 1] == '/' ? "" : "/",
                           (int)pid, attempt);
        if (ret < 0 || (size_t)ret >= sizeof(path)) {
            errno = ENAMETOOLONG;
            return -1;
        }

        fd = host_open_impl(path, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (fd < 0) {
            if (errno == EEXIST) {
                continue;
            }
            return -1;
        }
        (void)host_unlink_impl(path);
        if (host_fcntl_impl(fd, F_SETFD, FD_CLOEXEC) != 0) {
            int saved_errno = errno;
            host_close_impl(fd);
            errno = saved_errno;
            return -1;
        }
        return fd;
    }

    errno = EEXIST;
    return -1;
}
