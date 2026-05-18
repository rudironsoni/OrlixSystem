/* OrlixKernel - IOCTL Operations
 *
 * Canonical owner for ioctl:
 * - ioctl()
 *
 * Linux-shaped canonical owner - iOS mediation as implementation detail
 */

#include <asm/ioctls.h>
#include <linux/errno.h>
#include <linux/types.h>

#include "fdtable.h"
#include "private/fs/fdtable_state.h"
#include "private/fs/pty_state.h"
#include "internal/fs/ioctl.h"

int ioctl_impl(int fd, unsigned long request, void *arg) {
    if (fd < 0 || fd >= NR_OPEN_DEFAULT) {
        return -EBADF;
    }

    if (fd <= 2) {
        return backing_ioctl(fd, request, arg);
    }

    void *entry = get_fd_entry_impl(fd);
    if (!entry) {
        return -EBADF;
    }

    if (get_fd_is_synthetic_pty_impl(entry)) {
        unsigned int pty_index = get_fd_synthetic_pty_index_impl(entry);
        bool is_master = get_fd_is_synthetic_pty_master_impl(entry);
        int result = -1;

        switch (request) {
        case TIOCGPTN:
            if (!is_master || !arg) {
                result = !arg ? -EFAULT : -EINVAL;
                break;
            }
            *(unsigned int *)arg = pty_index;
            result = 0;
            break;
        case TIOCSPTLCK:
            if (!is_master || !arg) {
                result = !arg ? -EFAULT : -EINVAL;
                break;
            }
            result = pty_set_lock_impl(pty_index, (*(const int *)arg) != 0);
            break;
        case TCGETS:
            if (!arg) {
                result = -EFAULT;
                break;
            }
            result = pty_get_termios_impl(pty_index, (struct termios *)arg);
            break;
        case TCSETS:
            if (!arg) {
                result = -EFAULT;
                break;
            }
            result = pty_set_termios_with_action_impl(pty_index, (const struct termios *)arg, TCSANOW);
            break;
        case TCSETSW:
            if (!arg) {
                result = -EFAULT;
                break;
            }
            result = pty_set_termios_with_action_impl(pty_index, (const struct termios *)arg, TCSADRAIN);
            break;
        case TCSETSF:
            if (!arg) {
                result = -EFAULT;
                break;
            }
            result = pty_set_termios_with_action_impl(pty_index, (const struct termios *)arg, TCSAFLUSH);
            break;
        case TIOCGWINSZ:
            if (!arg) {
                result = -EFAULT;
                break;
            }
            result = pty_get_winsize_impl(pty_index, (struct winsize *)arg);
            break;
        case TIOCSWINSZ:
            if (!arg) {
                result = -EFAULT;
                break;
            }
            result = pty_set_winsize_impl(pty_index, (const struct winsize *)arg);
            break;
        case TIOCSCTTY:
            result = pty_set_controlling_tty_impl(pty_index, (int)(intptr_t)arg);
            break;
        case TIOCNOTTY:
            result = pty_detach_controlling_tty_impl();
            break;
        case TIOCGPGRP:
            if (!arg) {
                result = -EFAULT;
                break;
            }
            result = pty_get_foreground_pgrp_impl(pty_index, (int32_t *)arg);
            break;
        case TIOCGSID:
            if (!arg) {
                result = -EFAULT;
                break;
            }
            result = pty_get_controlling_sid_impl(pty_index, (int32_t *)arg);
            break;
        case TIOCSPGRP:
            if (!arg) {
                result = -EFAULT;
                break;
            }
            result = pty_set_foreground_pgrp_impl(pty_index, *(const int32_t *)arg);
            break;
        case FIONREAD: {
            if (!arg) {
                result = -EFAULT;
                break;
            }
            result = pty_get_readable_bytes_impl(pty_index, is_master, (int *)arg);
            break;
        }
        default:
            result = -ENOTTY;
            break;
        }

        put_fd_entry_impl(entry);
        return result;
    }

    int result;
    if (get_fd_is_synthetic_dev_impl(entry) || get_fd_is_synthetic_dir_impl(entry) ||
        get_fd_is_synthetic_proc_file_impl(entry)) {
        put_fd_entry_impl(entry);
        return -ENOTTY;
    }

    result = backing_ioctl(get_real_fd_impl(entry), request, arg);
    put_fd_entry_impl(entry);
    return result;
}
