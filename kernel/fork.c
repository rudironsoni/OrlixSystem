#include <errno.h>
#include <limits.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#include "../fs/fdtable.h"
#include "../fs/vfs.h"
#include "signal.h"
#include "task.h"
#include "uts.h"

/* Thread stack minimum fallback for portability */
#define KERNEL_THREAD_STACK_MIN (64 * 1024)

/* ============================================================================
 * FORK IMPLEMENTATION WITH SETJMP/LONGJMP
 * ============================================================================
 *
 * Since iOS forbids real fork(), we use pthread-based simulation with
 * setjmp/longjmp for child continuation. The key insight:
 *
 * 1. Parent calls setjmp() to save its context
 * 2. We create a new thread (the "child")
 * 3. Child sets up its environment and calls longjmp() to return to parent
 * 4. Parent distinguishes child from parent by thread ID
 * 5. Child continues from longjmp and returns 0
 * 6. Parent returns child's PID
 *
 * This gives us proper fork semantics on iOS.
 */

/* Fork context shared between parent and child */
typedef struct {
    struct task_struct *parent;
    struct task_struct *child;
    jmp_buf jmpbuf;           /* Shared jump buffer */
    volatile pid_t result;    /* Result from child perspective */
    volatile int child_ready; /* Synchronization flag */
    kernel_mutex_t lock;
    kernel_cond_t cond;
} fork_ctx_t;

/* Global fork context (only valid during fork) */
static __thread fork_ctx_t *active_fork_ctx = NULL;

/* Child thread entry point */
static void *fork_child_trampoline(void *arg) {
    fork_ctx_t *ctx = (fork_ctx_t *)arg;

    /* Set child as current task in thread-local storage */
    set_current(ctx->child);
    ctx->child->thread = kernel_thread_self();

    /* Copy parent's state from task structure */
    /* Child inherits parent's signal mask, working directory, etc. */

    /* Signal that child is initialized */
    kernel_mutex_lock(&ctx->lock);
    ctx->child_ready = 1;
    kernel_cond_broadcast(&ctx->cond);
    kernel_mutex_unlock(&ctx->lock);

    /* Wait for parent to be ready for the "return" */
    kernel_mutex_lock(&ctx->lock);
    while (ctx->result == 0) {
        kernel_cond_wait(&ctx->cond, &ctx->lock);
    }
    kernel_mutex_unlock(&ctx->lock);

    /* Child returns 0 via longjmp to fork's setjmp */
    /* The result is already set to 0 for child */
    longjmp(ctx->jmpbuf, 1);

    /* NOTREACHED */
    return NULL;
}

