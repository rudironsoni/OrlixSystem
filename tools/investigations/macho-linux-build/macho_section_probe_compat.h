#ifndef ORLIX_MACHO_SECTION_PROBE_COMPAT_H
#define ORLIX_MACHO_SECTION_PROBE_COMPAT_H

#if __has_include(<linux/compiler_types.h>)
#include <linux/compiler_types.h>
#endif

#if __has_include(<linux/compiler.h>)
#include <linux/compiler.h>
#endif

#if __has_include(<linux/percpu-defs.h>)
#include <linux/percpu-defs.h>
#endif

#if defined(__MACH__)
/*
 * Probe-only mapping for generic __section() users. This deliberately does not
 * solve arbitrary direct section attributes or Mach-O's 16-byte section-name
 * limit; those remain real product linker blockers. This is not the final
 * runtime section placement policy for OrlixKernel.framework.
 */
#undef __section
#define __section(section) __attribute__((__section__("__ORLIX," section)))

/* Linux's generic name is longer than Mach-O's 16-byte section-name limit. */
#ifndef __ro_after_init
#define __ro_after_init __attribute__((__section__("__ORLIX,__roinit")))
#endif

/* EXPORT_SYMBOL() uses __ADDRESSABLE() with .discard.addressable. */
#ifdef __ADDRESSABLE
#undef __ADDRESSABLE
#define __ADDRESSABLE(sym) \
	___ADDRESSABLE(sym, __attribute__((__section__("__ORLIX,__addr"))))
#endif

/* Probe-only mapping for percpu declarations using section(PER_CPU_BASE_SECTION sec). */
#ifndef PER_CPU_BASE_SECTION
#define PER_CPU_BASE_SECTION "__ORLIX,__percpu"
#endif
#ifdef __PCPU_ATTRS
#undef __PCPU_ATTRS
#define __PCPU_ATTRS(sec) \
	__percpu __attribute__((section("__ORLIX,__percpu"))) PER_CPU_ATTRIBUTES
#endif
#endif

#endif /* ORLIX_MACHO_SECTION_PROBE_COMPAT_H */
