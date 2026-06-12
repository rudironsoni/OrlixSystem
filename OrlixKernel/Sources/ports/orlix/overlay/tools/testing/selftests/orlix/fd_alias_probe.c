// SPDX-License-Identifier: GPL-2.0

#include <fcntl.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <unistd.h>

#include "orlix_kselftest_user.h"

static bool path_is_directory(const char *path)
{
	struct stat st;

	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool fd_alias_opens_referenced_file(int fd, const char *alias_path)
{
	static const char payload[] = "orlix-fd-alias\n";
	char buffer[sizeof(payload)];
	struct stat expected;
	struct stat actual;
	int alias_fd;
	ssize_t nread;

	if (write(fd, payload, sizeof(payload) - 1) !=
	    (ssize_t)(sizeof(payload) - 1))
		return false;

	if (lseek(fd, 0, SEEK_SET) != 0)
		return false;

	alias_fd = open(alias_path, O_RDONLY);
	if (alias_fd < 0)
		return false;

	if (fstat(fd, &expected) != 0 || fstat(alias_fd, &actual) != 0) {
		close(alias_fd);
		return false;
	}

	nread = read(alias_fd, buffer, sizeof(payload) - 1);
	if (close(alias_fd) != 0)
		return false;

	return expected.st_dev == actual.st_dev &&
	       expected.st_ino == actual.st_ino &&
	       expected.st_mode == actual.st_mode &&
	       nread == (ssize_t)(sizeof(payload) - 1) &&
	       orlix_memcmp(buffer, payload, sizeof(payload) - 1) == 0;
}

static bool stdio_alias_matches(const char *path, int fd)
{
	struct stat expected;
	struct stat actual;

	return fstat(fd, &expected) == 0 &&
	       stat(path, &actual) == 0 &&
	       expected.st_dev == actual.st_dev &&
	       expected.st_ino == actual.st_ino &&
	       expected.st_mode == actual.st_mode;
}

int main(void)
{
	char fd_path[32] = "/dev/fd/";
	size_t pos;
	int tmp_fd;

	orlix_write_all("ORLIX-FD-ALIAS-PROBE\n");
	orlix_test_plan(5);

	tmp_fd = open("/tmp/orlix-fd-alias-probe",
		      O_CREAT | O_TRUNC | O_RDWR, 0600);
	if (tmp_fd >= 0) {
		pos = orlix_strlen(fd_path);
		pos = orlix_append_uint(fd_path, pos, sizeof(fd_path),
					(unsigned int)tmp_fd);
		fd_path[pos] = '\0';
	}

	orlix_test_result(path_is_directory("/dev/fd"),
			  "/dev/fd is a directory");
	orlix_test_result(tmp_fd >= 0 &&
			  fd_alias_opens_referenced_file(tmp_fd, fd_path),
			  "/dev/fd opens the referenced descriptor path");
	orlix_test_result(stdio_alias_matches("/dev/stdin", STDIN_FILENO),
			  "/dev/stdin aliases fd 0");
	orlix_test_result(stdio_alias_matches("/dev/stdout", STDOUT_FILENO),
			  "/dev/stdout aliases fd 1");
	orlix_test_result(stdio_alias_matches("/dev/stderr", STDERR_FILENO),
			  "/dev/stderr aliases fd 2");

	if (tmp_fd >= 0)
		close(tmp_fd);
	unlink("/tmp/orlix-fd-alias-probe");
	orlix_test_exit();
}
