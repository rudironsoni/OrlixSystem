#ifndef PIPE_H
#define PIPE_H

#include <linux/types.h>
#include <linux/stddef.h>

#include "fdtable.h"

#ifdef __cplusplus
extern "C" {
#endif

int pipe_impl(int pipefd[2]);
int pipe2_impl(int pipefd[2], int flags);

#ifdef __cplusplus
}
#endif

#endif
