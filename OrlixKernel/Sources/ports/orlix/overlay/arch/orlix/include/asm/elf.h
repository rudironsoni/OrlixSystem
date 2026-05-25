/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_ORLIX_ELF_H
#define _ASM_ORLIX_ELF_H

#include <linux/elf-em.h>
#include <linux/types.h>
#include <uapi/asm/ptrace.h>
#include <asm/processor.h>

#define ELF_CLASS	ELFCLASS64
#define ELF_DATA	ELFDATA2LSB
#define ELF_ARCH	EM_AARCH64

#define elf_check_arch(hdr)	((hdr)->e_machine == ELF_ARCH)

#define CORE_DUMP_USE_REGSET
#define ELF_EXEC_PAGESIZE	PAGE_SIZE
#if defined(ORLIX_APP_HOSTED_BOOT)
#define ELF_ET_DYN_BASE		ORLIX_HOSTED_USER_BASE
#else
#define ELF_ET_DYN_BASE		((TASK_SIZE / 3) * 2)
#endif

#define ELF_HWCAP	(0)
#define ELF_PLATFORM	(NULL)

typedef unsigned long elf_greg_t;
#define ELF_NGREG	(sizeof(struct user_pt_regs) / sizeof(elf_greg_t))
typedef elf_greg_t elf_gregset_t[ELF_NGREG];
typedef unsigned long elf_fpregset_t;

#endif /* _ASM_ORLIX_ELF_H */
