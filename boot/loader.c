#include "OrlixKernel.h"

__attribute__((visibility("hidden"))) int OrlixPrepareBootConfig(const struct OrlixBootConfig *config);

int OrlixBoot(const struct OrlixBootConfig *config) {
    if (OrlixPrepareBootConfig(config) != 0) {
        return -1;
    }

    return -1;
}
