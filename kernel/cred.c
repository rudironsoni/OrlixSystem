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
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <linux/limits.h>
#include <linux/capability.h>
#include <linux/prctl.h>
#include <linux/securebits.h>
#include <linux/stat.h>

#include "cred_internal.h"
#include "task.h"

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
#define KERNEL_DEFAULT_FSUID    0       /* Virtual filesystem root */
#define KERNEL_DEFAULT_FSGID    0       /* Virtual filesystem root */

static uint64_t cred_full_cap_mask(void) {
    return (CAP_LAST_CAP >= 63) ? UINT64_MAX : ((1ULL << (CAP_LAST_CAP + 1)) - 1ULL);
}

static uint64_t cred_cap_bit(int cap) {
    if (!cap_valid(cap) || cap >= 64) {
        return 0;
    }
    return 1ULL << cap;
}

static void cred_reset_caps_for_root(struct cred *cred) {
    uint64_t full;

    if (!cred) {
        return;
    }
    full = cred_full_cap_mask();
    cred->cap_permitted = full;
    cred->cap_effective = full;
    cred->cap_inheritable = 0;
    cred->cap_bounding = full;
    cred->cap_ambient = 0;
}

static void cred_drop_privilege_caps(struct cred *cred) {
    if (!cred) {
        return;
    }
    cred->cap_permitted = 0;
    cred->cap_effective = 0;
    cred->cap_ambient = 0;
}

static bool cred_keepcaps(const struct cred *cred) {
    return cred && (cred->securebits & SECBIT_KEEP_CAPS) != 0;
}

static void cred_apply_uid_cap_fixup(struct cred *cred) {
    if (!cred) {
        return;
    }
    if ((cred->securebits & SECBIT_NO_SETUID_FIXUP) != 0) {
        return;
    }
    if (cred->euid == 0) {
        cred_reset_caps_for_root(cred);
    } else if (cred_keepcaps(cred)) {
        cred->cap_effective = 0;
        cred->cap_ambient = 0;
    } else {
        cred_drop_privilege_caps(cred);
    }
}

/* Global virtual credential state for init/standalone tasks */
static struct cred global_init_cred = {
    .uid = KERNEL_DEFAULT_UID,
    .gid = KERNEL_DEFAULT_GID,
    .euid = KERNEL_DEFAULT_EUID,
    .egid = KERNEL_DEFAULT_EGID,
    .suid = KERNEL_DEFAULT_SUID,
    .sgid = KERNEL_DEFAULT_SGID,
    .fsuid = KERNEL_DEFAULT_FSUID,
    .fsgid = KERNEL_DEFAULT_FSGID,
    .groups = NULL,
    .group_count = 0,
    .no_new_privs = false,
    .securebits = SECUREBITS_DEFAULT,
    .cap_permitted = 0,
    .cap_effective = 0,
    .cap_inheritable = 0,
    .cap_bounding = 0,
    .cap_ambient = 0,
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
        new->fsuid = cred->fsuid;
        new->fsgid = cred->fsgid;
        if (cred->group_count > 0) {
            new->groups = calloc(cred->group_count, sizeof(gid_t));
            if (!new->groups) {
                free_cred(new);
                return NULL;
            }
            memcpy(new->groups, cred->groups, cred->group_count * sizeof(gid_t));
            new->group_count = cred->group_count;
        }
        new->no_new_privs = cred->no_new_privs;
        new->securebits = cred->securebits;
        new->cap_permitted = cred->cap_permitted;
        new->cap_effective = cred->cap_effective;
        new->cap_inheritable = cred->cap_inheritable;
        new->cap_bounding = cred->cap_bounding;
        new->cap_ambient = cred->cap_ambient;
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
    cred->fsuid = KERNEL_DEFAULT_FSUID;
    cred->fsgid = KERNEL_DEFAULT_FSGID;
    cred->groups = NULL;
    cred->group_count = 0;
    cred->no_new_privs = false;
    cred->securebits = SECUREBITS_DEFAULT;
    cred_reset_caps_for_root(cred);
}

