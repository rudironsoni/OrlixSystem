$(ORLIXOS_BASH_BINARY): $(ORLIXOS_BASH_SOURCE_STAMP) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB)
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	rtlib="$(ORLIXOS_MLIBC_RTLIB)"; \
	[ -s "$$sysroot/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $$sysroot/usr/lib/libc.a" >&2; exit 1; }; \
	[ -d "$$headers" ] || { echo "missing Orlix Linux UAPI headers: $$headers" >&2; exit 1; }; \
	[ -s "$$rtlib" ] || { echo "missing Orlix compiler runtime archive: $$rtlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_CC)" >/dev/null 2>&1 || { echo "clang is required to build Bash; set ORLIXOS_CC=/path/to/clang" >&2; exit 1; }; \
	command -v "$(ORLIXOS_AR)" >/dev/null 2>&1 || { echo "llvm-ar is required to build Bash; set ORLIXOS_AR=/path/to/llvm-ar" >&2; exit 1; }; \
	command -v "$(ORLIXOS_RANLIB)" >/dev/null 2>&1 || { echo "llvm-ranlib is required to build Bash; set ORLIXOS_RANLIB=/path/to/llvm-ranlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_STRIP)" >/dev/null 2>&1 || { echo "llvm-strip is required to package Bash; set ORLIXOS_STRIP=/path/to/llvm-strip" >&2; exit 1; }; \
	export PATH="$(ORLIXOS_PACKAGE_BOOTSTRAP_PATH)"; \
	command -v bison >/dev/null 2>&1 || { echo "GNU bison is required to build Bash; set ORLIXOS_PACKAGE_BOOTSTRAP_PATH to include it" >&2; exit 1; }; \
	rm -rf "$(ORLIXOS_BASH_BUILD_DIR)" "$(ORLIXOS_BASH_BINARY)"; \
	mkdir -p "$(ORLIXOS_BASH_BUILD_DIR)" "$(dir $(ORLIXOS_BASH_BINARY))"; \
	cd "$(ORLIXOS_BASH_BUILD_DIR)"; \
	export CC="$(ORLIXOS_CC) --target=aarch64-linux-gnu --sysroot=$$sysroot -isystem $$headers -D_GNU_SOURCE -fhosted -fno-builtin -ffixed-x18 -fno-pie"; \
	export CFLAGS="-O2 -Wno-unknown-warning-option"; \
	export LDFLAGS="--target=aarch64-linux-gnu --sysroot=$$sysroot -static -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--image-base=$(ORLIXOS_HOSTED_USER_BASE_ADDRESS) $$sysroot/usr/lib/crt1.o $$sysroot/usr/lib/crti.o -Wl,--start-group"; \
	export LIBS="$$sysroot/usr/lib/libc.a $$sysroot/usr/lib/libm.a $$sysroot/usr/lib/libpthread.a $$sysroot/usr/lib/libssp_nonshared.a $$sysroot/usr/lib/libssp.a $$rtlib -Wl,--end-group $$sysroot/usr/lib/crtn.o"; \
	export AR="$(ORLIXOS_AR)"; \
	export RANLIB="$(ORLIXOS_RANLIB)"; \
	export CC_FOR_BUILD="$(ORLIXOS_BUILD_CC)"; \
	export bash_cv_getenv_redef=no; \
	export bash_cv_getcwd_malloc=yes; \
	export bash_cv_func_strchrnul_works=yes; \
	"$(ORLIXOS_BASH_SRC_DIR)/configure" --host=aarch64-linux-gnu --build="$$(uname -m)-apple-darwin" --prefix=/usr --without-bash-malloc --enable-static-link --disable-nls --disable-readline --without-installed-readline --without-curses; \
	$(MAKE) -j1 bash; \
	cp "$(ORLIXOS_BASH_BUILD_DIR)/bash" "$(ORLIXOS_BASH_BINARY)"; \
	"$(ORLIXOS_STRIP)" "$(ORLIXOS_BASH_BINARY)"; \
	file "$(ORLIXOS_BASH_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_BASH_BINARY)" >&2; exit 1; }; \
	printf 'profile=%s\ndistribution=%s\nchannel=%s\npackage=bash\nversion=%s\nsha256=%s\n' "$(PROFILE)" "$(ORLIXOS_DISTRIBUTION_ID)" "$(ORLIXOS_DISTRIBUTION_CHANNEL)" "$(BASH_VERSION)" "$(BASH_SHA256)" > "$(ORLIXOS_PACKAGE_INSTALL_DIR)/bash.proof"; \
	rm -rf "$(ORLIXOS_BASH_BUILD_DIR)"; \
	echo "built Orlix Linux Bash package input: $(ORLIXOS_BASH_BINARY)"

$(ORLIXOS_COREUTILS_PROOF): $(ORLIXOS_COREUTILS_SOURCE_STAMP) $(ORLIXOS_ACL_PROOF) $(ORLIXOS_LIBCAP_PROOF) $(ORLIXOS_LIBSELINUX_PROOF) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB)
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	rtlib="$(ORLIXOS_MLIBC_RTLIB)"; \
	[ -s "$$sysroot/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $$sysroot/usr/lib/libc.a" >&2; exit 1; }; \
	[ -d "$$headers" ] || { echo "missing Orlix Linux UAPI headers: $$headers" >&2; exit 1; }; \
	[ -s "$$rtlib" ] || { echo "missing Orlix compiler runtime archive: $$rtlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_CC)" >/dev/null 2>&1 || { echo "clang is required to build coreutils; set ORLIXOS_CC=/path/to/clang" >&2; exit 1; }; \
	command -v "$(ORLIXOS_AR)" >/dev/null 2>&1 || { echo "llvm-ar is required to build coreutils; set ORLIXOS_AR=/path/to/llvm-ar" >&2; exit 1; }; \
	command -v "$(ORLIXOS_RANLIB)" >/dev/null 2>&1 || { echo "llvm-ranlib is required to build coreutils; set ORLIXOS_RANLIB=/path/to/llvm-ranlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_STRIP)" >/dev/null 2>&1 || { echo "llvm-strip is required to package coreutils; set ORLIXOS_STRIP=/path/to/llvm-strip" >&2; exit 1; }; \
	rm -rf "$(ORLIXOS_COREUTILS_BUILD_DIR)" "$(ORLIXOS_COREUTILS_PROOF)"; \
	for program in $(ORLIXOS_COREUTILS_PROGRAMS); do rm -f "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program"; done; \
	mkdir -p "$(ORLIXOS_COREUTILS_BUILD_DIR)" "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin"; \
	cd "$(ORLIXOS_COREUTILS_BUILD_DIR)"; \
	export CC="$(ORLIXOS_CC) --target=aarch64-linux-gnu --sysroot=$$sysroot -isystem $$headers -D_GNU_SOURCE -fhosted -fno-builtin -ffixed-x18 -fno-pie"; \
	export CPPFLAGS="-I$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/include"; \
	export CFLAGS="$(ORLIXOS_PACKAGE_CFLAGS)"; \
	export LDFLAGS="--target=aarch64-linux-gnu --sysroot=$$sysroot -L$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/lib -static -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--image-base=$(ORLIXOS_HOSTED_USER_BASE_ADDRESS) $$sysroot/usr/lib/crt1.o $$sysroot/usr/lib/crti.o -Wl,--start-group"; \
	export LIBS="$(ORLIXOS_LIBSELINUX_A) $(ORLIXOS_LIBCAP_A) $(ORLIXOS_LIBACL_A) $(ORLIXOS_LIBATTR_A) $(ORLIXOS_LIBSEPOL_A) $(ORLIXOS_LIBPCRE2_8_A) $(ORLIXOS_LIBFTS_A) $$sysroot/usr/lib/libc.a $$sysroot/usr/lib/libm.a $$sysroot/usr/lib/libpthread.a $$sysroot/usr/lib/libssp_nonshared.a $$sysroot/usr/lib/libssp.a $$rtlib -Wl,--end-group $$sysroot/usr/lib/crtn.o"; \
	export AR="$(ORLIXOS_AR)"; \
	export RANLIB="$(ORLIXOS_RANLIB)"; \
	export PATH="$(ORLIXOS_COREUTILS_BOOTSTRAP_PATH)"; \
	export gl_cv_header_working_fcntl_h=yes; \
	export gl_cv_func_getopt_gnu=yes; \
	export gl_cv_func_getopt_long_gnu=yes; \
	export gl_cv_func_strtod_works=yes; \
	"$(ORLIXOS_COREUTILS_SRC_DIR)/configure" --host=aarch64-linux-gnu --build=aarch64-apple-darwin --prefix=/usr --disable-nls --with-selinux --enable-libcap --disable-gcc-warnings; \
	$(MAKE) -j1 all PROGRAMS= LIBRARIES= MANS= INFO_DEPS=; \
	for program in $(ORLIXOS_COREUTILS_PROGRAMS); do \
		source_program="$$program"; \
		if [ "$$program" = install ]; then source_program=ginstall; fi; \
		$(MAKE) -j1 "src/$$source_program" MANS= INFO_DEPS=; \
	done; \
	$(MAKE) -j1 src/getlimits; \
	for program in $(ORLIXOS_COREUTILS_PROGRAMS); do \
		source_program="$$program"; \
		if [ "$$program" = install ]; then source_program=ginstall; fi; \
		"$(ORLIXOS_STRIP)" "$(ORLIXOS_COREUTILS_BUILD_DIR)/src/$$source_program"; \
	done; \
	"$(ORLIXOS_STRIP)" "$(ORLIXOS_COREUTILS_BUILD_DIR)/src/getlimits"; \
	for program in $(ORLIXOS_COREUTILS_PROGRAMS); do \
		source_program="$$program"; \
		if [ "$$program" = install ]; then source_program=ginstall; fi; \
		install -m 0755 "$(ORLIXOS_COREUTILS_BUILD_DIR)/src/$$source_program" "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program"; \
	done; \
	cp "$(ORLIXOS_COREUTILS_BUILD_DIR)/src/getlimits" "$(ORLIXOS_GETLIMITS_BINARY)"; \
	file "$(ORLIXOS_GETLIMITS_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_GETLIMITS_BINARY)" >&2; exit 1; }; \
	for program in $(ORLIXOS_COREUTILS_PROGRAMS); do \
		"$(ORLIXOS_STRIP)" "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program"; \
		file "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program" >&2; exit 1; }; \
	done; \
	printf 'profile=%s\ndistribution=%s\nchannel=%s\npackage=coreutils\nprograms=%s\nversion=%s\ngit_url=%s\ngit_ref=%s\ngit_commit=%s\ngnulib_git_url=%s\n' "$(PROFILE)" "$(ORLIXOS_DISTRIBUTION_ID)" "$(ORLIXOS_DISTRIBUTION_CHANNEL)" "$(ORLIXOS_COREUTILS_PROGRAMS)" "$(COREUTILS_VERSION)" "$(COREUTILS_GIT_URL)" "$(COREUTILS_GIT_REF)" "$(COREUTILS_GIT_COMMIT)" "$(COREUTILS_GNULIB_GIT_URL)" > "$(ORLIXOS_COREUTILS_PROOF)"; \
	echo "built Orlix Linux coreutils package inputs: $(ORLIXOS_COREUTILS_PROGRAMS)"

