#ifndef PRIVATE_KERNEL_CRED_STATE_H
#define PRIVATE_KERNEL_CRED_STATE_H

#include "kernel/cred.h"

#ifdef __cplusplus
extern "C" {
#endif

int cred_system_init(void);
struct cred *alloc_cred(void);
void free_cred(struct cred *cred);
struct cred *dup_cred(const struct cred *cred);
void cred_init_defaults(struct cred *cred);
void cred_reset_to_defaults(void);
struct cred *cred_current(void);
void set_current_cred(struct cred *cred);
void cred_acquire(struct cred *cred);
void cred_release(struct cred *cred);
int cred_setuid(struct cred *cred, __kernel_uid32_t uid);
int cred_setgid(struct cred *cred, __kernel_gid32_t gid);
int cred_seteuid(struct cred *cred, __kernel_uid32_t euid);
int cred_setegid(struct cred *cred, __kernel_gid32_t egid);
int cred_setreuid(struct cred *cred, __kernel_uid32_t ruid, __kernel_uid32_t euid);
int cred_setregid(struct cred *cred, __kernel_gid32_t rgid, __kernel_gid32_t egid);
int cred_setresuid(struct cred *cred, __kernel_uid32_t ruid, __kernel_uid32_t euid, __kernel_uid32_t suid);
int cred_setresgid(struct cred *cred, __kernel_gid32_t rgid, __kernel_gid32_t egid, __kernel_gid32_t sgid);
bool cred_has_group(const struct cred *cred, __kernel_gid32_t gid);
int cred_setgroups(struct cred *cred, size_t size, const __kernel_gid32_t *list);
int cred_write_uid_map(struct cred *cred, const char *buf, size_t count);
int cred_write_gid_map(struct cred *cred, const char *buf, size_t count);
int cred_write_setgroups(struct cred *cred, const char *buf, size_t count);
const char *cred_setgroups_state(const struct cred *cred);
void cred_apply_exec_metadata(struct cred *cred, __kernel_uid32_t file_uid,
                              __kernel_gid32_t file_gid, uint32_t mode);
void cred_apply_exec_file_capabilities(struct cred *cred, uint64_t permitted,
                                       uint64_t inheritable, bool effective);
bool cred_no_new_privs(const struct cred *cred);
int cred_set_no_new_privs(struct cred *cred);
bool cred_has_cap(const struct cred *cred, int cap);
bool cred_has_cap_in_user_namespace(const struct cred *cred, uint64_t user_ns_id, int cap);
uint64_t cred_user_namespace_id(const struct cred *cred);
int cred_unshare_user_namespace(struct cred *cred);

#ifndef _LINUX_CRED_H
struct cred {
    __kernel_uid32_t uid;
    __kernel_gid32_t gid;
    __kernel_uid32_t euid;
    __kernel_gid32_t egid;
    __kernel_uid32_t suid;
    __kernel_gid32_t sgid;
    __kernel_uid32_t fsuid;
    __kernel_gid32_t fsgid;
    __kernel_gid32_t *groups;
    size_t group_count;
    bool no_new_privs;
    uint32_t securebits;
    uint64_t cap_permitted;
    uint64_t cap_effective;
    uint64_t cap_inheritable;
    uint64_t cap_bounding;
    uint64_t cap_ambient;
    uint64_t user_ns_id;
    __kernel_uid32_t uid_map_inside;
    __kernel_uid32_t uid_map_outside;
    __kernel_uid32_t uid_map_count;
    __kernel_gid32_t gid_map_inside;
    __kernel_gid32_t gid_map_outside;
    __kernel_gid32_t gid_map_count;
    bool setgroups_allowed;
    int refs;
};
#endif

#ifdef __cplusplus
}
#endif

#endif
