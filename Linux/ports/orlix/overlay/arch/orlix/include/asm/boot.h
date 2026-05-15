#ifndef _ASM_ORLIX_BOOT_H
#define _ASM_ORLIX_BOOT_H

struct boot_params {
	const char *cmdline;
	unsigned long memory_base;
	unsigned long memory_size;
	const void *initrd_base;
	unsigned long initrd_size;
	const void *dtb_base;
	unsigned long dtb_size;
	const char *root_device;
	const char *console_device;
	unsigned long flags;
};

void arch_boot_entry(const struct boot_params *params);

#endif /* _ASM_ORLIX_BOOT_H */
