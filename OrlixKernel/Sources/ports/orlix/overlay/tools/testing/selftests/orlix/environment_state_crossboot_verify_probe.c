// SPDX-License-Identifier: GPL-2.0

#include <errno.h>
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
	bool reread;

	orlix_test_plan(3);

	mounted = mount_state();
	orlix_test_result(mounted,
			  "cross-boot writable state block remounts as ext4");

	reread = mounted && marker_rereads();
	orlix_test_result(reread,
			  "cross-boot state marker survives fresh boot boundary");
	orlix_test_result(reread && unlink(STATE_MARKER) == 0,
			  "cross-boot state marker cleanup succeeds");

	if (mounted)
		umount(STATE_MOUNT);
	orlix_test_exit();
}
