/* internal/ios/fs/poll_host.h
 * Narrow seam for poll/select host operations
 */

#ifndef POLL_HOST_H
#define POLL_HOST_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration for pollfd */
struct pollfd;

/* Poll host operation */
int host_poll_impl(struct pollfd *fds, unsigned int nfds, int timeout);

#ifdef __cplusplus
}
#endif

#endif /* POLL_HOST_H */
