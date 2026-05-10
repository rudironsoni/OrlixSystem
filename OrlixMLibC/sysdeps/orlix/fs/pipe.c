#include <errno.h>

extern int pipe_impl(int pipefd[2]);
extern int pipe2_impl(int pipefd[2], int flags);

static int wrap_int_result(int ret) {
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}

__attribute__((visibility("default"))) int pipe(int pipefd[2]) {
    return wrap_int_result(pipe_impl(pipefd));
}

__attribute__((visibility("default"))) int pipe2(int pipefd[2], int flags) {
    return wrap_int_result(pipe2_impl(pipefd, flags));
}
