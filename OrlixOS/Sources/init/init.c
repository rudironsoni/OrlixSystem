// SPDX-License-Identifier: GPL-2.0

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <sys/ioctl.h>
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
	execve(argv[0], argv, envp);
	write_literal(STDERR_FILENO, "orlix-init: exec /bin/sh failed\n");

	for (;;)
		pause();
}
