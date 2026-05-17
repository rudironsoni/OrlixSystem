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
const struct boot_params *arch_boot_params(void);

#if defined(CONFIG_ORLIX_BOOT_KUNIT_TEST)
void arch_boot_test_record_handoff(const struct boot_params *params);
int arch_boot_handoff_count(void);
const struct boot_params *arch_boot_last_params(void);
void arch_boot_reset_handoff(void);
#endif

#endif /* _ASM_ORLIX_BOOT_H */