int cred_init(void) {
    static int initialized = 0;
    if (!initialized) {
        initialized = 1;
        cred_reset_caps_for_root(&global_init_cred);
        /* Ensure current_cred is initialized to global_init_cred */
        if (!current_cred) {
            current_cred = &global_init_cred;
        }
    }
    return 0;
}

/* Reset global credentials to IXLand defaults - for testing */
void cred_reset_to_defaults(void) {
    struct task_struct *task;

    /* Reset global init cred to defaults */
    global_init_cred.uid = KERNEL_DEFAULT_UID;
    global_init_cred.gid = KERNEL_DEFAULT_GID;
    global_init_cred.euid = KERNEL_DEFAULT_EUID;
    global_init_cred.egid = KERNEL_DEFAULT_EGID;
    global_init_cred.suid = KERNEL_DEFAULT_SUID;
    global_init_cred.sgid = KERNEL_DEFAULT_SGID;
    global_init_cred.fsuid = KERNEL_DEFAULT_FSUID;
    global_init_cred.fsgid = KERNEL_DEFAULT_FSGID;
    free(global_init_cred.groups);
    global_init_cred.groups = NULL;
    global_init_cred.group_count = 0;
    global_init_cred.no_new_privs = false;
    global_init_cred.securebits = SECUREBITS_DEFAULT;
    cred_reset_caps_for_root(&global_init_cred);
    global_init_cred.refs = 1;
    
    /* Reset current_cred pointer to point to global_init_cred */
    current_cred = &global_init_cred;
    task = get_current();
    if (task && task->cred != &global_init_cred) {
        put_cred(task->cred);
        get_cred(&global_init_cred);
        task->cred = &global_init_cred;
    }
}

struct cred *get_current_cred(void) {
    struct task_struct *task = get_current();
    if (task && task->cred) {
        current_cred = task->cred;
        return task->cred;
    }
    if (!current_cred) {
        /* Fallback to global init cred for early boot/standalone */
        current_cred = &global_init_cred;
    }
    return current_cred;
}

