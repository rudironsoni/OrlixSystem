#include "OrlixHostAdapter/runtime/time.h"
#include "OrlixHostAdapter/runtime/host_tls.h"

#include <stdint.h>
#include <time.h>

__attribute__((visibility("hidden"))) unsigned long long orlix_host_time_monotonic_ns(void)
{
    struct timespec now = {
        .tv_sec = 0,
        .tv_nsec = 0,
    };
    unsigned long active_tls = OrlixHostEnterHostTls();
    unsigned long long result;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        OrlixHostLeaveHostTls(active_tls);
        return 0;
    }

    result = ((uint64_t)now.tv_sec * 1000000000ULL) + (uint64_t)now.tv_nsec;
    OrlixHostLeaveHostTls(active_tls);
    return result;
}
