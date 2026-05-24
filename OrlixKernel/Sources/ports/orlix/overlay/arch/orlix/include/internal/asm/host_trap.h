/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _INTERNAL_ASM_ORLIX_HOST_TRAP_H
#define _INTERNAL_ASM_ORLIX_HOST_TRAP_H

typedef void (*orlix_host_user_trap_entry_t)(int signal_number,
					     unsigned long user_pc,
					     unsigned long user_sp);

int orlix_host_user_trap_install(unsigned long user_base,
				 unsigned long user_limit,
				 const unsigned long *kernel_sp,
				 orlix_host_user_trap_entry_t entry);

#endif /* _INTERNAL_ASM_ORLIX_HOST_TRAP_H */
