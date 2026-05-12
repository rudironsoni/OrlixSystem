/* iXland - Resource Limits and Usage
 *
 * Canonical owner for resource syscalls:
 * - getrlimit(), setrlimit(), getrlimit64(), setrlimit64()
 * - getrusage()
 * - prlimit(), prlimit64()
 *
 * Linux-shaped canonical owner - iOS mediation as implementation detail
 */

#include "resource.h"
#include "task.h"

#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>

/* ============================================================================
 * RLIMIT - Resource limits (private implementation)
 * ============================================================================ */

int getrlimit_impl(int resource, struct rlimit *rlim) {
    struct task *task = task_current();

    if (!rlim) {
        return -EFAULT;
    }
    if (resource < 0 || resource >= 16) {
        return -EINVAL;
    }
    if (!task) {
        return -ESRCH;
    }
    rlim->rlim_cur = (__kernel_ulong_t)task->rlimits[resource].cur;
    rlim->rlim_max = (__kernel_ulong_t)task->rlimits[resource].max;
    return 0;
}

int setrlimit_impl(int resource, const struct rlimit *rlim) {
    struct task *task = task_current();

    if (!rlim) {
        return -EFAULT;
    }
    if (resource < 0 || resource >= 16) {
        return -EINVAL;
    }
    if (rlim->rlim_cur > rlim->rlim_max) {
        return -EINVAL;
    }
    if (!task) {
        return -ESRCH;
    }
    task->rlimits[resource].cur = (uint64_t)rlim->rlim_cur;
    task->rlimits[resource].max = (uint64_t)rlim->rlim_max;
    return 0;
}

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

/* ============================================================================
 * PRLIMIT - Process resource limits (private implementation)
 * ============================================================================ */

int prlimit_impl(int32_t pid, int resource, const struct rlimit *new_limit,
                 struct rlimit *old_limit) {
    struct task *task = task_current();

    if (pid != 0 && (!task || pid != task->pid)) {
        return -ESRCH;
    }

    /* Get old values first */
    if (old_limit) {
        if (getrlimit_impl(resource, old_limit) < 0) {
            return -1;
        }
    }

    /* Set new values */
    if (new_limit) {
        return setrlimit_impl(resource, new_limit);
    }

    return 0;
}
