$(ORLIXOS_INIT_BINARY): $(ORLIXOS_INIT_SOURCE) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB)
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	rtlib="$(ORLIXOS_MLIBC_RTLIB)"; \
	[ -s "$$sysroot/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $$sysroot/usr/lib/libc.a" >&2; exit 1; }; \
	[ -d "$$headers" ] || { echo "missing Orlix Linux UAPI headers: $$headers" >&2; exit 1; }; \
	[ -s "$$rtlib" ] || { echo "missing Orlix compiler runtime archive: $$rtlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_CC)" >/dev/null 2>&1 || { echo "clang is required to build Orlix init; set ORLIXOS_CC=/path/to/clang" >&2; exit 1; }; \
	command -v "$(ORLIXOS_STRIP)" >/dev/null 2>&1 || { echo "llvm-strip is required to package Orlix init; set ORLIXOS_STRIP=/path/to/llvm-strip" >&2; exit 1; }; \
	mkdir -p "$(dir $(ORLIXOS_INIT_BINARY))"; \
	"$(ORLIXOS_CC)" --target=aarch64-linux-gnu --sysroot="$$sysroot" -isystem "$$headers" -D_GNU_SOURCE -std=c17 -O2 -fhosted -fno-builtin -femulated-tls -ffixed-x18 -fno-pie -static -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--image-base=$(ORLIXOS_HOSTED_USER_BASE_ADDRESS) "$$sysroot/usr/lib/crt1.o" "$$sysroot/usr/lib/crti.o" "$(ORLIXOS_INIT_SOURCE)" -Wl,--start-group "$$sysroot/usr/lib/libc.a" "$$sysroot/usr/lib/libm.a" "$$sysroot/usr/lib/libpthread.a" "$$sysroot/usr/lib/libssp_nonshared.a" "$$sysroot/usr/lib/libssp.a" "$$rtlib" -Wl,--end-group "$$sysroot/usr/lib/crtn.o" -o "$(ORLIXOS_INIT_BINARY)"; \
	"$(ORLIXOS_STRIP)" "$(ORLIXOS_INIT_BINARY)"; \
	file "$(ORLIXOS_INIT_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_INIT_BINARY)" >&2; exit 1; }; \
	printf 'profile=%s\ndistribution=%s\nchannel=%s\nprogram=init\nconsole=/dev/hvc0\nshell=/bin/sh\n' "$(PROFILE)" "$(ORLIXOS_DISTRIBUTION_ID)" "$(ORLIXOS_DISTRIBUTION_CHANNEL)" > "$(ORLIXOS_PACKAGE_INSTALL_DIR)/init.proof"; \
	echo "built OrlixOS first-stage init: $(ORLIXOS_INIT_BINARY)"

$(ORLIXOS_ROOT_INIT_BINARY): $(ORLIXOS_ROOT_INIT_SOURCE) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB)
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	rtlib="$(ORLIXOS_MLIBC_RTLIB)"; \
	[ -s "$$sysroot/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $$sysroot/usr/lib/libc.a" >&2; exit 1; }; \
	[ -d "$$headers" ] || { echo "missing Orlix Linux UAPI headers: $$headers" >&2; exit 1; }; \
	[ -s "$$rtlib" ] || { echo "missing Orlix compiler runtime archive: $$rtlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_CC)" >/dev/null 2>&1 || { echo "clang is required to build Orlix root init; set ORLIXOS_CC=/path/to/clang" >&2; exit 1; }; \
	command -v "$(ORLIXOS_STRIP)" >/dev/null 2>&1 || { echo "llvm-strip is required to package Orlix root init; set ORLIXOS_STRIP=/path/to/llvm-strip" >&2; exit 1; }; \
	mkdir -p "$(dir $(ORLIXOS_ROOT_INIT_BINARY))"; \
	"$(ORLIXOS_CC)" --target=aarch64-linux-gnu --sysroot="$$sysroot" -isystem "$$headers" -D_GNU_SOURCE -std=c17 -O2 -fhosted -fno-builtin -femulated-tls -ffixed-x18 -fno-pie -static -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--image-base=$(ORLIXOS_HOSTED_USER_BASE_ADDRESS) "$$sysroot/usr/lib/crt1.o" "$$sysroot/usr/lib/crti.o" "$(ORLIXOS_ROOT_INIT_SOURCE)" -Wl,--start-group "$$sysroot/usr/lib/libc.a" "$$sysroot/usr/lib/libm.a" "$$sysroot/usr/lib/libpthread.a" "$$sysroot/usr/lib/libssp_nonshared.a" "$$sysroot/usr/lib/libssp.a" "$$rtlib" -Wl,--end-group "$$sysroot/usr/lib/crtn.o" -o "$(ORLIXOS_ROOT_INIT_BINARY)"; \
	"$(ORLIXOS_STRIP)" "$(ORLIXOS_ROOT_INIT_BINARY)"; \
	file "$(ORLIXOS_ROOT_INIT_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_ROOT_INIT_BINARY)" >&2; exit 1; }; \
	printf 'profile=%s\ndistribution=%s\nchannel=%s\nprogram=rootinit\nroot_mode=%s\n' "$(PROFILE)" "$(ORLIXOS_DISTRIBUTION_ID)" "$(ORLIXOS_DISTRIBUTION_CHANNEL)" "$(ORLIXOS_PROFILE_ROOT_MODE)" > "$(ORLIXOS_PACKAGE_INSTALL_DIR)/rootinit.proof"; \
	echo "built OrlixOS root initramfs init: $(ORLIXOS_ROOT_INIT_BINARY)"

