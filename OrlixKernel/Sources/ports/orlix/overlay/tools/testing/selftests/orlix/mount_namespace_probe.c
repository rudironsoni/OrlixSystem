// SPDX-License-Identifier: GPL-2.0

#include <errno.h>
#include <sched.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include "orlix_kselftest_user.h"

#define PROBE_MOUNTPOINT "/mnt"
#define PROBE_MARKER "/mnt/orlix-mount-namespace-marker"

static bool mountinfo_contains_tmpfs_mount(void)
{
	char buffer[4096];
	size_t size;

	if (orlix_read_file("/proc/self/mountinfo", buffer, sizeof(buffer),
			    &size) != 0)
		return false;

	return orlix_contains(buffer, size, " /mnt ") &&
	       orlix_contains(buffer, size, " - tmpfs ");
}

static int child_probe(void)
{
	int fd;

	if (syscall(SYS_unshare, CLONE_NEWNS) != 0)
		return 10;
	if (mkdir(PROBE_MOUNTPOINT, 0755) < 0 && errno != EEXIST)
		return 11;
	if (mount("tmpfs", PROBE_MOUNTPOINT, "tmpfs", 0, "mode=755") != 0)
		return 12;
	if (!mountinfo_contains_tmpfs_mount())
		return 18;

	fd = open(PROBE_MARKER, O_CREAT | O_EXCL | O_WRONLY, 0644);
	if (fd < 0)
		return 13;
	if (write(fd, "child\n", 6) != 6) {
		close(fd);
		return 14;
	}
	if (close(fd) != 0)
		return 15;
	if (access(PROBE_MARKER, F_OK) != 0)
		return 16;
	if (umount(PROBE_MOUNTPOINT) != 0)
		return 17;

	return 0;
}

int main(void)
{
	int status = 0;
	pid_t child;

	orlix_test_plan(4);
	if (mkdir(PROBE_MOUNTPOINT, 0755) < 0 && errno != EEXIST) {
		orlix_test_result(false, "mount namespace probe mountpoint exists");
		orlix_test_result(false, "mount namespace child started");
		orlix_test_result(false,
				  "mount namespace child verified mountinfo");
		orlix_test_result(false, "child tmpfs mount is hidden from parent");
		orlix_test_exit();
	}

	orlix_test_result(true, "mount namespace probe mountpoint exists");
	child = fork();
	if (child == 0)
		_exit(child_probe());

	orlix_test_result(child > 0, "mount namespace child started");
	if (child > 0 && waitpid(child, &status, 0) == child) {
		orlix_test_result(WIFEXITED(status) && WEXITSTATUS(status) == 0,
				  "mount namespace child verified mountinfo");
	} else {
		orlix_test_result(false,
				  "mount namespace child verified mountinfo");
	}

	if (access(PROBE_MARKER, F_OK) == 0) {
		(void)unlink(PROBE_MARKER);
		orlix_test_result(false, "child tmpfs mount is hidden from parent");
	} else {
		orlix_test_result(errno == ENOENT,
				  "child tmpfs mount is hidden from parent");
	}

	orlix_test_exit();
}
