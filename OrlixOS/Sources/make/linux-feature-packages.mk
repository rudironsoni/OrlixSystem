$(ORLIXOS_ATTR_PROOF): $(ORLIXOS_ATTR_SOURCE_STAMP) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB)
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	rtlib="$(ORLIXOS_MLIBC_RTLIB)"; \
	[ -s "$$sysroot/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $$sysroot/usr/lib/libc.a" >&2; exit 1; }; \
	[ -d "$$headers" ] || { echo "missing Orlix Linux UAPI headers: $$headers" >&2; exit 1; }; \
	[ -s "$$rtlib" ] || { echo "missing Orlix compiler runtime archive: $$rtlib" >&2; exit 1; }; \
	rm -rf "$(ORLIXOS_ATTR_BUILD_DIR)" "$(ORLIXOS_GETFATTR_BINARY)" "$(ORLIXOS_SETFATTR_BINARY)" "$(ORLIXOS_LIBATTR_A)" "$(ORLIXOS_ATTR_PROOF)"; \
	mkdir -p "$(ORLIXOS_ATTR_BUILD_DIR)/.orlix-toolchain" "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin" "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/lib"; \
	{ \
		printf '%s\n' '#!/bin/bash'; \
		printf '%s\n' 'set -euo pipefail'; \
		printf '%s\n' 'cc="$(ORLIXOS_CC)"'; \
		printf '%s\n' 'sysroot="$(ORLIXOS_MLIBC_SYSROOT)"'; \
		printf '%s\n' 'headers="$(ORLIXOS_MLIBC_HEADERS)"'; \
		printf '%s\n' 'rtlib="$(ORLIXOS_MLIBC_RTLIB)"'; \
		printf '%s\n' 'link=1'; \
		printf '%s\n' 'output='; \
		printf '%s\n' 'next_output=0'; \
		printf '%s\n' 'for arg in "$$@"; do'; \
		printf '%s\n' '  if [ "$$next_output" -eq 1 ]; then output="$$arg"; next_output=0; continue; fi'; \
		printf '%s\n' '  case "$$arg" in -c|-E|-S) link=0 ;; -o) next_output=1 ;; esac'; \
		printf '%s\n' 'done'; \
		printf '%s\n' 'common=(--target=aarch64-linux-gnu "--sysroot=$$sysroot" -isystem "$$headers" -D_GNU_SOURCE -fhosted -fno-builtin -femulated-tls -ffixed-x18 -fPIC)'; \
		printf '%s\n' 'if [ "$$link" -eq 1 ] && [ "$${output##*.}" != la ]; then'; \
		printf '%s\n' '  exec "$$cc" "$${common[@]}" "$$@" -static -no-pie -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--image-base=$(ORLIXOS_HOSTED_USER_BASE_ADDRESS) "$$sysroot/usr/lib/crt1.o" "$$sysroot/usr/lib/crti.o" -Wl,--start-group "$$sysroot/usr/lib/libc.a" "$$sysroot/usr/lib/libm.a" "$$sysroot/usr/lib/libpthread.a" "$$sysroot/usr/lib/libssp_nonshared.a" "$$sysroot/usr/lib/libssp.a" "$$rtlib" -Wl,--end-group "$$sysroot/usr/lib/crtn.o"'; \
		printf '%s\n' 'fi'; \
		printf '%s\n' 'exec "$$cc" "$${common[@]}" "$$@"'; \
	} > "$(ORLIXOS_ATTR_BUILD_DIR)/.orlix-toolchain/aarch64-linux-gnu-gcc"; \
	chmod +x "$(ORLIXOS_ATTR_BUILD_DIR)/.orlix-toolchain/aarch64-linux-gnu-gcc"; \
	cd "$(ORLIXOS_ATTR_BUILD_DIR)"; \
	export CC="$(ORLIXOS_ATTR_BUILD_DIR)/.orlix-toolchain/aarch64-linux-gnu-gcc"; \
	export CFLAGS="$(ORLIXOS_PACKAGE_CFLAGS)"; \
	export LDFLAGS=""; \
	export LIBS=""; \
	export AR="$(ORLIXOS_AR)"; \
	export RANLIB="$(ORLIXOS_RANLIB)"; \
	"$(ORLIXOS_ATTR_SRC_DIR)/configure" --host=aarch64-linux-gnu --build=aarch64-apple-darwin --prefix=/usr --disable-nls --disable-shared --enable-static; \
	$(MAKE) -j1 all; \
	$(MAKE) -j1 install DESTDIR="$(ORLIXOS_PACKAGE_INSTALL_DIR)"; \
	for program in getfattr setfattr; do \
		"$(ORLIXOS_STRIP)" "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program"; \
		file "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program" >&2; exit 1; }; \
	done; \
	[ -s "$(ORLIXOS_LIBATTR_A)" ] || { echo "missing libattr archive: $(ORLIXOS_LIBATTR_A)" >&2; exit 1; }; \
	printf 'profile=%s\ndistribution=%s\nchannel=%s\npackage=attr\nversion=%s\nsha256=%s\nprograms=getfattr,setfattr\n' "$(PROFILE)" "$(ORLIXOS_DISTRIBUTION_ID)" "$(ORLIXOS_DISTRIBUTION_CHANNEL)" "$(ATTR_VERSION)" "$(ATTR_SHA256)" > "$(ORLIXOS_ATTR_PROOF)"; \
	rm -rf "$(ORLIXOS_ATTR_BUILD_DIR)"; \
	echo "built Orlix Linux attr package inputs: $(ORLIXOS_GETFATTR_BINARY) $(ORLIXOS_SETFATTR_BINARY)"

$(ORLIXOS_GETFATTR_BINARY) $(ORLIXOS_SETFATTR_BINARY) $(ORLIXOS_LIBATTR_A): $(ORLIXOS_ATTR_PROOF)
	@set -euo pipefail; \
	[ -x "$(ORLIXOS_GETFATTR_BINARY)" ] || { echo "missing attr getfattr package input: $(ORLIXOS_GETFATTR_BINARY)" >&2; exit 1; }; \
	[ -x "$(ORLIXOS_SETFATTR_BINARY)" ] || { echo "missing attr setfattr package input: $(ORLIXOS_SETFATTR_BINARY)" >&2; exit 1; }; \
	[ -s "$(ORLIXOS_LIBATTR_A)" ] || { echo "missing libattr archive: $(ORLIXOS_LIBATTR_A)" >&2; exit 1; }