$(ORLIXOS_COREUTILS_TEST_LIST): $(ORLIXOS_COREUTILS_PROOF) $(PROJECT_DIR)/Makefile
	@set -euo pipefail; \
	makefile="$(ORLIXOS_COREUTILS_BUILD_DIR)/Makefile"; \
	[ -s "$$makefile" ] || { echo "missing configured Coreutils Makefile: $$makefile" >&2; exit 1; }; \
	mkdir -p "$(dir $(ORLIXOS_COREUTILS_TEST_LIST))"; \
	raw_list="$(ORLIXOS_COREUTILS_TEST_LIST).raw"; \
	awk '\
		function flush(mode, line, i, value) { \
			gsub(/\\/, "", line); \
			for (i = 1; i <= split(line, fields, /[[:space:]]+/); i++) { \
				value = fields[i]; \
				if (value == "" || value == "=") continue; \
				gsub(/\044[(]tf[)]/, "tests/factor", value); \
				if (value ~ /^tests\//) print mode " " value; \
			} \
		} \
		/^(all_tests|factor_tests) =/ { mode = "user"; collecting = 1; flush(mode, $$0); if ($$0 !~ /\\$$/) collecting = 0; next } \
		/^all_root_tests =/ { mode = "root"; collecting = 1; flush(mode, $$0); if ($$0 !~ /\\$$/) collecting = 0; next } \
		collecting { flush(mode, $$0); if ($$0 !~ /\\$$/) collecting = 0 } \
	' "$$makefile" | sort -u > "$$raw_list"; \
	awk '\
		$$1 == "root" { root[$$2] = $$0; next } \
		$$1 == "user" { user[$$2] = $$0; next } \
		END { \
			for (test in user) if (!(test in root)) print user[test]; \
			for (test in root) print root[test]; \
		} \
	' "$$raw_list" | sort -k2,2 > "$(ORLIXOS_COREUTILS_TEST_LIST)"; \
	if [ -n "$(ORLIXOS_COREUTILS_TESTS)" ]; then \
		filtered_list="$(ORLIXOS_COREUTILS_TEST_LIST).filtered"; \
		: > "$$filtered_list"; \
		for test in $(ORLIXOS_COREUTILS_TESTS); do \
			awk -v selected="$$test" '$$2 == selected { print }' "$(ORLIXOS_COREUTILS_TEST_LIST)" >> "$$filtered_list"; \
		done; \
		mv "$$filtered_list" "$(ORLIXOS_COREUTILS_TEST_LIST)"; \
	fi; \
	rm -f "$$raw_list"; \
	[ -s "$(ORLIXOS_COREUTILS_TEST_LIST)" ] || { echo "empty Coreutils upstream test list" >&2; exit 1; }; \
	cp "$(ORLIXOS_COREUTILS_BUILD_DIR)/lib/config.h" "$(ORLIXOS_COREUTILS_CONFIG_HEADER)"; \
	[ -s "$(ORLIXOS_COREUTILS_CONFIG_HEADER)" ] || { echo "missing Coreutils config snapshot: $(ORLIXOS_COREUTILS_CONFIG_HEADER)" >&2; exit 1; }; \
	rm -rf "$(ORLIXOS_COREUTILS_BUILD_DIR)"; \
	echo "wrote upstream Coreutils test list: $(ORLIXOS_COREUTILS_TEST_LIST) ($$(wc -l < "$(ORLIXOS_COREUTILS_TEST_LIST)") tests)"

