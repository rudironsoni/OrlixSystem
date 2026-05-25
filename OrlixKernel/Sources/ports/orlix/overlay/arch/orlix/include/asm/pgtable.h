/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_ORLIX_PGTABLE_H
#define _ASM_ORLIX_PGTABLE_H

#include <linux/compiler.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <asm/page.h>
#include <asm/processor.h>

/*
 * Orlix uses unsigned long page-table entries.  Derive the number of index
 * bits from PAGE_SHIFT so level geometry stays correct for both 4K and 16K
 * pages.
 */
#define ORLIX_PGTABLE_INDEX_BITS	(PAGE_SHIFT - 3)

#define PMD_SHIFT	(PAGE_SHIFT + ORLIX_PGTABLE_INDEX_BITS)
#define PMD_SIZE	(1UL << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE - 1))

#define PUD_SHIFT	(PMD_SHIFT + ORLIX_PGTABLE_INDEX_BITS)
#define PUD_SIZE	(1UL << PUD_SHIFT)
#define PUD_MASK	(~(PUD_SIZE - 1))

#define PGDIR_SHIFT	(PUD_SHIFT + ORLIX_PGTABLE_INDEX_BITS)
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE - 1))

#define PTRS_PER_PTE	(PAGE_SIZE / sizeof(pte_t))
#define PTRS_PER_PMD	(PAGE_SIZE / sizeof(pmd_t))
#define PTRS_PER_PUD	(PAGE_SIZE / sizeof(pud_t))
#define PTRS_PER_PGD	(PAGE_SIZE / sizeof(pgd_t))
#define USER_PTRS_PER_PGD	((TASK_SIZE + PGDIR_SIZE - 1) / PGDIR_SIZE)

#define KERN_VIRT_SIZE	((PTRS_PER_PGD / 2 * PGDIR_SIZE) / 2)
#define VMALLOC_SIZE	(KERN_VIRT_SIZE >> 1)
#if defined(ORLIX_APP_HOSTED_BOOT)
#define VMALLOC_START	_AC(0x0000700000000000, UL)
#define VMALLOC_END	_AC(0x0000780000000000, UL)
#else
#define VMALLOC_END	PAGE_OFFSET
#define VMALLOC_START	(PAGE_OFFSET - VMALLOC_SIZE)
#endif

#ifndef __ASSEMBLY__

struct vm_area_struct;
struct vm_fault;

typedef struct { unsigned long pmd; } pmd_t;
typedef struct { unsigned long pud; } pud_t;

#define pmd_val(x)	((x).pmd)
#define pud_val(x)	((x).pud)
#define __pmd(x)	((pmd_t) { (x) })
#define __pud(x)	((pud_t) { (x) })

#include <asm-generic/pgtable-nop4d.h>

#define _PAGE_PRESENT		(1UL << 0)
#define _PAGE_WRITE		(1UL << 1)
#define _PAGE_USER		(1UL << 2)
#define _PAGE_ACCESSED		(1UL << 3)
#define _PAGE_DIRTY		(1UL << 4)
#define _PAGE_EXEC		(1UL << 5)
#define _PAGE_PROT_NONE		(1UL << 6)
#define _PAGE_SWP_EXCLUSIVE	(1UL << 7)

#define _PAGE_TABLE	(_PAGE_PRESENT | _PAGE_ACCESSED | _PAGE_DIRTY)
#define _PAGE_PFN_MASK	PAGE_MASK
#define _PAGE_CHG_MASK	(_PAGE_PFN_MASK | _PAGE_ACCESSED | _PAGE_DIRTY)

#define PAGE_NONE	__pgprot(_PAGE_PROT_NONE | _PAGE_USER)
#define PAGE_READONLY	__pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_ACCESSED)
#define PAGE_READONLY_EXEC	__pgprot(_PAGE_PRESENT | _PAGE_USER | \
					 _PAGE_ACCESSED | _PAGE_EXEC)
#define PAGE_COPY	PAGE_READONLY
#define PAGE_SHARED	__pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_WRITE | \
				 _PAGE_ACCESSED | _PAGE_DIRTY)
#define PAGE_SHARED_EXEC	__pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_WRITE | \
				 _PAGE_ACCESSED | _PAGE_DIRTY | _PAGE_EXEC)
#define PAGE_KERNEL	__pgprot(_PAGE_PRESENT | _PAGE_WRITE | _PAGE_ACCESSED | \
				 _PAGE_DIRTY | _PAGE_EXEC)

extern unsigned long empty_zero_page[PAGE_SIZE / sizeof(unsigned long)];
#define ZERO_PAGE(vaddr)	virt_to_page(empty_zero_page)

#define set_pte(ptep, pte)	WRITE_ONCE(*(ptep), (pte))

static inline void set_pmd(pmd_t *pmdp, pmd_t pmd)
{
	WRITE_ONCE(*pmdp, pmd);
}