$(ORLIXOS_ACL_PROOF): $(ORLIXOS_ACL_SOURCE_STAMP) $(ORLIXOS_ATTR_PROOF) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB)
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	rtlib="$(ORLIXOS_MLIBC_RTLIB)"; \
	[ -s "$$sysroot/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $$sysroot/usr/lib/libc.a" >&2; exit 1; }; \
	[ -d "$$headers" ] || { echo "missing Orlix Linux UAPI headers: $$headers" >&2; exit 1; }; \
	[ -s "$$rtlib" ] || { echo "missing Orlix compiler runtime archive: $$rtlib" >&2; exit 1; }; \
	rm -rf "$(ORLIXOS_ACL_BUILD_DIR)" "$(ORLIXOS_GETFACL_BINARY)" "$(ORLIXOS_SETFACL_BINARY)" "$(ORLIXOS_LIBACL_A)" "$(ORLIXOS_ACL_PROOF)"; \
	mkdir -p "$(ORLIXOS_ACL_BUILD_DIR)/.orlix-toolchain" "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin" "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/lib"; \
	{ \
		printf '%s\n' '#!/bin/bash'; \
		printf '%s\n' 'set -euo pipefail'; \
		printf '%s\n' 'cc="$(ORLIXOS_CC)"'; \
		printf '%s\n' 'sysroot="$(ORLIXOS_MLIBC_SYSROOT)"'; \
		printf '%s\n' 'headers="$(ORLIXOS_MLIBC_HEADERS)"'; \
		printf '%s\n' 'rtlib="$(ORLIXOS_MLIBC_RTLIB)"'; \
		printf '%s\n' 'link=1'; \
		printf '%s\n' 'output='; \
		printf '%s\n' 'next_output=0'; \
		printf '%s\n' 'for arg in "$$@"; do'; \
		printf '%s\n' '  if [ "$$next_output" -eq 1 ]; then output="$$arg"; next_output=0; continue; fi'; \
		printf '%s\n' '  case "$$arg" in -c|-E|-S) link=0 ;; -o) next_output=1 ;; esac'; \
		printf '%s\n' 'done'; \
		printf '%s\n' 'common=(--target=aarch64-linux-gnu "--sysroot=$$sysroot" -isystem "$$headers" -D_GNU_SOURCE -fhosted -fno-builtin -femulated-tls -ffixed-x18 -fPIC)'; \
		printf '%s\n' 'if [ "$$link" -eq 1 ] && [ "$${output##*.}" != la ]; then'; \
		printf '%s\n' '  exec "$$cc" "$${common[@]}" "$$@" -static -no-pie -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--image-base=$(ORLIXOS_HOSTED_USER_BASE_ADDRESS) "$$sysroot/usr/lib/crt1.o" "$$sysroot/usr/lib/crti.o" -Wl,--start-group "$(ORLIXOS_LIBATTR_A)" "$$sysroot/usr/lib/libc.a" "$$sysroot/usr/lib/libm.a" "$$sysroot/usr/lib/libpthread.a" "$$sysroot/usr/lib/libssp_nonshared.a" "$$sysroot/usr/lib/libssp.a" "$$rtlib" -Wl,--end-group "$$sysroot/usr/lib/crtn.o"'; \
		printf '%s\n' 'fi'; \
		printf '%s\n' 'exec "$$cc" "$${common[@]}" "$$@"'; \
	} > "$(ORLIXOS_ACL_BUILD_DIR)/.orlix-toolchain/aarch64-linux-gnu-gcc"; \
	chmod +x "$(ORLIXOS_ACL_BUILD_DIR)/.orlix-toolchain/aarch64-linux-gnu-gcc"; \
	cd "$(ORLIXOS_ACL_BUILD_DIR)"; \
	export CC="$(ORLIXOS_ACL_BUILD_DIR)/.orlix-toolchain/aarch64-linux-gnu-gcc"; \
	export CPPFLAGS="-I$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/include"; \
	export CFLAGS="$(ORLIXOS_PACKAGE_CFLAGS)"; \
	export LDFLAGS="-L$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/lib"; \
	export LIBS=""; \
	export AR="$(ORLIXOS_AR)"; \
	export RANLIB="$(ORLIXOS_RANLIB)"; \
	"$(ORLIXOS_ACL_SRC_DIR)/configure" --host=aarch64-linux-gnu --build=aarch64-apple-darwin --prefix=/usr --disable-nls --disable-shared --enable-static; \
	$(MAKE) -j1 all; \
	$(MAKE) -j1 install DESTDIR="$(ORLIXOS_PACKAGE_INSTALL_DIR)"; \
	for program in getfacl setfacl; do \
		"$(ORLIXOS_STRIP)" "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program"; \
		file "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program" >&2; exit 1; }; \
	done; \
	[ -s "$(ORLIXOS_LIBACL_A)" ] || { echo "missing libacl archive: $(ORLIXOS_LIBACL_A)" >&2; exit 1; }; \
	printf 'profile=%s\ndistribution=%s\nchannel=%s\npackage=acl\nversion=%s\nsha256=%s\nprograms=getfacl,setfacl\n' "$(PROFILE)" "$(ORLIXOS_DISTRIBUTION_ID)" "$(ORLIXOS_DISTRIBUTION_CHANNEL)" "$(ACL_VERSION)" "$(ACL_SHA256)" > "$(ORLIXOS_ACL_PROOF)"; \
	rm -rf "$(ORLIXOS_ACL_BUILD_DIR)"; \
	echo "built Orlix Linux acl package inputs: $(ORLIXOS_GETFACL_BINARY) $(ORLIXOS_SETFACL_BINARY)"

$(ORLIXOS_GETFACL_BINARY) $(ORLIXOS_SETFACL_BINARY) $(ORLIXOS_LIBACL_A): $(ORLIXOS_ACL_PROOF)
	@set -euo pipefail; \
	[ -x "$(ORLIXOS_GETFACL_BINARY)" ] || { echo "missing acl getfacl package input: $(ORLIXOS_GETFACL_BINARY)" >&2; exit 1; }; \
	[ -x "$(ORLIXOS_SETFACL_BINARY)" ] || { echo "missing acl setfacl package input: $(ORLIXOS_SETFACL_BINARY)" >&2; exit 1; }; \
	[ -s "$(ORLIXOS_LIBACL_A)" ] || { echo "missing libacl archive: $(ORLIXOS_LIBACL_A)" >&2; exit 1; }

