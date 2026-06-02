// SPDX-License-Identifier: GPL-3.0-or-later

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static char test_list[131072];
static unsigned int test_index;
static unsigned int test_failures;
static unsigned int test_skips;
#ifndef ORLIXOS_COREUTILS_TEST_TIMEOUT_SECONDS
#define ORLIXOS_COREUTILS_TEST_TIMEOUT_SECONDS 600
#endif
#ifndef ORLIXOS_COREUTILS_TEST_SHELL_TRACE
#define ORLIXOS_COREUTILS_TEST_SHELL_TRACE 0
#endif
#ifndef ORLIXOS_COREUTILS_TEST_GETLIMITS_PROBE
#define ORLIXOS_COREUTILS_TEST_GETLIMITS_PROBE 0
#endif
static const unsigned int test_timeout_seconds =
	ORLIXOS_COREUTILS_TEST_TIMEOUT_SECONDS;
static const uid_t test_user_uid = 65534;
static const gid_t test_user_gid = 65534;

enum test_mode {
	TEST_MODE_USER,
	TEST_MODE_ROOT,
};

static char *const test_envp[] = {
	"HOME=/tmp",
	"LANG=C",
	"LC_ALL=C",
	"LOGNAME=nobody",
	"PATH=/coreutils-build/src:/bin:/usr/bin",
	"PWD=/coreutils-build",
	"SHELL=/bin/bash",
	"CONFIG_SHELL=/bin/bash",
	"srcdir=/coreutils",
	"top_srcdir=/coreutils",
	"abs_srcdir=/coreutils",
	"abs_top_srcdir=/coreutils",
	"abs_top_builddir=/coreutils-build",
	"CONFIG_HEADER=/coreutils-build/lib/config.h",
	"VERSION=9.11",
	"PACKAGE_VERSION=9.11",
	"PERL=/usr/bin/perl",
	"PERL5LIB=/usr/lib/perl5/5.40.2",
	"built_programs=[ b2sum base32 base64 basenc basename cat chgrp chmod chown chroot cksum comm cp csplit cut date dd df dir dircolors dirname du echo env expand expr factor false fmt fold groups head hostid id install join link ln logname ls md5sum mkdir mkfifo mknod mktemp mv nice nl nohup nproc numfmt od paste pathchk pinky pr printenv printf ptx pwd readlink realpath rm rmdir seq sha1sum sha224sum sha256sum sha384sum sha512sum shred shuf sleep sort split stat stty sum sync tac tail tee test timeout touch tr true truncate tsort tty uname unexpand uniq unlink users vdir wc who whoami yes ]",
	"AWK=awk",
	"COREUTILS_GROUPS=0 2",
	"EGREP=grep -E",
	"EXEEXT=",
	"MAKE=make",
	"NON_ROOT_GID=65534",
	"NON_ROOT_USERNAME=nobody",
	"RUN_EXPENSIVE_TESTS=yes",
	"RUN_VERY_EXPENSIVE_TESTS=yes",
	"TMPDIR=/tmp",
	"USER=nobody",
	"VERBOSE=",
	NULL,
};

static void park_init(void)
{
	for (;;)
		(void)pause();
}

static void test_result(const char *state, const char *name)
{
	test_index++;
	if (!strcmp(state, "not ok"))
		test_failures++;
	if (!strcmp(state, "ok # SKIP"))
		test_skips++;
	printf("%s %u - %s\n", state, test_index, name);
	fflush(stdout);
}

static bool read_file(const char *path, char *buffer, size_t capacity,
		      size_t *size)
{
	FILE *file;

	file = fopen(path, "r");
	if (!file)
		return false;
	*size = fread(buffer, 1, capacity - 1, file);
	buffer[*size] = '\0';
	fclose(file);
	return true;
}

