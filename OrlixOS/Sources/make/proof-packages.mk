$(ORLIXOS_GREP_BINARY): $(ORLIXOS_GREP_SOURCE_STAMP) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB)
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	rtlib="$(ORLIXOS_MLIBC_RTLIB)"; \
	[ -s "$$sysroot/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $$sysroot/usr/lib/libc.a" >&2; exit 1; }; \
	[ -d "$$headers" ] || { echo "missing Orlix Linux UAPI headers: $$headers" >&2; exit 1; }; \
	[ -s "$$rtlib" ] || { echo "missing Orlix compiler runtime archive: $$rtlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_CC)" >/dev/null 2>&1 || { echo "clang is required to build grep; set ORLIXOS_CC=/path/to/clang" >&2; exit 1; }; \
	command -v "$(ORLIXOS_AR)" >/dev/null 2>&1 || { echo "llvm-ar is required to build grep; set ORLIXOS_AR=/path/to/llvm-ar" >&2; exit 1; }; \
	command -v "$(ORLIXOS_RANLIB)" >/dev/null 2>&1 || { echo "llvm-ranlib is required to build grep; set ORLIXOS_RANLIB=/path/to/llvm-ranlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_STRIP)" >/dev/null 2>&1 || { echo "llvm-strip is required to package grep; set ORLIXOS_STRIP=/path/to/llvm-strip" >&2; exit 1; }; \
	rm -rf "$(ORLIXOS_GREP_BUILD_DIR)" "$(ORLIXOS_GREP_BINARY)"; \
	mkdir -p "$(ORLIXOS_GREP_BUILD_DIR)" "$(dir $(ORLIXOS_GREP_BINARY))"; \
	cd "$(ORLIXOS_GREP_BUILD_DIR)"; \
	export CC="$(ORLIXOS_CC) --target=aarch64-linux-gnu --sysroot=$$sysroot -isystem $$headers -D_GNU_SOURCE -fhosted -fno-builtin -femulated-tls -ffixed-x18 -fno-pie"; \
	export CFLAGS="$(ORLIXOS_PACKAGE_CFLAGS)"; \
	export LDFLAGS="--target=aarch64-linux-gnu --sysroot=$$sysroot -static -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--image-base=$(ORLIXOS_HOSTED_USER_BASE_ADDRESS) $$sysroot/usr/lib/crt1.o $$sysroot/usr/lib/crti.o -Wl,--start-group"; \
	export LIBS="$$sysroot/usr/lib/libc.a $$sysroot/usr/lib/libm.a $$sysroot/usr/lib/libpthread.a $$sysroot/usr/lib/libssp_nonshared.a $$sysroot/usr/lib/libssp.a $$rtlib -Wl,--end-group $$sysroot/usr/lib/crtn.o"; \
	export AR="$(ORLIXOS_AR)"; \
	export RANLIB="$(ORLIXOS_RANLIB)"; \
	export gl_cv_func_getopt_gnu=yes; \
	export gl_cv_func_getopt_long_gnu=yes; \
	"$(ORLIXOS_GREP_SRC_DIR)/configure" --host=aarch64-linux-gnu --build=aarch64-apple-darwin --prefix=/usr --disable-nls --disable-perl-regexp --disable-gcc-warnings; \
	$(MAKE) -C lib -j1 all; \
	$(MAKE) -C src -j1 grep; \
	cp "$(ORLIXOS_GREP_BUILD_DIR)/src/grep" "$(ORLIXOS_GREP_BINARY)"; \
	"$(ORLIXOS_STRIP)" "$(ORLIXOS_GREP_BINARY)"; \
	file "$(ORLIXOS_GREP_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_GREP_BINARY)" >&2; exit 1; }; \
	printf 'profile=%s\ndistribution=%s\nchannel=%s\npackage=grep\nversion=%s\nsha256=%s\n' "$(PROFILE)" "$(ORLIXOS_DISTRIBUTION_ID)" "$(ORLIXOS_DISTRIBUTION_CHANNEL)" "$(GREP_VERSION)" "$(GREP_SHA256)" > "$(ORLIXOS_PACKAGE_INSTALL_DIR)/grep.proof"; \
	rm -rf "$(ORLIXOS_GREP_BUILD_DIR)"; \
	echo "built Orlix Linux grep package input: $(ORLIXOS_GREP_BINARY)"

$(ORLIXOS_SED_BINARY): $(ORLIXOS_SED_SOURCE_STAMP) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB)
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	rtlib="$(ORLIXOS_MLIBC_RTLIB)"; \
	[ -s "$$sysroot/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $$sysroot/usr/lib/libc.a" >&2; exit 1; }; \
	[ -d "$$headers" ] || { echo "missing Orlix Linux UAPI headers: $$headers" >&2; exit 1; }; \
	[ -s "$$rtlib" ] || { echo "missing Orlix compiler runtime archive: $$rtlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_CC)" >/dev/null 2>&1 || { echo "clang is required to build sed; set ORLIXOS_CC=/path/to/clang" >&2; exit 1; }; \
	command -v "$(ORLIXOS_AR)" >/dev/null 2>&1 || { echo "llvm-ar is required to build sed; set ORLIXOS_AR=/path/to/llvm-ar" >&2; exit 1; }; \
	command -v "$(ORLIXOS_RANLIB)" >/dev/null 2>&1 || { echo "llvm-ranlib is required to build sed; set ORLIXOS_RANLIB=/path/to/llvm-ranlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_STRIP)" >/dev/null 2>&1 || { echo "llvm-strip is required to package sed; set ORLIXOS_STRIP=/path/to/llvm-strip" >&2; exit 1; }; \
	rm -rf "$(ORLIXOS_SED_BUILD_DIR)" "$(ORLIXOS_SED_BINARY)"; \
	mkdir -p "$(ORLIXOS_SED_BUILD_DIR)" "$(dir $(ORLIXOS_SED_BINARY))"; \
	cd "$(ORLIXOS_SED_BUILD_DIR)"; \
	export CC="$(ORLIXOS_CC) --target=aarch64-linux-gnu --sysroot=$$sysroot -isystem $$headers -D_GNU_SOURCE -fhosted -fno-builtin -femulated-tls -ffixed-x18 -fno-pie"; \
	export CFLAGS="$(ORLIXOS_PACKAGE_CFLAGS)"; \
	export LDFLAGS="--target=aarch64-linux-gnu --sysroot=$$sysroot -static -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--image-base=$(ORLIXOS_HOSTED_USER_BASE_ADDRESS) $$sysroot/usr/lib/crt1.o $$sysroot/usr/lib/crti.o -Wl,--start-group"; \
	export LIBS="$$sysroot/usr/lib/libc.a $$sysroot/usr/lib/libm.a $$sysroot/usr/lib/libpthread.a $$sysroot/usr/lib/libssp_nonshared.a $$sysroot/usr/lib/libssp.a $$rtlib -Wl,--end-group $$sysroot/usr/lib/crtn.o"; \
	export AR="$(ORLIXOS_AR)"; \
	export RANLIB="$(ORLIXOS_RANLIB)"; \
	export gl_cv_func_mbrtowc_incomplete_state=yes; \
	export gl_cv_func_mbrtowc_sanitycheck=yes; \
	export gl_cv_func_mbrtowc_null_arg1=yes; \
	export gl_cv_func_mbrtowc_null_arg2=yes; \
	export gl_cv_func_mbrtowc_retval=yes; \
	export gl_cv_func_mbrtowc_nul_retval=yes; \
	export gl_cv_func_mbrtowc_stores_incomplete=no; \
	export gl_cv_func_mbrtowc_empty_input=yes; \
	export gl_cv_func_mbrtowc_C_locale_sans_EILSEQ=no; \
	"$(ORLIXOS_SED_SRC_DIR)/configure" --host=aarch64-linux-gnu --build=aarch64-apple-darwin --prefix=/usr --disable-nls --disable-acl --disable-gcc-warnings; \
	$(MAKE) -j1 all; \
	$(MAKE) -j1 install DESTDIR="$(ORLIXOS_PACKAGE_INSTALL_DIR)"; \
	"$(ORLIXOS_STRIP)" "$(ORLIXOS_SED_BINARY)"; \
	file "$(ORLIXOS_SED_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_SED_BINARY)" >&2; exit 1; }; \
	printf 'profile=%s\ndistribution=%s\nchannel=%s\npackage=sed\nversion=%s\nsha256=%s\nregex=bundled-gnulib\n' "$(PROFILE)" "$(ORLIXOS_DISTRIBUTION_ID)" "$(ORLIXOS_DISTRIBUTION_CHANNEL)" "$(SED_VERSION)" "$(SED_SHA256)" > "$(ORLIXOS_PACKAGE_INSTALL_DIR)/sed.proof"; \
	if [ "$(ORLIXOS_KEEP_SED_BUILD)" != "1" ]; then rm -rf "$(ORLIXOS_SED_BUILD_DIR)"; fi; \
	echo "built Orlix Linux sed package input: $(ORLIXOS_SED_BINARY)"

$(ORLIXOS_DIFF_BINARY): $(ORLIXOS_DIFFUTILS_SOURCE_STAMP) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB)
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	rtlib="$(ORLIXOS_MLIBC_RTLIB)"; \
	[ -s "$$sysroot/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $$sysroot/usr/lib/libc.a" >&2; exit 1; }; \
	[ -d "$$headers" ] || { echo "missing Orlix Linux UAPI headers: $$headers" >&2; exit 1; }; \
	[ -s "$$rtlib" ] || { echo "missing Orlix compiler runtime archive: $$rtlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_CC)" >/dev/null 2>&1 || { echo "clang is required to build diffutils; set ORLIXOS_CC=/path/to/clang" >&2; exit 1; }; \
	command -v "$(ORLIXOS_AR)" >/dev/null 2>&1 || { echo "llvm-ar is required to build diffutils; set ORLIXOS_AR=/path/to/llvm-ar" >&2; exit 1; }; \
	command -v "$(ORLIXOS_RANLIB)" >/dev/null 2>&1 || { echo "llvm-ranlib is required to build diffutils; set ORLIXOS_RANLIB=/path/to/llvm-ranlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_STRIP)" >/dev/null 2>&1 || { echo "llvm-strip is required to package diffutils; set ORLIXOS_STRIP=/path/to/llvm-strip" >&2; exit 1; }; \
	rm -rf "$(ORLIXOS_DIFFUTILS_BUILD_DIR)"; \
	for program in $(ORLIXOS_DIFFUTILS_PROGRAMS); do rm -f "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program"; done; \
	mkdir -p "$(ORLIXOS_DIFFUTILS_BUILD_DIR)" "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin"; \
	cd "$(ORLIXOS_DIFFUTILS_BUILD_DIR)"; \
	export CC="$(ORLIXOS_CC) --target=aarch64-linux-gnu --sysroot=$$sysroot -isystem $$headers -D_GNU_SOURCE -fhosted -fno-builtin -femulated-tls -ffixed-x18 -fno-pie"; \
	export CFLAGS="$(ORLIXOS_PACKAGE_CFLAGS)"; \
	export LDFLAGS="--target=aarch64-linux-gnu --sysroot=$$sysroot -static -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--image-base=$(ORLIXOS_HOSTED_USER_BASE_ADDRESS) $$sysroot/usr/lib/crt1.o $$sysroot/usr/lib/crti.o -Wl,--start-group"; \
	export LIBS="$$sysroot/usr/lib/libc.a $$sysroot/usr/lib/libm.a $$sysroot/usr/lib/libpthread.a $$sysroot/usr/lib/libssp_nonshared.a $$sysroot/usr/lib/libssp.a $$rtlib -Wl,--end-group $$sysroot/usr/lib/crtn.o"; \
	export AR="$(ORLIXOS_AR)"; \
	export RANLIB="$(ORLIXOS_RANLIB)"; \
	export gl_cv_func_getopt_gnu=yes; \
	export gl_cv_func_getopt_long_gnu=yes; \
	export gl_cv_func_strcasecmp_works=yes; \
	"$(ORLIXOS_DIFFUTILS_SRC_DIR)/configure" --host=aarch64-linux-gnu --build=aarch64-apple-darwin --prefix=/usr --disable-nls --disable-gcc-warnings; \
	$(MAKE) -j1 all; \
	$(MAKE) -j1 install DESTDIR="$(ORLIXOS_PACKAGE_INSTALL_DIR)"; \
	for program in $(ORLIXOS_DIFFUTILS_PROGRAMS); do \
		"$(ORLIXOS_STRIP)" "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program"; \
		file "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program" >&2; exit 1; }; \
	done; \
	printf 'profile=%s\ndistribution=%s\nchannel=%s\npackage=diffutils\nversion=%s\nsha256=%s\nprograms=%s\n' "$(PROFILE)" "$(ORLIXOS_DISTRIBUTION_ID)" "$(ORLIXOS_DISTRIBUTION_CHANNEL)" "$(DIFFUTILS_VERSION)" "$(DIFFUTILS_SHA256)" "$(ORLIXOS_DIFFUTILS_PROGRAMS)" > "$(ORLIXOS_PACKAGE_INSTALL_DIR)/diffutils.proof"; \
	rm -rf "$(ORLIXOS_DIFFUTILS_BUILD_DIR)"; \
	echo "built Orlix Linux diffutils package inputs: $(ORLIXOS_DIFFUTILS_PROGRAMS)"

$(ORLIXOS_GAWK_BINARY): $(ORLIXOS_GAWK_SOURCE_STAMP) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB) $(PROJECT_DIR)/Sources/make/config.mk $(PROJECT_DIR)/Sources/make/proof-packages.mk $(ORLIXOS_PACKAGE_TOOLCHAIN_SCRIPT)
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	rtlib="$(ORLIXOS_MLIBC_RTLIB)"; \
	[ -s "$$sysroot/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $$sysroot/usr/lib/libc.a" >&2; exit 1; }; \
	[ -d "$$headers" ] || { echo "missing Orlix Linux UAPI headers: $$headers" >&2; exit 1; }; \
	[ -s "$$rtlib" ] || { echo "missing Orlix compiler runtime archive: $$rtlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_CC)" >/dev/null 2>&1 || { echo "clang is required to build gawk; set ORLIXOS_CC=/path/to/clang" >&2; exit 1; }; \
	command -v "$(ORLIXOS_AR)" >/dev/null 2>&1 || { echo "llvm-ar is required to build gawk; set ORLIXOS_AR=/path/to/llvm-ar" >&2; exit 1; }; \
	command -v "$(ORLIXOS_LD)" >/dev/null 2>&1 || { echo "ld.lld is required to build gawk; set ORLIXOS_LD=/path/to/ld.lld" >&2; exit 1; }; \
	command -v "$(ORLIXOS_RANLIB)" >/dev/null 2>&1 || { echo "llvm-ranlib is required to build gawk; set ORLIXOS_RANLIB=/path/to/llvm-ranlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_NM)" >/dev/null 2>&1 || { echo "llvm-nm is required to build gawk; set ORLIXOS_NM=/path/to/llvm-nm" >&2; exit 1; }; \
	command -v "$(ORLIXOS_STRIP)" >/dev/null 2>&1 || { echo "llvm-strip is required to package gawk; set ORLIXOS_STRIP=/path/to/llvm-strip" >&2; exit 1; }; \
	command -v "$(ORLIXOS_OBJDUMP)" >/dev/null 2>&1 || { echo "llvm-objdump is required to build gawk; set ORLIXOS_OBJDUMP=/path/to/llvm-objdump" >&2; exit 1; }; \
	command -v "$(ORLIXOS_READELF)" >/dev/null 2>&1 || { echo "llvm-readelf is required to build gawk; set ORLIXOS_READELF=/path/to/llvm-readelf" >&2; exit 1; }; \
	rm -rf "$(ORLIXOS_GAWK_BUILD_DIR)" "$(ORLIXOS_GAWK_TOOLCHAIN_DIR)" "$(ORLIXOS_GAWK_BINARY)"; \
	mkdir -p "$(ORLIXOS_GAWK_BUILD_DIR)" "$(ORLIXOS_GAWK_TOOLCHAIN_DIR)" "$(dir $(ORLIXOS_GAWK_BINARY))"; \
	export ORLIXOS_CC="$(ORLIXOS_CC)"; \
	export ORLIXOS_LD="$(ORLIXOS_LD)"; \
	export ORLIXOS_AR="$(ORLIXOS_AR)"; \
	export ORLIXOS_RANLIB="$(ORLIXOS_RANLIB)"; \
	export ORLIXOS_NM="$(ORLIXOS_NM)"; \
	export ORLIXOS_STRIP="$(ORLIXOS_STRIP)"; \
	export ORLIXOS_OBJDUMP="$(ORLIXOS_OBJDUMP)"; \
	export ORLIXOS_READELF="$(ORLIXOS_READELF)"; \
	export ORLIXOS_MLIBC_SYSROOT="$$sysroot"; \
	export ORLIXOS_MLIBC_HEADERS="$$headers"; \
	export ORLIXOS_MLIBC_RTLIB="$$rtlib"; \
	export ORLIXOS_HOSTED_USER_BASE_ADDRESS="$(ORLIXOS_HOSTED_USER_BASE_ADDRESS)"; \
	export ORLIXOS_PACKAGE_TOOLCHAIN_DIR="$(ORLIXOS_GAWK_TOOLCHAIN_DIR)"; \
	export ORLIXOS_PACKAGE_CODE_MODEL_FLAG="-fno-pie"; \
	"$(ORLIXOS_PACKAGE_TOOLCHAIN_SCRIPT)" "$(ORLIXOS_GAWK_TOOLCHAIN_DIR)"; \
	cd "$(ORLIXOS_GAWK_BUILD_DIR)"; \
	export PATH="$(ORLIXOS_GAWK_TOOLCHAIN_DIR):$$PATH"; \
	export CC="$(ORLIXOS_GAWK_TOOLCHAIN_DIR)/aarch64-linux-gnu-gcc"; \
	export CFLAGS="$(ORLIXOS_PACKAGE_CFLAGS) -D__KLIBC__"; \
	export LDFLAGS=""; \
	export LIBS=""; \
	export AR="$(ORLIXOS_GAWK_TOOLCHAIN_DIR)/aarch64-linux-gnu-ar"; \
	export RANLIB="$(ORLIXOS_GAWK_TOOLCHAIN_DIR)/aarch64-linux-gnu-ranlib"; \
	export NM="$(ORLIXOS_GAWK_TOOLCHAIN_DIR)/aarch64-linux-gnu-nm"; \
	export STRIP="$(ORLIXOS_GAWK_TOOLCHAIN_DIR)/aarch64-linux-gnu-strip"; \
	export OBJDUMP="$(ORLIXOS_GAWK_TOOLCHAIN_DIR)/aarch64-linux-gnu-objdump"; \
	"$(ORLIXOS_GAWK_SRC_DIR)/configure" --host=aarch64-linux-gnu --build=aarch64-apple-darwin --prefix=/usr --disable-nls --disable-mpfr --without-readline; \
	$(MAKE) -j1 all; \
	cp "$(ORLIXOS_GAWK_BUILD_DIR)/gawk" "$(ORLIXOS_GAWK_BINARY)"; \
	"$(ORLIXOS_STRIP)" "$(ORLIXOS_GAWK_BINARY)"; \
	file "$(ORLIXOS_GAWK_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_GAWK_BINARY)" >&2; exit 1; }; \
	printf 'profile=%s\ndistribution=%s\nchannel=%s\npackage=gawk\nversion=%s\nsha256=%s\n' "$(PROFILE)" "$(ORLIXOS_DISTRIBUTION_ID)" "$(ORLIXOS_DISTRIBUTION_CHANNEL)" "$(GAWK_VERSION)" "$(GAWK_SHA256)" > "$(ORLIXOS_PACKAGE_INSTALL_DIR)/gawk.proof"; \
	rm -rf "$(ORLIXOS_GAWK_BUILD_DIR)" "$(ORLIXOS_GAWK_TOOLCHAIN_DIR)"; \
	echo "built Orlix Linux gawk package input: $(ORLIXOS_GAWK_BINARY)"

$(ORLIXOS_PERL_BINARY): $(ORLIXOS_PERL_SOURCE_STAMP) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB)
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	rtlib="$(ORLIXOS_MLIBC_RTLIB)"; \
	[ -s "$$sysroot/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $$sysroot/usr/lib/libc.a" >&2; exit 1; }; \
	[ -d "$$headers" ] || { echo "missing Orlix Linux UAPI headers: $$headers" >&2; exit 1; }; \
	[ -s "$$rtlib" ] || { echo "missing Orlix compiler runtime archive: $$rtlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_CC)" >/dev/null 2>&1 || { echo "clang is required to build perl; set ORLIXOS_CC=/path/to/clang" >&2; exit 1; }; \
	command -v "$(ORLIXOS_AR)" >/dev/null 2>&1 || { echo "llvm-ar is required to build perl; set ORLIXOS_AR=/path/to/llvm-ar" >&2; exit 1; }; \
	command -v "$(ORLIXOS_RANLIB)" >/dev/null 2>&1 || { echo "llvm-ranlib is required to build perl; set ORLIXOS_RANLIB=/path/to/llvm-ranlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_NM)" >/dev/null 2>&1 || { echo "llvm-nm is required to build perl; set ORLIXOS_NM=/path/to/llvm-nm" >&2; exit 1; }; \
	command -v "$(ORLIXOS_STRIP)" >/dev/null 2>&1 || { echo "llvm-strip is required to package perl; set ORLIXOS_STRIP=/path/to/llvm-strip" >&2; exit 1; }; \
	command -v "$(ORLIXOS_READELF)" >/dev/null 2>&1 || { echo "llvm-readelf is required to build perl; set ORLIXOS_READELF=/path/to/llvm-readelf" >&2; exit 1; }; \
	command -v "$(ORLIXOS_OBJDUMP)" >/dev/null 2>&1 || { echo "llvm-objdump is required to build perl; set ORLIXOS_OBJDUMP=/path/to/llvm-objdump" >&2; exit 1; }; \
	command -v gsed >/dev/null 2>&1 || { echo "GNU sed is required to configure perl; install gsed or put it on PATH" >&2; exit 1; }; \
	rm -rf "$(ORLIXOS_PERL_TOOLCHAIN_DIR)" "$(ORLIXOS_PERL_BINARY)" "$(ORLIXOS_PERL_LIB_DIR)" "$(ORLIXOS_PERL_PROOF)"; \
	mkdir -p "$(ORLIXOS_PERL_TOOLCHAIN_DIR)" "$(dir $(ORLIXOS_PERL_BINARY))" "$(dir $(ORLIXOS_PERL_LIB_DIR))"; \
	{ \
		printf '%s\n' '#!/bin/bash'; \
		printf '%s\n' 'set -euo pipefail'; \
		printf '%s\n' 'cc="$(ORLIXOS_CC)"'; \
		printf '%s\n' 'sysroot="$(ORLIXOS_MLIBC_SYSROOT)"'; \
		printf '%s\n' 'headers="$(ORLIXOS_MLIBC_HEADERS)"'; \
		printf '%s\n' 'rtlib="$(ORLIXOS_MLIBC_RTLIB)"'; \
		printf '%s\n' 'link=1'; \
		printf '%s\n' 'for arg in "$$@"; do'; \
		printf '%s\n' '  case "$$arg" in -c|-E|-S) link=0 ;; esac'; \
		printf '%s\n' 'done'; \
		printf '%s\n' 'common=(--target=aarch64-linux-gnu "--sysroot=$$sysroot" -isystem "$$headers" -D_GNU_SOURCE -fhosted -fno-builtin -femulated-tls -ffixed-x18 -fno-pie)'; \
		printf '%s\n' 'if [ "$$link" -eq 1 ]; then'; \
		printf '%s\n' '  exec "$$cc" "$${common[@]}" "$$@" -static -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--image-base=$(ORLIXOS_HOSTED_USER_BASE_ADDRESS) "$$sysroot/usr/lib/crt1.o" "$$sysroot/usr/lib/crti.o" -Wl,--start-group "$$sysroot/usr/lib/libc.a" "$$sysroot/usr/lib/libm.a" "$$sysroot/usr/lib/libpthread.a" "$$sysroot/usr/lib/libssp_nonshared.a" "$$sysroot/usr/lib/libssp.a" "$$rtlib" -Wl,--end-group "$$sysroot/usr/lib/crtn.o"'; \
		printf '%s\n' 'fi'; \
		printf '%s\n' 'exec "$$cc" "$${common[@]}" "$$@"'; \
	} > "$(ORLIXOS_PERL_TOOLCHAIN_DIR)/aarch64-linux-gnu-gcc"; \
	chmod +x "$(ORLIXOS_PERL_TOOLCHAIN_DIR)/aarch64-linux-gnu-gcc"; \
	ln -sf "$(ORLIXOS_AR)" "$(ORLIXOS_PERL_TOOLCHAIN_DIR)/aarch64-linux-gnu-ar"; \
	ln -sf "$(ORLIXOS_RANLIB)" "$(ORLIXOS_PERL_TOOLCHAIN_DIR)/aarch64-linux-gnu-ranlib"; \
	ln -sf "$(ORLIXOS_NM)" "$(ORLIXOS_PERL_TOOLCHAIN_DIR)/aarch64-linux-gnu-nm"; \
	ln -sf "$(ORLIXOS_OBJDUMP)" "$(ORLIXOS_PERL_TOOLCHAIN_DIR)/aarch64-linux-gnu-objdump"; \
	ln -sf "$(ORLIXOS_READELF)" "$(ORLIXOS_PERL_TOOLCHAIN_DIR)/readelf"; \
	ln -sf "$$(command -v gsed)" "$(ORLIXOS_PERL_TOOLCHAIN_DIR)/sed"; \
	cd "$(ORLIXOS_PERL_SRC_DIR)"; \
	PATH="$(ORLIXOS_PERL_TOOLCHAIN_DIR):$$PATH" READELF=readelf ./configure --target=aarch64-linux-gnu --prefix=/usr --sysroot="$$sysroot" --target-tools-prefix=aarch64-linux-gnu- --no-dynaloader --only-mod=Errno,Fcntl,File-Glob,IO --host-cc="$(ORLIXOS_CC)" --host-set=d_nanosleep=define -Ud_syscall -Ud_syscallproto -Dcharsize=1 -Dshortsize=2 -Dintsize=4 -Dlongsize=8 -Ddoublesize=8 -Dptrsize=8 -Dlongdblsize=16 -Dlonglongsize=8; \
	perl -0pi -e 's/^# HAS_NANOSLEEP/#define HAS_NANOSLEEP/m' xconfig.h; \
	PATH="$(ORLIXOS_PERL_TOOLCHAIN_DIR):$$PATH" READELF=readelf $(MAKE) -j1 perl; \
	cp "$(ORLIXOS_PERL_SRC_DIR)/perl" "$(ORLIXOS_PERL_BINARY)"; \
	"$(ORLIXOS_STRIP)" "$(ORLIXOS_PERL_BINARY)"; \
	cp -Rf "$(ORLIXOS_PERL_SRC_DIR)/lib/." "$(ORLIXOS_PERL_LIB_DIR)/"; \
	for module_lib in "$(ORLIXOS_PERL_SRC_DIR)"/cpan/*/lib "$(ORLIXOS_PERL_SRC_DIR)"/dist/*/lib "$(ORLIXOS_PERL_SRC_DIR)"/ext/*/lib; do \
		[ -d "$$module_lib" ] || continue; \
		cp -Rf "$$module_lib/." "$(ORLIXOS_PERL_LIB_DIR)/"; \
	done; \
	chmod -R u+w "$(ORLIXOS_PERL_LIB_DIR)"; \
	for module_pm in "$(ORLIXOS_PERL_SRC_DIR)"/cpan/*/*.pm "$(ORLIXOS_PERL_SRC_DIR)"/dist/*/*.pm "$(ORLIXOS_PERL_SRC_DIR)"/ext/*/*.pm; do \
		[ -f "$$module_pm" ] || continue; \
		[ -f "$$(dirname "$$module_pm")/Makefile" ] || continue; \
		module_path="$$(perl -ne 'if (/^package[[:space:]]+([A-Za-z0-9_:]+)[[:space:]]*;/) { $$m=$$1; $$m =~ s!::!/!g; print "$$m.pm"; exit }' "$$module_pm")"; \
		[ -n "$$module_path" ] || continue; \
		mkdir -p "$(ORLIXOS_PERL_LIB_DIR)/$$(dirname "$$module_path")"; \
		rm -f "$(ORLIXOS_PERL_LIB_DIR)/$$module_path"; \
		cp "$$module_pm" "$(ORLIXOS_PERL_LIB_DIR)/$$module_path"; \
	done; \
	if [ -f "$(ORLIXOS_PERL_SRC_DIR)/dist/IO/IO.pm" ]; then rm -f "$(ORLIXOS_PERL_LIB_DIR)/IO.pm"; cp "$(ORLIXOS_PERL_SRC_DIR)/dist/IO/IO.pm" "$(ORLIXOS_PERL_LIB_DIR)/IO.pm"; fi; \
	cd "$(ORLIXOS_PERL_SRC_DIR)/dist/XSLoader"; ../../miniperl_top -I../../lib XSLoader_pm.PL; rm -f "$(ORLIXOS_PERL_LIB_DIR)/XSLoader.pm"; cp XSLoader.pm "$(ORLIXOS_PERL_LIB_DIR)/XSLoader.pm"; \
	cd "$(ORLIXOS_PERL_SRC_DIR)/ext/DynaLoader"; ../../miniperl_top -I../../lib DynaLoader_pm.PL; rm -f "$(ORLIXOS_PERL_LIB_DIR)/DynaLoader.pm"; cp DynaLoader.pm "$(ORLIXOS_PERL_LIB_DIR)/DynaLoader.pm"; \
	file "$(ORLIXOS_PERL_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_PERL_BINARY)" >&2; exit 1; }; \
	printf 'profile=%s\ndistribution=%s\nchannel=%s\npackage=perl\nversion=%s\nsha256=%s\nperl_cross_version=%s\nperl_cross_sha256=%s\nstatic_modules=Errno,Fcntl,File-Glob,IO\n' "$(PROFILE)" "$(ORLIXOS_DISTRIBUTION_ID)" "$(ORLIXOS_DISTRIBUTION_CHANNEL)" "$(PERL_VERSION)" "$(PERL_SHA256)" "$(PERL_CROSS_VERSION)" "$(PERL_CROSS_SHA256)" > "$(ORLIXOS_PERL_PROOF)"; \
	rm -rf "$(ORLIXOS_PERL_TOOLCHAIN_DIR)"; \
	echo "built Orlix Linux perl package input: $(ORLIXOS_PERL_BINARY)"

$(ORLIXOS_PERL_PROOF): $(ORLIXOS_PERL_BINARY)
	@set -euo pipefail; \
	[ -x "$(ORLIXOS_PERL_BINARY)" ] || { echo "missing perl package input: $(ORLIXOS_PERL_BINARY)" >&2; exit 1; }; \
	file "$(ORLIXOS_PERL_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_PERL_BINARY)" >&2; exit 1; }; \
	printf 'profile=%s\ndistribution=%s\nchannel=%s\npackage=perl\nversion=%s\nsha256=%s\nperl_cross_version=%s\nperl_cross_sha256=%s\nstatic_modules=Errno,Fcntl,File-Glob,IO\n' "$(PROFILE)" "$(ORLIXOS_DISTRIBUTION_ID)" "$(ORLIXOS_DISTRIBUTION_CHANNEL)" "$(PERL_VERSION)" "$(PERL_SHA256)" "$(PERL_CROSS_VERSION)" "$(PERL_CROSS_SHA256)" > "$(ORLIXOS_PERL_PROOF)"

