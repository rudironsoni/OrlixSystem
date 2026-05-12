#include <errno.h>
#include <setjmp.h>
#include <stdlib.h>

#include "../../OrlixKernel/private/kernel/fork_frame_state.h"

struct fork_frame_impl {
    jmp_buf buf;
};

int fork_frame_init(fork_frame_t *frame) {
    struct fork_frame_impl *impl;

    if (!frame) {
        return -EINVAL;
    }
    if (frame->impl) {
        return 0;
    }
    impl = malloc(sizeof(*impl));
    if (!impl) {
        return -ENOMEM;
    }
    frame->impl = impl;
    return 0;
}

void fork_frame_destroy(fork_frame_t *frame) {
    if (!frame || !frame->impl) {
        return;
    }
    free(frame->impl);
    frame->impl = NULL;
}

int fork_frame_save(fork_frame_t *frame) {
    if (!frame || !frame->impl) {
        return -EINVAL;
    }
    return setjmp(((struct fork_frame_impl *)frame->impl)->buf);
}

void fork_frame_restore(fork_frame_t *frame, int value) {
    if (!frame || !frame->impl) {
        abort();
    }
    longjmp(((struct fork_frame_impl *)frame->impl)->buf, value);
}
