/* internal/ios/fs/epoll_bridge.h
 * Host bridge for epoll signal mask operations
 *
 * Narrow seam: only sigmask save/restore for epoll_pwait
 */

#ifndef EPOLL_BRIDGE_H
#define EPOLL_BRIDGE_H

#include <stdbool.h>
#include <signal.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque sigmask state for epoll_pwait */
typedef struct epoll_sigmask_state {
    char opaque[136]; /* Large enough for internal state */
} epoll_sigmask_state_t;

/* Save current sigmask and install new one if sigmask provided.
 * Returns true if sigmask was saved (and must be restored later). */
bool epoll_sigmask_save(epoll_sigmask_state_t *state, const sigset_t *sigmask);

/* Restore saved sigmask if one was saved */
void epoll_sigmask_restore(epoll_sigmask_state_t *state);

#ifdef __cplusplus
}
#endif

#endif /* EPOLL_BRIDGE_H */
