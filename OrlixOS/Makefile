SHELL := /bin/bash
.DEFAULT_GOAL := all
.NOTPARALLEL:

PROJECT_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO_ROOT := $(abspath $(PROJECT_DIR)/..)
MLIBC_MAKE := $(MAKE) -C $(REPO_ROOT) -f OrlixMLibC/Makefile
KERNEL_MAKE := $(MAKE) -C $(REPO_ROOT) -f OrlixKernel/Makefile

PROFILE ?= release

include $(PROJECT_DIR)/Sources/distribution/target-settings.xcconfig
include $(PROJECT_DIR)/Sources/distribution/manifest.mk

ifeq ($(PROFILE),release)
ORLIXOS_PROFILE_PACKAGES := $(ORLIXOS_RELEASE_PACKAGES)
ORLIXOS_PROFILE_ROOT_MODE := $(ORLIXOS_RELEASE_ROOT_MODE)
ORLIXOS_PROFILE_KERNEL_COMMAND_LINE := $(ORLIX_OS_RELEASE_KERNEL_COMMAND_LINE)
ORLIXOS_PROFILE_TEST_INITRAMFS_KERNEL_COMMAND_LINE := $(ORLIX_OS_RELEASE_TEST_INITRAMFS_KERNEL_COMMAND_LINE)
else
ORLIXOS_PROFILE_PACKAGES := $(ORLIXOS_DEVELOPMENT_PACKAGES)
ORLIXOS_PROFILE_ROOT_MODE := $(ORLIXOS_DEVELOPMENT_ROOT_MODE)
ORLIXOS_PROFILE_KERNEL_COMMAND_LINE := $(ORLIX_OS_DEVELOPMENT_KERNEL_COMMAND_LINE)
ORLIXOS_PROFILE_TEST_INITRAMFS_KERNEL_COMMAND_LINE := $(ORLIX_OS_DEVELOPMENT_TEST_INITRAMFS_KERNEL_COMMAND_LINE)
endif

include $(PROJECT_DIR)/Sources/make/config.mk

ORLIXOS_ENVIRONMENT_IMAGE_WORK_DIR ?= $(ORLIXOS_BUILD_ROOT)/environment-images/$(PROFILE)
ORLIXOS_ENVIRONMENT_BASE_IMAGE_SIZE ?= $(ORLIX_OS_BASE_ROOT_IMAGE_SIZE)
ORLIXOS_ENVIRONMENT_STATE_IMAGE_SIZE ?= $(ORLIX_OS_STATE_ROOT_IMAGE_SIZE)
ORLIXOS_ENVIRONMENT_RUNTIME_PROOF_STAMP := $(ORLIXOS_BUILD_ROOT)/environment-runtime-proof/.fixtures-ready
ORLIX_MKE2FS ?= $(shell if command -v mke2fs >/dev/null 2>&1; then command -v mke2fs; elif [ -x /opt/homebrew/opt/e2fsprogs/sbin/mke2fs ]; then printf '%s\n' /opt/homebrew/opt/e2fsprogs/sbin/mke2fs; else printf '%s\n' mke2fs; fi)
ORLIX_DEBUGFS ?= $(shell if command -v debugfs >/dev/null 2>&1; then command -v debugfs; elif [ -x /opt/homebrew/opt/e2fsprogs/sbin/debugfs ]; then printf '%s\n' /opt/homebrew/opt/e2fsprogs/sbin/debugfs; else printf '%s\n' debugfs; fi)

.PHONY: all help build rootfs kernel-payload environment-root-image environment-runtime-proof-fixtures test coreutils-test clean mrproper __coreutils-source FORCE

all: rootfs

