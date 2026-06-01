#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct getconf_name {
	const char *name;
	long value;
};

static const struct getconf_name sysconf_names[] = {
#ifdef _SC_ARG_MAX
	{"ARG_MAX", _SC_ARG_MAX},
#endif
#ifdef _SC_CHILD_MAX
	{"CHILD_MAX", _SC_CHILD_MAX},
#endif
#ifdef _SC_CLK_TCK
	{"CLK_TCK", _SC_CLK_TCK},
#endif
#ifdef _SC_NGROUPS_MAX
	{"NGROUPS_MAX", _SC_NGROUPS_MAX},
#endif
#ifdef _SC_OPEN_MAX
	{"OPEN_MAX", _SC_OPEN_MAX},
#endif
#ifdef _SC_STREAM_MAX
	{"STREAM_MAX", _SC_STREAM_MAX},
#endif
#ifdef _SC_TZNAME_MAX
	{"TZNAME_MAX", _SC_TZNAME_MAX},
#endif
#ifdef _SC_JOB_CONTROL
	{"_POSIX_JOB_CONTROL", _SC_JOB_CONTROL},
#endif
#ifdef _SC_SAVED_IDS
	{"_POSIX_SAVED_IDS", _SC_SAVED_IDS},
#endif
#ifdef _SC_VERSION
	{"_POSIX_VERSION", _SC_VERSION},
#endif
#ifdef _SC_PAGE_SIZE
	{"PAGE_SIZE", _SC_PAGE_SIZE},
	{"PAGESIZE", _SC_PAGE_SIZE},
#endif
#ifdef _SC_2_VERSION
	{"POSIX2_VERSION", _SC_2_VERSION},
	{"_POSIX2_VERSION", _SC_2_VERSION},
#endif
#ifdef _SC_LINE_MAX
	{"LINE_MAX", _SC_LINE_MAX},
#endif
#ifdef _SC_RE_DUP_MAX
	{"RE_DUP_MAX", _SC_RE_DUP_MAX},
#endif
#ifdef _SC_IOV_MAX
	{"IOV_MAX", _SC_IOV_MAX},
#endif
#ifdef _SC_THREADS
	{"_POSIX_THREADS", _SC_THREADS},
#endif
#ifdef _SC_THREAD_SAFE_FUNCTIONS
	{"_POSIX_THREAD_SAFE_FUNCTIONS", _SC_THREAD_SAFE_FUNCTIONS},
#endif
#ifdef _SC_GETGR_R_SIZE_MAX
	{"GETGR_R_SIZE_MAX", _SC_GETGR_R_SIZE_MAX},
#endif
#ifdef _SC_GETPW_R_SIZE_MAX
	{"GETPW_R_SIZE_MAX", _SC_GETPW_R_SIZE_MAX},
#endif
#ifdef _SC_LOGIN_NAME_MAX
	{"LOGIN_NAME_MAX", _SC_LOGIN_NAME_MAX},
#endif
#ifdef _SC_TTY_NAME_MAX
	{"TTY_NAME_MAX", _SC_TTY_NAME_MAX},
#endif
#ifdef _SC_THREAD_DESTRUCTOR_ITERATIONS
	{"PTHREAD_DESTRUCTOR_ITERATIONS", _SC_THREAD_DESTRUCTOR_ITERATIONS},
#endif
#ifdef _SC_THREAD_KEYS_MAX
	{"PTHREAD_KEYS_MAX", _SC_THREAD_KEYS_MAX},
#endif
#ifdef _SC_THREAD_STACK_MIN
	{"PTHREAD_STACK_MIN", _SC_THREAD_STACK_MIN},
#endif
#ifdef _SC_NPROCESSORS_CONF
	{"_NPROCESSORS_CONF", _SC_NPROCESSORS_CONF},
#endif
#ifdef _SC_NPROCESSORS_ONLN
	{"_NPROCESSORS_ONLN", _SC_NPROCESSORS_ONLN},
#endif
#ifdef _SC_PHYS_PAGES
	{"_PHYS_PAGES", _SC_PHYS_PAGES},
#endif
#ifdef _SC_AVPHYS_PAGES
	{"_AVPHYS_PAGES", _SC_AVPHYS_PAGES},
#endif
#ifdef _SC_ATEXIT_MAX
	{"ATEXIT_MAX", _SC_ATEXIT_MAX},
#endif
#ifdef _SC_PASS_MAX
	{"PASS_MAX", _SC_PASS_MAX},
#endif
#ifdef _SC_XOPEN_VERSION
	{"XOPEN_VERSION", _SC_XOPEN_VERSION},
	{"_XOPEN_VERSION", _SC_XOPEN_VERSION},
#endif
#ifdef _SC_HOST_NAME_MAX
	{"HOST_NAME_MAX", _SC_HOST_NAME_MAX},
#endif
};

