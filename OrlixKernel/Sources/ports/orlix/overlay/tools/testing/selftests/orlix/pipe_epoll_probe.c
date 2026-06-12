// SPDX-License-Identifier: GPL-2.0

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <sys/epoll.h>
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

static bool epoll_add(int epoll_fd, int fd, uint32_t events)
{
	struct epoll_event event;

	memset(&event, 0, sizeof(event));
	event.events = events;
	event.data.fd = fd;
	return epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) == 0;
}

static bool epoll_mod(int epoll_fd, int fd, uint32_t events)
{
	struct epoll_event event;

	memset(&event, 0, sizeof(event));
	event.events = events;
	event.data.fd = fd;
	return epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &event) == 0;
}

static bool empty_pipe_epoll_times_out(int epoll_fd)
{
	struct epoll_event event;

	memset(&event, 0, sizeof(event));
	return epoll_wait(epoll_fd, &event, 1, 0) == 0;
}

static bool pipe_epoll_reports_write_ready(int epoll_fd, int write_fd)
{
	struct epoll_event event;
	bool ready;

	memset(&event, 0, sizeof(event));
	if (!epoll_add(epoll_fd, write_fd, EPOLLOUT))
		return false;
	ready = epoll_wait(epoll_fd, &event, 1, 0) == 1 &&
		event.data.fd == write_fd &&
		(event.events & EPOLLOUT) != 0;
	if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, write_fd, NULL) != 0)
		return false;
	return ready;
}

static bool pipe_epoll_reports_read_ready(int epoll_fd, int read_fd)
{
	struct epoll_event event;

	memset(&event, 0, sizeof(event));
	return epoll_wait(epoll_fd, &event, 1, 0) == 1 &&
	       event.data.fd == read_fd &&
	       (event.events & EPOLLIN) != 0;
}

static bool pipe_read_returns_payload(int read_fd)
{
	static const char expected[] = "orlix-pipe-epoll\n";
	char buffer[sizeof(expected)];
	ssize_t nread;

	memset(buffer, 0, sizeof(buffer));
	nread = read(read_fd, buffer, sizeof(expected) - 1);
	return nread == (ssize_t)(sizeof(expected) - 1) &&
	       memcmp(buffer, expected, sizeof(expected) - 1) == 0;
}

static bool pipe_epoll_reports_hangup(int epoll_fd, int read_fd)
{
	struct epoll_event event;

	memset(&event, 0, sizeof(event));
	return epoll_wait(epoll_fd, &event, 1, 0) == 1 &&
	       event.data.fd == read_fd &&
	       (event.events & EPOLLHUP) != 0;
}

int main(void)
{
	static const char message[] = "ORLIX-PIPE-EPOLL-PROBE\n";
	static const char payload[] = "orlix-pipe-epoll\n";
	int fds[2] = { -1, -1 };
	int epoll_fd = -1;
	bool pipe_created;
	bool epoll_created;
	bool read_registered = false;
	bool wrote_payload = false;

	(void)write(STDOUT_FILENO, message, sizeof(message) - 1);
	orlix_test_plan(9);

	pipe_created = pipe(fds) == 0 && set_nonblock(fds[0]);
	epoll_fd = epoll_create1(EPOLL_CLOEXEC);
	epoll_created = epoll_fd >= 0;

	orlix_test_result(pipe_created,
			  "pipe creates nonblocking read descriptor for epoll");
	orlix_test_result(epoll_created,
			  "epoll_create1 returns epoll descriptor");
	orlix_test_result(pipe_created &&
			  empty_pipe_read_returns_eagain(fds[0]),
			  "empty nonblocking pipe read returns EAGAIN before epoll");

	if (pipe_created && epoll_created)
		read_registered = epoll_add(epoll_fd, fds[0], EPOLLIN);
	orlix_test_result(read_registered, "epoll_ctl adds pipe read end");
	orlix_test_result(read_registered &&
			  empty_pipe_epoll_times_out(epoll_fd),
			  "empty pipe read epoll times out");
	orlix_test_result(pipe_created && epoll_created &&
			  pipe_epoll_reports_write_ready(epoll_fd, fds[1]),
			  "pipe write end epolls writable");

	if (pipe_created)
		wrote_payload = write(fds[1], payload, sizeof(payload) - 1) ==
				(ssize_t)(sizeof(payload) - 1);
	orlix_test_result(wrote_payload && read_registered &&
			  pipe_epoll_reports_read_ready(epoll_fd, fds[0]),
			  "pipe read end epolls readable after write");
	orlix_test_result(wrote_payload && pipe_read_returns_payload(fds[0]),
			  "pipe read returns epoll payload");

	if (read_registered)
		read_registered = epoll_mod(epoll_fd, fds[0],
					    EPOLLIN | EPOLLHUP);
	if (fds[1] >= 0) {
		close(fds[1]);
		fds[1] = -1;
	}
	orlix_test_result(read_registered &&
			  pipe_epoll_reports_hangup(epoll_fd, fds[0]),
			  "pipe read end epolls hangup after writer closes");

	if (fds[0] >= 0)
		close(fds[0]);
	if (fds[1] >= 0)
		close(fds[1]);
	if (epoll_fd >= 0)
		close(epoll_fd);

	orlix_test_exit();
}
