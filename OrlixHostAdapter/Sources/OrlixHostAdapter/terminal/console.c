#include "OrlixHostAdapter/terminal/console.h"
#include "OrlixHostAdapter/runtime/host_tls.h"

#include <fcntl.h>
#include <limits.h>
#include <os/lock.h>
#include <os/log.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>

#define ORLIX_HOST_CONSOLE_CHUNK_BYTES 1024UL
#define ORLIX_HOST_CONSOLE_INPUT_BYTES 65536UL

static int OrlixHostConsoleOutputFD = -1;
static os_unfair_lock OrlixHostConsoleInputLock = OS_UNFAIR_LOCK_INIT;
static unsigned char OrlixHostConsoleInput[ORLIX_HOST_CONSOLE_INPUT_BYTES];
static unsigned long OrlixHostConsoleInputHead;
static unsigned long OrlixHostConsoleInputLength;

static os_log_t OrlixHostConsoleLog(void)
{
    static os_log_t log;

    if (!log) {
        log = os_log_create("org.orlix.OrlixTerminal", "kernel");
    }

    return log;
}

static void OrlixHostConsoleWriteFileDescriptor(const void *bytes,
                                                unsigned long length)
{
    const char *cursor = bytes;
    unsigned long offset = 0;
    int fd = OrlixHostConsoleOutputFD;

    if (fd < 0 || !bytes || length == 0) {
        return;
    }

    while (offset < length) {
        unsigned long remaining = length - offset;
        unsigned long chunk = remaining > (unsigned long)SSIZE_MAX ?
                              (unsigned long)SSIZE_MAX : remaining;
        ssize_t written = write(fd, cursor + offset, (size_t)chunk);

        if (written <= 0) {
            break;
        }
        offset += (unsigned long)written;
    }
}

__attribute__((visibility("default"))) void orlix_host_console_set_output_fd(int fd)
{
    int flags;

    if (fd >= 0) {
        flags = fcntl(fd, F_GETFL, 0);
        if (flags >= 0) {
            (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        }
    }
    OrlixHostConsoleOutputFD = fd;
}

__attribute__((visibility("default"))) unsigned long orlix_host_console_enqueue_input(
    const void *bytes,
    unsigned long length)
{
    const unsigned char *cursor = bytes;
    unsigned long copied = 0;

    if (!bytes || length == 0) {
        return 0;
    }

    os_unfair_lock_lock(&OrlixHostConsoleInputLock);
    while (copied < length &&
           OrlixHostConsoleInputLength < ORLIX_HOST_CONSOLE_INPUT_BYTES) {
        unsigned long tail =
            (OrlixHostConsoleInputHead + OrlixHostConsoleInputLength) %
            ORLIX_HOST_CONSOLE_INPUT_BYTES;
        unsigned long contiguous = ORLIX_HOST_CONSOLE_INPUT_BYTES - tail;
        unsigned long available =
            ORLIX_HOST_CONSOLE_INPUT_BYTES - OrlixHostConsoleInputLength;
        unsigned long remaining = length - copied;
        unsigned long chunk = contiguous;

        if (chunk > available) {
            chunk = available;
        }
        if (chunk > remaining) {
            chunk = remaining;
        }

        memcpy(&OrlixHostConsoleInput[tail], cursor + copied, (size_t)chunk);
        copied += chunk;
        OrlixHostConsoleInputLength += chunk;
    }
    os_unfair_lock_unlock(&OrlixHostConsoleInputLock);

    return copied;
}

__attribute__((visibility("hidden"))) unsigned long orlix_host_console_read_input(
    void *bytes,
    unsigned long length)
{
    unsigned char *cursor = bytes;
    unsigned long copied = 0;

    if (!bytes || length == 0) {
        return 0;
    }

    os_unfair_lock_lock(&OrlixHostConsoleInputLock);
    while (copied < length && OrlixHostConsoleInputLength > 0) {
        unsigned long contiguous =
            ORLIX_HOST_CONSOLE_INPUT_BYTES - OrlixHostConsoleInputHead;
        unsigned long chunk = contiguous;
        unsigned long remaining = length - copied;

        if (chunk > OrlixHostConsoleInputLength) {
            chunk = OrlixHostConsoleInputLength;
        }
        if (chunk > remaining) {
            chunk = remaining;
        }

        memcpy(cursor + copied,
               &OrlixHostConsoleInput[OrlixHostConsoleInputHead],
               (size_t)chunk);
        OrlixHostConsoleInputHead =
            (OrlixHostConsoleInputHead + chunk) %
            ORLIX_HOST_CONSOLE_INPUT_BYTES;
        OrlixHostConsoleInputLength -= chunk;
        copied += chunk;
    }
    os_unfair_lock_unlock(&OrlixHostConsoleInputLock);

    return copied;
}

__attribute__((visibility("hidden"))) void orlix_host_console_write(
    const void *bytes,
    unsigned long length)
{
    const char *text = bytes;
    unsigned long offset;
    unsigned long active_tls;

    if (!bytes || length == 0) {
        return;
    }

    active_tls = OrlixHostEnterHostTls();
    (void)write(STDERR_FILENO, bytes, (size_t)length);
    OrlixHostConsoleWriteFileDescriptor(bytes, length);

    for (offset = 0; offset < length;) {
        unsigned long remaining = length - offset;
        unsigned long chunk = remaining > ORLIX_HOST_CONSOLE_CHUNK_BYTES ?
                              ORLIX_HOST_CONSOLE_CHUNK_BYTES : remaining;
        int chunk_length = chunk > (unsigned long)INT_MAX ? INT_MAX : (int)chunk;

        os_log_with_type(OrlixHostConsoleLog(),
                         OS_LOG_TYPE_INFO,
                         "%{public}.*s",
                         chunk_length,
                         text + offset);
        offset += (unsigned long)chunk_length;
    }
    OrlixHostLeaveHostTls(active_tls);
}
