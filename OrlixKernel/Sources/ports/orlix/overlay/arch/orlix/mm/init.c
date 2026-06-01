// SPDX-License-Identifier: GPL-2.0-only

#include <linux/export.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/linkage.h>
#include <linux/memblock.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/pgtable.h>
#include <linux/pfn.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/sched/task_stack.h>
#include <linux/atomic.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/hosted_exec.h>
#include <internal/asm/host_trap.h>
#include <asm/processor.h>
#include <asm/ptrace.h>
#include <internal/asm/host_memory.h>

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

#if defined(ORLIX_APP_HOSTED_BOOT)
static int orlix_sync_kernel_page(unsigned long address)
{
	pgd_t *pgd = pgd_offset_k(address);
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	pte_t entry;
	void *source;

	if (pgd_none(*pgd) || pgd_bad(*pgd))
		goto unmap;

	p4d = p4d_offset(pgd, address);
	if (p4d_none(*p4d) || p4d_bad(*p4d))
		goto unmap;

	pud = pud_offset(p4d, address);
	if (pud_none(*pud) || pud_bad(*pud))
		goto unmap;

	pmd = pmd_offset(pud, address);
	if (pmd_none(*pmd) || pmd_bad(*pmd))
		goto unmap;

	pte = pte_offset_kernel(pmd, address);
	entry = READ_ONCE(*pte);
	if (!pte_present(entry))
		goto unmap;

	source = __va(PFN_PHYS(pte_pfn(entry)));
	return orlix_host_kernel_map_page(address, source, PAGE_SIZE);

unmap:
	orlix_host_kernel_unmap_pages(address, PAGE_SIZE);
	return 0;
}

void arch_sync_kernel_mappings(unsigned long start, unsigned long end)
{
	unsigned long address;

	for (address = start; address < end; address += PAGE_SIZE) {
		if (orlix_sync_kernel_page(address))
			panic("Orlix: failed to synchronize hosted kernel mapping %#lx\n",
			      address);
	}
}

static int orlix_fault_in_user_page_locked(struct mm_struct *mm,
					   unsigned long fault_address)
{
	struct pt_regs *regs = task_pt_regs(current);
	bool tried = false;

retry:
	{
		struct vm_area_struct *vma;
		unsigned int fault_flags = FAULT_FLAG_DEFAULT | FAULT_FLAG_USER;
		vm_flags_t required = VM_READ;
		vm_fault_t fault;

		vma = lock_mm_and_find_vma(mm, fault_address, regs);
		if (!vma)
			return -EFAULT;

		if (vma->vm_flags & VM_EXEC) {
			required = VM_EXEC;
			fault_flags |= FAULT_FLAG_INSTRUCTION;
		} else if (vma->vm_flags & VM_WRITE) {
			required = VM_WRITE;
			fault_flags |= FAULT_FLAG_WRITE;
		}
		if (tried)
			fault_flags |= FAULT_FLAG_TRIED;

		if (!(vma->vm_flags & required)) {
			mmap_read_unlock(mm);
			return -EFAULT;
		}

		fault = handle_mm_fault(vma, fault_address, fault_flags, regs);
		if (fault_signal_pending(fault, regs)) {
			return -EINTR;
		}
		if (fault & VM_FAULT_COMPLETED) {
			mmap_read_lock(mm);
			return 0;
		}
		if (fault & VM_FAULT_RETRY) {
			tried = true;
			goto retry;
		}
		if (unlikely(fault & VM_FAULT_ERROR)) {
			int err = vm_fault_to_errno(fault, 0);

			mmap_read_unlock(mm);
			return err ? err : -EFAULT;
		}

		return 0;
	}
}

