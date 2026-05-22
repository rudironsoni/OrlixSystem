// SPDX-License-Identifier: GPL-2.0-only

#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <asm/processor.h>
#include <asm/ptrace.h>

struct thread_info *orlix_current_thread_info = &init_thread_info;

asmlinkage void ret_from_fork(void);
extern struct task_struct *orlix_cpu_switch_context(struct orlix_cpu_context *prev,
						    struct orlix_cpu_context *next,
						    struct task_struct *last);

asm(
".p2align 2\n"
"	.globl _orlix_cpu_switch_context\n"
"_orlix_cpu_switch_context:\n"
"	mov	x9, sp\n"
"	stp	x19, x20, [x0], #16\n"
"	stp	x21, x22, [x0], #16\n"
"	stp	x23, x24, [x0], #16\n"
"	stp	x25, x26, [x0], #16\n"
"	stp	x27, x28, [x0], #16\n"
"	stp	x29, x9, [x0], #16\n"
"	str	lr, [x0]\n"
"	ldp	x19, x20, [x1], #16\n"
"	ldp	x21, x22, [x1], #16\n"
"	ldp	x23, x24, [x1], #16\n"
"	ldp	x25, x26, [x1], #16\n"
"	ldp	x27, x28, [x1], #16\n"
"	ldp	x29, x9, [x1], #16\n"
"	ldr	lr, [x1]\n"
"	mov	sp, x9\n"
"	mov	x0, x2\n"
"	ret\n"
".p2align 2\n"
"	.globl _ret_from_fork\n"
"_ret_from_fork:\n"
"	bl	_schedule_tail\n"
"	cbz	x19, 1f\n"
"	mov	x0, x20\n"
"	blr	x19\n"
"	mov	x0, #0\n"
"	bl	_do_exit\n"
"1:\n"
"	brk	#0\n"
);

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
	struct pt_regs *childregs = task_pt_regs(p);

	memset(&p->thread.cpu_context, 0, sizeof(p->thread.cpu_context));

	if (!args->fn)
		return -EOPNOTSUPP;

	memset(childregs, 0, sizeof(*childregs));
	childregs->pstate = PSR_MODE_EL1h;
	childregs->syscallno = NO_SYSCALL;

	p->thread.cpu_context.x19 = (unsigned long)args->fn;
	p->thread.cpu_context.x20 = (unsigned long)args->fn_arg;
	p->thread.cpu_context.sp = (unsigned long)childregs;
	p->thread.cpu_context.pc = (unsigned long)ret_from_fork;

	return 0;
}

struct task_struct *__switch_to(struct task_struct *prev, struct task_struct *next)
{
	orlix_current_thread_info = task_thread_info(next);
	return orlix_cpu_switch_context(&prev->thread.cpu_context,
					&next->thread.cpu_context, prev);
}

unsigned long __get_wchan(struct task_struct *p)
{
	/* No reliable blocked-task stack unwinder exists for Orlix yet. */
	return 0;
}
