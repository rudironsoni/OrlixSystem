/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _INTERNAL_ASM_ORLIX_HOST_MEMORY_H
#define _INTERNAL_ASM_ORLIX_HOST_MEMORY_H

int orlix_host_kernel_map_page(unsigned long target_address,
			       const void *source_page,
			       unsigned long length);
void orlix_host_kernel_unmap_pages(unsigned long target_address,
				   unsigned long length);
void *orlix_host_ioremap(unsigned long physical_address,
			 unsigned long length);
void orlix_host_iounmap(void *mapped_address);

#endif /* _INTERNAL_ASM_ORLIX_HOST_MEMORY_H */
