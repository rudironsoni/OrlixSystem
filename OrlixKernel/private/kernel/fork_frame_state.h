#ifndef PRIVATE_KERNEL_FORK_FRAME_STATE_H
#define PRIVATE_KERNEL_FORK_FRAME_STATE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fork_frame {
    void *impl;
} fork_frame_t;

int fork_frame_init(fork_frame_t *frame);
void fork_frame_destroy(fork_frame_t *frame);
int fork_frame_save(fork_frame_t *frame);
void fork_frame_restore(fork_frame_t *frame, int value) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif

#endif
