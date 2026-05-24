SHELL := /bin/bash
.DEFAULT_GOAL := all
.NOTPARALLEL:

PROJECT_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO_ROOT := $(abspath $(PROJECT_DIR)/..)
KERNEL_MAKE := $(MAKE) -C $(REPO_ROOT) -f OrlixKernel/Makefile

PROFILE ?= appstore

MLIBC_REMOTE ?= https://github.com/managarm/mlibc.git
MLIBC_COMMIT ?= 43ab07732cdfefe2e915bf98190f5d7f4bd8d765
LLVM_PROJECT_REMOTE ?= https://github.com/llvm/llvm-project.git
LLVM_PROJECT_REF ?= llvmorg-22.1.6
LLVM_PROJECT_COMMIT ?= fc4aad7b5db3fff421df9a9637605b9ca5667881
MLIBC_UPSTREAM_DIR := $(REPO_ROOT)/Build/OrlixMLibC/upstream/mlibc
LLVM_PROJECT_UPSTREAM_DIR := $(REPO_ROOT)/Build/OrlixMLibC/upstream/llvm-project
MLIBC_BUILD_DIR := $(REPO_ROOT)/Build/OrlixMLibC/build/$(PROFILE)
MLIBC_SYSROOT := $(REPO_ROOT)/Build/OrlixMLibC/sysroot/$(PROFILE)
MLIBC_CROSS_FILE := $(REPO_ROOT)/Build/OrlixMLibC/cross/$(PROFILE)-orlix-aarch64-linux.cross
MLIBC_PATCH_DIR := $(PROJECT_DIR)/Sources/patches
MLIBC_KERNEL_HEADERS := $(REPO_ROOT)/Build/OrlixMLibC/kernel-headers/$(PROFILE)/include
MLIBC_BUILD_KERNEL_HEADERS := $(MLIBC_BUILD_DIR)/kernel-headers/include
MLIBC_TEST_BUILD_DIR := $(REPO_ROOT)/Build/OrlixMLibC/tests/$(PROFILE)
MLIBC_TEST_INSTALL_DIR := $(REPO_ROOT)/Build/OrlixMLibC/tests-install/$(PROFILE)
MLIBC_TEST_INITRAMFS_DIR := $(REPO_ROOT)/Build/OrlixMLibC/test-initramfs/$(PROFILE)/OrlixMLibCTestInitramfs.bundle
MLIBC_TEST_LOCALE_DATA_DIR := $(REPO_ROOT)/Build/OrlixMLibC/locale-data/$(PROFILE)
MLIBC_TEST_LOCALE_ROOT := $(MLIBC_TEST_LOCALE_DATA_DIR)/root
MLIBC_TEST_LOCALES_ALL_URL ?= https://deb.debian.org/debian/pool/main/g/glibc/locales-all_2.41-12+deb13u3_arm64.deb
MLIBC_TEST_LOCALES_ALL_SHA256 ?= 869275c8dcbfc55823f4658b9aeaf3d8f436392578607b241f51d7b04e89b5ef
MLIBC_TEST_LIBC_BIN_URL ?= https://deb.debian.org/debian/pool/main/g/glibc/libc-bin_2.41-12+deb13u3_arm64.deb
MLIBC_TEST_LIBC_BIN_SHA256 ?= 02f366115bea79b87fe26c7dec4dc444f9fdcc76440905d88cc26dbef075511b
MLIBC_RTLIB_DIR := $(REPO_ROOT)/Build/OrlixMLibC/compiler-rt/$(PROFILE)
MLIBC_RTLIB := $(MLIBC_RTLIB_DIR)/liborlix_compiler_rt.a
MLIBC_LOCK_ROOT := $(REPO_ROOT)/Build/OrlixMLibC/locks
ORLIX_HOSTED_SYSCALL_GATE_ADDRESS ?= 0x00006ffff0000000
ORLIX_HOSTED_USER_BASE_ADDRESS ?= 0x0000600000000000
# Apple arm64 reserves x18 for the host platform; Orlix-built Linux ELF keeps it unavailable to user code.
MLIBC_HOSTED_EXEC_FIXED_REGISTER_FLAGS := -ffixed-x18
MLIBC_HOSTED_EXEC_CODEGEN_FLAGS := -femulated-tls $(MLIBC_HOSTED_EXEC_FIXED_REGISTER_FLAGS)
MLIBC_HOSTED_EXEC_MESON_ARGS := '-DORLIX_HOSTED_EXEC=1', '-DORLIX_HOSTED_SYSCALL_GATE_ADDRESS=$(ORLIX_HOSTED_SYSCALL_GATE_ADDRESS)', '-femulated-tls', '-ffixed-x18', '-ffunction-sections', '-fdata-sections'
MLIBC_LLVM_PREFIX ?= $(firstword $(wildcard /opt/homebrew/opt/llvm/bin) $(wildcard /usr/local/opt/llvm/bin))
MLIBC_CC ?= clang
MLIBC_CXX ?= clang++
MLIBC_AR ?= $(if $(MLIBC_LLVM_PREFIX),$(MLIBC_LLVM_PREFIX)/llvm-ar,llvm-ar)
MLIBC_STRIP ?= $(if $(MLIBC_LLVM_PREFIX),$(MLIBC_LLVM_PREFIX)/llvm-strip,llvm-strip)
MESON ?= meson
NINJA ?= ninja
MLIBC_TEST_ANSI_CASES := \
	ansi/alloc \
	ansi/sscanf \
	ansi/sprintf \
	ansi/snprintf \
	ansi/utf8 \
	ansi/strtol \
	ansi/strtof \
	ansi/abs \
	ansi/longjmp \
	ansi/mbrtoc32 \
	ansi/strftime \
	ansi/strchr \
	ansi/strrchr \
	ansi/wcsrtombs \
	ansi/mbsrtowcs \
	ansi/mbsnrtowcs \
	ansi/wcsnrtombs \
	ansi/mblen \
	ansi/mbrlen \
	ansi/wmemcmp \
	ansi/timegm \
	ansi/tz \
	ansi/ungetc \
	ansi/fopen \
	ansi/creal-cimag \
	ansi/qsort \
	ansi/freopen \
	ansi/strxfrm \
	ansi/calloc \
	ansi/fgetpos \
	ansi/fputs \
	ansi/ftell \
	ansi/tmpfile \
	ansi/mbstowcs \
	ansi/locale \
	ansi/asctime \
	ansi/wcstof \
	ansi/fputwc \
	ansi/mbrtowc \
	ansi/ungetwc \
	ansi/fgetws \
	ansi/swscanf \
	ansi/fwscanf \
	ansi/swprintf \
	ansi/wcsftime \
	ansi/wctype \
	ansi/wctrans \
	ansi/nanosleep \
	ansi/btowc \
	ansi/wctob
