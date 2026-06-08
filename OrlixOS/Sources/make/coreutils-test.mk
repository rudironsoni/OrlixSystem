test coreutils-test: $(ORLIXOS_COREUTILS_TEST_INITRAMFS)
	@set -euo pipefail; \
	run_log="$(ORLIXOS_COREUTILS_TEST_RUN_LOG)"; \
	mkdir -p "$(dir $(ORLIXOS_COREUTILS_TEST_RUN_LOG))"; \
	$(KERNEL_MAKE) run PROFILE="$(PROFILE)" type=coreutils libc=orlixmlibc ORLIX_KERNEL_TEST_INITRAMFS_INPUT="$(ORLIXOS_COREUTILS_TEST_INITRAMFS)" ORLIX_KERNEL_RUN_UNTIL_MARKER="ORLIX-COREUTILS-TEST-END" ORLIX_KERNEL_RUN_TIMEOUT_SECONDS="$(ORLIXOS_COREUTILS_TEST_TIMEOUT_SECONDS)" | tee "$$run_log"; \
	runtime_log="$$(awk -F'"' '/runtimeLogPath/ { print $$4 }' "$$run_log" | tail -n 1)"; \
	[ -n "$$runtime_log" ] || { echo "missing OrlixTerminal runtime log path in $$run_log" >&2; exit 1; }; \
	for _ in $$(seq 1 3600); do \
		grep -F -q 'ORLIX-COREUTILS-TEST-END' "$$runtime_log" && break; \
		sleep 1; \
	done; \
	if [ -n "$(ORLIXOS_COREUTILS_TESTS)" ]; then \
		LC_ALL=C tr -d '\r' < "$$runtime_log" | grep -E -q 'ORLIX-COREUTILS-TEST-END failures=0 skips=0 total=[1-9][0-9]*$$' || { echo "Coreutils upstream test subset did not complete successfully: $$runtime_log" >&2; exit 1; }; \
	else \
		LC_ALL=C tr -d '\r' < "$$runtime_log" | grep -F -q 'ORLIX-COREUTILS-TEST-END failures=0 skips=0 total=733' || { echo "Coreutils full upstream test suite did not complete successfully with failures=0 skips=0 total=733: $$runtime_log" >&2; exit 1; }; \
	fi; \
	echo "verified upstream Coreutils tests in simulator log: $$runtime_log"
$(ORLIXOS_COREUTILS_TEST_INIT_BINARY): $(ORLIXOS_COREUTILS_TEST_INIT_SOURCE) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB)
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	rtlib="$(ORLIXOS_MLIBC_RTLIB)"; \
	[ -s "$$sysroot/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $$sysroot/usr/lib/libc.a" >&2; exit 1; }; \
	[ -d "$$headers" ] || { echo "missing Orlix Linux UAPI headers: $$headers" >&2; exit 1; }; \
	[ -s "$$rtlib" ] || { echo "missing Orlix compiler runtime archive: $$rtlib" >&2; exit 1; }; \
	mkdir -p "$(dir $(ORLIXOS_COREUTILS_TEST_INIT_BINARY))"; \
	"$(ORLIXOS_CC)" --target=aarch64-linux-gnu --sysroot="$$sysroot" -isystem "$$headers" -D_GNU_SOURCE -DORLIXOS_COREUTILS_TEST_TIMEOUT_SECONDS=$(ORLIXOS_COREUTILS_TEST_TIMEOUT_SECONDS) -std=c17 -O2 -fhosted -fno-builtin -ffixed-x18 -fno-pie -static -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--image-base=$(ORLIXOS_HOSTED_USER_BASE_ADDRESS) "$$sysroot/usr/lib/crt1.o" "$$sysroot/usr/lib/crti.o" "$(ORLIXOS_COREUTILS_TEST_INIT_SOURCE)" -Wl,--start-group "$$sysroot/usr/lib/libc.a" "$$sysroot/usr/lib/libm.a" "$$sysroot/usr/lib/libpthread.a" "$$sysroot/usr/lib/libssp_nonshared.a" "$$sysroot/usr/lib/libssp.a" "$$rtlib" -Wl,--end-group "$$sysroot/usr/lib/crtn.o" -o "$(ORLIXOS_COREUTILS_TEST_INIT_BINARY)"; \
	"$(ORLIXOS_STRIP)" "$(ORLIXOS_COREUTILS_TEST_INIT_BINARY)"; \
	file "$(ORLIXOS_COREUTILS_TEST_INIT_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_COREUTILS_TEST_INIT_BINARY)" >&2; exit 1; }; \
	echo "built OrlixOS Coreutils test init: $(ORLIXOS_COREUTILS_TEST_INIT_BINARY)"

