// SPDX-License-Identifier: GPL-2.0

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

static int write_all(int fd, const void *bytes, size_t length)
{
	const char *cursor = bytes;

	while (length > 0) {
		ssize_t written = write(fd, cursor, length);

		if (written < 0 && errno == EINTR)
			continue;
		if (written <= 0)
			return -1;

		cursor += written;
		length -= (size_t)written;
	}

	return 0;
}

static void write_literal(int fd, const char *message)
{
	size_t length = 0;

	while (message[length] != '\0')
		length++;

	(void)write_all(fd, message, length);
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

static void mount_runtime_filesystems(void)
{
	if (ensure_dir("/dev", 0755) == 0 &&
	    mount_if_needed("devtmpfs", "/dev", "devtmpfs", 0, NULL) != 0)
		write_literal(STDERR_FILENO, "orlix-init: mount /dev failed\n");

	if (ensure_dir("/dev/pts", 0755) == 0 &&
	    mount_if_needed("devpts", "/dev/pts", "devpts", 0,
			    "gid=5,mode=620,ptmxmode=666") != 0)
		write_literal(STDERR_FILENO, "orlix-init: mount /dev/pts failed\n");

	if (ensure_dir("/run", 0755) == 0 &&
	    mount_if_needed("tmpfs", "/run", "tmpfs", MS_NOSUID | MS_NODEV,
			    "mode=0755") != 0)
		write_literal(STDERR_FILENO, "orlix-init: mount /run failed\n");

	if (ensure_dir("/tmp", 01777) == 0 &&
	    mount_if_needed("tmpfs", "/tmp", "tmpfs", MS_NOSUID | MS_NODEV,
			    "mode=1777") != 0)
		write_literal(STDERR_FILENO, "orlix-init: mount /tmp failed\n");

	if (ensure_dir("/sys/fs", 0755) == 0 &&
	    ensure_dir("/sys/fs/selinux", 0755) == 0 &&
	    mount_if_needed("selinuxfs", "/sys/fs/selinux", "selinuxfs",
			    MS_NOSUID | MS_NOEXEC, NULL) != 0)
		write_literal(STDERR_FILENO, "orlix-init: mount /sys/fs/selinux failed\n");
}

static void make_transport_raw(int fd)
{
	struct termios termios;

	if (tcgetattr(fd, &termios) != 0) {
		write_literal(STDERR_FILENO,
			      "orlix-init: tcgetattr transport failed\n");
		return;
	}

	termios.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	termios.c_oflag &= ~(OPOST);
	termios.c_cflag |= CS8;
	termios.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	termios.c_cc[VMIN] = 1;
	termios.c_cc[VTIME] = 0;

	if (tcsetattr(fd, TCSAFLUSH, &termios) != 0)
		write_literal(STDERR_FILENO,
			      "orlix-init: tcsetattr transport failed\n");
}

static int open_pty_master(void)
{
	int fd = open("/dev/ptmx", O_RDWR | O_NOCTTY);

	if (fd >= 0)
		return fd;

	return open("/dev/pts/ptmx", O_RDWR | O_NOCTTY);
}

static int open_pty_slave(int master)
{
	char path[64];
	unsigned int pty_number = 0;
	int unlock = 0;
	int length;

	if (ioctl(master, TIOCSPTLCK, &unlock) != 0) {
		write_literal(STDERR_FILENO, "orlix-init: unlock PTY failed\n");
		return -1;
	}

	if (ioctl(master, TIOCGPTN, &pty_number) != 0) {
		write_literal(STDERR_FILENO, "orlix-init: get PTY number failed\n");
		return -1;
	}

	length = snprintf(path, sizeof(path), "/dev/pts/%u", pty_number);
	if (length < 0 || (size_t)length >= sizeof(path)) {
		write_literal(STDERR_FILENO, "orlix-init: PTY path overflow\n");
		return -1;
	}

	return open(path, O_RDWR | O_NOCTTY);
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

static int copy_available(int input_fd, int output_fd)
{
	unsigned char buffer[4096];
	ssize_t bytes;

	do {
		bytes = read(input_fd, buffer, sizeof(buffer));
	} while (bytes < 0 && errno == EINTR);

	if (bytes <= 0)
		return -1;

	return write_all(output_fd, buffer, (size_t)bytes);
}

static int shell_exit_status(int status)
{
	if (WIFEXITED(status))
		return WEXITSTATUS(status);
	if (WIFSIGNALED(status))
		return 128 + WTERMSIG(status);

	return 1;
}

static int reap_shell_if_exited(pid_t shell, int *exit_status)
{
	int status;
	pid_t reaped;

	do {
		reaped = waitpid(shell, &status, WNOHANG);
	} while (reaped < 0 && errno == EINTR);

	if (reaped == 0)
		return 0;
	if (reaped != shell)
		return -1;

	*exit_status = shell_exit_status(status);
	return 1;
}

static int relay_pty(int console_fd, int master, pid_t shell)
{
	struct pollfd fds[] = {
		{
			.fd = console_fd,
			.events = POLLIN,
		},
		{
			.fd = master,
			.events = POLLIN,
		},
	};

	for (;;) {
		int exit_status = 0;
		int reaped = reap_shell_if_exited(shell, &exit_status);
		int ready;

		if (reaped != 0)
			return reaped > 0 ? exit_status : 1;

		do {
			ready = poll(fds, 2, -1);
		} while (ready < 0 && errno == EINTR);

		if (ready < 0)
			return 1;

		if ((fds[0].revents & POLLIN) != 0 &&
		    copy_available(console_fd, master) != 0)
			return 1;

		if ((fds[1].revents & POLLIN) != 0 &&
		    copy_available(master, STDOUT_FILENO) != 0)
			return 1;

		if ((fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 ||
		    (fds[1].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
			return 1;
	}
}

static pid_t start_shell_on_pty(int master, int slave)
{
	pid_t child = fork();

	if (child != 0)
		return child;

	close(master);

	if (setsid() < 0)
		write_literal(STDERR_FILENO, "orlix-init: shell setsid failed\n");

	if (ioctl(slave, TIOCSCTTY, 0) < 0)
		write_literal(STDERR_FILENO,
			      "orlix-init: shell TIOCSCTTY failed\n");

	install_stdio(slave);

	char *const argv[] = { "/bin/sh", "-i", NULL };
	char *const envp[] = {
		"HOME=/root",
		"PATH=/bin:/usr/bin:/sbin:/usr/sbin",
		"TERM=xterm-256color",
		NULL,
	};

	execve(argv[0], argv, envp);
	write_literal(STDERR_FILENO, "orlix-init: exec /bin/sh failed\n");
	_exit(127);
}

static int run_pty_shell(int console_fd)
{
	int master = open_pty_master();
	int slave;
	pid_t shell;
	int status;

	if (master < 0) {
		write_literal(STDERR_FILENO, "orlix-init: open PTY master failed\n");
		return 1;
	}

	slave = open_pty_slave(master);
	if (slave < 0) {
		close(master);
		return 1;
	}

	shell = start_shell_on_pty(master, slave);
	if (shell < 0) {
		write_literal(STDERR_FILENO, "orlix-init: fork shell failed\n");
		close(slave);
		close(master);
		return 1;
	}

	close(slave);
	make_transport_raw(console_fd);
	status = relay_pty(console_fd, master, shell);
	close(master);
	return status;
}

int main(void)
{
	int tty = open_controlling_tty();

	if (tty < 0) {
		write_literal(STDERR_FILENO,
			      "orlix-init: unable to open a Linux console\n");
		return 127;
	}

	install_stdio(tty);
	mount_runtime_filesystems();
	if (run_pty_shell(STDIN_FILENO) != 0)
		write_literal(STDERR_FILENO,
			      "orlix-init: PTY shell session ended\n");

	for (;;)
		pause();
}
