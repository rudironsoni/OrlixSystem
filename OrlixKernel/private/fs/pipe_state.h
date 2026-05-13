#ifndef PRIVATE_FS_PIPE_STATE_H
#define PRIVATE_FS_PIPE_STATE_H

#include "fs/pipe.h"

#ifdef __cplusplus
extern "C" {
#endif

struct wait_queue_head;
struct pipe_endpoint;

void pipe_close_endpoint_impl(struct pipe_endpoint *endpoint);
unsigned long long pipe_endpoint_id_impl(struct pipe_endpoint *endpoint);
bool pipe_endpoint_is_read_end_impl(struct pipe_endpoint *endpoint);
__kernel_ssize_t pipe_read_endpoint_impl(struct pipe_endpoint *endpoint, void *buf, size_t count,
                                         bool nonblock);
__kernel_ssize_t pipe_write_endpoint_impl(struct pipe_endpoint *endpoint, const void *buf,
                                          size_t count, bool nonblock);
__kernel_ssize_t pipe_peek_endpoint_impl(struct pipe_endpoint *endpoint, void *buf, size_t count);
__kernel_ssize_t pipe_tee_between_endpoints_impl(struct pipe_endpoint *src,
                                                 struct pipe_endpoint *dst, size_t count,
                                                 bool nonblock);
short pipe_poll_revents_impl(struct pipe_endpoint *endpoint, short events);
short pipe_poll_wait_revents_impl(struct pipe_endpoint *endpoint, short events);
void pipe_poll_wait_queue_impl(struct pipe_endpoint *endpoint, struct wait_queue_head **queue_out);

#ifdef __cplusplus
}
#endif

#endif
