// SPDX-License-Identifier: GPL-2.0-only

#include <linux/bug.h>
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/linkage.h>
#include <linux/mm.h>
#include <linux/panic.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>
#include <linux/signal.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/time64.h>
#include <asm/hosted_exec.h>
#include <asm/page.h>
#include <asm/processor.h>
#include <asm/ptrace.h>
#include <asm/signal.h>
#include <asm/time.h>
#include <internal/asm/host_memory.h>
#include <internal/asm/host_trap.h>

#if defined(ORLIX_APP_HOSTED_BOOT)
unsigned long orlix_hosted_kernel_sp;
unsigned long orlix_hosted_active_user_tls;
unsigned long orlix_hosted_user_active;
unsigned long orlix_hosted_entry_user_pc;
unsigned long orlix_hosted_entry_user_callee[12];
bool orlix_hosted_syscall_full_user_entry;
static unsigned char orlix_hosted_syscall_gate_page[PAGE_SIZE] __page_aligned_data;
static bool orlix_hosted_syscall_gate_ready;
static bool orlix_hosted_user_timer_ready;

void orlix_hosted_syscall_gate(void);

static void orlix_hosted_save_current_kernel_stack(void)
{
	/*
	 * Hosted user reentry is synthetic: Darwin resumes the same host
	 * thread on the saved Linux kernel stack after a user fault, syscall,
	 * or timer redirect.  Save a stable task-stack boundary instead of the
	 * current C frame's SP so repeated user/kernel transitions cannot walk
	 * the kernel stack downward.
	 */
	orlix_hosted_save_kernel_stack((unsigned long)task_pt_regs(current));
}

static void orlix_hosted_start_user_timer_once(void)
{
	/*
	 * The HostAdapter may suspend the hosted thread only while this file
	 * has marked execution as Linux user mode. Kernel entry and return
	 * windows switch TPIDR_EL0 between host and Linux TLS, so those windows
	 * must stay invisible to the async timer transport.
	 */
	if (!IS_ENABLED(CONFIG_ORLIX_HOSTED_ASYNC_USER_TIMER))
		return;

	if (orlix_hosted_user_timer_ready)
		return;

	if (orlix_host_user_trap_start_timer(NSEC_PER_SEC / 2))
		panic("Orlix: failed to start hosted user timer transport\n");
	orlix_hosted_user_timer_ready = true;
}

static void orlix_hosted_restore_user_tls(void)
{
	unsigned long user_tls = current->thread.user_tls;
	unsigned long hw_tls;

	WRITE_ONCE(orlix_hosted_active_user_tls, user_tls);
	asm volatile(
	"	msr	tpidr_el0, %0\n"
	"	isb\n"
	:
	: "r"(user_tls)
	: "memory");
	asm volatile("mrs %0, tpidr_el0" : "=r"(hw_tls));
	if (unlikely(hw_tls != user_tls)) {
		asm volatile(
		"	msr	tpidr_el0, %0\n"
		"	isb\n"
		:
		: "r"(user_tls)
		: "memory");
	}
}

static bool orlix_hosted_valid_user_tls(unsigned long user_tls)
{
	return user_tls >= ORLIX_HOSTED_USER_BASE &&
		user_tls < ORLIX_HOSTED_STACK_TOP &&
		!(user_tls & (sizeof(unsigned long) - 1));
}

static void orlix_hosted_apply_frame_user_state(
	const struct orlix_host_user_trap_frame *frame)
{
	if ((frame->frame_flags & ORLIX_HOST_USER_FRAME_HAS_TLS) &&
	    orlix_hosted_valid_user_tls(frame->user_tls))
		current->thread.user_tls = frame->user_tls;

	if (frame->frame_flags & ORLIX_HOST_USER_FRAME_HAS_SIMD) {
		memcpy(current->thread.user_simd, frame->simd,
		       sizeof(current->thread.user_simd));
		current->thread.user_fpsr = frame->fpsr;
		current->thread.user_fpcr = frame->fpcr;
		current->thread.user_simd_valid = 1;
	}
}

void orlix_hosted_preserve_user_tls(void)
{
	unsigned long user_tls;

	if (!current->mm)
		return;

	asm volatile("mrs %0, tpidr_el0" : "=r"(user_tls));
	if (!orlix_hosted_valid_user_tls(user_tls))
		return;

	current->thread.user_tls = user_tls;
	WRITE_ONCE(orlix_hosted_active_user_tls, user_tls);
}

