// SPDX-License-Identifier: GPL-2.0-only

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched/signal.h>
#include <linux/signal.h>
#include <linux/rseq.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <asm/processor.h>
#include <asm/ptrace.h>
#include <asm/signal.h>
#include <asm/syscall.h>
#include <asm/ucontext.h>
#include <asm/unistd.h>

struct rt_sigframe {
	struct siginfo info;
	struct ucontext uc;
};

static bool orlix_valid_user_regs(const struct pt_regs *regs)
{
	return user_mode(regs) && regs->pc < TASK_SIZE && regs->sp <= STACK_TOP;
}

static int setup_sigcontext(struct sigcontext __user *sc,
			    const struct pt_regs *regs)
{
	int i;

	if (put_user(0, &sc->fault_address))
		return -EFAULT;
	for (i = 0; i < 31; i++)
		if (put_user(regs->regs[i], &sc->regs[i]))
			return -EFAULT;
	if (put_user(regs->sp, &sc->sp) ||
	    put_user(regs->pc, &sc->pc) ||
	    put_user(regs->pstate, &sc->pstate) ||
	    clear_user(sc->__reserved, sizeof(sc->__reserved)))
		return -EFAULT;

	return 0;
}

static int restore_sigcontext(struct pt_regs *regs,
			      struct sigcontext __user *sc)
{
	int i;

	for (i = 0; i < 31; i++)
		if (get_user(regs->regs[i], &sc->regs[i]))
			return -EFAULT;
	if (get_user(regs->sp, &sc->sp) ||
	    get_user(regs->pc, &sc->pc) ||
	    get_user(regs->pstate, &sc->pstate))
		return -EFAULT;
	forget_syscall(regs);

	return orlix_valid_user_regs(regs) ? 0 : -EINVAL;
}

static struct rt_sigframe __user *get_sigframe(struct ksignal *ksig,
					       struct pt_regs *regs)
{
	unsigned long sp = sigsp(regs->sp, ksig);

	sp = round_down(sp - sizeof(struct rt_sigframe), 16);
	return (struct rt_sigframe __user *)sp;
}

static int setup_return(struct pt_regs *regs, struct k_sigaction *ka,
			struct rt_sigframe __user *frame, int usig)
{
	if (!(ka->sa.sa_flags & SA_RESTORER) || !ka->sa.sa_restorer)
		return -EINVAL;

	regs->regs[0] = usig;
	regs->regs[1] = 0;
	regs->regs[2] = 0;
	regs->sp = (unsigned long)frame;
	regs->pc = (unsigned long)ka->sa.sa_handler;
	regs->regs[30] = (unsigned long)ka->sa.sa_restorer;
	regs->pstate = PSR_MODE_EL0t;

	return 0;
}

static int setup_rt_frame(int usig, struct ksignal *ksig, sigset_t *set,
			  struct pt_regs *regs)
{
	struct rt_sigframe __user *frame = get_sigframe(ksig, regs);
	int err = 0;

	if (!access_ok(frame, sizeof(*frame)))
		return -EFAULT;

	if (put_user(0, &frame->uc.uc_flags) ||
	    put_user(NULL, &frame->uc.uc_link) ||
	    __save_altstack(&frame->uc.uc_stack, regs->sp) ||
	    copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set)) ||
	    setup_sigcontext(&frame->uc.uc_mcontext, regs))
		return -EFAULT;

	err = setup_return(regs, &ksig->ka, frame, usig);
	if (err)
		return err;

	if (ksig->ka.sa.sa_flags & SA_SIGINFO) {
		err = copy_siginfo_to_user(&frame->info, &ksig->info);
		if (err)
			return err;
		regs->regs[1] = (unsigned long)&frame->info;
		regs->regs[2] = (unsigned long)&frame->uc;
	}

	return orlix_valid_user_regs(regs) ? 0 : -EINVAL;
}

static void handle_signal(struct ksignal *ksig, struct pt_regs *regs)
{
	sigset_t *oldset = sigmask_to_save();
	int ret;

	rseq_signal_deliver(ksig, regs);
	ret = setup_rt_frame(ksig->sig, ksig, oldset, regs);
	signal_setup_done(ret, ksig, 0);
}

static void setup_restart_syscall(struct pt_regs *regs)
{
	regs->regs[0] = __NR_restart_syscall;
}

void orlix_do_signal_or_restart(struct pt_regs *regs)
{
	unsigned long continue_addr = 0;
	unsigned long restart_addr = 0;
	struct ksignal ksig;
	bool syscall = in_syscall(regs);
	int retval = 0;

	if (syscall) {
		continue_addr = regs->pc;
		restart_addr = continue_addr - 4;
		retval = regs->regs[0];
		forget_syscall(regs);

		switch (retval) {
		case -ERESTARTNOHAND:
		case -ERESTARTSYS:
		case -ERESTARTNOINTR:
		case -ERESTART_RESTARTBLOCK:
			regs->regs[0] = regs->orig_x0;
			regs->pc = restart_addr;
			break;
		}
	}

	if (get_signal(&ksig)) {
		if (regs->pc == restart_addr &&
		    (retval == -ERESTARTNOHAND ||
		     retval == -ERESTART_RESTARTBLOCK ||
		     (retval == -ERESTARTSYS &&
		      !(ksig.ka.sa.sa_flags & SA_RESTART)))) {
			syscall_set_return_value(current, regs, -EINTR, 0);
			regs->pc = continue_addr;
		}

		handle_signal(&ksig, regs);
		return;
	}

	if (syscall && regs->pc == restart_addr &&
	    retval == -ERESTART_RESTARTBLOCK)
		setup_restart_syscall(regs);

	restore_saved_sigmask();
}

static bool orlix_user_work_pending(void)
{
	return need_resched() || test_thread_flag(TIF_SIGPENDING) ||
		test_thread_flag(TIF_NOTIFY_SIGNAL) ||
		test_thread_flag(TIF_RESTORE_SIGMASK);
}

void orlix_exit_to_user_mode_work(struct pt_regs *regs)
{
	do {
		if (need_resched())
			schedule();

		if (in_syscall(regs) || test_thread_flag(TIF_SIGPENDING) ||
		    test_thread_flag(TIF_NOTIFY_SIGNAL) ||
		    test_thread_flag(TIF_RESTORE_SIGMASK))
			orlix_do_signal_or_restart(regs);
	} while (orlix_user_work_pending());
}

SYSCALL_DEFINE0(rt_sigreturn)
{
	struct pt_regs *regs = current_pt_regs();
	struct rt_sigframe __user *frame;
	sigset_t set;

	current->restart_block.fn = do_no_restart_syscall;

	if (regs->sp & 15)
		goto badframe;

	frame = (struct rt_sigframe __user *)regs->sp;
	if (!access_ok(frame, sizeof(*frame)))
		goto badframe;

	if (__copy_from_user(&set, &frame->uc.uc_sigmask, sizeof(set)))
		goto badframe;
	set_current_blocked(&set);

	if (restore_sigcontext(regs, &frame->uc.uc_mcontext))
		goto badframe;

	if (restore_altstack(&frame->uc.uc_stack))
		goto badframe;

	return regs->regs[0];

badframe:
	force_sig(SIGSEGV);
	return 0;
}
