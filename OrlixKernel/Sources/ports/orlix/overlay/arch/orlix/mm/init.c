// SPDX-License-Identifier: GPL-2.0-only

#include <linux/export.h>
#include <linux/init.h>
#include <linux/linkage.h>
#include <linux/memblock.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/pgtable.h>
#include <linux/pfn.h>
#include <asm/page.h>
#include <asm/pgtable.h>

pgd_t swapper_pg_dir[PTRS_PER_PGD] __page_aligned_bss;
phys_addr_t orlix_phys_ram_base __ro_after_init;

static pgprot_t protection_map[16] __ro_after_init = {
	[VM_NONE]					= PAGE_NONE,
	[VM_READ]					= PAGE_READONLY,
	[VM_WRITE]					= PAGE_READONLY,
	[VM_WRITE | VM_READ]				= PAGE_READONLY,
	[VM_EXEC]					= PAGE_READONLY_EXEC,
	[VM_EXEC | VM_READ]				= PAGE_READONLY_EXEC,
	[VM_EXEC | VM_WRITE]				= PAGE_READONLY_EXEC,
	[VM_EXEC | VM_WRITE | VM_READ]			= PAGE_READONLY_EXEC,
	[VM_SHARED]					= PAGE_NONE,
	[VM_SHARED | VM_READ]				= PAGE_READONLY,
	[VM_SHARED | VM_WRITE]				= PAGE_SHARED,
	[VM_SHARED | VM_WRITE | VM_READ]		= PAGE_SHARED,
	[VM_SHARED | VM_EXEC]				= PAGE_READONLY_EXEC,
	[VM_SHARED | VM_EXEC | VM_READ]			= PAGE_READONLY_EXEC,
	[VM_SHARED | VM_EXEC | VM_WRITE]		= PAGE_SHARED_EXEC,
	[VM_SHARED | VM_EXEC | VM_WRITE | VM_READ]	= PAGE_SHARED_EXEC,
};

DECLARE_VM_GET_PAGE_PROT

unsigned long empty_zero_page[PAGE_SIZE / sizeof(unsigned long)] __page_aligned_bss;
EXPORT_SYMBOL(empty_zero_page);

void __init paging_init(void)
{
	unsigned long max_zone_pfn[MAX_NR_ZONES] = { 0 };
	phys_addr_t start = memblock_start_of_DRAM();
	phys_addr_t end = memblock_end_of_DRAM();

	if (!end || end <= start)
		panic("Orlix: no memblock RAM available\n");

	min_low_pfn = PFN_UP(start);
	max_low_pfn = max_pfn = PFN_DOWN(end);
	orlix_phys_ram_base = PFN_PHYS(min_low_pfn);
	set_max_mapnr(max_low_pfn - ARCH_PFN_OFFSET);
	high_memory = __va(PFN_PHYS(max_low_pfn));

	max_zone_pfn[ZONE_NORMAL] = max_low_pfn;
	free_area_init(max_zone_pfn);
	memblock_allow_resize();
}

void __init mem_init(void)
{
#ifdef CONFIG_FLATMEM
	BUG_ON(!mem_map);
#endif
	memblock_free_all();
}
