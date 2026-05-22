SHELL := /bin/bash
.DEFAULT_GOAL := all

empty :=
space := $(empty) $(empty)
comma := ,

LINUX_VERSION ?= 6.12
LINUX_ARCH ?= orlix
LINUX_TAG ?= v$(LINUX_VERSION)
LINUX_REMOTE ?= https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git

PROFILE ?= appstore
ORLIX_PROFILES := appstore development

type ?= kunit
libc ?= linux

LINUX_UPSTREAM_DIR ?= OrlixKernel/Sources/upstream/linux-$(LINUX_VERSION)
LINUX_CLONE_CACHE_DIR ?= .cache/linux-clone
LINUX_CLONE_CACHE_REPO := $(CURDIR)/$(LINUX_CLONE_CACHE_DIR)/linux-$(LINUX_VERSION).git

ORLIX_LINUX_OVERLAY ?= OrlixKernel/Sources/ports/orlix/overlay
ORLIX_LINUX_PATCH_DIR ?= OrlixKernel/Sources/ports/orlix/patches
override ORLIX_PROFILE_CONFIG := OrlixKernel/Sources/ports/orlix/configs/$(PROFILE)_defconfig

ORLIX_KERNEL_PORT_DIR ?= Build/OrlixKernel/linux-$(LINUX_VERSION)-port
ORLIX_KERNEL_BUILD_ROOT := $(CURDIR)/Build/OrlixKernel/build
ORLIX_KERNEL_BUILD_DIR := $(ORLIX_KERNEL_BUILD_ROOT)/$(PROFILE)
ORLIX_KERNEL_ARCHIVE_ROOT := $(CURDIR)/Build/OrlixKernel/$(PROFILE)
ORLIX_KERNEL_ARCHIVE_MANIFEST := $(ORLIX_KERNEL_ARCHIVE_ROOT)/linux-object-manifest.txt
ORLIX_KERNEL_DEVICE_ARCHIVE_DIR := $(ORLIX_KERNEL_ARCHIVE_ROOT)/iphoneos
ORLIX_KERNEL_SIMULATOR_ARCHIVE_DIR := $(ORLIX_KERNEL_ARCHIVE_ROOT)/iphonesimulator
ORLIX_KERNEL_ARCHIVE_NAME := OrlixKernel.a
ORLIX_IOS_TARGET := arm64-apple-ios
ORLIX_IOS_SIMULATOR_TARGET := arm64-apple-ios-simulator
ORLIX_KERNEL_LINUX_SOURCES := \
	arch/$(LINUX_ARCH)/boot/boot.c \
	arch/$(LINUX_ARCH)/kernel/irq.c \
	arch/$(LINUX_ARCH)/kernel/process.c \
	arch/$(LINUX_ARCH)/kernel/ptrace.c \
	arch/$(LINUX_ARCH)/kernel/reboot.c \
	arch/$(LINUX_ARCH)/kernel/setup.c \
	arch/$(LINUX_ARCH)/kernel/time.c \
	arch/$(LINUX_ARCH)/kernel/traps.c \
	arch/$(LINUX_ARCH)/mm/delay.c \
	init/version.c \
	init/main.c \
	init/init_task.c \
	init/calibrate.c \
	init/initramfs.c \
	kernel/exec_domain.c \
	kernel/fork.c \
	kernel/async.c \
	kernel/capability.c \
	kernel/cred.c \
	kernel/cpu.c \
	kernel/exit.c \
	kernel/irq_work.c \
	kernel/irq/irqdesc.c \
	kernel/irq/handle.c \
	kernel/irq/manage.c \
	kernel/irq/spurious.c \
	kernel/irq/resend.c \
	kernel/irq/chip.c \
	kernel/irq/dummychip.c \
	kernel/irq/devres.c \
	kernel/irq/proc.c \
	kernel/kthread.c \
	kernel/notifier.c \
	kernel/nsproxy.c \
	kernel/resource.c \
	kernel/sysctl.c \
	kernel/ptrace.c \
	kernel/groups.c \
	kernel/extable.c \
	kernel/params.c \
	kernel/panic.c \
	kernel/pid.c \
	kernel/pid_namespace.c \
	kernel/signal.c \
	kernel/sys.c \
	kernel/sys_ni.c \
	kernel/locking/mutex.c \
	kernel/locking/semaphore.c \
	kernel/locking/rwsem.c \
	kernel/locking/percpu-rwsem.c \
	kernel/locking/rtmutex_api.c \
	kernel/printk/printk.c \
	kernel/printk/printk_safe.c \
	kernel/printk/nbcon.c \
	kernel/printk/printk_ringbuffer.c \
	kernel/printk/sysctl.c \
	kernel/sched/core.c \
	kernel/sched/fair.c \
	kernel/sched/build_policy.c \
	kernel/sched/build_utility.c \
	kernel/softirq.c \
	kernel/task_work.c \
	kernel/umh.c \
	kernel/ucount.c \
	kernel/user.c \
	kernel/user_namespace.c \
	kernel/utsname.c \
	kernel/workqueue.c \
	kernel/cgroup/cgroup.c \
	kernel/cgroup/cgroup-v1.c \
	kernel/cgroup/freezer.c \
	kernel/cgroup/namespace.c \
	kernel/cgroup/rstat.c \
	kernel/rcu/sync.c \
	kernel/rcu/srcutiny.c \
	kernel/rcu/tiny.c \
	kernel/rcu/update.c \
	kernel/kallsyms.c \
	kernel/ksysfs.c \
	kernel/reboot.c \
	kernel/range.c \
	kernel/smpboot.c \
	kernel/regset.c \
	kernel/ksyms_common.c \
	kernel/up.c \
	security/commoncap.c \
	lib/is_single_threaded.c \
	lib/kobject_uevent.c \
	lib/kobject.c \
	lib/klist.c \
	lib/string.c \
	lib/string_helpers.c \
	lib/strncpy_from_user.c \
	lib/strnlen_user.c \
	lib/ratelimit.c \
	lib/dump_stack.c \
	lib/vsprintf.c \
	lib/ctype.c \
	lib/cmdline.c \
	lib/kstrtox.c \
	lib/bitmap.c \
	lib/bitmap-str.c \
	lib/find_bit.c \
	lib/hweight.c \
	lib/hexdump.c \
	lib/uuid.c \
	lib/iov_iter.c \
	lib/scatterlist.c \
	lib/timerqueue.c \
	lib/maple_tree.c \
	lib/idr.c \
	lib/extable.c \
	lib/irq_regs.c \
	lib/argv_split.c \
	lib/list_sort.c \
	lib/radix-tree.c \
	lib/rbtree.c \
	lib/xarray.c \
	lib/errname.c \
	lib/bust_spinlocks.c \
	lib/kasprintf.c \
	lib/llist.c \
	lib/percpu-refcount.c \
	lib/refcount.c \
	lib/errseq.c \
	lib/crc32.c \
	lib/dec_and_lock.c \
	lib/debug_locks.c \
	lib/crypto/chacha.c \
	lib/crypto/blake2s.c \
	lib/crypto/blake2s-generic.c \
	lib/math/div64.c \
	lib/math/gcd.c \
	lib/math/lcm.c \
	lib/math/int_log.c \
	lib/math/int_pow.c \
	lib/math/int_sqrt.c \
	lib/math/reciprocal_div.c \
	lib/lockref.c \
	lib/logic_pio.c \
	lib/siphash.c \
	lib/plist.c \
	lib/seq_buf.c \
	lib/sort.c \
	lib/sbitmap.c \
	lib/flex_proportions.c \
	kernel/time/alarmtimer.c \
	kernel/time/clockevents.c \
	kernel/time/clocksource.c \
	kernel/time/hrtimer.c \
	kernel/time/itimer.c \
	kernel/time/jiffies.c \
	kernel/time/ntp.c \
	kernel/time/posix-clock.c \
	kernel/time/posix-cpu-timers.c \
	kernel/time/posix-timers.c \
	kernel/time/tick-common.c \
	kernel/time/time.c \
	kernel/time/timeconv.c \
	kernel/time/timecounter.c \
	kernel/time/timekeeping.c \
	kernel/time/timer.c \
	kernel/time/timer_list.c \
	mm/filemap.c \
	mm/mempool.c \
	mm/oom_kill.c \
	mm/fadvise.c \
	mm/maccess.c \
	mm/page-writeback.c \
	mm/folio-compat.c \
	mm/readahead.c \
	mm/swap.c \
	mm/truncate.c \
	mm/vmscan.c \
	mm/shrinker.c \
	mm/shmem.c \
	mm/util.c \
	mm/mmzone.c \
	mm/vmstat.c \
	mm/backing-dev.c \
	mm/mm_init.c \
	mm/percpu.c \
	mm/slab_common.c \
	mm/compaction.c \
	mm/show_mem.c \
	mm/interval_tree.c \
	mm/list_lru.c \
	mm/workingset.c \
	mm/debug.c \
	mm/gup.c \
	mm/mmap_lock.c \
	mm/highmem.c \
	mm/memory.c \
	mm/mincore.c \
	mm/mlock.c \
	mm/mmap.c \
	mm/mmu_gather.c \
	mm/mprotect.c \
	mm/mremap.c \
	mm/msync.c \
	mm/page_vma_mapped.c \
	mm/pagewalk.c \
	mm/pgtable-generic.c \
	mm/rmap.c \
	mm/vmalloc.c \
	mm/vma.c \
	mm/process_vm_access.c \
	mm/mseal.c \
	mm/page_alloc.c \
	mm/init-mm.c \
	mm/memblock.c \
	mm/memfd.c \
	mm/slub.c \
	mm/madvise.c \
	mm/page_io.c \
	mm/swap_state.c \
	mm/swapfile.c \
	mm/swap_slots.c \
	mm/dmapool.c \
	block/bdev.c \
	block/fops.c \
	block/bio.c \
	block/elevator.c \
	block/blk-core.c \
	block/blk-sysfs.c \
	block/blk-flush.c \
	block/blk-settings.c \
	block/blk-ioc.c \
	block/blk-map.c \
	block/blk-merge.c \
	block/blk-timeout.c \
	block/blk-lib.c \
	block/blk-mq.c \
	block/blk-mq-tag.c \
	block/blk-stat.c \
	block/blk-mq-sysfs.c \
	block/blk-mq-cpumap.c \
	block/blk-mq-sched.c \
	block/ioctl.c \
	block/genhd.c \
	block/ioprio.c \
	block/badblocks.c \
	block/partitions/core.c \
	block/partitions/msdos.c \
	block/partitions/efi.c \
	block/blk-rq-qos.c \
	block/disk-events.c \
	block/blk-ia-ranges.c \
	block/early-lookup.c \
	drivers/base/component.c \
	drivers/base/core.c \
	drivers/base/bus.c \
	drivers/base/dd.c \
	drivers/base/syscore.c \
	drivers/base/driver.c \
	drivers/base/class.c \
	drivers/base/platform.c \
	drivers/base/cpu.c \
	drivers/base/firmware.c \
	drivers/base/init.c \
	drivers/base/map.c \
	drivers/base/devres.c \
	drivers/base/attribute_container.c \
	drivers/base/transport_class.c \
	drivers/base/topology.c \
	drivers/base/container.c \
	drivers/base/property.c \
	drivers/base/cacheinfo.c \
	drivers/base/swnode.c \
	drivers/base/firmware_loader/main.c \
	drivers/base/firmware_loader/builtin/main.c \
	drivers/char/random.c \
	drivers/of/base.c \
	drivers/of/cpu.c \
	drivers/of/device.c \
	drivers/of/fdt.c \
	drivers/of/fdt_address.c \
	drivers/of/kobj.c \
	drivers/of/module.c \
	drivers/of/of_reserved_mem.c \
	drivers/of/platform.c \
	drivers/of/property.c \
	drivers/of/address.c \
	fs/anon_inodes.c \
	fs/attr.c \
	fs/bad_inode.c \
	fs/char_dev.c \
	fs/dcache.c \
	fs/d_path.c \
	fs/exec.c \
	fs/inode.c \
	fs/namespace.c \
	fs/namei.c \
	fs/file.c \
	fs/file_table.c \
	fs/fcntl.c \
	fs/fs_context.c \
	fs/fs_pin.c \
	fs/fs_parser.c \
	fs/fsopen.c \
	fs/fs_struct.c \
	fs/fs_types.c \
	fs/fs-writeback.c \
	fs/filesystems.c \
	fs/init.c \
	fs/ioctl.c \
	fs/kernel_read_file.c \
	fs/libfs.c \
	fs/locks.c \
	fs/mnt_idmapping.c \
	fs/nsfs.c \
	fs/open.c \
	fs/pipe.c \
	fs/pidfs.c \
	fs/pnode.c \
	fs/proc_namespace.c \
	fs/notify/dnotify/dnotify.c \
	fs/readdir.c \
	fs/read_write.c \
	fs/remap_range.c \
	fs/select.c \
	fs/splice.c \
	fs/stack.c \
	fs/stat.c \
	fs/statfs.c \
	fs/super.c \
	fs/sync.c \
	fs/utimes.c \
	fs/xattr.c \
	fs/buffer.c \
	fs/mpage.c \
	fs/eventpoll.c \
	fs/signalfd.c \
	fs/timerfd.c \
	fs/eventfd.c \
	fs/aio.c \
	fs/coredump.c \
	fs/drop_caches.c \
	fs/sysctls.c \
	fs/kernfs/dir.c \
	fs/kernfs/file.c \
	fs/kernfs/inode.c \
	fs/kernfs/mount.c \
	fs/kernfs/symlink.c \
	fs/sysfs/dir.c \
	fs/sysfs/file.c \
	fs/sysfs/group.c \
	fs/sysfs/mount.c \
	fs/sysfs/symlink.c \
	fs/proc/generic.c \
	fs/proc/inode.c \
	fs/proc/proc_sysctl.c \
	fs/proc/root.c \
	fs/proc/util.c \
	fs/seq_file.c \
	lib/fdt.c \
	lib/fdt_ro.c \
	lib/fdt_wip.c \
	lib/fdt_sw.c \
	lib/fdt_rw.c \
	lib/fdt_strerror.c \
	lib/fdt_empty_tree.c \
	lib/fdt_addresses.c
