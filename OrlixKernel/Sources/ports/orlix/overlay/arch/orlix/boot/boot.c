// SPDX-License-Identifier: GPL-2.0-only
#include <asm/boot.h>
#include <asm/page.h>
#include <asm/thread_info.h>
#include <linux/init.h>
#include <linux/start_kernel.h>

#if defined(ORLIX_APP_HOSTED_BOOT)
#define __orlix_boot_init
static unsigned long app_hosted_boot_memory[(64UL * 1024 * 1024) /
					    sizeof(unsigned long)]
	__aligned(PAGE_SIZE);
static struct boot_params app_hosted_boot_params;
#else
#define __orlix_boot_init __init
#endif

static const struct boot_params *last_boot_params;
static int boot_handoff_count;

#if defined(ORLIX_APP_HOSTED_BOOT)
extern unsigned long init_stack[THREAD_SIZE / sizeof(unsigned long)];

static __attribute__((noreturn)) void arch_boot_start_kernel(void)
{
	unsigned long stack_top = (unsigned long)init_stack + THREAD_SIZE;
	void (*entry)(void) = start_kernel;

	asm volatile("mov sp, %0\n"
		     "blr %1\n"
		     "brk #0\n"
		     :
		     : "r" (stack_top), "r" (entry)
		     : "memory");
	__builtin_unreachable();
}
#else
static void arch_boot_start_kernel(void)
{
	start_kernel();
}
#endif

static const struct boot_params *
arch_boot_materialize_handoff(const struct boot_params *params)
{
#if defined(ORLIX_APP_HOSTED_BOOT)
	app_hosted_boot_params = *params;
	if (!app_hosted_boot_params.memory_size) {
		app_hosted_boot_params.memory_base = __pa(app_hosted_boot_memory);
		app_hosted_boot_params.memory_size = sizeof(app_hosted_boot_memory);
	}
	return &app_hosted_boot_params;
#else
	return params;
#endif
}

static void arch_boot_record_handoff(const struct boot_params *params)
{
	last_boot_params = arch_boot_materialize_handoff(params);
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

	arch_boot_start_kernel();

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