static unsigned int count_tests(char *data, size_t size)
{
	unsigned int count = 0;
	char *cursor = data;

	while (cursor < data + size) {
		char *newline = strchr(cursor, '\n');

		if (!newline)
			newline = data + size;
		if (newline > cursor && *cursor != '#')
			count++;
		cursor = newline + 1;
	}
	return count;
}

static void ensure_runtime_filesystems(void)
{
	gid_t test_groups[] = {0, 2};

	(void)mkdir("/proc", 0555);
	(void)mkdir("/tmp", 01777);
	(void)mkdir("/dev", 0755);
	(void)mount("proc", "/proc", "proc", 0, NULL);
	(void)mount("tmpfs", "/tmp", "tmpfs", 0, "mode=1777");
	(void)mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);
	if (setgroups(2, test_groups) != 0) {
		printf("# setgroups for Coreutils group tests failed: %s (%d)\n",
		       strerror(errno), errno);
		fflush(stdout);
	}
	(void)chdir("/coreutils-build");
}

static int enter_test_identity(enum test_mode mode, const char *name)
{
	gid_t groups[] = {0, 2};

	if (mode == TEST_MODE_ROOT)
		return 0;
	if (setgroups(2, groups) != 0) {
		dprintf(STDERR_FILENO,
			"# setgroups for unprivileged test %s failed: %s (%d)\n",
			name, strerror(errno), errno);
		return -1;
	}
	if (setgid(test_user_gid) != 0) {
		dprintf(STDERR_FILENO,
			"# setgid for unprivileged test %s failed: %s (%d)\n",
			name, strerror(errno), errno);
		return -1;
	}
	if (setuid(test_user_uid) != 0) {
		dprintf(STDERR_FILENO,
			"# setuid for unprivileged test %s failed: %s (%d)\n",
			name, strerror(errno), errno);
		return -1;
	}
	return 0;
}

