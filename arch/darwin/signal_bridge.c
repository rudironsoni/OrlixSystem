/* IXLandKernel/arch/darwin/signal_bridge.c
 * Darwin host bridge for signal type conversions
 *
 * This file is ONLY for host-mediation. It includes Darwin
 * headers and provides private bridge helpers that convert
 * between Darwin types and IXLand internal types.
 *
 * The Linux-facing public signal contract is owned by kernel/signal.c
 */

#include <signal.h>
#include <string.h>

#include "kernel/signal.h"

/* Signal constants - must match kernel/signal.h */
#ifndef IXLAND_SIG_NUM
#define IXLAND_SIG_NUM 64
#endif
#ifndef IXLAND_SIG_NUM_WORDS
#define IXLAND_SIG_NUM_WORDS ((IXLAND_SIG_NUM + 63) / 64)
#endif

/* Private bridge helpers - used by kernel/signal.c public wrappers */

/* Convert Darwin sigset_t to internal signal_mask_bits */
void bridge_sigset_from_host(const sigset_t *host_set, struct signal_mask_bits *out_set) {
    memset(out_set, 0, sizeof(*out_set));
    if (!host_set) return;

    for (int sig = 1; sig <= 64; sig++) {
        if (sigismember(host_set, sig)) {
            int idx = (sig - 1) / 64;  // Fixed: signal 64 now maps to idx 0, bit 63
            int bit = (sig - 1) % 64;
            if (idx < IXLAND_SIG_NUM_WORDS && bit < 64) {
                out_set->sig[idx] |= (1ULL << bit);
            }
        }
    }
}

/* Convert internal signal_mask_bits to Darwin sigset_t */
void bridge_sigset_to_host(const struct signal_mask_bits *internal_set, sigset_t *host_set) {
    if (!host_set) return;
    sigemptyset(host_set);
    if (!internal_set) return;

    for (int sig = 1; sig <= 64; sig++) {
        int idx = (sig - 1) / 64;  // Fixed: signal 64 now maps to idx 0, bit 63
        int bit = (sig - 1) % 64;
        if (idx < IXLAND_SIG_NUM_WORDS && (internal_set->sig[idx] & (1ULL << bit))) {
            sigaddset(host_set, sig);
        }
    }
}

/* Convert Darwin struct sigaction to internal signal_action_slot */
void bridge_signal_from_host(const struct sigaction *host_act, struct signal_action_slot *out_act) {
    memset(out_act, 0, sizeof(*out_act));
    if (!host_act) return;

    out_act->handler = host_act->sa_handler;
    bridge_sigset_from_host(&host_act->sa_mask, &out_act->mask);
    out_act->flags = host_act->sa_flags;
}

/* Convert internal signal_action_slot to Darwin struct sigaction */
void bridge_signal_to_host(const struct signal_action_slot *internal_act, struct sigaction *host_act) {
    memset(host_act, 0, sizeof(*host_act));
    if (!internal_act) return;

    host_act->sa_handler = internal_act->handler;
    bridge_sigset_to_host(&internal_act->mask, &host_act->sa_mask);
    host_act->sa_flags = internal_act->flags;
}

/* No public Linux-facing symbols exported from this file.
 * The public contract is owned by kernel/signal.c
 */