ORLIX_KUNIT_BUILD_DIR := $(CURDIR)/Build/OrlixKernel/kunit/$(PROFILE)
ORLIX_TEMPORARY_KSELFTEST_INSTALL_DIR := $(CURDIR)/Build/OrlixKernel/kselftest/temporary/$(PROFILE)
ORLIX_TEMPORARY_TEST_INITRAMFS_DIR := $(CURDIR)/Build/OrlixKernel/test-initramfs/temporary/$(PROFILE)/OrlixTestInitramfs.bundle
ORLIX_MLIBC_KERNEL_HEADERS_DIR := $(CURDIR)/Build/OrlixMLibC/kernel-headers/$(PROFILE)
ORLIX_MLIBC_SYSROOT ?= Build/OrlixMLibC/sysroot/$(PROFILE)
ORLIX_MLIBC_KSELFTEST_INSTALL_DIR := $(CURDIR)/Build/OrlixMLibC/kselftest/$(PROFILE)
ORLIX_MLIBC_TEST_INITRAMFS_DIR := $(CURDIR)/Build/OrlixMLibC/test-initramfs/$(PROFILE)/OrlixTestInitramfs.bundle

ORLIX_LINUX_USERSPACE_SYSROOT_BOOTSTRAP_DIR ?= Build/OrlixKernel/linux-userspace-sysroot/aarch64
ORLIX_LINUX_USERSPACE_SYSROOT ?= $(ORLIX_LINUX_USERSPACE_SYSROOT_BOOTSTRAP_DIR)
ORLIX_KSELFTEST_ARCH ?= arm64

ORLIX_XCODE_PROJECT ?= OrlixSystem.xcodeproj
ORLIX_IOS_SIMULATOR_NAME ?= iPhone 17 Pro
ORLIX_IOS_SIMULATOR_ID ?=
ORLIX_IOS_SIMULATOR_DERIVED_DATA ?= $(CURDIR)/.deriveddata/OrlixSystem-sim
ORLIX_IOS_SIMULATOR_FRAMEWORK := $(ORLIX_IOS_SIMULATOR_DERIVED_DATA)/Build/Products/Debug-iphonesimulator/OrlixKernel.framework
ORLIX_KERNEL_PAYLOAD_DIR := $(CURDIR)/Build/OrlixKernel/payload/OrlixKernelPayload.bundle
ORLIX_KERNEL_XCFRAMEWORK ?= $(CURDIR)/Build/OrlixKernel/xcframework/OrlixKernel.xcframework
XCODEGEN ?= xcodegen
XCODEBUILD_MCP ?= xcodebuildmcp

LINUX_MAKE ?=
LINUX_SED ?=
LINUX_LLVM_BIN ?= $(shell if command -v llvm-ar >/dev/null 2>&1; then dirname "$$(command -v llvm-ar)"; elif [ -x /opt/homebrew/opt/llvm/bin/llvm-ar ]; then printf '%s\n' /opt/homebrew/opt/llvm/bin; fi)
LINUX_HOST_COMPAT_INCLUDE_ROOT := $(CURDIR)/tools/linux_host_compat/include
ORLIX_KERNEL_CC ?= clang
ORLIX_KERNEL_AR ?= llvm-ar
ORLIX_KERNEL_NM ?= nm
ORLIX_KERNEL_OTOOL ?= otool

TEST_TYPES := $(strip $(subst $(comma),$(space),$(type)))

