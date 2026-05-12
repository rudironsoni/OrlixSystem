/* OrlixKernel/kernel/cred.h
 * Credential subsystem owner header
 *
 * This is the kernel-owned private credential surface for Linux-shaped
 * identity, capability, and exec-credential behavior inside OrlixKernel.
 */

#ifndef KERNEL_CRED_H
#define KERNEL_CRED_H

#include <linux/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct cred;

int cred_init(void);
struct cred *alloc_cred(void);
void free_cred(struct cred *cred);
struct cred *dup_cred(const struct cred *cred);
void cred_init_defaults(struct cred *cred);
void cred_reset_to_defaults(void);
struct cred *get_current_cred(void);
void set_current_cred(struct cred *cred);
void get_cred(struct cred *cred);
void put_cred(struct cred *cred);

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

__kernel_uid32_t getuid_impl(void);
__kernel_uid32_t geteuid_impl(void);
__kernel_gid32_t getgid_impl(void);
__kernel_gid32_t getegid_impl(void);
int setuid_impl(__kernel_uid32_t uid);
int setgid_impl(__kernel_gid32_t gid);
int seteuid_impl(__kernel_uid32_t euid);
int setegid_impl(__kernel_gid32_t egid);
int setresuid_impl(__kernel_uid32_t ruid, __kernel_uid32_t euid, __kernel_uid32_t suid);
int setresgid_impl(__kernel_gid32_t rgid, __kernel_gid32_t egid, __kernel_gid32_t sgid);
int setreuid_impl(__kernel_uid32_t ruid, __kernel_uid32_t euid);
int setregid_impl(__kernel_gid32_t rgid, __kernel_gid32_t egid);
int getresuid_impl(__kernel_uid32_t *ruid, __kernel_uid32_t *euid, __kernel_uid32_t *suid);
int getresgid_impl(__kernel_gid32_t *rgid, __kernel_gid32_t *egid, __kernel_gid32_t *sgid);
__kernel_uid32_t setfsuid_impl(__kernel_uid32_t fsuid);
__kernel_gid32_t setfsgid_impl(__kernel_gid32_t fsgid);
int getgroups_impl(int size, __kernel_gid32_t list[]);
int setgroups_impl(int size, const __kernel_gid32_t *list);
int prctl_impl(int option, unsigned long arg2, unsigned long arg3, unsigned long arg4, unsigned long arg5);

#ifdef __cplusplus
}
#endif

#endif
