/* OrlixKernel/kernel/cred.c
 * Canonical owner for user/group identity syscalls
 *
 * Implements virtual Linux-shaped credentials within OrlixKernel boundaries.
 * Does NOT use host credential system.
 * Does NOT include Darwin headers.
 *
 * Linux ABI contract:
 * - getuid(), geteuid(), getgid(), getegid()
 * - setuid(), setgid()
 */

#include <linux/atomic.h>
#include <linux/capability.h>
#include <linux/errno.h>
#include <linux/gfp_types.h>
#include <linux/securebits.h>
#include <linux/stat.h>
#include <linux/string.h>

#include <linux/limits.h>
#include <uapi/linux/prctl.h>

extern void *__kmalloc_noprof(size_t size, gfp_t flags);
extern void kfree(const void *objp);

#include "../private/kernel/task_state.h"
#include "cred.h"
#include "task.h"

/* ============================================================================
 * VIRTUAL CREDENTIAL SUBSYSTEM
 * ============================================================================
 * Private credential state internal to OrlixKernel runtime.
 */

/* Default Orlix virtual credential values
 * These are deterministic Orlix-internal values, NOT host identity */
#define KERNEL_DEFAULT_UID      0       /* Virtual root user */
#define KERNEL_DEFAULT_GID      0       /* Virtual root group */
#define KERNEL_DEFAULT_EUID     0       /* Virtual effective root */
#define KERNEL_DEFAULT_EGID     0       /* Virtual effective root */
#define KERNEL_DEFAULT_SUID     0       /* Virtual saved root */
#define KERNEL_DEFAULT_SGID     0       /* Virtual saved root */
#define KERNEL_DEFAULT_FSUID    0       /* Virtual filesystem root */
#define KERNEL_DEFAULT_FSGID    0       /* Virtual filesystem root */

static uint64_t cred_full_cap_mask(void) {
    return (CAP_LAST_CAP >= 63) ? ~0ULL : ((1ULL << (CAP_LAST_CAP + 1)) - 1ULL);
}

static uint64_t cred_cap_bit(int cap) {
    if (!cap_valid(cap) || cap >= 64) {
        return 0;
    }
    return 1ULL << cap;
}

static atomic64_t cred_next_user_ns_id = ATOMIC64_INIT(2);

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
    .user_ns_id = 1,
    .uid_map_inside = 0,
    .uid_map_outside = 0,
    .uid_map_count = ~0U,
    .gid_map_inside = 0,
    .gid_map_outside = 0,
    .gid_map_count = ~0U,
    .setgroups_allowed = true,
    .refs = 1
};

/* Current task's credential pointer (thread-local in real implementation) */
static __thread struct cred *current_cred = NULL;

/* ============================================================================
 * VIRTUAL CREDENTIAL LIFECYCLE IMPLEMENTATIONS
 * ============================================================================ */

struct cred *alloc_cred(void) {
    struct cred *cred = __kmalloc_noprof(sizeof(struct cred), GFP_KERNEL | __GFP_ZERO);
    if (cred) {
        cred_init_defaults(cred);
        cred->refs = 1;
    }
    return cred;
}

