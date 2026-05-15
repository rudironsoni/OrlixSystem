#include <asm/boot.h>

#if defined(ORLIX_BOOT_TESTING)
static const struct boot_params *last_boot_params;
static int boot_handoff_count;
#endif

void arch_boot_entry(const struct boot_params *params)
{
#if defined(ORLIX_BOOT_TESTING)
	last_boot_params = params;
	boot_handoff_count++;
#else
	(void)params;
#endif
}

#if defined(ORLIX_BOOT_TESTING)
int arch_boot_handoff_count(void)
{
	return boot_handoff_count;
}

const struct boot_params *arch_boot_last_params(void)
{
	return last_boot_params;
}

void arch_boot_reset_handoff(void)
{
	last_boot_params = 0;
	boot_handoff_count = 0;
}
#endif
