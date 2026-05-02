#ifndef SIGNAL_SYSCALL_CONTRACT_H
#define SIGNAL_SYSCALL_CONTRACT_H

#ifdef __cplusplus
extern "C" {
#endif

int signal_syscall_contract_rt_sigaction_uses_linux_uapi_layout(void);
int signal_syscall_contract_sigaltstack_and_frame_policy(void);
int signal_syscall_contract_frame_writes_virtual_record(void);
int signal_syscall_contract_frame_records_handler_handoff(void);
int signal_syscall_contract_frame_records_mask_restorer_and_context(void);

#ifdef __cplusplus
}
#endif

#endif