$(ORLIXOS_LIBCAP_PROOF): $(ORLIXOS_LIBCAP_SOURCE_STAMP) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB)
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	rtlib="$(ORLIXOS_MLIBC_RTLIB)"; \
	[ -s "$$sysroot/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $$sysroot/usr/lib/libc.a" >&2; exit 1; }; \
	[ -d "$$headers" ] || { echo "missing Orlix Linux UAPI headers: $$headers" >&2; exit 1; }; \
	[ -s "$$rtlib" ] || { echo "missing Orlix compiler runtime archive: $$rtlib" >&2; exit 1; }; \
	rm -rf "$(ORLIXOS_LIBCAP_BUILD_DIR)" "$(ORLIXOS_LIBCAP_A)" "$(ORLIXOS_SETCAP_BINARY)" "$(ORLIXOS_GETCAP_BINARY)" "$(ORLIXOS_LIBCAP_PROOF)"; \
	cp -R "$(ORLIXOS_LIBCAP_SRC_DIR)" "$(ORLIXOS_LIBCAP_BUILD_DIR)"; \
	mkdir -p "$(ORLIXOS_LIBCAP_BUILD_DIR)/.orlix-host-tools" "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin" "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/include/sys" "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/include/linux" "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/lib"; \
	{ \
		printf '%s\n' '#!/bin/sh'; \
		printf '%s\n' 'pattern=$$1'; \
		printf '%s\n' 'shift'; \
		printf '%s\n' 'case "$$pattern" in'; \
		printf '%s\n' '  *CAP_*) pattern="^#define[[:space:]]+CAP_([^[:space:]]+)[[:space:]]+[0-9]+[[:space:]]*$$" ;;'; \
		printf '%s\n' 'esac'; \
		printf '%s\n' 'exec grep -E "$$pattern" "$$@"'; \
	} > "$(ORLIXOS_LIBCAP_BUILD_DIR)/.orlix-host-tools/egrep"; \
	chmod +x "$(ORLIXOS_LIBCAP_BUILD_DIR)/.orlix-host-tools/egrep"; \
	$(MAKE) -C "$(ORLIXOS_LIBCAP_BUILD_DIR)/libcap" -j1 CC="$(ORLIXOS_CC) --target=aarch64-linux-gnu --sysroot=$$sysroot -isystem $$headers -D_GNU_SOURCE -fhosted -fno-builtin -femulated-tls -ffixed-x18 -fPIC" BUILD_CC=cc BUILD_SED=/opt/homebrew/bin/gsed BUILD_EGREP="$(ORLIXOS_LIBCAP_BUILD_DIR)/.orlix-host-tools/egrep" AR="$(ORLIXOS_AR)" RANLIB="$(ORLIXOS_RANLIB)" CFLAGS="$(ORLIXOS_PACKAGE_CFLAGS) -Wno-error" USE_GPERF=no libcap.a; \
	grep -F -q 'cap_net_bind_service' "$(ORLIXOS_LIBCAP_BUILD_DIR)/libcap/cap_names.h" || { echo "libcap capability-name table missing cap_net_bind_service" >&2; exit 1; }; \
	install -m 0644 "$(ORLIXOS_LIBCAP_BUILD_DIR)/libcap/include/sys/capability.h" "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/include/sys/capability.h"; \
	install -m 0644 "$(ORLIXOS_LIBCAP_BUILD_DIR)/libcap/include/uapi/linux/capability.h" "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/include/linux/capability.h"; \
	install -m 0644 "$(ORLIXOS_LIBCAP_BUILD_DIR)/libcap/libcap.a" "$(ORLIXOS_LIBCAP_A)"; \
	"$(ORLIXOS_CC)" --target=aarch64-linux-gnu --sysroot="$$sysroot" -isystem "$$headers" -I"$(ORLIXOS_LIBCAP_BUILD_DIR)/libcap/include" -D_GNU_SOURCE $(ORLIXOS_PACKAGE_CFLAGS) -fhosted -fno-builtin -femulated-tls -ffixed-x18 -fno-pie -static -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--image-base=$(ORLIXOS_HOSTED_USER_BASE_ADDRESS) "$$sysroot/usr/lib/crt1.o" "$$sysroot/usr/lib/crti.o" "$(ORLIXOS_LIBCAP_BUILD_DIR)/progs/setcap.c" "$(ORLIXOS_LIBCAP_BUILD_DIR)/progs/capshdoc.c" -Wl,--start-group "$(ORLIXOS_LIBCAP_A)" "$$sysroot/usr/lib/libc.a" "$$sysroot/usr/lib/libm.a" "$$sysroot/usr/lib/libpthread.a" "$$sysroot/usr/lib/libssp_nonshared.a" "$$sysroot/usr/lib/libssp.a" "$$rtlib" -Wl,--end-group "$$sysroot/usr/lib/crtn.o" -o "$(ORLIXOS_SETCAP_BINARY)"; \
	"$(ORLIXOS_CC)" --target=aarch64-linux-gnu --sysroot="$$sysroot" -isystem "$$headers" -I"$(ORLIXOS_LIBCAP_BUILD_DIR)/libcap/include" -D_GNU_SOURCE $(ORLIXOS_PACKAGE_CFLAGS) -fhosted -fno-builtin -femulated-tls -ffixed-x18 -fno-pie -static -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--image-base=$(ORLIXOS_HOSTED_USER_BASE_ADDRESS) "$$sysroot/usr/lib/crt1.o" "$$sysroot/usr/lib/crti.o" "$(ORLIXOS_LIBCAP_BUILD_DIR)/progs/getcap.c" -Wl,--start-group "$(ORLIXOS_LIBCAP_A)" "$$sysroot/usr/lib/libc.a" "$$sysroot/usr/lib/libm.a" "$$sysroot/usr/lib/libpthread.a" "$$sysroot/usr/lib/libssp_nonshared.a" "$$sysroot/usr/lib/libssp.a" "$$rtlib" -Wl,--end-group "$$sysroot/usr/lib/crtn.o" -o "$(ORLIXOS_GETCAP_BINARY)"; \
	"$(ORLIXOS_STRIP)" "$(ORLIXOS_SETCAP_BINARY)" "$(ORLIXOS_GETCAP_BINARY)"; \
	file "$(ORLIXOS_SETCAP_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_SETCAP_BINARY)" >&2; exit 1; }; \
	file "$(ORLIXOS_GETCAP_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_GETCAP_BINARY)" >&2; exit 1; }; \
	[ -s "$(ORLIXOS_LIBCAP_A)" ] || { echo "missing libcap archive: $(ORLIXOS_LIBCAP_A)" >&2; exit 1; }; \
	printf 'profile=%s\ndistribution=%s\nchannel=%s\npackage=libcap\nversion=%s\nsha256=%s\nlibraries=libcap.a\nprograms=setcap,getcap\n' "$(PROFILE)" "$(ORLIXOS_DISTRIBUTION_ID)" "$(ORLIXOS_DISTRIBUTION_CHANNEL)" "$(LIBCAP_VERSION)" "$(LIBCAP_SHA256)" > "$(ORLIXOS_LIBCAP_PROOF)"; \
	rm -rf "$(ORLIXOS_LIBCAP_BUILD_DIR)"; \
	echo "built Orlix Linux libcap package inputs: $(ORLIXOS_LIBCAP_A) $(ORLIXOS_SETCAP_BINARY) $(ORLIXOS_GETCAP_BINARY)"

$(ORLIXOS_LIBCAP_A) $(ORLIXOS_SETCAP_BINARY) $(ORLIXOS_GETCAP_BINARY): $(ORLIXOS_LIBCAP_PROOF)
	@set -euo pipefail; \
	[ -s "$(ORLIXOS_LIBCAP_A)" ] || { echo "missing libcap archive: $(ORLIXOS_LIBCAP_A)" >&2; exit 1; }; \
	[ -x "$(ORLIXOS_SETCAP_BINARY)" ] || { echo "missing libcap setcap package input: $(ORLIXOS_SETCAP_BINARY)" >&2; exit 1; }; \
	[ -x "$(ORLIXOS_GETCAP_BINARY)" ] || { echo "missing libcap getcap package input: $(ORLIXOS_GETCAP_BINARY)" >&2; exit 1; }

