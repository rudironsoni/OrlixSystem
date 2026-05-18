// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kselftest.h"

static unsigned char *read_property(const char *path, size_t *size)
{
	FILE *file = fopen(path, "rb");
	unsigned char *buffer;
	size_t capacity = 128;

	if (!file)
		ksft_exit_fail_msg("missing property %s: %s\n", path, strerror(errno));

	buffer = malloc(capacity);
	if (!buffer)
		ksft_exit_fail_msg("malloc failed\n");

	*size = 0;
	for (;;) {
		size_t nread;

		if (*size + 1 == capacity) {
			unsigned char *grown;

			capacity *= 2;
			grown = realloc(buffer, capacity);
			if (!grown)
				ksft_exit_fail_msg("realloc failed\n");
			buffer = grown;
		}

		nread = fread(buffer + *size, 1, capacity - *size - 1, file);
		*size += nread;
		if (nread == 0)
			break;
	}
	if (ferror(file))
		ksft_exit_fail_msg("fread failed for %s: %s\n", path, strerror(errno));
	buffer[*size] = 0;
	fclose(file);

	return buffer;
}

static char *property_path(const char *node, const char *property)
{
	char *path;
	int len = snprintf(NULL, 0, "/proc/device-tree/%s/%s", node, property);

	if (len < 0)
		ksft_exit_fail_msg("snprintf failed\n");
	path = malloc((size_t)len + 1);
	if (!path)
		ksft_exit_fail_msg("malloc failed\n");
	snprintf(path, (size_t)len + 1, "/proc/device-tree/%s/%s", node, property);
	return path;
}

static bool property_contains_string(const char *node, const char *property,
				     const char *expected)
{
	size_t size;
	char *path = property_path(node, property);
	unsigned char *data = read_property(path, &size);
	bool matched = false;
	size_t i;

	for (i = 0; i + strlen(expected) <= size; i++) {
		if (memcmp(data + i, expected, strlen(expected)) == 0) {
			matched = true;
			break;
		}
	}

	free(data);
	free(path);
	return matched;
}

static uint32_t read_be32(const unsigned char *data)
{
	return ((uint32_t)data[0] << 24) |
	       ((uint32_t)data[1] << 16) |
	       ((uint32_t)data[2] << 8) |
	       (uint32_t)data[3];
}

static bool property_matches_u32_cells(const char *node, const char *property,
					       const uint32_t *expected,
					       size_t expected_count)
{
	size_t size;
	char *path = property_path(node, property);
	unsigned char *data = read_property(path, &size);
	bool matched = size == expected_count * sizeof(uint32_t);
	size_t i;

	if (matched) {
		for (i = 0; i < expected_count; i++) {
			if (read_be32(data + i * sizeof(uint32_t)) != expected[i]) {
				matched = false;
				break;
			}
		}
	}

	free(data);
	free(path);
	return matched;
}

static void expect_virtio_mmio_node(const char *node, uint32_t address,
				    uint32_t interrupt)
{
	uint32_t reg[] = { 0x0, address, 0x0, 0x200 };
	uint32_t interrupts[] = { interrupt };
	char test_name[128];

	snprintf(test_name, sizeof(test_name), "%s uses upstream virtio-mmio compatible", node);
	ksft_test_result(property_contains_string(node, "compatible", "virtio,mmio"),
			 "%s\n", test_name);

	snprintf(test_name, sizeof(test_name), "%s has expected MMIO register range", node);
	ksft_test_result(property_matches_u32_cells(node, "reg", reg, 4),
			 "%s\n", test_name);

	snprintf(test_name, sizeof(test_name), "%s has expected interrupt", node);
	ksft_test_result(property_matches_u32_cells(node, "interrupts", interrupts, 1),
			 "%s\n", test_name);
}

int main(void)
{
	ksft_print_header();
	ksft_set_plan(6);

	expect_virtio_mmio_node("virtio@10001000", 0x10001000, 32);
	expect_virtio_mmio_node("virtio@10001200", 0x10001200, 33);

	ksft_finished();
}
