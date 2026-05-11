#include <errno.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>

extern pid_t wait_impl(int *wstatus);
extern pid_t waitpid_impl(pid_t pid, int *wstatus, int options);
extern pid_t wait4_impl(pid_t pid, int *wstatus, int options, struct rusage *rusage);
extern int waitid_impl(idtype_t idtype, id_t id, siginfo_t *infop, int options, void *rusage);

static pid_t wrap_pid_result(pid_t ret) {
    if (ret < 0) {
        errno = (int)-ret;
        return -1;
    }
    return ret;
}

static int wrap_int_result(int ret) {
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}

__attribute__((visibility("default"))) pid_t waitpid(pid_t pid, int *wstatus, int options) {
    return wrap_pid_result(waitpid_impl(pid, wstatus, options));
}

__attribute__((visibility("default"))) pid_t wait4(pid_t pid, int *wstatus, int options,
                                                   struct rusage *rusage) {
    return wrap_pid_result(wait4_impl(pid, wstatus, options, rusage));
}

__attribute__((visibility("default"))) pid_t wait(int *wstatus) {
    return wrap_pid_result(wait_impl(wstatus));
}

__attribute__((visibility("default"))) pid_t wait3(int *wstatus, int options, struct rusage *rusage) {
    return wrap_pid_result(wait4_impl(-1, wstatus, options, rusage));
}

__attribute__((visibility("default"))) int waitid(idtype_t idtype, id_t id, siginfo_t *infop,
                                                  int options) {
    return wrap_int_result(waitid_impl(idtype, id, infop, options, 0));
}
