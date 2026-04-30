/* IXLandSystem/kernel/task.h
 * Private internal header for virtual task/process subsystem
 *
 * This is PRIVATE internal state for the virtual kernel's process model.
 * NOT a public Linux ABI header.
 *
 * Virtual task behavior emulated:
 * - virtual PID/TGID/PPID/PGID/SID namespace
 * - process groups and sessions
 * - parent/child relationships
 * - wait/reap semantics
 * - zombie/dead transitions
 * - clone/fork/vfork bookkeeping
 */

#ifndef KERNEL_TASK_H
#define KERNEL_TASK_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "../fs/fdtable.h"
#include "../fs/vfs.h"
#include "../internal/ios/kernel/sync.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TASK_COMM_LEN 16
#define TASK_MAX_ARGS 256
#define TASK_MAX_TASKS 1024

/* Forward declarations for private subsystem state */
struct task_struct;
struct signal_struct;
struct tty_struct;
struct mm_struct;
struct exec_image;
struct wait_queue_head;
struct nsproxy;
struct cgroup;
struct seccomp;
struct cred;
struct task_rlimit;

/* Task lifecycle states - virtual kernel internal */
enum task_state {
TASK_RUNNING = 0,
TASK_INTERRUPTIBLE = 1,
TASK_UNINTERRUPTIBLE = 2,
TASK_STOPPED = 4,
TASK_ZOMBIE = 8,
TASK_DEAD = 16,
};

/* Resource limits - private internal representation
 * Do not use host struct rlimit */
struct task_rlimit {
    uint64_t cur;
    uint64_t max;
};

/* TTY structure - virtual kernel internal */
struct tty_struct {
    int index;
    int32_t foreground_pgrp;
    atomic_int refs;
};

/* MM structure - virtual kernel internal */
struct mm_struct {
    void *exec_image_base;
    size_t exec_image_size;
    struct address_space *vma_addr_space;
};

/* Exec image types - virtual kernel internal */
enum exec_image_type {
    EXEC_IMAGE_NONE = 0,
    EXEC_IMAGE_NATIVE,
    EXEC_IMAGE_WASI,
    EXEC_IMAGE_SCRIPT,
};

/* Exec image entry - virtual kernel internal */
typedef int (*native_entry_t)(struct task_struct *task, int argc, char **argv, char **envp);

struct exec_image {
    enum exec_image_type type;
    char path[MAX_PATH];
    char interpreter[MAX_PATH];

    union {
        struct {
            native_entry_t entry;
        } native;
        struct {
            void *module;
            void *instance;
        } wasi;
        struct {
            char *interp_argv[TASK_MAX_ARGS];
            int interp_argc;
        } script;
    } u;
};

/* Task structure - virtual kernel's internal representation of a Linux task
 * This is PRIVATE internal state, NOT Linux UAPI.
 * Uses int32_t for PIDs to avoid host type contamination. */
struct task_struct {
    /* Virtual PID/TGID/PGID/SID namespace identity
     * int32_t used instead of host pid_t */
    int32_t pid;
    int32_t tgid;
    int32_t ppid;
    int32_t pgid;
    int32_t sid;

    /* Virtual task lifecycle state */
    atomic_int state;
    int exit_status;
    atomic_bool exited;
    atomic_bool signaled;
    atomic_int termsig;
    atomic_bool stopped;
    atomic_int stopsig;
    atomic_bool continued;
    atomic_bool stop_report_pending;
    atomic_bool continue_report_pending;
    atomic_bool execed;     /* Set after execve() - blocks setpgid per Linux semantics */

    /* Host thread backing for this virtual task */
    kernel_thread_t thread;
    char comm[TASK_COMM_LEN];
    char exe[MAX_PATH];
    int argc;
    char *argv[TASK_MAX_ARGS];

    /* Resource ownership - pointers to virtual subsystem state */
    struct files_struct *files;
    struct fs_struct *fs;
    struct signal_struct *signal;
    struct tty_struct *tty;
    struct mm_struct *mm;
    struct exec_image *exec_image;

    /* Virtual process hierarchy relationships */
    struct task_struct *parent;
    struct task_struct *children;
    struct task_struct *next_sibling;
    struct task_struct *hash_next;

    /* Vfork tracking - virtual kernel bookkeeping */
    struct task_struct *vfork_parent;

    /* Virtual wait queue / sleep state */
    kernel_cond_t wait_cond;
    kernel_mutex_t wait_lock;
    struct wait_queue_head *current_wait_queue;
    int waiters;

    /* Resource limits - virtual kernel tracked
     * Uses private struct task_rlimit, not host struct rlimit */
    struct task_rlimit rlimits[16];

    /* Start time - virtual kernel tracked
     * Stored as nanoseconds instead of host struct timespec */
    uint64_t start_time_ns;

    /* Reference counting and locking */
    atomic_int refs;
    kernel_mutex_t lock;
};

/* Task global table - virtual PID namespace */
extern kernel_mutex_t task_table_lock;
extern struct task_struct *task_table[TASK_MAX_TASKS];

/* The init_task (pid 1) - set up during kernel boot */
extern struct task_struct *init_task;

/* Task allocation - virtual kernel internal */
struct task_struct *alloc_task(void);
void free_task(struct task_struct *task);

/* Current task accessors - virtual kernel runtime */
struct task_struct *get_current(void);
void set_current(struct task_struct *task);

/* Virtual PID namespace management */
int32_t alloc_pid(void);
void free_pid(int32_t pid);
void pid_init(void);

/* Virtual task table management */
int task_init(void);
void task_deinit(void);
struct task_struct *task_lookup(int32_t pid);
int task_hash(int32_t pid);
struct task_struct *task_create_child_impl(struct task_struct *parent);
void task_unlink_child_impl(struct task_struct *parent, struct task_struct *child);
void task_mark_stopped_by_signal(struct task_struct *task, int32_t sig);
void task_mark_continued_by_signal(struct task_struct *task);
void task_mark_signaled_exit(struct task_struct *task, int32_t sig);
void task_mark_exited(struct task_struct *task, int status);
void task_notify_parent_state_change(struct task_struct *task);

/* Virtual process identity syscalls (internal helpers) */
int32_t getpid_impl(void);
int32_t getppid_impl(void);
int32_t getpgrp_impl(void);
int32_t getpgid_impl(int32_t pid);
int setpgid_impl(int32_t pid, int32_t pgid);
int32_t getsid_impl(int32_t pid);
int32_t setsid_impl(void);
int task_session_has_pgrp_impl(int32_t sid, int32_t pgid);

/* Virtual fork/exec - internal helpers */
int32_t fork_impl(void);
int32_t vfork_impl(void);
int task_exec_transition_impl(const char *path, const char *argv0);

/* Virtual exit/wait - internal helpers */
void exit_impl(int status);
int32_t wait_impl(int *wstatus);
int32_t waitpid_impl(int32_t pid, int *wstatus, int options);
int32_t wait4_impl(int32_t pid, int *wstatus, int options, void *rusage);

/* Virtual vfork notifications */
void vfork_exec_notify(void);
void vfork_exit_notify(void);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_TASK_H */
