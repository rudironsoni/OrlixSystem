/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_ASM_ORLIX_PTRACE_H
#define _UAPI_ASM_ORLIX_PTRACE_H

#include <linux/types.h>

#ifndef __ASSEMBLY__
struct user_pt_regs {
	__u64 regs[31];
	__u64 sp;
	__u64 pc;
	__u64 pstate;
};
#endif

#endif /* _UAPI_ASM_ORLIX_PTRACE_H */