help:
	@printf '%s\n' 'Targets:'
	@printf '%s\n' '  build    build OrlixOS userspace package inputs'
	@printf '%s\n' '  rootfs   build the immutable OrlixOS base root tree'
	@printf '%s\n' '  kernel-payload package the OrlixOS rootfs for the OrlixOS framework payload'
	@printf '%s\n' '  environment-root-image materialize a staged environment root into base/state ext4 images'
	@printf '%s\n' '  environment-runtime-proof-fixtures materialize tar and OCI runtime proof images'
	@printf '%s\n' '  test     run OrlixOS package tests in the app-hosted Orlix path'
	@printf '%s\n' '  clean    remove generated OrlixOS outputs'

build: $(ORLIXOS_BASH_BINARY) $(ORLIXOS_COREUTILS_PROOF) $(ORLIXOS_FINDUTILS_PROOF) $(ORLIXOS_GETCONF_BINARY) $(ORLIXOS_GETENT_BINARY) $(ORLIXOS_INIT_BINARY)

rootfs: $(ORLIXOS_ROOTFS_PROOF)

kernel-payload: rootfs
	@set -euo pipefail; \
	$(KERNEL_MAKE) __kernel-payload \
		PROFILE="$(PROFILE)" \
		ORLIX_KERNEL_PAYLOAD_BUNDLE_NAME="$(ORLIX_OS_PAYLOAD_SOURCE_BUNDLE_NAME)" \
		ORLIX_KERNEL_PAYLOAD_BUNDLE_EXTENSION="$(ORLIX_OS_PAYLOAD_BUNDLE_EXTENSION)" \
		ORLIX_KERNEL_PAYLOAD_BUNDLE_IDENTIFIER="$(ORLIX_OS_PAYLOAD_SOURCE_BUNDLE_IDENTIFIER)" \
		ORLIX_KERNEL_PAYLOAD_SELECTED_PROFILE_INFO_KEY="$(ORLIX_OS_PAYLOAD_SELECTED_PROFILE_INFO_KEY)" \
		ORLIX_KERNEL_PAYLOAD_KERNEL_COMMAND_LINE_INFO_KEY="$(ORLIX_OS_PAYLOAD_KERNEL_COMMAND_LINE_INFO_KEY)" \
		ORLIX_KERNEL_PAYLOAD_ROOT_INITRAMFS_INFO_KEY="$(ORLIX_OS_PAYLOAD_ROOT_INITRAMFS_INFO_KEY)" \
		ORLIX_KERNEL_PAYLOAD_BASE_ROOT_IMAGE_INFO_KEY="$(ORLIX_OS_PAYLOAD_BASE_ROOT_IMAGE_INFO_KEY)" \
		ORLIX_KERNEL_PAYLOAD_STATE_ROOT_IMAGE_INFO_KEY="$(ORLIX_OS_PAYLOAD_STATE_ROOT_IMAGE_INFO_KEY)" \
		ORLIX_KERNEL_PAYLOAD_BASE_ROOT_DEVICE_INFO_KEY="$(ORLIX_OS_PAYLOAD_BASE_ROOT_DEVICE_INFO_KEY)" \
		ORLIX_KERNEL_PAYLOAD_STATE_ROOT_DEVICE_INFO_KEY="$(ORLIX_OS_PAYLOAD_STATE_ROOT_DEVICE_INFO_KEY)" \
		ORLIX_KERNEL_PAYLOAD_BASE_ROOT_HOST_BLOCK_DEVICE_INFO_KEY="$(ORLIX_OS_PAYLOAD_BASE_ROOT_HOST_BLOCK_DEVICE_INFO_KEY)" \
		ORLIX_KERNEL_PAYLOAD_STATE_ROOT_HOST_BLOCK_DEVICE_INFO_KEY="$(ORLIX_OS_PAYLOAD_STATE_ROOT_HOST_BLOCK_DEVICE_INFO_KEY)" \
		ORLIX_KERNEL_PAYLOAD_STATE_ROOT_MINIMUM_BYTES_INFO_KEY="$(ORLIX_OS_PAYLOAD_STATE_ROOT_MINIMUM_BYTES_INFO_KEY)" \
		ORLIX_KERNEL_ROOT_INITRAMFS_RESOURCE="$(ORLIX_OS_ROOT_INITRAMFS_RESOURCE)" \
		ORLIX_KERNEL_BASE_ROOT_IMAGE_RESOURCE="$(ORLIX_OS_BASE_ROOT_IMAGE_RESOURCE)" \
		ORLIX_KERNEL_STATE_ROOT_IMAGE_RESOURCE="$(ORLIX_OS_STATE_ROOT_IMAGE_RESOURCE)" \
		ORLIX_KERNEL_ROOT_MODES="$(ORLIXOS_ROOT_MODES)" \
		ORLIX_KERNEL_SELECTED_ROOT_MODE="$(ORLIXOS_PROFILE_ROOT_MODE)" \
		ORLIX_KERNEL_COMMAND_LINE="$(ORLIXOS_PROFILE_KERNEL_COMMAND_LINE)" \
		ORLIX_KERNEL_TEST_INITRAMFS_COMMAND_LINE="$(ORLIXOS_PROFILE_TEST_INITRAMFS_KERNEL_COMMAND_LINE)" \
		ORLIX_KERNEL_BASE_ROOT_DEVICE="$(ORLIXOS_BASE_ROOT_DEVICE)" \
		ORLIX_KERNEL_STATE_ROOT_DEVICE="$(ORLIXOS_STATE_ROOT_DEVICE)" \
		ORLIX_KERNEL_BASE_ROOT_HOST_BLOCK_DEVICE="$(ORLIX_OS_BASE_ROOT_HOST_BLOCK_DEVICE)" \
		ORLIX_KERNEL_STATE_ROOT_HOST_BLOCK_DEVICE="$(ORLIX_OS_STATE_ROOT_HOST_BLOCK_DEVICE)" \
		ORLIX_KERNEL_BASE_ROOT_IMAGE_SIZE="$(ORLIX_OS_BASE_ROOT_IMAGE_SIZE)" \
		ORLIX_KERNEL_STATE_ROOT_IMAGE_SIZE="$(ORLIX_OS_STATE_ROOT_IMAGE_SIZE)" \
		ORLIX_KERNEL_STATE_ROOT_MINIMUM_BYTES="$(ORLIX_OS_STATE_ROOT_MINIMUM_BYTES)" \
		ORLIX_KERNEL_ROOT_INITRAMFS_INPUT="$(ORLIXOS_INITRAMFS_CPIO)" \
		ORLIX_KERNEL_BASE_ROOT_TREE_INPUT="$(ORLIXOS_BASE_ROOT_TREE)" \
		ORLIX_KERNEL_STATE_ROOT_TREE_INPUT="$(ORLIXOS_STATE_ROOT_TREE)"