$(ORLIXOS_COREUTILS_TEST_ENV): $(ORLIXOS_COREUTILS_PROOF) $(PROJECT_DIR)/Makefile
	@set -euo pipefail; \
	mkdir -p "$(dir $(ORLIXOS_COREUTILS_TEST_ENV))"; \
	{ \
		printf '%s\n' '# generated from OrlixOS/Makefile package inputs'; \
		printf 'export VERSION=%s\n' "$(COREUTILS_VERSION)"; \
		printf 'export PACKAGE_VERSION=%s\n' "$(COREUTILS_VERSION)"; \
		printf 'export PERL5LIB=/usr/lib/perl5/%s\n' "$(PERL_VERSION)"; \
		printf "export built_programs='%s'\n" "$(ORLIXOS_COREUTILS_PROGRAMS)"; \
	} > "$(ORLIXOS_COREUTILS_TEST_ENV)"; \
	echo "wrote Coreutils upstream test environment: $(ORLIXOS_COREUTILS_TEST_ENV)"

$(ORLIXOS_GETLIMITS_BINARY): $(ORLIXOS_COREUTILS_PROOF)
	@set -euo pipefail; \
	[ -x "$(ORLIXOS_GETLIMITS_BINARY)" ] || { echo "missing upstream Coreutils getlimits helper: $(ORLIXOS_GETLIMITS_BINARY)" >&2; exit 1; }; \
	file "$(ORLIXOS_GETLIMITS_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_GETLIMITS_BINARY)" >&2; exit 1; }

