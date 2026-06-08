#include <linux/init.h>
#include <linux/initrd.h>
#include <linux/memblock.h>
#include <linux/mm.h>
#include <linux/of_fdt.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <linux/types.h>
#include <asm/boot.h>
#include <asm/pgtable.h>
#include <asm/setup.h>

static char command_line[COMMAND_LINE_SIZE] __initdata;

#if defined(ORLIX_APP_HOSTED_BOOT)
static bool orlix_boot_memory_added __initdata;

static void __init orlix_add_boot_memory(const struct boot_params *params)
{
	if (!params || !params->memory_size || orlix_boot_memory_added)
		return;

	memblock_add(params->memory_base, params->memory_size);
	orlix_boot_memory_added = true;
}

void __init early_init_dt_add_memory_arch(u64 base, u64 size)
{
	const struct boot_params *params = arch_boot_params();

	if (params && params->memory_size) {
		orlix_add_boot_memory(params);
		return;
	}

	memblock_add(base, size);
}
#endif

static bool __init orlix_setup_devtree(const struct boot_params *params)
{
#if defined(CONFIG_OF_EARLY_FLATTREE)
	if (!params || !params->dtb_base || !params->dtb_size)
		return false;
	if (!early_init_dt_scan((void *)params->dtb_base)) {
		pr_err("Orlix boot handoff supplied an invalid device tree\n");
		return false;
	}

	early_init_fdt_scan_reserved_mem();
	unflatten_device_tree();
	return true;
#else
	return false;
#endif
}

static void __init orlix_setup_initrd(const struct boot_params *params)
{
#if defined(CONFIG_BLK_DEV_INITRD)
	unsigned long initrd_size;
	void *initrd;

	if (!params || !params->initrd_base || !params->initrd_size)
		return;

	initrd_size = PAGE_ALIGN(params->initrd_size);
	initrd = memblock_alloc(initrd_size, PAGE_SIZE);
	if (!initrd)
		panic("Orlix: failed to allocate initrd memory\n");

	memcpy(initrd, (const void *)params->initrd_base, params->initrd_size);
	initrd_start = (unsigned long)initrd;
	initrd_end = initrd_start + params->initrd_size;
	initrd_below_start_ok = 1;
#else
	(void)params;
#endif
}

#if defined(ORLIX_APP_HOSTED_BOOT) && defined(CONFIG_BLK_DEV_INITRD)
void __init free_initrd_mem(unsigned long start, unsigned long end)
{
	free_reserved_area((void *)start, (void *)PAGE_ALIGN(end),
			   POISON_FREE_INITMEM, "initrd");
}
#endif

void __init setup_arch(char **cmdline_p)
{
	const struct boot_params *params = arch_boot_params();
	bool dt_ready = orlix_setup_devtree(params);

	if (params && params->cmdline)
		strscpy(boot_command_line, params->cmdline, COMMAND_LINE_SIZE);
	strscpy(command_line, boot_command_line, COMMAND_LINE_SIZE);
	*cmdline_p = command_line;

	if (!dt_ready && params && params->memory_size)
		memblock_add(params->memory_base, params->memory_size);
	orlix_setup_initrd(params);
	paging_init();

	pr_info("Orlix architecture setup\n");
}