$(ORLIXOS_E2FSPROGS_PROOF): $(ORLIXOS_E2FSPROGS_SOURCE_STAMP) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB)
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	rtlib="$(ORLIXOS_MLIBC_RTLIB)"; \
	[ -s "$$sysroot/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $$sysroot/usr/lib/libc.a" >&2; exit 1; }; \
	[ -d "$$headers" ] || { echo "missing Orlix Linux UAPI headers: $$headers" >&2; exit 1; }; \
	[ -s "$$rtlib" ] || { echo "missing Orlix compiler runtime archive: $$rtlib" >&2; exit 1; }; \
	rm -rf "$(ORLIXOS_E2FSPROGS_BUILD_DIR)" "$(ORLIXOS_MKE2FS_BINARY)" "$(ORLIXOS_MKFS_EXT2_BINARY)" "$(ORLIXOS_MKFS_EXT4_BINARY)" "$(ORLIXOS_E2FSPROGS_PROOF)"; \
	mkdir -p "$(ORLIXOS_E2FSPROGS_BUILD_DIR)/.orlix-toolchain" "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin"; \
	{ \
		printf '%s\n' '#!/bin/bash'; \
		printf '%s\n' 'set -euo pipefail'; \
		printf '%s\n' 'cc="$(ORLIXOS_CC)"'; \
		printf '%s\n' 'sysroot="$(ORLIXOS_MLIBC_SYSROOT)"'; \
		printf '%s\n' 'headers="$(ORLIXOS_MLIBC_HEADERS)"'; \
		printf '%s\n' 'rtlib="$(ORLIXOS_MLIBC_RTLIB)"'; \
		printf '%s\n' 'link=1'; \
		printf '%s\n' 'for arg in "$$@"; do case "$$arg" in -c|-E|-S) link=0 ;; esac; done'; \
		printf '%s\n' 'common=(--target=aarch64-linux-gnu "--sysroot=$$sysroot" -isystem "$$headers" -D_GNU_SOURCE -fhosted -fno-builtin -femulated-tls -ffixed-x18 -fPIC)'; \
		printf '%s\n' 'if [ "$$link" -eq 1 ]; then'; \
		printf '%s\n' '  exec "$$cc" "$${common[@]}" "$$@" -static -no-pie -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--image-base=$(ORLIXOS_HOSTED_USER_BASE_ADDRESS) "$$sysroot/usr/lib/crt1.o" "$$sysroot/usr/lib/crti.o" -Wl,--start-group "$$sysroot/usr/lib/libc.a" "$$sysroot/usr/lib/libm.a" "$$sysroot/usr/lib/libpthread.a" "$$sysroot/usr/lib/libssp_nonshared.a" "$$sysroot/usr/lib/libssp.a" "$$rtlib" -Wl,--end-group "$$sysroot/usr/lib/crtn.o"'; \
		printf '%s\n' 'fi'; \
		printf '%s\n' 'exec "$$cc" "$${common[@]}" "$$@"'; \
	} > "$(ORLIXOS_E2FSPROGS_BUILD_DIR)/.orlix-toolchain/aarch64-linux-gnu-gcc"; \
	chmod +x "$(ORLIXOS_E2FSPROGS_BUILD_DIR)/.orlix-toolchain/aarch64-linux-gnu-gcc"; \
	cd "$(ORLIXOS_E2FSPROGS_BUILD_DIR)"; \
	export CC="$(ORLIXOS_E2FSPROGS_BUILD_DIR)/.orlix-toolchain/aarch64-linux-gnu-gcc"; \
	export BUILD_CC="cc"; \
	export CFLAGS="$(ORLIXOS_PACKAGE_CFLAGS)"; \
	export LDFLAGS=""; \
	export LIBS=""; \
	export AR="$(ORLIXOS_AR)"; \
	export RANLIB="$(ORLIXOS_RANLIB)"; \
	"$(ORLIXOS_E2FSPROGS_SRC_DIR)/configure" --host=aarch64-linux-gnu --build=aarch64-apple-darwin --prefix=/usr --enable-elf-shlibs=no --disable-uuidd --disable-fuse2fs --disable-backtrace --disable-debugfs --disable-imager --disable-resizer --disable-defrag --disable-nls; \
	$(MAKE) -j1 libs; \
	$(MAKE) -C misc -j1 mke2fs; \
	cp "$(ORLIXOS_E2FSPROGS_BUILD_DIR)/misc/mke2fs" "$(ORLIXOS_MKE2FS_BINARY)"; \
	cp "$(ORLIXOS_E2FSPROGS_BUILD_DIR)/misc/mke2fs" "$(ORLIXOS_MKFS_EXT2_BINARY)"; \
	cp "$(ORLIXOS_E2FSPROGS_BUILD_DIR)/misc/mke2fs" "$(ORLIXOS_MKFS_EXT4_BINARY)"; \
	"$(ORLIXOS_STRIP)" "$(ORLIXOS_MKE2FS_BINARY)"; \
	"$(ORLIXOS_STRIP)" "$(ORLIXOS_MKFS_EXT2_BINARY)"; \
	"$(ORLIXOS_STRIP)" "$(ORLIXOS_MKFS_EXT4_BINARY)"; \
	file "$(ORLIXOS_MKE2FS_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_MKE2FS_BINARY)" >&2; exit 1; }; \
	file "$(ORLIXOS_MKFS_EXT2_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_MKFS_EXT2_BINARY)" >&2; exit 1; }; \
	file "$(ORLIXOS_MKFS_EXT4_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_MKFS_EXT4_BINARY)" >&2; exit 1; }; \
	file "$(ORLIXOS_MKE2FS_BINARY)" | grep -F -q 'statically linked' || { file "$(ORLIXOS_MKE2FS_BINARY)" >&2; exit 1; }; \
	file "$(ORLIXOS_MKFS_EXT2_BINARY)" | grep -F -q 'statically linked' || { file "$(ORLIXOS_MKFS_EXT2_BINARY)" >&2; exit 1; }; \
	file "$(ORLIXOS_MKFS_EXT4_BINARY)" | grep -F -q 'statically linked' || { file "$(ORLIXOS_MKFS_EXT4_BINARY)" >&2; exit 1; }; \
	printf 'profile=%s\ndistribution=%s\nchannel=%s\npackage=e2fsprogs\nversion=%s\nsha256=%s\nprograms=mke2fs,mkfs.ext2,mkfs.ext4\n' "$(PROFILE)" "$(ORLIXOS_DISTRIBUTION_ID)" "$(ORLIXOS_DISTRIBUTION_CHANNEL)" "$(E2FSPROGS_VERSION)" "$(E2FSPROGS_SHA256)" > "$(ORLIXOS_E2FSPROGS_PROOF)"; \
	rm -rf "$(ORLIXOS_E2FSPROGS_BUILD_DIR)"; \
	echo "built Orlix Linux e2fsprogs package inputs: $(ORLIXOS_MKE2FS_BINARY) $(ORLIXOS_MKFS_EXT2_BINARY) $(ORLIXOS_MKFS_EXT4_BINARY)"

$(ORLIXOS_MKE2FS_BINARY) $(ORLIXOS_MKFS_EXT2_BINARY) $(ORLIXOS_MKFS_EXT4_BINARY): $(ORLIXOS_E2FSPROGS_PROOF)
	@set -euo pipefail; \
	[ -x "$(ORLIXOS_MKE2FS_BINARY)" ] || { echo "missing e2fsprogs mke2fs package input: $(ORLIXOS_MKE2FS_BINARY)" >&2; exit 1; }; \
	[ -x "$(ORLIXOS_MKFS_EXT2_BINARY)" ] || { echo "missing e2fsprogs mkfs.ext2 package input: $(ORLIXOS_MKFS_EXT2_BINARY)" >&2; exit 1; }; \
	[ -x "$(ORLIXOS_MKFS_EXT4_BINARY)" ] || { echo "missing e2fsprogs mkfs.ext4 package input: $(ORLIXOS_MKFS_EXT4_BINARY)" >&2; exit 1; }

