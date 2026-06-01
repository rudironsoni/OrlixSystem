// SPDX-License-Identifier: GPL-2.0-only

#include <linux/mm.h>
#include <linux/oom.h>
#include <linux/sched/signal.h>
#include <linux/signal.h>
#include <asm/hosted_exec.h>
#include <asm/processor.h>
#include <asm/ptrace.h>
#include <internal/asm/host_trap.h>

#if defined(ORLIX_APP_HOSTED_BOOT)
extern unsigned long orlix_hosted_active_user_tls;

static void orlix_force_user_fault_signal(unsigned long address,
					  unsigned long fault_flags,
					  int si_code)
{
	if (fault_flags & ORLIX_HOST_USER_FAULT_BUS) {
		force_sig_fault(SIGBUS, BUS_ADRERR, (void __user *)address);
		return;
	}

	force_sig_fault(SIGSEGV, si_code, (void __user *)address);
}

int orlix_handle_host_user_fault(struct pt_regs *regs, unsigned long address,
				 unsigned long fault_flags)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	vm_fault_t fault;
	vm_flags_t required;
	unsigned int flags = FAULT_FLAG_DEFAULT | FAULT_FLAG_USER;
	int si_code = SEGV_MAPERR;

	if (!user_mode(regs) || faulthandler_disabled() || !mm)
		return -EFAULT;

	if (fault_flags & ORLIX_HOST_USER_FAULT_EXEC) {
		required = VM_EXEC;
		flags |= FAULT_FLAG_INSTRUCTION;
	} else if (fault_flags & ORLIX_HOST_USER_FAULT_WRITE) {
		required = VM_WRITE;
		flags |= FAULT_FLAG_WRITE;
	} else {
		required = VM_READ;
	}

retry:
	vma = lock_mm_and_find_vma(mm, address, regs);
	if (!vma)
		goto bad_area_nosemaphore;

	si_code = SEGV_ACCERR;
	if (!(vma->vm_flags & required))
		goto bad_area;

	fault = handle_mm_fault(vma, address, flags, regs);
	if (fault_signal_pending(fault, regs))
		return 0;
	if (fault & VM_FAULT_COMPLETED)
		return orlix_sync_current_user_fault_window(address, fault_flags) ?
			-EFAULT : 0;
	if (unlikely(fault & VM_FAULT_ERROR)) {
		if (fault & VM_FAULT_OOM)
			goto out_of_memory;
		if (fault & VM_FAULT_SIGBUS) {
			mmap_read_unlock(mm);
			force_sig_fault(SIGBUS, BUS_ADRERR,
					(void __user *)address);
			return 0;
		}
		goto bad_area;
	}
	if (fault & VM_FAULT_RETRY) {
		flags |= FAULT_FLAG_TRIED;
		goto retry;
	}

	mmap_read_unlock(mm);
	return orlix_sync_current_user_fault_window(address, fault_flags) ?
		-EFAULT : 0;

bad_area:
	mmap_read_unlock(mm);
bad_area_nosemaphore:
		pr_info("Orlix: user fault pc=%#llx sp=%#llx addr=%#lx flags=%#lx si=%d task_tls=%#lx active_tls=%#lx\n",
			regs->pc, regs->sp, address, fault_flags, si_code,
			current->thread.user_tls, orlix_hosted_active_user_tls);
	orlix_force_user_fault_signal(address, fault_flags, si_code);
	return 0;

out_of_memory:
	mmap_read_unlock(mm);
	pagefault_out_of_memory();
	return 0;
}
#endif