static void orlix_hosted_restore_trap_frame(
	struct pt_regs *regs,
	const struct orlix_host_user_trap_frame *frame)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(regs->regs); i++)
		regs->regs[i] = frame->regs[i];
	regs->pc = frame->pc;
	regs->sp = frame->sp;
	regs->pstate = frame->pstate ? frame->pstate : PSR_MODE_EL0t;
	regs->syscallno = NO_SYSCALL;
}

static void orlix_hosted_save_trap_frame(
	struct orlix_host_user_trap_frame *frame,
	const struct pt_regs *regs)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(regs->regs); i++)
		frame->regs[i] = regs->regs[i];
	frame->pc = regs->pc;
	frame->sp = regs->sp;
	frame->pstate = regs->pstate;
	frame->fault_address = 0;
	frame->fault_flags = 0;
	frame->user_tls = current->thread.user_tls;
	frame->frame_flags = ORLIX_HOST_USER_FRAME_HAS_TLS;

	if (current->thread.user_simd_valid) {
		memcpy(frame->simd, current->thread.user_simd,
		       sizeof(frame->simd));
		frame->fpsr = current->thread.user_fpsr;
		frame->fpcr = current->thread.user_fpcr;
		frame->frame_flags |= ORLIX_HOST_USER_FRAME_HAS_SIMD;
	}
}

static void __noreturn orlix_hosted_resume_current_user(struct pt_regs *regs,
							bool full_stack_window)
{
	struct orlix_host_user_trap_frame resume_frame;

	if (full_stack_window)
		orlix_sync_current_user_mappings(regs);
	else
		orlix_sync_current_user_minimal_mappings(regs);
	orlix_hosted_save_current_kernel_stack();
	orlix_hosted_save_trap_frame(&resume_frame, regs);
	WRITE_ONCE(orlix_hosted_active_user_tls, resume_frame.user_tls);
	orlix_hosted_start_user_timer_once();
	orlix_host_user_trap_resume(&resume_frame);
}

static void __noreturn orlix_hosted_user_trap_entry(
	int signal_number,
	const struct orlix_host_user_trap_frame *frame)
{
	struct pt_regs *regs = task_pt_regs(current);
	long exit_code = signal_number & 0x7f;

	orlix_hosted_restore_trap_frame(regs, frame);
	orlix_hosted_apply_frame_user_state(frame);

	if (signal_number == ORLIX_HOST_USER_TRAP_TIMER) {
		struct pt_regs *resume_regs;
		unsigned long old_pc = regs->pc;
		unsigned long old_sp = regs->sp;

		orlix_timer_poll();
		orlix_exit_to_user_mode_work(regs);
		resume_regs = task_pt_regs(current);
		orlix_hosted_resume_current_user(resume_regs,
						 resume_regs != regs ||
						 resume_regs->pc != old_pc ||
						 resume_regs->sp != old_sp);
	}

	if (frame->fault_address &&
	    !orlix_handle_host_user_fault(regs, frame->fault_address,
					  frame->fault_flags)) {
		orlix_exit_to_user_mode_work(regs);
		orlix_hosted_resume_current_user(task_pt_regs(current), false);
	}

	if (!exit_code)
		exit_code = SIGKILL;

	pr_info("Orlix: hosted user trap signal %d at pc %#lx fault=%#lx flags=%#lx\n",
		signal_number, regs->pc, frame->fault_address,
		frame->fault_flags);
	do_group_exit(exit_code);
}

