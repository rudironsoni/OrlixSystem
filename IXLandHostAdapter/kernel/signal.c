#include <signal.h>
#include <string.h>
#include <errno.h>

#include "kernel_signal_bridge_contract.h"

void bridge_sigset_from_host(const sigset_t *host_set, struct signal_mask_bits *out_set) {
    memset(out_set, 0, sizeof(*out_set));
    if (!host_set)
        return;

    for (int sig = 1; sig <= KERNEL_SIG_NUM; sig++) {
        if (sigismember(host_set, sig)) {
            int idx = (sig - 1) / 64;
            int bit = (sig - 1) % 64;
            if (idx < KERNEL_SIG_NUM_WORDS && bit < 64) {
                out_set->sig[idx] |= (1ULL << bit);
            }
        }
    }
}

void bridge_sigset_to_host(const struct signal_mask_bits *internal_set, sigset_t *host_set) {
    if (!host_set)
        return;
    sigemptyset(host_set);
    if (!internal_set)
        return;

    for (int sig = 1; sig <= KERNEL_SIG_NUM; sig++) {
        int idx = (sig - 1) / 64;
        int bit = (sig - 1) % 64;
        if (idx < KERNEL_SIG_NUM_WORDS && (internal_set->sig[idx] & (1ULL << bit))) {
            sigaddset(host_set, sig);
        }
    }
}

void bridge_signal_from_host(const struct sigaction *host_act, struct signal_action_slot *out_act) {
    memset(out_act, 0, sizeof(*out_act));
    if (!host_act)
        return;

    out_act->handler = host_act->sa_handler;
    bridge_sigset_from_host(&host_act->sa_mask, &out_act->mask);
    out_act->flags = host_act->sa_flags;
}

void bridge_signal_to_host(const struct signal_action_slot *internal_act, struct sigaction *host_act) {
    memset(host_act, 0, sizeof(*host_act));
    if (!internal_act)
        return;

    host_act->sa_handler = internal_act->handler;
    bridge_sigset_to_host(&internal_act->mask, &host_act->sa_mask);
    host_act->sa_flags = internal_act->flags;
}

__attribute__((visibility("default"))) int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact) {
    struct signal_action_slot internal_act;
    struct signal_action_slot internal_oldact;
    struct signal_action_slot *internal_act_ptr = NULL;
    struct signal_action_slot *internal_oldact_ptr = NULL;

    if (act) {
        bridge_signal_from_host(act, &internal_act);
        internal_act_ptr = &internal_act;
    }

    if (oldact) {
        internal_oldact_ptr = &internal_oldact;
    }

    int result = do_sigaction(signum, internal_act_ptr, internal_oldact_ptr);

    if (oldact && result == 0) {
        bridge_signal_to_host(&internal_oldact, oldact);
    }

    return result;
}

__attribute__((visibility("default"))) sighandler_t signal(int signum, sighandler_t handler) {
    if (signum < 1 || signum >= KERNEL_SIG_NUM) {
        errno = EINVAL;
        return (sighandler_t)-1;
    }

    if (signum == SIGKILL || signum == SIGSTOP) {
        errno = EINVAL;
        return (sighandler_t)-1;
    }

    return do_signal(signum, handler);
}

__attribute__((visibility("default"))) int kill(pid_t pid, int sig) {
    return do_kill(pid, sig);
}

__attribute__((visibility("default"))) int killpg(pid_t pgrp, int sig) {
    return do_killpg(pgrp, sig);
}

__attribute__((visibility("default"))) int sigprocmask(int how, const sigset_t *set, sigset_t *oldset) {
    if (how < SIG_BLOCK || how > SIG_SETMASK) {
        errno = EINVAL;
        return -1;
    }

    struct signal_mask_bits internal_set;
    struct signal_mask_bits internal_oldset;
    struct signal_mask_bits *internal_set_ptr = NULL;
    struct signal_mask_bits *internal_oldset_ptr = NULL;

    if (set) {
        bridge_sigset_from_host(set, &internal_set);
        internal_set_ptr = &internal_set;
    }

    if (oldset) {
        internal_oldset_ptr = &internal_oldset;
    }

    int result = do_sigprocmask(how, internal_set_ptr, internal_oldset_ptr);

    if (oldset && result == 0) {
        bridge_sigset_to_host(&internal_oldset, oldset);
    }

    return result;
}

__attribute__((visibility("default"))) int sigpending(sigset_t *set) {
    if (!set) {
        errno = EFAULT;
        return -1;
    }

    struct signal_mask_bits internal_set;
    int result = do_sigpending(&internal_set);

    if (result == 0) {
        bridge_sigset_to_host(&internal_set, set);
    }

    return result;
}

__attribute__((visibility("default"))) int sigsuspend(const sigset_t *mask) {
    if (!mask) {
        errno = EFAULT;
        return -1;
    }

    struct signal_mask_bits internal_mask;
    bridge_sigset_from_host(mask, &internal_mask);

    return do_sigsuspend(&internal_mask);
}

__attribute__((visibility("default"))) int raise(int sig) {
    return do_raise(sig);
}

__attribute__((visibility("default"))) int pause(void) {
    return do_pause();
}
