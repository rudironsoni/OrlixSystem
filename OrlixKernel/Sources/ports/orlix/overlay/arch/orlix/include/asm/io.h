/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_ORLIX_IO_H
#define _ASM_ORLIX_IO_H

#include <linux/types.h>

#if defined(ORLIX_APP_HOSTED_BOOT)
#include <internal/asm/host_memory.h>
#endif

#define ORLIX_MMIO_APERTURE_BASE 0x10000000UL
#define ORLIX_MMIO_APERTURE_SIZE 0x00010000UL

static inline bool orlix_ioremap_in_profile_aperture(phys_addr_t offset,
						     size_t size)
{
	phys_addr_t end;

	if (!size)
		return false;
	if (offset < ORLIX_MMIO_APERTURE_BASE)
		return false;
	if (offset > (phys_addr_t)-1 - size)
		return false;

	end = offset + size;
	return end <= ORLIX_MMIO_APERTURE_BASE + ORLIX_MMIO_APERTURE_SIZE;
}

#define ioremap ioremap
static inline void __iomem *ioremap(phys_addr_t offset, size_t size)
{
#if defined(ORLIX_APP_HOSTED_BOOT)
	if (orlix_ioremap_in_profile_aperture(offset, size))
		return (void __iomem *)orlix_host_ioremap((unsigned long)offset,
							  (unsigned long)size);
#endif
	return NULL;
}

#define iounmap iounmap
static inline void iounmap(volatile void __iomem *addr)
{
#if defined(ORLIX_APP_HOSTED_BOOT)
	orlix_host_iounmap((void *)addr);
#else
	(void)addr;
#endif
}

#include <asm-generic/io.h>

#endif /* _ASM_ORLIX_IO_H */
