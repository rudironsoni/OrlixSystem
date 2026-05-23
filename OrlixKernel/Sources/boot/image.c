#include "boot/payload.h"

__attribute__((visibility("hidden"))) int OrlixLoadKernelImage(const void *image,
                                                                unsigned long image_size) {
    if (!image || image_size == 0) {
        return -1;
    }
    return 0;
}
