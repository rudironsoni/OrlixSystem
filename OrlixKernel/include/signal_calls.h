#ifndef SIGNAL_CALLS_H
#define SIGNAL_CALLS_H

#include <linux/signal.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KERNEL_SIG_NUM _NSIG
#define KERNEL_SIG_NUM_WORDS _NSIG_WORDS

int do_sigaction(int sig, const struct sigaction *act, struct sigaction *oldact);
int do_sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
int do_sigsetmask(const sigset_t *set, sigset_t *oldset);
int do_sigpending(sigset_t *set);
int do_signal(int signum, __sighandler_t handler, __sighandler_t *old_handler);
int do_raise(int sig);
int do_pause(void);
int do_sigsuspend(const sigset_t *mask);
int do_kill(int pid, int sig);
int do_killpg(int pgrp, int sig);

#ifdef __cplusplus
}
#endif

#endif
