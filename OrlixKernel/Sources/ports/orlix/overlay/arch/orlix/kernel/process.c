// SPDX-License-Identifier: GPL-2.0-only

#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/panic.h>
#include <linux/stddef.h>
#include <asm/hosted_exec.h>
#include <asm/processor.h>
#include <asm/ptrace.h>

struct thread_info *orlix_current_thread_info = &init_thread_info;

asmlinkage void ret_from_fork(void);
asmlinkage void orlix_ret_from_fork_user(struct pt_regs *regs);
extern struct task_struct *orlix_cpu_switch_context(struct orlix_cpu_context *prev,
						    struct orlix_cpu_context *next,
						    struct task_struct *last);
#if defined(ORLIX_APP_HOSTED_BOOT)
static __noreturn void orlix_hosted_enter_user(struct pt_regs *regs);
#endif

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
"1:\n"
"	mov	x0, sp\n"
"	bl	_orlix_ret_from_fork_user\n"
	"	brk	#0\n"
	);

#if defined(ORLIX_APP_HOSTED_BOOT)
static __noreturn void orlix_hosted_enter_user(struct pt_regs *regs)
{
	unsigned long kernel_sp;
	unsigned long user_tls;

	asm volatile("mov %0, sp" : "=r"(kernel_sp));
	orlix_hosted_save_kernel_stack(kernel_sp);
	user_tls = orlix_hosted_prepare_user_entry();

	asm volatile(
	"	mov	x9, %0\n"
	"	mov	x12, %1\n"
	"	ldr	x10, [x9, #%c[pc_offset]]\n"
	"	ldr	x11, [x9, #%c[sp_offset]]\n"
	"	msr	tpidr_el0, x12\n"
	"	ldr	x8, [x9, #%c[x8_offset]]\n"
	"	ldp	x6, x7, [x9, #%c[x6_offset]]\n"
	"	ldp	x4, x5, [x9, #%c[x4_offset]]\n"
	"	ldp	x2, x3, [x9, #%c[x2_offset]]\n"
	"	ldp	x0, x1, [x9, #%c[x0_offset]]\n"
	"	mov	sp, x11\n"
	"	br	x10\n"
	:
	: "r"(regs), "r"(user_tls),
	  [pc_offset] "i"(offsetof(struct pt_regs, pc)),
	  [sp_offset] "i"(offsetof(struct pt_regs, sp)),
	  [x8_offset] "i"(offsetof(struct pt_regs, regs[8])),
	  [x6_offset] "i"(offsetof(struct pt_regs, regs[6])),
	  [x4_offset] "i"(offsetof(struct pt_regs, regs[4])),
	  [x2_offset] "i"(offsetof(struct pt_regs, regs[2])),
	  [x0_offset] "i"(offsetof(struct pt_regs, regs[0]))
	: "memory");
	unreachable();
}
#endif

asmlinkage void orlix_ret_from_fork_user(struct pt_regs *regs)
{
	if (!user_mode(regs))
		do_exit(0);

#if defined(ORLIX_APP_HOSTED_BOOT)
	orlix_sync_current_user_mappings(regs);
	orlix_hosted_enter_user(regs);
#endif
	panic("Orlix: user return requires hosted entry support\n");
}

void start_thread(struct pt_regs *regs, unsigned long pc, unsigned long sp)
{
	memset(regs, 0, sizeof(*regs));
	regs->pc = pc;
	regs->sp = sp;
	regs->pstate = PSR_MODE_EL0t;
	regs->syscallno = NO_SYSCALL;
#if defined(ORLIX_APP_HOSTED_BOOT)
	current->thread.user_tls = 0;
#endif
}

void flush_thread(void)
{
	/* Orlix has no TLS/FPU/vector state to reset yet. */
}

int copy_thread(struct task_struct *p, const struct kernel_clone_args *args)
{
	struct pt_regs *childregs = task_pt_regs(p);

	memset(&p->thread.cpu_context, 0, sizeof(p->thread.cpu_context));

	if (!args->fn) {
		*childregs = *task_pt_regs(current);
		childregs->regs[0] = 0;
		childregs->syscallno = NO_SYSCALL;
		if (args->stack)
			childregs->sp = args->stack;
#if defined(ORLIX_APP_HOSTED_BOOT)
		p->thread.user_tls = current->thread.user_tls;
		if (args->flags & CLONE_SETTLS)
			p->thread.user_tls = args->tls;
#endif
	} else {
		memset(childregs, 0, sizeof(*childregs));
		childregs->pstate = PSR_MODE_EL1h;
		childregs->syscallno = NO_SYSCALL;

		p->thread.cpu_context.x19 = (unsigned long)args->fn;
		p->thread.cpu_context.x20 = (unsigned long)args->fn_arg;
	}
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
