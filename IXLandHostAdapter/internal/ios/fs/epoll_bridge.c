/* internal/ios/fs/epoll_bridge.c
 * Host bridge for epoll public API wrappers and signal mask operations
 */

#include <signal.h>
#include <stdbool.h>
#include <string.h>

#include "epoll_bridge.h"

typedef struct epoll_sigmask_state_internal {
    sigset_t oldmask;
    bool saved;
} epoll_sigmask_state_internal_t;

/* Ensure internal struct fits in opaque state */
_Static_assert(sizeof(epoll_sigmask_state_internal_t) <= sizeof(epoll_sigmask_state_t),
               "epoll_sigmask_state_t must be large enough for internal state");

bool epoll_sigmask_save(epoll_sigmask_state_t *state, const sigset_t *sigmask) {
    if (!state)
        return false;

    epoll_sigmask_state_internal_t *internal = (epoll_sigmask_state_internal_t *)state;
    internal->saved = false;

    if (sigmask) {
        sigset_t newmask;
        memset(&newmask, 0, sizeof(newmask));
        memcpy(&newmask, sigmask, sizeof(newmask) < 128 ? sizeof(newmask) : 128);
        pthread_sigmask(SIG_SETMASK, &newmask, &internal->oldmask);
        internal->saved = true;
    }

    return internal->saved;
}

void epoll_sigmask_restore(epoll_sigmask_state_t *state) {
    if (!state)
        return;

    epoll_sigmask_state_internal_t *internal = (epoll_sigmask_state_internal_t *)state;

    if (internal->saved) {
        pthread_sigmask(SIG_SETMASK, &internal->oldmask, NULL);
        internal->saved = false;
    }
}
