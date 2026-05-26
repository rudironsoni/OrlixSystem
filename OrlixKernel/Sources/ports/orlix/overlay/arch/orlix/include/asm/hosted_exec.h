/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_ORLIX_HOSTED_EXEC_H
#define _ASM_ORLIX_HOSTED_EXEC_H

#include <linux/types.h>

struct pt_regs;

#if defined(ORLIX_APP_HOSTED_BOOT)
void orlix_hosted_capture_host_context(void);
void orlix_hosted_save_kernel_stack(unsigned long sp);
void orlix_hosted_preserve_user_tls(void);
unsigned long orlix_hosted_prepare_user_entry(void);
void __noreturn orlix_hosted_enter_user(struct pt_regs *regs);
int orlix_hosted_sync_syscall_gate(void);
void orlix_sync_current_user_mappings(struct pt_regs *regs);
int orlix_sync_current_user_mapping_page(unsigned long address);
int orlix_handle_host_user_fault(struct pt_regs *regs, unsigned long address,
				 unsigned long fault_flags);
long orlix_hosted_syscall_dispatch(unsigned long scno, unsigned long arg0,
				   unsigned long arg1, unsigned long arg2,
				   unsigned long arg3, unsigned long arg4,
				   unsigned long arg5, unsigned long user_sp);
void __noreturn orlix_hosted_syscall_enter_user(void);
#endif

long orlix_syscall_dispatch(struct pt_regs *regs);

#endif /* _ASM_ORLIX_HOSTED_EXEC_H */
