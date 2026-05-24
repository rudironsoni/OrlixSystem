#include "OrlixHostAdapter/runtime/thread.h"

#include <errno.h>
#include <time.h>

__attribute__((visibility("hidden"))) void orlix_host_thread_idle(void)
{
    struct timespec delay = {
        .tv_sec = 0,
        .tv_nsec = 1000000,
    };

    while (nanosleep(&delay, &delay) == -1 && errno == EINTR) {
    }
}
