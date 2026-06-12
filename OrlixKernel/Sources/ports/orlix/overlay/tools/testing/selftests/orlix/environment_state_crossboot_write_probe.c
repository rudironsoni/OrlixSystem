// SPDX-License-Identifier: GPL-2.0

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#include "orlix_kselftest_user.h"

#define STATE_MOUNT "/mnt/orlix-state-crossboot"
#define STATE_MARKER STATE_MOUNT "/orlix-state-crossboot-probe"
#define STATE_PAYLOAD "orlix-state-crossboot-ok\n"

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

	(void)unlink(STATE_MARKER);
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

int main(void)
{
	bool mounted;
	bool wrote;

	orlix_test_plan(4);

	mounted = mount_state();
	orlix_test_result(mounted, "cross-boot writable state block mounts as ext4");

	wrote = mounted && write_marker_and_sync();
	orlix_test_result(wrote, "cross-boot state marker write succeeds");
	orlix_test_result(wrote, "cross-boot state marker sync succeeds");
	orlix_test_result(wrote && marker_rereads(),
			  "cross-boot state marker remains readable before boot boundary");

	if (mounted)
		umount(STATE_MOUNT);
	orlix_test_exit();
}
