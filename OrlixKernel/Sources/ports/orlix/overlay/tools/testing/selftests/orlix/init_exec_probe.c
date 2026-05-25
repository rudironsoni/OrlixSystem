// SPDX-License-Identifier: GPL-2.0

#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include "orlix_kselftest_user.h"

static bool mmap_syscall_returns_writable_memory(void)
{
	void *mapping;
	volatile char *bytes;
	bool ok = false;

	mapping = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mapping != MAP_FAILED) {
		bytes = mapping;
		bytes[0] = 0x2a;
		ok = bytes[0] == 0x2a;
		(void)munmap(mapping, 4096);
	}

	return ok;
}

static void park_init(void)
{
	for (;;)
		(void)pause();
}

int main(void)
{
	static const char message[] = "ORLIX-INIT-EXEC-PROBE\n";
	int status = 0;
	pid_t child;

	(void)write(STDOUT_FILENO, message, sizeof(message) - 1);
	orlix_test_plan(4);

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

	if (getpid() == 1)
		park_init();
	orlix_test_exit();
}