ifeq ($(libc),linux)
KSELFTEST_SYSROOT := $(ORLIX_LINUX_USERSPACE_SYSROOT)
KSELFTEST_INSTALL_DIR := $(ORLIX_TEMPORARY_KSELFTEST_INSTALL_DIR)
KSELFTEST_INITRAMFS_DIR := $(ORLIX_TEMPORARY_TEST_INITRAMFS_DIR)
KSELFTEST_HEADER_FLAGS :=
KSELFTEST_PROOF_LABEL := temporary-kselftest-kernel-interface
ifeq ($(ORLIX_LINUX_USERSPACE_SYSROOT),$(ORLIX_LINUX_USERSPACE_SYSROOT_BOOTSTRAP_DIR))
KSELFTEST_PREREQS := __linux-userspace-sysroot prepare
else
KSELFTEST_PREREQS := prepare
endif
else ifeq ($(libc),orlixmlibc)
KSELFTEST_SYSROOT := $(ORLIX_MLIBC_SYSROOT)
KSELFTEST_INSTALL_DIR := $(ORLIX_MLIBC_KSELFTEST_INSTALL_DIR)
KSELFTEST_INITRAMFS_DIR := $(ORLIX_MLIBC_TEST_INITRAMFS_DIR)
KSELFTEST_HEADER_FLAGS := -isystem $(ORLIX_MLIBC_KERNEL_HEADERS_DIR)/include
KSELFTEST_PROOF_LABEL := orlixmlibc-kselftest-syscall-uapi
KSELFTEST_PREREQS := headers_install
else
KSELFTEST_SYSROOT :=
KSELFTEST_INSTALL_DIR :=
KSELFTEST_INITRAMFS_DIR :=
KSELFTEST_HEADER_FLAGS :=
KSELFTEST_PROOF_LABEL :=
KSELFTEST_PREREQS :=
endif

include OrlixKernel/Sources/ports/orlix/kbuild/product-compile-adapter.mk

.PHONY: all setup-env build test clean mrproper help prepare scripts dtbs headers_install kunit kselftest kselftest-install xcodeproj run __bootstrap-linux-upstream __validate-profile __prepare-port __prepare-kbuild __headers-install __kunit __kernel-archive __verify-xcodegen-boundary __verify-framework-symbols __linux-userspace-sysroot __kselftest-install __kselftest-initramfs __kernel-payload __ios-simulator-framework __ios-simulator-xcframework

all: build

help:
	@printf '%s\n' 'Targets:'
	@printf '%s\n' '  setup-env                 fetch upstream Linux and generate the Xcode project'
	@printf '%s\n' '  build                     build the app-hosted OrlixKernel iOS artifact'
	@printf '%s\n' '  test                      run test type(s), default: type=kunit'
	@printf '%s\n' '  test type=kunit           build Linux KUnit-selected Orlix tests'
	@printf '%s\n' '  test type=kunit,kselftest build KUnit and Linux kselftest artifacts'
	@printf '%s\n' '  kselftest libc=linux      install and package temporary Linux-libc kselftests'
	@printf '%s\n' '  kselftest libc=orlixmlibc install and package OrlixMLibC kselftests'
	@printf '%s\n' '  headers_install           install Linux UAPI headers for OrlixMLibC'
	@printf '%s\n' '  clean                     remove normal generated outputs'
	@printf '%s\n' '  mrproper                  remove all generated local outputs'

setup-env: __bootstrap-linux-upstream xcodeproj

build: __ios-simulator-xcframework

prepare scripts dtbs: __prepare-kbuild

headers_install: __headers-install

kunit: __kunit

kselftest-install: $(KSELFTEST_PREREQS) __kselftest-install

kselftest: kselftest-install __kselftest-initramfs

test:
	@set -euo pipefail; \
	if [ -z "$(TEST_TYPES)" ]; then \
		echo "type must name at least one test class: kunit,kselftest" >&2; \
		exit 1; \
	fi; \
	for selected in $(TEST_TYPES); do \
		case "$$selected" in \
			kunit) $(MAKE) kunit PROFILE="$(PROFILE)" ;; \
			kselftest) $(MAKE) kselftest PROFILE="$(PROFILE)" libc="$(libc)" ;; \
			*) echo "unsupported test type: $$selected (expected kunit or kselftest)" >&2; exit 1 ;; \
		esac; \
	done

xcodeproj:
	@set -euo pipefail; \
	command -v "$(XCODEGEN)" >/dev/null 2>&1 || { echo "XcodeGen is required; install xcodegen or set XCODEGEN=/path/to/xcodegen" >&2; exit 1; }; \
	"$(XCODEGEN)" generate --spec project.yml; \
	$(MAKE) -f OrlixKernel/Makefile __verify-xcodegen-boundary

run: __kernel-payload
	@set -euo pipefail; \
	$(MAKE) xcodeproj; \
	command -v "$(XCODEBUILD_MCP)" >/dev/null 2>&1 || { echo "XcodeBuildMCP is required; install xcodebuildmcp or set XCODEBUILD_MCP=/path/to/xcodebuildmcp" >&2; exit 1; }; \
	selector=(); \
	if [ -n "$(ORLIX_IOS_SIMULATOR_ID)" ]; then \
		selector=(--simulator-id "$(ORLIX_IOS_SIMULATOR_ID)"); \
	else \
		selector=(--simulator-name "$(ORLIX_IOS_SIMULATOR_NAME)" --use-latest-os); \
	fi; \
	ORLIX_PROFILE="$(PROFILE)" "$(XCODEBUILD_MCP)" simulator build-and-run \
		--project-path "$(CURDIR)/$(ORLIX_XCODE_PROJECT)" \
		--scheme "OrlixTerminal" \
		--configuration "Debug" \
		--derived-data-path "$(ORLIX_IOS_SIMULATOR_DERIVED_DATA)" \
		"$${selector[@]}" \
		--output json

clean:
	@set -euo pipefail; \
	port_dir="$(ORLIX_KERNEL_PORT_DIR)"; \
	expected_port_dir="Build/OrlixKernel/linux-$(LINUX_VERSION)-port"; \
	if [ "$$port_dir" != "$$expected_port_dir" ]; then \
		echo "Orlix kernel port tree must be $$expected_port_dir: $$port_dir" >&2; \
		exit 1; \
	fi; \
	for path in \
		"$$port_dir" \
		Build/OrlixKernel/build \
		Build/OrlixKernel/kunit \
		Build/OrlixKernel/kselftest \
		Build/OrlixKernel/appstore \
		Build/OrlixKernel/development \
		Build/OrlixKernel/payload \
		Build/OrlixKernel/test-initramfs \
		Build/OrlixKernel/tool-shims \
		Build/OrlixKernel/xcframework \
		Build/OrlixMLibC/kernel-headers \
		Build/OrlixMLibC/kselftest \
		Build/OrlixMLibC/test-initramfs \
		.deriveddata/OrlixSystem-sim; do \
		if [ -L "$$path" ]; then echo "refusing to clean symlinked path: $$path" >&2; exit 1; fi; \
		rm -rf "$$path"; \
	done; \
	echo "cleaned generated build outputs"

mrproper: clean
	@set -euo pipefail; \
	if [ -L .cache ]; then echo "refusing to remove Linux clone cache through symlinked parent: .cache" >&2; exit 1; fi; \
	for path in Build .deriveddata OrlixSystem.xcodeproj OrlixKernel/Sources/upstream .cache/linux-clone; do \
		if [ -L "$$path" ]; then echo "refusing to remove symlinked path: $$path" >&2; exit 1; fi; \
		rm -rf "$$path"; \
	done; \
	echo "removed generated local outputs"

__bootstrap-linux-upstream:
	@set -euo pipefail; \
	linux_remote="$(LINUX_REMOTE)"; \
	linux_tag="$(LINUX_TAG)"; \
	upstream_dir="$(LINUX_UPSTREAM_DIR)"; \
	clone_cache="$(LINUX_CLONE_CACHE_REPO)"; \
	expected_upstream_dir="OrlixKernel/Sources/upstream/linux-$(LINUX_VERSION)"; \
	expected_clone_cache="$(CURDIR)/.cache/linux-clone/linux-$(LINUX_VERSION).git"; \
	if [ "$$upstream_dir" != "$$expected_upstream_dir" ]; then \
		echo "Linux upstream directory must be $$expected_upstream_dir: $$upstream_dir" >&2; \
		exit 1; \
	fi; \
	if [ "$$clone_cache" != "$$expected_clone_cache" ]; then \
		echo "Linux clone cache repo must be $$expected_clone_cache: $$clone_cache" >&2; \
		exit 1; \
	fi; \
	for path in Build OrlixKernel/Sources OrlixKernel/Sources/upstream .cache .cache/linux-clone "$$upstream_dir" "$$clone_cache"; do \
		if [ -L "$$path" ]; then echo "refusing to use symlinked path: $$path" >&2; exit 1; fi; \
	done; \
	if [ -d "$$upstream_dir/.git" ]; then \
		checked_tag="$$(git -C "$$upstream_dir" describe --tags --exact-match HEAD 2>/dev/null || true)"; \
		if [ "$$checked_tag" = "$$linux_tag" ]; then \
			echo "upstream Linux ready: $$upstream_dir ($$checked_tag)"; \
			exit 0; \
		fi; \
	fi; \
	mkdir -p "$$(dirname "$$upstream_dir")" "$$(dirname "$$clone_cache")"; \
	if [ ! -d "$$clone_cache/objects" ]; then \
		rm -rf "$$clone_cache"; \
		git init --bare "$$clone_cache" >/dev/null; \
		git -C "$$clone_cache" remote add origin "$$linux_remote"; \
	else \
		git -C "$$clone_cache" remote set-url origin "$$linux_remote"; \
	fi; \
	git -C "$$clone_cache" fetch --force --depth 1 origin "refs/tags/$$linux_tag:refs/tags/$$linux_tag"; \
	rm -rf "$$upstream_dir"; \
	git clone --shared --no-checkout --origin origin "$$clone_cache" "$$upstream_dir"; \
	git -C "$$upstream_dir" -c advice.detachedHead=false checkout --quiet --force --detach "$$linux_tag"; \
	checked_tag="$$(git -C "$$upstream_dir" describe --tags --exact-match HEAD)"; \
	checked_commit="$$(git -C "$$upstream_dir" rev-parse HEAD)"; \
	tag_commit="$$(git -C "$$clone_cache" rev-list -n1 "$$linux_tag")"; \
	if [ "$$checked_tag" != "$$linux_tag" ] || [ "$$checked_commit" != "$$tag_commit" ]; then \
		echo "expected $$linux_tag at $$tag_commit but checked out $$checked_tag at $$checked_commit" >&2; \
		exit 1; \
	fi; \
	echo "upstream Linux ready: $$upstream_dir ($$checked_tag, clone cache $$clone_cache)"