environment-root-image:
	@set -euo pipefail; \
	[ -n "$(ORLIXOS_ENVIRONMENT_STAGING_ROOT)" ] || { echo "ORLIXOS_ENVIRONMENT_STAGING_ROOT is required" >&2; exit 1; }; \
	[ -n "$(ORLIXOS_ENVIRONMENT_BASE_IMAGE)" ] || { echo "ORLIXOS_ENVIRONMENT_BASE_IMAGE is required" >&2; exit 1; }; \
	[ -n "$(ORLIXOS_ENVIRONMENT_STATE_IMAGE)" ] || { echo "ORLIXOS_ENVIRONMENT_STATE_IMAGE is required" >&2; exit 1; }; \
	case "$(ORLIXOS_ENVIRONMENT_STAGING_ROOT)" in /|"") echo "refusing unsafe ORLIXOS_ENVIRONMENT_STAGING_ROOT=$(ORLIXOS_ENVIRONMENT_STAGING_ROOT)" >&2; exit 1 ;; esac; \
	case "$(ORLIXOS_ENVIRONMENT_BASE_IMAGE)" in /|*/|"") echo "refusing unsafe ORLIXOS_ENVIRONMENT_BASE_IMAGE=$(ORLIXOS_ENVIRONMENT_BASE_IMAGE)" >&2; exit 1 ;; esac; \
	case "$(ORLIXOS_ENVIRONMENT_STATE_IMAGE)" in /|*/|"") echo "refusing unsafe ORLIXOS_ENVIRONMENT_STATE_IMAGE=$(ORLIXOS_ENVIRONMENT_STATE_IMAGE)" >&2; exit 1 ;; esac; \
	[ -d "$(ORLIXOS_ENVIRONMENT_STAGING_ROOT)" ] || { echo "missing environment staging root: $(ORLIXOS_ENVIRONMENT_STAGING_ROOT)" >&2; exit 1; }; \
	command -v "$(ORLIX_MKE2FS)" >/dev/null 2>&1 || { echo "mke2fs is required to generate Orlix environment root block images; install e2fsprogs or set ORLIX_MKE2FS=/path/to/mke2fs" >&2; exit 1; }; \
	command -v "$(ORLIX_DEBUGFS)" >/dev/null 2>&1 || { echo "debugfs is required to normalize Orlix environment root image ownership; install e2fsprogs or set ORLIX_DEBUGFS=/path/to/debugfs" >&2; exit 1; }; \
	base_tree="$(ORLIXOS_ENVIRONMENT_IMAGE_WORK_DIR)/base-tree"; \
	state_tree="$(ORLIXOS_ENVIRONMENT_IMAGE_WORK_DIR)/state-tree"; \
	base_debugfs_commands="$(ORLIXOS_ENVIRONMENT_IMAGE_WORK_DIR)/base-debugfs.commands"; \
	state_debugfs_commands="$(ORLIXOS_ENVIRONMENT_IMAGE_WORK_DIR)/state-debugfs.commands"; \
	rm -rf "$$base_tree" "$$state_tree" "$(ORLIXOS_ENVIRONMENT_BASE_IMAGE)" "$(ORLIXOS_ENVIRONMENT_STATE_IMAGE)"; \
	mkdir -p "$$base_tree/dev" "$$base_tree/proc" "$$base_tree/root" "$$base_tree/run" "$$base_tree/sys" "$$base_tree/tmp"; \
	(cd "$(ORLIXOS_ENVIRONMENT_STAGING_ROOT)" && tar -cf - .) | (cd "$$base_tree" && tar -xf -); \
	chmod 0755 "$$base_tree" "$$base_tree/dev" "$$base_tree/proc" "$$base_tree/run" "$$base_tree/sys"; \
	[ -d "$$base_tree/root" ] && chmod 0700 "$$base_tree/root"; \
	[ -d "$$base_tree/tmp" ] && chmod 1777 "$$base_tree/tmp"; \
	mkdir -p "$$(dirname "$(ORLIXOS_ENVIRONMENT_BASE_IMAGE)")" "$$(dirname "$(ORLIXOS_ENVIRONMENT_STATE_IMAGE)")"; \
	truncate -s "$(ORLIXOS_ENVIRONMENT_BASE_IMAGE_SIZE)" "$(ORLIXOS_ENVIRONMENT_BASE_IMAGE)"; \
	"$(ORLIX_MKE2FS)" -q -t ext4 -F -m 0 -U clear -L ORLIXROOT -E root_owner=0:0 -d "$$base_tree" "$(ORLIXOS_ENVIRONMENT_BASE_IMAGE)"; \
	{ \
		for path in /dev /proc /root /run /sys /tmp; do \
			printf 'set_inode_field %s uid 0\n' "$$path"; \
			printf 'set_inode_field %s gid 0\n' "$$path"; \
		done; \
	} > "$$base_debugfs_commands"; \
	"$(ORLIX_DEBUGFS)" -w -f "$$base_debugfs_commands" "$(ORLIXOS_ENVIRONMENT_BASE_IMAGE)" >/dev/null; \
	if [ -n "$(ORLIXOS_ENVIRONMENT_BASE_DEBUGFS_COMMANDS)" ]; then \
		[ -f "$(ORLIXOS_ENVIRONMENT_BASE_DEBUGFS_COMMANDS)" ] || { echo "missing ORLIXOS_ENVIRONMENT_BASE_DEBUGFS_COMMANDS: $(ORLIXOS_ENVIRONMENT_BASE_DEBUGFS_COMMANDS)" >&2; exit 1; }; \
		"$(ORLIX_DEBUGFS)" -w -f "$(ORLIXOS_ENVIRONMENT_BASE_DEBUGFS_COMMANDS)" "$(ORLIXOS_ENVIRONMENT_BASE_IMAGE)" >/dev/null; \
	fi; \
	mkdir -p "$$state_tree/upper" "$$state_tree/work"; \
	chmod 0755 "$$state_tree" "$$state_tree/upper" "$$state_tree/work"; \
	truncate -s "$(ORLIXOS_ENVIRONMENT_STATE_IMAGE_SIZE)" "$(ORLIXOS_ENVIRONMENT_STATE_IMAGE)"; \
	"$(ORLIX_MKE2FS)" -q -t ext4 -F -m 0 -U clear -L ORLIXSTATE -E root_owner=0:0 -d "$$state_tree" "$(ORLIXOS_ENVIRONMENT_STATE_IMAGE)"; \
	{ \
		for path in /upper /work; do \
			printf 'set_inode_field %s uid 0\n' "$$path"; \
			printf 'set_inode_field %s gid 0\n' "$$path"; \
			printf 'set_inode_field %s mode 040755\n' "$$path"; \
		done; \
	} > "$$state_debugfs_commands"; \
	"$(ORLIX_DEBUGFS)" -w -f "$$state_debugfs_commands" "$(ORLIXOS_ENVIRONMENT_STATE_IMAGE)" >/dev/null; \
	if [ -n "$(ORLIXOS_ENVIRONMENT_STATE_DEBUGFS_COMMANDS)" ]; then \
		[ -f "$(ORLIXOS_ENVIRONMENT_STATE_DEBUGFS_COMMANDS)" ] || { echo "missing ORLIXOS_ENVIRONMENT_STATE_DEBUGFS_COMMANDS: $(ORLIXOS_ENVIRONMENT_STATE_DEBUGFS_COMMANDS)" >&2; exit 1; }; \
		"$(ORLIX_DEBUGFS)" -w -f "$(ORLIXOS_ENVIRONMENT_STATE_DEBUGFS_COMMANDS)" "$(ORLIXOS_ENVIRONMENT_STATE_IMAGE)" >/dev/null; \
	fi; \
	printf 'materialized Orlix environment root images: %s %s\n' "$(ORLIXOS_ENVIRONMENT_BASE_IMAGE)" "$(ORLIXOS_ENVIRONMENT_STATE_IMAGE)"

