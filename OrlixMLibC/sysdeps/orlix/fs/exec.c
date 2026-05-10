#include <crt_externs.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs/vfs.h"

#define environ (*_NSGetEnviron())

extern int execve_impl(const char *pathname, char *const argv[], char *const envp[]);
extern int fexecve_impl(int fd, char *const argv[], char *const envp[]);

static int wrap_int_result(int ret) {
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}

__attribute__((visibility("default"))) int execve(const char *pathname, char *const argv[],
                                                  char *const envp[]) {
    return wrap_int_result(execve_impl(pathname, argv, envp));
}

__attribute__((visibility("default"))) int execv(const char *pathname, char *const argv[]) {
    return execve(pathname, argv, environ);
}

__attribute__((visibility("default"))) int execvp(const char *file, char *const argv[]) {
    const char *path_env;
    char *path_copy;
    char *saveptr;
    char *dir;

    if (strchr(file, '/') != NULL) {
        return execv(file, argv);
    }

    path_env = getenv("PATH");
    if (!path_env) {
        path_env = "/usr/bin:/bin";
    }

    path_copy = strdup(path_env);
    if (!path_copy) {
        return -1;
    }

    saveptr = NULL;
    dir = strtok_r(path_copy, ":", &saveptr);
    while (dir) {
        char fullpath[MAX_PATH];
        int len = snprintf(fullpath, sizeof(fullpath), "%s/%s", dir, file);

        if (len > 0 && (size_t)len < sizeof(fullpath)) {
            int result = execv(fullpath, argv);

            if (result != -1) {
                free(path_copy);
                return result;
            }
            if (errno != ENOENT && errno != ENOTDIR) {
                free(path_copy);
                return -1;
            }
        }

        dir = strtok_r(NULL, ":", &saveptr);
    }

    free(path_copy);
    errno = ENOENT;
    return -1;
}

__attribute__((visibility("default"))) int fexecve(int fd, char *const argv[],
                                                   char *const envp[]) {
    return wrap_int_result(fexecve_impl(fd, argv, envp));
}