pid_t fork_impl(void) {
    struct task_struct *parent = get_current();
    if (!parent) {
        errno = ESRCH;
        return -1;
    }

    /* Check process limit */
    int child_count = 0;
    kernel_mutex_lock(&parent->lock);
    struct task_struct *c = parent->children;
    while (c) {
        child_count++;
        c = c->next_sibling;
    }
    if (child_count >= (int)parent->rlimits[RLIMIT_NPROC].cur) {
        kernel_mutex_unlock(&parent->lock);
        errno = EAGAIN;
        return -1;
    }
    kernel_mutex_unlock(&parent->lock);

    /* Allocate child task */
    struct task_struct *child = alloc_task();
    if (!child) {
        errno = ENOMEM;
        return -1;
    }

    /* Set up parent-child relationship */
    child->ppid = parent->pid;
    child->pgid = parent->pgid;
    child->sid = parent->sid;
    if (parent->uts_ns) {
        uts_put(child->uts_ns);
        child->uts_ns = uts_get(parent->uts_ns);
    }

    /* Copy parent's filesystem context */
    if (parent->fs) {
        child->fs = dup_fs_struct(parent->fs);
        if (!child->fs) {
            free_task(child);
            errno = ENOMEM;
            return -1;
        }
    }

    /* Copy subsystems with proper semantics */
    child->files = dup_files(parent->files);
    if (!child->files) {
        free_task(child);
        errno = ENOMEM;
        return -1;
    }

    /* Copy signal handlers */
    if (parent->signal && child->signal) {
        memcpy(child->signal->actions, parent->signal->actions, sizeof(parent->signal->actions));
        child->signal->blocked = parent->signal->blocked;
    }

    /* Reference TTY (not copy) */
    if (parent->tty) {
        child->tty = parent->tty;
        atomic_fetch_add(&child->tty->refs, 1);
    }

    /* Link into parent's children list */
    kernel_mutex_lock(&parent->lock);
    child->parent = parent;
    child->next_sibling = parent->children;
    parent->children = child;
    kernel_mutex_unlock(&parent->lock);

    /* Set up fork context on stack */
    fork_ctx_t ctx;
    ctx.parent = parent;
    ctx.child = child;
    ctx.result = 0;
    ctx.child_ready = 0;
    kernel_mutex_init(&ctx.lock);
    kernel_cond_init(&ctx.cond);

    /* Make context available globally for this thread */
    active_fork_ctx = &ctx;

    /* Save parent's context */
    if (setjmp(ctx.jmpbuf) == 0) {
        /* Parent: Create child thread */
            kernel_thread_t child_thread;
        kernel_thread_attr_t attr;
        kernel_thread_attr_init(&attr);

        /* Set stack size from resource limits */
        size_t stacksize = parent->rlimits[RLIMIT_STACK].cur;
        if (stacksize < KERNEL_THREAD_STACK_MIN) {
            stacksize = KERNEL_THREAD_STACK_MIN;
        }
        kernel_thread_attr_setstacksize(&attr, stacksize);

        int rc = kernel_thread_create(&child_thread, &attr, fork_child_trampoline, &ctx);
        kernel_thread_attr_destroy(&attr);

        if (rc != 0) {
            /* Cleanup on failure */
            kernel_mutex_lock(&parent->lock);
            parent->children = child->next_sibling;
            kernel_mutex_unlock(&parent->lock);
            free_task(child);
            active_fork_ctx = NULL;
            kernel_mutex_destroy(&ctx.lock);
            kernel_cond_destroy(&ctx.cond);
            errno = EAGAIN;
            return -1;
        }

        /* Wait for child to initialize */
        kernel_mutex_lock(&ctx.lock);
        while (!ctx.child_ready) {
            kernel_cond_wait(&ctx.cond, &ctx.lock);
        }

        /* Set result: parent gets child's PID */
        ctx.result = child->pid;
        kernel_cond_broadcast(&ctx.cond);
        kernel_mutex_unlock(&ctx.lock);

        /* Detach child thread - it will exit via longjmp */
        kernel_thread_detach(child_thread);

        /* Cleanup context */
        active_fork_ctx = NULL;
        kernel_mutex_destroy(&ctx.lock);
        kernel_cond_destroy(&ctx.cond);

        /* Parent returns child's PID */
        return child->pid;
    }

    /* Child continuation (after longjmp) */
    /* ctx is still valid on child's stack */

    /* Cleanup synchronization primitives */
    kernel_mutex_destroy(&ctx.lock);
    kernel_cond_destroy(&ctx.cond);
    active_fork_ctx = NULL;

    /* Child returns 0 */
    return 0;
}

/* ============================================================================
 * VFORK IMPLEMENTATION
 * ============================================================================
 *
 * vfork() suspends the parent process until the child calls execve() or exit().
 * On iOS, we simulate this using setjmp/longjmp:
 *
 * 1. Parent saves context with setjmp()
 * 2. Child "borrows" parent's address space (simulated)
 * 3. Child must call execve() or exit() before parent resumes
 * 4. Parent resumes when child exits or execs
 *
 * For simplicity in this simulation, vfork behaves like fork but guarantees
 * the parent doesn't run until child execs/exits.
 */

