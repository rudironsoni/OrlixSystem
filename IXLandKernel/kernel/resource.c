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

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/resource.h>

/* Forward declare struct rlimit64 for visibility on platforms where it may
 * not be visible in the global scope (Darwin defines it inside the struct
 * rlimit expansion but not as a standalone forward-declarable type) */
struct rlimit64;

/* ============================================================================
 * RLIMIT - Resource limits (private implementation)
 * ============================================================================ */

static int getrlimit_impl(int resource, struct rlimit *rlim) {
    return getrlimit(resource, rlim);
}

static int setrlimit_impl(int resource, const struct rlimit *rlim) {
    /* iOS restriction: some limits cannot be changed */
    return setrlimit(resource, rlim);
}

#ifdef __LP64__
static int getrlimit64_impl(int resource, struct rlimit64 *rlim) {
    return getrlimit_impl(resource, (struct rlimit *)rlim);
}

static int setrlimit64_impl(int resource, const struct rlimit64 *rlim) {
    return setrlimit_impl(resource, (const struct rlimit *)rlim);
}
#endif

/* ============================================================================
 * RUSAGE - Resource usage (private implementation)
 * ============================================================================ */

long times_impl(struct linux_tms_kernel *buf) {
    if (buf) {
        memset(buf, 0, sizeof(*buf));
    }
    return 0;
}

int linux_getrusage_impl(int who, struct linux_rusage_kernel *usage) {
    if (who != 0 && who != -1 && who != 1) {
        errno = EINVAL;
        return -1;
    }
    if (!usage) {
        errno = EFAULT;
        return -1;
    }
    memset(usage, 0, sizeof(*usage));
    return 0;
}

static int getrusage_impl(int who, struct rusage *usage) {
    if (who != 0 && who != -1 && who != 1) {
        errno = EINVAL;
        return -1;
    }
    if (!usage) {
        errno = EFAULT;
        return -1;
    }
    memset(usage, 0, sizeof(*usage));
    return 0;
}

/* ============================================================================
 * PRLIMIT - Process resource limits (private implementation)
 * ============================================================================ */

static int prlimit_impl(int32_t pid, int resource, const struct rlimit *new_limit,
                        struct rlimit *old_limit) {
    (void)pid;

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

/* ============================================================================
 * Public Canonical Syscalls
 * ============================================================================ */

__attribute__((visibility("default"))) int getrlimit(int resource, struct rlimit *rlim) {
    return getrlimit_impl(resource, rlim);
}

__attribute__((visibility("default"))) int setrlimit(int resource, const struct rlimit *rlim) {
    return setrlimit_impl(resource, rlim);
}

#ifdef __LP64__
__attribute__((visibility("default"))) int getrlimit64(int resource, struct rlimit64 *rlim) {
    return getrlimit64_impl(resource, rlim);
}

__attribute__((visibility("default"))) int setrlimit64(int resource, const struct rlimit64 *rlim) {
    return setrlimit64_impl(resource, rlim);
}
#endif

__attribute__((visibility("default"))) int getrusage(int who, struct rusage *usage) {
    return getrusage_impl(who, usage);
}

__attribute__((visibility("default"))) int prlimit(int32_t pid, int resource, const struct rlimit *new_limit,
                                                   struct rlimit *old_limit) {
    return prlimit_impl(pid, resource, new_limit, old_limit);
}
