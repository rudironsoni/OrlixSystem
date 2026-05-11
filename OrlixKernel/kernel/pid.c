#include <linux/errno.h>
#include <linux/atomic.h>
#include <linux/types.h>

#include "task.h"

#define PID_MIN 1
#define PID_MAX 65535
#define PID_COUNT (PID_MAX - PID_MIN + 1)

/* Free list stack for O(1) PID allocation/reuse */
static __kernel_pid_t pid_free_stack[PID_COUNT];
static atomic_t pid_stack_top = ATOMIC_INIT(0);
static kernel_mutex_t pid_lock = KERNEL_MUTEX_INITIALIZER;
static atomic_t pid_initialized = ATOMIC_INIT(0);

/* Private implementation - matches _impl() suffix convention */
static int32_t pid_alloc_impl(void) {
    /* Ensure initialized (thread-safe via atomic flag) */
    if (atomic_read(&pid_initialized) == 0) {
        pid_init();
    }

    kernel_mutex_lock(&pid_lock);
    int top = atomic_read(&pid_stack_top);
    if (top <= 0) {
        kernel_mutex_unlock(&pid_lock);
        return -ENOSPC;
    }

    top--;
    __kernel_pid_t pid = pid_free_stack[top];
    atomic_set(&pid_stack_top, top);
    kernel_mutex_unlock(&pid_lock);

    return (int32_t)pid;
}

static void pid_free_impl(int32_t pid) {
    /* Validate PID range */
    if (pid < PID_MIN || pid > PID_MAX) {
        return;
    }

    /* Ensure initialized */
    if (atomic_read(&pid_initialized) == 0) {
        pid_init();
    }

    kernel_mutex_lock(&pid_lock);
    int top = atomic_read(&pid_stack_top);

    /* Defensive: check for stack overflow (shouldn't happen with correct usage) */
    if (top >= PID_COUNT) {
        kernel_mutex_unlock(&pid_lock);
        return;
    }

    pid_free_stack[top] = pid;
    atomic_set(&pid_stack_top, top + 1);
    kernel_mutex_unlock(&pid_lock);
}

/**
 * @brief Initialize the PID allocator with all available PIDs.
 *
 * Populates the free stack with PIDs from PID_MAX down to PID_MIN
 * so that sequential allocation starts from PID_MIN.
 */
void pid_init(void) {
    if (atomic_read(&pid_initialized) != 0) {
        return;
    }

    kernel_mutex_lock(&pid_lock);
    if (atomic_read(&pid_initialized) != 0) {
        kernel_mutex_unlock(&pid_lock);
        return;
    }

    /* Push PIDs in reverse order so PID_MIN is popped first */
    int idx = 0;
    for (__kernel_pid_t pid = PID_MAX; pid >= PID_MIN; pid--) {
        pid_free_stack[idx++] = pid;
    }
    atomic_set(&pid_stack_top, PID_COUNT);
    atomic_set(&pid_initialized, 1);

    kernel_mutex_unlock(&pid_lock);
}

/* Public wrappers declared in task.h */
int32_t alloc_pid(void) {
    return pid_alloc_impl();
}

int pid_reserve(int32_t pid) {
    if (pid < PID_MIN || pid > PID_MAX) {
        return -EINVAL;
    }

    if (atomic_read(&pid_initialized) == 0) {
        pid_init();
    }

    kernel_mutex_lock(&pid_lock);
    int top = atomic_read(&pid_stack_top);
    for (int i = 0; i < top; i++) {
        if (pid_free_stack[i] == pid) {
            pid_free_stack[i] = pid_free_stack[top - 1];
            atomic_set(&pid_stack_top, top - 1);
            kernel_mutex_unlock(&pid_lock);
            return 0;
        }
    }
    kernel_mutex_unlock(&pid_lock);

    return -EEXIST;
}

void free_pid(int32_t pid) {
    pid_free_impl(pid);
}