$(ORLIXOS_PCRE2_PROOF): $(ORLIXOS_PCRE2_SOURCE_STAMP) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB)
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	rtlib="$(ORLIXOS_MLIBC_RTLIB)"; \
	[ -s "$$sysroot/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $$sysroot/usr/lib/libc.a" >&2; exit 1; }; \
	[ -d "$$headers" ] || { echo "missing Orlix Linux UAPI headers: $$headers" >&2; exit 1; }; \
	[ -s "$$rtlib" ] || { echo "missing Orlix compiler runtime archive: $$rtlib" >&2; exit 1; }; \
	rm -rf "$(ORLIXOS_PCRE2_BUILD_DIR)" "$(ORLIXOS_LIBPCRE2_8_A)" "$(ORLIXOS_PCRE2_PROOF)"; \
	mkdir -p "$(ORLIXOS_PCRE2_BUILD_DIR)" "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/lib"; \
	cd "$(ORLIXOS_PCRE2_BUILD_DIR)"; \
	export CC="$(ORLIXOS_CC) --target=aarch64-linux-gnu --sysroot=$$sysroot -isystem $$headers -D_GNU_SOURCE -fhosted -fno-builtin -femulated-tls -ffixed-x18 -fPIC"; \
	export CFLAGS="$(ORLIXOS_PACKAGE_CFLAGS)"; \
	export LDFLAGS="--target=aarch64-linux-gnu --sysroot=$$sysroot -static -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--image-base=$(ORLIXOS_HOSTED_USER_BASE_ADDRESS) $$sysroot/usr/lib/crt1.o $$sysroot/usr/lib/crti.o -Wl,--start-group"; \
	export LIBS="$$sysroot/usr/lib/libc.a $$sysroot/usr/lib/libm.a $$sysroot/usr/lib/libpthread.a $$sysroot/usr/lib/libssp_nonshared.a $$sysroot/usr/lib/libssp.a $$rtlib -Wl,--end-group $$sysroot/usr/lib/crtn.o"; \
	export AR="$(ORLIXOS_AR)"; \
	export RANLIB="$(ORLIXOS_RANLIB)"; \
	"$(ORLIXOS_PCRE2_SRC_DIR)/configure" --host=aarch64-linux-gnu --build=aarch64-apple-darwin --prefix=/usr --disable-shared --enable-static --disable-cpp --disable-pcre2-16 --disable-pcre2-32 --disable-jit; \
	$(MAKE) -j1 install DESTDIR="$(ORLIXOS_PACKAGE_INSTALL_DIR)"; \
	"$(ORLIXOS_AR)" d "$(ORLIXOS_LIBPCRE2_8_A)" libc.a libm.a libpthread.a libssp_nonshared.a libssp.a liborlix_compiler_rt.a >/dev/null 2>&1 || true; \
	[ -s "$(ORLIXOS_LIBPCRE2_8_A)" ] || { echo "missing pcre2 archive: $(ORLIXOS_LIBPCRE2_8_A)" >&2; exit 1; }; \
	printf 'profile=%s\ndistribution=%s\nchannel=%s\npackage=pcre2\nversion=%s\nsha256=%s\nlibraries=libpcre2-8.a\n' "$(PROFILE)" "$(ORLIXOS_DISTRIBUTION_ID)" "$(ORLIXOS_DISTRIBUTION_CHANNEL)" "$(PCRE2_VERSION)" "$(PCRE2_SHA256)" > "$(ORLIXOS_PCRE2_PROOF)"; \
	rm -rf "$(ORLIXOS_PCRE2_BUILD_DIR)"; \
	echo "built Orlix Linux pcre2 package input: $(ORLIXOS_LIBPCRE2_8_A)"

$(ORLIXOS_LIBPCRE2_8_A): $(ORLIXOS_PCRE2_PROOF)
	@set -euo pipefail; \
	[ -s "$(ORLIXOS_LIBPCRE2_8_A)" ] || { echo "missing pcre2 archive: $(ORLIXOS_LIBPCRE2_8_A)" >&2; exit 1; }

$(ORLIXOS_FTS_STANDALONE_PROOF): $(ORLIXOS_FTS_STANDALONE_SOURCE_STAMP) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB)
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	rtlib="$(ORLIXOS_MLIBC_RTLIB)"; \
	[ -s "$$sysroot/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $$sysroot/usr/lib/libc.a" >&2; exit 1; }; \
	[ -d "$$headers" ] || { echo "missing Orlix Linux UAPI headers: $$headers" >&2; exit 1; }; \
	[ -s "$$rtlib" ] || { echo "missing Orlix compiler runtime archive: $$rtlib" >&2; exit 1; }; \
	rm -rf "$(ORLIXOS_FTS_STANDALONE_BUILD_DIR)" "$(ORLIXOS_LIBFTS_A)" "$(ORLIXOS_FTS_STANDALONE_PROOF)"; \
	cp -R "$(ORLIXOS_FTS_STANDALONE_SRC_DIR)" "$(ORLIXOS_FTS_STANDALONE_BUILD_DIR)"; \
	mkdir -p "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/include" "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/lib/pkgconfig"; \
	printf '%s\n' '#define HAVE_DIRFD 1' '#define HAVE_DECL_MAX 1' '#define HAVE_DECL_UINTMAX_MAX 1' > "$(ORLIXOS_FTS_STANDALONE_BUILD_DIR)/config.h"; \
	"$(ORLIXOS_CC)" --target=aarch64-linux-gnu --sysroot="$$sysroot" -isystem "$$headers" -I"$(ORLIXOS_FTS_STANDALONE_BUILD_DIR)" -D_GNU_SOURCE $(ORLIXOS_PACKAGE_CFLAGS) -fhosted -fno-builtin -femulated-tls -ffixed-x18 -fPIC -c "$(ORLIXOS_FTS_STANDALONE_BUILD_DIR)/fts.c" -o "$(ORLIXOS_FTS_STANDALONE_BUILD_DIR)/fts.o"; \
	"$(ORLIXOS_AR)" rcs "$(ORLIXOS_LIBFTS_A)" "$(ORLIXOS_FTS_STANDALONE_BUILD_DIR)/fts.o"; \
	"$(ORLIXOS_RANLIB)" "$(ORLIXOS_LIBFTS_A)"; \
	install -m 644 "$(ORLIXOS_FTS_STANDALONE_BUILD_DIR)/fts.h" "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/include/fts.h"; \
	printf 'prefix=/usr\nexec_prefix=$${prefix}\nlibdir=$${exec_prefix}/lib\nincludedir=$${prefix}/include\n\nName: musl-fts\nDescription: standalone fts implementation\nVersion: %s\nLibs: -L$${libdir} -lfts\nCflags: -I$${includedir}\n' "$(FTS_STANDALONE_VERSION)" > "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/lib/pkgconfig/musl-fts.pc"; \
	[ -s "$(ORLIXOS_LIBFTS_A)" ] || { echo "missing musl-fts archive: $(ORLIXOS_LIBFTS_A)" >&2; exit 1; }; \
	printf 'profile=%s\ndistribution=%s\nchannel=%s\npackage=musl-fts\nversion=%s\nsha256=%s\nlibraries=libfts.a\n' "$(PROFILE)" "$(ORLIXOS_DISTRIBUTION_ID)" "$(ORLIXOS_DISTRIBUTION_CHANNEL)" "$(FTS_STANDALONE_VERSION)" "$(FTS_STANDALONE_SHA256)" > "$(ORLIXOS_FTS_STANDALONE_PROOF)"; \
	rm -rf "$(ORLIXOS_FTS_STANDALONE_BUILD_DIR)"; \
	echo "built Orlix Linux musl-fts package input: $(ORLIXOS_LIBFTS_A)"

$(ORLIXOS_LIBFTS_A): $(ORLIXOS_FTS_STANDALONE_PROOF)
	@set -euo pipefail; \
	[ -s "$(ORLIXOS_LIBFTS_A)" ] || { echo "missing musl-fts archive: $(ORLIXOS_LIBFTS_A)" >&2; exit 1; }

