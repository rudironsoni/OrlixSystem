#ifndef SIGNAL_CALLS_H
#define SIGNAL_CALLS_H

#ifdef __cplusplus
extern "C" {
#endif

#define KERNEL_SIG_NUM 64
#define KERNEL_SIG_NUM_WORDS ((KERNEL_SIG_NUM + 63) / 64)

typedef void (*sighandler_t)(int);

struct signal_mask_bits {
    unsigned long long sig[KERNEL_SIG_NUM_WORDS];
};

struct signal_action_slot {
    sighandler_t handler;
    struct signal_mask_bits mask;
    int flags;
    unsigned long long restorer;
};

int do_sigaction(int sig, const struct signal_action_slot *act,
                 struct signal_action_slot *oldact);
int do_sigprocmask(int how, const struct signal_mask_bits *set,
                   struct signal_mask_bits *oldset);
int do_sigsetmask(const struct signal_mask_bits *set, struct signal_mask_bits *oldset);
int do_sigpending(struct signal_mask_bits *set);
int do_signal(int signum, sighandler_t handler, sighandler_t *old_handler);
int do_raise(int sig);
int do_pause(void);
int do_sigsuspend(const struct signal_mask_bits *mask);
int do_kill(int pid, int sig);
int do_killpg(int pgrp, int sig);

#ifdef __cplusplus
}
#endif

#endif
