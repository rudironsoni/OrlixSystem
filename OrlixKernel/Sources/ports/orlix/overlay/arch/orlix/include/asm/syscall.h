/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_ORLIX_SYSCALL_H
#define _ASM_ORLIX_SYSCALL_H

#include <linux/err.h>
#include <uapi/linux/audit.h>
#include <asm/ptrace.h>

struct task_struct;

static inline int syscall_get_nr(struct task_struct *task,
				 struct pt_regs *regs)
{
	return regs->syscallno;
}

static inline void syscall_rollback(struct task_struct *task,
				    struct pt_regs *regs)
{
	regs->regs[0] = regs->orig_x0;
}

static inline long syscall_get_return_value(struct task_struct *task,
					    struct pt_regs *regs)
{
	return regs->regs[0];
}

static inline long syscall_get_error(struct task_struct *task,
				     struct pt_regs *regs)
{
	unsigned long error = syscall_get_return_value(task, regs);

	return IS_ERR_VALUE(error) ? error : 0;
}

static inline void syscall_set_return_value(struct task_struct *task,
					    struct pt_regs *regs,
					    int error, long val)
{
	regs->regs[0] = error ? error : val;
}

static inline void syscall_get_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 unsigned long *args)
{
	args[0] = regs->orig_x0;
	args[1] = regs->regs[1];
	args[2] = regs->regs[2];
	args[3] = regs->regs[3];
	args[4] = regs->regs[4];
	args[5] = regs->regs[5];
}

static inline int syscall_get_arch(struct task_struct *task)
{
	return AUDIT_ARCH_AARCH64;
}

#endif /* _ASM_ORLIX_SYSCALL_H */
