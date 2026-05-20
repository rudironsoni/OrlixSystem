// SPDX-License-Identifier: GPL-2.0-only
#include <asm/boot.h>
#include <linux/init.h>
#include <linux/start_kernel.h>

#if defined(ORLIX_APP_HOSTED_BOOT)
#define __orlix_boot_init
#else
#define __orlix_boot_init __init
#endif

static const struct boot_params *last_boot_params;
static int boot_handoff_count;

static void arch_boot_record_handoff(const struct boot_params *params)
{
	last_boot_params = params;
	boot_handoff_count++;
}

static int arch_boot_params_valid(const struct boot_params *params)
{
	return params && params->cmdline && params->cmdline[0] &&
	       params->dtb_base && params->dtb_size &&
	       params->root_device && params->root_device[0] &&
	       params->console_device && params->console_device[0];
}

int arch_boot_prepare_entry(const struct boot_params *params)
{
	if (!arch_boot_params_valid(params))
		return ORLIX_ARCH_BOOT_INVALID_CONFIG;

	arch_boot_record_handoff(params);
	return ORLIX_ARCH_BOOT_OK;
}

int __orlix_boot_init arch_boot_entry(const struct boot_params *params)
{
	int status = arch_boot_prepare_entry(params);

	if (status != ORLIX_ARCH_BOOT_OK)
		return status;

	start_kernel();

	return ORLIX_ARCH_BOOT_OK;
}

const struct boot_params *arch_boot_params(void)
{
	return last_boot_params;
}

#if defined(CONFIG_ORLIX_BOOT_KUNIT_TEST) || defined(ORLIX_APP_HOSTED_BOOT)
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
