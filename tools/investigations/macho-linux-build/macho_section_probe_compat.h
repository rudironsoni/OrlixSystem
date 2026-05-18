#ifndef ORLIX_MACHO_SECTION_PROBE_COMPAT_H
#define ORLIX_MACHO_SECTION_PROBE_COMPAT_H

#if __has_include(<linux/compiler_types.h>)
#include <linux/compiler_types.h>
#endif

#if defined(__MACH__)
/*
 * Probe-only mapping for generic __section() users. This deliberately does not
 * solve direct section attributes, percpu section construction, or Mach-O's
 * 16-byte section-name limit; those remain real product linker blockers. This
 * is not the final runtime section placement policy for OrlixKernel.framework.
 */
#undef __section
#define __section(section) __attribute__((__section__("__ORLIX," section)))
#endif

#endif /* ORLIX_MACHO_SECTION_PROBE_COMPAT_H */
