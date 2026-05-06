#ifndef IXLANDSYSTEMLINUXKERNELTESTS_SECCOMPCONTRACT_H
#define IXLANDSYSTEMLINUXKERNELTESTS_SECCOMPCONTRACT_H

#ifdef __cplusplus
extern "C" {
#endif

int seccomp_contract_task_errno_policy_denies_syscall_dispatch(void);
int seccomp_contract_unmentioned_syscall_remains_allowed(void);
int seccomp_contract_thread_group_policy_applies_to_thread_peer(void);

#ifdef __cplusplus
}
#endif

#endif /* IXLANDSYSTEMLINUXKERNELTESTS_SECCOMPCONTRACT_H */
