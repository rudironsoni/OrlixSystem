/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_ORLIX_PGALLOC_H
#define _ASM_ORLIX_PGALLOC_H

#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <asm-generic/pgalloc.h>

static inline void sync_kernel_mappings(pgd_t *pgd)
{
	memcpy(pgd + USER_PTRS_PER_PGD,
	       init_mm.pgd + USER_PTRS_PER_PGD,
	       (PTRS_PER_PGD - USER_PTRS_PER_PGD) * sizeof(pgd_t));
}

static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	pgd_t *pgd = (pgd_t *)__get_free_page(GFP_KERNEL);

	if (pgd) {
		memset(pgd, 0, USER_PTRS_PER_PGD * sizeof(pgd_t));
		sync_kernel_mappings(pgd);
	}
	return pgd;
}

#define __pte_free_tlb(tlb, pte, address)	pte_free((tlb)->mm, pte)
#define __pmd_free_tlb(tlb, pmd, address)	pmd_free((tlb)->mm, pmd)
#define __pud_free_tlb(tlb, pud, address)	pud_free((tlb)->mm, pud)

#endif /* _ASM_ORLIX_PGALLOC_H */
