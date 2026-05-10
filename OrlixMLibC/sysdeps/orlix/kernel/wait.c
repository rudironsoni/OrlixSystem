#include <errno.h>

#include <linux/types.h>
#include <asm/posix_types.h>
#include <asm-generic/siginfo.h>

extern __kernel_pid_t wait_impl(int *wstatus);
extern __kernel_pid_t waitpid_impl(__kernel_pid_t pid, int *wstatus, int options);
extern __kernel_pid_t wait4_impl(__kernel_pid_t pid, int *wstatus, int options, void *rusage);
extern int waitid_impl(int idtype, __kernel_pid_t id, void *infop, int options, void *rusage);

static __kernel_pid_t wrap_pid_result(__kernel_pid_t ret) {
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

__attribute__((visibility("default"))) __kernel_pid_t waitpid(__kernel_pid_t pid, int *wstatus, int options) {
    return wrap_pid_result(waitpid_impl(pid, wstatus, options));
}

__attribute__((visibility("default"))) __kernel_pid_t wait4(__kernel_pid_t pid, int *wstatus, int options,
                                                            void *rusage) {
    return wrap_pid_result(wait4_impl(pid, wstatus, options, rusage));
}

__attribute__((visibility("default"))) __kernel_pid_t wait(int *wstatus) {
    return wrap_pid_result(wait_impl(wstatus));
}

__attribute__((visibility("default"))) __kernel_pid_t wait3(int *wstatus, int options, void *rusage) {
    return wrap_pid_result(wait4_impl(-1, wstatus, options, rusage));
}

__attribute__((visibility("default"))) int waitid(int idtype, __kernel_pid_t id, siginfo_t *infop,
                                                  int options) {
    return wrap_int_result(waitid_impl(idtype, id, infop, options, 0));
}
