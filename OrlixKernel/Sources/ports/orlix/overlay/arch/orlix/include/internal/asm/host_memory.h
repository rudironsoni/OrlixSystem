/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _INTERNAL_ASM_ORLIX_HOST_MEMORY_H
#define _INTERNAL_ASM_ORLIX_HOST_MEMORY_H

int orlix_host_kernel_map_page(unsigned long target_address,
			       const void *source_page,
			       unsigned long length);
void orlix_host_kernel_unmap_pages(unsigned long target_address,
				   unsigned long length);
int orlix_host_user_map_page(unsigned long target_address,
			     const void *source_page,
			     unsigned long length,
			     int writable,
			     int executable);
void orlix_host_user_unmap_pages(unsigned long target_address,
				 unsigned long length);
void *orlix_host_ioremap(unsigned long physical_address,
			 unsigned long length);
void orlix_host_iounmap(void *mapped_address);
int orlix_host_iomem_physical_address(const void *mapped_address,
				      unsigned long *physical_address);

#endif /* _INTERNAL_ASM_ORLIX_HOST_MEMORY_H */
