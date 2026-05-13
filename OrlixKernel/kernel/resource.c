/* OrlixKernel - Resource Limits and Usage
 *
 * Canonical owner for resource syscalls:
 * - getrlimit(), setrlimit(), getrlimit64(), setrlimit64()
 * - getrusage()
 * - prlimit(), prlimit64()
 *
 * Linux-shaped canonical owner - iOS mediation as implementation detail
 */

#include <linux/resource.h>
#include <uapi/linux/times.h>
#include "../private/kernel/resource_state.h"
#include "task.h"
#include "../private/kernel/task_state.h"

#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>

/* ============================================================================
 * RUSAGE - Resource usage (private implementation)
 * ============================================================================ */

long times_impl(struct tms *buf) {
    if (buf) {
        memset(buf, 0, sizeof(*buf));
    }
    return 0;
}

int getrusage_impl(int who, struct rusage *usage) {
    if (who != 0 && who != -1 && who != 1) {
        return -EINVAL;
    }
    if (!usage) {
        return -EFAULT;
    }
    memset(usage, 0, sizeof(*usage));
    return 0;
}
