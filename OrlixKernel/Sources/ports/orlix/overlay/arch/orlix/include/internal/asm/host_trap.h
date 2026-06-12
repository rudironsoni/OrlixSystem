/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _INTERNAL_ASM_ORLIX_HOST_TRAP_H
#define _INTERNAL_ASM_ORLIX_HOST_TRAP_H

#define ORLIX_HOST_USER_TRAP_TIMER	0x10000
#define ORLIX_HOST_USER_TRAP_SYSCALL	0x10001
#define ORLIX_HOST_USER_TRAP_TLS_WRITE	0x10002
#define ORLIX_HOST_USER_TRAP_MEMORY_FAULT	0x10003

#define ORLIX_HOST_USER_FAULT_WRITE	(1UL << 0)
#define ORLIX_HOST_USER_FAULT_EXEC	(1UL << 1)
#define ORLIX_HOST_USER_FAULT_BUS	(1UL << 2)
#define ORLIX_HOST_USER_FRAME_HAS_TLS	(1UL << 0)
#define ORLIX_HOST_USER_FRAME_HAS_SIMD	(1UL << 1)
#define ORLIX_HOST_USER_FRAME_SYSCALL_RETURN	(1UL << 2)

#define ORLIX_HOST_USER_TRAP_TLS_RESUME_OFFSET	16UL

#define ORLIX_HOST_AARCH64_SVC0_INSN		0xd4000001UL
#define ORLIX_HOST_AARCH64_SYSCALL_BRK_INSN	0xd4209e80UL
#define ORLIX_HOST_AARCH64_MSR_TPIDR_EL0	0xd51bd040UL
#define ORLIX_HOST_AARCH64_MSR_TPIDR_EL0_MASK	0xffffffe0UL
#define ORLIX_HOST_AARCH64_TLS_WRITE_BRK_BASE	0xd4209ec0UL
#define ORLIX_HOST_AARCH64_TLS_WRITE_BRK_MASK	0xffffffe0UL

struct orlix_host_user_trap_frame {
	unsigned long regs[31];
	unsigned long sp;
	unsigned long pc;
	unsigned long pstate;
	unsigned long simd[64];
	unsigned long fpsr;
	unsigned long fpcr;
	unsigned long fault_address;
	unsigned long fault_flags;
	unsigned long user_tls;
	unsigned long frame_flags;
};

typedef void (*orlix_host_user_trap_entry_t)(int signal_number,
					     const struct orlix_host_user_trap_frame *frame);
typedef int (*orlix_host_kernel_fault_handler_t)(unsigned long pc,
						 unsigned long fault_address,
						 unsigned long fault_flags);

int orlix_host_user_trap_install(unsigned long user_base,
				 unsigned long user_limit,
				 unsigned long syscall_gate,
				 unsigned long syscall_gate_size,
				 const unsigned long *kernel_sp,
				 const unsigned long *active_user_tls,
				 unsigned long *user_active,
				 orlix_host_kernel_fault_handler_t kernel_fault_handler,
				 orlix_host_user_trap_entry_t entry);
int orlix_host_user_trap_start_timer(unsigned long long period_ns);
void orlix_host_user_trap_resume(
	const struct orlix_host_user_trap_frame *frame) __attribute__((noreturn));

#endif /* _INTERNAL_ASM_ORLIX_HOST_TRAP_H */