__validate-profile:
	@set -euo pipefail; \
	profile="$(PROFILE)"; \
	case " $(ORLIX_PROFILES) " in \
		*" $$profile "*) ;; \
		*) echo "unsupported PROFILE=$$profile (expected one of: $(ORLIX_PROFILES))" >&2; exit 1 ;; \
	esac; \
	config="$(ORLIX_PROFILE_CONFIG)"; \
	[ -f "$$config" ] || { echo "missing profile defconfig: $$config" >&2; exit 1; }

__prepare-port: __validate-profile __bootstrap-linux-upstream
	@set -euo pipefail; \
	upstream_dir="$(LINUX_UPSTREAM_DIR)"; \
	port_dir="$(ORLIX_KERNEL_PORT_DIR)"; \
	expected_port_dir="Build/OrlixKernel/linux-$(LINUX_VERSION)-port"; \
	overlay_dir="$(ORLIX_LINUX_OVERLAY)"; \
	patch_dir="$(ORLIX_LINUX_PATCH_DIR)"; \
	profile_config="$(ORLIX_PROFILE_CONFIG)"; \
	exception_dir="$$patch_dir/exceptions"; \
	forbidden_re='^(fs|kernel|mm|ipc|net|include/linux|include/uapi)(/|$$)'; \
	if [ "$$port_dir" != "$$expected_port_dir" ]; then \
		echo "Orlix kernel port tree must be $$expected_port_dir: $$port_dir" >&2; \
		exit 1; \
	fi; \
	if [ "$$port_dir" = "$$upstream_dir" ]; then \
		echo "Orlix kernel port tree must not equal upstream directory: $$port_dir" >&2; \
		exit 1; \
	fi; \
	for path in Build Build/OrlixKernel "$$port_dir"; do \
		if [ -L "$$path" ]; then echo "refusing to use symlinked path: $$path" >&2; exit 1; fi; \
	done; \
	[ -d "$$overlay_dir" ] || { echo "missing Linux overlay directory: $$overlay_dir" >&2; exit 1; }; \
	rm -rf "$$port_dir"; \
	mkdir -p "$$port_dir"; \
	git -C "$$upstream_dir" archive --format=tar HEAD | tar -x -C "$$port_dir"; \
	cp -R "$$overlay_dir/." "$$port_dir"; \
	mkdir -p "$$port_dir/arch/$(LINUX_ARCH)/configs"; \
	cp "$$profile_config" "$$port_dir/arch/$(LINUX_ARCH)/configs/defconfig"; \
	if [ -d "$$patch_dir" ]; then \
		for patch in "$$patch_dir"/*.patch "$$patch_dir"/*.diff; do \
			[ -e "$$patch" ] || continue; \
			patch_name="$$(basename "$$patch")"; \
			case "$$patch" in \
				/*) patch_abs="$$patch" ;; \
				*) patch_abs="$(CURDIR)/$$patch" ;; \
			esac; \
			if awk -v re="$$forbidden_re" '/^\+\+\+ b\// { path = substr($$2, 3); if (path ~ re) bad = 1 } END { exit bad ? 0 : 1 }' "$$patch_abs"; then \
				if [ ! -f "$$exception_dir/$$patch_name.md" ]; then \
					echo "patch $$patch_name touches a forbidden upstream path; add $$exception_dir/$$patch_name.md" >&2; \
					exit 1; \
				fi; \
			fi; \
			patch -d "$$port_dir" -p1 < "$$patch_abs" >/dev/null; \
		done; \
	fi; \
	echo "prepared Orlix kernel port tree: $$port_dir (profile $(PROFILE))"

__prepare-kbuild: __prepare-port
	@set -euo pipefail; \
	linux_make="$(LINUX_MAKE)"; \
	if [ -z "$$linux_make" ]; then linux_make="$$(command -v gmake || true)"; fi; \
	if [ -z "$$linux_make" ]; then \
		echo "GNU Make >= 4.0 is required by Linux Kbuild; install gmake or set LINUX_MAKE=/path/to/gmake" >&2; \
		exit 1; \
	fi; \
	build_dir="$(ORLIX_KERNEL_BUILD_DIR)"; \
	expected_build_dir="$(CURDIR)/Build/OrlixKernel/build/$(PROFILE)"; \
	if [ "$$build_dir" != "$$expected_build_dir" ]; then \
		echo "Orlix kernel build directory must be $$expected_build_dir: $$build_dir" >&2; \
		exit 1; \
	fi; \
	for path in Build/OrlixKernel/build; do \
		if [ -e "$$path" ] && [ -L "$$path" ]; then echo "refusing to use symlinked path: $$path" >&2; exit 1; fi; \
	done; \
	linux_sed_dir=""; \
	if [ -n "$(LINUX_SED)" ]; then \
		linux_sed="$(LINUX_SED)"; \
		case "$$linux_sed" in /*) ;; *) linux_sed="$(CURDIR)/$$linux_sed" ;; esac; \
		[ -x "$$linux_sed" ] || { echo "GNU sed is required by Linux Kbuild on this host; LINUX_SED is not executable: $$linux_sed" >&2; exit 1; }; \
		sed_shim_dir="$(CURDIR)/Build/OrlixKernel/tool-shims/$(PROFILE)"; \
		if [ -e Build/OrlixKernel/tool-shims ] && [ -L Build/OrlixKernel/tool-shims ]; then echo "refusing to use symlinked Build/OrlixKernel/tool-shims directory" >&2; exit 1; fi; \
		if [ -e Build/OrlixKernel/tool-shims/$(PROFILE) ] && [ -L Build/OrlixKernel/tool-shims/$(PROFILE) ]; then echo "refusing to use symlinked Build/OrlixKernel/tool-shims/$(PROFILE) directory" >&2; exit 1; fi; \
		mkdir -p "$$sed_shim_dir"; \
		ln -sf "$$linux_sed" "$$sed_shim_dir/sed"; \
		linux_sed_dir="$$sed_shim_dir"; \
	elif [ -x /opt/homebrew/opt/gnu-sed/libexec/gnubin/sed ]; then linux_sed_dir=/opt/homebrew/opt/gnu-sed/libexec/gnubin; fi; \
	if [ -z "$$linux_sed_dir" ]; then \
		echo "GNU sed is required by Linux Kbuild on this host; install gnu-sed or set LINUX_SED=/path/to/gnu/sed" >&2; \
		exit 1; \
	fi; \
	linux_llvm_bin="$(LINUX_LLVM_BIN)"; \
	PATH="$$linux_sed_dir:$${linux_llvm_bin:+$$linux_llvm_bin:}$$PATH"; \
	export PATH; \
	sed --version >/dev/null 2>&1 || { echo "GNU sed is required by Linux Kbuild on this host" >&2; exit 1; }; \
	command -v llvm-ar >/dev/null 2>&1 || { echo "llvm-ar is required by Linux Kbuild; install LLVM or set LINUX_LLVM_BIN=/path/to/llvm/bin" >&2; exit 1; }; \
	rm -rf "$$build_dir"; \
	mkdir -p "$$build_dir"; \
	"$$linux_make" -C "$(ORLIX_KERNEL_PORT_DIR)" O="$$build_dir" ARCH="$(LINUX_ARCH)" LLVM=1 CLANG_TARGET_FLAGS=aarch64-linux-gnu HOSTCFLAGS="-I$(LINUX_HOST_COMPAT_INCLUDE_ROOT) -include linux_arm_elf_compat.h -D_UUID_T" mrproper defconfig prepare scripts dtbs arch/$(LINUX_ARCH)/kernel/vmlinux.lds drivers/of/empty_root.dtb.o usr/initramfs_data.o lib/crc32.o; \
	for dtb in appstore development; do \
		[ -f "$$build_dir/arch/$(LINUX_ARCH)/boot/dts/$$dtb.dtb" ] || { echo "missing profile DTB: $$build_dir/arch/$(LINUX_ARCH)/boot/dts/$$dtb.dtb" >&2; exit 1; }; \
	done; \
	[ -s "$$build_dir/drivers/of/empty_root.dtb" ] || { echo "missing generated empty-root DTB: $$build_dir/drivers/of/empty_root.dtb" >&2; exit 1; }; \
	[ -s "$$build_dir/usr/initramfs_inc_data" ] || { echo "missing generated initramfs input: $$build_dir/usr/initramfs_inc_data" >&2; exit 1; }; \
	linker_script="$$build_dir/arch/$(LINUX_ARCH)/kernel/vmlinux.lds"; \
	[ -s "$$linker_script" ] || { echo "missing generated Orlix Kbuild linker script: $$linker_script" >&2; exit 1; }; \
	echo "verified Orlix Kbuild linker script: $$linker_script"; \
	echo "prepared Orlix Kbuild output without a standalone image: $$build_dir (profile $(PROFILE))"

__headers-install: __prepare-kbuild
	@set -euo pipefail; \
	linux_make="$(LINUX_MAKE)"; \
	if [ -z "$$linux_make" ]; then linux_make="$$(command -v gmake || true)"; fi; \
	if [ -z "$$linux_make" ]; then \
		echo "GNU Make >= 4.0 is required by Linux headers_install; install gmake or set LINUX_MAKE=/path/to/gmake" >&2; \
		exit 1; \
	fi; \
	linux_sed_dir=""; \
	if [ -n "$(LINUX_SED)" ]; then \
		linux_sed="$(LINUX_SED)"; \
		case "$$linux_sed" in /*) ;; *) linux_sed="$(CURDIR)/$$linux_sed" ;; esac; \
		[ -x "$$linux_sed" ] || { echo "GNU sed is required by Linux headers_install; LINUX_SED is not executable: $$linux_sed" >&2; exit 1; }; \
		sed_shim_dir="$(CURDIR)/Build/OrlixKernel/tool-shims/$(PROFILE)-headers"; \
		if [ -e Build/OrlixKernel/tool-shims ] && [ -L Build/OrlixKernel/tool-shims ]; then echo "refusing to use symlinked Build/OrlixKernel/tool-shims directory" >&2; exit 1; fi; \
		mkdir -p "$$sed_shim_dir"; \
		ln -sf "$$linux_sed" "$$sed_shim_dir/sed"; \
		linux_sed_dir="$$sed_shim_dir"; \
	elif [ -x /opt/homebrew/opt/gnu-sed/libexec/gnubin/sed ]; then linux_sed_dir=/opt/homebrew/opt/gnu-sed/libexec/gnubin; fi; \
	if [ -z "$$linux_sed_dir" ]; then \
		echo "GNU sed is required by Linux headers_install; install gnu-sed or set LINUX_SED=/path/to/gnu/sed" >&2; \
		exit 1; \
	fi; \
	PATH="$$linux_sed_dir:$$PATH"; \
	export PATH; \
	sed --version >/dev/null 2>&1 || { echo "GNU sed is required by Linux headers_install" >&2; exit 1; }; \
	for path in Build/OrlixMLibC Build/OrlixMLibC/kernel-headers; do \
		if [ -e "$$path" ] && [ -L "$$path" ]; then echo "refusing to use symlinked path: $$path" >&2; exit 1; fi; \
	done; \
	rm -rf "$(ORLIX_MLIBC_KERNEL_HEADERS_DIR)"; \
	mkdir -p "$(ORLIX_MLIBC_KERNEL_HEADERS_DIR)"; \
	"$$linux_make" -C "$(ORLIX_KERNEL_PORT_DIR)" O="$(ORLIX_KERNEL_BUILD_DIR)" ARCH="$(LINUX_ARCH)" LLVM=1 INSTALL_HDR_PATH="$(ORLIX_MLIBC_KERNEL_HEADERS_DIR)" headers_install; \
	[ -d "$(ORLIX_MLIBC_KERNEL_HEADERS_DIR)/include" ] || { echo "missing installed Orlix UAPI headers: $(ORLIX_MLIBC_KERNEL_HEADERS_DIR)/include" >&2; exit 1; }; \
	echo "installed Orlix UAPI headers: $(ORLIX_MLIBC_KERNEL_HEADERS_DIR)/include"

__kunit: __prepare-kbuild
	@set -euo pipefail; \
	linux_make="$(LINUX_MAKE)"; \
	if [ -z "$$linux_make" ]; then linux_make="$$(command -v gmake || true)"; fi; \
	if [ -z "$$linux_make" ]; then \
		echo "GNU Make >= 4.0 is required by Linux KUnit builds; install gmake or set LINUX_MAKE=/path/to/gmake" >&2; \
		exit 1; \
	fi; \
	linux_llvm_bin="$(LINUX_LLVM_BIN)"; \
	linux_sed_dir=""; \
	if [ -n "$(LINUX_SED)" ]; then \
		linux_sed="$(LINUX_SED)"; \
		case "$$linux_sed" in /*) ;; *) linux_sed="$(CURDIR)/$$linux_sed" ;; esac; \
		[ -x "$$linux_sed" ] || { echo "GNU sed is required by Linux KUnit builds; LINUX_SED is not executable: $$linux_sed" >&2; exit 1; }; \
		sed_shim_dir="$(CURDIR)/Build/OrlixKernel/tool-shims/$(PROFILE)-kunit"; \
		if [ -e Build/OrlixKernel/tool-shims ] && [ -L Build/OrlixKernel/tool-shims ]; then echo "refusing to use symlinked Build/OrlixKernel/tool-shims directory" >&2; exit 1; fi; \
		mkdir -p "$$sed_shim_dir"; \
		ln -sf "$$linux_sed" "$$sed_shim_dir/sed"; \
		linux_sed_dir="$$sed_shim_dir"; \
	elif [ -x /opt/homebrew/opt/gnu-sed/libexec/gnubin/sed ]; then linux_sed_dir=/opt/homebrew/opt/gnu-sed/libexec/gnubin; fi; \
	if [ -z "$$linux_sed_dir" ]; then \
		echo "GNU sed is required by Linux KUnit builds; install gnu-sed or set LINUX_SED=/path/to/gnu/sed" >&2; \
		exit 1; \
	fi; \
	coreutils_dir=""; \
	if readlink -e / >/dev/null 2>&1; then coreutils_dir="$$(dirname "$$(command -v readlink)")"; \
	elif [ -x /opt/homebrew/opt/coreutils/libexec/gnubin/readlink ]; then coreutils_dir=/opt/homebrew/opt/coreutils/libexec/gnubin; \
	else echo "GNU readlink is required by Linux KUnit builds; install coreutils" >&2; exit 1; fi; \
	PATH="$$linux_sed_dir:$$coreutils_dir:$${linux_llvm_bin:+$$linux_llvm_bin:}$$PATH"; \
	export PATH; \
	sed --version >/dev/null 2>&1 || { echo "GNU sed is required by Linux KUnit builds" >&2; exit 1; }; \
	rm -rf "$(ORLIX_KUNIT_BUILD_DIR)"; \
	mkdir -p "$(ORLIX_KUNIT_BUILD_DIR)"; \
	"$$linux_make" -C "$(ORLIX_KERNEL_PORT_DIR)" O="$(ORLIX_KUNIT_BUILD_DIR)" ARCH="$(LINUX_ARCH)" LLVM=1 CLANG_TARGET_FLAGS=aarch64-linux-gnu HOSTCFLAGS="-I$(LINUX_HOST_COMPAT_INCLUDE_ROOT) -include linux_arm_elf_compat.h -D_UUID_T" defconfig; \
	"$(ORLIX_KERNEL_PORT_DIR)/scripts/kconfig/merge_config.sh" -m -O "$(ORLIX_KUNIT_BUILD_DIR)" "$(ORLIX_KUNIT_BUILD_DIR)/.config" "$(ORLIX_KERNEL_PORT_DIR)/arch/$(LINUX_ARCH)/.kunitconfig"; \
	"$$linux_make" -C "$(ORLIX_KERNEL_PORT_DIR)" O="$(ORLIX_KUNIT_BUILD_DIR)" ARCH="$(LINUX_ARCH)" LLVM=1 CLANG_TARGET_FLAGS=aarch64-linux-gnu HOSTCFLAGS="-I$(LINUX_HOST_COMPAT_INCLUDE_ROOT) -include linux_arm_elf_compat.h -D_UUID_T" olddefconfig arch/$(LINUX_ARCH)/boot/boot_test.o; \
	echo "built Orlix KUnit objects: $(ORLIX_KUNIT_BUILD_DIR)"

__kernel-archive: __prepare-kbuild
	@set -euo pipefail; \
	cc="$(ORLIX_KERNEL_CC)"; \
	ar_cmd="$(ORLIX_KERNEL_AR)"; \
	nm_cmd="$(ORLIX_KERNEL_NM)"; \
	otool_cmd="$(ORLIX_KERNEL_OTOOL)"; \
	linux_llvm_bin="$(LINUX_LLVM_BIN)"; \
	PATH="$${linux_llvm_bin:+$$linux_llvm_bin:}$$PATH"; \
	export PATH; \
	command -v "$$cc" >/dev/null 2>&1 || { echo "clang is required for OrlixKernel archive builds; set ORLIX_KERNEL_CC=/path/to/clang" >&2; exit 1; }; \
	command -v "$$ar_cmd" >/dev/null 2>&1 || { echo "llvm-ar is required for OrlixKernel archives; set ORLIX_KERNEL_AR=/path/to/llvm-ar" >&2; exit 1; }; \
	command -v "$$nm_cmd" >/dev/null 2>&1 || { echo "nm is required to verify OrlixKernel archive symbols; set ORLIX_KERNEL_NM=/path/to/nm" >&2; exit 1; }; \
	command -v "$$otool_cmd" >/dev/null 2>&1 || { echo "otool is required to verify OrlixKernel archive contracts; set ORLIX_KERNEL_OTOOL=/path/to/otool" >&2; exit 1; }; \
	root="$(ORLIX_KERNEL_ARCHIVE_ROOT)"; \
	case "$$root" in "$(CURDIR)"/Build/OrlixKernel/$(PROFILE)) ;; *) echo "refusing to write OrlixKernel archive outside Build/OrlixKernel/$(PROFILE): $$root" >&2; exit 1 ;; esac; \
	for path in Build/OrlixKernel "$$root"; do \
		if [ -e "$$path" ] && [ -L "$$path" ]; then echo "refusing to use symlinked kernel archive path: $$path" >&2; exit 1; fi; \
	done; \
	mkdir -p "$$root"; \
	$(call orlix_product_adapter_prepare); \
	mkdir -p "$(ORLIX_KERNEL_BUILD_DIR)/init"; \
	build_version="$${KBUILD_BUILD_VERSION:-$$(cd "$(ORLIX_KERNEL_BUILD_DIR)" && "$(CURDIR)/$(ORLIX_KERNEL_PORT_DIR)/scripts/build-version")}"; \
	build_timestamp="$${KBUILD_BUILD_TIMESTAMP:-$$(LC_ALL=C date)}"; \
	smp_flag=""; \
	preempt_flag=""; \
	if grep -q '^CONFIG_SMP=y$$' "$(ORLIX_KERNEL_BUILD_DIR)/.config"; then smp_flag="SMP"; fi; \
	if grep -q '^CONFIG_PREEMPT_BUILD=y$$' "$(ORLIX_KERNEL_BUILD_DIR)/.config"; then preempt_flag="PREEMPT"; fi; \
	if grep -q '^CONFIG_PREEMPT_DYNAMIC=y$$' "$(ORLIX_KERNEL_BUILD_DIR)/.config"; then preempt_flag="PREEMPT_DYNAMIC"; fi; \
	if grep -q '^CONFIG_PREEMPT_RT=y$$' "$(ORLIX_KERNEL_BUILD_DIR)/.config"; then preempt_flag="PREEMPT_RT"; fi; \
	uts_version="$$(printf '#%s %s %s %s' "$$build_version" "$$smp_flag" "$$preempt_flag" "$$build_timestamp" | cut -b -64)"; \
	printf '#define UTS_VERSION "%s"\n' "$$uts_version" > "$(ORLIX_KERNEL_BUILD_DIR)/init/utsversion-tmp.h"; \
	{ for src_rel in $(ORLIX_KERNEL_LINUX_SOURCES); do printf '%s\n' "$(ORLIX_KERNEL_PORT_DIR)/$$src_rel"; done; } > "$(ORLIX_KERNEL_ARCHIVE_MANIFEST)"; \
	$(call orlix_product_adapter_verify_object_contract) \
	$(call orlix_product_adapter_source_resolver) \
	$(call orlix_product_adapter_generate_payloads) \
	$(call orlix_product_adapter_generate_boundaries) \
	compile_slice() { \
		platform="$$1"; \
		target="$$2"; \
		output_dir="$$root/$$platform"; \
		obj_dir="$$output_dir/objects"; \
		archive="$$output_dir/$(ORLIX_KERNEL_ARCHIVE_NAME)"; \
		rm -rf "$$output_dir"; \
		mkdir -p "$$obj_dir"; \
		objs=(); \
		for src_rel in $(ORLIX_KERNEL_LINUX_SOURCES); do \
			src="$$(orlix_product_adapter_source_for "$$src_rel")"; \
			[ -s "$$src" ] || { echo "missing Linux source: $$src" >&2; exit 1; }; \
			kbuild_name="$${src_rel##*/}"; \
			kbuild_name="$${kbuild_name%.c}"; \
			kbuild_name="$${kbuild_name//-/_}"; \
			obj_name="$${src_rel//\//_}.o"; \
			obj="$$obj_dir/$$obj_name"; \
			dep="$$obj_dir/$${obj_name%.o}.d"; \
			extra_cflags=""; \
			case "$$src_rel" in \
				init/version.c) extra_cflags="-include $(ORLIX_KERNEL_BUILD_DIR)/init/utsversion-tmp.h" ;; \
				drivers/of/of_reserved_mem.c) extra_cflags="-I$(ORLIX_KERNEL_PORT_DIR)/drivers/of" ;; \
				kernel/sched/*.c) extra_cflags="-I$(ORLIX_KERNEL_PORT_DIR)/kernel/sched" ;; \
				lib/crc32.c) extra_cflags="-I$(ORLIX_KERNEL_PORT_DIR)/lib -I$(ORLIX_KERNEL_BUILD_DIR)/lib" ;; \
				lib/fdt*.c) extra_cflags="-I$(ORLIX_KERNEL_PORT_DIR)/scripts/dtc/libfdt" ;; \
			esac; \
			/usr/bin/env -u SDKROOT "$$cc" -target "$$target" -isysroot / -x c -ffreestanding $(ORLIX_PRODUCT_ADAPTER_CFLAGS) -fno-builtin -fno-stack-protector -fno-objc-arc -fno-common -nostdinc -D__KERNEL__ -DORLIX_APP_HOSTED_BOOT=1 -DKBUILD_MODNAME=\"$$kbuild_name\" -DKBUILD_BASENAME=\"$$kbuild_name\" -DKBUILD_MODFILE=\"$$src_rel\" -include "$(ORLIX_KERNEL_PORT_DIR)/include/linux/compiler-version.h" -include "$(ORLIX_KERNEL_PORT_DIR)/include/linux/kconfig.h" $$extra_cflags -I"$(ORLIX_KERNEL_PORT_DIR)/arch/$(LINUX_ARCH)/include" -I"$(ORLIX_KERNEL_BUILD_DIR)/arch/$(LINUX_ARCH)/include/generated" -I"$(ORLIX_KERNEL_PORT_DIR)/include" -I"$(ORLIX_KERNEL_BUILD_DIR)/include" -I"$(ORLIX_KERNEL_PORT_DIR)/arch/$(LINUX_ARCH)/include/uapi" -I"$(ORLIX_KERNEL_BUILD_DIR)/arch/$(LINUX_ARCH)/include/generated/uapi" -I"$(ORLIX_KERNEL_PORT_DIR)/include/uapi" -I"$(ORLIX_KERNEL_BUILD_DIR)/include/generated/uapi" -MMD -MF "$$dep" -c "$$src" -o "$$obj"; \
			if grep -E '(/Applications/|/Library/Developer/CommandLineTools/SDKs/|/System/Library/Frameworks|/usr/include)' "$$dep"; then \
				echo "Linux object included a host SDK or libc header: $$dep" >&2; \
				exit 1; \
			fi; \
			if grep -E '(OrlixHostAdapter/Sources|OrlixKernel/Sources/include|OrlixKernel/Sources/boot|OrlixMLibC/Sources|Build/OrlixMLibC/sysroot|/usr/local/include|/opt/homebrew/include)' "$$dep"; then \
				echo "Linux object included host, OrlixMLibC, or product headers: $$dep" >&2; \
				exit 1; \
			fi; \
			orlix_product_adapter_verify_object_contract "$$obj"; \
			objs+=("$$obj"); \
		done; \
		orlix_product_adapter_generate_payloads "$$platform" "$$target"; \
		orlix_product_adapter_generate_boundaries "$$platform" "$$target" "$${objs[@]}"; \
		"$$ar_cmd" rcs "$$archive" "$${objs[@]}"; \
		[ -s "$$archive" ] || { echo "missing OrlixKernel archive: $$archive" >&2; exit 1; }; \
		"$$nm_cmd" -gU "$$archive" > "$$output_dir/symbols.txt"; \
		grep -q '_arch_boot_entry' "$$output_dir/symbols.txt" || { echo "OrlixKernel archive missing _arch_boot_entry: $$archive" >&2; exit 1; }; \
		grep -q '_arch_boot_params' "$$output_dir/symbols.txt" || { echo "OrlixKernel archive missing _arch_boot_params: $$archive" >&2; exit 1; }; \
		echo "built OrlixKernel archive: $$archive ($$target)"; \
	}; \
	compile_slice iphoneos "$(ORLIX_IOS_TARGET)"; \
	compile_slice iphonesimulator "$(ORLIX_IOS_SIMULATOR_TARGET)"; \
	echo "wrote OrlixKernel archive manifest: $(ORLIX_KERNEL_ARCHIVE_MANIFEST)"

__verify-xcodegen-boundary:
	@set -euo pipefail; \
	project="$(ORLIX_XCODE_PROJECT)"; \
	[ -d "$$project" ] || { echo "missing generated Xcode project: $$project" >&2; exit 1; }; \
	if grep -R 'OrlixKernel/Sources/upstream/linux-6.12' "$$project"; then \
		echo "generated Xcode project references upstream Linux source" >&2; \
		exit 1; \
	fi; \
	if grep -R 'Build/OrlixKernel/linux-6.12-port' "$$project"; then \
		echo "generated Xcode project references disposable Linux port source" >&2; \
		exit 1; \
	fi; \
	if grep -R 'OrlixKernel/Sources/ports/orlix/overlay/.*\.c' "$$project"; then \
		echo "generated Xcode project references Linux overlay C source" >&2; \
		exit 1; \
	fi; \
	if grep -R 'Build/OrlixKernel/build' "$$project"; then \
		echo "generated Xcode project references Linux Kbuild output" >&2; \
		exit 1; \
	fi; \
	if grep -R 'Build/OrlixKernel/.*/objects' "$$project"; then \
		echo "generated Xcode project references compiled Linux object files" >&2; \
		exit 1; \
	fi; \
	if grep -R 'Build/OrlixMLibC/kernel-headers' "$$project"; then \
		echo "generated Xcode project references installed OrlixMLibC UAPI headers" >&2; \
		exit 1; \
	fi; \
	echo "verified generated Xcode project does not compile Linux-owned sources"

