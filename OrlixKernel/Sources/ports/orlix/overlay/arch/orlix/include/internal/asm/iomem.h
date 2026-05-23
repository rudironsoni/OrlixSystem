/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _INTERNAL_ASM_ORLIX_IOMEM_H
#define _INTERNAL_ASM_ORLIX_IOMEM_H

#include <linux/types.h>

u8 orlix_iomem_read8(const volatile void __iomem *addr);
u16 orlix_iomem_read16(const volatile void __iomem *addr);
u32 orlix_iomem_read32(const volatile void __iomem *addr);

void orlix_iomem_write8(u8 value, volatile void __iomem *addr);
void orlix_iomem_write16(u16 value, volatile void __iomem *addr);
void orlix_iomem_write32(u32 value, volatile void __iomem *addr);

#endif /* _INTERNAL_ASM_ORLIX_IOMEM_H */
