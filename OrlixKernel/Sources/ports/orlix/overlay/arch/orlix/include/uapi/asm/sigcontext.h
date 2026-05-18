/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_ASM_ORLIX_SIGCONTEXT_H
#define _UAPI_ASM_ORLIX_SIGCONTEXT_H

#ifndef __ASSEMBLY__

#include <linux/types.h>

struct sigcontext {
	__u64 fault_address;
	__u64 regs[31];
	__u64 sp;
	__u64 pc;
	__u64 pstate;
	__u8 __reserved[4096] __attribute__((__aligned__(16)));
};

#endif

#endif /* _UAPI_ASM_ORLIX_SIGCONTEXT_H */