$(ORLIXOS_FINDUTILS_PROOF): $(ORLIXOS_FINDUTILS_SOURCE_STAMP) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB)
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	rtlib="$(ORLIXOS_MLIBC_RTLIB)"; \
	[ -s "$$sysroot/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $$sysroot/usr/lib/libc.a" >&2; exit 1; }; \
	[ -d "$$headers" ] || { echo "missing Orlix Linux UAPI headers: $$headers" >&2; exit 1; }; \
	[ -s "$$rtlib" ] || { echo "missing Orlix compiler runtime archive: $$rtlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_CC)" >/dev/null 2>&1 || { echo "clang is required to build findutils; set ORLIXOS_CC=/path/to/clang" >&2; exit 1; }; \
	command -v "$(ORLIXOS_AR)" >/dev/null 2>&1 || { echo "llvm-ar is required to build findutils; set ORLIXOS_AR=/path/to/llvm-ar" >&2; exit 1; }; \
	command -v "$(ORLIXOS_RANLIB)" >/dev/null 2>&1 || { echo "llvm-ranlib is required to build findutils; set ORLIXOS_RANLIB=/path/to/llvm-ranlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_STRIP)" >/dev/null 2>&1 || { echo "llvm-strip is required to package findutils; set ORLIXOS_STRIP=/path/to/llvm-strip" >&2; exit 1; }; \
	rm -rf "$(ORLIXOS_FINDUTILS_BUILD_DIR)"; \
	for program in $(ORLIXOS_FINDUTILS_PROGRAMS); do rm -f "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program"; done; \
	mkdir -p "$(ORLIXOS_FINDUTILS_BUILD_DIR)" "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin"; \
	cd "$(ORLIXOS_FINDUTILS_BUILD_DIR)"; \
	export CC="$(ORLIXOS_CC) --target=aarch64-linux-gnu --sysroot=$$sysroot -isystem $$headers -D_GNU_SOURCE -fhosted -fno-builtin -ffixed-x18 -fno-pie"; \
	export CFLAGS="$(ORLIXOS_PACKAGE_CFLAGS)"; \
	export LDFLAGS="--target=aarch64-linux-gnu --sysroot=$$sysroot -static -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--image-base=$(ORLIXOS_HOSTED_USER_BASE_ADDRESS) $$sysroot/usr/lib/crt1.o $$sysroot/usr/lib/crti.o -Wl,--start-group"; \
	export LIBS="$$sysroot/usr/lib/libc.a $$sysroot/usr/lib/libm.a $$sysroot/usr/lib/libpthread.a $$sysroot/usr/lib/libssp_nonshared.a $$sysroot/usr/lib/libssp.a $$rtlib -Wl,--end-group $$sysroot/usr/lib/crtn.o"; \
	export AR="$(ORLIXOS_AR)"; \
	export RANLIB="$(ORLIXOS_RANLIB)"; \
	export PATH="$(ORLIXOS_COREUTILS_BOOTSTRAP_PATH)"; \
	export gl_cv_func_getopt_gnu=yes; \
	export gl_cv_func_getopt_long_gnu=yes; \
	"$(ORLIXOS_FINDUTILS_SRC_DIR)/configure" --host=aarch64-linux-gnu --build=aarch64-apple-darwin --prefix=/usr --disable-nls --without-selinux; \
	$(MAKE) -C gl/lib -j1 all; \
	$(MAKE) -C lib -j1 all; \
	$(MAKE) -C find -j1 find; \
	$(MAKE) -C xargs -j1 xargs; \
	for program in $(ORLIXOS_FINDUTILS_PROGRAMS); do \
		"$(ORLIXOS_STRIP)" "$(ORLIXOS_FINDUTILS_BUILD_DIR)/$$program/$$program"; \
		install -m 0755 "$(ORLIXOS_FINDUTILS_BUILD_DIR)/$$program/$$program" "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program"; \
		file "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program" >&2; exit 1; }; \
	done; \
	printf 'profile=%s\ndistribution=%s\nchannel=%s\npackage=findutils\nprograms=%s\nversion=%s\nsha256=%s\n' "$(PROFILE)" "$(ORLIXOS_DISTRIBUTION_ID)" "$(ORLIXOS_DISTRIBUTION_CHANNEL)" "$(ORLIXOS_FINDUTILS_PROGRAMS)" "$(FINDUTILS_VERSION)" "$(FINDUTILS_SHA256)" > "$(ORLIXOS_FINDUTILS_PROOF)"; \
	rm -rf "$(ORLIXOS_FINDUTILS_BUILD_DIR)"; \
	echo "built Orlix Linux findutils package inputs: $(ORLIXOS_FINDUTILS_PROGRAMS)"

$(ORLIXOS_FIND_BINARY): $(ORLIXOS_FINDUTILS_PROOF)
	@set -euo pipefail; \
	[ -x "$(ORLIXOS_FIND_BINARY)" ] || { echo "missing findutils find package input: $(ORLIXOS_FIND_BINARY)" >&2; exit 1; }; \
	file "$(ORLIXOS_FIND_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_FIND_BINARY)" >&2; exit 1; }

