#include <limits.h>

#include <linux/errno.h>
#include <linux/atomic.h>
#include <linux/string.h>
#include <uapi/linux/capability.h>
#include <uapi/linux/fcntl.h>
#include <uapi/linux/resource.h>
#include <uapi/linux/sched.h>

#include "../fs/fdtable.h"
#include "../fs/vfs.h"
#include "cgroup.h"
#include "cred.h"
#include "fork.h"
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
    struct task *parent;
    struct task *child;
    fork_frame_t frame;
    volatile __kernel_pid_t result; /* Result from child perspective */
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
        return -EINVAL;
    }
    if ((masked & CLONE_NEWNS) != 0 && (masked & CLONE_FS) != 0) {
        return -EINVAL;
    }
    if ((masked & CLONE_SIGHAND) != 0 && (masked & CLONE_VM) == 0) {
        return -EINVAL;
    }
    if ((masked & CLONE_THREAD) != 0 &&
        ((masked & CLONE_VM) == 0 || (masked & CLONE_SIGHAND) == 0)) {
        return -EINVAL;
    }
    return 0;
}

struct unshare_state_snapshot {
    bool had_current;
    bool new_pid_namespace_pending;
    struct fs_context *fs;
    struct uts_state *uts_ns;
    struct cgroup *cgroup_ns_root;
    uint64_t cgroup_ns_id;
    uint64_t cgroup_ns_owner_user_ns_id;
    struct cred cred;
};

static void unshare_snapshot_state(struct task *task, struct unshare_state_snapshot *snapshot) {
    memset(snapshot, 0, sizeof(*snapshot));
    if (!task) {
        return;
    }
    snapshot->had_current = true;
    snapshot->new_pid_namespace_pending = atomic_read(&task->new_pid_namespace_pending) != 0;
    snapshot->fs = task->fs;
    snapshot->uts_ns = task->uts_ns ? uts_get(task->uts_ns) : NULL;
    snapshot->cgroup_ns_root = task->cgroup_ns_root ? cgroup_get(task->cgroup_ns_root) : NULL;
    snapshot->cgroup_ns_id = task->cgroup_ns_id;
    snapshot->cgroup_ns_owner_user_ns_id = task->cgroup_ns_owner_user_ns_id;
    if (task->cred) {
        snapshot->cred = *task->cred;
    }
}

static void unshare_restore_state(struct task *task, const struct unshare_state_snapshot *snapshot) {
    if (!task || !snapshot || !snapshot->had_current) {
        return;
    }
    atomic_set(&task->new_pid_namespace_pending, snapshot->new_pid_namespace_pending ? 1 : 0);
    if (task->cred) {
        *task->cred = snapshot->cred;
    }
    if (task->uts_ns != snapshot->uts_ns) {
        if (task->uts_ns) {
            uts_put(task->uts_ns);
        }
        task->uts_ns = snapshot->uts_ns ? uts_get(snapshot->uts_ns) : NULL;
    }
    if (task->cgroup_ns_root != snapshot->cgroup_ns_root) {
        if (task->cgroup_ns_root) {
            cgroup_put(task->cgroup_ns_root);
        }
        task->cgroup_ns_root = snapshot->cgroup_ns_root ? cgroup_get(snapshot->cgroup_ns_root) : NULL;
    }
    task->cgroup_ns_id = snapshot->cgroup_ns_id;
    task->cgroup_ns_owner_user_ns_id = snapshot->cgroup_ns_owner_user_ns_id;
}

static void unshare_release_snapshot(struct unshare_state_snapshot *snapshot) {
    if (!snapshot || !snapshot->had_current) {
        return;
    }
    if (snapshot->uts_ns) {
        uts_put(snapshot->uts_ns);
        snapshot->uts_ns = NULL;
    }
    if (snapshot->cgroup_ns_root) {
        cgroup_put(snapshot->cgroup_ns_root);
        snapshot->cgroup_ns_root = NULL;
    }
}

