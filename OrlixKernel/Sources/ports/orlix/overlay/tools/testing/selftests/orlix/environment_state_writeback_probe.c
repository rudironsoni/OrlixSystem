// SPDX-License-Identifier: GPL-2.0

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#include "orlix_kselftest_user.h"

#define STATE_MOUNT "/mnt/orlix-state-writeback"
#define STATE_MARKER STATE_MOUNT "/orlix-state-writeback-probe"
#define STATE_PAYLOAD "orlix-state-writeback-ok\n"

static bool mount_state(void)
{
	if (mkdir("/mnt", 0755) < 0 && errno != EEXIST)
		return false;
	if (mkdir(STATE_MOUNT, 0755) < 0 && errno != EEXIST)
		return false;
	return mount("/dev/vdb", STATE_MOUNT, "ext4", 0, NULL) == 0;
}

static bool write_marker_and_sync(void)
{
	int fd;
	ssize_t written;

	fd = open(STATE_MARKER, O_CREAT | O_TRUNC | O_WRONLY, 0644);
	if (fd < 0)
		return false;

	written = write(fd, STATE_PAYLOAD, sizeof(STATE_PAYLOAD) - 1);
	if (written != (ssize_t)(sizeof(STATE_PAYLOAD) - 1)) {
		close(fd);
		return false;
	}
	if (fsync(fd) != 0) {
		close(fd);
		return false;
	}
	return close(fd) == 0;
}

static bool marker_rereads(void)
{
	char buffer[64];
	size_t size;

	if (orlix_read_file(STATE_MARKER, buffer, sizeof(buffer), &size) != 0)
		return false;
	return size >= sizeof(STATE_PAYLOAD) - 1 &&
	       orlix_memcmp(buffer, STATE_PAYLOAD, sizeof(STATE_PAYLOAD) - 1) == 0;
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
	bool mounted;
	bool wrote;
	bool reread;

	orlix_test_plan(7);

	mounted = mount_state();
	orlix_test_result(mounted, "writable state block mounts as ext4");

	wrote = mounted && write_marker_and_sync();
	orlix_test_result(wrote, "environment state marker write succeeds");
	orlix_test_result(wrote, "environment state marker sync succeeds");

	if (mounted)
		umount(STATE_MOUNT);

	mounted = mount_state();
	orlix_test_result(mounted, "writable state block remounts after sync");

	reread = mounted && wrote && marker_rereads();
	orlix_test_result(reread, "environment state marker reread succeeds");
	orlix_test_result(!block_device_accepts_same_sector_write("/dev/vda"),
			  "immutable base block still rejects writes");
	orlix_test_result(block_device_flushes_after_write("/dev/vdb"),
			  "writable state block still flushes writes");

	if (reread)
		unlink(STATE_MARKER);
	if (mounted)
		umount(STATE_MOUNT);
	orlix_test_exit();
}
