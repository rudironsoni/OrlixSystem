#ifndef ORLIX_KERNEL_H
#define ORLIX_KERNEL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct boot_params {
    const char *cmdline;
    uintptr_t memory_base;
    size_t memory_size;
    const void *initrd_base;
    size_t initrd_size;
    const void *dtb_base;
    size_t dtb_size;
    const char *root_device;
    const char *console_device;
    unsigned long flags;
};

__attribute__((visibility("default"))) int OrlixBoot(const struct boot_params *params);

#ifdef __cplusplus
}
#endif

#endif
