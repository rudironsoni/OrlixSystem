// SPDX-License-Identifier: GPL-2.0

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "orlix_kselftest_user.h"

static char test_list[4096];
static char selected_test[128];

static void park_init(void)
{
	for (;;)
		(void)pause();
}

static int mount_procfs(void)
{
	if (mkdir("/proc", 0555) < 0 && errno != EEXIST)
		return -1;
	if (mount("proc", "/proc", "proc", 0, NULL) < 0 && errno != EBUSY)
		return -1;
	return 0;
}

static int mount_sysfs(void)
{
	if (mkdir("/sys", 0555) < 0 && errno != EEXIST)
		return -1;
	if (mount("sysfs", "/sys", "sysfs", 0, NULL) < 0 && errno != EBUSY)
		return -1;
	return 0;
}

static int mount_devtmpfs(void)
{
	if (mkdir("/dev", 0755) < 0 && errno != EEXIST)
		return -1;
	if (mount("devtmpfs", "/dev", "devtmpfs", 0, NULL) < 0 && errno != EBUSY)
		return -1;
	return 0;
}

static int mount_devpts(void)
{
	if (mkdir("/dev/pts", 0755) < 0 && errno != EEXIST)
		return -1;
	if (mount("devpts", "/dev/pts", "devpts", 0,
		  "gid=5,mode=620,ptmxmode=666") < 0 && errno != EBUSY)
		return -1;
	return 0;
}

static int install_fd_alias(const char *target, const char *linkpath)
{
	(void)unlink(linkpath);
	if (symlink(target, linkpath) == 0 || errno == EEXIST)
		return 0;
	return -1;
}

static int install_fd_aliases(void)
{
	if (install_fd_alias("/proc/self/fd", "/dev/fd") != 0)
		return -1;
	if (install_fd_alias("/proc/self/fd/0", "/dev/stdin") != 0)
		return -1;
	if (install_fd_alias("/proc/self/fd/1", "/dev/stdout") != 0)
		return -1;
	if (install_fd_alias("/proc/self/fd/2", "/dev/stderr") != 0)
		return -1;
	return 0;
}

static int mount_tmpfs_at(const char *target, const char *data)
{
	if (mkdir(target, 01777) < 0 && errno != EEXIST)
		return -1;
	if (mount("tmpfs", target, "tmpfs", 0, data) < 0 && errno != EBUSY)
		return -1;
	return 0;
}

static bool parse_orlix_test(const char *line, size_t len, const char **name,
			     size_t *name_len)
{
	static const char prefix[] = "orlix:";
	size_t i;

	for (i = 0; prefix[i]; i++) {
		if (i >= len || line[i] != prefix[i])
			return false;
	}

	*name = line + i;
	*name_len = len - i;
	return *name_len > 0;
}

static void read_selected_test(void)
{
	static const char prefix[] = "orlix.kselftest=";
	char cmdline[1024];
	size_t size = 0;
	size_t pos;

	selected_test[0] = '\0';
	if (orlix_read_file("/proc/cmdline", cmdline, sizeof(cmdline),
			    &size) != 0)
		return;

	for (pos = 0; pos < size; pos++) {
		size_t i;
		size_t out = 0;

		if (pos > 0 && cmdline[pos - 1] != ' ')
			continue;
		for (i = 0; prefix[i]; i++) {
			if (pos + i >= size || cmdline[pos + i] != prefix[i])
				break;
		}
		if (prefix[i])
			continue;
		pos += i;
		while (pos < size && cmdline[pos] != ' ' &&
		       cmdline[pos] != '\n' && out + 1 < sizeof(selected_test))
			selected_test[out++] = cmdline[pos++];
		selected_test[out] = '\0';
		return;
	}
}

static bool selected_test_matches(const char *name, size_t name_len)
{
	if (!selected_test[0])
		return true;
	return orlix_strlen(selected_test) == name_len &&
	       orlix_memcmp(selected_test, name, name_len) == 0;
}