static inline void set_pud(pud_t *pudp, pud_t pud)
{
	WRITE_ONCE(*pudp, pud);
}

static inline void set_p4d(p4d_t *p4dp, p4d_t p4d)
{
	WRITE_ONCE(*p4dp, p4d);
}

static inline int pte_none(pte_t pte)
{
	return !pte_val(pte);
}

static inline int pte_present(pte_t pte)
{
	return pte_val(pte) & (_PAGE_PRESENT | _PAGE_PROT_NONE);
}

static inline void pte_clear(struct mm_struct *mm, unsigned long addr,
				     pte_t *ptep)
{
	set_pte(ptep, __pte(0));
}

static inline int pmd_none(pmd_t pmd)
{
	return !pmd_val(pmd);
}

static inline int pmd_bad(pmd_t pmd)
{
	return (pmd_val(pmd) & ~_PAGE_PFN_MASK) != _PAGE_TABLE;
}

static inline int pmd_present(pmd_t pmd)
{
	return pmd_val(pmd) & _PAGE_PRESENT;
}

static inline void pmd_clear(pmd_t *pmdp)
{
	set_pmd(pmdp, __pmd(0));
}

static inline int pud_none(pud_t pud)
{
	return !pud_val(pud);
}

static inline int pud_bad(pud_t pud)
{
	return (pud_val(pud) & ~_PAGE_PFN_MASK) != _PAGE_TABLE;
}

static inline int pud_present(pud_t pud)
{
	return pud_val(pud) & _PAGE_PRESENT;
}

static inline void pud_clear(pud_t *pudp)
{
	set_pud(pudp, __pud(0));
}

static inline int p4d_none(p4d_t p4d)
{
	return !p4d_val(p4d);
}

static inline int p4d_bad(p4d_t p4d)
{
	return (p4d_val(p4d) & ~_PAGE_PFN_MASK) != _PAGE_TABLE;
}

static inline int p4d_present(p4d_t p4d)
{
	return p4d_val(p4d) & _PAGE_PRESENT;
}

static inline void p4d_clear(p4d_t *p4dp)
{
	set_p4d(p4dp, __p4d(0));
}

static inline int pte_write(pte_t pte)
{
	return pte_val(pte) & _PAGE_WRITE;
}

static inline int pte_dirty(pte_t pte)
{
	return pte_val(pte) & _PAGE_DIRTY;
}

static inline int pte_young(pte_t pte)
{
	return pte_val(pte) & _PAGE_ACCESSED;
}

static inline pte_t pte_wrprotect(pte_t pte)
{
	pte_val(pte) &= ~_PAGE_WRITE;
	return pte;
}

static inline pte_t pte_mkclean(pte_t pte)
{
	pte_val(pte) &= ~_PAGE_DIRTY;
	return pte;
}

static inline pte_t pte_mkold(pte_t pte)
{
	pte_val(pte) &= ~_PAGE_ACCESSED;
	return pte;
}

static inline pte_t pte_mkwrite_novma(pte_t pte)
{
	pte_val(pte) |= _PAGE_WRITE;
	return pte;
}

static inline pte_t pte_mkdirty(pte_t pte)
{
	pte_val(pte) |= _PAGE_DIRTY;
	return pte;
}

static inline pte_t pte_mkyoung(pte_t pte)
{
	pte_val(pte) |= _PAGE_ACCESSED;
	return pte;
}

#define PFN_PTE_SHIFT	PAGE_SHIFT

static inline unsigned long pte_pfn(pte_t pte)
{
	return pte_val(pte) >> PFN_PTE_SHIFT;
}

static inline pte_t pfn_pte(unsigned long pfn, pgprot_t prot)
{
	return __pte((pfn << PFN_PTE_SHIFT) | pgprot_val(prot));
}

static inline pte_t mk_pte(struct page *page, pgprot_t prot)
{
	return pfn_pte(page_to_pfn(page), prot);
}

#define pte_page(pte)	pfn_to_page(pte_pfn(pte))

static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	return __pte((pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot));
}

static inline pmd_t pfn_pmd(unsigned long pfn, pgprot_t prot)
{
	return __pmd((pfn << PFN_PTE_SHIFT) | pgprot_val(prot));
}

static inline unsigned long pmd_pfn(pmd_t pmd)
{
	return pmd_val(pmd) >> PFN_PTE_SHIFT;
}

static inline unsigned long pmd_page_vaddr(pmd_t pmd)
{
	return (unsigned long)__va(pmd_val(pmd) & _PAGE_PFN_MASK);
}

#define pmd_page(pmd)	pfn_to_page(pmd_pfn(pmd))

static inline pud_t pfn_pud(unsigned long pfn, pgprot_t prot)
{
	return __pud((pfn << PFN_PTE_SHIFT) | pgprot_val(prot));
}

