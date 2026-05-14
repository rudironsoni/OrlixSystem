#ifndef _ASM_ORLIX_BOOT_H
#define _ASM_ORLIX_BOOT_H

struct boot_params {
	unsigned long flags;
};

void arch_boot_entry(const struct boot_params *params);

#endif /* _ASM_ORLIX_BOOT_H */
