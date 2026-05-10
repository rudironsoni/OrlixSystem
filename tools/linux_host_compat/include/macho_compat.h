#ifndef ORLIX_LINUX_HOST_COMPAT_MACHO_COMPAT_H
#define ORLIX_LINUX_HOST_COMPAT_MACHO_COMPAT_H

#if defined(__APPLE__) && defined(__MACH__) && defined(__KERNEL__)

/*
 * Xcode drives the OrlixKernel target through an Apple-clang Mach-O frontend.
 * Preload the vendored Linux compiler attribute graph here so later includes
 * see Linux-owned macros first, then neutralize ELF-only section attributes
 * that Apple clang rejects for Mach-O simulator objects.
 */

#ifdef __weak
#undef __weak
#endif

#ifdef __CONCAT
#undef __CONCAT
#endif

#ifdef SIZE_MAX
#undef SIZE_MAX
#endif

#ifdef UINTPTR_MAX
#undef UINTPTR_MAX
#endif

#include <linux/compiler_types.h>

#undef __section
#define __section(section)

#include <linux/args.h>
#include <linux/cache.h>
#include <linux/init.h>
#include <linux/limits.h>
#include <linux/percpu-defs.h>
#include <vdso/limits.h>

#undef __cacheline_aligned
#define __cacheline_aligned __aligned(L1_CACHE_BYTES)

#undef ____cacheline_internodealigned_in_smp
#define ____cacheline_internodealigned_in_smp __aligned(L1_CACHE_BYTES)

#undef __cacheline_aligned_in_smp
#define __cacheline_aligned_in_smp __cacheline_aligned

#undef __PCPU_ATTRS
#define __PCPU_ATTRS(sec) __percpu

#endif

#endif
