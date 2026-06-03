$(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB): $(REPO_ROOT)/OrlixMLibC/Makefile $(ORLIXOS_MLIBC_PATCHES)
	@set -euo pipefail; \
	$(MLIBC_MAKE) build PROFILE="$(PROFILE)"; \
	[ -s "$(ORLIXOS_MLIBC_SYSROOT)/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $(ORLIXOS_MLIBC_SYSROOT)/usr/lib/libc.a" >&2; exit 1; }; \
	[ -s "$(ORLIXOS_MLIBC_RTLIB)" ] || { echo "missing Orlix compiler runtime archive: $(ORLIXOS_MLIBC_RTLIB)" >&2; exit 1; }
