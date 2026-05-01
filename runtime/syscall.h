#ifndef RUNTIME_SYSCALL_H
#define RUNTIME_SYSCALL_H

#ifdef __cplusplus
extern "C" {
#endif

long syscall_dispatch_impl(long number,
                           long arg0,
                           long arg1,
                           long arg2,
                           long arg3,
                           long arg4,
                           long arg5);

#ifdef __cplusplus
}
#endif

#endif