$(ORLIXOS_XARGS_BINARY): $(ORLIXOS_FINDUTILS_PROOF)
	@set -euo pipefail; \
	[ -x "$(ORLIXOS_XARGS_BINARY)" ] || { echo "missing findutils xargs package input: $(ORLIXOS_XARGS_BINARY)" >&2; exit 1; }; \
	file "$(ORLIXOS_XARGS_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_XARGS_BINARY)" >&2; exit 1; }

$(ORLIXOS_UTIL_LINUX_PROOF): $(ORLIXOS_UTIL_LINUX_SOURCE_STAMP) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB)
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	rtlib="$(ORLIXOS_MLIBC_RTLIB)"; \
	ldflags="--target=aarch64-linux-gnu --sysroot=$$sysroot -static -no-pie -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--no-dynamic-linker -Wl,--image-base=$(ORLIXOS_HOSTED_USER_BASE_ADDRESS) $$sysroot/usr/lib/crt1.o $$sysroot/usr/lib/crti.o"; \
	libs="-Wl,--start-group $$sysroot/usr/lib/libc.a $$sysroot/usr/lib/libm.a $$sysroot/usr/lib/libpthread.a $$sysroot/usr/lib/libssp_nonshared.a $$sysroot/usr/lib/libssp.a $$rtlib -Wl,--end-group $$sysroot/usr/lib/crtn.o"; \
	[ -s "$$sysroot/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $$sysroot/usr/lib/libc.a" >&2; exit 1; }; \
	[ -d "$$headers" ] || { echo "missing Orlix Linux UAPI headers: $$headers" >&2; exit 1; }; \
	[ -s "$$rtlib" ] || { echo "missing Orlix compiler runtime archive: $$rtlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_CC)" >/dev/null 2>&1 || { echo "clang is required to build util-linux package inputs; set ORLIXOS_CC=/path/to/clang" >&2; exit 1; }; \
	command -v "$(ORLIXOS_AR)" >/dev/null 2>&1 || { echo "llvm-ar is required to package util-linux mount; set ORLIXOS_AR=/path/to/llvm-ar" >&2; exit 1; }; \
	command -v "$(ORLIXOS_RANLIB)" >/dev/null 2>&1 || { echo "llvm-ranlib is required to package util-linux mount; set ORLIXOS_RANLIB=/path/to/llvm-ranlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_STRIP)" >/dev/null 2>&1 || { echo "llvm-strip is required to package util-linux package inputs; set ORLIXOS_STRIP=/path/to/llvm-strip" >&2; exit 1; }; \
	rm -rf "$(ORLIXOS_UTIL_LINUX_BUILD_DIR)" "$(ORLIXOS_SETSID_BINARY)" "$(ORLIXOS_MOUNT_BINARY)" "$(ORLIXOS_UMOUNT_BINARY)" "$(ORLIXOS_MKFS_BINARY)" "$(ORLIXOS_UTIL_LINUX_PROOF)"; \
	mkdir -p "$(ORLIXOS_UTIL_LINUX_BUILD_DIR)" "$(dir $(ORLIXOS_SETSID_BINARY))"; \
	cd "$(ORLIXOS_UTIL_LINUX_BUILD_DIR)"; \
	export CC="$(ORLIXOS_CC) --target=aarch64-linux-gnu --sysroot=$$sysroot -isystem $$headers -D_GNU_SOURCE -fhosted -fno-builtin -ffixed-x18 -fPIC"; \
	export AR="$(ORLIXOS_AR)"; \
	export RANLIB="$(ORLIXOS_RANLIB)"; \
	export STRIP="$(ORLIXOS_STRIP)"; \
	export CFLAGS="$(ORLIXOS_PACKAGE_CFLAGS) -include signal.h"; \
	export LDFLAGS="$$ldflags"; \
	export LIBS="$$libs"; \
	"$(ORLIXOS_UTIL_LINUX_SRC_DIR)/configure" --host=aarch64-linux-gnu --build=aarch64-apple-darwin --prefix=/usr --without-python --without-systemd --without-systemdsystemunitdir --without-cap-ng --without-libz --without-libmagic --without-udev --disable-nls --enable-static --disable-shared --enable-static-programs=mount,umount,mkfs; \
	$(MAKE) -C "$(ORLIXOS_UTIL_LINUX_BUILD_DIR)" V=0 mount umount mkfs -j1 LDFLAGS="$$ldflags" LIBS="$$libs"; \
	cp "$(ORLIXOS_UTIL_LINUX_BUILD_DIR)/mount" "$(ORLIXOS_MOUNT_BINARY)"; \
	cp "$(ORLIXOS_UTIL_LINUX_BUILD_DIR)/umount" "$(ORLIXOS_UMOUNT_BINARY)"; \
	cp "$(ORLIXOS_UTIL_LINUX_BUILD_DIR)/mkfs" "$(ORLIXOS_MKFS_BINARY)"; \
	"$(ORLIXOS_STRIP)" "$(ORLIXOS_MOUNT_BINARY)"; \
	"$(ORLIXOS_STRIP)" "$(ORLIXOS_UMOUNT_BINARY)"; \
	"$(ORLIXOS_STRIP)" "$(ORLIXOS_MKFS_BINARY)"; \
	file "$(ORLIXOS_MOUNT_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_MOUNT_BINARY)" >&2; exit 1; }; \
	file "$(ORLIXOS_UMOUNT_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_UMOUNT_BINARY)" >&2; exit 1; }; \
	file "$(ORLIXOS_MKFS_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_MKFS_BINARY)" >&2; exit 1; }; \
	file "$(ORLIXOS_MOUNT_BINARY)" | grep -F -q 'statically linked' || { file "$(ORLIXOS_MOUNT_BINARY)" >&2; exit 1; }; \
	file "$(ORLIXOS_UMOUNT_BINARY)" | grep -F -q 'statically linked' || { file "$(ORLIXOS_UMOUNT_BINARY)" >&2; exit 1; }; \
	file "$(ORLIXOS_MKFS_BINARY)" | grep -F -q 'statically linked' || { file "$(ORLIXOS_MKFS_BINARY)" >&2; exit 1; }; \
	"$(ORLIXOS_CC)" --target=aarch64-linux-gnu --sysroot="$$sysroot" -isystem "$$headers" -D_GNU_SOURCE -DHAVE_CONFIG_H -include "$(ORLIXOS_UTIL_LINUX_BUILD_DIR)/config.h" -I"$(ORLIXOS_UTIL_LINUX_BUILD_DIR)" -I"$(ORLIXOS_UTIL_LINUX_SRC_DIR)/include" -I"$(ORLIXOS_UTIL_LINUX_SRC_DIR)" $(ORLIXOS_PACKAGE_CFLAGS) -fhosted -fno-builtin -ffixed-x18 -fno-pie -static -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--image-base=$(ORLIXOS_HOSTED_USER_BASE_ADDRESS) "$$sysroot/usr/lib/crt1.o" "$$sysroot/usr/lib/crti.o" "$(ORLIXOS_UTIL_LINUX_SRC_DIR)/sys-utils/setsid.c" -Wl,--start-group "$$sysroot/usr/lib/libc.a" "$$sysroot/usr/lib/libm.a" "$$sysroot/usr/lib/libpthread.a" "$$sysroot/usr/lib/libssp_nonshared.a" "$$sysroot/usr/lib/libssp.a" "$$rtlib" -Wl,--end-group "$$sysroot/usr/lib/crtn.o" -o "$(ORLIXOS_SETSID_BINARY)"; \
	"$(ORLIXOS_STRIP)" "$(ORLIXOS_SETSID_BINARY)"; \
	file "$(ORLIXOS_SETSID_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_SETSID_BINARY)" >&2; exit 1; }; \
	printf 'profile=%s\ndistribution=%s\nchannel=%s\npackage=util-linux\nprograms=setsid,mount,umount,mkfs\nversion=%s\nsha256=%s\n' "$(PROFILE)" "$(ORLIXOS_DISTRIBUTION_ID)" "$(ORLIXOS_DISTRIBUTION_CHANNEL)" "$(UTIL_LINUX_VERSION)" "$(UTIL_LINUX_SHA256)" > "$(ORLIXOS_UTIL_LINUX_PROOF)"; \
	rm -rf "$(ORLIXOS_UTIL_LINUX_BUILD_DIR)"; \
	echo "built Orlix Linux util-linux package inputs: $(ORLIXOS_SETSID_BINARY) $(ORLIXOS_MOUNT_BINARY) $(ORLIXOS_UMOUNT_BINARY) $(ORLIXOS_MKFS_BINARY)"

