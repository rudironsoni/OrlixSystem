// SPDX-License-Identifier: GPL-2.0
#include <stdbool.h>
#include <stddef.h>

#include "orlix_kselftest_user.h"

static char cmdline[4096];
static char bootargs[4096];
static char compatible[1024];
static char consoles[1024];
static char root_mode[128];
static char root_modes[256];
static char base_storage_role[128];
static char state_storage_role[128];
static char block_dev[64];

int main(void)
{
	size_t cmdline_size;
	size_t bootargs_size;
	size_t compatible_size;
	size_t consoles_size;
	size_t root_mode_size;
	size_t root_modes_size;
	size_t base_storage_role_size;
	size_t state_storage_role_size;
	size_t block_dev_size;
	bool has_cmdline;
	bool has_bootargs;
	bool has_compatible;
	bool has_consoles;
	bool has_root_mode;
	bool has_root_modes;
	bool has_base_storage_role;
	bool has_state_storage_role;
	bool direct_root_cmdline;
	bool overlay_root_cmdline;
	bool initramfs_only_cmdline;
	bool release_profile;
	bool development_profile;
	const char *profile = NULL;

	orlix_test_plan(17);

	has_cmdline = orlix_read_file("/proc/cmdline", cmdline,
				      sizeof(cmdline), &cmdline_size) == 0;
	has_bootargs = orlix_read_file(
		"/sys/firmware/devicetree/base/chosen/bootargs",
		bootargs, sizeof(bootargs), &bootargs_size) == 0;
	has_compatible = orlix_read_file(
		"/sys/firmware/devicetree/base/compatible",
		compatible, sizeof(compatible), &compatible_size) == 0;
	has_consoles = orlix_read_file("/proc/consoles", consoles,
				       sizeof(consoles), &consoles_size) == 0;
	has_root_mode = orlix_read_file(
		"/sys/firmware/devicetree/base/chosen/orlix,root-mode",
		root_mode, sizeof(root_mode), &root_mode_size) == 0;
	has_root_modes = orlix_read_file(
		"/sys/firmware/devicetree/base/chosen/orlix,root-modes",
		root_modes, sizeof(root_modes), &root_modes_size) == 0;
	has_base_storage_role = orlix_read_file(
		"/sys/firmware/devicetree/base/virtio@10001000/orlix,storage-role",
		base_storage_role, sizeof(base_storage_role),
		&base_storage_role_size) == 0;
	has_state_storage_role = orlix_read_file(
		"/sys/firmware/devicetree/base/virtio@10001200/orlix,storage-role",
		state_storage_role, sizeof(state_storage_role),
		&state_storage_role_size) == 0;

	direct_root_cmdline = has_cmdline &&
		orlix_contains(cmdline, cmdline_size, "root=/dev/vda") &&
		orlix_contains(cmdline, cmdline_size, "rootfstype=ext4") &&
		orlix_contains(cmdline, cmdline_size, " ro ");
	overlay_root_cmdline = has_cmdline &&
		orlix_contains(cmdline, cmdline_size, "rdinit=/init") &&
		orlix_contains(cmdline, cmdline_size, "orlix.root=overlay");
	initramfs_only_cmdline = has_cmdline &&
		orlix_contains(cmdline, cmdline_size, "rdinit=/init") &&
		orlix_contains(cmdline, cmdline_size, "orlix.root=initramfs-only") &&
		!orlix_contains(cmdline, cmdline_size, "root=/dev/vda");

	release_profile = has_cmdline &&
		orlix_contains(cmdline, cmdline_size, "orlix.profile=release");
	development_profile = has_cmdline &&
		orlix_contains(cmdline, cmdline_size, "orlix.profile=development");

	if (release_profile)
		profile = "release";
	else if (development_profile)
		profile = "development";

	orlix_test_result(profile != NULL,
			  "cmdline selects a supported Orlix profile");
	orlix_test_result(has_cmdline &&
			  orlix_contains(cmdline, cmdline_size, "console=ttyS0"),
			  "cmdline keeps the Orlix serial console fallback");
	orlix_test_result(has_cmdline &&
			  orlix_contains(cmdline, cmdline_size, "console=hvc0"),
			  "cmdline selects the Orlix virtio console");
	orlix_test_result(has_cmdline &&
			  !orlix_contains(cmdline, cmdline_size, "root=/dev/ram0"),
			  "cmdline does not select an absent ram block root");
	orlix_test_result((direct_root_cmdline + overlay_root_cmdline +
			   initramfs_only_cmdline) == 1,
			  "cmdline selects exactly one supported Linux root handoff");
	orlix_test_result((release_profile &&
			   (direct_root_cmdline || initramfs_only_cmdline)) ||
			  (development_profile &&
			   (overlay_root_cmdline || initramfs_only_cmdline)),
			  "cmdline root handoff is allowed for the selected profile");
	orlix_test_result(direct_root_cmdline || overlay_root_cmdline ||
			  initramfs_only_cmdline,
			  "cmdline carries the selected root mode policy");
	orlix_test_result(profile && has_bootargs &&
			  orlix_contains(bootargs, bootargs_size, profile),
			  "live device tree bootargs match the selected profile");
	orlix_test_result(has_compatible &&
			  orlix_contains(compatible, compatible_size, "orlix"),
			  "live device tree exposes the Orlix compatible string");
	orlix_test_result(profile && has_compatible &&
			  orlix_contains(compatible, compatible_size, profile),
			  "live device tree exposes the selected profile compatible string");
	orlix_test_result(has_consoles &&
			  orlix_contains(consoles, consoles_size, "hvc0"),
			  "live consoles include the Orlix virtio console");
	orlix_test_result(has_root_mode &&
			  ((release_profile &&
			    orlix_contains(root_mode, root_mode_size, "direct")) ||
			   (development_profile &&
			    orlix_contains(root_mode, root_mode_size, "overlay"))),
			  "live device tree selects the profile default root mode explicitly");
	orlix_test_result(has_root_modes &&
			  orlix_contains(root_modes, root_modes_size, "direct") &&
			  orlix_contains(root_modes, root_modes_size, "overlay"),
			  "live device tree keeps direct and overlay root modes distinct");
	orlix_test_result(has_base_storage_role &&
			  orlix_contains(base_storage_role, base_storage_role_size,
					 "immutable-base"),
			  "live device tree labels vda as immutable base storage");
	orlix_test_result(has_state_storage_role &&
			  orlix_contains(state_storage_role, state_storage_role_size,
					 "writable-state"),
			  "live device tree labels vdb as writable state storage");
	orlix_test_result(orlix_read_file("/sys/block/vda/dev", block_dev,
					  sizeof(block_dev), &block_dev_size) == 0,
			  "Linux exposes the immutable base block device");
	orlix_test_result(orlix_read_file("/sys/block/vdb/dev", block_dev,
					  sizeof(block_dev), &block_dev_size) == 0,
			  "Linux exposes the writable state block device");

	orlix_test_exit();
}
