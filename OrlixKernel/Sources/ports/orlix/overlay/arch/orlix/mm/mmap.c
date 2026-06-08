// SPDX-License-Identifier: GPL-2.0-only

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/sched.h>
#include <linux/security.h>
#include <asm/boot.h>

static bool orlix_hosted_prot_none_reservation(struct file *file,
					       unsigned long flags,
					       vm_flags_t vm_flags)
{
	return !file && !(flags & MAP_FIXED) &&
	       !(vm_flags & VM_ACCESS_FLAGS);
}

static unsigned long orlix_hosted_mmap_align_mask(struct file *file,
						  unsigned long flags,
						  vm_flags_t vm_flags)
{
	unsigned long granule = arch_boot_host_page_size();

	if (granule <= PAGE_SIZE ||
	    !orlix_hosted_prot_none_reservation(file, flags, vm_flags))
		return 0;
	return granule - 1;
}

static unsigned long orlix_hosted_mmap_align_offset(struct file *file,
						    unsigned long flags,
						    vm_flags_t vm_flags)
{
	unsigned long mask = orlix_hosted_mmap_align_mask(file, flags,
							  vm_flags);

	if (!mask)
		return 0;
	return (mask + 1 - PAGE_SIZE) & mask;
}

unsigned long arch_get_unmapped_area(struct file *file, unsigned long addr,
				     unsigned long len, unsigned long pgoff,
				     unsigned long flags, vm_flags_t vm_flags)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma, *prev;
	struct vm_unmapped_area_info info = {};
	const unsigned long mmap_end = arch_get_mmap_end(addr, len, flags);

	if (!orlix_hosted_mmap_align_mask(file, flags, vm_flags))
		return generic_get_unmapped_area(file, addr, len, pgoff,
						 flags, vm_flags);

	if (len > mmap_end - mmap_min_addr)
		return -ENOMEM;

	if (addr) {
		addr = PAGE_ALIGN(addr);
		vma = find_vma_prev(mm, addr, &prev);
		if (mmap_end - len >= addr && addr >= mmap_min_addr &&
		    (!vma || addr + len <= vm_start_gap(vma)) &&
		    (!prev || addr >= vm_end_gap(prev)))
			return addr;
	}

	info.length = len;
	info.low_limit = mm->mmap_base;
	info.high_limit = mmap_end;
	info.align_mask = orlix_hosted_mmap_align_mask(file, flags,
						       vm_flags);
	info.align_offset = orlix_hosted_mmap_align_offset(file, flags,
							   vm_flags);
	return vm_unmapped_area(&info);
}

unsigned long arch_get_unmapped_area_topdown(struct file *file,
					     unsigned long addr,
					     unsigned long len,
					     unsigned long pgoff,
					     unsigned long flags,
					     vm_flags_t vm_flags)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma, *prev;
	struct vm_unmapped_area_info info = {};
	const unsigned long mmap_end = arch_get_mmap_end(addr, len, flags);
	unsigned long result;

	if (!orlix_hosted_mmap_align_mask(file, flags, vm_flags))
		return generic_get_unmapped_area_topdown(file, addr, len,
							 pgoff, flags,
							 vm_flags);

	if (len > mmap_end - mmap_min_addr)
		return -ENOMEM;

	if (addr) {
		addr = PAGE_ALIGN(addr);
		vma = find_vma_prev(mm, addr, &prev);
		if (mmap_end - len >= addr && addr >= mmap_min_addr &&
		    (!vma || addr + len <= vm_start_gap(vma)) &&
		    (!prev || addr >= vm_end_gap(prev)))
			return addr;
	}

	info.flags = VM_UNMAPPED_AREA_TOPDOWN;
	info.length = len;
	info.low_limit = PAGE_SIZE;
	info.high_limit = arch_get_mmap_base(addr, mm->mmap_base);
	info.align_mask = orlix_hosted_mmap_align_mask(file, flags,
						       vm_flags);
	info.align_offset = orlix_hosted_mmap_align_offset(file, flags,
							   vm_flags);
	result = vm_unmapped_area(&info);
	if (offset_in_page(result)) {
		VM_BUG_ON(result != -ENOMEM);
		info.flags = 0;
		info.low_limit = TASK_UNMAPPED_BASE;
		info.high_limit = mmap_end;
		result = vm_unmapped_area(&info);
	}

	return result;
}
