/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_ORLIX_THREAD_INFO_H
#define _ASM_ORLIX_THREAD_INFO_H

#ifdef __KERNEL__

#include <linux/const.h>

#define THREAD_SIZE_ORDER	3
#define THREAD_SIZE		(_AC(32768, UL))

#ifndef __ASSEMBLY__

struct task_struct;
struct pt_regs;

struct thread_info {
#ifndef CONFIG_THREAD_INFO_IN_TASK
	struct task_struct	*task;
#endif
	unsigned long		flags;
	unsigned int		cpu;
	int			preempt_count;
	struct pt_regs		*regs;
};

#ifdef CONFIG_THREAD_INFO_IN_TASK
#define INIT_THREAD_INFO(tsk)			\
{						\
	.flags		= 0,				\
	.cpu		= 0,				\
	.preempt_count	= INIT_PREEMPT_COUNT,		\
}
#else
#define INIT_THREAD_INFO(tsk)			\
{						\
	.task		= &(tsk),			\
	.flags		= 0,				\
	.cpu		= 0,				\
	.preempt_count	= INIT_PREEMPT_COUNT,		\
}
#endif

#ifndef CONFIG_THREAD_INFO_IN_TASK
extern struct thread_info *orlix_current_thread_info;

static inline struct thread_info *current_thread_info(void)
{
	return orlix_current_thread_info;
}
#endif

#endif /* !__ASSEMBLY__ */

#define TIF_SYSCALL_TRACE	0
#define TIF_NOTIFY_RESUME	1
#define TIF_SIGPENDING		2
#define TIF_NEED_RESCHED	3
#define TIF_NOTIFY_SIGNAL	4
#define TIF_RESTORE_SIGMASK	5
#define TIF_MEMDIE		6
#define TIF_SYSCALL_AUDIT	7
#define TIF_SECCOMP		8
#define TIF_SYSCALL_TRACEPOINT	9
#define TIF_UPROBE		10
#define TIF_SINGLESTEP		11
#define TIF_POLLING_NRFLAG	16

#define _TIF_SYSCALL_TRACE	(1 << TIF_SYSCALL_TRACE)
#define _TIF_NOTIFY_RESUME	(1 << TIF_NOTIFY_RESUME)
#define _TIF_SIGPENDING		(1 << TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1 << TIF_NEED_RESCHED)
#define _TIF_NOTIFY_SIGNAL	(1 << TIF_NOTIFY_SIGNAL)
#define _TIF_RESTORE_SIGMASK	(1 << TIF_RESTORE_SIGMASK)
#define _TIF_MEMDIE		(1 << TIF_MEMDIE)
#define _TIF_SYSCALL_AUDIT	(1 << TIF_SYSCALL_AUDIT)
#define _TIF_SECCOMP		(1 << TIF_SECCOMP)
#define _TIF_SYSCALL_TRACEPOINT	(1 << TIF_SYSCALL_TRACEPOINT)
#define _TIF_UPROBE		(1 << TIF_UPROBE)
#define _TIF_SINGLESTEP		(1 << TIF_SINGLESTEP)
#define _TIF_POLLING_NRFLAG	(1 << TIF_POLLING_NRFLAG)

#define _TIF_WORK_MASK		(_TIF_NEED_RESCHED | _TIF_SIGPENDING | \
				 _TIF_NOTIFY_RESUME | _TIF_NOTIFY_SIGNAL | \
				 _TIF_UPROBE)

#define _TIF_SYSCALL_WORK	(_TIF_SYSCALL_TRACE | _TIF_SYSCALL_AUDIT | \
				 _TIF_SYSCALL_TRACEPOINT | _TIF_SECCOMP)

#endif /* __KERNEL__ */

#endif /* _ASM_ORLIX_THREAD_INFO_H */
