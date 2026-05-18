// SPDX-License-Identifier: GPL-2.0-only

#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <asm/ptrace.h>

void show_regs(struct pt_regs *regs)
{
	int i;

	if (!regs) {
		pr_info("Orlix pt_regs unavailable\n");
		return;
	}

	pr_info("pc: %016llx sp: %016llx pstate: %016llx\n",
		(unsigned long long)regs->pc,
		(unsigned long long)regs->sp,
		(unsigned long long)regs->pstate);
	for (i = 0; i < 31; i += 2) {
		if (i == 30) {
			pr_info("x%-2d: %016llx\n", i,
				(unsigned long long)regs->regs[i]);
			break;
		}
		pr_info("x%-2d: %016llx x%-2d: %016llx\n",
			i, (unsigned long long)regs->regs[i],
			i + 1, (unsigned long long)regs->regs[i + 1]);
	}
}

void show_stack(struct task_struct *task, unsigned long *sp, const char *loglvl)
{
	(void)task;
	(void)sp;
	printk("%sOrlix stack unwinding is not implemented\n",
	       loglvl ? loglvl : KERN_DEFAULT);
}
