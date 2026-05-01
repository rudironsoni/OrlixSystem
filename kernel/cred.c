/* IXLandSystem/kernel/cred.c
 * Canonical owner for user/group identity syscalls
 *
 * Implements virtual Linux-shaped credentials within IXLandSystem boundaries.
 * Does NOT use host credential system.
 * Does NOT include Darwin headers.
 *
 * Linux ABI contract:
 * - getuid(), geteuid(), getgid(), getegid()
 * - setuid(), setgid()
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <linux/limits.h>

#include "cred_internal.h"

/* ============================================================================
 * VIRTUAL CREDENTIAL SUBSYSTEM
 * ============================================================================
 * Private credential state internal to IXLandSystem runtime.
 */

/* Default IXLand virtual credential values
 * These are deterministic IXLand-internal values, NOT host identity */
#define KERNEL_DEFAULT_UID      0       /* Virtual root user */
#define KERNEL_DEFAULT_GID      0       /* Virtual root group */
#define KERNEL_DEFAULT_EUID     0       /* Virtual effective root */
#define KERNEL_DEFAULT_EGID     0       /* Virtual effective root */
#define KERNEL_DEFAULT_SUID     0       /* Virtual saved root */
#define KERNEL_DEFAULT_SGID     0       /* Virtual saved root */

/* Global virtual credential state for init/standalone tasks */
static struct cred global_init_cred = {
    .uid = KERNEL_DEFAULT_UID,
    .gid = KERNEL_DEFAULT_GID,
    .euid = KERNEL_DEFAULT_EUID,
    .egid = KERNEL_DEFAULT_EGID,
    .suid = KERNEL_DEFAULT_SUID,
    .sgid = KERNEL_DEFAULT_SGID,
    .groups = NULL,
    .group_count = 0,
    .refs = 1
};

/* Current task's credential pointer (thread-local in real implementation) */
static __thread struct cred *current_cred = NULL;

/* ============================================================================
 * VIRTUAL CREDENTIAL LIFECYCLE IMPLEMENTATIONS
 * ============================================================================ */

struct cred *alloc_cred(void) {
    struct cred *cred = calloc(1, sizeof(struct cred));
    if (cred) {
        cred_init_defaults(cred);
        cred->refs = 1;
    }
    return cred;
}

void free_cred(struct cred *cred) {
    if (cred) {
        free(cred->groups);
        free(cred);
    }
}

struct cred *dup_cred(const struct cred *cred) {
    if (!cred) {
        return NULL;
    }
    struct cred *new = alloc_cred();
    if (new) {
        new->uid = cred->uid;
        new->gid = cred->gid;
        new->euid = cred->euid;
        new->egid = cred->egid;
        new->suid = cred->suid;
        new->sgid = cred->sgid;
        if (cred->group_count > 0) {
            new->groups = calloc(cred->group_count, sizeof(gid_t));
            if (!new->groups) {
                free_cred(new);
                return NULL;
            }
            memcpy(new->groups, cred->groups, cred->group_count * sizeof(gid_t));
            new->group_count = cred->group_count;
        }
        new->refs = 1;
    }
    return new;
}

void cred_init_defaults(struct cred *cred) {
    if (!cred) {
        return;
    }
    cred->uid = KERNEL_DEFAULT_UID;
    cred->gid = KERNEL_DEFAULT_GID;
    cred->euid = KERNEL_DEFAULT_EUID;
    cred->egid = KERNEL_DEFAULT_EGID;
    cred->suid = KERNEL_DEFAULT_SUID;
    cred->sgid = KERNEL_DEFAULT_SGID;
    cred->groups = NULL;
    cred->group_count = 0;
}

int cred_init(void) {
    static int initialized = 0;
    if (!initialized) {
        initialized = 1;
        /* Ensure current_cred is initialized to global_init_cred */
        if (!current_cred) {
            current_cred = &global_init_cred;
        }
    }
    return 0;
}