__verify-framework-symbols:
	@set -euo pipefail; \
	nm_cmd="$(ORLIX_KERNEL_NM)"; \
	framework_binary="$(ORLIX_IOS_SIMULATOR_FRAMEWORK)/OrlixKernel"; \
	[ -s "$$framework_binary" ] || { echo "missing OrlixKernel framework binary: $$framework_binary" >&2; exit 1; }; \
	symbols="$$(mktemp)"; \
	trap 'rm -f "$$symbols"' EXIT; \
	"$$nm_cmd" -gU "$$framework_binary" > "$$symbols"; \
	grep -q '_arch_boot_entry' "$$symbols" || { echo "OrlixKernel.framework missing _arch_boot_entry" >&2; exit 1; }; \
	grep -q '_arch_boot_params' "$$symbols" || { echo "OrlixKernel.framework missing _arch_boot_params" >&2; exit 1; }; \
	echo "verified OrlixKernel framework symbols: $$framework_binary"

__linux-userspace-sysroot:
	@set -euo pipefail; \
	if [ -L "$(ORLIX_LINUX_USERSPACE_SYSROOT_BOOTSTRAP_DIR)" ]; then \
		echo "refusing to use symlinked Linux userspace sysroot: $(ORLIX_LINUX_USERSPACE_SYSROOT_BOOTSTRAP_DIR)" >&2; \
		exit 1; \
	fi; \
	if [ ! -d "$(ORLIX_LINUX_USERSPACE_SYSROOT_BOOTSTRAP_DIR)" ]; then \
		./scripts/bootstrap-orlix-linux-userspace-sysroot.sh --output "$(ORLIX_LINUX_USERSPACE_SYSROOT_BOOTSTRAP_DIR)"; \
	fi; \
	[ -d "$(ORLIX_LINUX_USERSPACE_SYSROOT_BOOTSTRAP_DIR)" ] || { echo "missing Linux userspace sysroot: $(ORLIX_LINUX_USERSPACE_SYSROOT_BOOTSTRAP_DIR)" >&2; exit 1; }

