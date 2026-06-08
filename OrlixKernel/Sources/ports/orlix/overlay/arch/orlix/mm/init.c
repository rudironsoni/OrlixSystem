// SPDX-License-Identifier: GPL-2.0-only

#include <linux/export.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/linkage.h>
#include <linux/memblock.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/slab.h>
#include <linux/pgtable.h>
#include <linux/pfn.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/sched/task_stack.h>
#include <linux/atomic.h>
#include <asm/boot.h>
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
struct orlix_host_pte_window {
	unsigned long target;
	unsigned long length;
	unsigned long pfn;
	bool writable;
	bool executable;
};

static unsigned long orlix_host_mapping_granule(void)
{
	return arch_boot_host_page_size();
}

static unsigned long orlix_host_window_start(unsigned long address)
{
	unsigned long granule = orlix_host_mapping_granule();

	return address & ~(granule - 1);
}

static unsigned long orlix_host_window_end(unsigned long start)
{
	return start + orlix_host_mapping_granule();
}

static int orlix_kernel_pte_page(unsigned long address,
				 struct orlix_host_pte_window *window)
{
	pgd_t *pgd = pgd_offset_k(address);
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	pte_t entry;

	if (pgd_none(*pgd))
		return -ENOENT;
	if (pgd_bad(*pgd))
		return -EFAULT;

	p4d = p4d_offset(pgd, address);
	if (p4d_none(*p4d))
		return -ENOENT;
	if (p4d_bad(*p4d))
		return -EFAULT;

	pud = pud_offset(p4d, address);
	if (pud_none(*pud))
		return -ENOENT;
	if (pud_bad(*pud))
		return -EFAULT;

	pmd = pmd_offset(pud, address);
	if (pmd_none(*pmd))
		return -ENOENT;
	if (pmd_bad(*pmd))
		return -EFAULT;

	pte = pte_offset_kernel(pmd, address);
	entry = READ_ONCE(*pte);
	if (!pte_present(entry))
		return -ENOENT;

	window->target = address & PAGE_MASK;
	window->length = PAGE_SIZE;
	window->pfn = pte_pfn(entry);
	window->writable = !!pte_write(entry);
	window->executable = !!(pte_val(entry) & _PAGE_EXEC);
	return 0;
}

static int orlix_sync_kernel_page(unsigned long address)
{
	struct orlix_host_pte_window window;
	void *source;
	int ret;

	ret = orlix_kernel_pte_page(address, &window);
	if (ret == -ENOENT)
		goto unmap;
	if (ret)
		return ret;

	source = __va(PFN_PHYS(window.pfn));
	return orlix_host_kernel_map_page(window.target, source, window.length);

unmap:
	orlix_host_kernel_unmap_pages(orlix_host_window_start(address),
				      orlix_host_mapping_granule());
	return 0;
}

static int orlix_fault_in_user_page_locked(struct mm_struct *mm,
					   unsigned long fault_address);

static int orlix_user_pte_window(struct mm_struct *mm,
				 unsigned long page,
				 struct orlix_host_pte_window *window)
{
	int ret;
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	pte_t entry;

	if (page >= TASK_SIZE)
		return -EFAULT;

	ret = orlix_fault_in_user_page_locked(mm, page);
	if (ret)
		return ret;

	pgd = pgd_offset(mm, page);
	if (pgd_none(*pgd) || pgd_bad(*pgd))
		goto unlock_fault;

	p4d = p4d_offset(pgd, page);
	if (p4d_none(*p4d) || p4d_bad(*p4d))
		goto unlock_fault;

	pud = pud_offset(p4d, page);
	if (pud_none(*pud) || pud_bad(*pud))
		goto unlock_fault;

	pmd = pmd_offset(pud, page);
	if (pmd_none(*pmd) || pmd_bad(*pmd))
		goto unlock_fault;

	pte = pte_offset_kernel(pmd, page);
	entry = READ_ONCE(*pte);
	if (!pte_present(entry))
		goto unlock_fault;

	window->target = page;
	window->length = PAGE_SIZE;
	window->pfn = pte_pfn(entry);
	window->writable = !!pte_write(entry);
	window->executable = !!(pte_val(entry) & _PAGE_EXEC);
	mmap_read_unlock(mm);
	return 0;

unlock_fault:
	mmap_read_unlock(mm);
	return -EFAULT;
}

static int orlix_user_present_pte_window(struct mm_struct *mm,
					 unsigned long page,
					 struct orlix_host_pte_window *window)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	pte_t entry;

	if (page >= TASK_SIZE)
		return -EFAULT;

	pgd = pgd_offset(mm, page);
	if (pgd_none(*pgd) || pgd_bad(*pgd))
		return -EFAULT;

	p4d = p4d_offset(pgd, page);
	if (p4d_none(*p4d) || p4d_bad(*p4d))
		return -EFAULT;

	pud = pud_offset(p4d, page);
	if (pud_none(*pud) || pud_bad(*pud))
		return -EFAULT;

	pmd = pmd_offset(pud, page);
	if (pmd_none(*pmd) || pmd_bad(*pmd))
		return -EFAULT;

	pte = pte_offset_kernel(pmd, page);
	entry = READ_ONCE(*pte);
	if (!pte_present(entry))
		return -EFAULT;

	window->target = page;
	window->length = PAGE_SIZE;
	window->pfn = pte_pfn(entry);
	window->writable = !!pte_write(entry);
	window->executable = !!(pte_val(entry) & _PAGE_EXEC);
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