/* Reset global credentials to IXLand defaults - for testing */
void cred_reset_to_defaults(void) {
    /* Reset global init cred to defaults */
    global_init_cred.uid = KERNEL_DEFAULT_UID;
    global_init_cred.gid = KERNEL_DEFAULT_GID;
    global_init_cred.euid = KERNEL_DEFAULT_EUID;
    global_init_cred.egid = KERNEL_DEFAULT_EGID;
    global_init_cred.suid = KERNEL_DEFAULT_SUID;
    global_init_cred.sgid = KERNEL_DEFAULT_SGID;
    free(global_init_cred.groups);
    global_init_cred.groups = NULL;
    global_init_cred.group_count = 0;
    global_init_cred.refs = 1;
    
    /* Reset current_cred pointer to point to global_init_cred */
    current_cred = &global_init_cred;
}

struct cred *get_current_cred(void) {
    if (!current_cred) {
        /* Fallback to global init cred for early boot/standalone */
        current_cred = &global_init_cred;
    }
    return current_cred;
}

void set_current_cred(struct cred *cred) {
    current_cred = cred;
}

void get_cred(struct cred *cred) {
    if (cred) {
        cred->refs++;
    }
}

void put_cred(struct cred *cred) {
    if (!cred) {
        return;
    }
    cred->refs--;
    if (cred->refs <= 0) {
        /* Global cred is never freed */
        if (cred != &global_init_cred) {
            free_cred(cred);
        }
    }
}

/* ============================================================================
 * INTERNAL VIRTUAL CREDENTIAL OPERATIONS
 * ============================================================================
 * These implement Linux-shaped semantics within IXLandSystem.
 * No host privilege changes are attempted.
 */

static int check_setuid_perm(struct cred *cred, uint32_t uid) {
    (void)uid;
    /*
     * Linux rules for setuid:
     * - If euid is 0 (root), any UID is allowed
     * - Otherwise, only to/from real, effective, or saved UIDs
     */
    if (cred->euid == 0) {
        return 0; /* Root can setuid to anything */
    }
    /* Non-root can only set to real/effective/saved UIDs */
    if (uid == cred->uid || uid == cred->euid || uid == cred->suid) {
        return 0;
    }
    return -EPERM;
}

static int check_setgid_perm(struct cred *cred, uint32_t gid) {
    (void)gid;
    if (cred->egid == 0) {
        return 0; /* Root can setgid to anything */
    }
    if (gid == cred->gid || gid == cred->egid || gid == cred->sgid) {
        return 0;
    }
    return -EPERM;
}

int cred_setuid(struct cred *cred, uint32_t uid) {
    if (!cred) {
        return -EINVAL;
    }

    int ret = check_setuid_perm(cred, uid);
    if (ret < 0) {
        return ret;
    }

    if (cred->euid == 0) {
        /* Root: setuid sets real, effective, and saved */
        cred->suid = uid;
        cred->uid = uid;
        cred->euid = uid;
    } else if (uid == cred->uid) {
        /* Setting to real UID: revert effective to real */
        cred->euid = uid;
    } else if (uid == cred->suid) {
        /* Setting to saved UID: restore effective to saved */
        cred->euid = uid;
    } else if (uid == cred->euid) {
        /* Setting to effective UID: no change */
    } else {
        return -EPERM;
    }

    return 0;
}

int cred_setgid(struct cred *cred, uint32_t gid) {
    if (!cred) {
        return -EINVAL;
    }

    int ret = check_setgid_perm(cred, gid);
    if (ret < 0) {
        return ret;
    }

    if (cred->egid == 0) {
        /* Root: setgid sets real, effective, and saved */
        cred->sgid = gid;
        cred->gid = gid;
        cred->egid = gid;
    } else if (gid == cred->gid) {
        /* Setting to real GID: revert effective to real */
        cred->egid = gid;
    } else if (gid == cred->sgid) {
        /* Setting to saved GID: restore effective to saved */
        cred->egid = gid;
    } else if (gid == cred->egid) {
        /* Setting to effective GID: no change */
    } else {
        return -EPERM;
    }

    return 0;
}

int cred_seteuid(struct cred *cred, uint32_t euid) {
    if (!cred) {
        return -EINVAL;
    }

    /* seteuid can only set to real, effective, or saved UID */
    if (cred->euid != 0) {
        if (euid != cred->uid && euid != cred->euid && euid != cred->suid) {
            return -EPERM;
        }
    }

    cred->euid = euid;
    return 0;
}