__kselftest-install: __validate-profile
	@set -euo pipefail; \
	selected_libc="$(libc)"; \
	case "$$selected_libc" in linux|orlixmlibc) ;; *) echo "unsupported libc=$$selected_libc (expected linux or orlixmlibc)" >&2; exit 1 ;; esac; \
	sysroot="$(KSELFTEST_SYSROOT)"; \
	install_dir="$(KSELFTEST_INSTALL_DIR)"; \
	header_flags="$(KSELFTEST_HEADER_FLAGS)"; \
	proof_label="$(KSELFTEST_PROOF_LABEL)"; \
	case "$$sysroot" in /*) ;; *) sysroot="$(CURDIR)/$$sysroot" ;; esac; \
	[ -d "$$sysroot" ] || { echo "missing kselftest sysroot: $$sysroot" >&2; exit 1; }; \
	linux_make="$(LINUX_MAKE)"; \
	if [ -z "$$linux_make" ]; then linux_make="$$(command -v gmake || true)"; fi; \
	if [ -z "$$linux_make" ]; then echo "GNU Make >= 4.0 is required by Linux kselftest; install gmake or set LINUX_MAKE=/path/to/gmake" >&2; exit 1; fi; \
	linux_llvm_bin="$(LINUX_LLVM_BIN)"; \
	coreutils_dir=""; \
	if readlink -e / >/dev/null 2>&1; then coreutils_dir="$$(dirname "$$(command -v readlink)")"; \
	elif [ -x /opt/homebrew/opt/coreutils/libexec/gnubin/readlink ]; then coreutils_dir=/opt/homebrew/opt/coreutils/libexec/gnubin; \
	else echo "GNU readlink is required by kselftest install; install coreutils" >&2; exit 1; fi; \
	PATH="$$coreutils_dir:$${linux_llvm_bin:+$$linux_llvm_bin:}$$PATH"; \
	export PATH; \
	command -v clang >/dev/null 2>&1 || { echo "clang is required to build Linux kselftest artifacts" >&2; exit 1; }; \
	rm -rf "$$install_dir"; \
	mkdir -p "$$(dirname "$$install_dir")"; \
	"$$linux_make" -C "$(ORLIX_KERNEL_PORT_DIR)/tools/testing/selftests" \
		O="$(ORLIX_KERNEL_BUILD_DIR)" \
		TARGETS=orlix \
		KSFT_INSTALL_PATH="$$install_dir" \
		ARCH="$(ORLIX_KSELFTEST_ARCH)" \
		LLVM=1 \
		FORCE_TARGETS=1 \
		USERCFLAGS="--sysroot=$$sysroot $$header_flags" \
		USERLDFLAGS="--sysroot=$$sysroot -static -fuse-ld=lld" \
		install; \
	printf 'proof_lane=%s\n' "$$proof_label" > "$$install_dir/proof_lane.txt"; \
	[ -s "$$install_dir/run_kselftest.sh" ] || { echo "missing installed kselftest runner" >&2; exit 1; }; \
	echo "installed Orlix kselftests: $$install_dir (libc $$selected_libc)"

__kselftest-initramfs: kselftest-install
	@set -euo pipefail; \
	selected_libc="$(libc)"; \
	case "$$selected_libc" in linux|orlixmlibc) ;; *) echo "unsupported libc=$$selected_libc (expected linux or orlixmlibc)" >&2; exit 1 ;; esac; \
	install_dir="$(KSELFTEST_INSTALL_DIR)"; \
	output="$(KSELFTEST_INITRAMFS_DIR)"; \
	proof_label="$(KSELFTEST_PROOF_LABEL)"; \
	case "$$output" in \
		"$(CURDIR)"/Build/OrlixKernel/test-initramfs/*|Build/OrlixKernel/test-initramfs/*|"$(CURDIR)"/Build/OrlixMLibC/test-initramfs/*|Build/OrlixMLibC/test-initramfs/*) ;; \
		*) echo "refusing to write test initramfs outside Build test-initramfs roots: $$output" >&2; exit 1 ;; \
	esac; \
	for path in Build Build/OrlixKernel Build/OrlixMLibC Build/OrlixKernel/test-initramfs Build/OrlixMLibC/test-initramfs "$$output"; do \
		if [ -L "$$path" ]; then echo "refusing to package test initramfs through symlinked path: $$path" >&2; exit 1; fi; \
	done; \
	[ -d "$$install_dir" ] || { echo "missing kselftest install directory: $$install_dir" >&2; exit 1; }; \
	for required in \
		"$$install_dir/run_kselftest.sh" \
		"$$install_dir/kselftest/runner.sh" \
		"$$install_dir/kselftest-list.txt" \
		"$$install_dir/orlix/boot_profile_contract" \
		"$$install_dir/orlix/virtio_mmio_probe_contract"; do \
		[ -s "$$required" ] || { echo "missing non-empty kselftest install input: $$required" >&2; exit 1; }; \
	done; \
	grep -qx 'orlix:boot_profile_contract' "$$install_dir/kselftest-list.txt" || { echo "kselftest install list is missing orlix:boot_profile_contract" >&2; exit 1; }; \
	grep -qx 'orlix:virtio_mmio_probe_contract' "$$install_dir/kselftest-list.txt" || { echo "kselftest install list is missing orlix:virtio_mmio_probe_contract" >&2; exit 1; }; \
	output_parent="$$(dirname "$$output")"; \
	rm -rf "$$output"; \
	mkdir -p "$$output_parent" "$$output/kselftest"; \
	cp -R "$$install_dir/." "$$output/kselftest"; \
	{ \
		printf '%s\n' '#!/bin/sh'; \
		printf '%s\n' 'set -eu'; \
		printf '\n'; \
		printf '%s\n' 'mkdir -p /proc /sys /sys/kernel/debug /dev'; \
		printf '%s\n' 'mount -t proc proc /proc 2>/dev/null || true'; \
		printf '%s\n' 'mount -t sysfs sysfs /sys 2>/dev/null || true'; \
		printf '%s\n' 'mount -t debugfs debugfs /sys/kernel/debug 2>/dev/null || true'; \
		printf '\n'; \
		printf '%s\n' "echo \"ORLIX-PROOF-LANE $$proof_label\""; \
		printf '%s\n' "echo \"ORLIX-PROFILE $(PROFILE)\""; \
		printf '%s\n' "echo \"ORLIX-LINUX-ARCH $(LINUX_ARCH)\""; \
		printf '%s\n' "echo \"ORLIX-LINUX-VERSION $(LINUX_VERSION)\""; \
		printf '\n'; \
		printf '%s\n' 'echo "ORLIX-KUNIT-BEGIN"'; \
		printf '%s\n' 'if [ -d /sys/kernel/debug/kunit ]; then'; \
		printf '%s\n' '    found_kunit_results=0'; \
		printf '%s\n' '    for result in /sys/kernel/debug/kunit/*/results; do'; \
		printf '%s\n' '        if [ -r "$$result" ]; then'; \
		printf '%s\n' '            found_kunit_results=1'; \
		printf '%s\n' '            cat "$$result"'; \
		printf '%s\n' '        fi'; \
		printf '%s\n' '    done'; \
		printf '%s\n' '    if [ "$$found_kunit_results" -eq 0 ]; then'; \
		printf '%s\n' '        dmesg || true'; \
		printf '%s\n' '    fi'; \
		printf '%s\n' 'else'; \
		printf '%s\n' '    dmesg || true'; \
		printf '%s\n' 'fi'; \
		printf '%s\n' 'echo "ORLIX-KUNIT-END"'; \
		printf '\n'; \
		printf '%s\n' 'echo "ORLIX-KSELFTEST-BEGIN"'; \
		printf '%s\n' 'cd /kselftest'; \
		printf '%s\n' 'set +e'; \
		printf '%s\n' './run_kselftest.sh -c orlix'; \
		printf '%s\n' 'kselftest_status="$$?"'; \
		printf '%s\n' 'set -e'; \
		printf '%s\n' 'echo "ORLIX-KSELFTEST-END status=$$kselftest_status"'; \
		printf '%s\n' 'exit "$$kselftest_status"'; \
	} > "$$output/init"; \
	chmod 755 "$$output/init"; \
	printf 'proof_lane=%s\n' "$$proof_label" > "$$output/proof_lane.txt"; \
	printf '%s\n' "$(PROFILE)" > "$$output/selected_profile.txt"; \
	printf '%s\n' "$(LINUX_ARCH)" > "$$output/linux_arch.txt"; \
	printf '%s\n' "$(LINUX_VERSION)" > "$$output/linux_version.txt"; \
	shasum -a 256 "$$output/kselftest/kselftest-list.txt" | awk '{ print $$1 }' > "$$output/kselftest-list.sha256"; \
	{ \
		printf '%s\n' '<?xml version="1.0" encoding="UTF-8"?>'; \
		printf '%s\n' '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">'; \
		printf '%s\n' '<plist version="1.0">'; \
		printf '%s\n' '<dict>'; \
		printf '%s\n' '    <key>CFBundleIdentifier</key>'; \
		printf '%s\n' '    <string>org.orlix.OrlixTestInitramfs</string>'; \
		printf '%s\n' '    <key>CFBundleName</key>'; \
		printf '%s\n' '    <string>OrlixTestInitramfs</string>'; \
		printf '%s\n' '    <key>CFBundlePackageType</key>'; \
		printf '%s\n' '    <string>BNDL</string>'; \
		printf '%s\n' '    <key>CFBundleShortVersionString</key>'; \
		printf '%s\n' '    <string>0.1</string>'; \
		printf '%s\n' '    <key>CFBundleVersion</key>'; \
		printf '%s\n' '    <string>1</string>'; \
		printf '%s\n' '    <key>OrlixLinuxArch</key>'; \
		printf '%s\n' '    <string>$(LINUX_ARCH)</string>'; \
		printf '%s\n' '    <key>OrlixLinuxVersion</key>'; \
		printf '%s\n' '    <string>$(LINUX_VERSION)</string>'; \
		printf '%s\n' '    <key>OrlixProfile</key>'; \
		printf '%s\n' '    <string>$(PROFILE)</string>'; \
		printf '%s\n' '    <key>OrlixProofLane</key>'; \
		printf '%s\n' "    <string>$$proof_label</string>"; \
		printf '%s\n' '</dict>'; \
		printf '%s\n' '</plist>'; \
	} > "$$output/Info.plist"; \
	plutil -lint "$$output/Info.plist" >/dev/null; \
	echo "packaged kselftest initramfs: $$output (libc $$selected_libc)"

