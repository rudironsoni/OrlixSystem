#include "OrlixHostAdapter/terminal/console.h"

#include <fcntl.h>
#include <limits.h>
#include <os/log.h>
#include <stddef.h>
#include <unistd.h>

#define ORLIX_HOST_CONSOLE_CHUNK_BYTES 1024UL

static int OrlixHostConsoleOutputFD = -1;

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

__attribute__((visibility("hidden"))) void orlix_host_console_write(
    const void *bytes,
    unsigned long length)
{
    const char *text = bytes;
    unsigned long offset;

    if (!bytes || length == 0) {
        return;
    }

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
}
