#ifndef PRIVATE_KERNEL_TASK_STATE_H
#define PRIVATE_KERNEL_TASK_STATE_H

#include "kernel/task.h"
#include "private/kernel/kthread_state.h"
#include "private/kernel/mutex_state.h"

#ifdef __cplusplus
extern "C" {
#endif

extern kernel_mutex_t task_table_lock;

void task_clear_vmas_impl(struct memory_space *mm);
struct memory_space *task_mm_get_impl(struct memory_space *mm);
struct memory_space *task_mm_dup_impl(const struct memory_space *mm);
void task_mm_put_impl(struct memory_space *mm);
void task_mm_update_high_water_impl(struct memory_space *mm);
void mm_shared_mapping_get_impl(struct vm_shared_mapping *mapping);
void mm_shared_mapping_put_impl(struct vm_shared_mapping *mapping);
void mm_private_page_put_impl(struct vm_private_page *page);
long mm_shared_vma_read_impl(struct task_vma *vma, uint64_t addr, void *buf, size_t count);
long mm_shared_vma_write_impl(struct task_vma *vma, uint64_t addr, const void *buf, size_t count);
long mm_private_vma_read_impl(struct task_vma *vma, uint64_t addr, void *buf, size_t count);
long mm_private_vma_write_impl(struct task_vma *vma, uint64_t addr, const void *buf, size_t count);
const struct task_vma *task_find_vma_impl(struct task *task, uint64_t addr);
struct task_vma *task_find_vma_mutable_impl(struct task *task, uint64_t addr);
uint32_t task_vma_page_flags_impl(const struct task_vma *vma, uint64_t addr);
int task_set_vma_page_flags_impl(struct task *task, uint64_t addr, uint64_t size, uint32_t flags);
void task_rename_vma_backing_path_impl(const char *old_path, const char *new_path);
void task_exchange_vma_backing_paths_impl(const char *left_path, const char *right_path);
const struct task_exec_handoff *task_get_exec_handoff_impl(struct task *task);

enum task_vma_kind {
    TASK_VMA_EXEC = 1,
    TASK_VMA_INTERP = 2,
    TASK_VMA_STACK = 3,
    TASK_VMA_ANON = 4,
    TASK_VMA_FILE = 5,
    TASK_VMA_GUARD = 6,
};

struct task_vma {
    uint64_t start;
    uint64_t end;
    uint32_t flags;
    enum task_vma_kind kind;
    void *image;
    size_t image_size;
    uint64_t page_count;
    uint32_t *page_flags;
    uint8_t *resident_pages;
    uint8_t *dirty_pages;
    struct vm_private_page **private_pages;
    int backing_fd;
    uint64_t backing_file_identity;
    char backing_path[MAX_PATH];
    uint64_t backing_offset;
    int shared;
    struct vm_shared_mapping *shared_mapping;
    struct vm_shared_mapping **shared_pages;
    uint64_t shared_mapping_offset;
};

struct task_dynamic_info {
    uint64_t vaddr;
    uint64_t size;
    uint64_t rela_vaddr;
    uint64_t rela_size;
    uint64_t rela_entry_size;
    uint64_t plt_rela_vaddr;
    uint64_t plt_rela_size;
    uint64_t plt_rela_type;
    uint64_t strtab_vaddr;
    uint64_t strtab_size;
    uint64_t symtab_vaddr;
    uint64_t needed_offsets[TASK_EXEC_MAX_DYNAMIC_NEEDED];
    uint32_t needed_count;
};

struct task_exec_handoff {
    uint64_t entry_point;
    uint64_t initial_stack_pointer;
    uint64_t aarch64_pc;
    uint64_t aarch64_sp;
    long (*read_memory)(struct task *task, uint64_t addr, void *buf, size_t count);
    long (*write_memory)(struct task *task, uint64_t addr, const void *buf, size_t count);
};

enum run_state {
RUN_STATE_RUNNING = 0,
RUN_STATE_INTERRUPTIBLE = 1,
RUN_STATE_UNINTERRUPTIBLE = 2,
RUN_STATE_STOPPED = 4,
RUN_STATE_ZOMBIE = 8,
RUN_STATE_DEAD = 16,
};

struct task_rlimit {
    uint64_t cur;
    uint64_t max;
};

struct tty_state {
    int index;
    int32_t foreground_pgrp;
    atomic_t refs;
};

struct memory_space {
    atomic_t refs;
    void *exec_image_base;
    size_t exec_image_size;
    uint64_t exec_entry;
    uint64_t exec_dynamic_vaddr;
    uint64_t exec_dynamic_size;
    struct task_dynamic_info exec_dynamic;
    uint32_t exec_segment_count;
    struct {
        uint64_t vaddr;
        uint64_t memsz;
        uint64_t filesz;
        uint64_t offset;
        uint32_t flags;
        void *image;
        size_t image_size;
    } exec_segments[TASK_EXEC_MAX_LOAD_SEGMENTS];
    void *interp_image_base;
    size_t interp_image_size;
    uint64_t interp_entry;
    uint64_t interp_dynamic_vaddr;
    uint64_t interp_dynamic_size;
    struct task_dynamic_info interp_dynamic;
    uint32_t interp_segment_count;
    char interp_path[MAX_PATH];
    struct {
        uint64_t vaddr;
        uint64_t memsz;
        uint64_t filesz;
        uint64_t offset;
        uint32_t flags;
        void *image;
        size_t image_size;
    } interp_segments[TASK_EXEC_MAX_LOAD_SEGMENTS];
    uint64_t entry_point;
    uint64_t initial_stack_base;
    uint64_t initial_stack_size;
    uint64_t initial_stack_pointer;
    void *stack_guard_image;
    void *initial_stack_image;
    size_t initial_stack_image_size;
    int initial_argc;
    int initial_envc;
    uint64_t initial_argv[TASK_MAX_ARGS];
    uint64_t initial_envp[TASK_MAX_ARGS];
    uint64_t auxv_random_addr;
    uint64_t auxv_platform_addr;
    uint64_t auxv_execfn_addr;
    struct {
        uint64_t type;
        uint64_t value;
    } auxv[TASK_EXEC_MAX_AUXV];
    uint32_t auxv_count;
    struct task_vma vmas[TASK_EXEC_MAX_VMAS];
    uint32_t vma_count;
    uint64_t vm_peak_pages;
    uint64_t vm_high_water_rss_pages;
    struct task_exec_handoff handoff;
    struct address_space *vma_addr_space;
    uint64_t brk_start;
    uint64_t brk_current;
    uint64_t signal_frame_sp;
    uint64_t signal_frame_signo;
    uint64_t signal_frame_return_pc;
    uint64_t signal_handler_pc;
    uint64_t signal_frame_flags;
    uint64_t signal_frame_restorer_pc;
    uint64_t signal_frame_mask;
    uint64_t signal_frame_altstack_sp;
    uint64_t signal_frame_altstack_size;
    uint64_t signal_frame_altstack_flags;
    uint64_t signal_frame_current_sp;
    uint64_t signal_frame_size;
    uint64_t signal_frame_ucontext_flags;
    uint64_t signal_frame_restartable;
    uint64_t signal_frame_restart_return_pc;
    uint64_t signal_frame_restart_sp;
    uint64_t signal_frame_restart_signo;
    uint64_t signal_frame_restart_kind;
    uint64_t signal_frame_restart_arg0;
    uint64_t signal_frame_restart_arg1;
    uint64_t signal_frame_restart_arg2;
    uint64_t signal_frame_restart_arg3;
    uint64_t signal_frame_restart_arg4;
    uint64_t signal_frame_restart_arg5;
};

enum exec_image_type {
    EXEC_IMAGE_NONE = 0,
    EXEC_IMAGE_INVALID,
    EXEC_IMAGE_NATIVE,
    EXEC_IMAGE_ELF,
    EXEC_IMAGE_WASI,
    EXEC_IMAGE_SCRIPT,
};

typedef int (*native_entry_t)(struct task *task, int argc, char **argv, char **envp);

struct exec_image {
    enum exec_image_type type;
    char path[MAX_PATH];
    char interpreter[MAX_PATH];

    union {
        struct {
            native_entry_t entry;
        } native;
        struct {
            uint64_t entry;
            uint16_t type;
            uint16_t machine;
        } elf;
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

struct task {
    int32_t pid;
    int32_t tgid;
    int32_t ppid;
    int32_t pgid;
    int32_t sid;
    int32_t ns_pid;
    int32_t pid_ns_level;
    atomic_t state;
    int exit_status;
    atomic_t exited;
    atomic_t signaled;
    atomic_t termsig;
    atomic_t stopped;
    atomic_t stopsig;
    atomic_t continued;
    atomic_t stop_report_pending;
    atomic_t continue_report_pending;
    atomic_t execed;
    uint64_t clone_flags;
    uint64_t thread_pending_signals;
    atomic_t new_pid_namespace_pending;
    kernel_thread_t thread;
    char comm[TASK_COMM_CAPACITY];
    char exe[MAX_PATH];
    int argc;
    char *argv[TASK_MAX_ARGS];
    int envc;
    char *envp[TASK_MAX_ARGS];
    struct fd_table *files;
    struct fs_context *fs;
    struct signal_state *signal;
    struct cred *cred;
    struct cgroup *cgroup;
    struct cgroup *cgroup_ns_root;
    uint64_t cgroup_ns_id;
    uint64_t cgroup_ns_owner_user_ns_id;
    struct seccomp_policy *seccomp;
    struct tty_state *tty;
    struct memory_space *mm;
    struct exec_image *exec_image;
    struct uts_state *uts_ns;
    uint64_t exec_secure;
    uint64_t exec_dumpable;
    int32_t ptracer_pid;
    bool ptrace_attached;
    bool ptrace_syscall_trace;
    bool ptrace_syscall_exit_next;
    bool ptrace_signal_bypass;
    int32_t ptrace_signal;
    int32_t ptrace_signal_stop;
    uint64_t ptrace_options;
    uint64_t ptrace_event;
    uint64_t ptrace_event_message;
    uint8_t ptrace_syscall_op;
    uint64_t ptrace_syscall_nr;
    uint64_t ptrace_syscall_args[6];
    int64_t ptrace_syscall_retval;
    bool ptrace_syscall_is_error;
    uint64_t ptrace_regs[31];
    uint64_t ptrace_sp;
    uint64_t ptrace_pc;
    uint64_t ptrace_pstate;
    uint64_t clear_child_tid;
    uint64_t robust_list_head;
    uint64_t robust_list_len;
    int32_t last_fault_signal;
    int32_t last_fault_code;
    uint64_t last_fault_addr;
    struct task *parent;
    struct task *children;
    struct task *next_sibling;
    struct task *hash_next;
    struct task *vfork_parent;
    kernel_cond_t wait_cond;
    kernel_mutex_t wait_lock;
    struct wait_queue_head *current_wait_queue;
    int waiters;
    struct task_rlimit rlimits[16];
    uint64_t start_time_ns;
    atomic_t refs;
    kernel_mutex_t lock;
};

int task_restart_record_impl(struct task *task,
                             enum task_restart_kind kind,
                             uint64_t arg0,
                             uint64_t arg1,
                             uint64_t arg2,
                             uint64_t arg3,
                             uint64_t arg4,
                             uint64_t arg5);

#ifdef __cplusplus
}
#endif

#endif
