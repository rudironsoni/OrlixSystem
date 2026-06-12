// SPDX-License-Identifier: MIT

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void print_errno_event(const char *operation,
			      const char *path,
			      int expected)
{
	int actual = errno;

	printf("{\"operation\":\"%s\",\"path\":\"%s\",\"errno\":%d,\"name\":\"%s\",\"expected\":%d}\n",
	       operation, path, actual, strerror(actual), expected);
}

static int expect_open_errno(const char *path, int expected)
{
	int fd = open(path, O_RDONLY);

	if (fd >= 0) {
		close(fd);
		printf("{\"operation\":\"open\",\"path\":\"%s\",\"unexpected\":\"success\"}\n",
		       path);
		return 1;
	}
	print_errno_event("open", path, expected);
	return errno == expected ? 0 : 1;
}

static int expect_stat_errno(const char *path, int expected)
{
	struct stat st;

	if (stat(path, &st) == 0) {
		printf("{\"operation\":\"stat\",\"path\":\"%s\",\"unexpected\":\"success\"}\n",
		       path);
		return 1;
	}
	print_errno_event("stat", path, expected);
	return errno == expected ? 0 : 1;
}

int main(void)
{
	int failed = 0;

	(void)unlink("regular");
	(void)unlink("loop-a");
	(void)unlink("loop-b");
	if (rmdir("dir") != 0 && errno != ENOENT)
		return 10;

	{
		int fd = open("regular", O_CREAT | O_TRUNC | O_WRONLY, 0644);

		if (fd < 0)
			return 11;
		if (write(fd, "x", 1) != 1)
			return 12;
		if (close(fd) != 0)
			return 13;
	}
	if (mkdir("dir", 0755) != 0)
		return 14;
	if (symlink("loop-b", "loop-a") != 0)
		return 15;
	if (symlink("loop-a", "loop-b") != 0)
		return 16;

	failed |= expect_open_errno("missing", ENOENT);
	failed |= expect_open_errno("regular/child", ENOTDIR);
	failed |= expect_stat_errno("loop-a", ELOOP);
	failed |= expect_stat_errno("regular/", ENOTDIR);

	if (unlink("regular") != 0)
		return 17;
	if (unlink("loop-a") != 0)
		return 18;
	if (unlink("loop-b") != 0)
		return 19;
	if (rmdir("dir") != 0)
		return 20;

	return failed ? 1 : 0;
}
