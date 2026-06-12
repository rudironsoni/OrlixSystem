// SPDX-License-Identifier: GPL-2.0

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

#include "orlix_kselftest_user.h"

static bool set_nonblock(int fd)
{
	int flags = fcntl(fd, F_GETFL);

	return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

static bool empty_pipe_read_returns_eagain(int read_fd)
{
	char byte;

	errno = 0;
	return read(read_fd, &byte, sizeof(byte)) == -1 && errno == EAGAIN;
}

static bool empty_pipe_select_read_times_out(int read_fd)
{
	fd_set readfds;
	struct timeval timeout = {
		.tv_sec = 0,
		.tv_usec = 0,
	};

	FD_ZERO(&readfds);
	FD_SET(read_fd, &readfds);
	return select(read_fd + 1, &readfds, NULL, NULL, &timeout) == 0 &&
	       !FD_ISSET(read_fd, &readfds);
}

static bool pipe_select_reports_write_ready(int write_fd)
{
	fd_set writefds;
	struct timeval timeout = {
		.tv_sec = 0,
		.tv_usec = 0,
	};

	FD_ZERO(&writefds);
	FD_SET(write_fd, &writefds);
	return select(write_fd + 1, NULL, &writefds, NULL, &timeout) == 1 &&
	       FD_ISSET(write_fd, &writefds);
}

static bool pipe_select_reports_read_ready(int read_fd)
{
	fd_set readfds;
	struct timeval timeout = {
		.tv_sec = 0,
		.tv_usec = 0,
	};

	FD_ZERO(&readfds);
	FD_SET(read_fd, &readfds);
	return select(read_fd + 1, &readfds, NULL, NULL, &timeout) == 1 &&
	       FD_ISSET(read_fd, &readfds);
}

static bool pipe_read_returns_payload(int read_fd)
{
	static const char expected[] = "orlix-pipe-select\n";
	char buffer[sizeof(expected)];
	ssize_t nread;

	memset(buffer, 0, sizeof(buffer));
	nread = read(read_fd, buffer, sizeof(expected) - 1);
	return nread == (ssize_t)(sizeof(expected) - 1) &&
	       memcmp(buffer, expected, sizeof(expected) - 1) == 0;
}

static bool pipe_read_returns_eof(int read_fd)
{
	char byte;

	return read(read_fd, &byte, sizeof(byte)) == 0;
}

int main(void)
{
	static const char message[] = "ORLIX-PIPE-SELECT-PROBE\n";
	static const char payload[] = "orlix-pipe-select\n";
	int fds[2] = { -1, -1 };
	bool pipe_created;
	bool wrote_payload = false;

	(void)write(STDOUT_FILENO, message, sizeof(message) - 1);
	orlix_test_plan(8);

	pipe_created = pipe(fds) == 0 && set_nonblock(fds[0]);
	orlix_test_result(pipe_created,
			  "pipe creates nonblocking read descriptor for select");
	orlix_test_result(pipe_created &&
			  empty_pipe_read_returns_eagain(fds[0]),
			  "empty nonblocking pipe read returns EAGAIN before select");
	orlix_test_result(pipe_created &&
			  empty_pipe_select_read_times_out(fds[0]),
			  "empty pipe read select times out");
	orlix_test_result(pipe_created &&
			  pipe_select_reports_write_ready(fds[1]),
			  "pipe write end selects writable");

	if (pipe_created)
		wrote_payload = write(fds[1], payload, sizeof(payload) - 1) ==
				(ssize_t)(sizeof(payload) - 1);
	orlix_test_result(wrote_payload &&
			  pipe_select_reports_read_ready(fds[0]),
			  "pipe read end selects readable after write");
	orlix_test_result(wrote_payload && pipe_read_returns_payload(fds[0]),
			  "pipe read returns selected payload");

	if (fds[1] >= 0) {
		close(fds[1]);
		fds[1] = -1;
	}
	orlix_test_result(pipe_created && pipe_select_reports_read_ready(fds[0]),
			  "pipe read end selects readable after writer closes");
	orlix_test_result(pipe_created && pipe_read_returns_eof(fds[0]),
			  "pipe read returns EOF after selected writer close");

	if (fds[0] >= 0)
		close(fds[0]);
	if (fds[1] >= 0)
		close(fds[1]);

	orlix_test_exit();
}
