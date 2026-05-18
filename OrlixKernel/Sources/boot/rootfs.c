#include "boot/payload.h"

__attribute__((visibility("hidden"))) int OrlixSelectRootImage(const char *identifier) {
    if (!identifier || identifier[0] == '\0') {
        return -1;
    }
    return 0;
}
