#include "OrlixHostAdapter/runtime/thread.h"
#include "OrlixHostAdapter/runtime/host_tls.h"
#include "OrlixHostAdapter/runtime/time.h"

#include <errno.h>
#include <time.h>

#define ORLIX_HOST_IDLE_FALLBACK_NS 1000000ULL

static void orlix_host_thread_sleep_ns(unsigned long long delay_ns)
{
    struct timespec delay = {
        .tv_sec = (time_t)(delay_ns / 1000000000ULL),
        .tv_nsec = (long)(delay_ns % 1000000000ULL),
    };
    unsigned long active_tls = OrlixHostEnterHostTls();

    while (nanosleep(&delay, &delay) == -1 && errno == EINTR) {
    }

    OrlixHostLeaveHostTls(active_tls);
}

__attribute__((visibility("hidden"))) void orlix_host_thread_idle(void)
{
    orlix_host_thread_sleep_ns(ORLIX_HOST_IDLE_FALLBACK_NS);
}

__attribute__((visibility("hidden"))) void orlix_host_thread_idle_until(unsigned long long deadline_ns)
{
    unsigned long long now_ns;

    if (deadline_ns == 0) {
        orlix_host_thread_idle();
        return;
    }

    now_ns = orlix_host_time_monotonic_ns();
    if (now_ns >= deadline_ns) {
        return;
    }

    orlix_host_thread_sleep_ns(deadline_ns - now_ns);
}
