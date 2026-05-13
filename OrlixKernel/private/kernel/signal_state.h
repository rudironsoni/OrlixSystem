#ifndef PRIVATE_KERNEL_SIGNAL_STATE_H
#define PRIVATE_KERNEL_SIGNAL_STATE_H

#include <linux/atomic.h>
#include <linux/signal.h>
#include <linux/types.h>

#include "kernel/signal.h"
#include "private/kernel/mutex_state.h"

#ifdef __cplusplus
extern "C" {
#endif

struct signal_queue_entry {
    int32_t sig;
    int32_t si_signo;
    int32_t si_errno;
    int32_t si_code;
    u64 fault_addr;
    struct signal_queue_entry *next;
};

struct signal_queue {
    struct signal_queue_entry *head;
    struct signal_queue_entry *tail;
    int count;
    kernel_mutex_t lock;
};

struct signal_state {
    atomic_t refs;
    struct sigaction actions[KERNEL_SIG_NUM];
    sigset_t blocked;
    sigset_t pending;
    sigset_t shared_pending;
    struct signal_queue queue;
    stack_t altstack;
    kernel_mutex_t lock;
};

struct pending_signals {
    sigset_t signal;
    struct signal_queue queue;
};

struct signal_state *alloc_signal_struct(void);
void free_signal_struct(struct signal_state *sig);
struct signal_state *dup_signal_struct(struct signal_state *parent);
int signal_init_task(struct task *task);
int signal_copy_fork_state_task(struct task *child, const struct task *parent);
void signal_reset_task_state(struct task *task);
bool signal_action_default_task(const struct task *task, int32_t sig);
bool signal_altstack_has_flags_task(const struct task *task, int32_t flags);
int signal_handler_get_task(const struct task *task,
                            int32_t sig,
                            __sighandler_t *handler);
int signal_handler_set_task(struct task *task,
                            int32_t sig,
                            __sighandler_t handler);

int signal_frame_metadata_get_task(const struct task *task,
                                   uint64_t *signo_out,
                                   uint64_t *return_pc_out,
                                   uint64_t *handler_pc_out,
                                   uint64_t *flags_out,
                                   uint64_t *restorer_pc_out,
                                   uint64_t *mask_out,
                                   uint64_t *current_sp_out,
                                   uint64_t *size_out);
int signal_frame_restart_kind_get_task(const struct task *task,
                                       uint64_t *kind_out);
bool signal_frame_restart_is_task(const struct task *task,
                                  uint64_t kind);
bool signal_frame_restart_matches_task(const struct task *task,
                                       uint64_t kind,
                                       uint64_t arg0,
                                       uint64_t arg1,
                                       uint64_t arg2,
                                       uint64_t arg3,
                                       uint64_t arg4,
                                       uint64_t arg5);
int signal_frame_restart_arg_get_task(const struct task *task,
                                      int index,
                                      uint64_t *value_out);
int signal_frame_restart_status_get_task(const struct task *task,
                                         uint64_t *restartable_out,
                                         uint64_t *restart_return_pc_out,
                                         uint64_t *restart_sp_out,
                                         uint64_t *restart_signo_out);
int signal_frame_restart_record_task(struct task *task,
                                     uint64_t kind,
                                     uint64_t arg0,
                                     uint64_t arg1,
                                     uint64_t arg2,
                                     uint64_t arg3,
                                     uint64_t arg4,
                                     uint64_t arg5);
void signal_frame_restart_clear_task(struct task *task);
void signal_frame_clear_task(struct task *task);
int signal_prepare_frame_impl(struct task *task,
                              int32_t sig,
                              uint64_t return_pc,
                              uint64_t current_sp,
                              uint64_t *frame_sp_out);
long signal_finish_sigreturn_task(struct task *task);

#ifdef __cplusplus
}
#endif

#endif
