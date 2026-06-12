// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <unistd.h>

#include "orlix_kselftest_user.h"

static const char *errno_message(int value)
{
	switch (value) {
	case ENOENT:
		return "No such file or directory";
	case ENOTDIR:
		return "Not a directory";
	case ELOOP:
		return "Too many levels of symbolic links";
	default:
		return "Unknown error";
	}
}

static void write_errno_event(const char *operation, const char *path,
			      int actual_errno, int expected_errno)
{
	char line[256];
	size_t pos = 0;

	pos = orlix_append_cstr(line, pos, sizeof(line), "{\"operation\":\"");
	pos = orlix_append_cstr(line, pos, sizeof(line), operation);
	pos = orlix_append_cstr(line, pos, sizeof(line), "\",\"path\":\"");
	pos = orlix_append_cstr(line, pos, sizeof(line), path);
	pos = orlix_append_cstr(line, pos, sizeof(line), "\",\"errno\":");
	pos = orlix_append_uint(line, pos, sizeof(line),
				(unsigned int)actual_errno);
	pos = orlix_append_cstr(line, pos, sizeof(line), ",\"name\":\"");
	pos = orlix_append_cstr(line, pos, sizeof(line),
				errno_message(actual_errno));
	pos = orlix_append_cstr(line, pos, sizeof(line), "\",\"expected\":");
	pos = orlix_append_uint(line, pos, sizeof(line),
				(unsigned int)expected_errno);
	pos = orlix_append_cstr(line, pos, sizeof(line), "}\n");
	orlix_write_bytes(line, pos);
}

static bool open_fails_with_errno(const char *path, const char *oracle_path,
				  int expected_errno)
{
	int fd;
	int actual_errno;

	errno = 0;
	fd = open(path, O_RDONLY);
	if (fd >= 0) {
		close(fd);
		return false;
	}
	actual_errno = errno;
	write_errno_event("open", oracle_path, actual_errno, expected_errno);
	return actual_errno == expected_errno;
}

static bool stat_fails_with_errno(const char *path, const char *oracle_path,
				  int expected_errno)
{
	struct stat st;
	int actual_errno;

	errno = 0;
	if (stat(path, &st) == 0)
		return false;
	actual_errno = errno;
	write_errno_event("stat", oracle_path, actual_errno, expected_errno);
	return actual_errno == expected_errno;
}

int main(void)
{
	int fd;
	bool fixture_created;
	bool cleaned;

	orlix_test_plan(6);

	unlink("path-errno-root/regular");
	unlink("path-errno-root/loop-a");
	unlink("path-errno-root/loop-b");
	rmdir("path-errno-root/dir");
	rmdir("path-errno-root");

	fixture_created = mkdir("path-errno-root", 0700) == 0 &&
			  mkdir("path-errno-root/dir", 0700) == 0;
	fd = open("path-errno-root/regular", O_CREAT | O_EXCL | O_WRONLY, 0600);
	fixture_created = fixture_created && fd >= 0;
	if (fd >= 0)
		close(fd);
	fixture_created = fixture_created &&
			  symlink("loop-b", "path-errno-root/loop-a") == 0 &&
			  symlink("loop-a", "path-errno-root/loop-b") == 0;

	orlix_test_result(fixture_created,
			  "path errno fixture created through Linux VFS");
	orlix_write_all("ORLIX-ORACLE-BEGIN path-errno\n");
	orlix_test_result(open_fails_with_errno("path-errno-root/missing",
						"missing", ENOENT),
			  "missing path returns ENOENT");
	orlix_test_result(
		open_fails_with_errno("path-errno-root/regular/child",
				      "regular/child", ENOTDIR),
		"non-directory child returns ENOTDIR");
	orlix_test_result(stat_fails_with_errno("path-errno-root/loop-a",
						"loop-a", ELOOP),
			  "symlink loop returns ELOOP");
	orlix_test_result(stat_fails_with_errno("path-errno-root/regular/",
						"regular/", ENOTDIR),
			  "trailing slash on regular file returns ENOTDIR");
	orlix_write_all("ORLIX-ORACLE-END path-errno\n");

	cleaned = unlink("path-errno-root/loop-a") == 0 &&
		  unlink("path-errno-root/loop-b") == 0 &&
		  unlink("path-errno-root/regular") == 0 &&
		  rmdir("path-errno-root/dir") == 0 &&
		  rmdir("path-errno-root") == 0;
	orlix_test_result(cleaned, "path errno fixture cleaned");

	orlix_test_exit();
}
