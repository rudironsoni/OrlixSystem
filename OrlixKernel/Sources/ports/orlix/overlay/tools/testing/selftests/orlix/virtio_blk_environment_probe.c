// SPDX-License-Identifier: GPL-2.0
#include <stdbool.h>
#include <stddef.h>
#include <sys/stat.h>
#include <unistd.h>

#include "orlix_kselftest_user.h"

static bool path_is_block_device(const char *path)
{
	struct stat st;

	return stat(path, &st) == 0 && S_ISBLK(st.st_mode);
}

static bool file_starts_with(const char *path, const char *expected)
{
	char buffer[64];
	size_t size;

	if (orlix_read_file(path, buffer, sizeof(buffer), &size) != 0)
		return false;
	return size >= orlix_strlen(expected) &&
	       orlix_memcmp(buffer, expected, orlix_strlen(expected)) == 0;
}

static bool sysfs_size_is_nonzero(const char *path)
{
	char buffer[32];
	size_t size;
	size_t i;

	if (orlix_read_file(path, buffer, sizeof(buffer), &size) != 0)
		return false;

	for (i = 0; i < size; i++) {
		if (buffer[i] >= '1' && buffer[i] <= '9')
			return true;
		if (buffer[i] != '0' && buffer[i] != '\n')
			return false;
	}

	return false;
}

static bool block_device_can_read_sector(const char *path)
{
	char sector[512];
	int fd;
	ssize_t nread;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return false;

	nread = read(fd, sector, sizeof(sector));
	close(fd);
	return nread == sizeof(sector);
}

static bool block_device_accepts_same_sector_write(const char *path)
{
	char sector[512];
	int fd;
	ssize_t nread;
	ssize_t nwritten;

	fd = open(path, O_RDWR);
	if (fd < 0)
		return false;

	nread = pread(fd, sector, sizeof(sector), 0);
	if (nread != sizeof(sector)) {
		close(fd);
		return false;
	}

	nwritten = pwrite(fd, sector, sizeof(sector), 0);
	close(fd);
	return nwritten == sizeof(sector);
}

static bool block_device_flushes_after_write(const char *path)
{
	char sector[512];
	int fd;
	ssize_t nread;
	ssize_t nwritten;
	bool ok;

	fd = open(path, O_RDWR);
	if (fd < 0)
		return false;

	nread = pread(fd, sector, sizeof(sector), 0);
	if (nread != sizeof(sector)) {
		close(fd);
		return false;
	}

	nwritten = pwrite(fd, sector, sizeof(sector), 0);
	ok = nwritten == sizeof(sector) && fsync(fd) == 0;
	close(fd);
	return ok;
}

int main(void)
{
	orlix_test_plan(13);

	orlix_test_result(path_is_block_device("/dev/vda"),
			  "immutable base root is visible as /dev/vda block device");
	orlix_test_result(path_is_block_device("/dev/vdb"),
			  "writable state root is visible as /dev/vdb block device");
	orlix_test_result(file_starts_with("/sys/block/vda/ro", "1"),
			  "sysfs marks /dev/vda read-only");
	orlix_test_result(file_starts_with("/sys/block/vdb/ro", "0"),
			  "sysfs marks /dev/vdb writable");
	orlix_test_result(sysfs_size_is_nonzero("/sys/block/vda/size"),
			  "sysfs reports nonzero /dev/vda size");
	orlix_test_result(sysfs_size_is_nonzero("/sys/block/vdb/size"),
			  "sysfs reports nonzero /dev/vdb size");
	orlix_test_result(file_starts_with("/sys/block/vda/serial",
					   "orlix-base-block0"),
			  "sysfs exposes /dev/vda virtio block identifier");
	orlix_test_result(file_starts_with("/sys/block/vdb/serial",
					   "orlix-state-block1"),
			  "sysfs exposes /dev/vdb virtio block identifier");
	orlix_test_result(block_device_can_read_sector("/dev/vda"),
			  "/dev/vda serves sector reads through Linux block layer");
	orlix_test_result(block_device_can_read_sector("/dev/vdb"),
			  "/dev/vdb serves sector reads through Linux block layer");
	orlix_test_result(!block_device_accepts_same_sector_write("/dev/vda"),
			  "/dev/vda rejects sector writes");
	orlix_test_result(block_device_accepts_same_sector_write("/dev/vdb"),
			  "/dev/vdb accepts sector writes");
	orlix_test_result(block_device_flushes_after_write("/dev/vdb"),
			  "/dev/vdb flushes after sector writes");

	orlix_test_exit();
}
