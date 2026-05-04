/*
 * LinuxKHeadersCompileSmoke.c
 *
 * KERNEL HEADER COMPILE TEST
 *
 * This file proves that vendored Linux kernel headers resolve correctly
 * through Linux srctree/objtree-style include paths. It is a pure C
 * compile-smoke test and does not prove runtime behavior.
 */

#include <generated/autoconf.h>
#include <linux/kconfig.h>
#include <asm/unistd.h>

#ifndef CONFIG_ARM64
#error "CONFIG_ARM64 not defined from generated/autoconf.h"
#endif

#ifndef __ARCH_WANT_SYS_CLONE
#error "__ARCH_WANT_SYS_CLONE not defined from asm/unistd.h"
#endif

#ifndef NR_syscalls
#error "NR_syscalls not defined from asm/unistd.h"
#endif

static unsigned long kheaders_compile_smoke_sys_clone(void) {
    return 1;
}

static unsigned long kheaders_compile_smoke_nr_syscalls(void) {
    return NR_syscalls;
}

__attribute__((unused)) static void (*volatile kheaders_smoke_refs[])(void) = {
    (void (*)(void))kheaders_compile_smoke_sys_clone,
    (void (*)(void))kheaders_compile_smoke_nr_syscalls,
};
