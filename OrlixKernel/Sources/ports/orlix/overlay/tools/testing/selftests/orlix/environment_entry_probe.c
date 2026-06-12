// SPDX-License-Identifier: GPL-2.0

#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdbool.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include "orlix_kselftest_user.h"

#define ENTRY_ROOT "/mnt/orlix-environment-entry-root"
#define ENTRY_BINARY ENTRY_ROOT "/environment-entry-probe"
#define ENTRY_OS_RELEASE ENTRY_ROOT "/etc/os-release"
#define ENTRY_OLDROOT ENTRY_ROOT "/.oldroot"
#define PARENT_MARKER "/orlix-environment-entry-parent-marker"

static bool env_has_child_marker(char *const envp[])
{
	size_t i;

	for (i = 0; envp[i]; i++) {
		static const char marker[] = "ORLIX_ENV_ENTRY_CHILD=1";

		if (orlix_strlen(envp[i]) == sizeof(marker) - 1 &&
		    orlix_memcmp(envp[i], marker, sizeof(marker) - 1) == 0)
			return true;
	}
	return false;
}

static bool file_starts_with(const char *path, const char *expected)
{
	char buffer[64];
	size_t size;

	if (orlix_read_file(path, buffer, sizeof(buffer), &size) != 0)
		return false;
	return size >= orlix_strlen(expected) &&
	       orlix_memcmp(buffer, expected, orlix_strlen(expected)) == 0;
}

static int write_file(const char *path, const char *data)
{
	int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
	size_t len;
	ssize_t written;

	if (fd < 0)
		return -1;
	len = orlix_strlen(data);
	written = write(fd, data, len);
	if (close(fd) != 0)
		return -1;
	return written == (ssize_t)len ? 0 : -1;
}

static int copy_file(const char *from, const char *to)
{
	char buffer[4096];
	int in_fd;
	int out_fd;
	int result = -1;

	in_fd = open(from, O_RDONLY);
	if (in_fd < 0)
		return -1;
	out_fd = open(to, O_CREAT | O_TRUNC | O_WRONLY, 0755);
	if (out_fd < 0)
		goto out_close_in;

	for (;;) {
		ssize_t nread = read(in_fd, buffer, sizeof(buffer));
		char *cursor = buffer;

		if (nread < 0)
			goto out_close_out;
		if (nread == 0)
			break;
		while (nread > 0) {
			ssize_t nwritten = write(out_fd, cursor, (size_t)nread);

			if (nwritten <= 0)
				goto out_close_out;
			cursor += nwritten;
			nread -= nwritten;
		}
	}
	result = 0;

out_close_out:
	if (close(out_fd) != 0)
		result = -1;
out_close_in:
	if (close(in_fd) != 0)
		result = -1;
	return result;
}

static void test_comment_uint(const char *prefix, unsigned int value)
{
	char line[96];
	size_t pos = 0;

	pos = orlix_append_cstr(line, pos, sizeof(line), "# ");
	pos = orlix_append_cstr(line, pos, sizeof(line), prefix);
	pos = orlix_append_uint(line, pos, sizeof(line), value);
	pos = orlix_append_cstr(line, pos, sizeof(line), "\n");
	orlix_write_bytes(line, pos);
}

static int child_after_exec(void)
{
	if (!file_starts_with("/etc/os-release", "NAME=OrlixEnvEntryProbe\n"))
		return 20;
	if (access("/environment-entry-probe", X_OK) != 0)
		return 21;
	if (access(PARENT_MARKER, F_OK) == 0)
		return 22;
	if (errno != ENOENT)
		return 23;
	if (access("/.oldroot/orlix-environment-entry-parent-marker", F_OK) == 0)
		return 26;
	if (errno != ENOENT)
		return 27;
	return 0;
}

static int child_enter_environment(void)
{
	char *const argv[] = { "/environment-entry-probe", NULL };
	char *const envp[] = { "ORLIX_ENV_ENTRY_CHILD=1", NULL };

	if (syscall(SYS_unshare, CLONE_NEWNS) != 0)
		return 10;
	if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) != 0)
		return 80 + errno;
	if (mkdir("/mnt", 0755) < 0 && errno != EEXIST)
		return 11;
	if (mkdir(ENTRY_ROOT, 0755) < 0 && errno != EEXIST)
		return 12;
	if (mount("tmpfs", ENTRY_ROOT, "tmpfs", 0, "mode=755") != 0)
		return 13;
	if (mkdir(ENTRY_ROOT "/etc", 0755) != 0)
		return 14;
	if (write_file(ENTRY_OS_RELEASE, "NAME=OrlixEnvEntryProbe\n") != 0)
		return 15;
	if (copy_file("/proc/self/exe", ENTRY_BINARY) != 0)
		return 16;
	if (chmod(ENTRY_BINARY, 0755) != 0)
		return 17;
	if (mkdir(ENTRY_OLDROOT, 0755) != 0)
		return 18;
	if (syscall(SYS_pivot_root, ENTRY_ROOT, ENTRY_OLDROOT) != 0)
		return 100 + errno;
	if (chdir("/") != 0)
		return 24;
	if (umount2("/.oldroot", MNT_DETACH) != 0)
		return 26;
	if (rmdir("/.oldroot") != 0)
		return 27;
	execve(argv[0], argv, envp);
	return 25;
}

int main(int argc, char **argv, char *const envp[])
{
	int status = 0;
	pid_t child;

	(void)argc;
	(void)argv;

	if (env_has_child_marker(envp))
		return child_after_exec();

	orlix_test_plan(6);
	if (write_file(PARENT_MARKER, "parent\n") != 0) {
		orlix_test_result(false, "environment entry parent marker created");
		orlix_test_result(false, "environment entry child started");
		orlix_test_result(false,
				  "environment entry child pivot_root completed");
		orlix_test_result(false, "environment entry child exited cleanly");
		orlix_test_result(false, "environment entry root is hidden from parent");
		orlix_test_result(false, "environment entry parent marker cleaned");
		orlix_test_exit();
	}

	orlix_test_result(true, "environment entry parent marker created");
	child = fork();
	if (child == 0)
		_exit(child_enter_environment());

	orlix_test_result(child > 0, "environment entry child started");
	if (child > 0 && waitpid(child, &status, 0) == child) {
		if (WIFEXITED(status))
			test_comment_uint("environment entry child exit status ",
					  (unsigned int)WEXITSTATUS(status));
		else if (WIFSIGNALED(status))
			test_comment_uint("environment entry child signal ",
					  (unsigned int)WTERMSIG(status));
		orlix_test_result(WIFEXITED(status) && WEXITSTATUS(status) == 0,
				  "environment entry child pivot_root completed");
		orlix_test_result(WIFEXITED(status) && WEXITSTATUS(status) == 0,
				  "environment entry child exited cleanly");
	} else {
		orlix_test_result(false,
				  "environment entry child pivot_root completed");
		orlix_test_result(false, "environment entry child exited cleanly");
	}

	orlix_test_result(access(ENTRY_BINARY, F_OK) != 0 && errno == ENOENT,
			  "environment entry root is hidden from parent");
	orlix_test_result(unlink(PARENT_MARKER) == 0,
			  "environment entry parent marker cleaned");
	orlix_test_exit();
}