static int run_test(enum test_mode mode, const char *name)
{
	pid_t child;
	int timeout_fd = -1;
	int status;
	const char *interpreter = "/bin/bash";
	char test_path[512];
	struct timespec start_time;
	bool timed_out = false;
	bool is_perl = strlen(name) > 3 && !strcmp(name + strlen(name) - 3, ".pl");

	snprintf(test_path, sizeof(test_path), "/coreutils/%s", name);
	if (is_perl)
		interpreter = "/usr/bin/perl";

	printf("# exec %s %s%s %s\n", mode == TEST_MODE_ROOT ? "root" : "user",
	       interpreter,
	       is_perl ? " -w -I/coreutils/tests -MCuSkip -MCoreutils" : "",
	       test_path);
	fflush(stdout);

	child = fork();
	if (child == 0) {
		char *const shell_argv[] = {
			(char *)interpreter,
#if ORLIXOS_COREUTILS_TEST_SHELL_TRACE
			(char *)"-x",
#endif
			test_path,
			NULL,
		};
		char *const perl_argv[] = {
			(char *)interpreter,
			"-w",
			"-I/coreutils/tests",
			"-MCuSkip",
			"-MCoreutils",
			test_path,
			NULL,
		};
		char *const *argv = is_perl ? perl_argv : shell_argv;

		(void)setpgid(0, 0);
		if (enter_test_identity(mode, name) != 0)
			_exit(125);
		if (chdir("/coreutils-build") != 0) {
			dprintf(STDERR_FILENO,
				"# chdir /coreutils-build failed for %s: %s (%d)\n",
				test_path, strerror(errno), errno);
			_exit(125);
		}
		if (dup2(STDERR_FILENO, 9) < 0) {
			dprintf(STDERR_FILENO,
				"# dup2 stderr to fd 9 failed for %s: %s (%d)\n",
				test_path, strerror(errno), errno);
			_exit(125);
		}
		execve(argv[0], argv, test_envp);
		dprintf(STDERR_FILENO, "# execve %s failed for %s: %s (%d)\n",
			argv[0], test_path, strerror(errno), errno);
		_exit(127);
	}
	if (child < 0)
		return -1;
	(void)setpgid(child, child);
	(void)clock_gettime(CLOCK_MONOTONIC, &start_time);
	timeout_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
	if (timeout_fd >= 0) {
		struct itimerspec timeout = {0};

		timeout.it_value.tv_sec = test_timeout_seconds;
		if (timerfd_settime(timeout_fd, 0, &timeout, NULL) != 0) {
			close(timeout_fd);
			timeout_fd = -1;
		}
	}
	for (;;) {
		pid_t waited = waitpid(child, &status, WNOHANG);

		if (waited == child)
			break;
		if (waited < 0) {
			if (errno == EINTR)
				continue;
			if (timeout_fd >= 0)
				close(timeout_fd);
			printf("# waitpid failed for %s: %s (%d)\n", name,
			       strerror(errno), errno);
			fflush(stdout);
			return -1;
		}
		if (timeout_fd >= 0) {
			uint64_t expirations;
			ssize_t bytes = read(timeout_fd, &expirations,
					     sizeof(expirations));

			if (bytes == (ssize_t)sizeof(expirations) &&
			    expirations > 0)
				timed_out = true;
		} else {
			struct timespec now;

			(void)clock_gettime(CLOCK_MONOTONIC, &now);
			if ((unsigned int)(now.tv_sec - start_time.tv_sec) >=
			    test_timeout_seconds)
				timed_out = true;
		}
		if (timed_out) {
			printf("# %s timed out after %u seconds\n", name,
			       test_timeout_seconds);
			fflush(stdout);
			(void)kill(-child, SIGKILL);
			(void)kill(child, SIGKILL);
			for (unsigned int reap_wait_ms = 0; reap_wait_ms < 5000;
			     reap_wait_ms += 100) {
				if (waitpid(child, &status, WNOHANG) == child)
					break;
				usleep(100000);
			}
			if (timeout_fd >= 0)
				close(timeout_fd);
			return 124;
		}
		usleep(100000);
	}
	if (timeout_fd >= 0)
		close(timeout_fd);
	if (timed_out)
		return 124;
	if (WIFEXITED(status)) {
		int exit_status = WEXITSTATUS(status);

		if (exit_status != 0)
			printf("# %s exited with status %d\n", name, exit_status);
		fflush(stdout);
		return exit_status;
	}
	if (WIFSIGNALED(status))
		printf("# %s killed by signal %d\n", name, WTERMSIG(status));
	else
		printf("# %s ended with wait status 0x%x\n", name, status);
	fflush(stdout);
	return -1;
}

