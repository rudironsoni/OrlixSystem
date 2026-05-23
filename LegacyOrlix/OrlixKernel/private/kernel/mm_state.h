#ifndef PRIVATE_KERNEL_MM_STATE_H
#define PRIVATE_KERNEL_MM_STATE_H

#include "kernel/mm.h"

#ifdef __cplusplus
extern "C" {
#endif

void mm_note_file_truncate_impl(int fd, int64_t length);

#ifdef __cplusplus
}
#endif

#endif
