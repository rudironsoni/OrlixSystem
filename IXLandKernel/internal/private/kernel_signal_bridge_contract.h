#ifndef IXLAND_INTERNAL_PRIVATE_KERNEL_SIGNAL_BRIDGE_CONTRACT_H
#define IXLAND_INTERNAL_PRIVATE_KERNEL_SIGNAL_BRIDGE_CONTRACT_H

#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KERNEL_SIG_NUM 64
#define KERNEL_SIG_NUM_WORDS ((KERNEL_SIG_NUM + 63) / 64)

typedef void (*sighandler_t)(int);

struct signal_mask_bits {
    uint64_t sig[KERNEL_SIG_NUM_WORDS];
};

struct signal_action_slot {
    sighandler_t handler;
    struct signal_mask_bits mask;
    int32_t flags;
    uint64_t restorer;
};

int do_sigaction(int32_t sig, const struct signal_action_slot *act,
                 struct signal_action_slot *oldact);
int do_sigprocmask(int how, const struct signal_mask_bits *set,
                   struct signal_mask_bits *oldset);
int do_sigpending(struct signal_mask_bits *set);
sighandler_t do_signal(int32_t signum, sighandler_t handler);
int do_raise(int32_t sig);
int do_pause(void);
int do_sigsuspend(const struct signal_mask_bits *mask);
int do_kill(int32_t pid, int32_t sig);
int do_killpg(int32_t pgrp, int32_t sig);

#ifdef __cplusplus
}
#endif

#endif