int cred_setegid(struct cred *cred, uint32_t egid) {
    if (!cred) {
        return -EINVAL;
    }

    if (cred->egid != 0) {
        if (egid != cred->gid && egid != cred->egid && egid != cred->sgid) {
            return -EPERM;
        }
    }

    cred->egid = egid;
    return 0;
}

int cred_setreuid(struct cred *cred, uint32_t ruid, uint32_t euid) {
    if (!cred) {
        return -EINVAL;
    }

    /* setreuid requires permission to set both values */
    if (cred->euid != 0) {
        if (ruid != (uint32_t)-1 && ruid != cred->uid && ruid != cred->euid && ruid != cred->suid) {
            return -EPERM;
        }
        if (euid != (uint32_t)-1 && euid != cred->uid && euid != cred->euid && euid != cred->suid) {
            return -EPERM;
        }
    }

    if (ruid != (uint32_t)-1) {
        cred->uid = ruid;
    }
    if (euid != (uint32_t)-1) {
        /* Update saved UID under certain conditions */
        if (ruid != (uint32_t)-1 && cred->euid != ruid && cred->euid == cred->uid) {
            cred->suid = cred->euid;
        }
        cred->euid = euid;
    }

    return 0;
}

int cred_setregid(struct cred *cred, uint32_t rgid, uint32_t egid) {
    if (!cred) {
        return -EINVAL;
    }

    if (cred->egid != 0) {
        if (rgid != (uint32_t)-1 && rgid != cred->gid && rgid != cred->egid && rgid != cred->sgid) {
            return -EPERM;
        }
        if (egid != (uint32_t)-1 && egid != cred->gid && egid != cred->egid && egid != cred->sgid) {
            return -EPERM;
        }
    }

    if (rgid != (uint32_t)-1) {
        cred->gid = rgid;
    }
    if (egid != (uint32_t)-1) {
        if (rgid != (uint32_t)-1 && cred->egid != rgid && cred->egid == cred->gid) {
            cred->sgid = cred->egid;
        }
        cred->egid = egid;
    }

    return 0;
}

int cred_setresuid(struct cred *cred, uint32_t ruid, uint32_t euid, uint32_t suid) {
    if (!cred) {
        return -EINVAL;
    }

    /* setresuid is privileged; only root or allowed transitions */
    if (cred->euid != 0) {
        /* 
         * Non-root can only use current values when -1 is specified,
         * or switch to values they already have.
         */
        if (ruid != (uint32_t)-1 && ruid != cred->uid && ruid != cred->euid && ruid != cred->suid) {
            return -EPERM;
        }
        if (euid != (uint32_t)-1 && euid != cred->uid && euid != cred->euid && euid != cred->suid) {
            return -EPERM;
        }
        if (suid != (uint32_t)-1 && suid != cred->uid && suid != cred->euid && suid != cred->suid) {
            return -EPERM;
        }
    }

    if (ruid != (uint32_t)-1) {
        cred->uid = ruid;
    }
    if (euid != (uint32_t)-1) {
        cred->euid = euid;
    }
    if (suid != (uint32_t)-1) {
        cred->suid = suid;
    }

    return 0;
}

int cred_setresgid(struct cred *cred, uint32_t rgid, uint32_t egid, uint32_t sgid) {
    if (!cred) {
        return -EINVAL;
    }

    if (cred->egid != 0) {
        if (rgid != (uint32_t)-1 && rgid != cred->gid && rgid != cred->egid && rgid != cred->sgid) {
            return -EPERM;
        }
        if (egid != (uint32_t)-1 && egid != cred->gid && egid != cred->egid && egid != cred->sgid) {
            return -EPERM;
        }
        if (sgid != (uint32_t)-1 && sgid != cred->gid && sgid != cred->egid && sgid != cred->sgid) {
            return -EPERM;
        }
    }

    if (rgid != (uint32_t)-1) {
        cred->gid = rgid;
    }
    if (egid != (uint32_t)-1) {
        cred->egid = egid;
    }
    if (sgid != (uint32_t)-1) {
        cred->sgid = sgid;
    }

    return 0;
}

