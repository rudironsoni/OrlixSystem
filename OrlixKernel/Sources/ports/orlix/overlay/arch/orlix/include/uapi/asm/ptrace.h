/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_ASM_ORLIX_PTRACE_H
#define _UAPI_ASM_ORLIX_PTRACE_H

#include <linux/types.h>

#define PSR_MODE_EL0t	0x00000000
#define PSR_MODE_EL1t	0x00000004
#define PSR_MODE_EL1h	0x00000005
#define PSR_MODE_MASK	0x0000000f

#ifndef __ASSEMBLY__
struct user_pt_regs {
	__u64 regs[31];
	__u64 sp;
	__u64 pc;
	__u64 pstate;
};
#endif

#endif /* _UAPI_ASM_ORLIX_PTRACE_H */
