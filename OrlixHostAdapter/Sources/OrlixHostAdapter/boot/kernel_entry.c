#include "OrlixHostAdapter/boot/kernel_entry.h"

#include <dlfcn.h>

__attribute__((visibility("hidden"))) OrlixHostKernelEntrypoint OrlixHostResolveStartKernel(void)
{
    return (OrlixHostKernelEntrypoint)dlsym(RTLD_DEFAULT, "start_kernel");
}
