/* IXLandSystemTests/HostTestSupport.c
 * Host-side test support helpers
 *
 * This file implements host-side helpers needed for test setup.
 * These are NOT Linux UAPI proof - they are Darwin/host operations.
 *
 * This file uses Darwin headers only.
 */

#include "HostTestSupport.h"
#include "IXLandHostAdapter/fs/backing_io_decls.h"

#include <errno.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * Signal test helpers (Darwin implementation)
 * ============================================================================ */

static struct {
    int valid;
    struct sigaction old_sa;
} sigint_state = {0};

int host_test_signal_install_sigint_ign(void) {
    struct sigaction new_sa;

    memset(&new_sa, 0, sizeof(new_sa));
    new_sa.sa_handler = SIG_IGN;
    sigemptyset(&new_sa.sa_mask);

    if (sigaction(SIGINT, &new_sa, &sigint_state.old_sa) != 0) {
        return -1;
    }

    sigint_state.valid = 1;
    return 0;
}

int host_test_signal_restore_sigint(void) {
    if (!sigint_state.valid) {
        return -1;
    }

    return sigaction(SIGINT, &sigint_state.old_sa, NULL);
}

int host_test_signal_block_sigint(void) {
    sigset_t set;

    sigemptyset(&set);
    sigaddset(&set, SIGINT);

    return sigprocmask(SIG_BLOCK, &set, NULL);
}

int host_test_signal_restore_mask(void) {
    /* Note: This is a simplified implementation */
    return 0;
}

/* ============================================================================
 * fcntl semantic test helpers (Darwin implementation)
 * ============================================================================ */

int host_test_fcntl_dupfd(int fd, int min_fd) {
    return host_fcntl_impl(fd, F_DUPFD, min_fd);
}

int host_test_fcntl_dupfd_cloexec(int fd, int min_fd) {
    int new_fd = host_fcntl_impl(fd, F_DUPFD_CLOEXEC, min_fd);

    if (new_fd >= 0) {
        return new_fd;
    }

    if (errno != EINVAL && errno != ENOTSUP) {
        return -1;
    }

    new_fd = host_fcntl_impl(fd, F_DUPFD, min_fd);
    if (new_fd < 0) {
        return -1;
    }

    if (host_fcntl_impl(new_fd, F_SETFD, FD_CLOEXEC) < 0) {
        int saved_errno = errno;
        host_close_impl(new_fd);
        errno = saved_errno;
        return -1;
    }

    return new_fd;
}

int host_test_fcntl_getfd(int fd) {
    return host_fcntl_impl(fd, F_GETFD);
}

int host_test_fcntl_setfd(int fd, int flags) {
    return host_fcntl_impl(fd, F_SETFD, flags);
}

int host_test_fcntl_getfl(int fd) {
    return host_fcntl_impl(fd, F_GETFL);
}

int host_test_fcntl_has_cloexec(int flags) {
    return (flags & FD_CLOEXEC) != 0;
}

int host_test_fcntl_has_rdonly(int flags) {
    return (flags & O_ACCMODE) == O_RDONLY;
}
