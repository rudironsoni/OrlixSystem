#ifndef ORLIX_LINUX_HOST_COMPAT_MACHO_COMPAT_H
#define ORLIX_LINUX_HOST_COMPAT_MACHO_COMPAT_H

#if defined(__APPLE__) && defined(__MACH__) && (defined(__KERNEL__) || defined(ORLIX_LINT_KERNEL_TEST))

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

#ifndef BUILD_VDSO
#define BUILD_VDSO 1
#endif

#include <linux/compiler_types.h>
#include <linux/compiler_attributes.h>

#ifndef __always_inline
#define __always_inline inline
#endif

#ifndef __latent_entropy
#define __latent_entropy
#endif

#ifdef __cold
#undef __cold
#endif
#define __cold

#ifndef likely
#define likely(x)   __builtin_expect(!!(x), 1)
#endif

#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

#ifndef unreachable
#define unreachable() __builtin_unreachable()
#endif

#ifndef __no_kasan_or_inline
#define __no_kasan_or_inline __always_inline
#endif

#ifndef __no_sanitize_or_inline
#define __no_sanitize_or_inline __always_inline
#endif

#undef __section
#define __section(section)

#ifndef __ASM_ALTERNATIVE_MACROS_H
#define __ASM_ALTERNATIVE_MACROS_H

#ifndef ALTERNATIVE
#define ALTERNATIVE(oldinstr, newinstr, ...) oldinstr
#endif

#ifndef ALTERNATIVE_CB
#define ALTERNATIVE_CB(oldinstr, cpucap, cb) oldinstr
#endif

static __always_inline int alternative_has_cap_likely(const unsigned long cpucap) {
    (void)cpucap;
    return 0;
}

static __always_inline int alternative_has_cap_unlikely(const unsigned long cpucap) {
    (void)cpucap;
    return 0;
}
#endif

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

#undef __section
#define __section(section)

#endif

#endif
