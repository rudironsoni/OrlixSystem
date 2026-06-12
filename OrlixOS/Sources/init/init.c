// SPDX-License-Identifier: GPL-2.0

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#define ORLIX_INIT_CMDLINE_SIZE 16384

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

static void write_unsigned_decimal(int fd, unsigned long value)
{
	char buffer[32];
	size_t offset = sizeof(buffer);

	buffer[--offset] = '\0';
	do {
		buffer[--offset] = (char)('0' + (value % 10));
		value /= 10;
	} while (value != 0);

	(void)write_all(fd, &buffer[offset], sizeof(buffer) - offset - 1);
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
		write_literal(STDERR_FILENO, "orlix-init: opening tty candidate\n");
		fd = open(*path, O_RDWR | O_NONBLOCK);
		if (fd >= 0) {
			int flags = fcntl(fd, F_GETFL, 0);

			if (flags >= 0)
				(void)fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
			break;
		}
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

static void mount_device_filesystem(void)
{
	if (ensure_dir("/dev", 0755) == 0 &&
	    mount_if_needed("devtmpfs", "/dev", "devtmpfs", 0, NULL) != 0)
		write_literal(STDERR_FILENO, "orlix-init: mount /dev failed\n");
}

static int install_fd_alias(const char *target, const char *linkpath)
{
	(void)unlink(linkpath);
	if (symlink(target, linkpath) == 0 || errno == EEXIST)
		return 0;

	return -1;
}

static void install_standard_fd_aliases(void)
{
	if (install_fd_alias("/proc/self/fd", "/dev/fd") != 0)
		write_literal(STDERR_FILENO, "orlix-init: /dev/fd alias failed\n");
	if (install_fd_alias("/proc/self/fd/0", "/dev/stdin") != 0)
		write_literal(STDERR_FILENO, "orlix-init: /dev/stdin alias failed\n");
	if (install_fd_alias("/proc/self/fd/1", "/dev/stdout") != 0)
		write_literal(STDERR_FILENO, "orlix-init: /dev/stdout alias failed\n");
	if (install_fd_alias("/proc/self/fd/2", "/dev/stderr") != 0)
		write_literal(STDERR_FILENO, "orlix-init: /dev/stderr alias failed\n");
}

static void mount_runtime_filesystems(void)
{
	if (ensure_dir("/proc", 0555) == 0 &&
	    mount_if_needed("proc", "/proc", "proc", 0, NULL) != 0)
		write_literal(STDERR_FILENO, "orlix-init: mount /proc failed\n");

	if (ensure_dir("/sys", 0555) == 0 &&
	    mount_if_needed("sysfs", "/sys", "sysfs", 0, NULL) != 0)
		write_literal(STDERR_FILENO, "orlix-init: mount /sys failed\n");

	mount_device_filesystem();

	if (ensure_dir("/dev/pts", 0755) == 0 &&
	    mount_if_needed("devpts", "/dev/pts", "devpts", 0,
			    "gid=5,mode=620,ptmxmode=666") != 0)
		write_literal(STDERR_FILENO, "orlix-init: mount /dev/pts failed\n");

	install_standard_fd_aliases();

	if (ensure_dir("/dev/shm", 01777) == 0 &&
	    mount_if_needed("tmpfs", "/dev/shm", "tmpfs", MS_NOSUID | MS_NODEV,
			    "mode=1777") != 0)
		write_literal(STDERR_FILENO, "orlix-init: mount /dev/shm failed\n");

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

static int read_cmdline_value(const char *key, char *value, size_t value_size)
{
	char *buffer;
	ssize_t bytes;
	size_t key_length = strlen(key);
	char *cursor;
	int fd;

	if (value_size == 0)
		return -1;
	value[0] = '\0';
	buffer = malloc(ORLIX_INIT_CMDLINE_SIZE);
	if (buffer == NULL)
		return -1;

	fd = open("/proc/cmdline", O_RDONLY);
	if (fd < 0) {
		free(buffer);
		return -1;
	}

	do {
		bytes = read(fd, buffer, ORLIX_INIT_CMDLINE_SIZE - 1);
	} while (bytes < 0 && errno == EINTR);
	close(fd);

	if (bytes <= 0) {
		free(buffer);
		return -1;
	}

	if (buffer[bytes - 1] == '\n')
		bytes--;
	buffer[bytes] = '\0';
	cursor = buffer;
	while (*cursor != '\0') {
		char *end = cursor;
		size_t token_length;

		while (*end != '\0' && *end != ' ')
			end++;
		token_length = (size_t)(end - cursor);

		if (token_length >= key_length &&
		    strncmp(cursor, key, key_length) == 0) {
			size_t length = token_length - key_length;

			if (length >= value_size) {
				free(buffer);
				return -1;
			}
			memcpy(value, cursor + key_length, length);
			value[length] = '\0';
			free(buffer);
			return 0;
		}

		cursor = end;
		while (*cursor == ' ')
			cursor++;
	}

	free(buffer);
	return -1;
}

static int hex_value(char value)
{
	if (value >= '0' && value <= '9')
		return value - '0';
	if (value >= 'a' && value <= 'f')
		return value - 'a' + 10;
	if (value >= 'A' && value <= 'F')
		return value - 'A' + 10;

	return -1;
}

static int percent_decode(char *value)
{
	char *read = value;
	char *write = value;

	while (*read != '\0') {
		if (*read == '%') {
			int high = hex_value(read[1]);
			int low = hex_value(read[2]);

			if (high < 0 || low < 0)
				return -1;
			*write++ = (char)((high << 4) | low);
			read += 3;
			continue;
		}
		*write++ = *read++;
	}
	*write = '\0';
	return 0;
}

static int read_cmdline_decoded(const char *key, char *value,
				size_t value_size)
{
	if (read_cmdline_value(key, value, value_size) != 0)
		return -1;

	return percent_decode(value);
}

static int read_cmdline_unsigned(const char *key, unsigned long *value)
{
	char buffer[32];
	char *end = NULL;
	unsigned long parsed;

	if (read_cmdline_value(key, buffer, sizeof(buffer)) != 0)
		return -1;

	errno = 0;
	parsed = strtoul(buffer, &end, 10);
	if (errno != 0 || end == buffer || *end != '\0')
		return -1;

	*value = parsed;
	return 0;
}

static int valid_exec_path(const char *path)
{
	if (path[0] == '\0')
		return 0;
	if (path[0] == '/')
		return 1;
	if (strchr(path, '/') != NULL)
		return 0;

	return 1;
}

static int valid_working_directory(const char *path)
{
	return path[0] == '/';
}

static int valid_environment_assignment(const char *value)
{
	const char *separator = strchr(value, '=');

	if (separator == NULL || separator == value)
		return 0;

	return 1;
}

enum {
	ORLIX_INIT_MAX_ARGS = 16,
	ORLIX_INIT_MAX_ENV = 32,
	ORLIX_INIT_VALUE_SIZE = 2048,
};

struct orlix_command_config {
	char argv_storage[ORLIX_INIT_MAX_ARGS][ORLIX_INIT_VALUE_SIZE];
	char env_storage[ORLIX_INIT_MAX_ENV][ORLIX_INIT_VALUE_SIZE];
	char cwd[ORLIX_INIT_VALUE_SIZE];
	char *argv[ORLIX_INIT_MAX_ARGS + 2];
	char *envp[ORLIX_INIT_MAX_ENV + 1];
	int argc;
	int envc;
	unsigned long uid;
	unsigned long gid;
};

static void selected_command_config(struct orlix_command_config *config)
{
	char key[32];
	char exec_path[ORLIX_INIT_VALUE_SIZE];

	memset(config, 0, sizeof(*config));
	strncpy(config->argv_storage[0], "/bin/sh", ORLIX_INIT_VALUE_SIZE);
	strncpy(config->argv_storage[1], "-i", ORLIX_INIT_VALUE_SIZE);
	config->argv[0] = config->argv_storage[0];
	config->argv[1] = config->argv_storage[1];
	config->argc = 2;

	strncpy(config->env_storage[0], "HOME=/root", ORLIX_INIT_VALUE_SIZE);
	strncpy(config->env_storage[1],
		"PATH=/bin:/usr/bin:/sbin:/usr/sbin",
		ORLIX_INIT_VALUE_SIZE);
	strncpy(config->env_storage[2], "TERM=xterm-256color",
		ORLIX_INIT_VALUE_SIZE);
	config->envp[0] = config->env_storage[0];
	config->envp[1] = config->env_storage[1];
	config->envp[2] = config->env_storage[2];
	config->envc = 3;
	strncpy(config->cwd, "/", ORLIX_INIT_VALUE_SIZE);

	if (read_cmdline_decoded("orlix.exec=", exec_path, sizeof(exec_path)) != 0 ||
	    !valid_exec_path(exec_path))
		return;

	config->argc = 0;
	for (int index = 0; index < ORLIX_INIT_MAX_ARGS; index++) {
		snprintf(key, sizeof(key), "orlix.argv%d=", index);
		if (read_cmdline_decoded(key, config->argv_storage[index],
					 ORLIX_INIT_VALUE_SIZE) != 0)
			break;
		if (index == 0 && !valid_exec_path(config->argv_storage[index]))
			break;
		config->argv[index] = config->argv_storage[index];
		config->argc++;
	}
	if (config->argc == 0) {
		strncpy(config->argv_storage[0], exec_path, ORLIX_INIT_VALUE_SIZE);
		config->argv[0] = config->argv_storage[0];
		config->argc = 1;
	}
	if (config->argc == 1 && strcmp(config->argv[0], "/bin/sh") == 0) {
		strncpy(config->argv_storage[1], "-i", ORLIX_INIT_VALUE_SIZE);
		config->argv[1] = config->argv_storage[1];
		config->argc = 2;
	}
	config->argv[config->argc] = NULL;

	config->envc = 0;
	for (int index = 0; index < ORLIX_INIT_MAX_ENV; index++) {
		snprintf(key, sizeof(key), "orlix.env%d=", index);
		if (read_cmdline_decoded(key, config->env_storage[index],
					 ORLIX_INIT_VALUE_SIZE) != 0)
			break;
		if (!valid_environment_assignment(config->env_storage[index]))
			break;
		config->envp[index] = config->env_storage[index];
		config->envc++;
	}
	if (config->envc == 0) {
		strncpy(config->env_storage[0], "HOME=/root",
			ORLIX_INIT_VALUE_SIZE);
		strncpy(config->env_storage[1],
			"PATH=/bin:/usr/bin:/sbin:/usr/sbin",
			ORLIX_INIT_VALUE_SIZE);
		strncpy(config->env_storage[2], "TERM=xterm-256color",
			ORLIX_INIT_VALUE_SIZE);
		config->envp[0] = config->env_storage[0];
		config->envp[1] = config->env_storage[1];
		config->envp[2] = config->env_storage[2];
		config->envc = 3;
	}
	config->envp[config->envc] = NULL;

	if (read_cmdline_decoded("orlix.cwd=", config->cwd,
				 sizeof(config->cwd)) != 0 ||
	    !valid_working_directory(config->cwd))
		strncpy(config->cwd, "/", sizeof(config->cwd));

	(void)read_cmdline_unsigned("orlix.uid=", &config->uid);
	(void)read_cmdline_unsigned("orlix.gid=", &config->gid);

	config->cwd[sizeof(config->cwd) - 1] = '\0';
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

static void write_shell_exit_status(int exit_status)
{
	write_literal(STDERR_FILENO, "orlix-init: shell exit status=");
	write_unsigned_decimal(STDERR_FILENO, (unsigned long)exit_status);
	write_literal(STDERR_FILENO, "\n");
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

		if (reaped > 0) {
			write_shell_exit_status(exit_status);
			return exit_status;
		}
		if (reaped < 0)
			return 1;

		do {
			ready = poll(fds, 2, -1);
		} while (ready < 0 && errno == EINTR);

		if (ready < 0)
			return 1;

		short console_revents = fds[0].revents;
		short pty_revents = fds[1].revents;

		if ((console_revents & POLLIN) != 0 &&
		    copy_available(console_fd, master) != 0) {
			write_literal(STDERR_FILENO,
				      "orlix-init: console relay failed\n");
			return 1;
		}

		if ((pty_revents & POLLIN) != 0 &&
		    copy_available(master, STDOUT_FILENO) != 0) {
			write_literal(STDERR_FILENO,
				      "orlix-init: pty relay failed\n");
			return 1;
		}

		if ((console_revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 ||
		    (pty_revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
			reaped = reap_shell_if_exited(shell, &exit_status);
			if (reaped > 0) {
				write_shell_exit_status(exit_status);
				return exit_status;
			}
			return 1;
		}
	}
}

static const char *configured_path(char *const envp[])
{
	for (char *const *entry = envp; *entry != NULL; entry++) {
		if (strncmp(*entry, "PATH=", 5) == 0)
			return *entry + 5;
	}

	return "/bin:/usr/bin";
}

static void exec_path_candidate(const char *directory, size_t directory_length,
				const char *command, char *const argv[],
				char *const envp[])
{
	char candidate[ORLIX_INIT_VALUE_SIZE];
	size_t command_length = strlen(command);

	if (directory_length == 0) {
		if (command_length >= sizeof(candidate))
			return;
		memcpy(candidate, command, command_length + 1);
		execve(candidate, argv, envp);
		return;
	}

	if (directory_length + 1 + command_length >= sizeof(candidate))
		return;
	memcpy(candidate, directory, directory_length);
	candidate[directory_length] = '/';
	memcpy(candidate + directory_length + 1, command, command_length + 1);
	execve(candidate, argv, envp);
}

static void exec_configured_command(struct orlix_command_config *config)
{
	const char *command = config->argv[0];
	const char *path;
	const char *component;

	if (strchr(command, '/') != NULL) {
		execve(command, config->argv, config->envp);
		return;
	}

	path = configured_path(config->envp);
	component = path;
	for (;;) {
		const char *separator = strchr(component, ':');
		size_t length = separator != NULL ?
			(size_t)(separator - component) : strlen(component);

		exec_path_candidate(component, length, command, config->argv,
				    config->envp);
		if (separator == NULL)
			break;
		component = separator + 1;
	}
}

static pid_t start_command_on_pty(int master, int slave)
{
	pid_t child = fork();
	struct orlix_command_config *config;

	if (child != 0)
		return child;

	close(master);

	if (setsid() < 0)
		write_literal(STDERR_FILENO, "orlix-init: shell setsid failed\n");

	if (ioctl(slave, TIOCSCTTY, 0) < 0)
		write_literal(STDERR_FILENO,
			      "orlix-init: shell TIOCSCTTY failed\n");

	install_stdio(slave);
	config = calloc(1, sizeof(*config));
	if (config == NULL) {
		write_literal(STDERR_FILENO,
			      "orlix-init: command config allocation failed\n");
		_exit(127);
	}
	selected_command_config(config);
	if (chdir(config->cwd) != 0)
		write_literal(STDERR_FILENO, "orlix-init: chdir failed\n");
	if (config->gid != 0 && setgid((gid_t)config->gid) != 0)
		write_literal(STDERR_FILENO, "orlix-init: setgid failed\n");
	if (config->uid != 0 && setuid((uid_t)config->uid) != 0)
		write_literal(STDERR_FILENO, "orlix-init: setuid failed\n");

	exec_configured_command(config);
	write_literal(STDERR_FILENO, "orlix-init: exec command failed\n");
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

	shell = start_command_on_pty(master, slave);
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
	int tty;

	write_literal(STDERR_FILENO, "orlix-init: main entered\n");
	mount_device_filesystem();
	write_literal(STDERR_FILENO, "orlix-init: device filesystem mounted\n");
	tty = open_controlling_tty();
	if (tty < 0) {
		write_literal(STDERR_FILENO,
			      "orlix-init: unable to open a Linux console\n");
		return 127;
	}

	install_stdio(tty);
	write_literal(STDERR_FILENO, "orlix-init: stdio installed\n");
	mount_runtime_filesystems();
	write_literal(STDERR_FILENO, "orlix-init: runtime filesystems mounted\n");
	if (run_pty_shell(STDIN_FILENO) != 0)
		write_literal(STDERR_FILENO,
			      "orlix-init: PTY shell session ended\n");

	for (;;)
		pause();
}
