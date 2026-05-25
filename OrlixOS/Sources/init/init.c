// SPDX-License-Identifier: GPL-2.0

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
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

static int open_controlling_tty(void)
{
	static const char *const tty_candidates[] = {
		"/dev/hvc0",
		"/dev/ttyS0",
		NULL,
	};
	int fd = -1;

	if (setsid() < 0 && errno != EPERM)
		write_literal(STDERR_FILENO, "orlix-init: setsid failed\n");

	for (const char *const *path = tty_candidates; *path != NULL; path++) {
		fd = open(*path, O_RDWR);
		if (fd >= 0)
			break;
	}

	if (fd < 0) {
		fd = open("/dev/console", O_RDWR);
		if (fd < 0)
			return -1;
	}

	if (ioctl(fd, TIOCSCTTY, 0) < 0 && errno != EPERM)
		write_literal(fd, "orlix-init: TIOCSCTTY failed\n");

	return fd;
}

static int ensure_directory(const char *path, mode_t mode)
{
	if (mkdir(path, mode) < 0 && errno != EEXIST)
		return -1;

	return 0;
}

static int mount_filesystem(const char *source, const char *target,
			    const char *type, unsigned long flags,
			    const char *data)
{
	if (mount(source, target, type, flags, data) < 0 && errno != EBUSY)
		return -1;

	return 0;
}

static void mount_runtime_filesystems(void)
{
	if (ensure_directory("/proc", 0555) < 0 ||
	    mount_filesystem("proc", "/proc", "proc", 0, NULL) < 0)
		write_literal(STDERR_FILENO, "orlix-init: mount /proc failed\n");

	if (ensure_directory("/sys", 0555) < 0 ||
	    mount_filesystem("sysfs", "/sys", "sysfs", 0, NULL) < 0)
		write_literal(STDERR_FILENO, "orlix-init: mount /sys failed\n");

	if (ensure_directory("/run", 0755) < 0 ||
	    mount_filesystem("tmpfs", "/run", "tmpfs",
			     MS_NOSUID | MS_NODEV, "mode=0755") < 0)
		write_literal(STDERR_FILENO, "orlix-init: mount /run failed\n");

	if (ensure_directory("/tmp", 01777) < 0 ||
	    mount_filesystem("tmpfs", "/tmp", "tmpfs",
			     MS_NOSUID | MS_NODEV, "mode=1777") < 0)
		write_literal(STDERR_FILENO, "orlix-init: mount /tmp failed\n");
}

static void install_stdio(int fd)
{
	for (int target = STDIN_FILENO; target <= STDERR_FILENO; target++) {
		if (fd != target)
			dup2(fd, target);
	}

	if (fd > STDERR_FILENO)
		close(fd);
}

int main(void)
{
	char *const argv[] = { "/bin/sh", "-i", NULL };
	char *const envp[] = {
		"HOME=/root",
		"PATH=/bin:/usr/bin:/sbin:/usr/sbin",
		"TERM=xterm-256color",
		NULL,
	};
	int tty = open_controlling_tty();

	if (tty < 0) {
		write_literal(STDERR_FILENO,
			      "orlix-init: unable to open a Linux console\n");
		return 127;
	}

	install_stdio(tty);
	mount_runtime_filesystems();
	execve(argv[0], argv, envp);
	write_literal(STDERR_FILENO, "orlix-init: exec /bin/sh failed\n");

	for (;;)
		pause();
}