$(ORLIXOS_SETSID_BINARY): $(ORLIXOS_UTIL_LINUX_PROOF)
	@set -euo pipefail; \
	[ -x "$(ORLIXOS_SETSID_BINARY)" ] || { echo "missing util-linux setsid package input: $(ORLIXOS_SETSID_BINARY)" >&2; exit 1; }; \
	file "$(ORLIXOS_SETSID_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_SETSID_BINARY)" >&2; exit 1; }

$(ORLIXOS_MOUNT_BINARY): $(ORLIXOS_UTIL_LINUX_PROOF)
	@set -euo pipefail; \
	[ -x "$(ORLIXOS_MOUNT_BINARY)" ] || { echo "missing util-linux mount package input: $(ORLIXOS_MOUNT_BINARY)" >&2; exit 1; }; \
	file "$(ORLIXOS_MOUNT_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_MOUNT_BINARY)" >&2; exit 1; }; \
	file "$(ORLIXOS_MOUNT_BINARY)" | grep -F -q 'statically linked' || { file "$(ORLIXOS_MOUNT_BINARY)" >&2; exit 1; }

$(ORLIXOS_UMOUNT_BINARY): $(ORLIXOS_UTIL_LINUX_PROOF)
	@set -euo pipefail; \
	[ -x "$(ORLIXOS_UMOUNT_BINARY)" ] || { echo "missing util-linux umount package input: $(ORLIXOS_UMOUNT_BINARY)" >&2; exit 1; }; \
	file "$(ORLIXOS_UMOUNT_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_UMOUNT_BINARY)" >&2; exit 1; }; \
	file "$(ORLIXOS_UMOUNT_BINARY)" | grep -F -q 'statically linked' || { file "$(ORLIXOS_UMOUNT_BINARY)" >&2; exit 1; }

