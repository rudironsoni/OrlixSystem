#ifndef ORLIX_MLIBC_SIGNAL_H
#define ORLIX_MLIBC_SIGNAL_H

#include <sys/types.h>

#include <asm-generic/signal.h>
#include <asm-generic/siginfo.h>

typedef __sighandler_t sighandler_t;

static inline int sigemptyset(sigset_t *set) {
    if (!set) {
        return -1;
    }
    for (int i = 0; i < _NSIG_WORDS; i++) {
        set->sig[i] = 0;
    }
    return 0;
}

static inline int sigaddset(sigset_t *set, int signo) {
    if (!set || signo <= 0 || signo > _NSIG) {
        return -1;
    }
    set->sig[(signo - 1) / _NSIG_BPW] |= (1UL << ((signo - 1) % _NSIG_BPW));
    return 0;
}

static inline int sigismember(const sigset_t *set, int signo) {
    if (!set || signo <= 0 || signo > _NSIG) {
        return 0;
    }
    return (set->sig[(signo - 1) / _NSIG_BPW] & (1UL << ((signo - 1) % _NSIG_BPW))) != 0;
}

#endif
