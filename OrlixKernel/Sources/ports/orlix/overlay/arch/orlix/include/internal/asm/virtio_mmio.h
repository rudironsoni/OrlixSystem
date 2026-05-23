/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _INTERNAL_ASM_ORLIX_VIRTIO_MMIO_H
#define _INTERNAL_ASM_ORLIX_VIRTIO_MMIO_H

#include <linux/types.h>

bool orlix_virtio_mmio_read8(unsigned long physical_address, u8 *value);
bool orlix_virtio_mmio_read16(unsigned long physical_address, u16 *value);
bool orlix_virtio_mmio_read32(unsigned long physical_address, u32 *value);

bool orlix_virtio_mmio_write8(unsigned long physical_address, u8 value);
bool orlix_virtio_mmio_write16(unsigned long physical_address, u16 value);
bool orlix_virtio_mmio_write32(unsigned long physical_address, u32 value);

#endif /* _INTERNAL_ASM_ORLIX_VIRTIO_MMIO_H */
