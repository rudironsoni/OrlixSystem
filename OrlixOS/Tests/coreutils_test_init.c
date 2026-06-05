// SPDX-License-Identifier: GPL-3.0-or-later

#include <errno.h>
#include <fcntl.h>
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
static const uid_t root_uid = 0;
static const gid_t root_gid = 0;

static void park_init(void)
{
	for (;;)
		(void)pause();
}

static void load_selinux_policy(void)
{
	static const char policy_path[] =
		"/etc/selinux/targeted/policy/policy.33";
	static const char load_path[] = "/sys/fs/selinux/load";
	struct stat st;
	char *policy;
	ssize_t nread;
	int policy_fd;
	int load_fd;

	if (stat(policy_path, &st) != 0 || st.st_size <= 0)
		return;

	policy = malloc((size_t)st.st_size);
	if (!policy) {
		fprintf(stderr, "orlix-coreutils-init: malloc SELinux policy failed\n");
		return;
	}

	policy_fd = open(policy_path, O_RDONLY);
	if (policy_fd < 0) {
		fprintf(stderr, "orlix-coreutils-init: open %s failed: %s (%d)\n",
			policy_path, strerror(errno), errno);
		free(policy);
		return;
	}

	nread = read(policy_fd, policy, (size_t)st.st_size);
	close(policy_fd);
	if (nread != st.st_size) {
		fprintf(stderr, "orlix-coreutils-init: read %s failed: %s (%d)\n",
			policy_path, strerror(errno), errno);
		free(policy);
		return;
	}

	load_fd = open(load_path, O_RDWR);
	if (load_fd < 0) {
		fprintf(stderr, "orlix-coreutils-init: open %s failed: %s (%d)\n",
			load_path, strerror(errno), errno);
		free(policy);
		return;
	}

	if (write(load_fd, policy, (size_t)st.st_size) != st.st_size)
		fprintf(stderr, "orlix-coreutils-init: load SELinux policy failed: %s (%d)\n",
			strerror(errno), errno);

	close(load_fd);
	free(policy);
}

static void ensure_runtime_filesystems(void)
{
	(void)mkdir("/proc", 0555);
	(void)mkdir("/sys", 0555);
	(void)mkdir("/tmp", 01777);
	(void)mkdir("/dev", 0755);
	(void)mount("proc", "/proc", "proc", 0, NULL);
	(void)mount("sysfs", "/sys", "sysfs", 0, NULL);
	(void)mkdir("/sys/fs", 0755);
	(void)mkdir("/sys/fs/selinux", 0755);
	(void)mount("selinuxfs", "/sys/fs/selinux", "selinuxfs",
		     MS_NOSUID | MS_NOEXEC, NULL);
	load_selinux_policy();
	(void)mount("tmpfs", "/tmp", "tmpfs", 0, "mode=1777");
	(void)mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);
	(void)mkdir("/dev/pts", 0755);
	(void)mount("devpts", "/dev/pts", "devpts", 0, "gid=5,mode=620");
}

static int enter_identity(uid_t uid, gid_t gid, size_t ngroups,
			  const gid_t *groups)
{
	if (setgroups(ngroups, groups) != 0) {
		fprintf(stderr, "orlix-coreutils-init: setgroups failed: %s (%d)\n",
			strerror(errno), errno);
		return -1;
	}
	if (setgid(gid) != 0) {
		fprintf(stderr, "orlix-coreutils-init: setgid failed: %s (%d)\n",
			strerror(errno), errno);
		return -1;
	}
	if (setuid(uid) != 0) {
		fprintf(stderr, "orlix-coreutils-init: setuid failed: %s (%d)\n",
			strerror(errno), errno);
		return -1;
	}
	return 0;
}

static int enter_root_identity(void)
{
	gid_t groups[] = {root_gid};

	return enter_identity(root_uid, root_gid, 1, groups);
}

static int enter_user_identity(void)
{
	gid_t groups[] = {root_gid, 2};

	return enter_identity(test_user_uid, test_user_gid, 2, groups);
}

static int run_as(int argc, char **argv)
{
	if (argc < 4) {
		fprintf(stderr, "usage: %s --run-as root|user command [args...]\n",
			argv[0]);
		return 125;
	}
	if (strcmp(argv[2], "root") && strcmp(argv[2], "user")) {
		fprintf(stderr, "orlix-coreutils-init: unknown identity: %s\n",
			argv[2]);
		return 125;
	}
	if (!strcmp(argv[2], "root") && enter_root_identity() != 0)
		return 125;
	if (!strcmp(argv[2], "user") && enter_user_identity() != 0)
		return 125;
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
