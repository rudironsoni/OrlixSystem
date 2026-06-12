// SPDX-License-Identifier: GPL-2.0

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <unistd.h>

#include "orlix_kselftest_user.h"

static bool mountinfo_contains(const char *mountpoint,
			       const char *filesystem)
{
	char buffer[8192];
	char mount_fragment[64];
	char fs_fragment[64];
	size_t mount_pos = 0;
	size_t fs_pos = 0;
	size_t size;

	if (orlix_read_file("/proc/self/mountinfo", buffer, sizeof(buffer),
			    &size) != 0)
		return false;

	mount_pos = orlix_append_cstr(mount_fragment, mount_pos,
				      sizeof(mount_fragment), " ");
	mount_pos = orlix_append_cstr(mount_fragment, mount_pos,
				      sizeof(mount_fragment), mountpoint);
	mount_pos = orlix_append_cstr(mount_fragment, mount_pos,
				      sizeof(mount_fragment), " ");
	mount_fragment[mount_pos] = '\0';

	fs_pos = orlix_append_cstr(fs_fragment, fs_pos, sizeof(fs_fragment),
				   " - ");
	fs_pos = orlix_append_cstr(fs_fragment, fs_pos, sizeof(fs_fragment),
				   filesystem);
	fs_pos = orlix_append_cstr(fs_fragment, fs_pos, sizeof(fs_fragment),
				   " ");
	fs_fragment[fs_pos] = '\0';

	return orlix_contains(buffer, size, mount_fragment) &&
	       orlix_contains(buffer, size, fs_fragment);
}

static bool path_is_directory(const char *path)
{
	struct stat st;

	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool path_is_character_device(const char *path)
{
	struct stat st;

	return stat(path, &st) == 0 && S_ISCHR(st.st_mode);
}

static bool file_contains(const char *path, const char *needle)
{
	char buffer[4096];
	size_t size;

	if (orlix_read_file(path, buffer, sizeof(buffer), &size) != 0)
		return false;
	return orlix_contains(buffer, size, needle);
}

static bool proc_self_shape_is_readable(void)
{
	return file_contains("/proc/self/status", "Name:") &&
	       path_is_directory("/proc/self/fd") &&
	       file_contains("/proc/mounts", "proc /proc proc");
}

static bool dev_core_nodes_are_character_devices(void)
{
	return path_is_character_device("/dev/null") &&
	       path_is_character_device("/dev/zero") &&
	       path_is_character_device("/dev/random") &&
	       path_is_character_device("/dev/urandom");
}

static bool devpts_allocates_ptmx(void)
{
	static const char *const ptmx_paths[] = {
		"/dev/ptmx",
		"/dev/pts/ptmx",
		NULL,
	};
	int fd;
	struct stat st;
	const char *const *path;

	for (path = ptmx_paths; *path; path++) {
		fd = open(*path, O_RDWR | O_NOCTTY);
		if (fd < 0)
			continue;

		if (fstat(fd, &st) == 0 && S_ISCHR(st.st_mode)) {
			close(fd);
			return true;
		}
		close(fd);
	}
	return false;
}

int main(void)
{
	orlix_write_all("ORLIX-PSEUDO-FS-PROBE\n");
	orlix_test_plan(10);

	orlix_test_result(mountinfo_contains("/proc", "proc"),
			  "mountinfo exposes procfs at /proc");
	orlix_test_result(mountinfo_contains("/sys", "sysfs"),
			  "mountinfo exposes sysfs at /sys");
	orlix_test_result(mountinfo_contains("/dev", "devtmpfs"),
			  "mountinfo exposes devtmpfs at /dev");
	orlix_test_result(mountinfo_contains("/dev/pts", "devpts"),
			  "mountinfo exposes devpts at /dev/pts");
	orlix_test_result(mountinfo_contains("/tmp", "tmpfs"),
			  "mountinfo exposes tmpfs at /tmp");
	orlix_test_result(proc_self_shape_is_readable(),
			  "proc self status fd and mounts are readable");
	orlix_test_result(dev_core_nodes_are_character_devices(),
			  "core dev nodes are Linux character devices");
	orlix_test_result(path_is_directory("/dev/pts"),
			  "devpts mountpoint is a directory");
	orlix_test_result(devpts_allocates_ptmx(),
			  "devpts allocates a PTY master through ptmx");
	orlix_test_result(path_is_directory("/sys/bus/virtio/devices"),
			  "sysfs exposes virtio device directory");

	orlix_test_exit();
}
