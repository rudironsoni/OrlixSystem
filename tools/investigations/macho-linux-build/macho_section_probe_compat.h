#ifndef ORLIX_MACHO_SECTION_PROBE_COMPAT_H
#define ORLIX_MACHO_SECTION_PROBE_COMPAT_H

#if __has_include(<linux/compiler_types.h>)
#include <linux/compiler_types.h>
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

/* Probe-only mapping for percpu declarations using section(PER_CPU_BASE_SECTION sec). */
#ifndef PER_CPU_BASE_SECTION
#define PER_CPU_BASE_SECTION "__ORLIX,__percpu"
#endif
#endif

#endif /* ORLIX_MACHO_SECTION_PROBE_COMPAT_H */