$(ORLIXOS_MKFS_BINARY): $(ORLIXOS_UTIL_LINUX_PROOF)
	@set -euo pipefail; \
	[ -x "$(ORLIXOS_MKFS_BINARY)" ] || { echo "missing util-linux mkfs package input: $(ORLIXOS_MKFS_BINARY)" >&2; exit 1; }; \
	file "$(ORLIXOS_MKFS_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_MKFS_BINARY)" >&2; exit 1; }; \
	file "$(ORLIXOS_MKFS_BINARY)" | grep -F -q 'statically linked' || { file "$(ORLIXOS_MKFS_BINARY)" >&2; exit 1; }

$(ORLIXOS_GETCONF_BINARY): $(ORLIXOS_GETCONF_SOURCE) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB)
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	rtlib="$(ORLIXOS_MLIBC_RTLIB)"; \
	command -v "$(ORLIXOS_CC)" >/dev/null 2>&1 || { echo "clang is required to build getconf; set ORLIXOS_CC=/path/to/clang" >&2; exit 1; }; \
	command -v "$(ORLIXOS_STRIP)" >/dev/null 2>&1 || { echo "llvm-strip is required to package getconf; set ORLIXOS_STRIP=/path/to/llvm-strip" >&2; exit 1; }; \
	mkdir -p "$(dir $(ORLIXOS_GETCONF_BINARY))"; \
	"$(ORLIXOS_CC)" --target=aarch64-linux-gnu --sysroot="$$sysroot" -isystem "$$headers" -D_GNU_SOURCE -std=c17 -O2 -fhosted -fno-builtin -ffixed-x18 -fno-pie -static -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--image-base=$(ORLIXOS_HOSTED_USER_BASE_ADDRESS) "$$sysroot/usr/lib/crt1.o" "$$sysroot/usr/lib/crti.o" "$(ORLIXOS_GETCONF_SOURCE)" -Wl,--start-group "$$sysroot/usr/lib/libc.a" "$$sysroot/usr/lib/libm.a" "$$sysroot/usr/lib/libpthread.a" "$$sysroot/usr/lib/libssp_nonshared.a" "$$sysroot/usr/lib/libssp.a" "$$rtlib" -Wl,--end-group "$$sysroot/usr/lib/crtn.o" -o "$(ORLIXOS_GETCONF_BINARY)"; \
	"$(ORLIXOS_STRIP)" "$(ORLIXOS_GETCONF_BINARY)"; \
	file "$(ORLIXOS_GETCONF_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_GETCONF_BINARY)" >&2; exit 1; }; \
	echo "built Orlix Linux getconf package input: $(ORLIXOS_GETCONF_BINARY)"

