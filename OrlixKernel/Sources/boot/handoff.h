#ifndef ORLIX_BOOT_HANDOFF_H
#define ORLIX_BOOT_HANDOFF_H

#include "boot/input.h"

__attribute__((visibility("hidden"))) int OrlixBootHandoff(
    const struct OrlixBootInput *input);

#if defined(ORLIX_BOOT_TESTING)
void OrlixBootResetHandoff(void);
int OrlixBootHandoffCount(void);
const struct OrlixBootInput *OrlixBootLastHandoff(void);
#endif

#endif
