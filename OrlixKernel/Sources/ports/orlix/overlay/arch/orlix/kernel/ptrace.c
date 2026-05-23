// SPDX-License-Identifier: GPL-2.0-only

#include <linux/elf.h>
#include <linux/ptrace.h>
#include <linux/regset.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <asm/elf.h>
#include <asm/processor.h>
#include <asm/ptrace.h>

enum orlix_regset {
	REGSET_GPR,
};

static int orlix_gpr_get(struct task_struct *target,
				 const struct user_regset *regset,
				 struct membuf to)
{
	const struct pt_regs *regs = task_pt_regs(target);

	(void)regset;
	return membuf_write(&to, &regs->user_regs, sizeof(regs->user_regs));
}

static int orlix_gpr_set(struct task_struct *target,
				 const struct user_regset *regset,
				 unsigned int pos, unsigned int count,
				 const void *kbuf, const void __user *ubuf)
{
	struct pt_regs *regs = task_pt_regs(target);

	(void)regset;
	return user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				  &regs->user_regs, 0, sizeof(regs->user_regs));
}

static const struct user_regset orlix_regsets[] = {
	[REGSET_GPR] = {
		.core_note_type = NT_PRSTATUS,
		.n = sizeof(struct user_pt_regs) / sizeof(u64),
		.size = sizeof(u64),
		.align = sizeof(u64),
		.regset_get = orlix_gpr_get,
		.set = orlix_gpr_set,
	},
};

static const struct user_regset_view orlix_user_view = {
	.name = "orlix",
	.e_machine = ELF_ARCH,
	.ei_osabi = ELF_OSABI,
	.regsets = orlix_regsets,
	.n = ARRAY_SIZE(orlix_regsets),
};

const struct user_regset_view *task_user_regset_view(struct task_struct *task)
{
	(void)task;
	return &orlix_user_view;
}

void ptrace_disable(struct task_struct *child)
{
	(void)child;
}

long arch_ptrace(struct task_struct *child, long request,
			 unsigned long addr, unsigned long data)
{
	return ptrace_request(child, request, addr, data);
}
