// SPDX-License-Identifier: GPL-2.0

#include <sys/mman.h>
#include <sys/wait.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "orlix_kselftest_user.h"

#ifndef ORLIX_HOSTED_USER_BASE_ADDRESS
#error "ORLIX_HOSTED_USER_BASE_ADDRESS must be provided by the Orlix kselftest target metadata"
#endif

static bool mmap_syscall_returns_writable_memory(void)
{
	long page_size;
	void *mapping;
	volatile char *bytes;
	bool ok = false;

	page_size = sysconf(_SC_PAGESIZE);
	if (page_size <= 0)
		return false;

	mapping = mmap(NULL, (size_t)page_size, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mapping != MAP_FAILED) {
		bytes = mapping;
		bytes[0] = 0x2a;
		ok = bytes[0] == 0x2a;
		(void)munmap(mapping, (size_t)page_size);
	}

	return ok;
}

static bool anonymous_mmap_stays_inside_hosted_user_window(void)
{
	long page_size;
	void *mapping;
	bool ok;

	page_size = sysconf(_SC_PAGESIZE);
	if (page_size <= 0)
		return false;

	mapping = mmap(NULL, (size_t)page_size, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mapping == MAP_FAILED)
		return false;

	ok = (uintptr_t)mapping >= ORLIX_HOSTED_USER_BASE_ADDRESS;
	(void)munmap(mapping, (size_t)page_size);
	return ok;
}

static void park_init(void)
{
	for (;;)
		(void)pause();
}

static bool fork_exec_child_exits(const char *self_path)
{
	char *const exec_argv[] = { (char *)self_path, "--exec-child", NULL };
	int status = 0;
	pid_t child;

	if (!self_path || !self_path[0])
		return false;

	child = fork();
	if (child == 0) {
		execv(self_path, exec_argv);
		_exit(127);
	}
	if (child < 0)
		return false;
	if (waitpid(child, &status, 0) != child)
		return false;
	return WIFEXITED(status) && WEXITSTATUS(status) == 73;
}

int main(int argc, char **argv)
{
	static const char message[] = "ORLIX-INIT-EXEC-PROBE\n";
	int status = 0;
	pid_t child;

	if (argc == 2 && strcmp(argv[1], "--exec-child") == 0)
		return 73;

	(void)write(STDOUT_FILENO, message, sizeof(message) - 1);
	orlix_test_plan(6);

	child = fork();
	if (child == 0)
		_exit(42);

	orlix_test_result(child > 0, "fork creates a child task");
	if (child > 0) {
		pid_t waited = waitpid(child, &status, 0);

		orlix_test_result(waited == child,
				  "waitpid returns the forked child");
		orlix_test_result(WIFEXITED(status) &&
				  WEXITSTATUS(status) == 42,
				  "waitpid observes the child exit status");
	} else {
		orlix_test_result(false, "waitpid returns the forked child");
		orlix_test_result(false,
				  "waitpid observes the child exit status");
	}
	orlix_test_result(mmap_syscall_returns_writable_memory(),
			  "mmap syscall returns writable memory");
	orlix_test_result(anonymous_mmap_stays_inside_hosted_user_window(),
			  "writable anonymous mmap stays inside hosted user window");
	orlix_test_result(fork_exec_child_exits(argv[0]),
			  "forked child execs current image and exits");

	if (getpid() == 1)
		park_init();
	orlix_test_exit();
}
