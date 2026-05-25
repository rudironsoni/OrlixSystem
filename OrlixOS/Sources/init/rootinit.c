// SPDX-License-Identifier: GPL-2.0

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static void write_literal(int fd, const char *message)
{
	size_t length = 0;

	while (message[length] != '\0')
		length++;

	while (length > 0) {
		ssize_t written = write(fd, message, length);

		if (written <= 0)
			return;

		message += written;
		length -= (size_t)written;
	}
}

static int ensure_dir(const char *path, mode_t mode)
{
	if (mkdir(path, mode) == 0 || errno == EEXIST)
		return 0;

	return -1;
}

static int mount_if_needed(const char *source, const char *target,
			   const char *fstype, unsigned long flags,
			   const void *data)
{
	if (mount(source, target, fstype, flags, data) == 0 || errno == EBUSY)
		return 0;

	return -1;
}

static int wait_for_path(const char *path)
{
	for (int attempt = 0; attempt < 250; attempt++) {
		struct timespec delay = {
			.tv_sec = 0,
			.tv_nsec = 20000000,
		};
		int fd = open(path, O_RDONLY);

		if (fd >= 0) {
			close(fd);
			return 0;
		}

		nanosleep(&delay, NULL);
	}

	return -1;
}

static int mount_overlay_root(void)
{
	if (ensure_dir("/dev", 0755) != 0 ||
	    ensure_dir("/proc", 0755) != 0 ||
	    ensure_dir("/sys", 0755) != 0 ||
	    ensure_dir("/lower", 0755) != 0 ||
	    ensure_dir("/state", 0755) != 0 ||
	    ensure_dir("/newroot", 0755) != 0)
		return -1;

	if (mount_if_needed("devtmpfs", "/dev", "devtmpfs", 0, NULL) != 0 ||
	    mount_if_needed("proc", "/proc", "proc", 0, NULL) != 0 ||
	    mount_if_needed("sysfs", "/sys", "sysfs", 0, NULL) != 0)
		return -1;

	if (wait_for_path("/dev/vda") != 0 || wait_for_path("/dev/vdb") != 0)
		return -1;

	if (mount_if_needed("/dev/vda", "/lower", "ext4", MS_RDONLY, NULL) != 0 ||
	    mount_if_needed("/dev/vdb", "/state", "ext4", 0, NULL) != 0)
		return -1;

	if (ensure_dir("/state/upper", 0755) != 0 ||
	    ensure_dir("/state/work", 0755) != 0)
		return -1;

	return mount_if_needed("overlay", "/newroot", "overlay", 0,
			       "lowerdir=/lower,upperdir=/state/upper,workdir=/state/work");
}

static int mount_new_root_api_filesystems(void)
{
	if (ensure_dir("/newroot/dev", 0755) != 0 ||
	    ensure_dir("/newroot/proc", 0755) != 0 ||
	    ensure_dir("/newroot/sys", 0755) != 0)
		return -1;

	return mount_if_needed("devtmpfs", "/newroot/dev", "devtmpfs", 0, NULL) == 0 &&
		       mount_if_needed("proc", "/newroot/proc", "proc", 0, NULL) == 0 &&
		       mount_if_needed("sysfs", "/newroot/sys", "sysfs", 0, NULL) == 0 ?
		0 : -1;
}

static int switch_to_new_root(void)
{
	if (chdir("/newroot") != 0)
		return -1;
	if (mount(".", "/", NULL, MS_MOVE, NULL) != 0)
		return -1;
	if (chroot(".") != 0)
		return -1;

	return chdir("/");
}

int main(void)
{
	char *const argv[] = { "/sbin/init", NULL };
	char *const envp[] = {
		"HOME=/root",
		"PATH=/bin:/usr/bin:/sbin:/usr/sbin",
		"TERM=xterm-256color",
		NULL,
	};

	write_literal(STDERR_FILENO, "ORLIX-ROOTINIT-START\n");

	if (mount_overlay_root() != 0) {
		write_literal(STDERR_FILENO,
			      "orlix-rootinit: overlay root assembly failed\n");
		return 127;
	}
	if (mount_new_root_api_filesystems() != 0) {
		write_literal(STDERR_FILENO,
			      "orlix-rootinit: api filesystem setup failed\n");
		return 127;
	}
	if (switch_to_new_root() != 0) {
		write_literal(STDERR_FILENO,
			      "orlix-rootinit: switch_root failed\n");
		return 127;
	}

	write_literal(STDERR_FILENO, "ORLIX-ROOT-OVERLAY-READY\n");
	execve(argv[0], argv, envp);
	write_literal(STDERR_FILENO, "orlix-rootinit: exec /sbin/init failed\n");

	for (;;)
		pause();
}