$(ORLIXOS_COREUTILS_TEST_INITRAMFS): $(ORLIXOS_COREUTILS_TEST_INIT_BINARY) $(ORLIXOS_COREUTILS_TEST_RUNNER) $(ORLIXOS_COREUTILS_TEST_ENV) $(ORLIXOS_COREUTILS_SOURCE_STAMP) $(ORLIXOS_COREUTILS_TEST_LIST) $(ORLIXOS_COREUTILS_TEST_PASSWD) $(ORLIXOS_COREUTILS_TEST_GROUP) $(ORLIXOS_COREUTILS_TEST_SELINUX_CONFIG) $(ORLIXOS_COREUTILS_TEST_SELINUX_POLICY_BINARY) $(ORLIXOS_BASH_BINARY) $(ORLIXOS_COREUTILS_PROOF) $(ORLIXOS_GREP_BINARY) $(ORLIXOS_SED_BINARY) $(ORLIXOS_DIFF_BINARY) $(ORLIXOS_GAWK_BINARY) $(ORLIXOS_FINDUTILS_PROOF) $(ORLIXOS_SETSID_BINARY) $(ORLIXOS_MOUNT_BINARY) $(ORLIXOS_UMOUNT_BINARY) $(ORLIXOS_MKFS_BINARY) $(ORLIXOS_MKE2FS_BINARY) $(ORLIXOS_MKFS_EXT2_BINARY) $(ORLIXOS_MKFS_EXT4_BINARY) $(ORLIXOS_GETLIMITS_BINARY) $(ORLIXOS_GETCONF_BINARY) $(ORLIXOS_GETENT_BINARY) $(ORLIXOS_GETFATTR_BINARY) $(ORLIXOS_SETFATTR_BINARY) $(ORLIXOS_GETFACL_BINARY) $(ORLIXOS_SETFACL_BINARY) $(ORLIXOS_SETCAP_BINARY) $(ORLIXOS_GETCAP_BINARY) $(ORLIXOS_GETENFORCE_BINARY) $(ORLIXOS_SETENFORCE_BINARY) $(ORLIXOS_SELINUXENABLED_BINARY) $(ORLIXOS_POLICYVERS_BINARY) $(ORLIXOS_GETPOLICYLOAD_BINARY) $(ORLIXOS_SESTATUS_BINARY) $(ORLIXOS_PERL_BINARY) $(ORLIXOS_PERL_PROOF)
	@set -euo pipefail; \
	$(KERNEL_MAKE) prepare PROFILE="$(PROFILE)" >/dev/null; \
	gen_init_cpio="$(REPO_ROOT)/Build/OrlixKernel/build/$(PROFILE)/usr/gen_init_cpio"; \
	output="$(ORLIXOS_COREUTILS_TEST_INITRAMFS_DIR)"; \
	cpio_list="$(ORLIXOS_COREUTILS_TEST_INITRAMFS_DIR)/initramfs.list"; \
	case "$$output" in "$(REPO_ROOT)"/Build/OrlixOS/test-initramfs/*) ;; *) echo "refusing to write Coreutils test initramfs outside Build/OrlixOS/test-initramfs: $$output" >&2; exit 1 ;; esac; \
	[ -x "$$gen_init_cpio" ] || { echo "missing Linux gen_init_cpio: $$gen_init_cpio" >&2; exit 1; }; \
	[ -d "$(ORLIXOS_COREUTILS_SRC_DIR)/tests" ] || { echo "missing upstream Coreutils tests directory: $(ORLIXOS_COREUTILS_SRC_DIR)/tests" >&2; exit 1; }; \
	[ -d "$(ORLIXOS_COREUTILS_SRC_DIR)/build-aux" ] || { echo "missing upstream Coreutils build-aux directory: $(ORLIXOS_COREUTILS_SRC_DIR)/build-aux" >&2; exit 1; }; \
	[ -s "$(ORLIXOS_COREUTILS_SRC_DIR)/init.cfg" ] || { echo "missing upstream Coreutils init.cfg: $(ORLIXOS_COREUTILS_SRC_DIR)/init.cfg" >&2; exit 1; }; \
	[ -s "$(ORLIXOS_COREUTILS_CONFIG_HEADER)" ] || { echo "missing Coreutils config snapshot: $(ORLIXOS_COREUTILS_CONFIG_HEADER)" >&2; exit 1; }; \
	for path in "$(ORLIXOS_COREUTILS_TEST_INIT_BINARY)" "$(ORLIXOS_BASH_BINARY)" "$(ORLIXOS_GREP_BINARY)" "$(ORLIXOS_SED_BINARY)" "$(ORLIXOS_DIFF_BINARY)" "$(ORLIXOS_GAWK_BINARY)" "$(ORLIXOS_SETSID_BINARY)" "$(ORLIXOS_MOUNT_BINARY)" "$(ORLIXOS_UMOUNT_BINARY)" "$(ORLIXOS_MKFS_BINARY)" "$(ORLIXOS_MKE2FS_BINARY)" "$(ORLIXOS_MKFS_EXT2_BINARY)" "$(ORLIXOS_MKFS_EXT4_BINARY)" "$(ORLIXOS_GETLIMITS_BINARY)" "$(ORLIXOS_GETCONF_BINARY)" "$(ORLIXOS_GETENT_BINARY)" "$(ORLIXOS_GETFATTR_BINARY)" "$(ORLIXOS_SETFATTR_BINARY)" "$(ORLIXOS_GETFACL_BINARY)" "$(ORLIXOS_SETFACL_BINARY)" "$(ORLIXOS_SETCAP_BINARY)" "$(ORLIXOS_GETCAP_BINARY)" "$(ORLIXOS_GETENFORCE_BINARY)" "$(ORLIXOS_SETENFORCE_BINARY)" "$(ORLIXOS_SELINUXENABLED_BINARY)" "$(ORLIXOS_POLICYVERS_BINARY)" "$(ORLIXOS_GETPOLICYLOAD_BINARY)" "$(ORLIXOS_SESTATUS_BINARY)" "$(ORLIXOS_PERL_BINARY)"; do \
		[ -x "$$path" ] || { echo "missing executable package input: $$path" >&2; exit 1; }; \
	done; \
	[ -s "$(ORLIXOS_COREUTILS_TEST_SELINUX_POLICY_BINARY)" ] || { echo "missing Coreutils SELinux test policy: $(ORLIXOS_COREUTILS_TEST_SELINUX_POLICY_BINARY)" >&2; exit 1; }; \
	for program in $(ORLIXOS_COREUTILS_PROGRAMS); do \
		path="$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program"; \
		[ -x "$$path" ] || { echo "missing Coreutils package input: $$path" >&2; exit 1; }; \
	done; \
	for program in $(ORLIXOS_FINDUTILS_PROGRAMS) $(ORLIXOS_DIFFUTILS_PROGRAMS); do \
		path="$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program"; \
		[ -x "$$path" ] || { echo "missing upstream package input: $$path" >&2; exit 1; }; \
	done; \
	[ -d "$(ORLIXOS_PERL_LIB_DIR)" ] || { echo "missing Perl library directory: $(ORLIXOS_PERL_LIB_DIR)" >&2; exit 1; }; \
	rm -rf "$$output"; \
	mkdir -p "$$output/rootfs"; \
	{ \
		printf 'dir /bin 0755 0 0\n'; \
		printf 'dir /etc 0755 0 0\n'; \
		printf 'dir /etc/selinux 0755 0 0\n'; \
		printf 'dir /etc/selinux/targeted 0755 0 0\n'; \
		printf 'dir /etc/selinux/targeted/policy 0755 0 0\n'; \
		printf 'dir /usr 0755 0 0\n'; \
		printf 'dir /usr/bin 0755 0 0\n'; \
		printf 'dir /dev 0755 0 0\n'; \
		printf 'nod /dev/console 0600 0 0 c 5 1\n'; \
		printf 'dir /proc 0555 0 0\n'; \
		printf 'dir /sys 0555 0 0\n'; \
		printf 'dir /root 0700 0 0\n'; \
			printf 'dir /tmp 1777 0 0\n'; \
			printf 'dir /coreutils 0755 0 0\n'; \
			printf 'dir /coreutils-build 0777 0 0\n'; \
			printf 'slink /coreutils-build/coreutils /coreutils 0777 0 0\n'; \
			printf 'file /init %s 0755 0 0\n' "$(ORLIXOS_COREUTILS_TEST_INIT_BINARY)"; \
		printf 'file /coreutils-build/run-upstream-coreutils-tests.sh %s 0755 0 0\n' "$(ORLIXOS_COREUTILS_TEST_RUNNER)"; \
		printf 'file /coreutils-build/coreutils-test-env.sh %s 0644 0 0\n' "$(ORLIXOS_COREUTILS_TEST_ENV)"; \
		printf 'file /etc/passwd %s 0644 0 0\n' "$(ORLIXOS_COREUTILS_TEST_PASSWD)"; \
		printf 'file /etc/group %s 0644 0 0\n' "$(ORLIXOS_COREUTILS_TEST_GROUP)"; \
		printf 'file /etc/selinux/config %s 0644 0 0\n' "$(ORLIXOS_COREUTILS_TEST_SELINUX_CONFIG)"; \
		printf 'file /etc/selinux/targeted/policy/policy.33 %s 0644 0 0\n' "$(ORLIXOS_COREUTILS_TEST_SELINUX_POLICY_BINARY)"; \
		printf 'file /coreutils-test-list.txt %s 0644 0 0\n' "$(ORLIXOS_COREUTILS_TEST_LIST)"; \
		printf 'file /bin/bash %s 0755 0 0\n' "$(ORLIXOS_BASH_BINARY)"; \
		printf 'slink /bin/sh bash 0777 0 0\n'; \
		for program in $(ORLIXOS_COREUTILS_PROGRAMS); do printf 'file /bin/%s %s 0755 0 0\n' "$$program" "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program"; done; \
		printf 'file /bin/grep %s 0755 0 0\n' "$(ORLIXOS_GREP_BINARY)"; \
		printf 'file /bin/sed %s 0755 0 0\n' "$(ORLIXOS_SED_BINARY)"; \
		printf 'file /bin/gawk %s 0755 0 0\n' "$(ORLIXOS_GAWK_BINARY)"; \
		printf 'slink /bin/awk gawk 0777 0 0\n'; \
		for program in $(ORLIXOS_FINDUTILS_PROGRAMS); do printf 'file /bin/%s %s 0755 0 0\n' "$$program" "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program"; done; \
		for program in $(ORLIXOS_DIFFUTILS_PROGRAMS); do printf 'file /bin/%s %s 0755 0 0\n' "$$program" "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program"; done; \
		printf 'file /bin/setsid %s 0755 0 0\n' "$(ORLIXOS_SETSID_BINARY)"; \
		printf 'file /bin/mount %s 0755 0 0\n' "$(ORLIXOS_MOUNT_BINARY)"; \
		printf 'file /bin/umount %s 0755 0 0\n' "$(ORLIXOS_UMOUNT_BINARY)"; \
		printf 'file /bin/mkfs %s 0755 0 0\n' "$(ORLIXOS_MKFS_BINARY)"; \
		printf 'file /bin/mke2fs %s 0755 0 0\n' "$(ORLIXOS_MKE2FS_BINARY)"; \
		printf 'file /bin/mkfs.ext2 %s 0755 0 0\n' "$(ORLIXOS_MKFS_EXT2_BINARY)"; \
		printf 'file /bin/mkfs.ext4 %s 0755 0 0\n' "$(ORLIXOS_MKFS_EXT4_BINARY)"; \
		printf 'file /bin/getfattr %s 0755 0 0\n' "$(ORLIXOS_GETFATTR_BINARY)"; \
		printf 'file /bin/setfattr %s 0755 0 0\n' "$(ORLIXOS_SETFATTR_BINARY)"; \
		printf 'file /bin/getfacl %s 0755 0 0\n' "$(ORLIXOS_GETFACL_BINARY)"; \
		printf 'file /bin/setfacl %s 0755 0 0\n' "$(ORLIXOS_SETFACL_BINARY)"; \
		printf 'file /bin/getcap %s 0755 0 0\n' "$(ORLIXOS_GETCAP_BINARY)"; \
		printf 'file /bin/setcap %s 0755 0 0\n' "$(ORLIXOS_SETCAP_BINARY)"; \
		printf 'file /bin/getenforce %s 0755 0 0\n' "$(ORLIXOS_GETENFORCE_BINARY)"; \
		printf 'file /bin/setenforce %s 0755 0 0\n' "$(ORLIXOS_SETENFORCE_BINARY)"; \
		printf 'file /bin/selinuxenabled %s 0755 0 0\n' "$(ORLIXOS_SELINUXENABLED_BINARY)"; \
		printf 'file /bin/policyvers %s 0755 0 0\n' "$(ORLIXOS_POLICYVERS_BINARY)"; \
		printf 'file /bin/getpolicyload %s 0755 0 0\n' "$(ORLIXOS_GETPOLICYLOAD_BINARY)"; \
		printf 'file /bin/sestatus %s 0755 0 0\n' "$(ORLIXOS_SESTATUS_BINARY)"; \
			printf 'file /bin/getlimits %s 0755 0 0\n' "$(ORLIXOS_GETLIMITS_BINARY)"; \
			printf 'file /bin/getconf %s 0755 0 0\n' "$(ORLIXOS_GETCONF_BINARY)"; \
			printf 'file /bin/getent %s 0755 0 0\n' "$(ORLIXOS_GETENT_BINARY)"; \
		printf 'slink /bin/perl /usr/bin/perl 0777 0 0\n'; \
		printf 'slink /bin/egrep grep 0777 0 0\n'; \
		printf 'slink /usr/bin/bash /bin/bash 0777 0 0\n'; \
		printf 'slink /usr/bin/sh /bin/sh 0777 0 0\n'; \
		printf 'slink /usr/bin/grep /bin/grep 0777 0 0\n'; \
		printf 'slink /usr/bin/sed /bin/sed 0777 0 0\n'; \
		printf 'slink /usr/bin/gawk /bin/gawk 0777 0 0\n'; \
		printf 'slink /usr/bin/awk /bin/awk 0777 0 0\n'; \
		for program in $(ORLIXOS_FINDUTILS_PROGRAMS); do printf 'slink /usr/bin/%s /bin/%s 0777 0 0\n' "$$program" "$$program"; done; \
		for program in $(ORLIXOS_DIFFUTILS_PROGRAMS); do printf 'slink /usr/bin/%s /bin/%s 0777 0 0\n' "$$program" "$$program"; done; \
		printf 'slink /usr/bin/setsid /bin/setsid 0777 0 0\n'; \
		printf 'slink /usr/bin/mount /bin/mount 0777 0 0\n'; \
		printf 'slink /usr/bin/umount /bin/umount 0777 0 0\n'; \
		printf 'slink /usr/bin/mkfs /bin/mkfs 0777 0 0\n'; \
		printf 'slink /usr/bin/mke2fs /bin/mke2fs 0777 0 0\n'; \
		printf 'slink /usr/bin/mkfs.ext2 /bin/mkfs.ext2 0777 0 0\n'; \
		printf 'slink /usr/bin/mkfs.ext4 /bin/mkfs.ext4 0777 0 0\n'; \
		printf 'slink /usr/bin/getfattr /bin/getfattr 0777 0 0\n'; \
		printf 'slink /usr/bin/setfattr /bin/setfattr 0777 0 0\n'; \
		printf 'slink /usr/bin/getfacl /bin/getfacl 0777 0 0\n'; \
		printf 'slink /usr/bin/setfacl /bin/setfacl 0777 0 0\n'; \
		printf 'slink /usr/bin/getcap /bin/getcap 0777 0 0\n'; \
		printf 'slink /usr/bin/setcap /bin/setcap 0777 0 0\n'; \
		printf 'slink /usr/bin/getenforce /bin/getenforce 0777 0 0\n'; \
		printf 'slink /usr/bin/setenforce /bin/setenforce 0777 0 0\n'; \
		printf 'slink /usr/bin/selinuxenabled /bin/selinuxenabled 0777 0 0\n'; \
		printf 'slink /usr/bin/policyvers /bin/policyvers 0777 0 0\n'; \
		printf 'slink /usr/bin/getpolicyload /bin/getpolicyload 0777 0 0\n'; \
		printf 'slink /usr/bin/sestatus /bin/sestatus 0777 0 0\n'; \
			printf 'slink /usr/bin/getlimits /bin/getlimits 0777 0 0\n'; \
			printf 'slink /usr/bin/getconf /bin/getconf 0777 0 0\n'; \
			printf 'slink /usr/bin/getent /bin/getent 0777 0 0\n'; \
		printf 'file /usr/bin/perl %s 0755 0 0\n' "$(ORLIXOS_PERL_BINARY)"; \
		printf 'dir /usr/lib 0755 0 0\n'; \
		printf 'dir /usr/lib/perl5 0755 0 0\n'; \
		find "$(ORLIXOS_PERL_LIB_DIR)" -type d | sort | while read -r dir; do \
			rel="$${dir#"$(ORLIXOS_PACKAGE_INSTALL_DIR)"}"; \
			printf 'dir %s 0755 0 0\n' "$$rel"; \
		done; \
		find "$(ORLIXOS_PERL_LIB_DIR)" -type f | sort | while read -r file; do \
			rel="$${file#"$(ORLIXOS_PACKAGE_INSTALL_DIR)"}"; \
			printf 'file %s %s 0644 0 0\n' "$$rel" "$$file"; \
		done; \
		find "$(ORLIXOS_COREUTILS_SRC_DIR)/tests" -type d | sort | while read -r dir; do \
			rel="$${dir#"$(ORLIXOS_COREUTILS_SRC_DIR)"}"; \
			[ -n "$$rel" ] || rel=""; \
			printf 'dir /coreutils%s 0755 0 0\n' "$$rel"; \
		done; \
		find "$(ORLIXOS_COREUTILS_SRC_DIR)/tests" -type f | sort | while read -r file; do \
			rel="$${file#"$(ORLIXOS_COREUTILS_SRC_DIR)"}"; \
			printf 'file /coreutils%s %s 0644 0 0\n' "$$rel" "$$file"; \
		done; \
		find "$(ORLIXOS_COREUTILS_SRC_DIR)/build-aux" -type d | sort | while read -r dir; do \
			rel="$${dir#"$(ORLIXOS_COREUTILS_SRC_DIR)"}"; \
			printf 'dir /coreutils%s 0755 0 0\n' "$$rel"; \
		done; \
		find "$(ORLIXOS_COREUTILS_SRC_DIR)/build-aux" -type f | sort | while read -r file; do \
			rel="$${file#"$(ORLIXOS_COREUTILS_SRC_DIR)"}"; \
			mode=0644; \
			[ -x "$$file" ] && mode=0755; \
			printf 'file /coreutils%s %s %s 0 0\n' "$$rel" "$$file" "$$mode"; \
		done; \
		printf 'file /coreutils/init.cfg %s 0644 0 0\n' "$(ORLIXOS_COREUTILS_SRC_DIR)/init.cfg"; \
		printf 'dir /coreutils-build/src 0755 0 0\n'; \
		for program in $(ORLIXOS_COREUTILS_PROGRAMS); do printf 'file /coreutils-build/src/%s %s 0755 0 0\n' "$$program" "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program"; done; \
		printf 'dir /coreutils-build/lib 0755 0 0\n'; \
		if [ -s "$(ORLIXOS_COREUTILS_CONFIG_HEADER)" ]; then printf 'file /coreutils-build/lib/config.h %s 0644 0 0\n' "$(ORLIXOS_COREUTILS_CONFIG_HEADER)"; fi; \
	} > "$$cpio_list"; \
	"$$gen_init_cpio" "$$cpio_list" > "$$output/rootfs/initramfs.cpio"; \
	gzip -n -f "$$output/rootfs/initramfs.cpio"; \
	[ -s "$$output/rootfs/initramfs.cpio.gz" ] || { echo "missing packaged Coreutils test initramfs: $$output/rootfs/initramfs.cpio.gz" >&2; exit 1; }; \
	{ \
		printf '%s\n' '<?xml version="1.0" encoding="UTF-8"?>'; \
		printf '%s\n' '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">'; \
		printf '%s\n' '<plist version="1.0"><dict>'; \
		printf '%s\n' '<key>CFBundleIdentifier</key><string>$(ORLIXOS_COREUTILS_TEST_INITRAMFS_BUNDLE_IDENTIFIER)</string>'; \
		printf '%s\n' '<key>CFBundleName</key><string>$(ORLIXOS_COREUTILS_TEST_INITRAMFS_BUNDLE_NAME)</string>'; \
		printf '%s\n' '<key>CFBundlePackageType</key><string>BNDL</string>'; \
		printf '%s\n' '<key>CFBundleShortVersionString</key><string>0.1</string>'; \
		printf '%s\n' '<key>CFBundleVersion</key><string>1</string>'; \
		printf '%s\n' '<key>OrlixProfile</key><string>$(PROFILE)</string>'; \
		printf '%s\n' '<key>OrlixProofLane</key><string>coreutils-upstream-tests</string>'; \
		printf '%s\n' '</dict></plist>'; \
	} > "$$output/Info.plist"; \
	plutil -lint "$$output/Info.plist" >/dev/null; \
	echo "packaged upstream Coreutils test initramfs: $$output"
