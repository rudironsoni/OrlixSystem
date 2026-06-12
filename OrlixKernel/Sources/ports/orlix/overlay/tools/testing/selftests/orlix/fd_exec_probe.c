// SPDX-License-Identifier: GPL-2.0

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "orlix_kselftest_user.h"

static int parse_uint(const char *s)
{
	int value = 0;

	if (!s || !s[0])
		return -1;
	while (*s) {
		if (*s < '0' || *s > '9')
			return -1;
		value = value * 10 + (*s - '0');
		s++;
	}
	return value;
}

static void format_uint(char *buffer, size_t capacity, unsigned int value)
{
	size_t pos = 0;

	pos = orlix_append_uint(buffer, pos, capacity, value);
	buffer[pos] = '\0';
}

static int exec_child_main(const char *read_fd_text,
			   const char *cloexec_fd_text)
{
	static const char expected[] = "orlix-fd-inherit\n";
	char buffer[sizeof(expected)];
	int read_fd = parse_uint(read_fd_text);
	int cloexec_fd = parse_uint(cloexec_fd_text);
	ssize_t nread;

	orlix_write_all("ORLIX-FD-EXEC-CHILD\n");
	if (read_fd < 0 || cloexec_fd < 0)
		return 80;

	memset(buffer, 0, sizeof(buffer));
	nread = read(read_fd, buffer, sizeof(expected) - 1);
	if (nread != (ssize_t)(sizeof(expected) - 1) ||
	    memcmp(buffer, expected, sizeof(expected) - 1) != 0)
		return 81;
	orlix_write_all("ORLIX-FD-INHERITED-READ-OK\n");

	errno = 0;
	if (fcntl(cloexec_fd, F_GETFD) != -1 || errno != EBADF)
		return 82;
	orlix_write_all("ORLIX-FD-CLOEXEC-EBADF-OK\n");

	return 77;
}

static int run_exec_child(const char *self_path, int read_fd, int cloexec_fd)
{
	char read_fd_arg[16];
	char cloexec_fd_arg[16];
	pid_t child;
	int status = 0;

	format_uint(read_fd_arg, sizeof(read_fd_arg), (unsigned int)read_fd);
	format_uint(cloexec_fd_arg, sizeof(cloexec_fd_arg),
		    (unsigned int)cloexec_fd);

	child = fork();
	if (child == 0) {
		char *const exec_argv[] = {
			(char *)self_path,
			"--exec-child",
			read_fd_arg,
			cloexec_fd_arg,
			NULL
		};

		execv(self_path, exec_argv);
		_exit(127);
	}
	if (child < 0)
		return -1;
	if (waitpid(child, &status, 0) != child)
		return -1;
	if (!WIFEXITED(status))
		return -1;
	return WEXITSTATUS(status);
}

int main(int argc, char **argv)
{
	static const char message[] = "ORLIX-FD-EXEC-PROBE\n";
	static const char payload[] = "orlix-fd-inherit\n";
	int inherited_pipe[2] = { -1, -1 };
	int cloexec_pipe[2] = { -1, -1 };
	bool pipe_created = false;
	bool inherited_without_cloexec = false;
	bool marked_cloexec = false;
	int child_status = -1;

	if (argc == 4 && strcmp(argv[1], "--exec-child") == 0)
		return exec_child_main(argv[2], argv[3]);

	(void)write(STDOUT_FILENO, message, sizeof(message) - 1);
	orlix_test_plan(5);

	pipe_created = pipe(inherited_pipe) == 0 && pipe(cloexec_pipe) == 0;
	if (pipe_created) {
		int flags = fcntl(inherited_pipe[0], F_GETFD);

		inherited_without_cloexec = flags >= 0 &&
					    (flags & FD_CLOEXEC) == 0;
		flags = fcntl(cloexec_pipe[0], F_GETFD);
		if (flags >= 0 &&
		    fcntl(cloexec_pipe[0], F_SETFD,
			  flags | FD_CLOEXEC) == 0) {
			flags = fcntl(cloexec_pipe[0], F_GETFD);
			marked_cloexec = flags >= 0 &&
					 (flags & FD_CLOEXEC) != 0;
		}
	}

	orlix_test_result(pipe_created, "pipe creates descriptor pairs");
	orlix_test_result(inherited_without_cloexec,
			  "fcntl reports descriptor without close-on-exec");
	orlix_test_result(marked_cloexec,
			  "fcntl marks selected descriptor close-on-exec");

	if (pipe_created && marked_cloexec &&
	    write(inherited_pipe[1], payload, sizeof(payload) - 1) ==
		    (ssize_t)(sizeof(payload) - 1)) {
		close(inherited_pipe[1]);
		close(cloexec_pipe[1]);
		child_status = run_exec_child(argv[0], inherited_pipe[0],
					      cloexec_pipe[0]);
	}

	orlix_test_result(child_status == 77 || child_status == 82,
			  "exec preserves non-close-on-exec descriptor");
	orlix_test_result(child_status == 77,
			  "exec closes close-on-exec descriptor");

	if (inherited_pipe[0] >= 0)
		close(inherited_pipe[0]);
	if (inherited_pipe[1] >= 0)
		close(inherited_pipe[1]);
	if (cloexec_pipe[0] >= 0)
		close(cloexec_pipe[0]);
	if (cloexec_pipe[1] >= 0)
		close(cloexec_pipe[1]);

	orlix_test_exit();
}