$(ORLIXOS_LIBSEPOL_PROOF): $(ORLIXOS_LIBSEPOL_SOURCE_STAMP) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB)
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	[ -s "$$sysroot/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $$sysroot/usr/lib/libc.a" >&2; exit 1; }; \
	[ -d "$$headers" ] || { echo "missing Orlix Linux UAPI headers: $$headers" >&2; exit 1; }; \
	rm -rf "$(ORLIXOS_LIBSEPOL_BUILD_DIR)" "$(ORLIXOS_LIBSEPOL_A)" "$(ORLIXOS_LIBSEPOL_PROOF)"; \
	cp -R "$(ORLIXOS_LIBSEPOL_SRC_DIR)" "$(ORLIXOS_LIBSEPOL_BUILD_DIR)"; \
	$(MAKE) -C "$(ORLIXOS_LIBSEPOL_BUILD_DIR)/include" install DESTDIR="$(ORLIXOS_PACKAGE_INSTALL_DIR)" PREFIX=/usr; \
	$(MAKE) -C "$(ORLIXOS_LIBSEPOL_BUILD_DIR)/src" -j1 DISABLE_SHARED=y DISABLE_CIL=y CC="$(ORLIXOS_CC) --target=aarch64-linux-gnu --sysroot=$$sysroot -isystem $$headers -fhosted -fno-builtin -femulated-tls -ffixed-x18 -fPIC" AR="$(ORLIXOS_AR)" RANLIB="$(ORLIXOS_RANLIB)" CFLAGS="$(ORLIXOS_PACKAGE_CFLAGS) -DHAVE_REALLOCARRAY -Wno-error"; \
	$(MAKE) -C "$(ORLIXOS_LIBSEPOL_BUILD_DIR)/src" install DISABLE_SHARED=y DISABLE_CIL=y CC="$(ORLIXOS_CC) --target=aarch64-linux-gnu --sysroot=$$sysroot -isystem $$headers -fhosted -fno-builtin -femulated-tls -ffixed-x18 -fPIC" AR="$(ORLIXOS_AR)" RANLIB="$(ORLIXOS_RANLIB)" CFLAGS="$(ORLIXOS_PACKAGE_CFLAGS) -DHAVE_REALLOCARRAY -Wno-error" DESTDIR="$(ORLIXOS_PACKAGE_INSTALL_DIR)" PREFIX=/usr LIBDIR=/usr/lib; \
	[ -s "$(ORLIXOS_LIBSEPOL_A)" ] || { echo "missing libsepol archive: $(ORLIXOS_LIBSEPOL_A)" >&2; exit 1; }; \
	printf 'profile=%s\ndistribution=%s\nchannel=%s\npackage=libsepol\nversion=%s\nsha256=%s\nlibraries=libsepol.a\n' "$(PROFILE)" "$(ORLIXOS_DISTRIBUTION_ID)" "$(ORLIXOS_DISTRIBUTION_CHANNEL)" "$(LIBSEPOL_VERSION)" "$(LIBSEPOL_SHA256)" > "$(ORLIXOS_LIBSEPOL_PROOF)"; \
	rm -rf "$(ORLIXOS_LIBSEPOL_BUILD_DIR)"; \
	echo "built Orlix Linux libsepol package input: $(ORLIXOS_LIBSEPOL_A)"

$(ORLIXOS_LIBSEPOL_A): $(ORLIXOS_LIBSEPOL_PROOF)
	@set -euo pipefail; \
	[ -s "$(ORLIXOS_LIBSEPOL_A)" ] || { echo "missing libsepol archive: $(ORLIXOS_LIBSEPOL_A)" >&2; exit 1; }

$(ORLIXOS_LIBSELINUX_PROOF): $(ORLIXOS_LIBSELINUX_SOURCE_STAMP) $(ORLIXOS_LIBSEPOL_PROOF) $(ORLIXOS_PCRE2_PROOF) $(ORLIXOS_FTS_STANDALONE_PROOF) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB) $(PROJECT_DIR)/Sources/make/linux-feature-packages.mk
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	rtlib="$(ORLIXOS_MLIBC_RTLIB)"; \
	[ -s "$$sysroot/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $$sysroot/usr/lib/libc.a" >&2; exit 1; }; \
	[ -d "$$headers" ] || { echo "missing Orlix Linux UAPI headers: $$headers" >&2; exit 1; }; \
	[ -s "$$rtlib" ] || { echo "missing Orlix compiler runtime archive: $$rtlib" >&2; exit 1; }; \
	rm -rf "$(ORLIXOS_LIBSELINUX_BUILD_DIR)" "$(ORLIXOS_LIBSELINUX_A)" "$(ORLIXOS_GETENFORCE_BINARY)" "$(ORLIXOS_SETENFORCE_BINARY)" "$(ORLIXOS_SELINUXENABLED_BINARY)" "$(ORLIXOS_POLICYVERS_BINARY)" "$(ORLIXOS_GETPOLICYLOAD_BINARY)" "$(ORLIXOS_LIBSELINUX_PROOF)"; \
	cp -R "$(ORLIXOS_LIBSELINUX_SRC_DIR)" "$(ORLIXOS_LIBSELINUX_BUILD_DIR)"; \
	$(MAKE) -C "$(ORLIXOS_LIBSELINUX_BUILD_DIR)/include" install DESTDIR="$(ORLIXOS_PACKAGE_INSTALL_DIR)" PREFIX=/usr; \
	$(MAKE) -C "$(ORLIXOS_LIBSELINUX_BUILD_DIR)/src" -j1 OS=Linux DISABLE_SHARED=y DISABLE_RPM=y DISABLE_SETRANS=y DISABLE_X11=y DISABLE_FLAGS="-DNO_X_BACKEND -DNO_ANDROID_BACKEND" PKG_CONFIG_LIBDIR="$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/lib/pkgconfig" PKG_CONFIG_PATH="$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/lib/pkgconfig" PCRE_MODULE=libpcre2-8 PCRE_CFLAGS="-DUSE_PCRE2 -DPCRE2_CODE_UNIT_WIDTH=8 -I$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/include -DPCRE2_STATIC" PCRE_LDLIBS="$(ORLIXOS_LIBPCRE2_8_A)" FTS_LDLIBS="$(ORLIXOS_LIBFTS_A)" LIBSEPOLA="$(ORLIXOS_LIBSEPOL_A)" CC="$(ORLIXOS_CC) --target=aarch64-linux-gnu --sysroot=$$sysroot -isystem $$headers -I$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/include -fhosted -fno-builtin -femulated-tls -ffixed-x18 -fPIC" AR="$(ORLIXOS_AR)" RANLIB="$(ORLIXOS_RANLIB)" CFLAGS="$(ORLIXOS_PACKAGE_CFLAGS) -DHAVE_REALLOCARRAY -DHAVE_STRLCPY -DLIBSELINUX_USE_STRONG_PTHREAD_ONCE -Wno-error" LDFLAGS="-L$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/lib"; \
	$(MAKE) -C "$(ORLIXOS_LIBSELINUX_BUILD_DIR)/src" install OS=Linux DISABLE_SHARED=y DISABLE_RPM=y DISABLE_SETRANS=y DISABLE_X11=y DISABLE_FLAGS="-DNO_X_BACKEND -DNO_ANDROID_BACKEND" PKG_CONFIG_LIBDIR="$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/lib/pkgconfig" PKG_CONFIG_PATH="$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/lib/pkgconfig" PCRE_MODULE=libpcre2-8 PCRE_CFLAGS="-DUSE_PCRE2 -DPCRE2_CODE_UNIT_WIDTH=8 -I$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/include -DPCRE2_STATIC" PCRE_LDLIBS="$(ORLIXOS_LIBPCRE2_8_A)" FTS_LDLIBS="$(ORLIXOS_LIBFTS_A)" LIBSEPOLA="$(ORLIXOS_LIBSEPOL_A)" CC="$(ORLIXOS_CC) --target=aarch64-linux-gnu --sysroot=$$sysroot -isystem $$headers -I$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/include -fhosted -fno-builtin -femulated-tls -ffixed-x18 -fPIC" AR="$(ORLIXOS_AR)" RANLIB="$(ORLIXOS_RANLIB)" CFLAGS="$(ORLIXOS_PACKAGE_CFLAGS) -DHAVE_REALLOCARRAY -DHAVE_STRLCPY -DLIBSELINUX_USE_STRONG_PTHREAD_ONCE -Wno-error" LDFLAGS="-L$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/lib" DESTDIR="$(ORLIXOS_PACKAGE_INSTALL_DIR)" PREFIX=/usr LIBDIR=/usr/lib; \
	mkdir -p "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin"; \
	for program in getenforce setenforce selinuxenabled policyvers getpolicyload; do \
		"$(ORLIXOS_CC)" --target=aarch64-linux-gnu --sysroot="$$sysroot" -isystem "$$headers" -I"$(ORLIXOS_LIBSELINUX_BUILD_DIR)/include" -I"$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/include" -D_GNU_SOURCE $(ORLIXOS_PACKAGE_CFLAGS) -fhosted -fno-builtin -femulated-tls -ffixed-x18 -fno-pie -static -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--image-base=$(ORLIXOS_HOSTED_USER_BASE_ADDRESS) "$$sysroot/usr/lib/crt1.o" "$$sysroot/usr/lib/crti.o" "$(ORLIXOS_LIBSELINUX_BUILD_DIR)/utils/$$program.c" -Wl,--start-group "$(ORLIXOS_LIBSELINUX_A)" "$(ORLIXOS_LIBSEPOL_A)" "$(ORLIXOS_LIBPCRE2_8_A)" "$(ORLIXOS_LIBFTS_A)" "$$sysroot/usr/lib/libc.a" "$$sysroot/usr/lib/libm.a" "$$sysroot/usr/lib/libpthread.a" "$$sysroot/usr/lib/libssp_nonshared.a" "$$sysroot/usr/lib/libssp.a" "$$rtlib" -Wl,--end-group "$$sysroot/usr/lib/crtn.o" -o "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program"; \
		"$(ORLIXOS_STRIP)" "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program"; \
		file "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program" >&2; exit 1; }; \
	done; \
	[ -s "$(ORLIXOS_LIBSELINUX_A)" ] || { echo "missing libselinux archive: $(ORLIXOS_LIBSELINUX_A)" >&2; exit 1; }; \
	printf 'profile=%s\ndistribution=%s\nchannel=%s\npackage=libselinux\nversion=%s\nsha256=%s\nlibraries=libselinux.a\nprograms=getenforce,setenforce,selinuxenabled,policyvers,getpolicyload\n' "$(PROFILE)" "$(ORLIXOS_DISTRIBUTION_ID)" "$(ORLIXOS_DISTRIBUTION_CHANNEL)" "$(LIBSELINUX_VERSION)" "$(LIBSELINUX_SHA256)" > "$(ORLIXOS_LIBSELINUX_PROOF)"; \
	rm -rf "$(ORLIXOS_LIBSELINUX_BUILD_DIR)"; \
	echo "built Orlix Linux libselinux package inputs: $(ORLIXOS_LIBSELINUX_A) $(ORLIXOS_GETENFORCE_BINARY) $(ORLIXOS_SETENFORCE_BINARY) $(ORLIXOS_SELINUXENABLED_BINARY) $(ORLIXOS_POLICYVERS_BINARY) $(ORLIXOS_GETPOLICYLOAD_BINARY)"

