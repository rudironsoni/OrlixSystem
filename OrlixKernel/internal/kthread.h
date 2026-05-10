#ifndef INTERNAL_KTHREAD_H
#define INTERNAL_KTHREAD_H

#include <linux/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kernel_thread {
    void *impl;
} kernel_thread_t;

typedef struct kernel_thread_attr {
    void *impl;
} kernel_thread_attr_t;

int kernel_thread_attr_init(kernel_thread_attr_t *attr);
int kernel_thread_attr_destroy(kernel_thread_attr_t *attr);
int kernel_thread_attr_setstacksize(kernel_thread_attr_t *attr, size_t stacksize);
int kernel_thread_create(kernel_thread_t *thread, const kernel_thread_attr_t *attr,
                         void *(*start_routine)(void *), void *arg);
int kernel_thread_detach(kernel_thread_t thread);
kernel_thread_t kernel_thread_self(void);
void kernel_thread_exit(void *value_ptr);

#ifdef __cplusplus
}
#endif

#endif