static int orlix_sync_user_page(struct mm_struct *mm,
				unsigned long fault_address,
				unsigned long page)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	pte_t entry;
	int ret;
	void *source;

	ret = orlix_fault_in_user_page_locked(mm, fault_address);
	if (ret)
		goto unmap;

	pgd = pgd_offset(mm, page);
	if (pgd_none(*pgd) || pgd_bad(*pgd))
		goto unmap;

	p4d = p4d_offset(pgd, page);
	if (p4d_none(*p4d) || p4d_bad(*p4d))
		goto unmap;

	pud = pud_offset(p4d, page);
	if (pud_none(*pud) || pud_bad(*pud))
		goto unmap;

	pmd = pmd_offset(pud, page);
	if (pmd_none(*pmd) || pmd_bad(*pmd))
		goto unmap;

	pte = pte_offset_kernel(pmd, page);
	entry = READ_ONCE(*pte);
	if (!pte_present(entry))
		goto unmap;

	source = __va(PFN_PHYS(pte_pfn(entry)));
	ret = orlix_host_user_map_page(page, source, PAGE_SIZE,
				       !!pte_write(entry),
				       !!(pte_val(entry) & _PAGE_EXEC));
	mmap_read_unlock(mm);
	return ret;

unmap:
	if (!ret)
		mmap_read_unlock(mm);
	orlix_host_user_unmap_pages(page, PAGE_SIZE);
	return ret ? ret : -EFAULT;
}

void orlix_sync_current_user_mappings(struct pt_regs *regs)
{
	struct mm_struct *mm = current->mm;
	unsigned long sp_page = regs->sp & PAGE_MASK;

	if (!mm)
		panic("Orlix: current task has no user mm for pc %#llx\n",
		      regs->pc);

	if (regs->pc && regs->pc < TASK_SIZE &&
	    orlix_sync_current_user_mapping_page(regs->pc))
		panic("Orlix: failed to synchronize hosted user pc %#llx\n",
		      regs->pc);
	if (sp_page && orlix_sync_current_user_mapping_page(sp_page))
		panic("Orlix: failed to synchronize hosted user sp page %#lx\n",
		      sp_page);

	if (orlix_hosted_sync_syscall_gate())
		panic("Orlix: failed to synchronize hosted syscall gate %#lx\n",
		      ORLIX_HOSTED_SYSCALL_GATE);
}

int orlix_sync_current_user_mapping_page(unsigned long address)
{
	struct mm_struct *mm = current->mm;
	unsigned long page = address & PAGE_MASK;

	if (!mm)
		return -EINVAL;

	if (!page || page >= TASK_SIZE)
		return 0;

	return orlix_sync_user_page(mm, address, page);
}

#define ORLIX_HOSTED_FAULT_WINDOW_PAGES	16

int orlix_sync_current_user_fault_window(unsigned long address,
					 unsigned long fault_flags)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	unsigned long page = address & PAGE_MASK;
	unsigned long start;
	unsigned long end;
	unsigned long window_start;
	unsigned long window_end;
	unsigned long loop_page;
	int ret;

	if (!address || address >= TASK_SIZE)
		return 0;

	if (!mm)
		return -EINVAL;

	if (fault_flags & ORLIX_HOST_USER_FAULT_BUS)
		return orlix_sync_current_user_mapping_page(address);

	mmap_read_lock(mm);
	vma = vma_lookup(mm, address);
	if (!vma) {
		mmap_read_unlock(mm);
		return orlix_sync_current_user_mapping_page(address);
	}
	start = vma->vm_start;
	end = vma->vm_end;
	if ((fault_flags & ORLIX_HOST_USER_FAULT_WRITE) &&
	    !(vma->vm_flags & VM_GROWSDOWN)) {
		mmap_read_unlock(mm);
		return orlix_sync_current_user_mapping_page(address);
	}
	mmap_read_unlock(mm);

	if (!start || start >= end)
		return orlix_sync_current_user_mapping_page(address);

	window_start = page;
	if (window_start > start + (ORLIX_HOSTED_FAULT_WINDOW_PAGES / 2) * PAGE_SIZE)
		window_start -= (ORLIX_HOSTED_FAULT_WINDOW_PAGES / 2) * PAGE_SIZE;
	else
		window_start = start;

	window_end = window_start + ORLIX_HOSTED_FAULT_WINDOW_PAGES * PAGE_SIZE;
	if (window_end > end || window_end < window_start)
		window_end = end;

	for (loop_page = window_start; loop_page < window_end; loop_page += PAGE_SIZE) {
		ret = orlix_sync_current_user_mapping_page(loop_page);
		if (ret)
			return ret;
	}

	return 0;
}

#endif
