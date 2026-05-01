#ifndef FUTEX_CONTRACT_H
#define FUTEX_CONTRACT_H

#ifdef __cplusplus
extern "C" {
#endif

int futex_contract_wait_mismatch_returns_again(void);
int futex_contract_wake_without_waiters_returns_zero(void);
int futex_contract_wait_timeout_returns_timedout(void);
int futex_contract_wake_releases_waiter(void);

#ifdef __cplusplus
}
#endif

#endif
