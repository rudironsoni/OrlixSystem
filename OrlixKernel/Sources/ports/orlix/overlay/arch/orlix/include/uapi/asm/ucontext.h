/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_ASM_ORLIX_UCONTEXT_H
#define _UAPI_ASM_ORLIX_UCONTEXT_H

#include <asm/sigcontext.h>
#include <asm/signal.h>

#ifndef __ASSEMBLY__

struct ucontext {
	unsigned long uc_flags;
	struct ucontext *uc_link;
	stack_t uc_stack;
	sigset_t uc_sigmask;
	struct sigcontext uc_mcontext;
};

#endif

#endif /* _UAPI_ASM_ORLIX_UCONTEXT_H */