MLIBC_TEST_POSIX_CASES := \
	posix/inet_ntop \
	posix/inet_pton \
	posix/access \
	posix/pwd \
	posix/fdopen \
	posix/fopencookie \
	posix/fmemopen \
	posix/getdelim \
	posix/grp \
	posix/dprintf \
	posix/posix_memalign \
	posix/index \
	posix/rindex \
	posix/search \
	posix/semaphore \
	posix/open_memstream \
	posix/time \
	posix/realpath \
	posix/ffs \
	posix/getcwd \
	posix/memrchr \
	posix/rlimits \
	posix/setpriority \
	posix/nice \
	posix/timer \
	posix/wcwidth \
	posix/basename \
	posix/regex \
	posix/readv-writev \
	posix/if_indextoname \
	posix/getnameinfo \
	posix/gethostbyname \
	posix/getservbyname \
	posix/getservbyport \
	posix/strdupa \
	posix/mkstemp \
	posix/shm \
	posix/swab \
	posix/select \
	posix/wcsdup \
	posix/wcsncasecmp \
	posix/getsubopt \
	posix/locale \
	posix/jrand48 \
	posix/memccpy \
	posix/strfmon \
	posix/sig2str \
	posix/inet_aton \
	posix/protoent \
	posix/lfind \
	posix/lsearch \
	posix/nftw \
	posix/wcslcat \
	posix/wcslcpy \
	posix/wcpcpy \
	posix/wcpncpy \
	posix/getwchar \
	posix/strcoll_l \
	posix/strxfrm_l \
	posix/wcscoll_l \
	posix/wcsxfrm_l
MLIBC_TEST_BSD_CASES := \
	bsd/ns_get_put \
	bsd/reallocarray \
	bsd/strl \
	bsd/sbrk \
	bsd/getloadavg
MLIBC_TEST_GLIBC_CASES := \
	glibc/getopt \
	glibc/ffsl-ffsll \
	glibc/rpmatch \
	glibc/strverscmp \
	glibc/memmem \
	glibc/stdio_ext
MLIBC_TEST_LINUX_CASES := \
	linux/malloc-usable-size \
	linux/pthread_setname_np \
	linux/pthread_attr \
	linux/cpuset \
	linux/getifaddrs \
	linux/pidfd \
	linux/timerfd \
	linux/utmp \
	linux/utmpx
MLIBC_TEST_CASES ?= $(MLIBC_TEST_ANSI_CASES) $(MLIBC_TEST_POSIX_CASES) $(MLIBC_TEST_BSD_CASES) $(MLIBC_TEST_GLIBC_CASES) $(MLIBC_TEST_LINUX_CASES)
MLIBC_COMPILER_RT_BUILTINS := \
	addtf3.c \
	aarch64/fp_mode.c \
	comparetf2.c \
	divtf3.c \
	extenddftf2.c \
	extendhftf2.c \
	extendsftf2.c \
	extendxftf2.c \
	fixtfdi.c \
	fixtfsi.c \
	fixtfti.c \
	fixunstfdi.c \
	fixunstfsi.c \
	fixunstfti.c \
	floatditf.c \
	floatsitf.c \
	floattitf.c \
	floatunditf.c \
	floatunsitf.c \
	floatuntitf.c \
	multf3.c \
	powitf2.c \
	subtf3.c \
	trunctfbf2.c \
	trunctfdf2.c \
	trunctfhf2.c \
	trunctfsf2.c \
	trunctfxf2.c