$(ORLIXOS_JQ_BINARY): $(ORLIXOS_JQ_SOURCE_STAMP) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB)
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	rtlib="$(ORLIXOS_MLIBC_RTLIB)"; \
	[ -s "$$sysroot/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $$sysroot/usr/lib/libc.a" >&2; exit 1; }; \
	[ -d "$$headers" ] || { echo "missing Orlix Linux UAPI headers: $$headers" >&2; exit 1; }; \
	[ -s "$$rtlib" ] || { echo "missing Orlix compiler runtime archive: $$rtlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_CC)" >/dev/null 2>&1 || { echo "clang is required to build jq; set ORLIXOS_CC=/path/to/clang" >&2; exit 1; }; \
	command -v "$(ORLIXOS_AR)" >/dev/null 2>&1 || { echo "llvm-ar is required to build jq; set ORLIXOS_AR=/path/to/llvm-ar" >&2; exit 1; }; \
	command -v "$(ORLIXOS_RANLIB)" >/dev/null 2>&1 || { echo "llvm-ranlib is required to build jq; set ORLIXOS_RANLIB=/path/to/llvm-ranlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_STRIP)" >/dev/null 2>&1 || { echo "llvm-strip is required to package jq; set ORLIXOS_STRIP=/path/to/llvm-strip" >&2; exit 1; }; \
	rm -rf "$(ORLIXOS_JQ_BUILD_DIR)" "$(ORLIXOS_JQ_BINARY)"; \
	mkdir -p "$(dir $(ORLIXOS_JQ_BUILD_DIR))" "$(dir $(ORLIXOS_JQ_BINARY))"; \
	cp -R "$(ORLIXOS_JQ_SRC_DIR)" "$(ORLIXOS_JQ_BUILD_DIR)"; \
	cd "$(ORLIXOS_JQ_BUILD_DIR)"; \
	export CC="$(ORLIXOS_CC) --target=aarch64-linux-gnu --sysroot=$$sysroot -isystem $$headers -D_GNU_SOURCE -fhosted -fno-builtin -femulated-tls -ffixed-x18 -fno-pie"; \
	export CFLAGS="$(ORLIXOS_PACKAGE_CFLAGS)"; \
	export CPPFLAGS="-Imodules/oniguruma/src"; \
	export LDFLAGS="--target=aarch64-linux-gnu --sysroot=$$sysroot -static -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--image-base=$(ORLIXOS_HOSTED_USER_BASE_ADDRESS) $$sysroot/usr/lib/crt1.o $$sysroot/usr/lib/crti.o -Wl,--start-group"; \
	orlixos_package_libs="$$sysroot/usr/lib/libc.a $$sysroot/usr/lib/libm.a $$sysroot/usr/lib/libpthread.a $$sysroot/usr/lib/libssp_nonshared.a $$sysroot/usr/lib/libssp.a $$rtlib -Wl,--end-group $$sysroot/usr/lib/crtn.o"; \
	export LIBS="$$orlixos_package_libs"; \
	export AR="$(ORLIXOS_AR)"; \
	export RANLIB="$(ORLIXOS_RANLIB)"; \
	export STRIP="$(ORLIXOS_STRIP)"; \
	export ac_cv_func_isatty=yes; \
	export ac_cv_func_setlocale=yes; \
	export ac_cv_func_strptime=yes; \
	export ac_cv_func_pthread_key_create=yes; \
	export ac_cv_func_pthread_once=yes; \
	./configure --host=aarch64-linux-gnu --build=aarch64-apple-darwin --prefix=/usr --disable-shared --enable-static --enable-all-static --disable-docs --with-oniguruma=builtin; \
	$(MAKE) -C modules/oniguruma -j1 LIBS=; \
	$(MAKE) -j1 src/builtin.inc libjq.la LIBS=; \
	$(MAKE) -j1 jq LIBS="$$orlixos_package_libs"; \
	cp "$(ORLIXOS_JQ_BUILD_DIR)/jq" "$(ORLIXOS_JQ_BINARY)"; \
	"$(ORLIXOS_STRIP)" "$(ORLIXOS_JQ_BINARY)"; \
	file "$(ORLIXOS_JQ_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_JQ_BINARY)" >&2; exit 1; }; \
	printf 'profile=%s\ndistribution=%s\nchannel=%s\npackage=jq\nversion=%s\nsha256=%s\nregex=oniguruma-builtin\n' "$(PROFILE)" "$(ORLIXOS_DISTRIBUTION_ID)" "$(ORLIXOS_DISTRIBUTION_CHANNEL)" "$(JQ_VERSION)" "$(JQ_SHA256)" > "$(ORLIXOS_PACKAGE_INSTALL_DIR)/jq.proof"; \
	echo "built Orlix Linux jq package input: $(ORLIXOS_JQ_BINARY)"

$(ORLIXOS_CURL_BINARY): $(ORLIXOS_CURL_SOURCE_STAMP) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB)
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	rtlib="$(ORLIXOS_MLIBC_RTLIB)"; \
	[ -s "$$sysroot/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $$sysroot/usr/lib/libc.a" >&2; exit 1; }; \
	[ -d "$$headers" ] || { echo "missing Orlix Linux UAPI headers: $$headers" >&2; exit 1; }; \
	[ -s "$$rtlib" ] || { echo "missing Orlix compiler runtime archive: $$rtlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_CC)" >/dev/null 2>&1 || { echo "clang is required to build curl; set ORLIXOS_CC=/path/to/clang" >&2; exit 1; }; \
	command -v "$(ORLIXOS_AR)" >/dev/null 2>&1 || { echo "llvm-ar is required to build curl; set ORLIXOS_AR=/path/to/llvm-ar" >&2; exit 1; }; \
	command -v "$(ORLIXOS_RANLIB)" >/dev/null 2>&1 || { echo "llvm-ranlib is required to build curl; set ORLIXOS_RANLIB=/path/to/llvm-ranlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_STRIP)" >/dev/null 2>&1 || { echo "llvm-strip is required to package curl; set ORLIXOS_STRIP=/path/to/llvm-strip" >&2; exit 1; }; \
	rm -rf "$(ORLIXOS_CURL_BUILD_DIR)" "$(ORLIXOS_CURL_BINARY)"; \
	mkdir -p "$(ORLIXOS_CURL_BUILD_DIR)" "$(dir $(ORLIXOS_CURL_BINARY))"; \
	cd "$(ORLIXOS_CURL_BUILD_DIR)"; \
	export CC="$(ORLIXOS_CC) --target=aarch64-linux-gnu --sysroot=$$sysroot -isystem $$headers -D_GNU_SOURCE -fhosted -fno-builtin -femulated-tls -ffixed-x18 -fno-pie"; \
	export CFLAGS="$(ORLIXOS_PACKAGE_CFLAGS)"; \
	export CPPFLAGS="-I$(ORLIXOS_CURL_SRC_DIR)/include"; \
	rtlib_dir="$$(dirname "$$rtlib")"; \
	export LDFLAGS="--target=aarch64-linux-gnu --sysroot=$$sysroot -static -no-pie -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--no-dynamic-linker -Wl,--image-base=$(ORLIXOS_HOSTED_USER_BASE_ADDRESS) $$sysroot/usr/lib/crt1.o $$sysroot/usr/lib/crti.o -Wl,--start-group -L$$sysroot/usr/lib -L$$rtlib_dir"; \
	export LIBS="-lc -lm -lpthread -lssp_nonshared -lssp -lorlix_compiler_rt -Wl,--end-group $$sysroot/usr/lib/crtn.o"; \
	export AR="$(ORLIXOS_AR)"; \
	export RANLIB="$(ORLIXOS_RANLIB)"; \
	export STRIP="$(ORLIXOS_STRIP)"; \
	export ac_cv_func_getpwuid_r=yes; \
	export ac_cv_func_geteuid=yes; \
	export ac_cv_func_getppid=yes; \
	export ac_cv_func_setlocale=yes; \
	"$(ORLIXOS_CURL_SRC_DIR)/configure" --host=aarch64-linux-gnu --build=aarch64-apple-darwin --prefix=/usr --disable-shared --enable-static --disable-docs --disable-manual --disable-threaded-resolver --disable-ldap --disable-ldaps --without-ssl --without-zlib --without-brotli --without-zstd --without-libidn2 --without-nghttp2 --without-ngtcp2 --without-nghttp3 --without-libpsl; \
	$(MAKE) -C lib -j1 libcurl.la; \
	$(MAKE) -C src -j1 curl; \
	cp "$(ORLIXOS_CURL_BUILD_DIR)/src/curl" "$(ORLIXOS_CURL_BINARY)"; \
	"$(ORLIXOS_STRIP)" "$(ORLIXOS_CURL_BINARY)"; \
	file "$(ORLIXOS_CURL_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_CURL_BINARY)" >&2; exit 1; }; \
	printf 'profile=%s\ndistribution=%s\nchannel=%s\npackage=curl\nversion=%s\nsha256=%s\nfeatures=static-no-external-tls\n' "$(PROFILE)" "$(ORLIXOS_DISTRIBUTION_ID)" "$(ORLIXOS_DISTRIBUTION_CHANNEL)" "$(CURL_VERSION)" "$(CURL_SHA256)" > "$(ORLIXOS_PACKAGE_INSTALL_DIR)/curl.proof"; \
	echo "built Orlix Linux curl package input: $(ORLIXOS_CURL_BINARY)"

$(ORLIXOS_NCURSES_LIBTINFO): $(ORLIXOS_NCURSES_SOURCE_STAMP) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB)
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	rtlib="$(ORLIXOS_MLIBC_RTLIB)"; \
	[ -s "$$sysroot/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $$sysroot/usr/lib/libc.a" >&2; exit 1; }; \
	[ -d "$$headers" ] || { echo "missing Orlix Linux UAPI headers: $$headers" >&2; exit 1; }; \
	[ -s "$$rtlib" ] || { echo "missing Orlix compiler runtime archive: $$rtlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_CC)" >/dev/null 2>&1 || { echo "clang is required to build ncurses; set ORLIXOS_CC=/path/to/clang" >&2; exit 1; }; \
	command -v "$(ORLIXOS_AR)" >/dev/null 2>&1 || { echo "llvm-ar is required to build ncurses; set ORLIXOS_AR=/path/to/llvm-ar" >&2; exit 1; }; \
	command -v "$(ORLIXOS_RANLIB)" >/dev/null 2>&1 || { echo "llvm-ranlib is required to build ncurses; set ORLIXOS_RANLIB=/path/to/llvm-ranlib" >&2; exit 1; }; \
	rm -rf "$(ORLIXOS_NCURSES_BUILD_DIR)" "$(ORLIXOS_NCURSES_SYSROOT)"; \
	mkdir -p "$(ORLIXOS_NCURSES_BUILD_DIR)" "$(ORLIXOS_NCURSES_SYSROOT)" "$(ORLIXOS_PACKAGE_INSTALL_DIR)"; \
	cd "$(ORLIXOS_NCURSES_BUILD_DIR)"; \
	export CC="$(ORLIXOS_CC) --target=aarch64-linux-gnu --sysroot=$$sysroot -isystem $$headers -D_GNU_SOURCE -fhosted -fno-builtin -femulated-tls -ffixed-x18 -fno-pie"; \
	export CFLAGS="$(ORLIXOS_PACKAGE_CFLAGS)"; \
	export LDFLAGS="--target=aarch64-linux-gnu --sysroot=$$sysroot -static -no-pie -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--no-dynamic-linker -Wl,--image-base=$(ORLIXOS_HOSTED_USER_BASE_ADDRESS) $$sysroot/usr/lib/crt1.o $$sysroot/usr/lib/crti.o -Wl,--start-group -L$$sysroot/usr/lib -L$$(dirname "$$rtlib")"; \
	export LIBS="-lc -lm -lpthread -lssp_nonshared -lssp -lorlix_compiler_rt -Wl,--end-group $$sysroot/usr/lib/crtn.o"; \
	export AR="$(ORLIXOS_AR)"; \
	export RANLIB="$(ORLIXOS_RANLIB)"; \
	"$(ORLIXOS_NCURSES_SRC_DIR)/configure" --host=aarch64-linux-gnu --build=aarch64-apple-darwin --prefix=/usr --with-normal --without-shared --without-debug --without-ada --without-cxx --without-cxx-binding --without-progs --without-tests --without-manpages --with-termlib=tinfo --with-ticlib=tic --disable-db-install --disable-home-terminfo --disable-stripping; \
	$(MAKE) -j1 libs; \
	$(MAKE) -j1 DESTDIR="$(ORLIXOS_NCURSES_SYSROOT)" install.libs install.includes; \
	[ -s "$(ORLIXOS_NCURSES_LIBTINFO)" ] || { echo "missing ncurses terminfo archive: $(ORLIXOS_NCURSES_LIBTINFO)" >&2; exit 1; }; \
	[ -s "$(ORLIXOS_NCURSES_LIBNCURSES)" ] || { echo "missing ncurses archive: $(ORLIXOS_NCURSES_LIBNCURSES)" >&2; exit 1; }; \
	"$(ORLIXOS_AR)" t "$(ORLIXOS_NCURSES_LIBTINFO)" >/dev/null; \
	printf 'profile=%s\ndistribution=%s\nchannel=%s\npackage=ncurses\nversion=%s\nsha256=%s\nfeatures=static-zsh-build-dependency\n' "$(PROFILE)" "$(ORLIXOS_DISTRIBUTION_ID)" "$(ORLIXOS_DISTRIBUTION_CHANNEL)" "$(NCURSES_VERSION)" "$(NCURSES_SHA256)" > "$(ORLIXOS_PACKAGE_INSTALL_DIR)/ncurses.proof"; \
	echo "built Orlix Linux ncurses static package input: $(ORLIXOS_NCURSES_LIBTINFO)"

$(ORLIXOS_ZSH_BINARY): $(ORLIXOS_ZSH_SOURCE_STAMP) $(ORLIXOS_NCURSES_LIBTINFO) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB)
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	rtlib="$(ORLIXOS_MLIBC_RTLIB)"; \
	[ -s "$$sysroot/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $$sysroot/usr/lib/libc.a" >&2; exit 1; }; \
	[ -d "$$headers" ] || { echo "missing Orlix Linux UAPI headers: $$headers" >&2; exit 1; }; \
	[ -s "$$rtlib" ] || { echo "missing Orlix compiler runtime archive: $$rtlib" >&2; exit 1; }; \
	[ -s "$(ORLIXOS_NCURSES_LIBTINFO)" ] || { echo "missing ncurses terminfo archive: $(ORLIXOS_NCURSES_LIBTINFO)" >&2; exit 1; }; \
	[ -s "$(ORLIXOS_NCURSES_LIBNCURSES)" ] || { echo "missing ncurses archive: $(ORLIXOS_NCURSES_LIBNCURSES)" >&2; exit 1; }; \
	command -v "$(ORLIXOS_CC)" >/dev/null 2>&1 || { echo "clang is required to build zsh; set ORLIXOS_CC=/path/to/clang" >&2; exit 1; }; \
	command -v "$(ORLIXOS_STRIP)" >/dev/null 2>&1 || { echo "llvm-strip is required to package zsh; set ORLIXOS_STRIP=/path/to/llvm-strip" >&2; exit 1; }; \
	command -v "$(ORLIXOS_READELF)" >/dev/null 2>&1 || { echo "llvm-readelf is required to verify zsh; set ORLIXOS_READELF=/path/to/llvm-readelf" >&2; exit 1; }; \
	rm -rf "$(ORLIXOS_ZSH_BUILD_DIR)" "$(ORLIXOS_ZSH_BINARY)"; \
	mkdir -p "$(ORLIXOS_ZSH_BUILD_DIR)" "$(dir $(ORLIXOS_ZSH_BINARY))"; \
	cd "$(ORLIXOS_ZSH_BUILD_DIR)"; \
	rtlib_dir="$$(dirname "$$rtlib")"; \
	export CC="$(ORLIXOS_CC) --target=aarch64-linux-gnu --sysroot=$$sysroot -isystem $$headers -D_GNU_SOURCE -fhosted -fno-builtin -femulated-tls -ffixed-x18 -fno-pie"; \
	export CFLAGS="$(ORLIXOS_PACKAGE_CFLAGS)"; \
	export CPPFLAGS="-I$(ORLIXOS_NCURSES_SYSROOT)/usr/include"; \
	export LDFLAGS="--target=aarch64-linux-gnu --sysroot=$$sysroot -static -no-pie -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--no-dynamic-linker -Wl,--image-base=$(ORLIXOS_HOSTED_USER_BASE_ADDRESS) $$sysroot/usr/lib/crt1.o $$sysroot/usr/lib/crti.o -Wl,--start-group -L$(ORLIXOS_NCURSES_SYSROOT)/usr/lib -L$$sysroot/usr/lib -L$$rtlib_dir"; \
	export LIBS="-ltinfo -lc -lm -lpthread -lssp_nonshared -lssp -lorlix_compiler_rt -Wl,--end-group $$sysroot/usr/lib/crtn.o"; \
	export AR="$(ORLIXOS_AR)"; \
	export RANLIB="$(ORLIXOS_RANLIB)"; \
	export STRIP="$(ORLIXOS_STRIP)"; \
	export zsh_cv_sys_dynamic_clash_ok=yes; \
	export zsh_cv_sys_dynamic_execsyms=no; \
	export zsh_cv_shared_environ=yes; \
	"$(ORLIXOS_ZSH_SRC_DIR)/configure" --host=aarch64-linux-gnu --build=aarch64-apple-darwin --prefix=/usr --disable-dynamic --disable-gdbm --disable-pcre --disable-cap --disable-dynamic-nss --with-term-lib=tinfo --enable-etcdir=/etc/zsh --enable-zshenv=/etc/zsh/zshenv --enable-zshrc=/etc/zsh/zshrc --enable-zprofile=/etc/zsh/zprofile --enable-zlogin=/etc/zsh/zlogin --enable-zlogout=/etc/zsh/zlogout; \
	$(MAKE) -C Src -j1 zsh; \
	cp "$(ORLIXOS_ZSH_BUILD_DIR)/Src/zsh" "$(ORLIXOS_ZSH_BINARY)"; \
	"$(ORLIXOS_STRIP)" "$(ORLIXOS_ZSH_BINARY)"; \
	file "$(ORLIXOS_ZSH_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_ZSH_BINARY)" >&2; exit 1; }; \
	if "$(ORLIXOS_READELF)" -l "$(ORLIXOS_ZSH_BINARY)" | grep -F -q 'INTERP'; then echo "zsh must be a static Orlix Linux ELF without PT_INTERP" >&2; exit 1; fi; \
	printf 'profile=%s\ndistribution=%s\nchannel=%s\npackage=zsh\nversion=%s\nsha256=%s\nterminal_library=ncurses-%s-static\nfeatures=static-no-dynamic-modules\n' "$(PROFILE)" "$(ORLIXOS_DISTRIBUTION_ID)" "$(ORLIXOS_DISTRIBUTION_CHANNEL)" "$(ZSH_VERSION)" "$(ZSH_SHA256)" "$(NCURSES_VERSION)" > "$(ORLIXOS_PACKAGE_INSTALL_DIR)/zsh.proof"; \
	echo "built Orlix Linux zsh package input: $(ORLIXOS_ZSH_BINARY)"
