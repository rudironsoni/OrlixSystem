#ifndef RUNTIME_SYSCALL_H
#define RUNTIME_SYSCALL_H

#include <linux/signal.h>

#ifdef __cplusplus
extern "C" {
#endif

enum syscall_capability_class {
    SYSCALL_CAPABILITY_NONE = 0,
    SYSCALL_CAPABILITY_FD,
    SYSCALL_CAPABILITY_PROCESS,
    SYSCALL_CAPABILITY_SIGNAL,
    SYSCALL_CAPABILITY_VM,
    SYSCALL_CAPABILITY_NETWORK,
    SYSCALL_CAPABILITY_READINESS,
    SYSCALL_CAPABILITY_MOUNT,
    SYSCALL_CAPABILITY_XATTR,
    SYSCALL_CAPABILITY_TIME,
    SYSCALL_CAPABILITY_RESOURCE,
    SYSCALL_CAPABILITY_RANDOM,
};

enum syscall_gap_priority {
    SYSCALL_GAP_NONE = 0,
    SYSCALL_GAP_BOOT,
    SYSCALL_GAP_SHELL,
    SYSCALL_GAP_PACKAGE,
    SYSCALL_GAP_NETWORK,
};

enum syscall_matrix_override_class {
    SYSCALL_MATRIX_OVERRIDE_NONE = 0,
    SYSCALL_MATRIX_OVERRIDE_KERNEL_OWNED_NEXT_PROCESS,
};

long syscall_dispatch_impl(long number,
                           long arg0,
                           long arg1,
                           long arg2,
                           long arg3,
                           long arg4,
                           long arg5);
int syscall_is_implemented_impl(long number);
enum syscall_capability_class syscall_capability_class_impl(long number);
enum syscall_gap_priority syscall_gap_priority_impl(long number);
enum syscall_matrix_override_class syscall_matrix_override_class_impl(long number);

#ifdef __cplusplus
}
#endif

#endif