$(ORLIXOS_GETENT_BINARY): $(ORLIXOS_GETENT_SOURCE) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB)
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	rtlib="$(ORLIXOS_MLIBC_RTLIB)"; \
	[ -s "$$sysroot/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $$sysroot/usr/lib/libc.a" >&2; exit 1; }; \
	[ -d "$$headers" ] || { echo "missing Orlix Linux UAPI headers: $$headers" >&2; exit 1; }; \
	[ -s "$$rtlib" ] || { echo "missing Orlix compiler runtime archive: $$rtlib" >&2; exit 1; }; \
	mkdir -p "$(dir $(ORLIXOS_GETENT_BINARY))"; \
	"$(ORLIXOS_CC)" --target=aarch64-linux-gnu --sysroot="$$sysroot" -isystem "$$headers" -D_GNU_SOURCE -std=c17 -O2 -fhosted -fno-builtin -ffixed-x18 -fno-pie -static -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--image-base=$(ORLIXOS_HOSTED_USER_BASE_ADDRESS) "$$sysroot/usr/lib/crt1.o" "$$sysroot/usr/lib/crti.o" "$(ORLIXOS_GETENT_SOURCE)" -Wl,--start-group "$$sysroot/usr/lib/libc.a" "$$sysroot/usr/lib/libm.a" "$$sysroot/usr/lib/libpthread.a" "$$sysroot/usr/lib/libssp_nonshared.a" "$$sysroot/usr/lib/libssp.a" "$$rtlib" -Wl,--end-group "$$sysroot/usr/lib/crtn.o" -o "$(ORLIXOS_GETENT_BINARY)"; \
	"$(ORLIXOS_STRIP)" "$(ORLIXOS_GETENT_BINARY)"; \
	file "$(ORLIXOS_GETENT_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_GETENT_BINARY)" >&2; exit 1; }; \
	echo "built Orlix Linux getent package input: $(ORLIXOS_GETENT_BINARY)"
