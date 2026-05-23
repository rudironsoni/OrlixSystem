/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_ORLIX_PAGE_H
#define _ASM_ORLIX_PAGE_H

#include <linux/const.h>

#define PAGE_SHIFT	CONFIG_PAGE_SHIFT
#define PAGE_SIZE	(_AC(1, UL) << PAGE_SHIFT)
#define PAGE_MASK	(~(PAGE_SIZE - 1))

/* Keep the initial Orlix split aligned with TASK_SIZE in processor.h. */
#define PAGE_OFFSET	_AC(0xffff800000000000, UL)
#define KERNELBASE	PAGE_OFFSET

#ifndef __ASSEMBLY__

#include <linux/compiler.h>
#include <linux/pfn.h>
#include <linux/types.h>

struct page;

extern phys_addr_t orlix_phys_ram_base;
#define ARCH_PFN_OFFSET		PFN_DOWN(orlix_phys_ram_base)

#define clear_page(page)		memset((page), 0, PAGE_SIZE)
#define copy_page(to, from)	memcpy((to), (from), PAGE_SIZE)

#define clear_user_page(page, vaddr, pg)	clear_page(page)
#define copy_user_page(to, from, vaddr, pg)	copy_page(to, from)

typedef struct { unsigned long pte; } pte_t;
typedef struct { unsigned long pgd; } pgd_t;
typedef struct { unsigned long pgprot; } pgprot_t;
typedef struct page *pgtable_t;

#define pte_val(x)	((x).pte)
#define pgd_val(x)	((x).pgd)
#define pgprot_val(x)	((x).pgprot)

#define __pte(x)	((pte_t) { (x) })
#define __pgd(x)	((pgd_t) { (x) })
#define __pgprot(x)	((pgprot_t) { (x) })

#define __pa(x)		((unsigned long)(x) - PAGE_OFFSET)
#define __pa_symbol(x)	__pa(RELOC_HIDE((unsigned long)(x), 0))
#define __va(x)		((void *)((unsigned long)(x) + PAGE_OFFSET))

#define phys_to_pfn(phys)	(PFN_DOWN(phys))
#define pfn_to_phys(pfn)	(PFN_PHYS(pfn))
#define virt_to_pfn(vaddr)	(phys_to_pfn(__pa(vaddr)))
#define pfn_to_virt(pfn)	(__va(pfn_to_phys(pfn)))

#define virt_to_page(vaddr)	pfn_to_page(virt_to_pfn(vaddr))
#define page_to_virt(page)	pfn_to_virt(page_to_pfn(page))
#define page_to_phys(page)	pfn_to_phys(page_to_pfn(page))

#define pfn_to_kaddr(pfn)	__va(PFN_PHYS(pfn))
#define virt_addr_valid(vaddr)	pfn_valid(virt_to_pfn(vaddr))

#define VM_DATA_DEFAULT_FLAGS	VM_DATA_FLAGS_NON_EXEC

#include <asm-generic/memory_model.h>
#include <asm-generic/getorder.h>

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_ORLIX_PAGE_H */
