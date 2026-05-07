/* IXLandKernel/kernel/seccomp.c
 * Virtual seccomp-like syscall policy ownership.
 */

#include "task.h"
#include "seccomp.h"

#include <errno.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define SECCOMP_RULE_MAX 32

struct seccomp_rule {
    bool active;
    int64_t syscall_nr;
    int err;
};

struct seccomp {
    atomic_int refs;
    kernel_mutex_t lock;
    struct seccomp_rule rules[SECCOMP_RULE_MAX];
};

struct seccomp *seccomp_alloc(void) {
    struct seccomp *policy = calloc(1, sizeof(*policy));

    if (!policy) {
        errno = ENOMEM;
        return NULL;
    }
    atomic_init(&policy->refs, 1);
    kernel_mutex_init(&policy->lock);
    return policy;
}

struct seccomp *seccomp_get(struct seccomp *policy) {
    if (policy) {
        atomic_fetch_add(&policy->refs, 1);
    }
    return policy;
}

void seccomp_put(struct seccomp *policy) {
    if (!policy) {
        return;
    }
    if (atomic_fetch_sub(&policy->refs, 1) == 1) {
        kernel_mutex_destroy(&policy->lock);
        free(policy);
    }
}

static int seccomp_set_errno_rule(struct seccomp *policy, int64_t syscall_nr, int err) {
    int free_slot = -1;

    if (!policy || syscall_nr < 0 || err <= 0) {
        return -EINVAL;
    }

    kernel_mutex_lock(&policy->lock);
    for (size_t i = 0; i < SECCOMP_RULE_MAX; i++) {
        if (policy->rules[i].active && policy->rules[i].syscall_nr == syscall_nr) {
            policy->rules[i].err = err;
            kernel_mutex_unlock(&policy->lock);
            return 0;
        }
        if (!policy->rules[i].active && free_slot < 0) {
            free_slot = (int)i;
        }
    }
    if (free_slot < 0) {
        kernel_mutex_unlock(&policy->lock);
        return -ENOSPC;
    }
    policy->rules[free_slot].active = true;
    policy->rules[free_slot].syscall_nr = syscall_nr;
    policy->rules[free_slot].err = err;
    kernel_mutex_unlock(&policy->lock);
    return 0;
}

static int seccomp_attach_policy(struct task_struct *task, struct seccomp *policy) {
    struct seccomp *old;

    if (!task || !policy) {
        return -EINVAL;
    }
    old = task->seccomp;
    task->seccomp = seccomp_get(policy);
    seccomp_put(old);
    return 0;
}

int seccomp_set_task_errno_policy(struct task_struct *task, int64_t syscall_nr, int err) {
    struct seccomp *policy;
    int ret;

    if (!task) {
        return -ESRCH;
    }
    policy = task->seccomp;
    if (!policy) {
        policy = seccomp_alloc();
        if (!policy) {
            return -ENOMEM;
        }
        task->seccomp = policy;
    }
    ret = seccomp_set_errno_rule(policy, syscall_nr, err);
    return ret;
}

int seccomp_set_thread_group_errno_policy(struct task_struct *task, int64_t syscall_nr, int err) {
    struct seccomp *policy;
    int ret;

    if (!task) {
        return -ESRCH;
    }
    if (!task->seccomp) {
        task->seccomp = seccomp_alloc();
        if (!task->seccomp) {
            return -ENOMEM;
        }
    }
    ret = seccomp_set_errno_rule(task->seccomp, syscall_nr, err);
    if (ret != 0) {
        return ret;
    }
    policy = task->seccomp;

    kernel_mutex_lock(&task_table_lock);
    for (int i = 0; i < TASK_MAX_TASKS; i++) {
        for (struct task_struct *candidate = task_table[i]; candidate; candidate = candidate->hash_next) {
            if (candidate->tgid == task->tgid) {
                seccomp_attach_policy(candidate, policy);
            }
        }
    }
    kernel_mutex_unlock(&task_table_lock);
    return 0;
}

void seccomp_clear_task_policy(struct task_struct *task) {
    struct seccomp *old;

    if (!task) {
        return;
    }
    old = task->seccomp;
    task->seccomp = NULL;
    seccomp_put(old);
}

long seccomp_check_current_syscall(int64_t syscall_nr) {
    struct task_struct *task = get_current();
    struct seccomp *policy;
    long ret = 0;

    if (!task || !task->seccomp) {
        return 0;
    }
    policy = task->seccomp;
    kernel_mutex_lock(&policy->lock);
    for (size_t i = 0; i < SECCOMP_RULE_MAX; i++) {
        if (policy->rules[i].active && policy->rules[i].syscall_nr == syscall_nr) {
            ret = -(long)policy->rules[i].err;
            break;
        }
    }
    kernel_mutex_unlock(&policy->lock);
    return ret;
}
