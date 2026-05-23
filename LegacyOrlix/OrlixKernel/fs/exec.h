#ifndef FS_EXEC_H
#define FS_EXEC_H

#ifdef __cplusplus
extern "C" {
#endif

int execve_impl(const char *pathname, char *const argv[], char *const envp[]);
int fexecve_impl(int fd, char *const argv[], char *const envp[]);
int execveat_impl(int dirfd, const char *pathname, char *const argv[], char *const envp[],
                  int flags);

#ifdef __cplusplus
}
#endif

#endif
