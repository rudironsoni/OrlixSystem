// SPDX-License-Identifier: GPL-3.0-or-later

#include <errno.h>
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static const uid_t test_user_uid = 65534;
static const gid_t test_user_gid = 65534;

static void park_init(void)
{
	for (;;)
		(void)pause();
}

static void ensure_runtime_filesystems(void)
{
	(void)mkdir("/proc", 0555);
	(void)mkdir("/sys", 0555);
	(void)mkdir("/tmp", 01777);
	(void)mkdir("/dev", 0755);
	(void)mount("proc", "/proc", "proc", 0, NULL);
	(void)mount("sysfs", "/sys", "sysfs", 0, NULL);
	(void)mount("tmpfs", "/tmp", "tmpfs", 0, "mode=1777");
	(void)mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);
	(void)mkdir("/dev/pts", 0755);
	(void)mount("devpts", "/dev/pts", "devpts", 0, "gid=5,mode=620");
}

static int enter_user_identity(void)
{
	gid_t groups[] = {0, 2};

	if (setgroups(2, groups) != 0) {
		fprintf(stderr, "orlix-coreutils-init: setgroups failed: %s (%d)\n",
			strerror(errno), errno);
		return -1;
	}
	if (setgid(test_user_gid) != 0) {
		fprintf(stderr, "orlix-coreutils-init: setgid failed: %s (%d)\n",
			strerror(errno), errno);
		return -1;
	}
	if (setuid(test_user_uid) != 0) {
		fprintf(stderr, "orlix-coreutils-init: setuid failed: %s (%d)\n",
			strerror(errno), errno);
		return -1;
	}
	return 0;
}

static int run_as(int argc, char **argv)
{
	if (argc < 4) {
		fprintf(stderr, "usage: %s --run-as root|user command [args...]\n",
			argv[0]);
		return 125;
	}
	if (!strcmp(argv[2], "user") && enter_user_identity() != 0)
		return 125;
	if (strcmp(argv[2], "root") && strcmp(argv[2], "user")) {
		fprintf(stderr, "orlix-coreutils-init: unknown identity: %s\n",
			argv[2]);
		return 125;
	}
	execv(argv[3], &argv[3]);
	fprintf(stderr, "orlix-coreutils-init: execv %s failed: %s (%d)\n",
		argv[3], strerror(errno), errno);
	return 127;
}

static int run_upstream_check(void)
{
	pid_t child;
	int status;
	char *const argv[] = {
		"/bin/bash",
		"/coreutils-build/run-upstream-coreutils-tests.sh",
		NULL,
	};
	char *const envp[] = {
		"HOME=/tmp",
		"LANG=C",
		"LC_ALL=C",
		"LOGNAME=root",
		"PATH=/coreutils-build/src:/bin:/usr/bin",
		"PWD=/coreutils-build",
		"SHELL=/bin/bash",
		"TMPDIR=/tmp",
		"USER=root",
		NULL,
	};

	child = fork();
	if (child == 0) {
		(void)chdir("/coreutils-build");
		execve(argv[0], argv, envp);
		fprintf(stderr, "orlix-coreutils-init: execve %s failed: %s (%d)\n",
			argv[0], strerror(errno), errno);
		_exit(127);
	}
	if (child < 0) {
		fprintf(stderr, "orlix-coreutils-init: fork failed: %s (%d)\n",
			strerror(errno), errno);
		return EXIT_FAILURE;
	}
	if (waitpid(child, &status, 0) != child) {
		fprintf(stderr, "orlix-coreutils-init: waitpid failed: %s (%d)\n",
			strerror(errno), errno);
		return EXIT_FAILURE;
	}
	if (WIFEXITED(status))
		return WEXITSTATUS(status);
	if (WIFSIGNALED(status))
		fprintf(stderr, "orlix-coreutils-init: upstream runner killed by signal %d\n",
			WTERMSIG(status));
	else
		fprintf(stderr, "orlix-coreutils-init: upstream runner ended with status 0x%x\n",
			status);
	return EXIT_FAILURE;
}

int main(int argc, char **argv)
{
	int result;

	if (argc > 1 && !strcmp(argv[1], "--run-as"))
		return run_as(argc, argv);

	puts("ORLIX-COREUTILS-TEST-INIT");
	fflush(stdout);
	ensure_runtime_filesystems();
	result = run_upstream_check();
	if (getpid() == 1)
		park_init();
	return result;
}
