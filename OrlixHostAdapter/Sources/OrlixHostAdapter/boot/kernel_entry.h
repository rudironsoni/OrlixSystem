#ifndef ORLIX_HOST_ADAPTER_BOOT_KERNEL_ENTRY_H
#define ORLIX_HOST_ADAPTER_BOOT_KERNEL_ENTRY_H

typedef void (*OrlixHostKernelEntrypoint)(void);

__attribute__((visibility("hidden"))) OrlixHostKernelEntrypoint OrlixHostResolveStartKernel(void);

#endif
