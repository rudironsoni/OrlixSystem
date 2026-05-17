#include <asm/boot.h>
#include <linux/init.h>
#include <linux/start_kernel.h>

static const struct boot_params *last_boot_params;
static int boot_handoff_count;

static void arch_boot_record_handoff(const struct boot_params *params)
{
	last_boot_params = params;
	boot_handoff_count++;
}

void __init arch_boot_entry(const struct boot_params *params)
{
	arch_boot_record_handoff(params);
	start_kernel();
}

const struct boot_params *arch_boot_params(void)
{
	return last_boot_params;
}

#if defined(CONFIG_ORLIX_BOOT_KUNIT_TEST)
void arch_boot_test_record_handoff(const struct boot_params *params)
{
	arch_boot_record_handoff(params);
}

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