static inline unsigned long pud_pfn(pud_t pud)
{
	return pud_val(pud) >> PFN_PTE_SHIFT;
}

static inline pmd_t *pud_pgtable(pud_t pud)
{
	return (pmd_t *)__va(pud_val(pud) & _PAGE_PFN_MASK);
}

#define pud_page(pud)	pfn_to_page(pud_pfn(pud))

static inline p4d_t pfn_p4d(unsigned long pfn, pgprot_t prot)
{
	return __p4d((pfn << PFN_PTE_SHIFT) | pgprot_val(prot));
}

static inline unsigned long p4d_pfn(p4d_t p4d)
{
	return p4d_val(p4d) >> PFN_PTE_SHIFT;
}

static inline pud_t *p4d_pgtable(p4d_t p4d)
{
	return (pud_t *)__va(p4d_val(p4d) & _PAGE_PFN_MASK);
}

#define p4d_page(p4d)	pfn_to_page(p4d_pfn(p4d))

static inline void pmd_populate_kernel(struct mm_struct *mm, pmd_t *pmdp,
					       pte_t *ptep)
{
	set_pmd(pmdp, __pmd(__pa(ptep) | _PAGE_TABLE));
}

static inline void pmd_populate(struct mm_struct *mm, pmd_t *pmdp,
					pgtable_t ptep)
{
	pmd_populate_kernel(mm, pmdp, (pte_t *)page_to_virt(ptep));
}

static inline void pud_populate(struct mm_struct *mm, pud_t *pudp,
					pmd_t *pmdp)
{
	set_pud(pudp, __pud(__pa(pmdp) | _PAGE_TABLE));
}

static inline void p4d_populate(struct mm_struct *mm, p4d_t *p4dp,
					pud_t *pudp)
{
	set_p4d(p4dp, __p4d(__pa(pudp) | _PAGE_TABLE));
}

#define p4d_populate_safe(mm, p4dp, pudp)	p4d_populate(mm, p4dp, pudp)

static inline void update_mmu_cache_range(struct vm_fault *vmf,
						  struct vm_area_struct *vma,
						  unsigned long addr,
						  pte_t *ptep,
						  unsigned int nr)
{
}

#define update_mmu_cache(vma, addr, ptep) \
	update_mmu_cache_range(NULL, vma, addr, ptep, 1)
#define update_mmu_cache_pmd(vma, addr, pmd) do { } while (0)
#define update_mmu_cache_pud(vma, addr, pud) do { } while (0)

/*
 * Swap PTEs are !pte_none() && !pte_present().  Keep the encoded swap type
 * and offset clear of _PAGE_PRESENT, _PAGE_PROT_NONE, and the exclusive
 * marker so Linux never mistakes a swap/migration entry for a present PFN.
 */
#define __SWP_TYPE_SHIFT	8
#define __SWP_TYPE_BITS		5
#define __SWP_TYPE_MASK		((1UL << __SWP_TYPE_BITS) - 1)
#define __SWP_OFFSET_SHIFT	(__SWP_TYPE_SHIFT + __SWP_TYPE_BITS)

#define MAX_SWAPFILES_CHECK() \
	BUILD_BUG_ON(MAX_SWAPFILES_SHIFT > __SWP_TYPE_BITS)

#define __swp_type(x)		(((x).val >> __SWP_TYPE_SHIFT) & __SWP_TYPE_MASK)
#define __swp_offset(x)		((x).val >> __SWP_OFFSET_SHIFT)
#define __swp_entry(type, offset) \
	((swp_entry_t) { (((type) & __SWP_TYPE_MASK) << __SWP_TYPE_SHIFT) | \
			 ((offset) << __SWP_OFFSET_SHIFT) })
#define __pte_to_swp_entry(pte)	((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(x)	__pte((x).val)

static inline int pte_swp_exclusive(pte_t pte)
{
	return pte_val(pte) & _PAGE_SWP_EXCLUSIVE;
}

static inline pte_t pte_swp_mkexclusive(pte_t pte)
{
	pte_val(pte) |= _PAGE_SWP_EXCLUSIVE;
	return pte;
}

static inline pte_t pte_swp_clear_exclusive(pte_t pte)
{
	pte_val(pte) &= ~_PAGE_SWP_EXCLUSIVE;
	return pte;
}

#define pte_ERROR(e)	pr_err("bad pte %016lx\n", pte_val(e))
#define pmd_ERROR(e)	pr_err("bad pmd %016lx\n", pmd_val(e))
#define pud_ERROR(e)	pr_err("bad pud %016lx\n", pud_val(e))
#define pgd_ERROR(e)	pr_err("bad pgd %016lx\n", pgd_val(e))

extern pgd_t swapper_pg_dir[PTRS_PER_PGD];
extern void paging_init(void);

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_ORLIX_PGTABLE_H */
