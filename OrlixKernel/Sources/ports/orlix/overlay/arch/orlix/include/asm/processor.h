/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_ORLIX_PROCESSOR_H
#define _ASM_ORLIX_PROCESSOR_H

#include <linux/compiler.h>
#include <asm/thread_info.h>

#if defined(ORLIX_APP_HOSTED_BOOT)
#define ORLIX_HOSTED_USER_BASE	(0x0000600000000000UL)
#define ORLIX_HOSTED_STACK_TOP	(0x0000700000000000UL)
#define TASK_SIZE		ORLIX_HOSTED_STACK_TOP
#define TASK_UNMAPPED_BASE	ORLIX_HOSTED_USER_BASE
#else
#define TASK_SIZE		(0x0000800000000000UL)
#define TASK_UNMAPPED_BASE	(TASK_SIZE / 3)
#endif

#ifdef __KERNEL__
#if defined(ORLIX_APP_HOSTED_BOOT)
#define STACK_TOP		ORLIX_HOSTED_STACK_TOP
#else
#define STACK_TOP		TASK_SIZE
#endif
#define STACK_TOP_MAX		STACK_TOP
#endif

#ifndef __ASSEMBLY__

struct pt_regs;
struct task_struct;

void start_thread(struct pt_regs *regs, unsigned long pc, unsigned long sp);
unsigned long __get_wchan(struct task_struct *p);

struct orlix_cpu_context {
	unsigned long x19;
	unsigned long x20;
	unsigned long x21;
	unsigned long x22;
	unsigned long x23;
	unsigned long x24;
	unsigned long x25;
	unsigned long x26;
	unsigned long x27;
	unsigned long x28;
	unsigned long fp;
	unsigned long sp;
	unsigned long pc;
};

struct thread_struct {
	struct orlix_cpu_context cpu_context;
};

#define INIT_THREAD			\
{					\
	.cpu_context = { 0 },		\
}

#define task_pt_regs(task)	\
	((struct pt_regs *)(THREAD_SIZE + task_stack_page(task)) - 1)

#define KSTK_EIP(task)		(0UL)
#define KSTK_ESP(task)		(0UL)

static inline void cpu_relax(void)
{
	barrier();
}

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_ORLIX_PROCESSOR_H */
