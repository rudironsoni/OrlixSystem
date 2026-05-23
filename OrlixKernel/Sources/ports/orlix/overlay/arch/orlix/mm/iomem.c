// SPDX-License-Identifier: GPL-2.0-only

#include <linux/types.h>
#include <internal/asm/host_memory.h>
#include <internal/asm/iomem.h>
#include <internal/asm/virtio_mmio.h>

static bool orlix_iomem_physical_address(const volatile void __iomem *addr,
					 unsigned long *physical_address)
{
#if defined(ORLIX_APP_HOSTED_BOOT)
	return orlix_host_iomem_physical_address((const void *)addr,
						 physical_address) == 0;
#else
	(void)addr;
	(void)physical_address;
	return false;
#endif
}

u8 orlix_iomem_read8(const volatile void __iomem *addr)
{
	unsigned long physical_address;
	u8 value;

	if (orlix_iomem_physical_address(addr, &physical_address) &&
	    orlix_virtio_mmio_read8(physical_address, &value))
		return value;

	return *(const volatile u8 *)addr;
}

u16 orlix_iomem_read16(const volatile void __iomem *addr)
{
	unsigned long physical_address;
	u16 value;

	if (orlix_iomem_physical_address(addr, &physical_address) &&
	    orlix_virtio_mmio_read16(physical_address, &value))
		return value;

	return *(const volatile u16 *)addr;
}

u32 orlix_iomem_read32(const volatile void __iomem *addr)
{
	unsigned long physical_address;
	u32 value;

	if (orlix_iomem_physical_address(addr, &physical_address) &&
	    orlix_virtio_mmio_read32(physical_address, &value))
		return value;

	return *(const volatile u32 *)addr;
}

void orlix_iomem_write8(u8 value, volatile void __iomem *addr)
{
	unsigned long physical_address;

	if (orlix_iomem_physical_address(addr, &physical_address) &&
	    orlix_virtio_mmio_write8(physical_address, value))
		return;

	*(volatile u8 *)addr = value;
}

void orlix_iomem_write16(u16 value, volatile void __iomem *addr)
{
	unsigned long physical_address;

	if (orlix_iomem_physical_address(addr, &physical_address) &&
	    orlix_virtio_mmio_write16(physical_address, value))
		return;

	*(volatile u16 *)addr = value;
}

void orlix_iomem_write32(u32 value, volatile void __iomem *addr)
{
	unsigned long physical_address;

	if (orlix_iomem_physical_address(addr, &physical_address) &&
	    orlix_virtio_mmio_write32(physical_address, value))
		return;

	*(volatile u32 *)addr = value;
}
