/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_ORLIX_PTRACE_H
#define _ASM_ORLIX_PTRACE_H

#include <linux/stddef.h>
#include <linux/types.h>
#include <uapi/asm/ptrace.h>

#define NO_SYSCALL	(-1)

#ifndef __ASSEMBLY__

struct pt_regs {
	union {
		struct user_pt_regs user_regs;
		struct {
			u64 regs[31];
			u64 sp;
			u64 pc;
			u64 pstate;
		};
	};
	u64 orig_x0;
	s32 syscallno;
	u32 unused;
};

#define MAX_REG_OFFSET	offsetof(struct pt_regs, pstate)

#define user_mode(regs)		\
	(((regs)->pstate & PSR_MODE_MASK) == PSR_MODE_EL0t)
#define processor_mode(regs)	((regs)->pstate & PSR_MODE_MASK)

#define instruction_pointer(regs)		((regs)->pc)
#define instruction_pointer_set(regs, val)	((regs)->pc = (val))
#define profile_pc(regs)			instruction_pointer(regs)

#define user_stack_pointer(regs)		((regs)->sp)
#define user_stack_pointer_set(regs, val)	((regs)->sp = (val))
#define kernel_stack_pointer(regs)		((regs)->sp)

#define regs_return_value(regs)		((regs)->regs[0])
#define regs_set_return_value(regs, val)	((regs)->regs[0] = (val))

static inline bool in_syscall(const struct pt_regs *regs)
{
	return regs->syscallno != NO_SYSCALL;
}

static inline void forget_syscall(struct pt_regs *regs)
{
	regs->syscallno = NO_SYSCALL;
}

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_ORLIX_PTRACE_H */
