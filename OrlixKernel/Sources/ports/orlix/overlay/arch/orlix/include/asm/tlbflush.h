/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_ORLIX_TLBFLUSH_H
#define _ASM_ORLIX_TLBFLUSH_H

#include <asm/page.h>
#include <asm/processor.h>

#if defined(ORLIX_APP_HOSTED_BOOT)
#include <internal/asm/host_memory.h>
#endif

struct mm_struct;
struct vm_area_struct;

static inline void orlix_flush_host_user_range(unsigned long start,
					       unsigned long end)
{
#if defined(ORLIX_APP_HOSTED_BOOT)
	if (end <= ORLIX_HOSTED_USER_BASE || start >= ORLIX_HOSTED_STACK_TOP)
		return;

	if (start < ORLIX_HOSTED_USER_BASE)
		start = ORLIX_HOSTED_USER_BASE;
	if (end > ORLIX_HOSTED_STACK_TOP)
		end = ORLIX_HOSTED_STACK_TOP;

	start &= PAGE_MASK;
	end = (end + PAGE_SIZE - 1) & PAGE_MASK;
	if (end > start)
		orlix_host_user_unmap_pages(start, end - start);
#endif
}

static inline void flush_tlb_all(void)
{
	orlix_flush_host_user_range(ORLIX_HOSTED_USER_BASE,
				    ORLIX_HOSTED_STACK_TOP);
}

static inline void flush_tlb_mm(struct mm_struct *mm)
{
	orlix_flush_host_user_range(ORLIX_HOSTED_USER_BASE,
				    ORLIX_HOSTED_STACK_TOP);
}

static inline void flush_tlb_page(struct vm_area_struct *vma,
				  unsigned long address)
{
	orlix_flush_host_user_range(address, address + PAGE_SIZE);
}

static inline void flush_tlb_range(struct vm_area_struct *vma,
				   unsigned long start, unsigned long end)
{
	orlix_flush_host_user_range(start, end);
}

static inline void flush_tlb_kernel_range(unsigned long start,
					  unsigned long end)
{
}

#define flush_tlb_pgtables(mm, start, end)	do { } while (0)

#endif /* _ASM_ORLIX_TLBFLUSH_H */