static int orlix_sync_user_pte_page(struct mm_struct *mm, unsigned long page)
{
	struct orlix_host_pte_window window;
	int ret;
	void *source;

	ret = orlix_user_pte_window(mm, page, &window);
	if (ret)
		return ret;

	source = __va(PFN_PHYS(window.pfn));
	ret = orlix_host_user_map_page(window.target, source, window.length,
				       window.writable, window.executable);
	return ret;
}

static int orlix_refresh_user_pte_page(struct mm_struct *mm, unsigned long page)
{
	struct orlix_host_pte_window window;
	int ret;
	void *source;

	ret = orlix_user_pte_window(mm, page, &window);
	if (ret)
		return ret;

	source = __va(PFN_PHYS(window.pfn));
	ret = orlix_host_user_refresh_page(window.target, source, window.length,
					   window.writable, window.executable);
	return ret;
}

static int orlix_refresh_user_pte_page_from_kernel(struct mm_struct *mm,
						   unsigned long page,
						   const void *source)
{
	struct orlix_host_pte_window window;
	int ret;

	ret = orlix_user_present_pte_window(mm, page, &window);
	if (ret)
		return ret;

	ret = orlix_host_user_refresh_page(window.target, source, window.length,
					   window.writable, window.executable);
	return ret;
}

static int orlix_sync_user_page(struct mm_struct *mm, unsigned long page)
{
	return orlix_sync_user_pte_page(mm, page);
}

static int orlix_sync_user_host_window(struct mm_struct *mm,
				       unsigned long page)
{
	unsigned long window_start = orlix_host_window_start(page);
	unsigned long window_end = orlix_host_window_end(window_start);
	unsigned long window_pages;
	unsigned long cursor;
	struct orlix_host_user_page_segment *segments;
	unsigned long segment_count = 0;
	int page_ret = -EFAULT;
	int ret;

	if (window_end > TASK_SIZE || window_end < window_start)
		window_end = TASK_SIZE;

	window_pages = (window_end - window_start) / PAGE_SIZE;
	if (!window_pages)
		return -EFAULT;

	segments = kcalloc(window_pages, sizeof(*segments), GFP_KERNEL);
	if (!segments)
		return -ENOMEM;

	for (cursor = window_start; cursor < window_end; cursor += PAGE_SIZE) {
		struct orlix_host_pte_window window;
		const void *source;

		ret = orlix_fault_in_user_page_locked(mm, cursor);
		if (ret) {
			if (cursor == page)
				page_ret = ret;
			continue;
		}

		ret = orlix_user_present_pte_window(mm, cursor, &window);
		mmap_read_unlock(mm);
		if (ret) {
			if (cursor == page)
				page_ret = ret;
			continue;
		}

		source = __va(PFN_PHYS(window.pfn));
		segments[segment_count++] =
			(struct orlix_host_user_page_segment) {
				.target_address = window.target,
				.source_page = source,
				.length = window.length,
				.writable = window.writable,
				.executable = window.executable,
			};
		if (cursor == page)
			page_ret = 0;
	}

	if (page_ret) {
		kfree(segments);
		return page_ret;
	}

	ret = orlix_host_user_refresh_window(window_start,
					    window_end - window_start,
					    segments,
					    segment_count);
	kfree(segments);
	return ret;
}

static int orlix_sync_user_stack_page(struct mm_struct *mm, unsigned long page)
{
	return orlix_sync_user_pte_page(mm, page);
}

static int orlix_sync_current_user_stack_window(unsigned long start,
						unsigned long end);

#define ORLIX_HOSTED_STACK_ENTRY_WINDOW_PAGES	16

