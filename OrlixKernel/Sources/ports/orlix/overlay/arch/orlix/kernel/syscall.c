// SPDX-License-Identifier: GPL-2.0-only

#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/nospec.h>
#include <linux/sched.h>
#include <linux/syscalls.h>
#include <linux/unistd.h>
#include <asm/hosted_exec.h>
#include <asm/ptrace.h>
#include <asm/signal.h>
#include <asm/syscall.h>
#include <asm/time.h>

#undef __SYSCALL
#define __SYSCALL(nr, call)	[nr] = (call),
#define __SYSCALL_WITH_COMPAT(nr, native, compat) __SYSCALL(nr, native)

typedef long (*orlix_syscall_fn_t)(unsigned long, unsigned long,
				   unsigned long, unsigned long,
				   unsigned long, unsigned long);

SYSCALL_DEFINE6(mmap, unsigned long, addr, unsigned long, len,
		unsigned long, prot, unsigned long, flags,
		unsigned long, fd, unsigned long, off)
{
	if (offset_in_page(off) != 0)
		return -EINVAL;

	return ksys_mmap_pgoff(addr, len, prot, flags, fd, off >> PAGE_SHIFT);
}

asmlinkage long sys_ni_syscall(void);
asmlinkage long sys_rt_sigreturn(void);

void * const sys_call_table[__NR_syscalls] = {
#include <asm/syscall_table_64.h>
};

long orlix_syscall_dispatch(struct pt_regs *regs)
{
	unsigned long nr = regs->syscallno;
	long ret = -ENOSYS;

	regs->orig_x0 = regs->regs[0];

	if (nr < __NR_syscalls) {
		orlix_syscall_fn_t syscall_fn;

		syscall_fn = (orlix_syscall_fn_t)
			sys_call_table[array_index_nospec(nr, __NR_syscalls)];
		ret = syscall_fn(regs->orig_x0, regs->regs[1], regs->regs[2],
				 regs->regs[3], regs->regs[4], regs->regs[5]);
	}

	syscall_set_return_value(current, regs, 0, ret);
	orlix_timer_poll();
	orlix_exit_to_user_mode_work(regs);
	if (in_syscall(regs))
		forget_syscall(regs);
	return regs->regs[0];
}
