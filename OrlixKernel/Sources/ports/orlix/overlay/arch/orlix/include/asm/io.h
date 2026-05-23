/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_ORLIX_IO_H
#define _ASM_ORLIX_IO_H

#include <linux/types.h>

#if defined(ORLIX_APP_HOSTED_BOOT)
#include <internal/asm/host_memory.h>
#include <internal/asm/iomem.h>
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

#if defined(ORLIX_APP_HOSTED_BOOT)
#define __raw_readb __raw_readb
static inline u8 __raw_readb(const volatile void __iomem *addr)
{
	return orlix_iomem_read8(addr);
}

#define __raw_readw __raw_readw
static inline u16 __raw_readw(const volatile void __iomem *addr)
{
	return orlix_iomem_read16(addr);
}

#define __raw_readl __raw_readl
static inline u32 __raw_readl(const volatile void __iomem *addr)
{
	return orlix_iomem_read32(addr);
}

#define __raw_writeb __raw_writeb
static inline void __raw_writeb(u8 value, volatile void __iomem *addr)
{
	orlix_iomem_write8(value, addr);
}

#define __raw_writew __raw_writew
static inline void __raw_writew(u16 value, volatile void __iomem *addr)
{
	orlix_iomem_write16(value, addr);
}

#define __raw_writel __raw_writel
static inline void __raw_writel(u32 value, volatile void __iomem *addr)
{
	orlix_iomem_write32(value, addr);
}
#endif

#include <asm-generic/io.h>

#endif /* _ASM_ORLIX_IO_H */