environment-runtime-proof-fixtures:
	@set -euo pipefail; \
	proof_root="$(ORLIXOS_BUILD_ROOT)/environment-runtime-proof"; \
	stamp="$(ORLIXOS_ENVIRONMENT_RUNTIME_PROOF_STAMP)"; \
	fixtures_ready=1; \
	for fixture in tar-imported oci-imported; do \
		if [ ! -s "$$proof_root/$$fixture/.ready" ]; then \
			fixtures_ready=0; \
		fi; \
	done; \
	if [ "$$fixtures_ready" -eq 1 ] && [ -s "$$stamp" ] && [ -s "$(ORLIXOS_INIT_BINARY)" ] && [ -s "$(ORLIXOS_ROOTFS_PROOF)" ]; then \
		init_sha256="$$(shasum -a 256 "$(ORLIXOS_INIT_BINARY)" | awk '{print $$1}')"; \
		rootfs_proof_sha256="$$(shasum -a 256 "$(ORLIXOS_ROOTFS_PROOF)" | awk '{print $$1}')"; \
		if grep -F -q "profile=$(PROFILE)" "$$stamp" && \
			grep -F -q "init_sha256=$$init_sha256" "$$stamp" && \
			grep -F -q "rootfs_proof_sha256=$$rootfs_proof_sha256" "$$stamp" && \
			[ ! "$(ORLIXOS_INIT_SOURCE)" -nt "$$stamp" ] && \
			[ ! "$(ORLIXOS_MANIFEST)" -nt "$$stamp" ] && \
			[ ! "$(PROJECT_DIR)/Makefile" -nt "$$stamp" ] && \
			[ ! "$(PROJECT_DIR)/Sources/make/rootfs.mk" -nt "$$stamp" ]; then \
			printf 'Orlix environment runtime proof fixtures ready: %s\n' "$$proof_root"; \
			exit 0; \
		fi; \
	fi; \
	rm -f "$$stamp"; \
	$(MAKE) -f "$(PROJECT_DIR)/Makefile" "$(ORLIXOS_ENVIRONMENT_RUNTIME_PROOF_STAMP)" PROFILE="$(PROFILE)"