static const struct getconf_name pathconf_names[] = {
#ifdef _PC_LINK_MAX
	{"LINK_MAX", _PC_LINK_MAX},
#endif
#ifdef _PC_MAX_CANON
	{"MAX_CANON", _PC_MAX_CANON},
#endif
#ifdef _PC_MAX_INPUT
	{"MAX_INPUT", _PC_MAX_INPUT},
#endif
#ifdef _PC_NAME_MAX
	{"NAME_MAX", _PC_NAME_MAX},
#endif
#ifdef _PC_PATH_MAX
	{"PATH_MAX", _PC_PATH_MAX},
#endif
#ifdef _PC_PIPE_BUF
	{"PIPE_BUF", _PC_PIPE_BUF},
#endif
#ifdef _PC_CHOWN_RESTRICTED
	{"_POSIX_CHOWN_RESTRICTED", _PC_CHOWN_RESTRICTED},
#endif
#ifdef _PC_NO_TRUNC
	{"_POSIX_NO_TRUNC", _PC_NO_TRUNC},
#endif
#ifdef _PC_VDISABLE
	{"_POSIX_VDISABLE", _PC_VDISABLE},
#endif
};

static const struct getconf_name confstr_names[] = {
#ifdef _CS_PATH
	{"PATH", _CS_PATH},
	{"CS_PATH", _CS_PATH},
	{"_CS_PATH", _CS_PATH},
#endif
#ifdef _CS_GNU_LIBC_VERSION
	{"GNU_LIBC_VERSION", _CS_GNU_LIBC_VERSION},
#endif
#ifdef _CS_GNU_LIBPTHREAD_VERSION
	{"GNU_LIBPTHREAD_VERSION", _CS_GNU_LIBPTHREAD_VERSION},
#endif
};

static void usage(void) {
	fprintf(stderr, "usage: getconf [-a] variable [pathname]\n");
}

static int print_long(long value) {
	errno = 0;
	long result = sysconf(value);
	if(result == -1) {
		if(errno) {
			perror("getconf: sysconf");
			return 1;
		}
		puts("undefined");
		return 0;
	}
	printf("%ld\n", result);
	return 0;
}

static int print_pathconf(long value, const char *path) {
	errno = 0;
	long result = pathconf(path, value);
	if(result == -1) {
		if(errno) {
			perror("getconf: pathconf");
			return 1;
		}
		puts("undefined");
		return 0;
	}
	printf("%ld\n", result);
	return 0;
}

static int print_confstr(long value) {
	errno = 0;
	size_t needed = confstr(value, NULL, 0);
	if(!needed) {
		if(errno) {
			perror("getconf: confstr");
			return 1;
		}
		puts("undefined");
		return 0;
	}

	char *buffer = malloc(needed);
	if(!buffer) {
		perror("getconf: malloc");
		return 1;
	}

	confstr(value, buffer, needed);
	puts(buffer);
	free(buffer);
	return 0;
}

static const struct getconf_name *find_name(const struct getconf_name *names, size_t count, const char *name) {
	for(size_t i = 0; i < count; i++) {
		if(!strcmp(names[i].name, name))
			return &names[i];
	}
	return NULL;
}

static int print_one(const char *name, const char *path) {
	const struct getconf_name *entry;

	entry = find_name(sysconf_names, sizeof(sysconf_names) / sizeof(sysconf_names[0]), name);
	if(entry)
		return print_long(entry->value);

	entry = find_name(confstr_names, sizeof(confstr_names) / sizeof(confstr_names[0]), name);
	if(entry) {
		if(path) {
			fprintf(stderr, "getconf: %s does not accept a pathname\n", name);
			return 2;
		}
		return print_confstr(entry->value);
	}

	entry = find_name(pathconf_names, sizeof(pathconf_names) / sizeof(pathconf_names[0]), name);
	if(entry)
		return print_pathconf(entry->value, path ? path : ".");

	fprintf(stderr, "getconf: Unrecognized variable `%s'\n", name);
	return 2;
}

static int print_all(void) {
	int status = 0;

	for(size_t i = 0; i < sizeof(sysconf_names) / sizeof(sysconf_names[0]); i++) {
		printf("%s = ", sysconf_names[i].name);
		status |= print_long(sysconf_names[i].value);
	}
	for(size_t i = 0; i < sizeof(confstr_names) / sizeof(confstr_names[0]); i++) {
		printf("%s = ", confstr_names[i].name);
		status |= print_confstr(confstr_names[i].value);
	}
	for(size_t i = 0; i < sizeof(pathconf_names) / sizeof(pathconf_names[0]); i++) {
		printf("%s = ", pathconf_names[i].name);
		status |= print_pathconf(pathconf_names[i].value, ".");
	}

	return status ? 1 : 0;
}

int main(int argc, char **argv) {
	if(argc == 2 && !strcmp(argv[1], "-a"))
		return print_all();

	if(argc == 2)
		return print_one(argv[1], NULL);

	if(argc == 3)
		return print_one(argv[1], argv[2]);

	usage();
	return 2;
}
