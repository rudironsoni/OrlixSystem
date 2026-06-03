// SPDX-License-Identifier: MIT

#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_group(const struct group *group) {
	printf("%s:%s:%lu:", group->gr_name, group->gr_passwd,
			(unsigned long)group->gr_gid);
	if (group->gr_mem) {
		for (char **member = group->gr_mem; *member; ++member) {
			if (member != group->gr_mem)
				putchar(',');
			fputs(*member, stdout);
		}
	}
	putchar('\n');
}

static void print_passwd(const struct passwd *passwd) {
	printf("%s:%s:%lu:%lu:%s:%s:%s\n", passwd->pw_name, passwd->pw_passwd,
			(unsigned long)passwd->pw_uid, (unsigned long)passwd->pw_gid,
			passwd->pw_gecos, passwd->pw_dir, passwd->pw_shell);
}

static int get_group(int argc, char **argv) {
	int status = 0;

	if (argc == 0) {
		setgrent();
		for (struct group *group; (group = getgrent());)
			print_group(group);
		endgrent();
		return 0;
	}

	for (int i = 0; i < argc; ++i) {
		errno = 0;
		char *end = NULL;
		unsigned long id = strtoul(argv[i], &end, 10);
		struct group *group = end && *end == '\0' ? getgrgid((gid_t)id)
		                                          : getgrnam(argv[i]);
		if (group)
			print_group(group);
		else
			status = 2;
	}

	return status;
}

static int get_passwd(int argc, char **argv) {
	int status = 0;

	if (argc == 0) {
		setpwent();
		for (struct passwd *passwd; (passwd = getpwent());)
			print_passwd(passwd);
		endpwent();
		return 0;
	}

	for (int i = 0; i < argc; ++i) {
		errno = 0;
		char *end = NULL;
		unsigned long id = strtoul(argv[i], &end, 10);
		struct passwd *passwd = end && *end == '\0' ? getpwuid((uid_t)id)
		                                            : getpwnam(argv[i]);
		if (passwd)
			print_passwd(passwd);
		else
			status = 2;
	}

	return status;
}

int main(int argc, char **argv) {
	if (argc < 2) {
		fprintf(stderr, "usage: getent DATABASE [KEY...]\n");
		return 1;
	}

	if (strcmp(argv[1], "group") == 0)
		return get_group(argc - 2, argv + 2);
	if (strcmp(argv[1], "passwd") == 0)
		return get_passwd(argc - 2, argv + 2);

	fprintf(stderr, "getent: unsupported database '%s'\n", argv[1]);
	return 1;
}