void free_cred(struct cred *cred) {
    if (cred) {
        kfree(cred->groups);
        kfree(cred);
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
            new->groups = __kmalloc_noprof(cred->group_count * sizeof(__kernel_gid32_t),
                                           GFP_KERNEL | __GFP_ZERO);
            if (!new->groups) {
                free_cred(new);
                return NULL;
            }
            memcpy(new->groups, cred->groups, cred->group_count * sizeof(__kernel_gid32_t));
            new->group_count = cred->group_count;
        }
        new->no_new_privs = cred->no_new_privs;
        new->securebits = cred->securebits;
        new->cap_permitted = cred->cap_permitted;
        new->cap_effective = cred->cap_effective;
        new->cap_inheritable = cred->cap_inheritable;
        new->cap_bounding = cred->cap_bounding;
        new->cap_ambient = cred->cap_ambient;
        new->user_ns_id = cred->user_ns_id;
        new->uid_map_inside = cred->uid_map_inside;
        new->uid_map_outside = cred->uid_map_outside;
        new->uid_map_count = cred->uid_map_count;
        new->gid_map_inside = cred->gid_map_inside;
        new->gid_map_outside = cred->gid_map_outside;
        new->gid_map_count = cred->gid_map_count;
        new->setgroups_allowed = cred->setgroups_allowed;
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
    cred->user_ns_id = 1;
    cred->uid_map_inside = 0;
    cred->uid_map_outside = 0;
    cred->uid_map_count = ~0U;
    cred->gid_map_inside = 0;
    cred->gid_map_outside = 0;
    cred->gid_map_count = ~0U;
    cred->setgroups_allowed = true;
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

/* Reset global credentials to Orlix defaults - for testing */
void cred_reset_to_defaults(void) {
    struct task *task;

    /* Reset global init cred to defaults */
    global_init_cred.uid = KERNEL_DEFAULT_UID;
    global_init_cred.gid = KERNEL_DEFAULT_GID;
    global_init_cred.euid = KERNEL_DEFAULT_EUID;
    global_init_cred.egid = KERNEL_DEFAULT_EGID;
    global_init_cred.suid = KERNEL_DEFAULT_SUID;
    global_init_cred.sgid = KERNEL_DEFAULT_SGID;
    global_init_cred.fsuid = KERNEL_DEFAULT_FSUID;
    global_init_cred.fsgid = KERNEL_DEFAULT_FSGID;
    kfree(global_init_cred.groups);
    global_init_cred.groups = NULL;
    global_init_cred.group_count = 0;
    global_init_cred.no_new_privs = false;
    global_init_cred.securebits = SECUREBITS_DEFAULT;
    global_init_cred.user_ns_id = 1;
    global_init_cred.uid_map_inside = 0;
    global_init_cred.uid_map_outside = 0;
    global_init_cred.uid_map_count = ~0U;
    global_init_cred.gid_map_inside = 0;
    global_init_cred.gid_map_outside = 0;
    global_init_cred.gid_map_count = ~0U;
    global_init_cred.setgroups_allowed = true;
    cred_reset_caps_for_root(&global_init_cred);
    global_init_cred.refs = 1;
    
    /* Reset current_cred pointer to point to global_init_cred */
    current_cred = &global_init_cred;
    task = task_current();
    if (task && task->cred != &global_init_cred) {
        put_cred(task->cred);
        get_cred(&global_init_cred);
        task->cred = &global_init_cred;
    }
}

struct cred *get_current_cred(void) {
    struct task *task = task_current();
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
    struct task *task = task_current();
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
 * These implement Linux-shaped semantics within OrlixKernel.
 * No host privilege changes are attempted.
 */

static int check_setuid_perm(struct cred *cred, __kernel_uid32_t uid) {
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

static int check_setgid_perm(struct cred *cred, __kernel_gid32_t gid) {
    (void)gid;
    if (cred_has_cap(cred, CAP_SETGID)) {
        return 0; /* Root can setgid to anything */
    }
    if (gid == cred->gid || gid == cred->egid || gid == cred->sgid) {
        return 0;
    }
    return -EPERM;
}

int cred_setuid(struct cred *cred, __kernel_uid32_t uid) {
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

int cred_setgid(struct cred *cred, __kernel_gid32_t gid) {
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

int cred_seteuid(struct cred *cred, __kernel_uid32_t euid) {
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

int cred_setegid(struct cred *cred, __kernel_gid32_t egid) {
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

int cred_setreuid(struct cred *cred, __kernel_uid32_t ruid, __kernel_uid32_t euid) {
    if (!cred) {
        return -EINVAL;
    }

    /* setreuid requires permission to set both values */
    if (!cred_has_cap(cred, CAP_SETUID)) {
        if (ruid != (__kernel_uid32_t)-1 && ruid != cred->uid && ruid != cred->euid && ruid != cred->suid) {
            return -EPERM;
        }
        if (euid != (__kernel_uid32_t)-1 && euid != cred->uid && euid != cred->euid && euid != cred->suid) {
            return -EPERM;
        }
    }

    if (ruid != (__kernel_uid32_t)-1) {
        cred->uid = ruid;
    }
    if (euid != (__kernel_uid32_t)-1) {
        /* Update saved UID under certain conditions */
        if (ruid != (__kernel_uid32_t)-1 && cred->euid != ruid && cred->euid == cred->uid) {
            cred->suid = cred->euid;
        }
        cred->euid = euid;
        cred->fsuid = euid;
    }
    cred_apply_uid_cap_fixup(cred);

    return 0;
}

int cred_setregid(struct cred *cred, __kernel_gid32_t rgid, __kernel_gid32_t egid) {
    if (!cred) {
        return -EINVAL;
    }

    if (!cred_has_cap(cred, CAP_SETGID)) {
        if (rgid != (__kernel_gid32_t)-1 && rgid != cred->gid && rgid != cred->egid && rgid != cred->sgid) {
            return -EPERM;
        }
        if (egid != (__kernel_gid32_t)-1 && egid != cred->gid && egid != cred->egid && egid != cred->sgid) {
            return -EPERM;
        }
    }

    if (rgid != (__kernel_gid32_t)-1) {
        cred->gid = rgid;
    }
    if (egid != (__kernel_gid32_t)-1) {
        if (rgid != (__kernel_gid32_t)-1 && cred->egid != rgid && cred->egid == cred->gid) {
            cred->sgid = cred->egid;
        }
        cred->egid = egid;
        cred->fsgid = egid;
    }

    return 0;
}

int cred_setresuid(struct cred *cred, __kernel_uid32_t ruid, __kernel_uid32_t euid, __kernel_uid32_t suid) {
    if (!cred) {
        return -EINVAL;
    }

    /* setresuid is privileged; only root or allowed transitions */
    if (!cred_has_cap(cred, CAP_SETUID)) {
        /* 
         * Non-root can only use current values when -1 is specified,
         * or switch to values they already have.
         */
        if (ruid != (__kernel_uid32_t)-1 && ruid != cred->uid && ruid != cred->euid && ruid != cred->suid) {
            return -EPERM;
        }
        if (euid != (__kernel_uid32_t)-1 && euid != cred->uid && euid != cred->euid && euid != cred->suid) {
            return -EPERM;
        }
        if (suid != (__kernel_uid32_t)-1 && suid != cred->uid && suid != cred->euid && suid != cred->suid) {
            return -EPERM;
        }
    }

    if (ruid != (__kernel_uid32_t)-1) {
        cred->uid = ruid;
    }
    if (euid != (__kernel_uid32_t)-1) {
        cred->euid = euid;
        cred->fsuid = euid;
    }
    if (suid != (__kernel_uid32_t)-1) {
        cred->suid = suid;
    }
    cred_apply_uid_cap_fixup(cred);

    return 0;
}

int cred_setresgid(struct cred *cred, __kernel_gid32_t rgid, __kernel_gid32_t egid, __kernel_gid32_t sgid) {
    if (!cred) {
        return -EINVAL;
    }

    if (!cred_has_cap(cred, CAP_SETGID)) {
        if (rgid != (__kernel_gid32_t)-1 && rgid != cred->gid && rgid != cred->egid && rgid != cred->sgid) {
            return -EPERM;
        }
        if (egid != (__kernel_gid32_t)-1 && egid != cred->gid && egid != cred->egid && egid != cred->sgid) {
            return -EPERM;
        }
        if (sgid != (__kernel_gid32_t)-1 && sgid != cred->gid && sgid != cred->egid && sgid != cred->sgid) {
            return -EPERM;
        }
    }

    if (rgid != (__kernel_gid32_t)-1) {
        cred->gid = rgid;
    }
    if (egid != (__kernel_gid32_t)-1) {
        cred->egid = egid;
        cred->fsgid = egid;
    }
    if (sgid != (__kernel_gid32_t)-1) {
        cred->sgid = sgid;
    }

    return 0;
}

bool cred_has_group(const struct cred *cred, __kernel_gid32_t gid) {
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

int cred_setgroups(struct cred *cred, size_t size, const __kernel_gid32_t *list) {
    __kernel_gid32_t *new_groups = NULL;

    if (!cred) {
        return -EINVAL;
    }
    if (!cred->setgroups_allowed) {
        return -EPERM;
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
        new_groups = __kmalloc_noprof((size_t)size * sizeof(__kernel_gid32_t),
                                      GFP_KERNEL | __GFP_ZERO);
        if (!new_groups) {
            return -ENOMEM;
        }
        memcpy(new_groups, list, size * sizeof(__kernel_gid32_t));
    }

    kfree(cred->groups);
    cred->groups = new_groups;
    cred->group_count = size;
    return 0;
}

static int cred_parse_id_map_line(const char *buf, size_t count,
                                  uint32_t *inside, uint32_t *outside, uint32_t *extent) {
    uint64_t values[3] = {0, 0, 0};
    size_t index = 0;
    size_t pos = 0;

    if (!buf || !inside || !outside || !extent) {
        return -EFAULT;
    }
    while (pos < count && (buf[pos] == ' ' || buf[pos] == '\t' || buf[pos] == '\n')) {
        pos++;
    }
    while (index < 3) {
        bool saw_digit = false;
        uint64_t value = 0;
        while (pos < count && buf[pos] >= '0' && buf[pos] <= '9') {
            saw_digit = true;
            value = (value * 10U) + (uint64_t)(buf[pos] - '0');
            if (value > ~0U) {
                return -EINVAL;
            }
            pos++;
        }
        if (!saw_digit) {
            return -EINVAL;
        }
        values[index++] = value;
        while (pos < count && (buf[pos] == ' ' || buf[pos] == '\t')) {
            pos++;
        }
    }
    while (pos < count && (buf[pos] == ' ' || buf[pos] == '\t' || buf[pos] == '\n' || buf[pos] == '\0')) {
        pos++;
    }
    if (pos != count || values[2] == 0) {
        return -EINVAL;
    }
    *inside = (uint32_t)values[0];
    *outside = (uint32_t)values[1];
    *extent = (uint32_t)values[2];
    return 0;
}

int cred_write_uid_map(struct cred *cred, const char *buf, size_t count) {
    uint32_t inside;
    uint32_t outside;
    uint32_t extent;
    int ret;

    if (!cred) {
        return -EINVAL;
    }
    ret = cred_parse_id_map_line(buf, count, &inside, &outside, &extent);
    if (ret != 0) {
        return ret;
    }
    cred->uid_map_inside = inside;
    cred->uid_map_outside = outside;
    cred->uid_map_count = extent;
    return 0;
}

int cred_write_gid_map(struct cred *cred, const char *buf, size_t count) {
    uint32_t inside;
    uint32_t outside;
    uint32_t extent;
    int ret;

    if (!cred) {
        return -EINVAL;
    }
    if (cred->setgroups_allowed) {
        return -EPERM;
    }
    ret = cred_parse_id_map_line(buf, count, &inside, &outside, &extent);
    if (ret != 0) {
        return ret;
    }
    cred->gid_map_inside = inside;
    cred->gid_map_outside = outside;
    cred->gid_map_count = extent;
    return 0;
}

int cred_write_setgroups(struct cred *cred, const char *buf, size_t count) {
    if (!cred || (!buf && count > 0)) {
        return -EFAULT;
    }
    while (count > 0 && (buf[count - 1] == '\n' || buf[count - 1] == '\0' ||
                         buf[count - 1] == ' ' || buf[count - 1] == '\t')) {
        count--;
    }
    while (count > 0 && (*buf == ' ' || *buf == '\t')) {
        buf++;
        count--;
    }
    if (count == 4 && strncmp(buf, "deny", 4) == 0) {
        cred->setgroups_allowed = false;
        kfree(cred->groups);
        cred->groups = NULL;
        cred->group_count = 0;
        return 0;
    }
    if (count == 5 && strncmp(buf, "allow", 5) == 0) {
        if (!cred_has_cap(cred, CAP_SETGID)) {
            return -EPERM;
        }
        cred->setgroups_allowed = true;
        return 0;
    }
    return -EINVAL;
}

const char *cred_setgroups_state(const struct cred *cred) {
    return (!cred || cred->setgroups_allowed) ? "allow" : "deny";
}

void cred_apply_exec_metadata(struct cred *cred, __kernel_uid32_t file_uid,
                              __kernel_gid32_t file_gid, uint32_t mode) {
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

bool cred_has_cap_in_user_namespace(const struct cred *cred, uint64_t user_ns_id, int cap) {
    if (!cred || user_ns_id == 0 || cred->user_ns_id != user_ns_id) {
        return false;
    }
    return cred_has_cap(cred, cap);
}

uint64_t cred_user_namespace_id(const struct cred *cred) {
    return cred ? cred->user_ns_id : 0;
}

int cred_unshare_user_namespace(struct cred *cred) {
    if (!cred) {
        return -EINVAL;
    }
    cred->user_ns_id = (uint64_t)atomic64_inc_return(&cred_next_user_ns_id) - 1ULL;
    cred->uid_map_inside = 0;
    cred->uid_map_outside = cred->uid;
    cred->uid_map_count = 1;
    cred->gid_map_inside = 0;
    cred->gid_map_outside = cred->gid;
    cred->gid_map_count = 1;
    cred->setgroups_allowed = true;
    cred_reset_caps_for_root(cred);
    cred->uid = 0;
    cred->euid = 0;
    cred->suid = 0;
    cred->fsuid = 0;
    cred->gid = 0;
    cred->egid = 0;
    cred->sgid = 0;
    cred->fsgid = 0;
    return 0;
}

/* ============================================================================
 * LINUX-FACING PUBLIC SYSCALL ENTRIES
 * ============================================================================
 * These export the Linux ABI contract from the kernel owner.
 * They return Orlix virtual credential state, NOT host identities.
 */

/* Implementation of getuid_impl - internal entry point */
__kernel_uid32_t getuid_impl(void) {
    struct cred *cred = get_current_cred();
    return (__kernel_uid32_t)cred->uid;
}

/* Implementation of geteuid_impl - internal entry point */
__kernel_uid32_t geteuid_impl(void) {
    struct cred *cred = get_current_cred();
    return (__kernel_uid32_t)cred->euid;
}

/* Implementation of getgid_impl - internal entry point */
__kernel_gid32_t getgid_impl(void) {
    struct cred *cred = get_current_cred();
    return (__kernel_gid32_t)cred->gid;
}

/* Implementation of getegid_impl - internal entry point */
__kernel_gid32_t getegid_impl(void) {
    struct cred *cred = get_current_cred();
    return (__kernel_gid32_t)cred->egid;
}

/* Implementation of setuid_impl - internal entry point */
int setuid_impl(__kernel_uid32_t uid) {
    struct cred *cred = get_current_cred();
    return cred_setuid(cred, (uint32_t)uid);
}

/* Implementation of setgid_impl - internal entry point */
int setgid_impl(__kernel_gid32_t gid) {
    struct cred *cred = get_current_cred();
    return cred_setgid(cred, (uint32_t)gid);
}

int seteuid_impl(__kernel_uid32_t euid) {
    struct cred *cred = get_current_cred();
    return cred_seteuid(cred, (uint32_t)euid);
}

int setegid_impl(__kernel_gid32_t egid) {
    struct cred *cred = get_current_cred();
    return cred_setegid(cred, (uint32_t)egid);
}

int setresuid_impl(__kernel_uid32_t ruid, __kernel_uid32_t euid, __kernel_uid32_t suid) {
    struct cred *cred = get_current_cred();
    return cred_setresuid(cred, (uint32_t)ruid, (uint32_t)euid, (uint32_t)suid);
}

int setresgid_impl(__kernel_gid32_t rgid, __kernel_gid32_t egid, __kernel_gid32_t sgid) {
    struct cred *cred = get_current_cred();
    return cred_setresgid(cred, (uint32_t)rgid, (uint32_t)egid, (uint32_t)sgid);
}

int setreuid_impl(__kernel_uid32_t ruid, __kernel_uid32_t euid) {
    struct cred *cred = get_current_cred();
    return cred_setreuid(cred, (uint32_t)ruid, (uint32_t)euid);
}

int setregid_impl(__kernel_gid32_t rgid, __kernel_gid32_t egid) {
    struct cred *cred = get_current_cred();
    return cred_setregid(cred, (uint32_t)rgid, (uint32_t)egid);
}

int getresuid_impl(__kernel_uid32_t *ruid, __kernel_uid32_t *euid, __kernel_uid32_t *suid) {
    struct cred *cred = get_current_cred();

    if (!ruid || !euid || !suid) {
        return -EFAULT;
    }
    *ruid = (__kernel_uid32_t)cred->uid;
    *euid = (__kernel_uid32_t)cred->euid;
    *suid = (__kernel_uid32_t)cred->suid;
    return 0;
}

int getresgid_impl(__kernel_gid32_t *rgid, __kernel_gid32_t *egid, __kernel_gid32_t *sgid) {
    struct cred *cred = get_current_cred();

    if (!rgid || !egid || !sgid) {
        return -EFAULT;
    }
    *rgid = (__kernel_gid32_t)cred->gid;
    *egid = (__kernel_gid32_t)cred->egid;
    *sgid = (__kernel_gid32_t)cred->sgid;
    return 0;
}

__kernel_uid32_t setfsuid_impl(__kernel_uid32_t fsuid) {
    struct cred *cred = get_current_cred();
    __kernel_uid32_t old;

    if (!cred) {
        return (__kernel_uid32_t)-1;
    }
    old = (__kernel_uid32_t)cred->fsuid;
    if (cred_has_cap(cred, CAP_SETUID) ||
        fsuid == cred->uid || fsuid == cred->euid ||
        fsuid == cred->suid || fsuid == cred->fsuid) {
        cred->fsuid = (uint32_t)fsuid;
    }
    return old;
}

__kernel_gid32_t setfsgid_impl(__kernel_gid32_t fsgid) {
    struct cred *cred = get_current_cred();
    __kernel_gid32_t old;

    if (!cred) {
        return (__kernel_gid32_t)-1;
    }
    old = (__kernel_gid32_t)cred->fsgid;
    if (cred_has_cap(cred, CAP_SETGID) ||
        fsgid == cred->gid || fsgid == cred->egid ||
        fsgid == cred->sgid || fsgid == cred->fsgid) {
        cred->fsgid = (uint32_t)fsgid;
    }
    return old;
}

int getgroups_impl(int size, __kernel_gid32_t list[]) {
    struct cred *cred = get_current_cred();

    if (!cred) {
        return -EINVAL;
    }
    if (size < 0) {
        return -EINVAL;
    }
    if (size == 0) {
        return (int)cred->group_count;
    }
    if (!list) {
        return -EFAULT;
    }
    if ((size_t)size < cred->group_count) {
        return -EINVAL;
    }

    for (size_t i = 0; i < cred->group_count; i++) {
        list[i] = cred->groups[i];
    }
    return (int)cred->group_count;
}

int setgroups_impl(int size, const __kernel_gid32_t *list) {
    struct cred *cred = get_current_cred();

    if (size < 0) {
        return -EINVAL;
    }

    return cred_setgroups(cred, (size_t)size, list);
}

int prctl_impl(int option, unsigned long arg2, unsigned long arg3, unsigned long arg4, unsigned long arg5) {
    struct cred *cred = get_current_cred();

    switch (option) {
    case PR_CAPBSET_READ: {
        uint64_t bit = cred_cap_bit((int)arg2);

        if (arg3 != 0 || arg4 != 0 || arg5 != 0 || bit == 0) {
            return -EINVAL;
        }
        return (cred->cap_bounding & bit) != 0 ? 1 : 0;
    }
    case PR_CAPBSET_DROP: {
        uint64_t bit = cred_cap_bit((int)arg2);

        if (arg3 != 0 || arg4 != 0 || arg5 != 0 || bit == 0) {
            return -EINVAL;
        }
        if (!cred_has_cap(cred, CAP_SETPCAP)) {
            return -EPERM;
        }
        cred->cap_bounding &= ~bit;
        cred->cap_ambient &= ~bit;
        return 0;
    }
    case PR_CAP_AMBIENT: {
        uint64_t bit = cred_cap_bit((int)arg3);

        if (arg5 != 0) {
            return -EINVAL;
        }
        switch (arg2) {
        case PR_CAP_AMBIENT_IS_SET:
            if (arg4 != 0 || bit == 0) {
                return -EINVAL;
            }
            return (cred->cap_ambient & bit) != 0 ? 1 : 0;
        case PR_CAP_AMBIENT_RAISE:
            if (arg4 != 0 || bit == 0) {
                return -EINVAL;
            }
            if ((cred->securebits & SECBIT_NO_CAP_AMBIENT_RAISE) != 0) {
                return -EPERM;
            }
            if ((cred->cap_permitted & bit) == 0 || (cred->cap_inheritable & bit) == 0) {
                return -EPERM;
            }
            cred->cap_ambient |= bit;
            return 0;
        case PR_CAP_AMBIENT_LOWER:
            if (arg4 != 0 || bit == 0) {
                return -EINVAL;
            }
            cred->cap_ambient &= ~bit;
            return 0;
        case PR_CAP_AMBIENT_CLEAR_ALL:
            if (arg3 != 0 || arg4 != 0) {
                return -EINVAL;
            }
            cred->cap_ambient = 0;
            return 0;
        default:
            return -EINVAL;
        }
    }
    case PR_GET_KEEPCAPS:
        if (arg2 != 0 || arg3 != 0 || arg4 != 0 || arg5 != 0) {
            return -EINVAL;
        }
        return cred_keepcaps(cred) ? 1 : 0;
    case PR_SET_KEEPCAPS:
        if (arg2 > 1 || arg3 != 0 || arg4 != 0 || arg5 != 0) {
            return -EINVAL;
        }
        if ((cred->securebits & SECBIT_KEEP_CAPS_LOCKED) != 0) {
            return -EPERM;
        }
        if (arg2 == 1) {
            cred->securebits |= SECBIT_KEEP_CAPS;
        } else {
            cred->securebits &= ~SECBIT_KEEP_CAPS;
        }
        return 0;
    case PR_GET_SECUREBITS:
        if (arg2 != 0 || arg3 != 0 || arg4 != 0 || arg5 != 0) {
            return -EINVAL;
        }
        return (int)cred->securebits;
    case PR_SET_SECUREBITS: {
        unsigned long valid = SECURE_ALL_BITS | SECURE_ALL_LOCKS;
        uint32_t requested = (uint32_t)arg2;
        uint32_t locked = cred->securebits & SECURE_ALL_LOCKS;

        if ((arg2 & ~valid) != 0 || arg3 != 0 || arg4 != 0 || arg5 != 0) {
            return -EINVAL;
        }
        if (!cred_has_cap(cred, CAP_SETPCAP)) {
            return -EPERM;
        }
        if ((requested & locked) != locked) {
            return -EPERM;
        }
        if (((requested ^ cred->securebits) & (locked >> 1)) != 0) {
            return -EPERM;
        }
        cred->securebits = requested;
        return 0;
    }
    case PR_SET_NO_NEW_PRIVS:
        if (arg2 != 1 || arg3 != 0 || arg4 != 0 || arg5 != 0) {
            return -EINVAL;
        }
        if (cred_set_no_new_privs(cred) < 0) {
            return -EINVAL;
        }
        return 0;
    case PR_GET_NO_NEW_PRIVS:
        if (arg2 != 0 || arg3 != 0 || arg4 != 0 || arg5 != 0) {
            return -EINVAL;
        }
        return cred_no_new_privs(cred) ? 1 : 0;
    default:
        return -EINVAL;
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
        return -EFAULT;
    }
    if (header->version != _LINUX_CAPABILITY_VERSION_3) {
        header->version = _LINUX_CAPABILITY_VERSION_3;
        return -EINVAL;
    }
    if (header->pid != 0) {
        return -ESRCH;
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
        return -EFAULT;
    }
    if (header->version != _LINUX_CAPABILITY_VERSION_3) {
        header->version = _LINUX_CAPABILITY_VERSION_3;
        return -EINVAL;
    }
    if (header->pid != 0) {
        return -ESRCH;
    }

    effective = cred_cap_join(data, 0) & full;
    permitted = cred_cap_join(data, 1) & full;
    inheritable = cred_cap_join(data, 2) & full;

    if ((effective & ~permitted) != 0) {
        return -EPERM;
    }
    if ((permitted & ~cred->cap_permitted) != 0 && !cred_has_cap(cred, CAP_SETPCAP)) {
        return -EPERM;
    }
    if ((inheritable & ~cred->cap_bounding) != 0) {
        return -EPERM;
    }

    cred->cap_effective = effective;
    cred->cap_permitted = permitted;
    cred->cap_inheritable = inheritable;
    cred->cap_ambient &= permitted & inheritable;
    return 0;
}
