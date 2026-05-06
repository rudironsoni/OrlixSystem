/* IXLandHostAdapterTests/PTYTestSupport.c
 * PTY test helpers (host / Darwin implementation)
 *
 * This implements a minimal, host-only PTY helper set for the HostBridge
 * test target. The implementation uses POSIX helpers (posix_openpt/grantpt/
 * unlockpt/ptsname) and avoids constructing host-specific paths manually.
 *
 * The helpers are intentionally conservative: they return 0 on success and -1
 * on error and avoid touching Linux UAPI ioctl constants.
 */

#include "PTYTestSupport.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <termios.h>

/* posix_openpt/grantpt/unlockpt/ptsname are used to open a master and slave
 * pair without manual path formatting. This avoids manual host path formatting
 * and is the portable host approach for PTYs on Darwin/Unix platforms. */

int host_test_pty_get_number(int master_fd, unsigned int *pty_number) {
    if (!pty_number) {
        errno = EINVAL;
        return -1;
    }

    char *slave = ptsname(master_fd);
    if (!slave) {
        return -1;
    }

    /* Try to parse trailing digits from the slave name as a best-effort pty
     * number. This is host-specific and optional for tests; if parsing fails
     * return -1 so callers can treat absence of a numeric id as non-fatal. */
    size_t len = strlen(slave);
    ssize_t i = (ssize_t)len - 1;
    while (i >= 0 && slave[i] >= '0' && slave[i] <= '9') {
        i--;
    }
    i++;
    if (i >= (ssize_t)len) {
        return -1; /* no trailing digits */
    }

    unsigned int num = 0;
    for (size_t j = i; j < len; j++) {
        num = num * 10 + (unsigned int)(slave[j] - '0');
    }

    *pty_number = num;
    return 0;
}

int host_test_pty_unlock_slave(int master_fd) {
    return (unlockpt(master_fd) == 0) ? 0 : -1;
}

int host_test_pty_open_pair(int *master_fd, int *slave_fd) {
    if (!master_fd || !slave_fd) {
        errno = EINVAL;
        return -1;
    }

    int mfd = posix_openpt(O_RDWR | O_NOCTTY);
    if (mfd < 0) {
        return -1;
    }

    if (grantpt(mfd) != 0) {
        close(mfd);
        return -1;
    }

    if (unlockpt(mfd) != 0) {
        close(mfd);
        return -1;
    }

    char *slave_name = ptsname(mfd);
    if (!slave_name) {
        close(mfd);
        return -1;
    }

    int sfd = open(slave_name, O_RDWR | O_NOCTTY);
    if (sfd < 0) {
        close(mfd);
        return -1;
    }

    *master_fd = mfd;
    *slave_fd = sfd;
    return 0;
}

int host_test_tty_disassociate(int fd) {
    /* Best-effort: create a new session to disassociate controlling tty.
     * If the platform provides a TIOCNOTTY ioctl, attempt it; otherwise rely on
     * setsid(). This is a host helper and used only by HostBridge tests. */

    (void)fd; /* fd is unused on some platforms */

#ifdef TIOCNOTTY
    if (ioctl(0, TIOCNOTTY, 0) == 0) {
        return 0;
    }
#endif

    return (setsid() != -1) ? 0 : -1;
}