void set_current_cred(struct cred *cred) {
    struct task_struct *task = get_current();
    if (task && task->cred != cred) {
        if (cred) {
            get_cred(cred);
        }
        put_cred(task->cred);
        task->cred = cred;
    }
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
    if (cred_has_cap(cred, CAP_SETUID)) {
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
    if (cred_has_cap(cred, CAP_SETGID)) {
        return 0; /* Root can setgid to anything */
    }
    if (gid == cred->gid || gid == cred->egid || gid == cred->sgid) {
        return 0;
    }
    return -EPERM;
}

int cred_setuid(struct cred *cred, uint32_t uid) {
    bool privileged;

    if (!cred) {
        return -EINVAL;
    }

    privileged = cred_has_cap(cred, CAP_SETUID);
    int ret = check_setuid_perm(cred, uid);
    if (ret < 0) {
        return ret;
    }

    if (privileged) {
        cred->suid = uid;
        cred->uid = uid;
        cred->euid = uid;
        cred->fsuid = uid;
        cred_apply_uid_cap_fixup(cred);
    } else if (uid == cred->uid) {
        /* Setting to real UID: revert effective to real */
        cred->euid = uid;
        cred->fsuid = uid;
    } else if (uid == cred->suid) {
        /* Setting to saved UID: restore effective to saved */
        cred->euid = uid;
        cred->fsuid = uid;
    } else if (uid == cred->euid) {
        /* Setting to effective UID: no change */
    } else {
        return -EPERM;
    }

    return 0;
}

int cred_setgid(struct cred *cred, uint32_t gid) {
    bool privileged;

    if (!cred) {
        return -EINVAL;
    }

    privileged = cred_has_cap(cred, CAP_SETGID);
    int ret = check_setgid_perm(cred, gid);
    if (ret < 0) {
        return ret;
    }

    if (privileged) {
        cred->sgid = gid;
        cred->gid = gid;
        cred->egid = gid;
        cred->fsgid = gid;
    } else if (gid == cred->gid) {
        /* Setting to real GID: revert effective to real */
        cred->egid = gid;
        cred->fsgid = gid;
    } else if (gid == cred->sgid) {
        /* Setting to saved GID: restore effective to saved */
        cred->egid = gid;
        cred->fsgid = gid;
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
    if (!cred_has_cap(cred, CAP_SETUID)) {
        if (euid != cred->uid && euid != cred->euid && euid != cred->suid) {
            return -EPERM;
        }
    }

    cred->euid = euid;
    cred->fsuid = euid;
    cred_apply_uid_cap_fixup(cred);
    return 0;
}

int cred_setegid(struct cred *cred, uint32_t egid) {
    if (!cred) {
        return -EINVAL;
    }

    if (!cred_has_cap(cred, CAP_SETGID)) {
        if (egid != cred->gid && egid != cred->egid && egid != cred->sgid) {
            return -EPERM;
        }
    }

    cred->egid = egid;
    cred->fsgid = egid;
    return 0;
}

int cred_setreuid(struct cred *cred, uint32_t ruid, uint32_t euid) {
    if (!cred) {
        return -EINVAL;
    }

    /* setreuid requires permission to set both values */
    if (!cred_has_cap(cred, CAP_SETUID)) {
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
        cred->fsuid = euid;
    }
    cred_apply_uid_cap_fixup(cred);

    return 0;
}

int cred_setregid(struct cred *cred, uint32_t rgid, uint32_t egid) {
    if (!cred) {
        return -EINVAL;
    }

    if (!cred_has_cap(cred, CAP_SETGID)) {
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
        cred->fsgid = egid;
    }

    return 0;
}

int cred_setresuid(struct cred *cred, uint32_t ruid, uint32_t euid, uint32_t suid) {
    if (!cred) {
        return -EINVAL;
    }

    /* setresuid is privileged; only root or allowed transitions */
    if (!cred_has_cap(cred, CAP_SETUID)) {
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
        cred->fsuid = euid;
    }
    if (suid != (uint32_t)-1) {
        cred->suid = suid;
    }
    cred_apply_uid_cap_fixup(cred);

    return 0;
}

int cred_setresgid(struct cred *cred, uint32_t rgid, uint32_t egid, uint32_t sgid) {
    if (!cred) {
        return -EINVAL;
    }

    if (!cred_has_cap(cred, CAP_SETGID)) {
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
        cred->fsgid = egid;
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
    if (cred->fsgid == gid || cred->egid == gid) {
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
    if (!cred_has_cap(cred, CAP_SETGID)) {
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

void cred_apply_exec_metadata(struct cred *cred, uid_t file_uid, gid_t file_gid, uint32_t mode) {
    bool privileged_file;
    bool gains_privilege;

    if (!cred) {
        return;
    }

    privileged_file = (mode & (S_ISUID | S_ISGID)) != 0;
    gains_privilege = !cred->no_new_privs && privileged_file;

    if (!cred->no_new_privs && (mode & S_ISUID) != 0) {
        cred->euid = file_uid;
        cred->fsuid = file_uid;
    }
    if (!cred->no_new_privs && (mode & S_ISGID) != 0) {
        cred->egid = file_gid;
        cred->fsgid = file_gid;
    }

    if (privileged_file) {
        cred->cap_ambient = 0;
    }

    cred->suid = cred->euid;
    cred->sgid = cred->egid;

    if (cred->euid == 0 && !cred->no_new_privs &&
        (cred->securebits & SECBIT_NOROOT) == 0) {
        cred->cap_permitted = cred->cap_bounding;
        cred->cap_effective = cred->cap_permitted;
    } else if (cred->euid != 0) {
        if (gains_privilege) {
            cred_drop_privilege_caps(cred);
        } else {
            cred->cap_permitted |= cred->cap_ambient;
            cred->cap_effective |= cred->cap_ambient;
        }
    }
    cred->securebits &= ~SECBIT_KEEP_CAPS;
}

void cred_apply_exec_file_capabilities(struct cred *cred, uint64_t permitted,
                                       uint64_t inheritable, bool effective) {
    uint64_t full;
    uint64_t next_permitted;

    if (!cred) {
        return;
    }
    if (cred->no_new_privs) {
        if ((permitted | inheritable) != 0 || effective) {
            cred->cap_ambient = 0;
        }
        return;
    }

    full = cred_full_cap_mask();
    permitted &= full;
    inheritable &= full;
    next_permitted = (permitted | (cred->cap_inheritable & inheritable)) & cred->cap_bounding;

    cred->cap_permitted = next_permitted;
    cred->cap_effective = effective ? next_permitted : 0;
    cred->cap_ambient = 0;
}

bool cred_no_new_privs(const struct cred *cred) {
    return cred && cred->no_new_privs;
}

int cred_set_no_new_privs(struct cred *cred) {
    if (!cred) {
        return -EINVAL;
    }
    cred->no_new_privs = true;
    return 0;
}

bool cred_has_cap(const struct cred *cred, int cap) {
    uint64_t bit = cred_cap_bit(cap);

    if (!cred || bit == 0) {
        return false;
    }
    return (cred->cap_effective & bit) != 0;
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

int seteuid_impl(uid_t euid) {
    struct cred *cred = get_current_cred();
    int ret = cred_seteuid(cred, (uint32_t)euid);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return 0;
}

int setegid_impl(gid_t egid) {
    struct cred *cred = get_current_cred();
    int ret = cred_setegid(cred, (uint32_t)egid);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return 0;
}

int setresuid_impl(uid_t ruid, uid_t euid, uid_t suid) {
    struct cred *cred = get_current_cred();
    int ret = cred_setresuid(cred, (uint32_t)ruid, (uint32_t)euid, (uint32_t)suid);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return 0;
}

int setresgid_impl(gid_t rgid, gid_t egid, gid_t sgid) {
    struct cred *cred = get_current_cred();
    int ret = cred_setresgid(cred, (uint32_t)rgid, (uint32_t)egid, (uint32_t)sgid);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return 0;
}

int setreuid_impl(uid_t ruid, uid_t euid) {
    struct cred *cred = get_current_cred();
    int ret = cred_setreuid(cred, (uint32_t)ruid, (uint32_t)euid);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return 0;
}

int setregid_impl(gid_t rgid, gid_t egid) {
    struct cred *cred = get_current_cred();
    int ret = cred_setregid(cred, (uint32_t)rgid, (uint32_t)egid);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return 0;
}

int getresuid_impl(uid_t *ruid, uid_t *euid, uid_t *suid) {
    struct cred *cred = get_current_cred();

    if (!ruid || !euid || !suid) {
        errno = EFAULT;
        return -1;
    }
    *ruid = (uid_t)cred->uid;
    *euid = (uid_t)cred->euid;
    *suid = (uid_t)cred->suid;
    return 0;
}

int getresgid_impl(gid_t *rgid, gid_t *egid, gid_t *sgid) {
    struct cred *cred = get_current_cred();

    if (!rgid || !egid || !sgid) {
        errno = EFAULT;
        return -1;
    }
    *rgid = (gid_t)cred->gid;
    *egid = (gid_t)cred->egid;
    *sgid = (gid_t)cred->sgid;
    return 0;
}

uid_t setfsuid_impl(uid_t fsuid) {
    struct cred *cred = get_current_cred();
    uid_t old;

    if (!cred) {
        return (uid_t)-1;
    }
    old = (uid_t)cred->fsuid;
    if (cred_has_cap(cred, CAP_SETUID) ||
        fsuid == cred->uid || fsuid == cred->euid ||
        fsuid == cred->suid || fsuid == cred->fsuid) {
        cred->fsuid = (uint32_t)fsuid;
    }
    return old;
}

gid_t setfsgid_impl(gid_t fsgid) {
    struct cred *cred = get_current_cred();
    gid_t old;

    if (!cred) {
        return (gid_t)-1;
    }
    old = (gid_t)cred->fsgid;
    if (cred_has_cap(cred, CAP_SETGID) ||
        fsgid == cred->gid || fsgid == cred->egid ||
        fsgid == cred->sgid || fsgid == cred->fsgid) {
        cred->fsgid = (uint32_t)fsgid;
    }
    return old;
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

int prctl_impl(int option, unsigned long arg2, unsigned long arg3, unsigned long arg4, unsigned long arg5) {
    struct cred *cred = get_current_cred();

    switch (option) {
    case PR_CAPBSET_READ: {
        uint64_t bit = cred_cap_bit((int)arg2);

        if (arg3 != 0 || arg4 != 0 || arg5 != 0 || bit == 0) {
            errno = EINVAL;
            return -1;
        }
        return (cred->cap_bounding & bit) != 0 ? 1 : 0;
    }
    case PR_CAPBSET_DROP: {
        uint64_t bit = cred_cap_bit((int)arg2);

        if (arg3 != 0 || arg4 != 0 || arg5 != 0 || bit == 0) {
            errno = EINVAL;
            return -1;
        }
        if (!cred_has_cap(cred, CAP_SETPCAP)) {
            errno = EPERM;
            return -1;
        }
        cred->cap_bounding &= ~bit;
        cred->cap_ambient &= ~bit;
        return 0;
    }
    case PR_CAP_AMBIENT: {
        uint64_t bit = cred_cap_bit((int)arg3);

        if (arg5 != 0) {
            errno = EINVAL;
            return -1;
        }
        switch (arg2) {
        case PR_CAP_AMBIENT_IS_SET:
            if (arg4 != 0 || bit == 0) {
                errno = EINVAL;
                return -1;
            }
            return (cred->cap_ambient & bit) != 0 ? 1 : 0;
        case PR_CAP_AMBIENT_RAISE:
            if (arg4 != 0 || bit == 0) {
                errno = EINVAL;
                return -1;
            }
            if ((cred->securebits & SECBIT_NO_CAP_AMBIENT_RAISE) != 0) {
                errno = EPERM;
                return -1;
            }
            if ((cred->cap_permitted & bit) == 0 || (cred->cap_inheritable & bit) == 0) {
                errno = EPERM;
                return -1;
            }
            cred->cap_ambient |= bit;
            return 0;
        case PR_CAP_AMBIENT_LOWER:
            if (arg4 != 0 || bit == 0) {
                errno = EINVAL;
                return -1;
            }
            cred->cap_ambient &= ~bit;
            return 0;
        case PR_CAP_AMBIENT_CLEAR_ALL:
            if (arg3 != 0 || arg4 != 0) {
                errno = EINVAL;
                return -1;
            }
            cred->cap_ambient = 0;
            return 0;
        default:
            errno = EINVAL;
            return -1;
        }
    }
    case PR_GET_KEEPCAPS:
        if (arg2 != 0 || arg3 != 0 || arg4 != 0 || arg5 != 0) {
            errno = EINVAL;
            return -1;
        }
        return cred_keepcaps(cred) ? 1 : 0;
    case PR_SET_KEEPCAPS:
        if (arg2 > 1 || arg3 != 0 || arg4 != 0 || arg5 != 0) {
            errno = EINVAL;
            return -1;
        }
        if ((cred->securebits & SECBIT_KEEP_CAPS_LOCKED) != 0) {
            errno = EPERM;
            return -1;
        }
        if (arg2 == 1) {
            cred->securebits |= SECBIT_KEEP_CAPS;
        } else {
            cred->securebits &= ~SECBIT_KEEP_CAPS;
        }
        return 0;
    case PR_GET_SECUREBITS:
        if (arg2 != 0 || arg3 != 0 || arg4 != 0 || arg5 != 0) {
            errno = EINVAL;
            return -1;
        }
        return (int)cred->securebits;
    case PR_SET_SECUREBITS: {
        unsigned long valid = SECURE_ALL_BITS | SECURE_ALL_LOCKS;
        uint32_t requested = (uint32_t)arg2;
        uint32_t locked = cred->securebits & SECURE_ALL_LOCKS;

        if ((arg2 & ~valid) != 0 || arg3 != 0 || arg4 != 0 || arg5 != 0) {
            errno = EINVAL;
            return -1;
        }
        if (!cred_has_cap(cred, CAP_SETPCAP)) {
            errno = EPERM;
            return -1;
        }
        if ((requested & locked) != locked) {
            errno = EPERM;
            return -1;
        }
        if (((requested ^ cred->securebits) & (locked >> 1)) != 0) {
            errno = EPERM;
            return -1;
        }
        cred->securebits = requested;
        return 0;
    }
    case PR_SET_NO_NEW_PRIVS:
        if (arg2 != 1 || arg3 != 0 || arg4 != 0 || arg5 != 0) {
            errno = EINVAL;
            return -1;
        }
        if (cred_set_no_new_privs(cred) < 0) {
            errno = EINVAL;
            return -1;
        }
        return 0;
    case PR_GET_NO_NEW_PRIVS:
        if (arg2 != 0 || arg3 != 0 || arg4 != 0 || arg5 != 0) {
            errno = EINVAL;
            return -1;
        }
        return cred_no_new_privs(cred) ? 1 : 0;
    default:
        errno = EINVAL;
        return -1;
    }
}

static void cred_cap_split(uint64_t value, struct __user_cap_data_struct data[_LINUX_CAPABILITY_U32S_3],
                           int field) {
    uint32_t lo = (uint32_t)(value & 0xffffffffULL);
    uint32_t hi = (uint32_t)((value >> 32) & 0xffffffffULL);

    if (field == 0) {
        data[0].effective = lo;
        data[1].effective = hi;
    } else if (field == 1) {
        data[0].permitted = lo;
        data[1].permitted = hi;
    } else {
        data[0].inheritable = lo;
        data[1].inheritable = hi;
    }
}

static uint64_t cred_cap_join(const struct __user_cap_data_struct data[_LINUX_CAPABILITY_U32S_3],
                              int field) {
    uint64_t lo;
    uint64_t hi;

    if (field == 0) {
        lo = data[0].effective;
        hi = data[1].effective;
    } else if (field == 1) {
        lo = data[0].permitted;
        hi = data[1].permitted;
    } else {
        lo = data[0].inheritable;
        hi = data[1].inheritable;
    }
    return lo | (hi << 32);
}

int capget_impl(cap_user_header_t header, cap_user_data_t data) {
    struct cred *cred = get_current_cred();

    if (!header || !data) {
        errno = EFAULT;
        return -1;
    }
    if (header->version != _LINUX_CAPABILITY_VERSION_3) {
        header->version = _LINUX_CAPABILITY_VERSION_3;
        errno = EINVAL;
        return -1;
    }
    if (header->pid != 0) {
        errno = ESRCH;
        return -1;
    }

    memset(data, 0, sizeof(struct __user_cap_data_struct) * _LINUX_CAPABILITY_U32S_3);
    cred_cap_split(cred->cap_effective, data, 0);
    cred_cap_split(cred->cap_permitted, data, 1);
    cred_cap_split(cred->cap_inheritable, data, 2);
    return 0;
}

int capset_impl(cap_user_header_t header, const cap_user_data_t data) {
    struct cred *cred = get_current_cred();
    uint64_t full = cred_full_cap_mask();
    uint64_t effective;
    uint64_t permitted;
    uint64_t inheritable;

    if (!header || !data) {
        errno = EFAULT;
        return -1;
    }
    if (header->version != _LINUX_CAPABILITY_VERSION_3) {
        header->version = _LINUX_CAPABILITY_VERSION_3;
        errno = EINVAL;
        return -1;
    }
    if (header->pid != 0) {
        errno = ESRCH;
        return -1;
    }

    effective = cred_cap_join(data, 0) & full;
    permitted = cred_cap_join(data, 1) & full;
    inheritable = cred_cap_join(data, 2) & full;

    if ((effective & ~permitted) != 0) {
        errno = EPERM;
        return -1;
    }
    if ((permitted & ~cred->cap_permitted) != 0 && !cred_has_cap(cred, CAP_SETPCAP)) {
        errno = EPERM;
        return -1;
    }
    if ((inheritable & ~cred->cap_bounding) != 0) {
        errno = EPERM;
        return -1;
    }

    cred->cap_effective = effective;
    cred->cap_permitted = permitted;
    cred->cap_inheritable = inheritable;
    cred->cap_ambient &= permitted & inheritable;
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

__attribute__((visibility("default"))) int seteuid(uid_t euid) {
    return seteuid_impl(euid);
}

__attribute__((visibility("default"))) int setegid(gid_t egid) {
    return setegid_impl(egid);
}

__attribute__((visibility("default"))) int setresuid(uid_t ruid, uid_t euid, uid_t suid) {
    return setresuid_impl(ruid, euid, suid);
}

__attribute__((visibility("default"))) int setresgid(gid_t rgid, gid_t egid, gid_t sgid) {
    return setresgid_impl(rgid, egid, sgid);
}

__attribute__((visibility("default"))) int setreuid(uid_t ruid, uid_t euid) {
    return setreuid_impl(ruid, euid);
}

__attribute__((visibility("default"))) int setregid(gid_t rgid, gid_t egid) {
    return setregid_impl(rgid, egid);
}

__attribute__((visibility("default"))) int getresuid(uid_t *ruid, uid_t *euid, uid_t *suid) {
    return getresuid_impl(ruid, euid, suid);
}

__attribute__((visibility("default"))) int getresgid(gid_t *rgid, gid_t *egid, gid_t *sgid) {
    return getresgid_impl(rgid, egid, sgid);
}

__attribute__((visibility("default"))) uid_t setfsuid(uid_t fsuid) {
    return setfsuid_impl(fsuid);
}

__attribute__((visibility("default"))) gid_t setfsgid(gid_t fsgid) {
    return setfsgid_impl(fsgid);
}

__attribute__((visibility("default"))) int getgroups(int size, gid_t list[]) {
    return getgroups_impl(size, list);
}

__attribute__((visibility("default"))) int setgroups(int size, const gid_t *list) {
    return setgroups_impl(size, list);
}

__attribute__((visibility("default"))) int prctl(int option, ...) {
    va_list ap;
    unsigned long arg2;
    unsigned long arg3;
    unsigned long arg4;
    unsigned long arg5;
    int ret;

    va_start(ap, option);
    arg2 = va_arg(ap, unsigned long);
    arg3 = va_arg(ap, unsigned long);
    arg4 = va_arg(ap, unsigned long);
    arg5 = va_arg(ap, unsigned long);
    va_end(ap);

    ret = prctl_impl(option, arg2, arg3, arg4, arg5);
    return ret;
}

__attribute__((visibility("default"))) int capget(cap_user_header_t header, cap_user_data_t data) {
    return capget_impl(header, data);
}

__attribute__((visibility("default"))) int capset(cap_user_header_t header, const cap_user_data_t data) {
    return capset_impl(header, data);
}