void orlix_sync_current_user_mappings(struct pt_regs *regs)
{
	struct mm_struct *mm = current->mm;
	unsigned long sp_page = regs->sp & PAGE_MASK;
	unsigned long stack_access_page = regs->sp ?
		((regs->sp - 1) & PAGE_MASK) : 0;
	unsigned long stack_window_start = 0;
	unsigned long stack_window_end = 0;

	if (!mm)
		panic("Orlix: current task has no user mm for pc %#llx\n",
		      regs->pc);

	/*
	 * Darwin cannot reliably deliver the hosted trap signal if the first
	 * user stack access faults on the same unmapped stack needed for signal
	 * delivery. Keep the writable user stack VMA mirrored before user entry.
	 */
	if (regs->pc && regs->pc < TASK_SIZE &&
	    orlix_sync_current_user_mapping_page(regs->pc))
		panic("Orlix: failed to synchronize hosted user pc %#llx\n",
		      regs->pc);

	if (stack_access_page) {
		stack_window_end = sp_page +
			(ORLIX_HOSTED_STACK_ENTRY_WINDOW_PAGES + 1) * PAGE_SIZE;
		if (stack_window_end > TASK_SIZE || stack_window_end < sp_page)
			stack_window_end = TASK_SIZE;
	}
	if (stack_access_page >
	    ORLIX_HOSTED_STACK_ENTRY_WINDOW_PAGES * PAGE_SIZE)
		stack_window_start = stack_access_page -
			ORLIX_HOSTED_STACK_ENTRY_WINDOW_PAGES * PAGE_SIZE;
	else
		stack_window_start = stack_access_page;

	if (stack_access_page &&
	    orlix_sync_current_user_stack_window(stack_window_start,
						stack_window_end))
		panic("Orlix: failed to synchronize hosted user stack window %#lx\n",
		      stack_access_page);
	if (stack_access_page &&
	    orlix_sync_user_stack_page(mm, stack_access_page))
		panic("Orlix: failed to synchronize hosted user stack access page %#lx\n",
		      stack_access_page);
	if (sp_page != stack_access_page &&
	    orlix_sync_user_stack_page(mm, sp_page))
		panic("Orlix: failed to synchronize hosted user sp page %#lx\n",
		      sp_page);

	if (orlix_hosted_sync_syscall_gate())
		panic("Orlix: failed to synchronize hosted syscall gate %#lx\n",
		      ORLIX_HOSTED_SYSCALL_GATE);
}

void orlix_sync_current_user_minimal_mappings(struct pt_regs *regs)
{
	unsigned long sp_page = regs->sp & PAGE_MASK;
	unsigned long stack_access_page = regs->sp ?
		((regs->sp - 1) & PAGE_MASK) : 0;

	if (!current->mm)
		panic("Orlix: current task has no user mm for pc %#llx\n",
		      regs->pc);

	if (regs->pc && regs->pc < TASK_SIZE &&
	    orlix_sync_current_user_mapping_page(regs->pc))
		panic("Orlix: failed to synchronize hosted user pc %#llx\n",
		      regs->pc);
	if (stack_access_page &&
	    orlix_sync_user_stack_page(current->mm, stack_access_page))
		panic("Orlix: failed to synchronize hosted user stack access page %#lx\n",
		      stack_access_page);
	if (sp_page != stack_access_page &&
	    orlix_sync_user_stack_page(current->mm, sp_page))
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

	return orlix_sync_user_host_window(mm, page);
}

int orlix_refresh_current_user_mapping_page(unsigned long address)
{
	struct mm_struct *mm = current->mm;
	unsigned long page = address & PAGE_MASK;

	if (!mm)
		return -EINVAL;

	if (!page || page >= TASK_SIZE)
		return 0;

	return orlix_refresh_user_pte_page(mm, page);
}

int orlix_refresh_current_user_mapping_page_from_kernel(unsigned long address,
							const void *source_page)
{
	struct mm_struct *mm = current->mm;
	unsigned long page = address & PAGE_MASK;

	if (!mm)
		return -EINVAL;

	if (!page || page >= TASK_SIZE)
		return 0;

	return orlix_refresh_user_pte_page_from_kernel(mm, page, source_page);
}

static int orlix_sync_current_user_stack_window(unsigned long start,
						unsigned long end)
{
	unsigned long page;
	int ret;

	if (!start || start >= TASK_SIZE || start >= end)
		return 0;
	if (end > TASK_SIZE)
		end = TASK_SIZE;

	for (page = start; page < end; page += PAGE_SIZE) {
		ret = orlix_sync_user_stack_page(current->mm, page);
		if (ret)
			continue;
	}

	return 0;
}

static unsigned long orlix_hosted_fault_window_pages(void)
{
	return max_t(unsigned long, 1, arch_boot_host_page_size() / PAGE_SIZE);
}

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
	unsigned long window_pages;
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
	mmap_read_unlock(mm);

	if (!start || start >= end)
		return orlix_sync_current_user_mapping_page(address);

	window_pages = orlix_hosted_fault_window_pages();
	window_start = page;
	if (window_start > start + (window_pages / 2) * PAGE_SIZE)
		window_start -= (window_pages / 2) * PAGE_SIZE;
	else
		window_start = start;

	window_end = window_start + window_pages * PAGE_SIZE;
	if (window_end > end || window_end < window_start)
		window_end = end;

	ret = orlix_sync_user_host_window(mm, page);
	if (ret)
		return ret;

	for (loop_page = window_start; loop_page < window_end; loop_page += PAGE_SIZE) {
		if (orlix_host_window_start(loop_page) == orlix_host_window_start(page))
			continue;
		(void)orlix_sync_user_host_window(mm, loop_page);
	}

	return 0;
}

#endif