.PHONY: all help build headers_install test kselftest clean mrproper __build __headers-install __ensure-kernel-headers __prepare-test-locale-data __test-build __test-initramfs __test-run __clean __bootstrap-mlibc __apply-mlibc-patches __bootstrap-compiler-rt __build-compiler-rt __write-cross-file

define orlix_mlibc_with_profile_lock
	@set -euo pipefail; \
	lock_root="$(MLIBC_LOCK_ROOT)"; \
	for path in Build Build/OrlixMLibC "$$lock_root"; do \
		if [ -e "$$path" ] && [ -L "$$path" ]; then echo "refusing to use symlinked OrlixMLibC lock path: $$path" >&2; exit 1; fi; \
	done; \
	if [ "$${ORLIX_MLIBC_PROFILE_LOCK_HELD:-0}" = 1 ]; then \
		$(MAKE) -f "$(PROJECT_DIR)/Makefile" $(1) PROFILE="$(PROFILE)"; \
	else \
		mkdir -p "$$lock_root"; \
		lock_dir="$$lock_root/$(PROFILE).lock.dir"; \
		while ! mkdir "$$lock_dir" 2>/dev/null; do \
			if [ -s "$$lock_dir/pid" ] && ! kill -0 "$$(cat "$$lock_dir/pid")" 2>/dev/null; then rm -rf "$$lock_dir"; continue; fi; \
			sleep 1; \
		done; \
		printf '%s\n' "$$$$" > "$$lock_dir/pid"; \
		trap 'rm -rf "$$lock_dir"' EXIT INT TERM; \
		ORLIX_MLIBC_PROFILE_LOCK_HELD=1 $(MAKE) -f "$(PROJECT_DIR)/Makefile" $(1) PROFILE="$(PROFILE)"; \
	fi
endef

all: build

help:
	@printf '%s\n' 'Targets:'
	@printf '%s\n' '  build             build OrlixMLibC from upstream mlibc sysdeps/linux'
	@printf '%s\n' '  headers_install   install Linux UAPI headers for OrlixMLibC'
	@printf '%s\n' '  test              run selected upstream mlibc tests as Orlix Linux ELF'
	@printf '%s\n' '  kselftest         package OrlixMLibC-built kselftests'
	@printf '%s\n' '  clean             remove generated OrlixMLibC outputs'

build:
	$(call orlix_mlibc_with_profile_lock,__build)

__build: __write-cross-file
	@set -euo pipefail; \
	command -v "$(MESON)" >/dev/null 2>&1 || { echo "meson is required to build OrlixMLibC" >&2; exit 1; }; \
	command -v "$(NINJA)" >/dev/null 2>&1 || { echo "ninja is required to build OrlixMLibC" >&2; exit 1; }; \
	$(MAKE) -f "$(PROJECT_DIR)/Makefile" __ensure-kernel-headers PROFILE="$(PROFILE)"; \
	[ -d "$(MLIBC_KERNEL_HEADERS)" ] || { echo "missing installed Orlix Linux UAPI headers: $(MLIBC_KERNEL_HEADERS)" >&2; exit 1; }; \
	rm -rf "$(MLIBC_BUILD_DIR)" "$(MLIBC_SYSROOT)"; \
	mkdir -p "$(MLIBC_BUILD_DIR)" "$(MLIBC_SYSROOT)"; \
	mkdir -p "$(dir $(MLIBC_BUILD_KERNEL_HEADERS))"; \
	cp -R "$(MLIBC_KERNEL_HEADERS)" "$(MLIBC_BUILD_KERNEL_HEADERS)"; \
	[ -d "$(MLIBC_BUILD_KERNEL_HEADERS)" ] || { echo "missing OrlixMLibC build-local Linux UAPI headers: $(MLIBC_BUILD_KERNEL_HEADERS)" >&2; exit 1; }; \
	"$(MESON)" setup "$(MLIBC_BUILD_DIR)" "$(MLIBC_UPSTREAM_DIR)" \
		--cross-file "$(MLIBC_CROSS_FILE)" \
		--prefix /usr \
		-Ddefault_library=static \
		-Dheaders_only=false \
		-Dno_headers=false \
		-Dbuild_tests=false \
		-Dlibgcc_dependency=false \
		-Dlinux_option=enabled \
		-Dposix_option=enabled \
		-Dglibc_option=enabled \
		-Dbsd_option=enabled \
		-Dlinux_kernel_headers="$(MLIBC_BUILD_KERNEL_HEADERS)"; \
	"$(MESON)" compile -C "$(MLIBC_BUILD_DIR)"; \
	DESTDIR="$(MLIBC_SYSROOT)" "$(MESON)" install -C "$(MLIBC_BUILD_DIR)"; \
	[ -d "$(MLIBC_SYSROOT)/usr/include" ] || { echo "missing OrlixMLibC installed headers: $(MLIBC_SYSROOT)/usr/include" >&2; exit 1; }; \
	[ -d "$(MLIBC_SYSROOT)/usr/lib" ] || { echo "missing OrlixMLibC installed libraries: $(MLIBC_SYSROOT)/usr/lib" >&2; exit 1; }; \
	printf 'profile=%s\nmlibc_commit=%s\n' "$(PROFILE)" "$(MLIBC_COMMIT)" > "$(MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready"; \
	echo "built OrlixMLibC sysroot: $(MLIBC_SYSROOT)"

