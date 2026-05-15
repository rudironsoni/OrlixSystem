/* OrlixKernel/kernel/cred.h
 * Credential subsystem owner header
 *
 * This is the kernel-owned private credential surface for Linux-shaped
 * identity, capability, and exec-credential behavior inside OrlixKernel.
 */

#ifndef KERNEL_CRED_H
#define KERNEL_CRED_H

#include <linux/types.h>
#include <linux/capability.h>

#ifdef __cplusplus
extern "C" {
#endif

struct cred;

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
int capget_impl(cap_user_header_t header, cap_user_data_t data);
int capset_impl(cap_user_header_t header, const cap_user_data_t data);
int prctl_impl(int option, unsigned long arg2, unsigned long arg3, unsigned long arg4, unsigned long arg5);

#ifdef __cplusplus
}
#endif

#endif