$(ORLIXOS_ENVIRONMENT_RUNTIME_PROOF_STAMP): $(ORLIXOS_ROOTFS_PROOF) $(ORLIXOS_INIT_BINARY) $(PROJECT_DIR)/Makefile $(PROJECT_DIR)/Sources/make/rootfs.mk
	@set -euo pipefail; \
	init_sha256="$$(shasum -a 256 "$(ORLIXOS_INIT_BINARY)" | awk '{print $$1}')"; \
	rootfs_proof_sha256="$$(shasum -a 256 "$(ORLIXOS_ROOTFS_PROOF)" | awk '{print $$1}')"; \
	proof_root="$(ORLIXOS_BUILD_ROOT)/environment-runtime-proof"; \
	for fixture in tar-imported oci-imported; do \
		case "$$fixture" in \
			tar-imported) \
				env_id=tar-imported-runtime-proof; \
				name='Orlix Tar Runtime Proof'; \
				id=orlix-tar-runtime-proof; \
				;; \
			oci-imported) \
				env_id=oci-imported-runtime-proof; \
				name='Orlix OCI Runtime Proof'; \
				id=orlix-oci-runtime-proof; \
				;; \
			*) \
				echo "unknown environment runtime proof fixture: $$fixture" >&2; \
				exit 1; \
				;; \
		esac; \
		fixture_root="$$proof_root/$$fixture"; \
		imported_root="$$fixture_root/imported-root"; \
		state_root="$$fixture_root/state/environments/$$env_id"; \
		work_root="$$fixture_root/work/image-materialization"; \
		base_metadata_commands="$$work_root/base-debugfs-metadata.commands"; \
		state_metadata_commands="$$work_root/state-debugfs-metadata.commands"; \
		case "$$fixture_root" in "$(ORLIXOS_BUILD_ROOT)"/environment-runtime-proof/*) ;; *) echo "refusing unsafe environment runtime proof root: $$fixture_root" >&2; exit 1 ;; esac; \
		rm -rf "$$fixture_root"; \
		mkdir -p "$$imported_root" "$$state_root" "$$work_root"; \
		(cd "$(ORLIXOS_BASE_ROOT_TREE)" && tar -cf - .) | (cd "$$imported_root" && tar -xf -); \
		printf '%s\n' "NAME=$$name" "ID=$$id" "PRETTY_NAME=$$name" > "$$imported_root/etc/os-release"; \
		find "$$imported_root" -type d -exec chmod 0755 {} +; \
		find "$$imported_root" -type f -perm -0100 -exec chmod 0755 {} +; \
		find "$$imported_root" -type f ! -perm -0100 -exec chmod 0644 {} +; \
		find "$$imported_root" -type l -exec chmod -h 0777 {} +; \
		[ ! -d "$$imported_root/root" ] || chmod 0700 "$$imported_root/root"; \
		[ ! -d "$$imported_root/tmp" ] || chmod 1777 "$$imported_root/tmp"; \
		: > "$$base_metadata_commands"; \
		(cd "$$imported_root" && find . -mindepth 1 -type d -print) | while IFS= read -r path; do \
			image_path="/$${path#./}"; \
			printf 'set_inode_field "%s" uid 0\n' "$$image_path" >> "$$base_metadata_commands"; \
			printf 'set_inode_field "%s" gid 0\n' "$$image_path" >> "$$base_metadata_commands"; \
			if [ "$$image_path" = /root ]; then \
				printf 'set_inode_field "%s" mode 040700\n' "$$image_path" >> "$$base_metadata_commands"; \
			elif [ "$$image_path" = /tmp ]; then \
				printf 'set_inode_field "%s" mode 041777\n' "$$image_path" >> "$$base_metadata_commands"; \
			else \
				printf 'set_inode_field "%s" mode 040755\n' "$$image_path" >> "$$base_metadata_commands"; \
			fi; \
		done; \
		(cd "$$imported_root" && find . -mindepth 1 -type f -print) | while IFS= read -r path; do \
			image_path="/$${path#./}"; \
			printf 'set_inode_field "%s" uid 0\n' "$$image_path" >> "$$base_metadata_commands"; \
			printf 'set_inode_field "%s" gid 0\n' "$$image_path" >> "$$base_metadata_commands"; \
			if [ -x "$$imported_root/$${path#./}" ]; then \
				printf 'set_inode_field "%s" mode 0100755\n' "$$image_path" >> "$$base_metadata_commands"; \
			else \
				printf 'set_inode_field "%s" mode 0100644\n' "$$image_path" >> "$$base_metadata_commands"; \
			fi; \
		done; \
		(cd "$$imported_root" && find . -mindepth 1 -type l -print) | while IFS= read -r path; do \
			image_path="/$${path#./}"; \
			printf 'set_inode_field "%s" uid 0\n' "$$image_path" >> "$$base_metadata_commands"; \
			printf 'set_inode_field "%s" gid 0\n' "$$image_path" >> "$$base_metadata_commands"; \
			printf 'set_inode_field "%s" mode 0120777\n' "$$image_path" >> "$$base_metadata_commands"; \
		done; \
		{ \
			for path in /upper /work; do \
				printf 'set_inode_field %s uid 0\n' "$$path"; \
				printf 'set_inode_field %s gid 0\n' "$$path"; \
				printf 'set_inode_field %s mode 040755\n' "$$path"; \
			done; \
		} > "$$state_metadata_commands"; \
		$(MAKE) -f "$(PROJECT_DIR)/Makefile" environment-root-image \
			PROFILE="$(PROFILE)" \
			ORLIXOS_ENVIRONMENT_STAGING_ROOT="$$imported_root" \
			ORLIXOS_ENVIRONMENT_BASE_IMAGE="$$state_root/base.ext4" \
			ORLIXOS_ENVIRONMENT_STATE_IMAGE="$$state_root/state.ext4" \
			ORLIXOS_ENVIRONMENT_IMAGE_WORK_DIR="$$work_root" \
			ORLIXOS_ENVIRONMENT_BASE_DEBUGFS_COMMANDS="$$base_metadata_commands" \
			ORLIXOS_ENVIRONMENT_STATE_DEBUGFS_COMMANDS="$$state_metadata_commands"; \
		printf 'profile=%s\nfixture=%s\nenvironment=%s\ninit_sha256=%s\n' "$(PROFILE)" "$$fixture" "$$env_id" "$$init_sha256" > "$$fixture_root/.ready"; \
	done; \
	printf 'profile=%s\ninit_sha256=%s\nrootfs_proof_sha256=%s\n' "$(PROFILE)" "$$init_sha256" "$$rootfs_proof_sha256" > "$@"

include $(PROJECT_DIR)/Sources/make/rules.mk
