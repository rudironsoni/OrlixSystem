// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kselftest.h"

static char *read_file(const char *path)
{
	FILE *file = fopen(path, "rb");
	char *buffer;
	size_t capacity = 4096;
	size_t size = 0;

	if (!file)
		ksft_exit_fail_msg("missing file %s: %s\n", path, strerror(errno));

	buffer = malloc(capacity);
	if (!buffer)
		ksft_exit_fail_msg("malloc failed\n");

	for (;;) {
		size_t nread;

		if (size + 1 == capacity) {
			char *grown;

			capacity *= 2;
			grown = realloc(buffer, capacity);
			if (!grown)
				ksft_exit_fail_msg("realloc failed\n");
			buffer = grown;
		}

		nread = fread(buffer + size, 1, capacity - size - 1, file);
		size += nread;
		if (nread == 0)
			break;
	}
	if (ferror(file))
		ksft_exit_fail_msg("fread failed for %s: %s\n", path, strerror(errno));
	buffer[size] = '\0';
	fclose(file);

	return buffer;
}

static bool contains_string(const char *haystack, const char *needle)
{
	return strstr(haystack, needle) != NULL;
}

static bool contains_dt_string(const char *data, const char *expected)
{
	return contains_string(data, expected);
}

int main(void)
{
	char *cmdline = read_file("/proc/cmdline");
	char *bootargs = read_file("/proc/device-tree/chosen/bootargs");
	char *compatible = read_file("/proc/device-tree/compatible");
	const char *profile = NULL;

	ksft_print_header();
	ksft_set_plan(6);

	if (contains_string(cmdline, "orlix.profile=appstore"))
		profile = "appstore";
	else if (contains_string(cmdline, "orlix.profile=development"))
		profile = "development";

	ksft_test_result(profile != NULL, "cmdline selects a supported Orlix profile\n");
	ksft_test_result(contains_string(cmdline, "console=ttyS0"),
			 "cmdline selects the Orlix serial console\n");
	ksft_test_result(!contains_string(cmdline, "root=/dev/ram0"),
			 "cmdline does not select an absent ram block root\n");
	ksft_test_result(profile && contains_string(bootargs, profile),
			 "live device tree bootargs match the selected profile\n");
	ksft_test_result(contains_dt_string(compatible, "orlix"),
			 "live device tree exposes the Orlix compatible string\n");
	ksft_test_result(profile && contains_dt_string(compatible, profile),
			 "live device tree exposes the selected profile compatible string\n");

	free(cmdline);
	free(bootargs);
	free(compatible);
	ksft_finished();
}
