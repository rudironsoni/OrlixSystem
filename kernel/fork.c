#include <errno.h>
#include <limits.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#include <linux/fcntl.h>
#include <linux/sched.h>

#include "../fs/fdtable.h"
#include "../fs/vfs.h"
#include "cgroup.h"
#include "cred_internal.h"
#include "ptrace.h"
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

static unsigned long clone_namespace_flags(void) {
    return CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWPID | CLONE_NEWCGROUP | CLONE_NEWUSER;
}

static unsigned long clone_supported_flags(void) {
    return clone_namespace_flags() | CLONE_FS | CLONE_VM | CLONE_SIGHAND | CLONE_THREAD |
           CLONE_PARENT_SETTID | CLONE_CHILD_SETTID | CLONE_CHILD_CLEARTID | CLONE_PIDFD |
           CLONE_INTO_CGROUP;
}

static int validate_clone_namespace_flags(uint64_t flags) {
    uint64_t masked = flags & ~0xffULL;

    if ((masked & ~clone_supported_flags()) != 0) {
        errno = EINVAL;
        return -1;
    }
    if ((masked & CLONE_NEWNS) != 0 && (masked & CLONE_FS) != 0) {
        errno = EINVAL;
        return -1;
    }
    if ((masked & CLONE_SIGHAND) != 0 && (masked & CLONE_VM) == 0) {
        errno = EINVAL;
        return -1;
    }
    if ((masked & CLONE_THREAD) != 0 &&
        ((masked & CLONE_VM) == 0 || (masked & CLONE_SIGHAND) == 0)) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

static int task_apply_clone_namespace_flags(struct task_struct *task, uint64_t flags) {
    uint64_t masked = flags & ~0xffULL;
    struct uts_namespace *new_uts;

    if ((masked & CLONE_NEWUSER) != 0) {
        int ret = cred_unshare_user_namespace(task->cred);
        if (ret != 0) {
            errno = -ret;
            return -1;
        }
    }

    if ((masked & CLONE_NEWNS) != 0) {
        int ret;
        struct task_struct *saved;
        if (!task->fs) {
            errno = ESRCH;
            return -1;
        }
        saved = get_current();
        set_current(task);
        ret = fs_unshare_mount_namespace(task->fs);
        set_current(saved);
        if (ret < 0) {
            errno = -ret;
            return -1;
        }
    }

    if ((masked & CLONE_NEWUTS) != 0) {
        struct task_struct *saved = get_current();
        set_current(task);
        new_uts = uts_dup(task->uts_ns);
        set_current(saved);
        if (!new_uts) {
            errno = ENOMEM;
            return -1;
        }
        if (task->uts_ns) {
            uts_put(task->uts_ns);
        }
        task->uts_ns = new_uts;
    }

    if ((masked & CLONE_NEWPID) != 0) {
        task->pid_ns_level += 1;
        task->ns_pid = 1;
    }

    if ((masked & CLONE_NEWCGROUP) != 0) {
        if (task_unshare_cgroup_namespace(task) != 0) {
            errno = ENOMEM;
            return -1;
        }
    }

    return 0;
}

static int clone3_resolve_cgroup_target(int fd, char *cgroup_path, size_t cgroup_path_len) {
    fd_entry_t *entry;
    char virtual_path[MAX_PATH];
    synthetic_dir_class_t dir_class;
    enum cgroupfs_node_type type = CGROUPFS_NODE_NONE;
    int ret;
    int saved_errno;

    if (!cgroup_path || cgroup_path_len == 0) {
        errno = EINVAL;
        return -1;
    }

    entry = get_fd_entry_impl(fd);
    if (!entry) {
        errno = EBADF;
        return -1;
    }
    if (!get_fd_is_synthetic_dir_impl(entry)) {
        put_fd_entry_impl(entry);
        errno = ENOTDIR;
        return -1;
    }
    dir_class = get_fd_synthetic_dir_class_impl(entry);
    ret = get_fd_path_impl(entry, virtual_path, sizeof(virtual_path));
    saved_errno = errno;
    put_fd_entry_impl(entry);
    errno = saved_errno;
    if (ret != 0) {
        errno = ENOENT;
        return -1;
    }
    if (dir_class != SYNTHETIC_DIR_CGROUPFS) {
        errno = ENOTDIR;
        return -1;
    }
    if (cgroupfs_resolve_path(virtual_path, cgroup_path, cgroup_path_len, &type) != 0) {
        errno = ENOENT;
        return -1;
    }
    if (type != CGROUPFS_NODE_DIR) {
        errno = ENOTDIR;
        return -1;
    }
    return 0;
}

static void clone3_release_child_failure(struct task_struct *parent, struct task_struct *child) {
    if (!parent || !child) {
        return;
    }
    task_unlink_child_impl(parent, child);
    free_task(child);
    free_task(child);
}

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
    if (atomic_load(&parent->new_pid_namespace_pending)) {
        child->pid_ns_level = parent->pid_ns_level + 1;
        child->ns_pid = 1;
    } else {
        child->pid_ns_level = parent->pid_ns_level;
        child->ns_pid = child->pid;
    }
    if (parent->uts_ns) {
        uts_put(child->uts_ns);
        child->uts_ns = uts_get(parent->uts_ns);
    }
    if (parent->cred) {
        put_cred(child->cred);
        child->cred = dup_cred(parent->cred);
        if (!child->cred) {
            free_task(child);
            errno = ENOMEM;
            return -1;
        }
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
    ptrace_note_fork_event(parent, child->pid, 0);

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

int32_t clone_impl(uint64_t flags) {
    struct task_struct *parent = get_current();
    struct task_struct *child;

    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    if (validate_clone_namespace_flags(flags) != 0) {
        return -1;
    }

    child = task_create_child_with_flags_impl(parent, flags);
    if (!child) {
        return -1;
    }

    if ((flags & CLONE_FS) != 0 && parent->fs) {
        free_fs_struct(child->fs);
        child->fs = get_fs_struct(parent->fs);
        if (!child->fs) {
            task_unlink_child_impl(parent, child);
            free_task(child);
            errno = ENOMEM;
            return -1;
        }
    }

    if (task_apply_clone_namespace_flags(child, flags & ~(uint64_t)CLONE_NEWPID) != 0) {
        task_unlink_child_impl(parent, child);
        free_task(child);
        return -1;
    }

    return child->pid;
}

int32_t clone3_impl(const struct clone_args *args, size_t size) {
    int32_t child_pid;
    int pidfd = -1;
    int *pidfd_ptr = NULL;
    int *parent_tid = NULL;
    int *child_tid = NULL;
    struct task_struct *parent;
    struct task_struct *child;
    char cgroup_path[MAX_PATH];
    bool use_clone_into_cgroup = false;

    if (!args) {
        errno = EFAULT;
        return -1;
    }
    if (size < CLONE_ARGS_SIZE_VER0) {
        errno = EINVAL;
        return -1;
    }
    if (args->exit_signal > 0xff) {
        errno = EINVAL;
        return -1;
    }
    if (args->set_tid || args->set_tid_size) {
        errno = ENOSYS;
        return -1;
    }
    if ((args->flags & CLONE_PIDFD) != 0) {
        if (args->pidfd == 0) {
            errno = EFAULT;
            return -1;
        }
        pidfd_ptr = (int *)(uintptr_t)args->pidfd;
        if ((args->flags & CLONE_THREAD) != 0) {
            errno = EINVAL;
            return -1;
        }
    } else if (args->pidfd != 0) {
        errno = EINVAL;
        return -1;
    }
    if ((args->flags & CLONE_PARENT_SETTID) != 0) {
        parent_tid = (int *)(uintptr_t)args->parent_tid;
        if (!parent_tid) {
            errno = EFAULT;
            return -1;
        }
    }
    if ((args->flags & CLONE_CHILD_SETTID) != 0) {
        child_tid = (int *)(uintptr_t)args->child_tid;
        if (!child_tid) {
            errno = EFAULT;
            return -1;
        }
    }
    if ((args->flags & CLONE_CHILD_CLEARTID) != 0 && !args->child_tid) {
        errno = EFAULT;
        return -1;
    }
    if ((args->flags & CLONE_INTO_CGROUP) != 0) {
        if (clone3_resolve_cgroup_target((int)args->cgroup, cgroup_path, sizeof(cgroup_path)) != 0) {
            return -1;
        }
        use_clone_into_cgroup = true;
    } else if (args->cgroup != 0) {
        errno = EINVAL;
        return -1;
    }

    parent = get_current();
    if (!parent) {
        errno = ESRCH;
        return -1;
    }
    child_pid = clone_impl(args->flags);
    if (child_pid < 0) {
        return -1;
    }
    child = task_lookup(child_pid);
    if (!child) {
        errno = ESRCH;
        return -1;
    }
    if (use_clone_into_cgroup) {
        int ret = cgroup_attach_task_path(child, cgroup_path);
        if (ret != 0) {
            clone3_release_child_failure(parent, child);
            errno = -ret;
            return -1;
        }
    }
    if ((args->flags & CLONE_PIDFD) != 0) {
        pidfd = pidfd_create_for_task_impl(child, O_CLOEXEC);
        if (pidfd < 0) {
            clone3_release_child_failure(parent, child);
            return -1;
        }
        *pidfd_ptr = pidfd;
    }

    if ((args->flags & CLONE_PARENT_SETTID) != 0) {
        *parent_tid = child->pid;
    }
    if ((args->flags & CLONE_CHILD_SETTID) != 0) {
        *child_tid = child->pid;
    }
    if ((args->flags & CLONE_CHILD_CLEARTID) != 0) {
        child->clear_child_tid = args->child_tid;
    }

    free_task(child);
    return child_pid;
}

int unshare_impl(uint64_t flags) {
    struct task_struct *task = get_current();

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    if (validate_clone_namespace_flags(flags) != 0) {
        return -1;
    }
    if ((flags & CLONE_FS) != 0) {
        errno = ENOSYS;
        return -1;
    }
    if ((flags & CLONE_NEWPID) != 0) {
        atomic_store(&task->new_pid_namespace_pending, true);
        flags &= ~(uint64_t)CLONE_NEWPID;
    }

    return task_apply_clone_namespace_flags(task, flags);
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
    if (atomic_load(&parent->new_pid_namespace_pending)) {
        child->pid_ns_level = parent->pid_ns_level + 1;
        child->ns_pid = 1;
    } else {
        child->pid_ns_level = parent->pid_ns_level;
        child->ns_pid = child->pid;
    }
    if (parent->uts_ns) {
        uts_put(child->uts_ns);
        child->uts_ns = uts_get(parent->uts_ns);
    }
    if (parent->cred) {
        put_cred(child->cred);
        child->cred = dup_cred(parent->cred);
        if (!child->cred) {
            free_task(child);
            errno = ENOMEM;
            return -1;
        }
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

__attribute__((visibility("default"))) int clone(int (*fn)(void *), void *child_stack, int flags, void *arg) {
    (void)fn;
    (void)child_stack;
    (void)arg;
    return clone_impl((uint64_t)(unsigned int)flags);
}

__attribute__((visibility("default"))) int unshare(int flags) {
    return unshare_impl((uint64_t)(unsigned int)flags);
}
