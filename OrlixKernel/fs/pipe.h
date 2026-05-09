#ifndef PIPE_H
#define PIPE_H

#include <stdbool.h>
#include <stddef.h>

#include <linux/types.h>

#include "fdtable.h"

#ifdef __cplusplus
extern "C" {
#endif

struct wait_queue_head;
struct pipe_endpoint;

int pipe_impl(int pipefd[2]);
int pipe2_impl(int pipefd[2], int flags);

int pipe_create_endpoint_pair(struct pipe_endpoint **read_end, struct pipe_endpoint **write_end);
void pipe_close_endpoint_impl(struct pipe_endpoint *endpoint);
unsigned long long pipe_endpoint_id_impl(struct pipe_endpoint *endpoint);
bool pipe_endpoint_is_read_end_impl(struct pipe_endpoint *endpoint);
__kernel_ssize_t pipe_read_endpoint_impl(struct pipe_endpoint *endpoint, void *buf, size_t count, bool nonblock);
__kernel_ssize_t pipe_write_endpoint_impl(struct pipe_endpoint *endpoint, const void *buf, size_t count, bool nonblock);
__kernel_ssize_t pipe_peek_endpoint_impl(struct pipe_endpoint *endpoint, void *buf, size_t count);
__kernel_ssize_t pipe_tee_between_endpoints_impl(struct pipe_endpoint *src, struct pipe_endpoint *dst, size_t count, bool nonblock);
short pipe_poll_revents_impl(struct pipe_endpoint *endpoint, short events);
short pipe_poll_wait_revents_impl(struct pipe_endpoint *endpoint, short events);
void pipe_poll_wait_queue_impl(struct pipe_endpoint *endpoint, struct wait_queue_head **queue_out);

#ifdef __cplusplus
}
#endif

#endif