$(ORLIXOS_LIBSELINUX_A) $(ORLIXOS_GETENFORCE_BINARY) $(ORLIXOS_SETENFORCE_BINARY) $(ORLIXOS_SELINUXENABLED_BINARY) $(ORLIXOS_POLICYVERS_BINARY) $(ORLIXOS_GETPOLICYLOAD_BINARY): $(ORLIXOS_LIBSELINUX_PROOF)
	@set -euo pipefail; \
	[ -s "$(ORLIXOS_LIBSELINUX_A)" ] || { echo "missing libselinux archive: $(ORLIXOS_LIBSELINUX_A)" >&2; exit 1; }; \
	[ -x "$(ORLIXOS_GETENFORCE_BINARY)" ] || { echo "missing getenforce package input: $(ORLIXOS_GETENFORCE_BINARY)" >&2; exit 1; }; \
	[ -x "$(ORLIXOS_SETENFORCE_BINARY)" ] || { echo "missing setenforce package input: $(ORLIXOS_SETENFORCE_BINARY)" >&2; exit 1; }; \
	[ -x "$(ORLIXOS_SELINUXENABLED_BINARY)" ] || { echo "missing selinuxenabled package input: $(ORLIXOS_SELINUXENABLED_BINARY)" >&2; exit 1; }; \
	[ -x "$(ORLIXOS_POLICYVERS_BINARY)" ] || { echo "missing policyvers package input: $(ORLIXOS_POLICYVERS_BINARY)" >&2; exit 1; }; \
	[ -x "$(ORLIXOS_GETPOLICYLOAD_BINARY)" ] || { echo "missing getpolicyload package input: $(ORLIXOS_GETPOLICYLOAD_BINARY)" >&2; exit 1; }

$(ORLIXOS_CHECKPOLICY_PROOF): $(ORLIXOS_CHECKPOLICY_SOURCE_STAMP) $(ORLIXOS_LIBSEPOL_SOURCE_STAMP) $(ORLIXOS_CHECKPOLICY_HOST_COMPAT) $(PROJECT_DIR)/Sources/make/linux-feature-packages.mk
	@set -euo pipefail; \
	command -v bison >/dev/null 2>&1 || { echo "bison is required to build upstream checkpolicy" >&2; exit 1; }; \
	command -v flex >/dev/null 2>&1 || { echo "flex is required to build upstream checkpolicy" >&2; exit 1; }; \
	rm -rf "$(ORLIXOS_CHECKPOLICY_BUILD_DIR)" "$(ORLIXOS_CHECKPOLICY_HOST_BINARY)" "$(ORLIXOS_CHECKPOLICY_PROOF)"; \
	mkdir -p "$(ORLIXOS_CHECKPOLICY_BUILD_DIR)" "$(dir $(ORLIXOS_CHECKPOLICY_HOST_BINARY))"; \
	cp -R "$(ORLIXOS_LIBSEPOL_SRC_DIR)" "$(ORLIXOS_CHECKPOLICY_BUILD_DIR)/libsepol"; \
	cp -R "$(ORLIXOS_CHECKPOLICY_SRC_DIR)" "$(ORLIXOS_CHECKPOLICY_BUILD_DIR)/checkpolicy"; \
	$(MAKE) -C "$(ORLIXOS_CHECKPOLICY_BUILD_DIR)/libsepol/src" -j1 DISABLE_SHARED=y DISABLE_CIL=y CC=cc AR=ar RANLIB=ranlib CFLAGS="-O2 -Wall -Wno-error"; \
	$(MAKE) -C "$(ORLIXOS_CHECKPOLICY_BUILD_DIR)/checkpolicy" -j1 checkpolicy CPPFLAGS="-include $(ORLIXOS_CHECKPOLICY_HOST_COMPAT) -I$(ORLIXOS_CHECKPOLICY_BUILD_DIR)/libsepol/include" LIBSEPOLA="$(ORLIXOS_CHECKPOLICY_BUILD_DIR)/libsepol/src/libsepol.a" CFLAGS="-O2 -Wall -Wshadow -fno-strict-aliasing -Wno-error"; \
	cp "$(ORLIXOS_CHECKPOLICY_BUILD_DIR)/checkpolicy/checkpolicy" "$(ORLIXOS_CHECKPOLICY_HOST_BINARY)"; \
	"$(ORLIXOS_CHECKPOLICY_HOST_BINARY)" -V >/dev/null; \
	printf 'profile=%s\npackage=checkpolicy\nversion=%s\nsha256=%s\nhost_tool=checkpolicy\n' "$(PROFILE)" "$(CHECKPOLICY_VERSION)" "$(CHECKPOLICY_SHA256)" > "$(ORLIXOS_CHECKPOLICY_PROOF)"; \
	rm -rf "$(ORLIXOS_CHECKPOLICY_BUILD_DIR)"; \
	echo "built upstream checkpolicy host tool: $(ORLIXOS_CHECKPOLICY_HOST_BINARY)"

$(ORLIXOS_CHECKPOLICY_HOST_BINARY): $(ORLIXOS_CHECKPOLICY_PROOF)
	@set -euo pipefail; \
	[ -x "$(ORLIXOS_CHECKPOLICY_HOST_BINARY)" ] || { echo "missing checkpolicy host tool: $(ORLIXOS_CHECKPOLICY_HOST_BINARY)" >&2; exit 1; }

