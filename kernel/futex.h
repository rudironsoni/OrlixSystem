#ifndef KERNEL_FUTEX_H
#define KERNEL_FUTEX_H

#ifdef __cplusplus
extern "C" {
#endif

int futex_wait_impl(int *uaddr, int expected, int timeout_ms);
int futex_wake_impl(int *uaddr, int max_wake);
int futex_op_impl(int *uaddr, int futex_op, int val, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