bool cred_has_group(const struct cred *cred, gid_t gid) {
    if (!cred) {
        return false;
    }
    if (cred->egid == gid) {
        return true;
    }
    for (size_t i = 0; i < cred->group_count; i++) {
        if (cred->groups[i] == gid) {
            return true;
        }
    }
    return false;
}

int cred_setgroups(struct cred *cred, size_t size, const gid_t *list) {
    gid_t *new_groups = NULL;

    if (!cred) {
        return -EINVAL;
    }
    if (cred->euid != 0) {
        return -EPERM;
    }
    if (size > NGROUPS_MAX) {
        return -EINVAL;
    }
    if (size > 0 && !list) {
        return -EFAULT;
    }

    if (size > 0) {
        new_groups = calloc(size, sizeof(gid_t));
        if (!new_groups) {
            return -ENOMEM;
        }
        memcpy(new_groups, list, size * sizeof(gid_t));
    }

    free(cred->groups);
    cred->groups = new_groups;
    cred->group_count = size;
    return 0;
}

/* ============================================================================
 * LINUX-FACING PUBLIC SYSCALL ENTRIES
 * ============================================================================
 * These export the Linux ABI contract from the kernel owner.
 * They return IXLand virtual credential state, NOT host identities.
 */

/* Implementation of getuid_impl - internal entry point */
uid_t getuid_impl(void) {
    struct cred *cred = get_current_cred();
    return (uid_t)cred->uid;
}

/* Implementation of geteuid_impl - internal entry point */
uid_t geteuid_impl(void) {
    struct cred *cred = get_current_cred();
    return (uid_t)cred->euid;
}

/* Implementation of getgid_impl - internal entry point */
gid_t getgid_impl(void) {
    struct cred *cred = get_current_cred();
    return (gid_t)cred->gid;
}

/* Implementation of getegid_impl - internal entry point */
gid_t getegid_impl(void) {
    struct cred *cred = get_current_cred();
    return (gid_t)cred->egid;
}

/* Implementation of setuid_impl - internal entry point */
int setuid_impl(uid_t uid) {
    struct cred *cred = get_current_cred();
    int ret = cred_setuid(cred, (uint32_t)uid);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return 0;
}

/* Implementation of setgid_impl - internal entry point */
int setgid_impl(gid_t gid) {
    struct cred *cred = get_current_cred();
    int ret = cred_setgid(cred, (uint32_t)gid);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return 0;
}

int getgroups_impl(int size, gid_t list[]) {
    struct cred *cred = get_current_cred();

    if (!cred) {
        errno = EINVAL;
        return -1;
    }
    if (size < 0) {
        errno = EINVAL;
        return -1;
    }
    if (size == 0) {
        return (int)cred->group_count;
    }
    if (!list) {
        errno = EFAULT;
        return -1;
    }
    if ((size_t)size < cred->group_count) {
        errno = EINVAL;
        return -1;
    }

    for (size_t i = 0; i < cred->group_count; i++) {
        list[i] = cred->groups[i];
    }
    return (int)cred->group_count;
}

int setgroups_impl(int size, const gid_t *list) {
    struct cred *cred = get_current_cred();
    int ret;

    if (size < 0) {
        errno = EINVAL;
        return -1;
    }

    ret = cred_setgroups(cred, (size_t)size, list);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return 0;
}

/* ============================================================================
 * PUBLIC CANONICAL WRAPPERS
 * ============================================================================
 * These are the Linux-facing ABI entry points.
 */

__attribute__((visibility("default"))) uid_t getuid(void) {
    return getuid_impl();
}

__attribute__((visibility("default"))) uid_t geteuid(void) {
    return geteuid_impl();
}

__attribute__((visibility("default"))) gid_t getgid(void) {
    return getgid_impl();
}

__attribute__((visibility("default"))) gid_t getegid(void) {
    return getegid_impl();
}

__attribute__((visibility("default"))) int setuid(uid_t uid) {
    return setuid_impl(uid);
}

__attribute__((visibility("default"))) int setgid(gid_t gid) {
    return setgid_impl(gid);
}

__attribute__((visibility("default"))) int getgroups(int size, gid_t list[]) {
    return getgroups_impl(size, list);
}

__attribute__((visibility("default"))) int setgroups(int size, const gid_t *list) {
    return setgroups_impl(size, list);
}