$(ORLIXOS_COREUTILS_TEST_SELINUX_POLICY_SOURCE): $(ORLIXOS_COREUTILS_TEST_SELINUX_POLICY_GENERATOR) $(ORLIXOS_COREUTILS_TEST_SELINUX_POLICY_BODY)
	@set -euo pipefail; \
	$(KERNEL_MAKE) prepare PROFILE="$(PROFILE)" >/dev/null; \
	mkdir -p "$(dir $(ORLIXOS_COREUTILS_TEST_SELINUX_POLICY_BINARY))"; \
	[ -s "$(ORLIXOS_KERNEL_SELINUX_CLASSMAP)" ] || { echo "missing Linux SELinux class map: $(ORLIXOS_KERNEL_SELINUX_CLASSMAP)" >&2; exit 1; }; \
	[ -s "$(ORLIXOS_KERNEL_SELINUX_INITIAL_SIDS)" ] || { echo "missing Linux SELinux initial SID map: $(ORLIXOS_KERNEL_SELINUX_INITIAL_SIDS)" >&2; exit 1; }; \
	perl "$(ORLIXOS_COREUTILS_TEST_SELINUX_POLICY_GENERATOR)" "$(ORLIXOS_KERNEL_SELINUX_CLASSMAP)" "$(ORLIXOS_KERNEL_SELINUX_INITIAL_SIDS)" > "$(ORLIXOS_COREUTILS_TEST_SELINUX_POLICY_SOURCE)"; \
	class_set="$$(sed -n 's/^# ORLIX_SELINUX_ALL_CLASSES //p' "$(ORLIXOS_COREUTILS_TEST_SELINUX_POLICY_SOURCE)")"; \
	[ -n "$$class_set" ] || { echo "missing generated Linux SELinux class set" >&2; exit 1; }; \
	printf '\n' >> "$(ORLIXOS_COREUTILS_TEST_SELINUX_POLICY_SOURCE)"; \
	sed "s/@ORLIX_SELINUX_ALL_CLASSES@/$$class_set/g" "$(ORLIXOS_COREUTILS_TEST_SELINUX_POLICY_BODY)" >> "$(ORLIXOS_COREUTILS_TEST_SELINUX_POLICY_SOURCE)"; \
	[ -s "$(ORLIXOS_COREUTILS_TEST_SELINUX_POLICY_SOURCE)" ] || { echo "missing generated Coreutils SELinux test policy: $(ORLIXOS_COREUTILS_TEST_SELINUX_POLICY_SOURCE)" >&2; exit 1; }; \
	echo "generated Coreutils SELinux test policy from Linux SELinux class map: $(ORLIXOS_COREUTILS_TEST_SELINUX_POLICY_SOURCE)"

$(ORLIXOS_COREUTILS_TEST_SELINUX_POLICY_BINARY): $(ORLIXOS_CHECKPOLICY_HOST_BINARY) $(ORLIXOS_COREUTILS_TEST_SELINUX_POLICY_SOURCE)
	@set -euo pipefail; \
	mkdir -p "$(dir $(ORLIXOS_COREUTILS_TEST_SELINUX_POLICY_BINARY))"; \
	"$(ORLIXOS_CHECKPOLICY_HOST_BINARY)" -M -c 33 -o "$(ORLIXOS_COREUTILS_TEST_SELINUX_POLICY_BINARY)" "$(ORLIXOS_COREUTILS_TEST_SELINUX_POLICY_SOURCE)"; \
	[ -s "$(ORLIXOS_COREUTILS_TEST_SELINUX_POLICY_BINARY)" ] || { echo "missing compiled Coreutils SELinux test policy: $(ORLIXOS_COREUTILS_TEST_SELINUX_POLICY_BINARY)" >&2; exit 1; }; \
	echo "compiled Coreutils SELinux test policy: $(ORLIXOS_COREUTILS_TEST_SELINUX_POLICY_BINARY)"

$(ORLIXOS_POLICYCOREUTILS_PROOF): $(ORLIXOS_POLICYCOREUTILS_SOURCE_STAMP) $(ORLIXOS_LIBSELINUX_PROOF) $(ORLIXOS_LIBSEPOL_PROOF) $(ORLIXOS_PCRE2_PROOF) $(ORLIXOS_FTS_STANDALONE_PROOF) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB) $(PROJECT_DIR)/Sources/make/linux-feature-packages.mk
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	rtlib="$(ORLIXOS_MLIBC_RTLIB)"; \
	[ -s "$$sysroot/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $$sysroot/usr/lib/libc.a" >&2; exit 1; }; \
	[ -d "$$headers" ] || { echo "missing Orlix Linux UAPI headers: $$headers" >&2; exit 1; }; \
	[ -s "$$rtlib" ] || { echo "missing Orlix compiler runtime archive: $$rtlib" >&2; exit 1; }; \
	rm -rf "$(ORLIXOS_POLICYCOREUTILS_BUILD_DIR)" "$(ORLIXOS_SESTATUS_BINARY)" "$(ORLIXOS_POLICYCOREUTILS_PROOF)"; \
	cp -R "$(ORLIXOS_POLICYCOREUTILS_SRC_DIR)" "$(ORLIXOS_POLICYCOREUTILS_BUILD_DIR)"; \
	mkdir -p "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin"; \
	"$(ORLIXOS_CC)" --target=aarch64-linux-gnu --sysroot="$$sysroot" -isystem "$$headers" -I"$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/include" -D_GNU_SOURCE $(ORLIXOS_PACKAGE_CFLAGS) -fhosted -fno-builtin -femulated-tls -ffixed-x18 -fno-pie -static -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--image-base=$(ORLIXOS_HOSTED_USER_BASE_ADDRESS) "$$sysroot/usr/lib/crt1.o" "$$sysroot/usr/lib/crti.o" "$(ORLIXOS_POLICYCOREUTILS_BUILD_DIR)/sestatus/sestatus.c" -Wl,--start-group "$(ORLIXOS_LIBSELINUX_A)" "$(ORLIXOS_LIBSEPOL_A)" "$(ORLIXOS_LIBPCRE2_8_A)" "$(ORLIXOS_LIBFTS_A)" "$$sysroot/usr/lib/libc.a" "$$sysroot/usr/lib/libm.a" "$$sysroot/usr/lib/libpthread.a" "$$sysroot/usr/lib/libssp_nonshared.a" "$$sysroot/usr/lib/libssp.a" "$$rtlib" -Wl,--end-group "$$sysroot/usr/lib/crtn.o" -o "$(ORLIXOS_SESTATUS_BINARY)"; \
	"$(ORLIXOS_STRIP)" "$(ORLIXOS_SESTATUS_BINARY)"; \
	file "$(ORLIXOS_SESTATUS_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_SESTATUS_BINARY)" >&2; exit 1; }; \
	printf 'profile=%s\npackage=policycoreutils\nversion=%s\nsha256=%s\nprograms=sestatus\n' "$(PROFILE)" "$(POLICYCOREUTILS_VERSION)" "$(POLICYCOREUTILS_SHA256)" > "$(ORLIXOS_POLICYCOREUTILS_PROOF)"; \
	rm -rf "$(ORLIXOS_POLICYCOREUTILS_BUILD_DIR)"; \
	echo "built upstream policycoreutils package input: $(ORLIXOS_SESTATUS_BINARY)"

$(ORLIXOS_SESTATUS_BINARY): $(ORLIXOS_POLICYCOREUTILS_PROOF)
	@set -euo pipefail; \
	[ -x "$(ORLIXOS_SESTATUS_BINARY)" ] || { echo "missing policycoreutils sestatus package input: $(ORLIXOS_SESTATUS_BINARY)" >&2; exit 1; }
