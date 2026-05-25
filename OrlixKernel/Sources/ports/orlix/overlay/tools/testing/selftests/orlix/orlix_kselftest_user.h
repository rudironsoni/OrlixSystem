/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ORLIX_KSELFTEST_USER_H
#define ORLIX_KSELFTEST_USER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>

static int orlix_test_index;
static int orlix_test_failures;

static size_t orlix_strlen(const char *s)
{
	size_t len = 0;

	while (s[len])
		len++;
	return len;
}

static int orlix_memcmp(const void *left, const void *right, size_t len)
{
	const unsigned char *a = left;
	const unsigned char *b = right;
	size_t i;

	for (i = 0; i < len; i++) {
		if (a[i] != b[i])
			return (int)a[i] - (int)b[i];
	}
	return 0;
}

static void orlix_write_all(const char *s)
{
	size_t len = orlix_strlen(s);

	while (len > 0) {
		ssize_t written = write(STDOUT_FILENO, s, len);

		if (written <= 0)
			_exit(125);
		s += written;
		len -= (size_t)written;
	}
}

static void orlix_write_bytes(const char *s, size_t len)
{
	while (len > 0) {
		ssize_t written = write(STDOUT_FILENO, s, len);

		if (written <= 0)
			_exit(125);
		s += written;
		len -= (size_t)written;
	}
}

static size_t orlix_append_bytes(char *buffer, size_t pos, size_t capacity,
				 const char *s, size_t len)
{
	size_t i;

	for (i = 0; i < len && pos + 1 < capacity; i++)
		buffer[pos++] = s[i];
	return pos;
}

static size_t orlix_append_cstr(char *buffer, size_t pos, size_t capacity,
				const char *s)
{
	return orlix_append_bytes(buffer, pos, capacity, s, orlix_strlen(s));
}

static size_t orlix_append_uint(char *buffer, size_t pos, size_t capacity,
				unsigned int value)
{
	char digits[16];
	size_t digit_pos = sizeof(digits);

	digits[--digit_pos] = '\0';
	do {
		digits[--digit_pos] = (char)('0' + (value % 10));
		value /= 10;
	} while (value);
	return orlix_append_cstr(buffer, pos, capacity, &digits[digit_pos]);
}

static void orlix_test_plan(unsigned int count)
{
	char line[48];
	size_t pos = 0;

	pos = orlix_append_cstr(line, pos, sizeof(line), "TAP version 13\n1..");
	pos = orlix_append_uint(line, pos, sizeof(line), count);
	pos = orlix_append_cstr(line, pos, sizeof(line), "\n");
	orlix_write_bytes(line, pos);
}

static void orlix_test_comment(const char *prefix, const char *name,
			       size_t name_len)
{
	char line[192];
	size_t pos = 0;

	pos = orlix_append_cstr(line, pos, sizeof(line), "# ");
	pos = orlix_append_cstr(line, pos, sizeof(line), prefix);
	pos = orlix_append_bytes(line, pos, sizeof(line), name, name_len);
	pos = orlix_append_cstr(line, pos, sizeof(line), "\n");
	orlix_write_bytes(line, pos);
}

static void orlix_test_result(bool passed, const char *name)
{
	char line[256];
	size_t pos = 0;

	orlix_test_index++;
	if (!passed)
		orlix_test_failures++;

	pos = orlix_append_cstr(line, pos, sizeof(line),
				passed ? "ok " : "not ok ");
	pos = orlix_append_uint(line, pos, sizeof(line),
				(unsigned int)orlix_test_index);
	pos = orlix_append_cstr(line, pos, sizeof(line), " - ");
	pos = orlix_append_cstr(line, pos, sizeof(line), name);
	pos = orlix_append_cstr(line, pos, sizeof(line), "\n");
	orlix_write_bytes(line, pos);
}

static void orlix_test_exit(void)
{
	_exit(orlix_test_failures ? 1 : 0);
}

static bool orlix_contains(const char *haystack, size_t size, const char *needle)
{
	size_t needle_len = orlix_strlen(needle);
	size_t i;

	if (needle_len == 0)
		return true;
	if (needle_len > size)
		return false;
	for (i = 0; i + needle_len <= size; i++) {
		if (orlix_memcmp(haystack + i, needle, needle_len) == 0)
			return true;
	}
	return false;
}

static int orlix_read_file(const char *path, char *buffer, size_t capacity,
			   size_t *size)
{
	int fd = open(path, O_RDONLY);

	if (fd < 0)
		return -1;

	*size = 0;
	while (*size + 1 < capacity) {
		ssize_t nread = read(fd, buffer + *size, capacity - *size - 1);

		if (nread < 0) {
			close(fd);
			return -1;
		}
		if (nread == 0)
			break;
		*size += (size_t)nread;
	}
	buffer[*size] = '\0';
	close(fd);
	return 0;
}

static uint32_t orlix_read_be32(const unsigned char *data)
{
	return ((uint32_t)data[0] << 24) |
	       ((uint32_t)data[1] << 16) |
	       ((uint32_t)data[2] << 8) |
	       (uint32_t)data[3];
}

#endif /* ORLIX_KSELFTEST_USER_H */
