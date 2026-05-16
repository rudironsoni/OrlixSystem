/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_ORLIX_TLBFLUSH_H
#define _ASM_ORLIX_TLBFLUSH_H

struct mm_struct;
struct vm_area_struct;

static inline void flush_tlb_all(void)
{
}

static inline void flush_tlb_mm(struct mm_struct *mm)
{
}

static inline void flush_tlb_page(struct vm_area_struct *vma,
				  unsigned long address)
{
}

static inline void flush_tlb_range(struct vm_area_struct *vma,
				   unsigned long start, unsigned long end)
{
}

static inline void flush_tlb_kernel_range(unsigned long start,
					  unsigned long end)
{
}

#define flush_tlb_pgtables(mm, start, end)	do { } while (0)

#endif /* _ASM_ORLIX_TLBFLUSH_H */
