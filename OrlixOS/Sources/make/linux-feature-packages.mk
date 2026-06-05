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

$(ORLIXOS_E2FSPROGS_PROOF): $(ORLIXOS_E2FSPROGS_SOURCE_STAMP) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB)
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	rtlib="$(ORLIXOS_MLIBC_RTLIB)"; \
	[ -s "$$sysroot/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $$sysroot/usr/lib/libc.a" >&2; exit 1; }; \
	[ -d "$$headers" ] || { echo "missing Orlix Linux UAPI headers: $$headers" >&2; exit 1; }; \
	[ -s "$$rtlib" ] || { echo "missing Orlix compiler runtime archive: $$rtlib" >&2; exit 1; }; \
	rm -rf "$(ORLIXOS_E2FSPROGS_BUILD_DIR)" "$(ORLIXOS_MKE2FS_BINARY)" "$(ORLIXOS_MKFS_EXT2_BINARY)" "$(ORLIXOS_E2FSPROGS_PROOF)"; \
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
	"$(ORLIXOS_STRIP)" "$(ORLIXOS_MKE2FS_BINARY)"; \
	"$(ORLIXOS_STRIP)" "$(ORLIXOS_MKFS_EXT2_BINARY)"; \
	file "$(ORLIXOS_MKE2FS_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_MKE2FS_BINARY)" >&2; exit 1; }; \
	file "$(ORLIXOS_MKFS_EXT2_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_MKFS_EXT2_BINARY)" >&2; exit 1; }; \
	file "$(ORLIXOS_MKE2FS_BINARY)" | grep -F -q 'statically linked' || { file "$(ORLIXOS_MKE2FS_BINARY)" >&2; exit 1; }; \
	file "$(ORLIXOS_MKFS_EXT2_BINARY)" | grep -F -q 'statically linked' || { file "$(ORLIXOS_MKFS_EXT2_BINARY)" >&2; exit 1; }; \
	printf 'profile=%s\ndistribution=%s\nchannel=%s\npackage=e2fsprogs\nversion=%s\nsha256=%s\nprograms=mke2fs,mkfs.ext2\n' "$(PROFILE)" "$(ORLIXOS_DISTRIBUTION_ID)" "$(ORLIXOS_DISTRIBUTION_CHANNEL)" "$(E2FSPROGS_VERSION)" "$(E2FSPROGS_SHA256)" > "$(ORLIXOS_E2FSPROGS_PROOF)"; \
	rm -rf "$(ORLIXOS_E2FSPROGS_BUILD_DIR)"; \
	echo "built Orlix Linux e2fsprogs package inputs: $(ORLIXOS_MKE2FS_BINARY) $(ORLIXOS_MKFS_EXT2_BINARY)"

$(ORLIXOS_MKE2FS_BINARY) $(ORLIXOS_MKFS_EXT2_BINARY): $(ORLIXOS_E2FSPROGS_PROOF)
	@set -euo pipefail; \
	[ -x "$(ORLIXOS_MKE2FS_BINARY)" ] || { echo "missing e2fsprogs mke2fs package input: $(ORLIXOS_MKE2FS_BINARY)" >&2; exit 1; }; \
	[ -x "$(ORLIXOS_MKFS_EXT2_BINARY)" ] || { echo "missing e2fsprogs mkfs.ext2 package input: $(ORLIXOS_MKFS_EXT2_BINARY)" >&2; exit 1; }