asm(
".p2align 2\n"
"	.globl _orlix_hosted_syscall_gate\n"
"_orlix_hosted_syscall_gate:\n"
"	adrp	x9, _orlix_hosted_user_active@PAGE\n"
"	str	xzr, [x9, _orlix_hosted_user_active@PAGEOFF]\n"
"	adrp	x9, _orlix_hosted_entry_user_callee@PAGE\n"
"	add	x9, x9, _orlix_hosted_entry_user_callee@PAGEOFF\n"
"	stp	x19, x20, [x9]\n"
"	stp	x21, x22, [x9, #16]\n"
"	stp	x23, x24, [x9, #32]\n"
"	stp	x25, x26, [x9, #48]\n"
"	stp	x27, x28, [x9, #64]\n"
"	stp	x29, x30, [x9, #80]\n"
"	adrp	x13, _orlix_hosted_entry_user_pc@PAGE\n"
"	str	x30, [x13, _orlix_hosted_entry_user_pc@PAGEOFF]\n"
"	mrs	x12, tpidr_el0\n"
"	mov	x9, sp\n"
"	adrp	x10, _orlix_hosted_kernel_sp@PAGE\n"
"	ldr	x10, [x10, _orlix_hosted_kernel_sp@PAGEOFF]\n"
"	cbnz	x10, 1f\n"
"	brk	#1\n"
"1:\n"
"	mov	sp, x10\n"
"	stp	x29, x30, [sp, #-16]!\n"
"	mov	x29, sp\n"
"	stp	x9, x12, [sp, #-16]!\n"
"	sub	sp, sp, #528\n"
"	mrs	x10, fpcr\n"
"	mrs	x11, fpsr\n"
"	stp	x10, x11, [sp]\n"
"	stp	q0, q1, [sp, #16]\n"
"	stp	q2, q3, [sp, #48]\n"
"	stp	q4, q5, [sp, #80]\n"
"	stp	q6, q7, [sp, #112]\n"
"	stp	q8, q9, [sp, #144]\n"
"	stp	q10, q11, [sp, #176]\n"
"	stp	q12, q13, [sp, #208]\n"
"	stp	q14, q15, [sp, #240]\n"
"	stp	q16, q17, [sp, #272]\n"
"	stp	q18, q19, [sp, #304]\n"
"	stp	q20, q21, [sp, #336]\n"
"	stp	q22, q23, [sp, #368]\n"
"	stp	q24, q25, [sp, #400]\n"
"	stp	q26, q27, [sp, #432]\n"
"	stp	q28, q29, [sp, #464]\n"
"	stp	q30, q31, [sp, #496]\n"
"	mov	x7, x9\n"
"	bl	_orlix_hosted_syscall_dispatch\n"
"	adrp	x10, _orlix_hosted_syscall_full_user_entry@PAGE\n"
"	ldrb	w10, [x10, _orlix_hosted_syscall_full_user_entry@PAGEOFF]\n"
"	cbnz	w10, 2f\n"
"	ldp	x10, x11, [sp]\n"
"	msr	fpcr, x10\n"
"	msr	fpsr, x11\n"
"	ldp	q0, q1, [sp, #16]\n"
"	ldp	q2, q3, [sp, #48]\n"
"	ldp	q4, q5, [sp, #80]\n"
"	ldp	q6, q7, [sp, #112]\n"
"	ldp	q8, q9, [sp, #144]\n"
"	ldp	q10, q11, [sp, #176]\n"
"	ldp	q12, q13, [sp, #208]\n"
"	ldp	q14, q15, [sp, #240]\n"
"	ldp	q16, q17, [sp, #272]\n"
"	ldp	q18, q19, [sp, #304]\n"
"	ldp	q20, q21, [sp, #336]\n"
"	ldp	q22, q23, [sp, #368]\n"
"	ldp	q24, q25, [sp, #400]\n"
"	ldp	q26, q27, [sp, #432]\n"
"	ldp	q28, q29, [sp, #464]\n"
"	ldp	q30, q31, [sp, #496]\n"
"	add	sp, sp, #528\n"
"	ldp	x9, x12, [sp], #16\n"
"	stp	x0, x9, [sp, #-16]!\n"
"	bl	_orlix_hosted_prepare_user_entry\n"
"	mov	x12, x0\n"
"	ldp	x0, x9, [sp], #16\n"
"	ldp	x29, x30, [sp], #16\n"
"	adrp	x10, _orlix_hosted_kernel_sp@PAGE\n"
"	mov	x11, sp\n"
"	str	x11, [x10, _orlix_hosted_kernel_sp@PAGEOFF]\n"
"	msr	tpidr_el0, x12\n"
"	isb\n"
"	adrp	x10, _orlix_hosted_active_user_tls@PAGE\n"
"	str	x12, [x10, _orlix_hosted_active_user_tls@PAGEOFF]\n"
"	adrp	x10, _orlix_hosted_user_active@PAGE\n"
"	mov	x11, #1\n"
"	str	x11, [x10, _orlix_hosted_user_active@PAGEOFF]\n"
"	mov	sp, x9\n"
"	ret\n"
"2:\n"
"	ldp	x10, x11, [sp]\n"
"	msr	fpcr, x10\n"
"	msr	fpsr, x11\n"
"	ldp	q0, q1, [sp, #16]\n"
"	ldp	q2, q3, [sp, #48]\n"
"	ldp	q4, q5, [sp, #80]\n"
"	ldp	q6, q7, [sp, #112]\n"
"	ldp	q8, q9, [sp, #144]\n"
"	ldp	q10, q11, [sp, #176]\n"
"	ldp	q12, q13, [sp, #208]\n"
"	ldp	q14, q15, [sp, #240]\n"
"	ldp	q16, q17, [sp, #272]\n"
"	ldp	q18, q19, [sp, #304]\n"
"	ldp	q20, q21, [sp, #336]\n"
"	ldp	q22, q23, [sp, #368]\n"
"	ldp	q24, q25, [sp, #400]\n"
"	ldp	q26, q27, [sp, #432]\n"
"	ldp	q28, q29, [sp, #464]\n"
"	ldp	q30, q31, [sp, #496]\n"
"	add	sp, sp, #528\n"
"	ldp	x9, x12, [sp], #16\n"
"	ldp	x29, x30, [sp], #16\n"
"	adrp	x10, _orlix_hosted_kernel_sp@PAGE\n"
"	mov	x11, sp\n"
"	str	x11, [x10, _orlix_hosted_kernel_sp@PAGEOFF]\n"
"	bl	_orlix_hosted_syscall_enter_user\n"
"	brk	#2\n"
);

