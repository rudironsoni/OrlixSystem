// SPDX-License-Identifier: GPL-2.0

#include <sys/wait.h>
#include <unistd.h>

#include "orlix_kselftest_user.h"

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
	orlix_test_plan(3);

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

	park_init();
}