static int task_apply_clone_namespace_flags(struct task *task, uint64_t flags) {
    uint64_t masked = flags & ~0xffULL;
    struct uts_state *new_uts;

    if ((masked & CLONE_NEWUSER) != 0) {
        int ret = cred_unshare_user_namespace(task->cred);
        if (ret != 0) {
            return ret;
        }
    }

    if ((masked & CLONE_NEWNS) != 0) {
        int ret;
        struct task *saved;
        if (!task->fs) {
            return -ESRCH;
        }
        saved = task_current();
        task_set_current(task);
        ret = fs_unshare_mount_namespace(task->fs);
        task_set_current(saved);
        if (ret < 0) {
            return ret;
        }
    }

    if ((masked & CLONE_NEWUTS) != 0) {
        struct task *saved = task_current();
        task_set_current(task);
        new_uts = uts_dup(task->uts_ns);
        task_set_current(saved);
        if (!new_uts) {
            return -ENOMEM;
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
            return -ENOMEM;
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

    if (!cgroup_path || cgroup_path_len == 0) {
        return -EINVAL;
    }

    entry = get_fd_entry_impl(fd);
    if (!entry) {
        return -EBADF;
    }
    if (!get_fd_is_synthetic_dir_impl(entry)) {
        put_fd_entry_impl(entry);
        return -ENOTDIR;
    }
    dir_class = get_fd_synthetic_dir_class_impl(entry);
    ret = get_fd_path_impl(entry, virtual_path, sizeof(virtual_path));
    put_fd_entry_impl(entry);
    if (ret != 0) {
        return -ENOENT;
    }
    if (dir_class != SYNTHETIC_DIR_CGROUPFS) {
        return -ENOTDIR;
    }
    if (cgroupfs_resolve_path(virtual_path, cgroup_path, cgroup_path_len, &type) != 0) {
        return -ENOENT;
    }
    if (type != CGROUPFS_NODE_DIR) {
        return -ENOTDIR;
    }
    return 0;
}

static void clone3_release_child_failure(struct task *parent, struct task *child) {
    if (!parent || !child) {
        return;
    }
    task_unlink_child_impl(parent, child);
    task_put(child);
}

/* Child thread entry point */
static void *fork_child_trampoline(void *arg) {
    fork_ctx_t *ctx = (fork_ctx_t *)arg;

    /* Set child as current task in thread-local storage */
    task_set_current(ctx->child);
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
    fork_frame_restore(&ctx->frame, 1);

    /* NOTREACHED */
    return NULL;
}

__kernel_pid_t fork_impl(void) {
    struct task *parent = task_current();
    if (!parent) {
        return -ESRCH;
    }

    /* Check process limit */
    int child_count = 0;
    kernel_mutex_lock(&parent->lock);
    struct task *c = parent->children;
    while (c) {
        child_count++;
        c = c->next_sibling;
    }
    if (child_count >= (int)parent->rlimits[RLIMIT_NPROC].cur) {
        kernel_mutex_unlock(&parent->lock);
        return -EAGAIN;
    }
    kernel_mutex_unlock(&parent->lock);

    /* Allocate child task */
    struct task *child = alloc_task();
    if (!child) {
        return -ENOMEM;
    }

    /* Set up parent-child relationship */
    child->ppid = parent->pid;
    child->pgid = parent->pgid;
    child->sid = parent->sid;
    if (atomic_read(&parent->new_pid_namespace_pending) != 0) {
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
            task_put(child);
            return -ENOMEM;
        }
    }

    /* Copy parent's filesystem context */
    if (parent->fs) {
        child->fs = dup_fs_struct(parent->fs);
        if (!child->fs) {
            task_put(child);
            return -ENOMEM;
        }
    }

    /* Copy subsystems with proper semantics */
    child->files = dup_files(parent->files);
    if (!child->files) {
        task_put(child);
        return -ENOMEM;
    }

    /* Copy signal handlers */
    if (parent->signal && child->signal) {
        memcpy(child->signal->actions, parent->signal->actions, sizeof(parent->signal->actions));
        child->signal->blocked = parent->signal->blocked;
    }

    /* Reference TTY (not copy) */
    if (parent->tty) {
        child->tty = parent->tty;
        atomic_inc(&child->tty->refs);
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
    if (fork_frame_init(&ctx.frame) != 0) {
        task_put(child);
        return -ENOMEM;
    }
    kernel_mutex_init(&ctx.lock);
    kernel_cond_init(&ctx.cond);

    /* Make context available globally for this thread */
    active_fork_ctx = &ctx;

    /* Save parent's context */
    if (fork_frame_save(&ctx.frame) == 0) {
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
            task_put(child);
            active_fork_ctx = NULL;
            fork_frame_destroy(&ctx.frame);
            kernel_mutex_destroy(&ctx.lock);
            kernel_cond_destroy(&ctx.cond);
            return -EAGAIN;
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
        fork_frame_destroy(&ctx.frame);
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
    fork_frame_destroy(&ctx.frame);

    /* Child returns 0 */
    return 0;
}

int32_t clone_impl(uint64_t flags) {
    struct task *parent = task_current();
    struct task *child;

    if (!parent) {
        return -ESRCH;
    }
    {
        int ret = validate_clone_namespace_flags(flags);
        if (ret != 0) {
            return ret;
        }
    }

    child = task_create_child_with_flags_impl(parent, flags);
    if (!child) {
        return -ENOMEM;
    }

    if ((flags & CLONE_FS) != 0 && parent->fs) {
        free_fs_struct(child->fs);
        child->fs = get_fs_struct(parent->fs);
        if (!child->fs) {
            task_unlink_child_impl(parent, child);
            task_put(child);
            return -ENOMEM;
        }
    }

    {
        int ret = task_apply_clone_namespace_flags(child, flags & ~(uint64_t)CLONE_NEWPID);
        if (ret != 0) {
        task_unlink_child_impl(parent, child);
        task_put(child);
            return ret;
        }
    }

    return child->pid;
}

int32_t clone3_impl(const struct clone_args *args, size_t size) {
    int32_t child_pid;
    int32_t original_child_pid;
    int32_t requested_tid = 0;
    int pidfd = -1;
    int *pidfd_ptr = NULL;
    int *parent_tid = NULL;
    int *child_tid = NULL;
    struct task *parent;
    struct task *child;
    char cgroup_path[MAX_PATH];
    bool child_creates_new_pidns = false;
    bool use_clone_into_cgroup = false;
    int ret;

    if (!args) {
        return -EFAULT;
    }
    if (size < CLONE_ARGS_SIZE_VER0) {
        return -EINVAL;
    }
    if (args->exit_signal > 0xff) {
        return -EINVAL;
    }
    parent = task_current();
    if (!parent || !parent->cred) {
        return -ESRCH;
    }
    child_creates_new_pidns =
        ((args->flags & CLONE_NEWPID) != 0) || atomic_read(&parent->new_pid_namespace_pending) != 0;
    if ((args->set_tid == 0) != (args->set_tid_size == 0)) {
        return -EINVAL;
    }
    if (args->set_tid_size > 1) {
        return -EINVAL;
    }
    if (args->set_tid_size == 1) {
        if (!cred_has_cap(parent->cred, CAP_SYS_ADMIN) &&
            !cred_has_cap(parent->cred, CAP_CHECKPOINT_RESTORE)) {
            return -EPERM;
        }
        requested_tid = *(const int32_t *)(uintptr_t)args->set_tid;
        if (requested_tid <= 0) {
            return -EINVAL;
        }
        if (child_creates_new_pidns && requested_tid != 1) {
            return -EINVAL;
        }
    }
    if ((args->flags & CLONE_PIDFD) != 0) {
        if (args->pidfd == 0) {
            return -EFAULT;
        }
        pidfd_ptr = (int *)(uintptr_t)args->pidfd;
        if ((args->flags & CLONE_THREAD) != 0) {
            return -EINVAL;
        }
    } else if (args->pidfd != 0) {
        return -EINVAL;
    }
    if ((args->flags & CLONE_PARENT_SETTID) != 0) {
        parent_tid = (int *)(uintptr_t)args->parent_tid;
        if (!parent_tid) {
            return -EFAULT;
        }
    }
    if ((args->flags & CLONE_CHILD_SETTID) != 0) {
        child_tid = (int *)(uintptr_t)args->child_tid;
        if (!child_tid) {
            return -EFAULT;
        }
    }
    if ((args->flags & CLONE_CHILD_CLEARTID) != 0 && !args->child_tid) {
        return -EFAULT;
    }
    if ((args->flags & CLONE_INTO_CGROUP) != 0) {
        ret = clone3_resolve_cgroup_target((int)args->cgroup, cgroup_path, sizeof(cgroup_path));
        if (ret != 0) {
            return ret;
        }
        use_clone_into_cgroup = true;
    } else if (args->cgroup != 0) {
        return -EINVAL;
    }
    child_pid = clone_impl(args->flags);
    if (child_pid < 0) {
        return child_pid;
    }
    child = task_lookup(child_pid);
    if (!child) {
        return -ESRCH;
    }
    original_child_pid = child->pid;
    if (args->set_tid_size == 1) {
        if (child_creates_new_pidns) {
            child->ns_pid = requested_tid;
        } else if ((ret = task_reassign_pid_impl(child, requested_tid)) != 0) {
            clone3_release_child_failure(parent, child);
            return ret;
        }
        if (child->pid != original_child_pid) {
            ptrace_rewrite_fork_event_message(parent, original_child_pid, child->pid,
                                              (args->flags & CLONE_THREAD) != 0);
        }
        child_pid = child->pid;
    }
    if (use_clone_into_cgroup) {
        ret = cgroup_attach_task_path(child, cgroup_path);
        if (ret != 0) {
            clone3_release_child_failure(parent, child);
            return ret;
        }
    }
    if ((args->flags & CLONE_PIDFD) != 0) {
        pidfd = pidfd_create_for_task_impl(child, O_CLOEXEC);
        if (pidfd < 0) {
            clone3_release_child_failure(parent, child);
            return pidfd;
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

    task_put(child);
    return child_pid;
}

int unshare_impl(uint64_t flags) {
    struct task *task = task_current();
    struct unshare_state_snapshot snapshot;
    struct fs_context *old_fs = NULL;
    uint64_t namespace_flags = flags;
    int ret;

    if (!task) {
        return -ESRCH;
    }
    unshare_snapshot_state(task, &snapshot);
    if ((namespace_flags & CLONE_NEWNS) != 0 || (namespace_flags & CLONE_NEWUSER) != 0) {
        namespace_flags |= CLONE_FS;
    }
    ret = validate_clone_namespace_flags(namespace_flags & ~(uint64_t)CLONE_FS);
    if (ret != 0) {
        unshare_release_snapshot(&snapshot);
        return ret;
    }
    if ((namespace_flags & CLONE_NEWPID) != 0) {
        atomic_set(&task->new_pid_namespace_pending, 1);
        namespace_flags &= ~(uint64_t)CLONE_NEWPID;
    }
    if ((namespace_flags & CLONE_FS) != 0) {
        if (!task->fs) {
            unshare_restore_state(task, &snapshot);
            unshare_release_snapshot(&snapshot);
            return -ESRCH;
        }
        old_fs = task->fs;
        task->fs = dup_fs_struct(old_fs);
        if (!task->fs) {
            task->fs = old_fs;
            unshare_restore_state(task, &snapshot);
            unshare_release_snapshot(&snapshot);
            return -ENOMEM;
        }
    }
    ret = task_apply_clone_namespace_flags(task, namespace_flags & ~(uint64_t)CLONE_FS);
    if (ret != 0) {
        if (old_fs) {
            free_fs_struct(task->fs);
            task->fs = old_fs;
        }
        unshare_restore_state(task, &snapshot);
        unshare_release_snapshot(&snapshot);
        return ret;
    }
    if (old_fs) {
        free_fs_struct(old_fs);
    }
    unshare_release_snapshot(&snapshot);
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
    struct task *parent;
    struct task *child;
    fork_frame_t parent_frame;
    fork_frame_t child_frame;
    volatile int child_done;   /* Set when child execs or exits */
    volatile int child_execed; /* Set if child called execve */
    kernel_mutex_t lock;
    kernel_cond_t cond;
    __kernel_pid_t child_pid;
} vfork_ctx_t;

/* Global vfork context */
static __thread vfork_ctx_t *active_vfork_ctx = NULL;

/* Vfork child entry */
static void *vfork_child_trampoline(void *arg) {
    vfork_ctx_t *ctx = (vfork_ctx_t *)arg;

    /* Set child as current task */
    task_set_current(ctx->child);
    ctx->child->thread = kernel_thread_self();

    /* Signal that child is ready */
    kernel_mutex_lock(&ctx->lock);
    kernel_cond_broadcast(&ctx->cond);
    kernel_mutex_unlock(&ctx->lock);

    /* Jump to child continuation */
    fork_frame_restore(&ctx->child_frame, 1);

    /* NOTREACHED */
    return NULL;
}

int vfork_impl(void) {
    struct task *parent = task_current();
    if (!parent) {
        return -ESRCH;
    }

    /* Check resource limits */
    kernel_mutex_lock(&parent->lock);
    int child_count = 0;
    struct task *c = parent->children;
    while (c) {
        child_count++;
        c = c->next_sibling;
    }
    if (child_count >= (int)parent->rlimits[RLIMIT_NPROC].cur) {
        kernel_mutex_unlock(&parent->lock);
        return -EAGAIN;
    }
    kernel_mutex_unlock(&parent->lock);

    /* Allocate child task */
    struct task *child = alloc_task();
    if (!child) {
        return -ENOMEM;
    }

    /* Set up parent-child relationship */
    child->ppid = parent->pid;
    child->pgid = parent->pgid;
    child->sid = parent->sid;
    child->vfork_parent = parent; /* Mark as vfork child */
    if (atomic_read(&parent->new_pid_namespace_pending) != 0) {
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
            task_put(child);
            return -ENOMEM;
        }
    }

    /* Copy parent's filesystem context */
    if (parent->fs) {
        child->fs = dup_fs_struct(parent->fs);
        if (!child->fs) {
            kernel_mutex_lock(&parent->lock);
            parent->children = child->next_sibling;
            kernel_mutex_unlock(&parent->lock);
            task_put(child);
            return -ENOMEM;
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
            task_put(child);
            return -ENOMEM;
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
        atomic_inc(&child->tty->refs);
    }

    /* Link into parent's children list */
    kernel_mutex_lock(&parent->lock);
    child->parent = parent;
    child->next_sibling = parent->children;
    parent->children = child;
    kernel_mutex_unlock(&parent->lock);

    /* Mark parent as suspended (vfork semantics) */
    atomic_set(&parent->state, RUN_STATE_UNINTERRUPTIBLE);

    /* Set up vfork context */
    vfork_ctx_t ctx;
    ctx.parent = parent;
    ctx.child = child;
    ctx.child_done = 0;
    ctx.child_execed = 0;
    ctx.child_pid = child->pid;
    if (fork_frame_init(&ctx.parent_frame) != 0) {
        task_put(child);
        return -ENOMEM;
    }
    if (fork_frame_init(&ctx.child_frame) != 0) {
        fork_frame_destroy(&ctx.parent_frame);
        task_put(child);
        return -ENOMEM;
    }
    kernel_mutex_init(&ctx.lock);
    kernel_cond_init(&ctx.cond);

    active_vfork_ctx = &ctx;

    /* Parent: Save context */
    if (fork_frame_save(&ctx.parent_frame) == 0) {
        /* Child: Set up entry point */
        if (fork_frame_save(&ctx.child_frame) == 0) {
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
                atomic_set(&parent->state, RUN_STATE_RUNNING);
                task_put(child);
                active_vfork_ctx = NULL;
                fork_frame_destroy(&ctx.child_frame);
                fork_frame_destroy(&ctx.parent_frame);
                kernel_mutex_destroy(&ctx.lock);
                kernel_cond_destroy(&ctx.cond);
                return -EAGAIN;
            }

            /* Wait for child to exec or exit (vfork semantics) */
            kernel_mutex_lock(&ctx.lock);
            while (!ctx.child_done) {
                kernel_cond_wait(&ctx.cond, &ctx.lock);
            }
            kernel_mutex_unlock(&ctx.lock);

            /* Child has execed or exited - parent can resume */
            atomic_set(&parent->state, RUN_STATE_RUNNING);

            kernel_thread_detach(child_thread);

            /* Cleanup */
            active_vfork_ctx = NULL;
            fork_frame_destroy(&ctx.child_frame);
            fork_frame_destroy(&ctx.parent_frame);
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
    fork_frame_destroy(&ctx.child_frame);
    fork_frame_destroy(&ctx.parent_frame);
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

__attribute__((visibility("default"))) __kernel_pid_t fork(void) {
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