void orlix_hosted_capture_host_context(void)
{
	if (orlix_host_user_trap_install(ORLIX_HOSTED_USER_BASE,
					 ORLIX_HOSTED_STACK_TOP,
					 ORLIX_HOSTED_SYSCALL_GATE,
					 PAGE_SIZE,
					 &orlix_hosted_kernel_sp,
					 &orlix_hosted_active_user_tls,
					 &orlix_hosted_user_active,
					 orlix_hosted_user_trap_entry))
		panic("Orlix: failed to install hosted user trap transport\n");
}

void orlix_hosted_save_kernel_stack(unsigned long sp)
{
	WRITE_ONCE(orlix_hosted_kernel_sp, sp);
}

unsigned long orlix_hosted_prepare_user_entry(void)
{
	orlix_hosted_restore_user_tls();
	return current->thread.user_tls;
}

static void orlix_hosted_save_callee_registers(struct pt_regs *regs)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(orlix_hosted_entry_user_callee); i++)
		regs->regs[19 + i] =
			READ_ONCE(orlix_hosted_entry_user_callee[i]);
}

static void orlix_hosted_prepare_syscall_gate(void)
{
	u32 *insn = (u32 *)orlix_hosted_syscall_gate_page;
	u64 *literal = (u64 *)(orlix_hosted_syscall_gate_page + 8);

	if (orlix_hosted_syscall_gate_ready)
		return;

	insn[0] = 0x58000050; /* ldr x16, .+8 */
	insn[1] = 0xd61f0200; /* br x16 */
	*literal = (u64)(unsigned long)orlix_hosted_syscall_gate;
	orlix_hosted_syscall_gate_ready = true;
}

int orlix_hosted_sync_syscall_gate(void)
{
	orlix_hosted_prepare_syscall_gate();
	return orlix_host_user_map_page(ORLIX_HOSTED_SYSCALL_GATE,
					orlix_hosted_syscall_gate_page,
					PAGE_SIZE, 0, 1);
}

long orlix_hosted_syscall_dispatch(unsigned long scno, unsigned long arg0,
				   unsigned long arg1, unsigned long arg2,
				   unsigned long arg3, unsigned long arg4,
				   unsigned long arg5, unsigned long user_sp)
{
	struct pt_regs *regs = task_pt_regs(current);
	unsigned long entry_pc;
	long ret;
	bool full_user_entry;

	orlix_hosted_preserve_user_tls();
	orlix_hosted_start_user_timer_once();
	orlix_hosted_save_callee_registers(regs);
	entry_pc = READ_ONCE(orlix_hosted_entry_user_pc);
	regs->regs[0] = arg0;
	regs->regs[1] = arg1;
	regs->regs[2] = arg2;
	regs->regs[3] = arg3;
	regs->regs[4] = arg4;
	regs->regs[5] = arg5;
	regs->regs[8] = scno;
	regs->sp = user_sp;
	regs->pc = entry_pc;
	regs->pstate = PSR_MODE_EL0t;
	regs->syscallno = scno;

	ret = orlix_syscall_dispatch(regs);
	orlix_sync_current_user_mappings(regs);
	full_user_entry = regs->pc != entry_pc || regs->sp != user_sp;
	WRITE_ONCE(orlix_hosted_syscall_full_user_entry, full_user_entry);
	return ret;
}

void __noreturn orlix_hosted_syscall_enter_user(void)
{
	WRITE_ONCE(orlix_hosted_syscall_full_user_entry, false);
	orlix_hosted_enter_user(task_pt_regs(current));
}
#endif
