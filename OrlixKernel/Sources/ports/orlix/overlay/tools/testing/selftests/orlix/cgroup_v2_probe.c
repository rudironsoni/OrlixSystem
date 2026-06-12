// SPDX-License-Identifier: GPL-2.0

#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <unistd.h>

#include "orlix_kselftest_user.h"

static bool file_is_readable(const char *path)
{
	char buffer[256];
	size_t size = 0;

	return orlix_read_file(path, buffer, sizeof(buffer), &size) == 0;
}

static bool proc_self_cgroup_has_v2_root(void)
{
	char buffer[512];
	size_t size = 0;

	return orlix_read_file("/proc/self/cgroup", buffer, sizeof(buffer),
			       &size) == 0 &&
	       orlix_contains(buffer, size, "0::/");
}

static bool mountinfo_has_cgroup2_root(void)
{
	char buffer[4096];
	size_t size = 0;

	return orlix_read_file("/proc/self/mountinfo", buffer, sizeof(buffer),
			       &size) == 0 &&
	       orlix_contains(buffer, size, " /sys/fs/cgroup ") &&
	       orlix_contains(buffer, size, " - cgroup2 ");
}

static bool cgroup_procs_accepts_self(void)
{
	int fd = open("/sys/fs/cgroup/cgroup.procs", O_WRONLY | O_CLOEXEC);
	ssize_t written;

	if (fd < 0)
		return false;

	written = write(fd, "0\n", 2);
	close(fd);
	return written == 2;
}

int main(void)
{
	orlix_write_all("ORLIX-CGROUP-V2-PROBE\n");
	orlix_test_plan(5);

	orlix_test_result(mountinfo_has_cgroup2_root(),
			  "mountinfo exposes cgroup2 at /sys/fs/cgroup");
	orlix_test_result(file_is_readable("/sys/fs/cgroup/cgroup.controllers"),
			  "cgroup v2 controllers file is readable");
	orlix_test_result(file_is_readable("/sys/fs/cgroup/cgroup.procs"),
			  "cgroup v2 procs file is readable");
	orlix_test_result(proc_self_cgroup_has_v2_root(),
			  "proc self cgroup reports unified root");
	orlix_test_result(cgroup_procs_accepts_self(),
			  "cgroup v2 procs accepts current task");

	orlix_test_exit();
}
