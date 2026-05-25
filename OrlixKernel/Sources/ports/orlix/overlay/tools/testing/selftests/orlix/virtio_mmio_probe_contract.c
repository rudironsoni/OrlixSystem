// SPDX-License-Identifier: GPL-2.0
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "orlix_kselftest_user.h"

static char property_data[512];
static char property_path_buffer[128];

static void build_property_path(const char *node, const char *property)
{
	const char prefix[] = "/proc/device-tree/";
	size_t pos = 0;
	size_t i;

	for (i = 0; prefix[i]; i++)
		property_path_buffer[pos++] = prefix[i];
	for (i = 0; node[i]; i++)
		property_path_buffer[pos++] = node[i];
	property_path_buffer[pos++] = '/';
	for (i = 0; property[i]; i++)
		property_path_buffer[pos++] = property[i];
	property_path_buffer[pos] = '\0';
}

static bool read_property(const char *node, const char *property, size_t *size)
{
	build_property_path(node, property);
	return orlix_read_file(property_path_buffer, property_data,
			       sizeof(property_data), size) == 0;
}

static bool property_contains_string(const char *node, const char *property,
				     const char *expected)
{
	size_t size;

	if (!read_property(node, property, &size))
		return false;
	return orlix_contains(property_data, size, expected);
}

static bool property_matches_u32_cells(const char *node, const char *property,
				       const uint32_t *expected,
				       size_t expected_count)
{
	size_t size;
	size_t i;

	if (!read_property(node, property, &size))
		return false;
	if (size != expected_count * sizeof(uint32_t))
		return false;

	for (i = 0; i < expected_count; i++) {
		const unsigned char *cell =
			(const unsigned char *)property_data + i * sizeof(uint32_t);

		if (orlix_read_be32(cell) != expected[i])
			return false;
	}
	return true;
}

static void expect_virtio_mmio_node(const char *node, uint32_t address,
				    uint32_t interrupt)
{
	uint32_t reg[] = { 0x0, address, 0x0, 0x200 };
	uint32_t interrupts[] = { interrupt };

	orlix_test_result(property_contains_string(node, "compatible", "virtio,mmio"),
			  "virtio-mmio node uses upstream compatible string");
	orlix_test_result(property_matches_u32_cells(node, "reg", reg, 4),
			  "virtio-mmio node has expected register range");
	orlix_test_result(property_matches_u32_cells(node, "interrupts",
						     interrupts, 1),
			  "virtio-mmio node has expected interrupt");
}

int main(void)
{
	orlix_test_plan(9);

	expect_virtio_mmio_node("virtio@10001000", 0x10001000, 32);
	expect_virtio_mmio_node("virtio@10001200", 0x10001200, 33);
	expect_virtio_mmio_node("virtio@10001400", 0x10001400, 34);

	orlix_test_exit();
}
