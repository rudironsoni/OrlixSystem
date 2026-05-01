#ifndef NATIVE_SYSCALL_CONTRACT_H
#define NATIVE_SYSCALL_CONTRACT_H

#ifdef __cplusplus
extern "C" {
#endif

int native_syscall_contract_dispatches_fd_pipe_and_procfs(void);
int native_syscall_contract_dispatches_vm_identity_time_and_dirs(void);
int native_syscall_contract_dispatches_process_startup_syscalls(void);
int native_syscall_contract_registers_native_artifact_descriptor(void);
int native_syscall_contract_execs_sbin_init_through_syscall_surface(void);
int native_syscall_contract_returns_raw_negative_errno(void);
int native_syscall_contract_registered_program_uses_syscall_surface(void);

#ifdef __cplusplus
}
#endif

#endif
