/* IXLandSystem/kernel/cred_internal.h
 * Private internal header for virtual credential subsystem
 *
 * This is PRIVATE internal state for the virtual kernel's credential handling.
 * NOT a public Linux ABI header.
 *
 * Virtual credential behavior emulated:
 * - real and effective UID/GID tracking
 * - saved UID/GID for setuid/setgid semantics
 * - credential inheritance on fork/clone
 * - credential reset on exec
 */

#ifndef KERNEL_CRED_INTERNAL_H
#define KERNEL_CRED_INTERNAL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Use Linux-compatible UID/GID types */
typedef uint32_t uid_t;
typedef uint32_t gid_t;

#ifdef __cplusplus
extern "C" {
#endif

/* Virtual credential structure - private internal
 * This is the IXLandSystem-internal credential representation,
 * NOT the Linux UAPI struct cred.
 *
 * Tracks virtual UID/GID identity within IXLand boundaries.
 * Does NOT reflect host iOS identity.
 */
struct cred {
    uint32_t uid;           /* Real user ID */
    uint32_t gid;           /* Real group ID */
    uint32_t euid;          /* Effective user ID */
    uint32_t egid;          /* Effective group ID */
    uint32_t suid;          /* Saved user ID */
    uint32_t sgid;          /* Saved group ID */
    uint32_t fsuid;         /* Filesystem user ID */
    uint32_t fsgid;         /* Filesystem group ID */
    gid_t *groups;          /* Supplementary group IDs */
    size_t group_count;     /* Number of supplementary groups */
    bool no_new_privs;      /* Blocks exec-time privilege gains */
    uint32_t securebits;
    uint64_t cap_permitted;
    uint64_t cap_effective;
    uint64_t cap_inheritable;
    uint64_t cap_bounding;
    uint64_t cap_ambient;
    uint64_t user_ns_id;
    uint32_t uid_map_inside;
    uint32_t uid_map_outside;
    uint32_t uid_map_count;
    uint32_t gid_map_inside;
    uint32_t gid_map_outside;
    uint32_t gid_map_count;

    /* Reference counting for credential sharing */
    int refs;
};

/* ============================================================================
 * VIRTUAL CREDENTIAL LIFECYCLE
 * ============================================================================ */

/* Allocate a new credential structure with default values */
struct cred *alloc_cred(void);

/* Free a credential structure */
void free_cred(struct cred *cred);

/* Duplicate a credential structure (for fork/clone) */
struct cred *dup_cred(const struct cred *cred);

/* ============================================================================
 * VIRTUAL CREDENTIAL INITIALIZATION
 * ============================================================================ */

/* Initialize the credential subsystem */
int cred_init(void);

/* Initialize a credential with IXLand default values
 * Uses deterministic virtual defaults, not host identity */
void cred_init_defaults(struct cred *cred);

/* Reset global credentials to IXLand defaults - for testing only */
void cred_reset_to_defaults(void);

/* ============================================================================
 * VIRTUAL CREDENTIAL OPERATIONS
 * ============================================================================ */

/* Get the current task's credential */
struct cred *get_current_cred(void);

/* Set the current task's credential */
void set_current_cred(struct cred *cred);

/* Reference a credential structure */
void get_cred(struct cred *cred);

/* Release a credential reference */
void put_cred(struct cred *cred);

/* ============================================================================
 * INTERNAL VIRTUAL CREDENTIAL HELPERS
 * ============================================================================ */

/* Virtual setuid implementation - no host privilege change */
int cred_setuid(struct cred *cred, uint32_t uid);

/* Virtual setgid implementation - no host privilege change */
int cred_setgid(struct cred *cred, uint32_t gid);

/* Virtual seteuid implementation */
int cred_seteuid(struct cred *cred, uint32_t euid);

/* Virtual setegid implementation */
int cred_setegid(struct cred *cred, uint32_t egid);

/* Virtual setreuid implementation */
int cred_setreuid(struct cred *cred, uint32_t ruid, uint32_t euid);

/* Virtual setregid implementation */
int cred_setregid(struct cred *cred, uint32_t rgid, uint32_t egid);

/* Virtual setresuid implementation */
int cred_setresuid(struct cred *cred, uint32_t ruid, uint32_t euid, uint32_t suid);

/* Virtual setresgid implementation */
int cred_setresgid(struct cred *cred, uint32_t rgid, uint32_t egid, uint32_t sgid);

/* Virtual supplementary group membership check */
bool cred_has_group(const struct cred *cred, gid_t gid);

/* Virtual setgroups implementation */
int cred_setgroups(struct cred *cred, size_t size, const gid_t *list);

/* Apply executable owner and mode metadata during exec */
void cred_apply_exec_metadata(struct cred *cred, uid_t file_uid, gid_t file_gid, uint32_t mode);

/* Virtual no_new_privs state */
bool cred_no_new_privs(const struct cred *cred);
int cred_set_no_new_privs(struct cred *cred);

/* Virtual capability state */
bool cred_has_cap(const struct cred *cred, int cap);
bool cred_has_cap_in_user_namespace(const struct cred *cred, uint64_t user_ns_id, int cap);
uint64_t cred_user_namespace_id(const struct cred *cred);
int cred_unshare_user_namespace(struct cred *cred);
void cred_apply_exec_file_capabilities(struct cred *cred, uint64_t permitted,
                                       uint64_t inheritable, bool effective);

/* ============================================================================
 * INTERNAL IMPLEMENTATION ENTRY POINTS
 * ============================================================================
 * These are the actual implementations, exposed for internal testing.
 * Tests use _impl() entry points to verify Linux-shaped semantics.
 */

/* Get real UID - virtual implementation */
uid_t getuid_impl(void);

/* Get effective UID - virtual implementation */
uid_t geteuid_impl(void);

/* Get real GID - virtual implementation */
gid_t getgid_impl(void);

/* Get effective GID - virtual implementation */
gid_t getegid_impl(void);

/* Set UID - virtual implementation */
int setuid_impl(uid_t uid);

/* Set GID - virtual implementation */
int setgid_impl(gid_t gid);

/* Set effective UID - virtual implementation */
int seteuid_impl(uid_t euid);

/* Set effective GID - virtual implementation */
int setegid_impl(gid_t egid);

/* Set real/effective/saved UID - virtual implementation */
int setresuid_impl(uid_t ruid, uid_t euid, uid_t suid);

/* Set real/effective/saved GID - virtual implementation */
int setresgid_impl(gid_t rgid, gid_t egid, gid_t sgid);

/* Set real/effective UID - virtual implementation */
int setreuid_impl(uid_t ruid, uid_t euid);

/* Set real/effective GID - virtual implementation */
int setregid_impl(gid_t rgid, gid_t egid);

/* Get real/effective/saved UID - virtual implementation */
int getresuid_impl(uid_t *ruid, uid_t *euid, uid_t *suid);

/* Get real/effective/saved GID - virtual implementation */
int getresgid_impl(gid_t *rgid, gid_t *egid, gid_t *sgid);

/* Set filesystem UID/GID - virtual implementation */
uid_t setfsuid_impl(uid_t fsuid);
gid_t setfsgid_impl(gid_t fsgid);

/* Get supplementary groups - virtual implementation */
int getgroups_impl(int size, gid_t list[]);

/* Set supplementary groups - virtual implementation */
int setgroups_impl(int size, const gid_t *list);

/* prctl - virtual implementation */
int prctl_impl(int option, unsigned long arg2, unsigned long arg3, unsigned long arg4, unsigned long arg5);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_CRED_INTERNAL_H */