#if ORLIXOS_COREUTILS_TEST_GETLIMITS_PROBE
static void run_getlimits_probe(void)
{
	pid_t child;
	int timeout_fd = -1;
	int status = 0;
	bool timed_out = false;
	char *const argv[] = {
		(char *)"/bin/getlimits",
		NULL,
	};

	printf("# getlimits probe start\n");
	fflush(stdout);

	child = fork();
	if (child == 0) {
		(void)setpgid(0, 0);
		if (enter_test_identity(TEST_MODE_USER, "getlimits-probe") != 0)
			_exit(125);
		if (chdir("/coreutils-build") != 0) {
			dprintf(STDERR_FILENO,
				"# getlimits probe chdir failed: %s (%d)\n",
				strerror(errno), errno);
			_exit(125);
		}
		execve(argv[0], argv, test_envp);
		dprintf(STDERR_FILENO, "# getlimits probe execve failed: %s (%d)\n",
			strerror(errno), errno);
		_exit(127);
	}
	if (child < 0) {
		printf("# getlimits probe fork failed: %s (%d)\n", strerror(errno),
		       errno);
		fflush(stdout);
		return;
	}
	(void)setpgid(child, child);
	timeout_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
	if (timeout_fd >= 0) {
		struct itimerspec timeout = {0};

		timeout.it_value.tv_sec = test_timeout_seconds;
		if (timerfd_settime(timeout_fd, 0, &timeout, NULL) != 0) {
			close(timeout_fd);
			timeout_fd = -1;
		}
	}
	for (;;) {
		pid_t waited = waitpid(child, &status, WNOHANG);

		if (waited == child)
			break;
		if (waited < 0) {
			if (errno == EINTR)
				continue;
			printf("# getlimits probe waitpid failed: %s (%d)\n",
			       strerror(errno), errno);
			break;
		}
		if (timeout_fd >= 0) {
			uint64_t expirations;
			ssize_t bytes = read(timeout_fd, &expirations,
					     sizeof(expirations));

			if (bytes == (ssize_t)sizeof(expirations) &&
			    expirations > 0)
				timed_out = true;
		}
		if (timed_out) {
			printf("# getlimits probe timed out after %u seconds\n",
			       test_timeout_seconds);
			(void)kill(-child, SIGKILL);
			(void)kill(child, SIGKILL);
			for (unsigned int reap_wait_ms = 0; reap_wait_ms < 5000;
			     reap_wait_ms += 100) {
				if (waitpid(child, &status, WNOHANG) == child)
					break;
				usleep(100000);
			}
			break;
		}
		usleep(100000);
	}
	if (timeout_fd >= 0)
		close(timeout_fd);
	if (timed_out)
		return;
	if (WIFEXITED(status))
		printf("# getlimits probe exited with status %d\n",
		       WEXITSTATUS(status));
	else if (WIFSIGNALED(status))
		printf("# getlimits probe killed by signal %d\n", WTERMSIG(status));
	else
		printf("# getlimits probe ended with wait status 0x%x\n", status);
	fflush(stdout);
}
#endif

static void run_tests(char *data, size_t size)
{
	char *cursor = data;

	while (cursor < data + size) {
		char *line = cursor;
		char *newline = strchr(cursor, '\n');
		int result;

		if (!newline)
			newline = data + size;
		*newline = '\0';
		if (*line != '\0' && *line != '#') {
			enum test_mode mode = TEST_MODE_USER;
			char *name = line;
			char *space = strchr(line, ' ');

			if (space) {
				*space = '\0';
				name = space + 1;
				if (!strcmp(line, "root"))
					mode = TEST_MODE_ROOT;
				else if (strcmp(line, "user")) {
					printf("# unknown Coreutils test mode %s for %s\n",
					       line, name);
					fflush(stdout);
					test_result("not ok", name);
					cursor = newline + 1;
					continue;
				}
			}
			result = run_test(mode, name);
			if (result == 0)
				test_result("ok", name);
			else if (result == 77)
				test_result("ok # SKIP", name);
			else
				test_result("not ok", name);
		}
		cursor = newline + 1;
	}
}

int main(void)
{
	size_t list_size = 0;
	unsigned int test_count = 0;
	bool have_list;

	puts("ORLIX-COREUTILS-TEST-INIT");
	ensure_runtime_filesystems();
#if ORLIXOS_COREUTILS_TEST_GETLIMITS_PROBE
	run_getlimits_probe();
#endif

	have_list = read_file("/coreutils-test-list.txt", test_list,
			      sizeof(test_list), &list_size);
	if (have_list)
		test_count = count_tests(test_list, list_size);

	printf("TAP version 13\n1..%u\n", test_count + 1);
	test_result(have_list && test_count > 0 ? "ok" : "not ok",
		    "installed upstream coreutils test list is readable");
	if (have_list) {
		have_list = read_file("/coreutils-test-list.txt", test_list,
				      sizeof(test_list), &list_size);
		if (have_list)
			run_tests(test_list, list_size);
	}
	printf("ORLIX-COREUTILS-TEST-END failures=%u skips=%u total=%u\n",
	       test_failures, test_skips, test_index);
	fflush(stdout);

	if (getpid() == 1)
		park_init();
	return test_failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
