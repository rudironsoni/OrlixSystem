// SPDX-License-Identifier: GPL-2.0-only

#include <linux/bug.h>
#include <linux/compiler.h>
#include <linux/linkage.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/string.h>
#include <asm/hosted_exec.h>
#include <asm/page.h>
#include <asm/processor.h>
#include <asm/ptrace.h>
#include <internal/asm/host_memory.h>

#if defined(ORLIX_APP_HOSTED_BOOT)
unsigned long orlix_hosted_kernel_sp;
unsigned long orlix_hosted_host_tpidr_el0;
unsigned long orlix_hosted_entry_user_tpidr_el0;
unsigned long orlix_hosted_entry_user_pc;
unsigned long orlix_hosted_return_user_tpidr_el0;
static unsigned char orlix_hosted_syscall_gate_page[PAGE_SIZE] __page_aligned_data;
static bool orlix_hosted_syscall_gate_ready;

void orlix_hosted_syscall_gate(void);

asm(
".p2align 2\n"
"	.globl _orlix_hosted_syscall_gate\n"
"_orlix_hosted_syscall_gate:\n"
"	mrs	x12, tpidr_el0\n"
"	adrp	x13, _orlix_hosted_entry_user_tpidr_el0@PAGE\n"
"	str	x12, [x13, _orlix_hosted_entry_user_tpidr_el0@PAGEOFF]\n"
"	adrp	x13, _orlix_hosted_entry_user_pc@PAGE\n"
"	str	x30, [x13, _orlix_hosted_entry_user_pc@PAGEOFF]\n"
"	adrp	x13, _orlix_hosted_host_tpidr_el0@PAGE\n"
"	ldr	x13, [x13, _orlix_hosted_host_tpidr_el0@PAGEOFF]\n"
"	cbz	x13, 0f\n"
"	msr	tpidr_el0, x13\n"
"0:\n"
"	mov	x9, sp\n"
"	adrp	x10, _orlix_hosted_kernel_sp@PAGE\n"
"	ldr	x10, [x10, _orlix_hosted_kernel_sp@PAGEOFF]\n"
"	cbnz	x10, 1f\n"
"	brk	#1\n"
"1:\n"
"	mov	sp, x10\n"
"	stp	x29, x30, [sp, #-16]!\n"
"	mov	x29, sp\n"
"	str	x9, [sp, #-16]!\n"
"	mov	x7, x9\n"
"	bl	_orlix_hosted_syscall_dispatch\n"
"	ldr	x9, [sp], #16\n"
"	ldp	x29, x30, [sp], #16\n"
"	adrp	x10, _orlix_hosted_kernel_sp@PAGE\n"
"	mov	x11, sp\n"
"	str	x11, [x10, _orlix_hosted_kernel_sp@PAGEOFF]\n"
"	adrp	x10, _orlix_hosted_return_user_tpidr_el0@PAGE\n"
"	ldr	x10, [x10, _orlix_hosted_return_user_tpidr_el0@PAGEOFF]\n"
"	msr	tpidr_el0, x10\n"
"	mov	sp, x9\n"
"	ret\n"
);

static unsigned long orlix_read_tpidr_el0(void)
{
	unsigned long value;

	asm volatile("mrs %0, tpidr_el0" : "=r"(value));
	return value;
}

void orlix_hosted_save_kernel_stack(unsigned long sp)
{
	WRITE_ONCE(orlix_hosted_kernel_sp, sp);
}

unsigned long orlix_hosted_prepare_user_entry(void)
{
	unsigned long user_tls = current->thread.user_tls;

	WRITE_ONCE(orlix_hosted_host_tpidr_el0, orlix_read_tpidr_el0());
	WRITE_ONCE(orlix_hosted_return_user_tpidr_el0, user_tls);
	return user_tls;
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
	long ret;

	current->thread.user_tls = READ_ONCE(orlix_hosted_entry_user_tpidr_el0);
	regs->regs[0] = arg0;
	regs->regs[1] = arg1;
	regs->regs[2] = arg2;
	regs->regs[3] = arg3;
	regs->regs[4] = arg4;
	regs->regs[5] = arg5;
	regs->regs[8] = scno;
	regs->sp = user_sp;
	regs->pc = READ_ONCE(orlix_hosted_entry_user_pc);
	regs->pstate = PSR_MODE_EL0t;
	regs->syscallno = scno;

	ret = orlix_syscall_dispatch(regs);
	orlix_sync_current_user_mappings(regs);
	WRITE_ONCE(orlix_hosted_return_user_tpidr_el0, current->thread.user_tls);
	return ret;
}
#endif