__kernel-payload: __prepare-kbuild
	@set -euo pipefail; \
	output="$(ORLIX_KERNEL_PAYLOAD_DIR)"; \
	case "$$output" in \
		"$(CURDIR)"/Build/OrlixKernel/payload/OrlixKernelPayload.bundle) ;; \
		*) echo "refusing to write OrlixKernel payload outside Build/OrlixKernel/payload: $$output" >&2; exit 1 ;; \
	esac; \
	for path in Build Build/OrlixKernel Build/OrlixKernel/payload "$$output"; do \
		if [ -L "$$path" ]; then echo "refusing to package OrlixKernel payload through symlinked path: $$path" >&2; exit 1; fi; \
	done; \
	rm -rf "$$output"; \
	mkdir -p "$$output/arch/$(LINUX_ARCH)/boot/dts"; \
	for dtb in appstore development; do \
		input="$(ORLIX_KERNEL_BUILD_DIR)/arch/$(LINUX_ARCH)/boot/dts/$$dtb.dtb"; \
		[ -s "$$input" ] || { echo "missing non-empty profile DTB: $$input" >&2; exit 1; }; \
		cp "$$input" "$$output/arch/$(LINUX_ARCH)/boot/dts/$$dtb.dtb"; \
	done; \
	{ \
		printf '%s\n' '<?xml version="1.0" encoding="UTF-8"?>'; \
		printf '%s\n' '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">'; \
		printf '%s\n' '<plist version="1.0">'; \
		printf '%s\n' '<dict>'; \
		printf '%s\n' '    <key>CFBundleIdentifier</key>'; \
		printf '%s\n' '    <string>org.orlix.OrlixKernelPayload</string>'; \
		printf '%s\n' '    <key>CFBundleName</key>'; \
		printf '%s\n' '    <string>OrlixKernelPayload</string>'; \
		printf '%s\n' '    <key>CFBundlePackageType</key>'; \
		printf '%s\n' '    <string>BNDL</string>'; \
		printf '%s\n' '    <key>CFBundleShortVersionString</key>'; \
		printf '%s\n' '    <string>0.1</string>'; \
		printf '%s\n' '    <key>CFBundleVersion</key>'; \
		printf '%s\n' '    <string>1</string>'; \
		printf '%s\n' '    <key>OrlixLinuxArch</key>'; \
		printf '%s\n' '    <string>$(LINUX_ARCH)</string>'; \
		printf '%s\n' '    <key>OrlixLinuxVersion</key>'; \
		printf '%s\n' '    <string>$(LINUX_VERSION)</string>'; \
		printf '%s\n' '    <key>OrlixSelectedProfile</key>'; \
		printf '%s\n' '    <string>$(PROFILE)</string>'; \
		printf '%s\n' '</dict>'; \
		printf '%s\n' '</plist>'; \
	} > "$$output/Info.plist"; \
	plutil -lint "$$output/Info.plist" >/dev/null; \
	echo "packaged OrlixKernel payload: $$output (profile $(PROFILE))"