static void build_test_path(char *path, size_t capacity, const char *name,
			    size_t name_len)
{
	static const char prefix[] = "/orlix/";
	size_t pos = 0;
	size_t i;

	for (i = 0; prefix[i] && pos + 1 < capacity; i++)
		path[pos++] = prefix[i];
	for (i = 0; i < name_len && pos + 1 < capacity; i++)
		path[pos++] = name[i];
	path[pos] = '\0';
}

static int run_test(const char *name, size_t name_len)
{
	char path[160];
	pid_t child;
	int status;

	build_test_path(path, sizeof(path), name, name_len);
	orlix_test_comment("exec /orlix/", name, name_len);
	child = fork();
	if (child == 0) {
		char *const argv[] = { path, NULL };

		orlix_test_comment("child exec /orlix/", name, name_len);
		execv(path, argv);
		_exit(127);
	}
	if (child < 0)
		return -1;
	if (waitpid(child, &status, 0) != child)
		return -1;
	if (!WIFEXITED(status))
		return -1;
	return WEXITSTATUS(status);
}

static unsigned int count_orlix_tests(const char *data, size_t size)
{
	unsigned int count = 0;
	size_t line_start = 0;
	size_t pos;

	for (pos = 0; pos <= size; pos++) {
		if (pos != size && data[pos] != '\n')
			continue;

		const char *name;
		size_t name_len;

		if (parse_orlix_test(data + line_start, pos - line_start,
				     &name, &name_len) &&
		    selected_test_matches(name, name_len))
			count++;
		line_start = pos + 1;
	}
	return count;
}

static void run_orlix_tests(const char *data, size_t size)
{
	size_t line_start = 0;
	size_t pos;

	for (pos = 0; pos <= size; pos++) {
		if (pos != size && data[pos] != '\n')
			continue;

		const char *name;
		size_t name_len;

		if (parse_orlix_test(data + line_start, pos - line_start,
				     &name, &name_len) &&
		    selected_test_matches(name, name_len)) {
			int result = run_test(name, name_len);

			orlix_test_result(result == 0, name);
		}
		line_start = pos + 1;
	}
}

int main(void)
{
	size_t list_size = 0;
	unsigned int test_count;
	bool have_list;
	int procfs_result;

	orlix_write_all("ORLIX-KSELFTEST-INIT\n");
	procfs_result = mount_procfs();
	read_selected_test();
	have_list = orlix_read_file("/kselftest-list.txt", test_list,
				    sizeof(test_list), &list_size) == 0;
	test_count = have_list ? count_orlix_tests(test_list, list_size) : 0;

	orlix_test_plan(test_count + 9);
	orlix_test_result(procfs_result == 0, "procfs mounted for kselftest");
	orlix_test_result(mount_sysfs() == 0, "sysfs mounted for kselftest");
	orlix_test_result(mount_devtmpfs() == 0, "devtmpfs mounted for kselftest");
	orlix_test_result(mount_devpts() == 0, "devpts mounted for kselftest");
	orlix_test_result(install_fd_aliases() == 0,
			  "standard fd aliases installed for kselftest");
	orlix_test_result(mount_tmpfs_at("/dev/shm", "mode=1777") == 0,
			  "dev shm tmpfs mounted for kselftest");
	orlix_test_result(mount_tmpfs_at("/run", "mode=0755") == 0,
			  "run tmpfs mounted for kselftest");
	orlix_test_result(mount_tmpfs_at("/tmp", "mode=1777") == 0,
			  "tmpfs mounted at /tmp for kselftest");
	orlix_test_result(have_list && test_count > 0,
			  "installed Orlix kselftest list is readable");
	if (have_list)
		run_orlix_tests(test_list, list_size);
	orlix_write_all("ORLIX-KSELFTEST-END\n");
	park_init();
}
