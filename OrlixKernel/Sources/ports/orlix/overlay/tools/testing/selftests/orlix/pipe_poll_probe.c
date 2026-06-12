// SPDX-License-Identifier: GPL-2.0

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <string.h>
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

static bool empty_pipe_poll_read_times_out(int read_fd)
{
	struct pollfd pfd = {
		.fd = read_fd,
		.events = POLLIN,
		.revents = 0,
	};

	return poll(&pfd, 1, 0) == 0 && pfd.revents == 0;
}

static bool pipe_poll_reports_write_ready(int write_fd)
{
	struct pollfd pfd = {
		.fd = write_fd,
		.events = POLLOUT,
		.revents = 0,
	};

	return poll(&pfd, 1, 0) == 1 && (pfd.revents & POLLOUT) != 0;
}

static bool pipe_poll_reports_read_ready(int read_fd)
{
	struct pollfd pfd = {
		.fd = read_fd,
		.events = POLLIN,
		.revents = 0,
	};

	return poll(&pfd, 1, 0) == 1 && (pfd.revents & POLLIN) != 0;
}

static bool pipe_read_returns_payload(int read_fd)
{
	static const char expected[] = "orlix-pipe-poll\n";
	char buffer[sizeof(expected)];
	ssize_t nread;

	memset(buffer, 0, sizeof(buffer));
	nread = read(read_fd, buffer, sizeof(expected) - 1);
	return nread == (ssize_t)(sizeof(expected) - 1) &&
	       memcmp(buffer, expected, sizeof(expected) - 1) == 0;
}

static bool pipe_poll_reports_hangup(int read_fd)
{
	struct pollfd pfd = {
		.fd = read_fd,
		.events = POLLIN,
		.revents = 0,
	};

	return poll(&pfd, 1, 0) == 1 && (pfd.revents & POLLHUP) != 0;
}

int main(void)
{
	static const char message[] = "ORLIX-PIPE-POLL-PROBE\n";
	static const char payload[] = "orlix-pipe-poll\n";
	int fds[2] = { -1, -1 };
	bool pipe_created;
	bool wrote_payload = false;

	(void)write(STDOUT_FILENO, message, sizeof(message) - 1);
	orlix_test_plan(7);

	pipe_created = pipe(fds) == 0 && set_nonblock(fds[0]);
	orlix_test_result(pipe_created,
			  "pipe creates nonblocking read descriptor");
	orlix_test_result(pipe_created &&
			  empty_pipe_read_returns_eagain(fds[0]),
			  "empty nonblocking pipe read returns EAGAIN");
	orlix_test_result(pipe_created &&
			  empty_pipe_poll_read_times_out(fds[0]),
			  "empty pipe read poll times out");
	orlix_test_result(pipe_created &&
			  pipe_poll_reports_write_ready(fds[1]),
			  "pipe write end polls writable");

	if (pipe_created)
		wrote_payload = write(fds[1], payload, sizeof(payload) - 1) ==
				(ssize_t)(sizeof(payload) - 1);
	orlix_test_result(wrote_payload &&
			  pipe_poll_reports_read_ready(fds[0]),
			  "pipe read end polls readable after write");
	orlix_test_result(wrote_payload && pipe_read_returns_payload(fds[0]),
			  "pipe read returns written payload");

	if (fds[1] >= 0) {
		close(fds[1]);
		fds[1] = -1;
	}
	orlix_test_result(pipe_created && pipe_poll_reports_hangup(fds[0]),
			  "pipe read end polls hangup after writer closes");

	if (fds[0] >= 0)
		close(fds[0]);
	if (fds[1] >= 0)
		close(fds[1]);

	orlix_test_exit();
}
