// SPDX-License-Identifier: GPL-2.0
#include <stdbool.h>
#include <stddef.h>

#include "orlix_kselftest_user.h"

static char cmdline[4096];
static char bootargs[4096];
static char compatible[1024];
static char consoles[1024];

int main(void)
{
	size_t cmdline_size;
	size_t bootargs_size;
	size_t compatible_size;
	size_t consoles_size;
	bool has_cmdline;
	bool has_bootargs;
	bool has_compatible;
	bool has_consoles;
	bool release_profile;
	bool development_profile;
	const char *profile = NULL;

	orlix_test_plan(9);

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
	orlix_test_result(has_cmdline &&
			  orlix_contains(cmdline, cmdline_size, "root=/dev/vda"),
			  "cmdline selects the immutable virtio base image as root");
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

	orlix_test_exit();
}