$(ORLIXOS_INITRAMFS_CPIO): $(ORLIXOS_ROOT_INIT_BINARY) $(ORLIXOS_MANIFEST)
	@set -euo pipefail; \
	$(KERNEL_MAKE) prepare PROFILE="$(PROFILE)" >/dev/null; \
	gen_init_cpio="$(REPO_ROOT)/Build/OrlixKernel/build/$(PROFILE)/usr/gen_init_cpio"; \
	output="$(ORLIXOS_INITRAMFS_CPIO)"; \
	cpio_list="$(ORLIXOS_ROOTFS_DIR)/initramfs.list"; \
	case "$$output" in "$(REPO_ROOT)"/Build/OrlixOS/rootfs/*/rootfs/initramfs.cpio.gz) ;; *) echo "refusing to write OrlixOS initramfs outside Build/OrlixOS/rootfs: $$output" >&2; exit 1 ;; esac; \
	[ -x "$$gen_init_cpio" ] || { echo "missing Linux gen_init_cpio: $$gen_init_cpio" >&2; exit 1; }; \
	mkdir -p "$(ORLIXOS_INITRAMFS_DIR)"; \
	{ \
		printf 'dir /dev 0755 0 0\n'; \
		printf 'nod /dev/console 0600 0 0 c 5 1\n'; \
		printf 'dir /root 0700 0 0\n'; \
		if [ "$(ORLIXOS_PROFILE_ROOT_MODE)" = overlay ]; then \
			printf 'file /init %s 0755 0 0\n' "$(ORLIXOS_ROOT_INIT_BINARY)"; \
		fi; \
	} > "$$cpio_list"; \
	"$$gen_init_cpio" "$$cpio_list" > "$$output.tmp"; \
	gzip -n -c "$$output.tmp" > "$$output"; \
	rm -f "$$output.tmp"; \
	[ -s "$$output" ] || { echo "missing generated OrlixOS initramfs: $$output" >&2; exit 1; }; \
	echo "built OrlixOS product initramfs: $$output"

$(ORLIXOS_ROOTFS_PROOF): $(ORLIXOS_BASH_BINARY) $(ORLIXOS_COREUTILS_PROOF) $(ORLIXOS_FINDUTILS_PROOF) $(ORLIXOS_GETCONF_BINARY) $(ORLIXOS_INIT_BINARY) $(ORLIXOS_INITRAMFS_CPIO) $(ORLIXOS_MANIFEST)
	@set -euo pipefail; \
	root_tree="$(ORLIXOS_BASE_ROOT_TREE)"; \
	state_tree="$(ORLIXOS_STATE_ROOT_TREE)"; \
	case "$$root_tree" in "$(REPO_ROOT)"/Build/OrlixOS/rootfs/*/base-tree) ;; *) echo "refusing to write OrlixOS root tree outside Build/OrlixOS/rootfs: $$root_tree" >&2; exit 1 ;; esac; \
	case "$$state_tree" in "$(REPO_ROOT)"/Build/OrlixOS/rootfs/*/state-tree) ;; *) echo "refusing to write OrlixOS state tree outside Build/OrlixOS/rootfs: $$state_tree" >&2; exit 1 ;; esac; \
	for path in "$(REPO_ROOT)/Build" "$(ORLIXOS_BUILD_ROOT)" "$(ORLIXOS_ROOTFS_DIR)" "$$root_tree" "$$state_tree"; do \
		if [ -L "$$path" ]; then echo "refusing to package OrlixOS root tree through symlinked path: $$path" >&2; exit 1; fi; \
	done; \
	rm -rf "$$root_tree" "$$state_tree"; \
	mkdir -p "$$root_tree/bin" "$$root_tree/dev" "$$root_tree/etc" "$$root_tree/proc" "$$root_tree/root" "$$root_tree/run" "$$root_tree/sbin" "$$root_tree/sys" "$$root_tree/tmp" "$$root_tree/usr/bin" "$$root_tree/usr/share/orlixos" "$$root_tree/var/tmp"; \
	mkdir -p "$$state_tree/upper" "$$state_tree/work"; \
	install -m 0755 "$(ORLIXOS_BASH_BINARY)" "$$root_tree/bin/bash"; \
	for program in $(ORLIXOS_COREUTILS_PROGRAMS); do install -m 0755 "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program" "$$root_tree/bin/$$program"; done; \
	for program in $(ORLIXOS_FINDUTILS_PROGRAMS); do install -m 0755 "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program" "$$root_tree/bin/$$program"; done; \
	install -m 0755 "$(ORLIXOS_GETCONF_BINARY)" "$$root_tree/usr/bin/getconf"; \
	install -m 0755 "$(ORLIXOS_INIT_BINARY)" "$$root_tree/sbin/init"; \
	ln -s bash "$$root_tree/bin/sh"; \
	printf '%s\n' 'root:x:0:0:root:/root:/bin/sh' > "$$root_tree/etc/passwd"; \
	printf '%s\n' 'root:x:0:root' > "$$root_tree/etc/group"; \
	printf '%s\n' 'NAME=$(ORLIXOS_DISTRIBUTION_NAME)' 'ID=$(ORLIXOS_DISTRIBUTION_ID)' 'PRETTY_NAME=$(ORLIXOS_DISTRIBUTION_NAME)' > "$$root_tree/etc/os-release"; \
	{ \
		printf 'distribution=%s\n' "$(ORLIXOS_DISTRIBUTION_ID)"; \
		printf 'profile=%s\n' "$(PROFILE)"; \
		printf 'channel=%s\n' "$(ORLIXOS_DISTRIBUTION_CHANNEL)"; \
		printf 'root_modes=%s\n' "$(ORLIXOS_ROOT_MODES)"; \
		printf 'selected_root_mode=%s\n' "$(ORLIXOS_PROFILE_ROOT_MODE)"; \
		printf 'base_root_device=%s\n' "$(ORLIXOS_BASE_ROOT_DEVICE)"; \
		printf 'state_root_device=%s\n' "$(ORLIXOS_STATE_ROOT_DEVICE)"; \
		printf 'packages=%s\n' "$(ORLIXOS_PROFILE_PACKAGES)"; \
		printf 'proof_ladder=%s\n' "$(ORLIXOS_PACKAGE_PROOF_LADDER)"; \
		printf 'downloaded_binary_repositories=%s\n' "$(ORLIXOS_DOWNLOADED_BINARY_REPOSITORIES)"; \
	} > "$$root_tree/usr/share/orlixos/distribution.manifest"; \
	chmod 0755 "$$root_tree" "$$root_tree/bin" "$$root_tree/dev" "$$root_tree/etc" "$$root_tree/proc" "$$root_tree/run" "$$root_tree/sbin" "$$root_tree/sys" "$$root_tree/usr" "$$root_tree/usr/bin" "$$root_tree/usr/share" "$$root_tree/usr/share/orlixos" "$$root_tree/var"; \
	chmod 0700 "$$root_tree/root"; \
	chmod 1777 "$$root_tree/tmp" "$$root_tree/var/tmp"; \
	chmod 0755 "$$state_tree" "$$state_tree/upper" "$$state_tree/work"; \
	printf 'profile=%s\ndistribution=%s\nchannel=%s\nroot_modes=%s\nselected_root_mode=%s\nbase_root_device=%s\nstate_root_device=%s\ninitramfs=%s\nbase_root_tree=%s\nstate_root_tree=%s\ninit=/sbin/init\nshell=/bin/sh\nbase_packages=bash coreutils findutils\ncoreutils_programs=%s\nfindutils_programs=%s\nbash_version=%s\ncoreutils_version=%s\nfindutils_version=%s\n' "$(PROFILE)" "$(ORLIXOS_DISTRIBUTION_ID)" "$(ORLIXOS_DISTRIBUTION_CHANNEL)" "$(ORLIXOS_ROOT_MODES)" "$(ORLIXOS_PROFILE_ROOT_MODE)" "$(ORLIXOS_BASE_ROOT_DEVICE)" "$(ORLIXOS_STATE_ROOT_DEVICE)" "$(ORLIXOS_INITRAMFS_CPIO)" "$$root_tree" "$$state_tree" "$(ORLIXOS_COREUTILS_PROGRAMS)" "$(ORLIXOS_FINDUTILS_PROGRAMS)" "$(BASH_VERSION)" "$(COREUTILS_VERSION)" "$(FINDUTILS_VERSION)" > "$(ORLIXOS_ROOTFS_PROOF)"; \
	echo "built OrlixOS base root tree: $$root_tree"
