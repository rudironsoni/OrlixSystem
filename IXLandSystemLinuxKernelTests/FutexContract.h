#ifndef FUTEX_CONTRACT_H
#define FUTEX_CONTRACT_H

#ifdef __cplusplus
extern "C" {
#endif

int futex_contract_wait_mismatch_returns_again(void);
int futex_contract_wake_without_waiters_returns_zero(void);
int futex_contract_wait_timeout_returns_timedout(void);
int futex_contract_wake_releases_waiter(void);
int futex_contract_interrupted_wait_records_restart(void);
int futex_contract_sets_and_gets_robust_list(void);
int futex_contract_rejects_missing_robust_list_outputs(void);
int futex_contract_exit_clears_child_tid_and_marks_robust_futex(void);
int futex_contract_clone_thread_shares_vm_and_thread_group(void);
int futex_contract_clone3_sets_parent_child_and_clear_tid(void);
int futex_contract_clear_child_tid_is_per_thread_not_mm_shared(void);

#ifdef __cplusplus
}
#endif

#endif