headers_install:
	$(call orlix_mlibc_with_profile_lock,__headers-install)

__headers-install:
	@$(KERNEL_MAKE) headers_install PROFILE="$(PROFILE)"

__ensure-kernel-headers:
	@set -euo pipefail; \
	if [ ! -d "$(MLIBC_KERNEL_HEADERS)" ]; then \
		$(KERNEL_MAKE) headers_install PROFILE="$(PROFILE)"; \
	fi; \
	[ -d "$(MLIBC_KERNEL_HEADERS)" ] || { echo "missing installed Orlix Linux UAPI headers: $(MLIBC_KERNEL_HEADERS)" >&2; exit 1; }

kselftest: build
	@$(KERNEL_MAKE) kselftest PROFILE="$(PROFILE)" libc=orlixmlibc

test:
	$(call orlix_mlibc_with_profile_lock,__test-run)

__test-run: __test-initramfs
	@set -euo pipefail; \
	run_log="$(MLIBC_TEST_BUILD_DIR)/orlix-kernel-run-output.log"; \
	mkdir -p "$(MLIBC_TEST_BUILD_DIR)"; \
	$(KERNEL_MAKE) run PROFILE="$(PROFILE)" type=mlibc libc=orlixmlibc ORLIX_KERNEL_TEST_INITRAMFS_INPUT="$(MLIBC_TEST_INITRAMFS_DIR)/rootfs/initramfs.cpio.gz" | tee "$$run_log"; \
	runtime_log="$$(sed -n 's/.*"runtimeLogPath": "\([^"]*\)".*/\1/p' "$$run_log" | tail -n 1)"; \
	[ -n "$$runtime_log" ] || { echo "missing OrlixTerminal runtime log path in $$run_log" >&2; exit 1; }; \
	for _ in $$(seq 1 50); do [ -s "$$runtime_log" ] && break; sleep 0.1; done; \
	[ -s "$$runtime_log" ] || { echo "missing OrlixTerminal runtime log: $$runtime_log" >&2; exit 1; }; \
	for _ in $$(seq 1 600); do \
		if grep -E -q '(^|[^[:alnum:]])not ok[[:space:]][0-9]|Kernel panic|Out of memory|oom-killer|Assertion .* failed' "$$runtime_log"; then break; fi; \
		grep -F -q 'ORLIX-MLIBC-TEST-END' "$$runtime_log" && break; \
		sleep 0.1; \
	done; \
	if grep -E '(^|[^[:alnum:]])not ok[[:space:]][0-9]|Kernel panic|Out of memory|oom-killer|Assertion .* failed' "$$runtime_log"; then \
		echo "OrlixMLibC tests reported failure in runtime log: $$runtime_log" >&2; \
		exit 1; \
	fi; \
	grep -F -q 'ORLIX-MLIBC-TEST-END' "$$runtime_log" || { echo "OrlixMLibC tests did not reach completion marker: $$runtime_log" >&2; exit 1; }; \
	echo "verified OrlixMLibC upstream tests in simulator log: $$runtime_log"

__test-build: build __build-compiler-rt
	@set -euo pipefail; \
	sysroot="$(MLIBC_SYSROOT)"; \
	test_build="$(MLIBC_TEST_BUILD_DIR)"; \
	install_dir="$(MLIBC_TEST_INSTALL_DIR)"; \
	rm -rf "$$test_build" "$$install_dir"; \
	mkdir -p "$$test_build" "$$install_dir/mlibc-tests"; \
	crt_flags="$$sysroot/usr/lib/crt1.o $$sysroot/usr/lib/crti.o"; \
	ldlibs="-Wl,--start-group $$sysroot/usr/lib/libc.a $$sysroot/usr/lib/libm.a $$sysroot/usr/lib/libpthread.a $$sysroot/usr/lib/libssp_nonshared.a $$sysroot/usr/lib/libssp.a $(MLIBC_RTLIB) -Wl,--end-group $$sysroot/usr/lib/crtn.o"; \
	cflags="--target=aarch64-linux-gnu --sysroot=$$sysroot -isystem $(MLIBC_KERNEL_HEADERS) -D_GNU_SOURCE -DUSE_CROSS_LIBC -U__MLIBC_DEBUG -U__MLIBC_BUILDING_MLIBC -fhosted -fno-builtin $(MLIBC_HOSTED_EXEC_CODEGEN_FLAGS) -fno-pie -Wno-unknown-warning-option"; \
	ldflags="--target=aarch64-linux-gnu --sysroot=$$sysroot -static -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--image-base=$(ORLIX_HOSTED_USER_BASE_ADDRESS)"; \
	[ -s "$$sysroot/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $$sysroot/usr/lib/libc.a" >&2; exit 1; }; \
	command -v "$(MLIBC_STRIP)" >/dev/null 2>&1 || { echo "llvm-strip is required to package OrlixMLibC tests; set MLIBC_STRIP=/path/to/llvm-strip" >&2; exit 1; }; \
	for test_name in $(MLIBC_TEST_CASES); do \
		source="$(MLIBC_UPSTREAM_DIR)/tests/$$test_name.c"; \
		binary_name="$${test_name////-}"; \
		[ -s "$$source" ] || { echo "missing upstream mlibc test source: $$source" >&2; exit 1; }; \
		"$(MLIBC_CC)" $$cflags $$ldflags $$crt_flags "$$source" $$ldlibs -o "$$install_dir/mlibc-tests/$$binary_name"; \
		"$(MLIBC_STRIP)" "$$install_dir/mlibc-tests/$$binary_name"; \
		printf '%s:%s\n' "$$test_name" "/mlibc-tests/$$binary_name" >> "$$install_dir/mlibc-test-list.txt"; \
	done; \
	"$(MLIBC_CC)" $$cflags $$ldflags $$crt_flags "$(PROJECT_DIR)/Tests/mlibc_test_init.c" $$ldlibs -o "$$install_dir/init"; \
	"$(MLIBC_STRIP)" "$$install_dir/init"; \
	printf 'proof_lane=mlibc-upstream-libc-tests\nprofile=%s\n' "$(PROFILE)" > "$$install_dir/proof_lane.txt"; \
	echo "built OrlixMLibC upstream test programs: $$install_dir"

__prepare-test-locale-data:
	@set -euo pipefail; \
	locale_dir="$(MLIBC_TEST_LOCALE_DATA_DIR)"; \
	locale_root="$(MLIBC_TEST_LOCALE_ROOT)"; \
	locale_unpack="$$locale_dir/unpack"; \
	case "$$locale_dir" in "$(REPO_ROOT)"/Build/OrlixMLibC/locale-data/*) ;; *) echo "refusing to prepare OrlixMLibC locale data outside Build/OrlixMLibC/locale-data: $$locale_dir" >&2; exit 1 ;; esac; \
	for path in "$(REPO_ROOT)/Build" "$(REPO_ROOT)/Build/OrlixMLibC" "$(REPO_ROOT)/Build/OrlixMLibC/locale-data" "$$locale_dir"; do \
		if [ -L "$$path" ]; then echo "refusing to prepare OrlixMLibC locale data through symlinked path: $$path" >&2; exit 1; fi; \
	done; \
	command -v curl >/dev/null 2>&1 || { echo "curl is required to fetch OrlixMLibC locale test data" >&2; exit 1; }; \
	command -v ar >/dev/null 2>&1 || { echo "ar is required to unpack OrlixMLibC locale test data" >&2; exit 1; }; \
	command -v tar >/dev/null 2>&1 || { echo "tar is required to unpack OrlixMLibC locale test data" >&2; exit 1; }; \
	command -v shasum >/dev/null 2>&1 || { echo "shasum is required to verify OrlixMLibC locale test data" >&2; exit 1; }; \
	rm -rf "$$locale_dir"; \
	mkdir -p "$$locale_dir/packages" "$$locale_root/usr/lib/locale" "$$locale_unpack"; \
	locales_all_pkg="$$locale_dir/packages/locales-all.deb"; \
	libc_bin_pkg="$$locale_dir/packages/libc-bin.deb"; \
	curl -fL -o "$$locales_all_pkg" "$(MLIBC_TEST_LOCALES_ALL_URL)"; \
	printf '%s  %s\n' "$(MLIBC_TEST_LOCALES_ALL_SHA256)" "$$locales_all_pkg" | shasum -a 256 -c - >/dev/null; \
	curl -fL -o "$$libc_bin_pkg" "$(MLIBC_TEST_LIBC_BIN_URL)"; \
	printf '%s  %s\n' "$(MLIBC_TEST_LIBC_BIN_SHA256)" "$$libc_bin_pkg" | shasum -a 256 -c - >/dev/null; \
	ar p "$$locales_all_pkg" data.tar.xz > "$$locale_dir/locales-all-data.tar.xz"; \
	ar p "$$libc_bin_pkg" data.tar.xz > "$$locale_dir/libc-bin-data.tar.xz"; \
	tar -xJf "$$locale_dir/locales-all-data.tar.xz" -C "$$locale_unpack" ./usr/lib/locale; \
	tar -xJf "$$locale_dir/libc-bin-data.tar.xz" -C "$$locale_unpack" ./usr/lib/locale/C.utf8; \
	for locale_name in C.utf8 en_US.utf8 de_DE de_DE.utf8 ru_RU.utf8 fr_FR.utf8; do \
		[ -d "$$locale_unpack/usr/lib/locale/$$locale_name" ] || { echo "missing $$locale_name locale test data in package" >&2; exit 1; }; \
		cp -R -L "$$locale_unpack/usr/lib/locale/$$locale_name" "$$locale_root/usr/lib/locale/"; \
	done; \
	[ -s "$$locale_root/usr/lib/locale/en_US.utf8/LC_CTYPE" ] || { echo "missing en_US.utf8 locale test data" >&2; exit 1; }; \
	[ -s "$$locale_root/usr/lib/locale/C.utf8/LC_CTYPE" ] || { echo "missing C.utf8 locale test data" >&2; exit 1; }; \
	echo "prepared OrlixMLibC locale test data: $$locale_root/usr/lib/locale"

__test-initramfs: __test-build __prepare-test-locale-data
	@set -euo pipefail; \
	output="$(MLIBC_TEST_INITRAMFS_DIR)"; \
	install_dir="$(MLIBC_TEST_INSTALL_DIR)"; \
	locale_root="$(MLIBC_TEST_LOCALE_ROOT)"; \
	$(KERNEL_MAKE) prepare PROFILE="$(PROFILE)"; \
	gen_init_cpio="$(REPO_ROOT)/Build/OrlixKernel/build/$(PROFILE)/usr/gen_init_cpio"; \
	case "$$output" in "$(REPO_ROOT)"/Build/OrlixMLibC/test-initramfs/*) ;; *) echo "refusing to write OrlixMLibC test initramfs outside Build/OrlixMLibC/test-initramfs: $$output" >&2; exit 1 ;; esac; \
	for path in "$(REPO_ROOT)/Build" "$(REPO_ROOT)/Build/OrlixMLibC" "$(REPO_ROOT)/Build/OrlixMLibC/test-initramfs" "$$output"; do \
		if [ -L "$$path" ]; then echo "refusing to package OrlixMLibC test initramfs through symlinked path: $$path" >&2; exit 1; fi; \
	done; \
	[ -x "$$gen_init_cpio" ] || { echo "missing executable Linux gen_init_cpio: $$gen_init_cpio" >&2; exit 1; }; \
	[ -s "$$install_dir/init" ] || { echo "missing OrlixMLibC test init: $$install_dir/init" >&2; exit 1; }; \
	[ -s "$$install_dir/mlibc-test-list.txt" ] || { echo "missing OrlixMLibC test list: $$install_dir/mlibc-test-list.txt" >&2; exit 1; }; \
	rm -rf "$$output"; \
	mkdir -p "$$output/rootfs"; \
	mkdir -p "$$output/fixtures"; \
	printf '%s\n' 'root:x:0:0:root:/root:/bin/sh' > "$$output/fixtures/passwd"; \
	printf '%s\n' 'root:x:0:root' > "$$output/fixtures/group"; \
	{ \
		printf '%s\n' 'ip 0 IP'; \
		printf '%s\n' 'icmp 1 ICMP'; \
		printf '%s\n' 'tcp 6 TCP'; \
		printf '%s\n' 'udp 17 UDP'; \
	} > "$$output/fixtures/protocols"; \
	{ \
		printf '%s\n' '127.0.0.1 localhost localhost.localdomain'; \
		printf '%s\n' '::1 localhost localhost.localdomain'; \
		printf '%s\n' '8.8.8.8 dns.google'; \
		printf '%s\n' '93.184.216.34 example.net'; \
		printf '%s\n' '2606:2800:220:1:248:1893:25c8:1946 example.net'; \
	} > "$$output/fixtures/hosts"; \
	{ \
		printf '%s\n' 'http 80/tcp'; \
		printf '%s\n' 'https 443/tcp'; \
		printf '%s\n' 'domain 53/tcp'; \
		printf '%s\n' 'domain 53/udp'; \
		printf '%s\n' 'babel 6696/udp'; \
	} > "$$output/fixtures/services"; \
	cpio_list="$$output/initramfs.list"; \
	{ \
		printf '%s\n' 'dir /bin 755 0 0'; \
		printf '%s\n' 'dir /dev 755 0 0'; \
		printf '%s\n' 'dir /etc 755 0 0'; \
		printf '%s\n' 'dir /proc 555 0 0'; \
		printf '%s\n' 'dir /root 700 0 0'; \
		printf '%s\n' 'dir /tmp 1777 0 0'; \
		printf '%s\n' 'dir /mlibc-tests 755 0 0'; \
		printf '%s\n' 'nod /dev/console 600 0 0 c 5 1'; \
		printf '%s\n' 'nod /dev/zero 666 0 0 c 1 5'; \
		printf 'file /etc/passwd %s 644 0 0\n' "$$output/fixtures/passwd"; \
		printf 'file /etc/group %s 644 0 0\n' "$$output/fixtures/group"; \
		printf 'file /etc/hosts %s 644 0 0\n' "$$output/fixtures/hosts"; \
		printf 'file /etc/protocols %s 644 0 0\n' "$$output/fixtures/protocols"; \
		printf 'file /etc/services %s 644 0 0\n' "$$output/fixtures/services"; \
		find "$$locale_root/usr" -type d -print | sort | while IFS= read -r path; do \
			printf 'dir /%s 755 0 0\n' "$${path#$$locale_root/}"; \
		done; \
		find "$$locale_root/usr" -type f -print | sort | while IFS= read -r path; do \
			printf 'file /%s %s 644 0 0\n' "$${path#$$locale_root/}" "$$path"; \
		done; \
		printf 'file /init %s 755 0 0\n' "$$install_dir/init"; \
		printf 'file /mlibc-test-list.txt %s 644 0 0\n' "$$install_dir/mlibc-test-list.txt"; \
		while IFS=: read -r test_name test_path; do \
			binary_name="$${test_path#/mlibc-tests/}"; \
			test_binary="$$install_dir/mlibc-tests/$$binary_name"; \
			[ -s "$$test_binary" ] || { echo "missing OrlixMLibC test binary: $$test_binary" >&2; exit 1; }; \
			printf 'file %s %s 755 0 0\n' "$$test_path" "$$test_binary"; \
		done < "$$install_dir/mlibc-test-list.txt"; \
	} > "$$cpio_list"; \
	"$$gen_init_cpio" "$$cpio_list" > "$$output/rootfs/initramfs.cpio"; \
	gzip -n -f "$$output/rootfs/initramfs.cpio"; \
	{ \
		printf '%s\n' '<?xml version="1.0" encoding="UTF-8"?>'; \
		printf '%s\n' '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">'; \
		printf '%s\n' '<plist version="1.0"><dict>'; \
		printf '%s\n' '<key>CFBundleIdentifier</key><string>org.orlix.OrlixMLibCTestInitramfs</string>'; \
		printf '%s\n' '<key>CFBundleName</key><string>OrlixMLibCTestInitramfs</string>'; \
		printf '%s\n' '<key>CFBundlePackageType</key><string>BNDL</string>'; \
		printf '%s\n' '<key>CFBundleShortVersionString</key><string>0.1</string>'; \
		printf '%s\n' '<key>CFBundleVersion</key><string>1</string>'; \
		printf '%s\n' '<key>OrlixProfile</key><string>$(PROFILE)</string>'; \
		printf '%s\n' '<key>OrlixProofLane</key><string>mlibc-upstream-libc-tests</string>'; \
		printf '%s\n' '</dict></plist>'; \
	} > "$$output/Info.plist"; \
	plutil -lint "$$output/Info.plist" >/dev/null; \
	echo "packaged OrlixMLibC test initramfs: $$output"

clean:
	$(call orlix_mlibc_with_profile_lock,__clean)

__clean:
	@set -euo pipefail; \
	for path in \
		"$(REPO_ROOT)/Build/OrlixMLibC/build" \
		"$(REPO_ROOT)/Build/OrlixMLibC/compiler-rt" \
		"$(REPO_ROOT)/Build/OrlixMLibC/cross" \
		"$(REPO_ROOT)/Build/OrlixMLibC/kselftest" \
		"$(REPO_ROOT)/Build/OrlixMLibC/locale-data" \
		"$(REPO_ROOT)/Build/OrlixMLibC/sysroot" \
		"$(REPO_ROOT)/Build/OrlixMLibC/tests" \
		"$(REPO_ROOT)/Build/OrlixMLibC/tests-install" \
		"$(REPO_ROOT)/Build/OrlixMLibC/test-initramfs" \
		"$(REPO_ROOT)/Build/OrlixMLibC/upstream"; do \
		if [ -L "$$path" ]; then echo "refusing to clean symlinked path: $$path" >&2; exit 1; fi; \
		rm -rf "$$path"; \
	done; \
	echo "cleaned generated OrlixMLibC outputs"

mrproper: clean

__bootstrap-mlibc:
	@set -euo pipefail; \
	for path in "$(REPO_ROOT)/Build" "$(REPO_ROOT)/Build/OrlixMLibC" "$(REPO_ROOT)/Build/OrlixMLibC/upstream" "$(MLIBC_UPSTREAM_DIR)"; do \
		if [ -L "$$path" ]; then echo "refusing to use symlinked mlibc path: $$path" >&2; exit 1; fi; \
	done; \
	rm -rf "$(MLIBC_UPSTREAM_DIR)"; \
	mkdir -p "$(dir $(MLIBC_UPSTREAM_DIR))"; \
	git clone --no-checkout "$(MLIBC_REMOTE)" "$(MLIBC_UPSTREAM_DIR)"; \
	git -C "$(MLIBC_UPSTREAM_DIR)" checkout --quiet --detach "$(MLIBC_COMMIT)"; \
	checked_commit="$$(git -C "$(MLIBC_UPSTREAM_DIR)" rev-parse HEAD)"; \
	if [ "$$checked_commit" != "$(MLIBC_COMMIT)" ]; then \
		echo "expected mlibc commit $(MLIBC_COMMIT), checked out $$checked_commit" >&2; \
		exit 1; \
	fi; \
	echo "upstream mlibc ready: $(MLIBC_UPSTREAM_DIR) ($$checked_commit)"

__apply-mlibc-patches: __bootstrap-mlibc
	@set -euo pipefail; \
	[ -d "$(MLIBC_UPSTREAM_DIR)/.git" ] || { echo "missing upstream mlibc clone: $(MLIBC_UPSTREAM_DIR)" >&2; exit 1; }; \
	if [ -d "$(MLIBC_PATCH_DIR)" ]; then \
		for patch in "$(MLIBC_PATCH_DIR)"/*.patch; do \
			[ -e "$$patch" ] || continue; \
			git -C "$(MLIBC_UPSTREAM_DIR)" apply --check "$$patch"; \
			git -C "$(MLIBC_UPSTREAM_DIR)" apply "$$patch"; \
		done; \
	fi; \
	git -C "$(MLIBC_UPSTREAM_DIR)" diff --check; \
	echo "applied OrlixMLibC upstream mlibc patches"

__bootstrap-compiler-rt:
	@set -euo pipefail; \
	for path in "$(REPO_ROOT)/Build" "$(REPO_ROOT)/Build/OrlixMLibC" "$(REPO_ROOT)/Build/OrlixMLibC/upstream" "$(LLVM_PROJECT_UPSTREAM_DIR)"; do \
		if [ -L "$$path" ]; then echo "refusing to use symlinked compiler-rt path: $$path" >&2; exit 1; fi; \
	done; \
	rm -rf "$(LLVM_PROJECT_UPSTREAM_DIR)"; \
	mkdir -p "$(dir $(LLVM_PROJECT_UPSTREAM_DIR))"; \
	git clone --depth 1 --branch "$(LLVM_PROJECT_REF)" --filter=blob:none --sparse "$(LLVM_PROJECT_REMOTE)" "$(LLVM_PROJECT_UPSTREAM_DIR)"; \
	git -C "$(LLVM_PROJECT_UPSTREAM_DIR)" sparse-checkout set compiler-rt/lib/builtins; \
	checked_commit="$$(git -C "$(LLVM_PROJECT_UPSTREAM_DIR)" rev-parse HEAD)"; \
	if [ "$$checked_commit" != "$(LLVM_PROJECT_COMMIT)" ]; then \
		echo "expected llvm-project commit $(LLVM_PROJECT_COMMIT), checked out $$checked_commit" >&2; \
		exit 1; \
	fi; \
	echo "upstream compiler-rt ready: $(LLVM_PROJECT_UPSTREAM_DIR) ($$checked_commit)"

__build-compiler-rt: __bootstrap-compiler-rt
	@set -euo pipefail; \
	rtlib_dir="$(MLIBC_RTLIB_DIR)"; \
	builtins_dir="$(LLVM_PROJECT_UPSTREAM_DIR)/compiler-rt/lib/builtins"; \
	rm -rf "$$rtlib_dir"; \
	mkdir -p "$$rtlib_dir/objects"; \
	for source_name in $(MLIBC_COMPILER_RT_BUILTINS); do \
		source="$$builtins_dir/$$source_name"; \
		object_name="$${source_name////-}"; \
		object="$$rtlib_dir/objects/$${object_name%.c}.o"; \
		[ -s "$$source" ] || { echo "missing compiler-rt builtin source: $$source" >&2; exit 1; }; \
		"$(MLIBC_CC)" --target=aarch64-linux-gnu -ffreestanding -fno-builtin $(MLIBC_HOSTED_EXEC_FIXED_REGISTER_FLAGS) -O2 -I"$$builtins_dir" -c "$$source" -o "$$object"; \
	done; \
	"$(MLIBC_AR)" rcs "$(MLIBC_RTLIB)" "$$rtlib_dir"/objects/*.o; \
	[ -s "$(MLIBC_RTLIB)" ] || { echo "missing Orlix compiler runtime archive: $(MLIBC_RTLIB)" >&2; exit 1; }; \
	echo "built Orlix compiler runtime archive: $(MLIBC_RTLIB)"

__write-cross-file: __apply-mlibc-patches
	@set -euo pipefail; \
	mkdir -p "$(dir $(MLIBC_CROSS_FILE))"; \
	{ \
		printf '%s\n' '[binaries]'; \
		printf "c = ['%s', '--target=aarch64-linux-gnu']\n" "$(MLIBC_CC)"; \
		printf "cpp = ['%s', '--target=aarch64-linux-gnu']\n" "$(MLIBC_CXX)"; \
		printf "ar = '%s'\n" "$(MLIBC_AR)"; \
		printf "strip = '%s'\n" "$(MLIBC_STRIP)"; \
		printf '%s\n' ''; \
		printf '%s\n' '[host_machine]'; \
		printf "%s\n" "system = 'linux'"; \
		printf "%s\n" "cpu_family = 'aarch64'"; \
		printf "%s\n" "cpu = 'aarch64'"; \
		printf "%s\n" "endian = 'little'"; \
		printf '%s\n' ''; \
		printf '%s\n' '[properties]'; \
		printf '%s\n' 'needs_exe_wrapper = true'; \
		printf '%s\n' ''; \
		printf '%s\n' '[built-in options]'; \
		printf "%s\n" "c_args = [$(MLIBC_HOSTED_EXEC_MESON_ARGS)]"; \
		printf "%s\n" "cpp_args = [$(MLIBC_HOSTED_EXEC_MESON_ARGS)]"; \
		printf "%s\n" "c_link_args = ['-fuse-ld=lld']"; \
		printf "%s\n" "cpp_link_args = ['-fuse-ld=lld']"; \
	} > "$(MLIBC_CROSS_FILE)"; \
	echo "wrote OrlixMLibC mlibc cross file: $(MLIBC_CROSS_FILE)"