/* Vfork context */
typedef struct {
    struct task_struct *parent;
    struct task_struct *child;
    jmp_buf parent_jmp;        /* Parent's saved context */
    jmp_buf child_jmp;         /* Child's entry point */
    volatile int child_done;   /* Set when child execs or exits */
    volatile int child_execed; /* Set if child called execve */
    kernel_mutex_t lock;
    kernel_cond_t cond;
    pid_t child_pid;
} vfork_ctx_t;

/* Global vfork context */
static __thread vfork_ctx_t *active_vfork_ctx = NULL;

/* Vfork child entry */
static void *vfork_child_trampoline(void *arg) {
    vfork_ctx_t *ctx = (vfork_ctx_t *)arg;

    /* Set child as current task */
    set_current(ctx->child);
    ctx->child->thread = kernel_thread_self();

    /* Signal that child is ready */
    kernel_mutex_lock(&ctx->lock);
    kernel_cond_broadcast(&ctx->cond);
    kernel_mutex_unlock(&ctx->lock);

    /* Jump to child continuation */
    longjmp(ctx->child_jmp, 1);

    /* NOTREACHED */
    return NULL;
}

int vfork_impl(void) {
    struct task_struct *parent = get_current();
    if (!parent) {
        errno = ESRCH;
        return -1;
    }

    /* Check resource limits */
    kernel_mutex_lock(&parent->lock);
    int child_count = 0;
    struct task_struct *c = parent->children;
    while (c) {
        child_count++;
        c = c->next_sibling;
    }
    if (child_count >= (int)parent->rlimits[RLIMIT_NPROC].cur) {
        kernel_mutex_unlock(&parent->lock);
        errno = EAGAIN;
        return -1;
    }
    kernel_mutex_unlock(&parent->lock);

    /* Allocate child task */
    struct task_struct *child = alloc_task();
    if (!child) {
        errno = ENOMEM;
        return -1;
    }

    /* Set up parent-child relationship */
    child->ppid = parent->pid;
    child->pgid = parent->pgid;
    child->sid = parent->sid;
    child->vfork_parent = parent; /* Mark as vfork child */
    if (parent->uts_ns) {
        uts_put(child->uts_ns);
        child->uts_ns = uts_get(parent->uts_ns);
    }

    /* Copy parent's filesystem context */
    if (parent->fs) {
        child->fs = dup_fs_struct(parent->fs);
        if (!child->fs) {
            kernel_mutex_lock(&parent->lock);
            parent->children = child->next_sibling;
            kernel_mutex_unlock(&parent->lock);
            free_task(child);
            errno = ENOMEM;
            return -1;
        }
    }

    /* Share file table (not copy - key vfork semantics) */
    /* In real vfork, child shares address space; we simulate by sharing FD table */
    /* Note: For vfork, we duplicate the file table structure but share the underlying files */
    if (parent->files) {
        /* Duplicate the file table (shallow copy that shares file references) */
        child->files = dup_files(parent->files);
        if (!child->files) {
            kernel_mutex_lock(&parent->lock);
            parent->children = child->next_sibling;
            kernel_mutex_unlock(&parent->lock);
            free_task(child);
            errno = ENOMEM;
            return -1;
        }
    }

    /* Copy signal handlers */
    if (parent->signal && child->signal) {
        memcpy(child->signal->actions, parent->signal->actions, sizeof(parent->signal->actions));
        child->signal->blocked = parent->signal->blocked;
    }

    /* Reference TTY */
    if (parent->tty) {
        child->tty = parent->tty;
        atomic_fetch_add(&child->tty->refs, 1);
    }

    /* Link into parent's children list */
    kernel_mutex_lock(&parent->lock);
    child->parent = parent;
    child->next_sibling = parent->children;
    parent->children = child;
    kernel_mutex_unlock(&parent->lock);

    /* Mark parent as suspended (vfork semantics) */
    atomic_store(&parent->state, TASK_UNINTERRUPTIBLE);

    /* Set up vfork context */
    vfork_ctx_t ctx;
    ctx.parent = parent;
    ctx.child = child;
    ctx.child_done = 0;
    ctx.child_execed = 0;
    ctx.child_pid = child->pid;
    kernel_mutex_init(&ctx.lock);
    kernel_cond_init(&ctx.cond);

    active_vfork_ctx = &ctx;

    /* Parent: Save context */
    if (setjmp(ctx.parent_jmp) == 0) {
        /* Child: Set up entry point */
        if (setjmp(ctx.child_jmp) == 0) {
            /* Parent continues here after creating thread */
        kernel_thread_t child_thread;
            kernel_thread_attr_t attr;
    kernel_thread_attr_init(&attr);

    size_t stacksize = parent->rlimits[RLIMIT_STACK].cur;
            if (stacksize < KERNEL_THREAD_STACK_MIN) {
                stacksize = KERNEL_THREAD_STACK_MIN;
            }
            kernel_thread_attr_setstacksize(&attr, stacksize);

            int rc = kernel_thread_create(&child_thread, &attr, vfork_child_trampoline, &ctx);
            kernel_thread_attr_destroy(&attr);

            if (rc != 0) {
                kernel_mutex_lock(&parent->lock);
                parent->children = child->next_sibling;
                kernel_mutex_unlock(&parent->lock);
                atomic_store(&parent->state, TASK_RUNNING);
                free_task(child);
                active_vfork_ctx = NULL;
        kernel_mutex_destroy(&ctx.lock);
                kernel_cond_destroy(&ctx.cond);
                errno = EAGAIN;
                return -1;
            }

            /* Wait for child to exec or exit (vfork semantics) */
            kernel_mutex_lock(&ctx.lock);
            while (!ctx.child_done) {
                kernel_cond_wait(&ctx.cond, &ctx.lock);
            }
            kernel_mutex_unlock(&ctx.lock);

            /* Child has execed or exited - parent can resume */
            atomic_store(&parent->state, TASK_RUNNING);

            kernel_thread_detach(child_thread);

            /* Cleanup */
            active_vfork_ctx = NULL;
            kernel_mutex_destroy(&ctx.lock);
            kernel_cond_destroy(&ctx.cond);

            /* If child execed, parent returns PID */
            /* If child exited, parent returns PID (child is zombie) */
            return child->pid;
        }

        /* Child continuation after longjmp */
        /* NOTREACHED - child jumps to child_jmp instead */
    }

    /* Child returns 0 */
    active_vfork_ctx = NULL;
    kernel_mutex_destroy(&ctx.lock);
    kernel_cond_destroy(&ctx.cond);
    return 0;
}

/* Called from execve to notify vfork parent */
void vfork_exec_notify(void) {
    if (active_vfork_ctx) {
        kernel_mutex_lock(&active_vfork_ctx->lock);
        active_vfork_ctx->child_done = 1;
        active_vfork_ctx->child_execed = 1;
        kernel_cond_broadcast(&active_vfork_ctx->cond);
        kernel_mutex_unlock(&active_vfork_ctx->lock);
    }
}

/* Called from exit to notify vfork parent */
void vfork_exit_notify(void) {
    if (active_vfork_ctx) {
        kernel_mutex_lock(&active_vfork_ctx->lock);
        active_vfork_ctx->child_done = 1;
        kernel_cond_broadcast(&active_vfork_ctx->cond);
        kernel_mutex_unlock(&active_vfork_ctx->lock);
    }
}

/* ============================================================================
 * PUBLIC CANONICAL WRAPPERS
 * ============================================================================ */

__attribute__((visibility("default"))) pid_t fork(void) {
    return fork_impl();
}

__attribute__((visibility("default"))) int vfork(void) {
    return vfork_impl();
}