__ios-simulator-framework: __kernel-payload __kernel-archive
	@set -euo pipefail; \
	$(MAKE) xcodeproj; \
	command -v "$(XCODEBUILD_MCP)" >/dev/null 2>&1 || { echo "XcodeBuildMCP is required; install xcodebuildmcp or set XCODEBUILD_MCP=/path/to/xcodebuildmcp" >&2; exit 1; }; \
	selector=(); \
	if [ -n "$(ORLIX_IOS_SIMULATOR_ID)" ]; then \
		selector=(--simulator-id "$(ORLIX_IOS_SIMULATOR_ID)"); \
	else \
		selector=(--simulator-name "$(ORLIX_IOS_SIMULATOR_NAME)" --use-latest-os); \
	fi; \
	ORLIX_PROFILE="$(PROFILE)" "$(XCODEBUILD_MCP)" simulator build \
		--project-path "$(CURDIR)/$(ORLIX_XCODE_PROJECT)" \
		--scheme "OrlixKernel" \
		--configuration "Debug" \
		--derived-data-path "$(ORLIX_IOS_SIMULATOR_DERIVED_DATA)" \
		"$${selector[@]}" \
		--output json; \
	[ -d "$(ORLIX_IOS_SIMULATOR_FRAMEWORK)" ] || { echo "missing simulator framework: $(ORLIX_IOS_SIMULATOR_FRAMEWORK)" >&2; exit 1; }; \
	$(MAKE) -f OrlixKernel/Makefile __verify-framework-symbols PROFILE="$(PROFILE)"

__ios-simulator-xcframework: __ios-simulator-framework
	@set -euo pipefail; \
	./scripts/package-orlixkernel-simulator-xcframework.sh --framework "$(ORLIX_IOS_SIMULATOR_FRAMEWORK)" --output "$(ORLIX_KERNEL_XCFRAMEWORK)"; \
	./scripts/verify-orlixkernel-simulator-xcframework.sh --xcframework "$(ORLIX_KERNEL_XCFRAMEWORK)" --profile "$(PROFILE)" --linux-version "$(LINUX_VERSION)" --linux-arch "$(LINUX_ARCH)"
