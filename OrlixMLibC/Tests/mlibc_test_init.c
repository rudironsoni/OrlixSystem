/* SPDX-License-Identifier: MIT */

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static char test_list[32768];
static unsigned int test_index;
static unsigned int test_failures;
static char *const test_envp[] = {
	"LANG=en_US.utf8",
	"HOME=/root",
	"LOGNAME=root",
	"SHELL=/bin/sh",
	"USER=root",
	NULL,
};

static void park_init(void)
{
	for (;;)
		(void)pause();
}

static void test_result(bool passed, const char *name)
{
	test_index++;
	if (!passed)
		test_failures++;
	printf("%s %u - %s\n", passed ? "ok" : "not ok", test_index, name);
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

static bool parse_test_line(char *line, char **label, char **path)
{
	char *separator;

	if (line[0] == '\0' || line[0] == '#')
		return false;
	separator = strchr(line, ':');
	if (!separator)
		return false;
	*separator = '\0';
	*label = line;
	*path = separator + 1;
	return **label != '\0' && **path != '\0';
}

static unsigned int count_tests(char *data, size_t size)
{
	unsigned int count = 0;
	char *cursor = data;

	while (cursor < data + size) {
		char *line = cursor;
		char *newline = strchr(cursor, '\n');
		char *label;
		char *path;

		if (!newline)
			newline = data + size;
		*newline = '\0';
		if (parse_test_line(line, &label, &path))
			count++;
		cursor = newline + 1;
	}
	return count;
}

static int run_test(const char *label, const char *path)
{
	pid_t child;
	int status;

	printf("# exec %s (%s)\n", path, label);
	fflush(stdout);

	child = fork();
	if (child == 0) {
		char *const argv[] = { (char *)path, NULL };

		execve(path, argv, test_envp);
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

static void run_tests(char *data, size_t size)
{
	char *cursor = data;

	while (cursor < data + size) {
		char *line = cursor;
		char *newline = strchr(cursor, '\n');
		char *label;
		char *path;

		if (!newline)
			newline = data + size;
		*newline = '\0';
		if (parse_test_line(line, &label, &path))
			test_result(run_test(label, path) == 0, label);
		cursor = newline + 1;
	}
}

int main(void)
{
	size_t list_size = 0;
	unsigned int test_count = 0;
	bool have_list;

	puts("ORLIX-MLIBC-TEST-INIT");
	(void)mkdir("/proc", 0555);
	(void)mount("proc", "/proc", "proc", 0, NULL);

	have_list = read_file("/mlibc-test-list.txt", test_list,
			      sizeof(test_list), &list_size);
	if (have_list)
		test_count = count_tests(test_list, list_size);

	printf("TAP version 13\n1..%u\n", test_count + 1);
	test_result(have_list && test_count > 0,
		    "installed upstream mlibc test list is readable");
	if (have_list) {
		have_list = read_file("/mlibc-test-list.txt", test_list,
				      sizeof(test_list), &list_size);
		if (have_list)
			run_tests(test_list, list_size);
	}
	puts("ORLIX-MLIBC-TEST-END");
	fflush(stdout);

	if (getpid() == 1)
		park_init();
	return test_failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
