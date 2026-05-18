// SPDX-License-Identifier: GPL-2.0-only

#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <asm/processor.h>
#include <asm/ptrace.h>

void start_thread(struct pt_regs *regs, unsigned long pc, unsigned long sp)
{
	memset(regs, 0, sizeof(*regs));
	regs->pc = pc;
	regs->sp = sp;
	regs->pstate = PSR_MODE_EL0t;
	regs->syscallno = NO_SYSCALL;
}

void flush_thread(void)
{
	/* Orlix has no TLS/FPU/vector state to reset yet. */
}

int copy_thread(struct task_struct *p, const struct kernel_clone_args *args)
{
	(void)p;
	(void)args;
	return -EOPNOTSUPP;
}

struct task_struct *__switch_to(struct task_struct *prev, struct task_struct *next)
{
	(void)next;
	panic("Orlix context switching is not implemented\n");
	return prev;
}

unsigned long __get_wchan(struct task_struct *p)
{
	/* No reliable blocked-task stack unwinder exists for Orlix yet. */
	return 0;
}
