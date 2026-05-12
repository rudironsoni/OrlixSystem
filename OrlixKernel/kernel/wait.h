#ifndef KERNEL_WAIT_H
#define KERNEL_WAIT_H

#include <linux/types.h>
#include <uapi/asm/posix_types.h>

#ifdef __cplusplus
extern "C" {
#endif

__kernel_pid_t wait_impl(int *wstatus);
__kernel_pid_t waitpid_impl(__kernel_pid_t pid, int *wstatus, int options);
__kernel_pid_t wait4_impl(__kernel_pid_t pid, int *wstatus, int options, void *rusage);
int waitid_impl(int idtype, __kernel_pid_t id, void *infop, int options, void *rusage);

#ifdef __cplusplus
}
#endif

#endif
