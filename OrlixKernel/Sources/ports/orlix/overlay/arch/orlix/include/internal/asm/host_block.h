/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _INTERNAL_ASM_ORLIX_HOST_BLOCK_H
#define _INTERNAL_ASM_ORLIX_HOST_BLOCK_H

int orlix_host_block_capacity(unsigned int device,
			      unsigned long long *sectors);
int orlix_host_block_read(unsigned int device,
			  unsigned long long sector,
			  void *buffer,
			  unsigned int length);
int orlix_host_block_write(unsigned int device,
			   unsigned long long sector,
			   const void *buffer,
			   unsigned int length);
int orlix_host_block_flush(unsigned int device);

#endif /* _INTERNAL_ASM_ORLIX_HOST_BLOCK_H */
