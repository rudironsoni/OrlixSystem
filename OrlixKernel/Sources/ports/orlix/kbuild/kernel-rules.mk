SHELL := /bin/bash
.DEFAULT_GOAL := all
.NOTPARALLEL:

empty :=
space := $(empty) $(empty)
comma := ,

LINUX_VERSION ?= 6.12
ORLIX_PORT_ARCH ?= orlix
LINUX_UAPI_ARCH ?= arm64
LINUX_TAG ?= v$(LINUX_VERSION)
LINUX_REMOTE ?= https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git
ORLIX_HEADERS_INSTALL_JOBS ?= 1

PROFILE ?= release
ORLIX_PROFILES := release development

type ?= kunit
libc ?= orlixmlibc

LINUX_UPSTREAM_DIR ?= Build/OrlixKernel/upstream/linux-$(LINUX_VERSION).git

ORLIX_LINUX_OVERLAY ?= OrlixKernel/Sources/ports/orlix/overlay
ORLIX_LINUX_PATCH_DIR ?= OrlixKernel/Sources/ports/orlix/patches
override ORLIX_PROFILE_CONFIG := OrlixKernel/Sources/ports/orlix/configs/$(PROFILE)_defconfig

ORLIX_KERNEL_PORT_DIR ?= Build/OrlixKernel/src/linux-$(LINUX_VERSION)-port
ORLIX_KERNEL_PORT_ARCHIVE_PATHS := \
	.clang-format \
	.cocciconfig \
	.editorconfig \
	.gitattributes \
	.gitignore \
	.mailmap \
	.rustfmt.toml \
	COPYING \
	CREDITS \
	Documentation/Kconfig \
	Kbuild \
	Kconfig \
	LICENSES \
	MAINTAINERS \
	Makefile \
	README \
	arch \
	block \
	certs \
	crypto \
	drivers \
	fs \
	include \
	init \
	io_uring \
	ipc \
	kernel \
	lib \
	mm \
	net \
	rust \
	samples \
	scripts \
	security \
	sound \
	usr \
	virt \
	tools/testing/selftests/Makefile \
	tools/testing/selftests/kselftest \
	tools/testing/selftests/kselftest.h \
	tools/testing/selftests/kselftest_harness.h \
	tools/testing/selftests/kselftest_module.h \
	tools/testing/selftests/lib.mk \
	tools/testing/selftests/run_kselftest.sh
ORLIX_KERNEL_BUILD_ROOT := $(CURDIR)/Build/OrlixKernel/build
ORLIX_KERNEL_BUILD_DIR := $(ORLIX_KERNEL_BUILD_ROOT)/$(PROFILE)
ORLIX_KERNEL_BUILD_LOCK_ROOT := $(CURDIR)/Build/OrlixKernel/locks
ORLIX_KERNEL_BUILD_LOCK := $(ORLIX_KERNEL_BUILD_LOCK_ROOT)/$(PROFILE).lock
ORLIX_KERNEL_ARCHIVE_ROOT := $(CURDIR)/Build/OrlixKernel/$(PROFILE)
ORLIX_KERNEL_ARCHIVE_MANIFEST := $(ORLIX_KERNEL_ARCHIVE_ROOT)/linux-object-manifest.txt
ORLIX_KERNEL_DEVICE_ARCHIVE_DIR := $(ORLIX_KERNEL_ARCHIVE_ROOT)/iphoneos
ORLIX_KERNEL_SIMULATOR_ARCHIVE_DIR := $(ORLIX_KERNEL_ARCHIVE_ROOT)/iphonesimulator
ORLIX_KERNEL_ARCHIVE_NAME := OrlixKernel.a
ORLIX_KERNEL_ARCHIVE_PLATFORMS ?= iphoneos iphonesimulator
ORLIX_IOS_TARGET := arm64-apple-ios
ORLIX_IOS_SIMULATOR_TARGET := arm64-apple-ios-simulator
ORLIX_HOSTED_SYSCALL_GATE_ADDRESS ?= 0x00006ffff0000000
ORLIX_KERNEL_HOSTCFLAGS = -I$(LINUX_HOST_COMPAT_INCLUDE_ROOT) -I$(ORLIX_KERNEL_PORT_ABS)/arch/$(ORLIX_PORT_ARCH)/include/uapi -include linux_arm_elf_compat.h -D_UUID_T

define orlix_kernel_acquire_profile_lock
if [ "$${ORLIX_KERNEL_PROFILE_LOCK_HELD:-0}" != 1 ]; then \
lock_root="$(ORLIX_KERNEL_BUILD_LOCK_ROOT)"; \
for path in Build Build/OrlixKernel "$$lock_root"; do \
	if [ -e "$$path" ] && [ -L "$$path" ]; then echo "refusing to use symlinked OrlixKernel lock path: $$path" >&2; exit 1; fi; \
done; \
mkdir -p "$$lock_root"; \
lock_dir="$(ORLIX_KERNEL_BUILD_LOCK).dir"; \
while ! mkdir "$$lock_dir" 2>/dev/null; do \
	if [ -s "$$lock_dir/pid" ] && ! kill -0 "$$(cat "$$lock_dir/pid")" 2>/dev/null; then \
		rm -rf "$$lock_dir"; \
		continue; \
	fi; \
	sleep 1; \
done; \
printf '%s\n' "$$$$" > "$$lock_dir/pid"; \
trap 'rm -rf "$$lock_dir"' EXIT INT TERM; \
export ORLIX_KERNEL_PROFILE_LOCK_HELD=1; \
fi
endef

ORLIX_KERNEL_LINUX_SOURCES := \
	arch/$(ORLIX_PORT_ARCH)/boot/boot.c \
	arch/$(ORLIX_PORT_ARCH)/kernel/cpuinfo.c \
	arch/$(ORLIX_PORT_ARCH)/kernel/hosted_exec.c \
	arch/$(ORLIX_PORT_ARCH)/kernel/idle.c \
	arch/$(ORLIX_PORT_ARCH)/kernel/irq.c \
	arch/$(ORLIX_PORT_ARCH)/kernel/process.c \
	arch/$(ORLIX_PORT_ARCH)/kernel/ptrace.c \
	arch/$(ORLIX_PORT_ARCH)/kernel/reboot.c \
	arch/$(ORLIX_PORT_ARCH)/kernel/setup.c \
	arch/$(ORLIX_PORT_ARCH)/kernel/signal.c \
	arch/$(ORLIX_PORT_ARCH)/kernel/syscall.c \
	arch/$(ORLIX_PORT_ARCH)/kernel/time.c \
	arch/$(ORLIX_PORT_ARCH)/kernel/traps.c \
	arch/$(ORLIX_PORT_ARCH)/mm/delay.c \
	arch/$(ORLIX_PORT_ARCH)/mm/fault.c \
	arch/$(ORLIX_PORT_ARCH)/mm/iomem.c \
	arch/$(ORLIX_PORT_ARCH)/mm/init.c \
	arch/$(ORLIX_PORT_ARCH)/mm/mmap.c \
	arch/$(ORLIX_PORT_ARCH)/mm/uaccess.c \
	init/version.c \
	init/main.c \
	init/init_task.c \
	init/calibrate.c \
	init/initramfs.c \
	init/do_mounts.c \
	init/do_mounts_initrd.c \
	kernel/exec_domain.c \
	kernel/fork.c \
	kernel/async.c \
	kernel/capability.c \
	kernel/cred.c \
	kernel/cpu.c \
	kernel/exit.c \
	kernel/iomem.c \
	kernel/irq_work.c \
	kernel/irq/irqdesc.c \
	kernel/irq/handle.c \
	kernel/irq/manage.c \
	kernel/irq/spurious.c \
	kernel/irq/resend.c \
	kernel/irq/irqdomain.c \
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
	kernel/futex/core.c \
	kernel/futex/syscalls.c \
	kernel/futex/pi.c \
	kernel/futex/requeue.c \
	kernel/futex/waitwake.c \
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
	kernel/utsname_sysctl.c \
	kernel/utsname.c \
	kernel/workqueue.c \
	kernel/cgroup/cgroup.c \
	kernel/cgroup/cgroup-v1.c \
	kernel/cgroup/freezer.c \
	kernel/cgroup/namespace.c \
	kernel/cgroup/rstat.c \
	kernel/dma/direct.c \
	kernel/dma/coherent.c \
	kernel/dma/mapping.c \
	kernel/bpf/core.c \
	kernel/rcu/sync.c \
	kernel/rcu/srcutiny.c \
	kernel/rcu/tiny.c \
	kernel/rcu/update.c \
	kernel/kallsyms.c \
	kernel/ksysfs.c \
	kernel/audit.c \
	kernel/auditfilter.c \
	kernel/reboot.c \
	kernel/range.c \
	kernel/smpboot.c \
	kernel/regset.c \
	kernel/ksyms_common.c \
	kernel/up.c \
	ipc/util.c \
	ipc/msgutil.c \
	ipc/msg.c \
	ipc/sem.c \
	ipc/shm.c \
	ipc/syscall.c \
	ipc/ipc_sysctl.c \
	ipc/mqueue.c \
	ipc/namespace.c \
	ipc/mq_sysctl.c \
	security/security.c \
	security/lsm_syscalls.c \
	security/lsm_audit.c \
	security/inode.c \
	security/commoncap.c \
	security/min_addr.c \
	security/integrity/iint.c \
	security/integrity/integrity_audit.c \
	security/selinux/avc.c \
	security/selinux/hooks.c \
	security/selinux/selinuxfs.c \
	security/selinux/netlink.c \
	security/selinux/nlmsgtab.c \
	security/selinux/netif.c \
	security/selinux/netnode.c \
	security/selinux/netport.c \
	security/selinux/status.c \
	security/selinux/ss/ebitmap.c \
	security/selinux/ss/hashtab.c \
	security/selinux/ss/symtab.c \
	security/selinux/ss/sidtab.c \
	security/selinux/ss/avtab.c \
	security/selinux/ss/policydb.c \
	security/selinux/ss/services.c \
	security/selinux/ss/conditional.c \
	security/selinux/ss/mls.c \
	security/selinux/ss/context.c \
	crypto/api.c \
	crypto/cipher.c \
	crypto/compress.c \
	crypto/algapi.c \
	crypto/scatterwalk.c \
	crypto/proc.c \
	crypto/ahash.c \
	crypto/crc32c_generic.c \
	crypto/shash.c \
	lib/is_single_threaded.c \
	lib/kobject_uevent.c \
	lib/kobject.c \
	lib/klist.c \
	lib/string.c \
	lib/string_helpers.c \
	lib/buildid.c \
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
	lib/sg_pool.c \
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
	lib/crc16.c \
	lib/crc32.c \
	lib/dec_and_lock.c \
	lib/debug_locks.c \
	lib/crypto/chacha.c \
	lib/crypto/blake2s.c \
	lib/crypto/blake2s-generic.c \
	lib/crypto/sha1.c \
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
	lib/decompress.c \
	lib/decompress_inflate.c \
	lib/decompress_bunzip2.c \
	lib/decompress_unlzma.c \
	lib/decompress_unxz.c \
	lib/decompress_unlzo.c \
	lib/decompress_unlz4.c \
	lib/decompress_unzstd.c \
	lib/xxhash.c \
	lib/zlib_inflate/inffast.c \
	lib/zlib_inflate/inflate.c \
	lib/zlib_inflate/infutil.c \
	lib/zlib_inflate/inftrees.c \
	lib/zlib_inflate/inflate_syms.c \
	lib/lzo/lzo1x_decompress_safe.c \
	lib/lz4/lz4_decompress.c \
	lib/xz/xz_dec_syms.c \
	lib/xz/xz_dec_stream.c \
	lib/xz/xz_dec_lzma2.c \
	lib/xz/xz_dec_bcj.c \
	lib/zstd/zstd_common_module.c \
	lib/zstd/common/debug.c \
	lib/zstd/common/entropy_common.c \
	lib/zstd/common/error_private.c \
	lib/zstd/common/fse_decompress.c \
	lib/zstd/common/zstd_common.c \
	lib/zstd/zstd_decompress_module.c \
	lib/zstd/decompress/huf_decompress.c \
	lib/zstd/decompress/zstd_ddict.c \
	lib/zstd/decompress/zstd_decompress.c \
	lib/zstd/decompress/zstd_decompress_block.c \
	lib/sort.c \
	lib/sbitmap.c \
	lib/flex_proportions.c \
	lib/bcd.c \
	lib/parser.c \
	lib/random32.c \
	lib/clz_ctz.c \
	lib/bsearch.c \
	lib/lwq.c \
	lib/memweight.c \
	lib/kfifo.c \
	lib/rhashtable.c \
	lib/base64.c \
	lib/once.c \
	lib/usercopy.c \
	lib/rcuref.c \
	lib/bucket_locks.c \
	lib/generic-radix-tree.c \
	lib/group_cpus.c \
	lib/nlattr.c \
	lib/devres.c \
	lib/checksum.c \
	lib/win_minmax.c \
	lib/dynamic_queue_limits.c \
	lib/net_utils.c \
	lib/dim/dim.c \
	lib/dim/net_dim.c \
	lib/dim/rdma_dim.c \
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
	kernel/time/tick-oneshot.c \
	kernel/time/tick-sched.c \
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
	mm/migrate.c \
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
	block/blk-mq-virtio.c \
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
	drivers/base/devtmpfs.c \
	drivers/base/firmware_loader/main.c \
	drivers/base/firmware_loader/builtin/main.c \
	drivers/block/loop.c \
	drivers/block/virtio_blk.c \
	drivers/char/mem.c \
	drivers/char/misc.c \
	drivers/char/random.c \
	drivers/char/hw_random/core.c \
	drivers/char/hw_random/virtio-rng.c \
	drivers/char/virtio_console.c \
	drivers/net/loopback.c \
	drivers/tty/hvc/hvc_console.c \
	drivers/tty/tty_io.c \
	drivers/tty/n_tty.c \
	drivers/tty/tty_ioctl.c \
	drivers/tty/tty_ldisc.c \
	drivers/tty/tty_buffer.c \
	drivers/tty/tty_port.c \
	drivers/tty/tty_mutex.c \
	drivers/tty/tty_ldsem.c \
	drivers/tty/tty_baudrate.c \
	drivers/tty/tty_jobctrl.c \
	drivers/tty/n_null.c \
	drivers/tty/tty_audit.c \
	drivers/tty/pty.c \
	drivers/tty/vt/vt_ioctl.c \
	drivers/tty/vt/vc_screen.c \
	drivers/tty/vt/selection.c \
	drivers/tty/vt/keyboard.c \
	drivers/tty/vt/vt.c \
	drivers/tty/vt/defkeymap.c \
	drivers/tty/vt/consolemap.c \
	drivers/tty/vt/consolemap_deftbl.c \
	drivers/video/console/dummycon.c \
	drivers/orlix/tty/console.c \
	drivers/input/input.c \
	drivers/input/input-compat.c \
	drivers/input/input-mt.c \
	drivers/input/input-poller.c \
	drivers/input/ff-core.c \
	drivers/input/touchscreen.c \
	drivers/input/ff-memless.c \
	drivers/input/vivaldi-fmap.c \
	drivers/input/keyboard/atkbd.c \
	drivers/input/serio/serio.c \
	drivers/input/serio/serport.c \
	drivers/input/serio/libps2.c \
	drivers/input/mouse/psmouse-base.c \
	drivers/input/mouse/synaptics.c \
	drivers/input/mouse/focaltech.c \
	drivers/input/mouse/alps.c \
	drivers/input/mouse/byd.c \
	drivers/input/mouse/logips2pp.c \
	drivers/input/mouse/cypress_ps2.c \
	drivers/input/mouse/trackpoint.c \
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
	drivers/of/irq.c \
	drivers/orlix/virtio/mmio.c \
	drivers/virtio/virtio.c \
	drivers/virtio/virtio_ring.c \
	drivers/virtio/virtio_anchor.c \
	drivers/virtio/virtio_mmio.c \
	fs/anon_inodes.c \
	fs/attr.c \
	fs/bad_inode.c \
	fs/char_dev.c \
	fs/dcache.c \
	fs/d_path.c \
	fs/exec.c \
	fs/binfmt_elf.c \
	fs/binfmt_script.c \
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
	fs/posix_acl.c \
	fs/proc_namespace.c \
	fs/notify/fsnotify.c \
	fs/notify/notification.c \
	fs/notify/group.c \
	fs/notify/mark.c \
	fs/notify/fdinfo.c \
	fs/notify/dnotify/dnotify.c \
	fs/notify/inotify/inotify_fsnotify.c \
	fs/notify/inotify/inotify_user.c \
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
	fs/backing-file.c \
	fs/buffer.c \
	fs/mbcache.c \
	fs/mpage.c \
	fs/eventpoll.c \
	fs/signalfd.c \
	fs/timerfd.c \
	fs/eventfd.c \
	fs/aio.c \
	fs/coredump.c \
	fs/drop_caches.c \
	fs/sysctls.c \
	fs/ramfs/inode.c \
	fs/ramfs/file-mmu.c \
	fs/devpts/inode.c \
	fs/jbd2/transaction.c \
	fs/jbd2/commit.c \
	fs/jbd2/recovery.c \
	fs/jbd2/checkpoint.c \
	fs/jbd2/revoke.c \
	fs/jbd2/journal.c \
	fs/ext4/balloc.c \
	fs/ext4/bitmap.c \
	fs/ext4/block_validity.c \
	fs/ext4/dir.c \
	fs/ext4/ext4_jbd2.c \
	fs/ext4/extents.c \
	fs/ext4/extents_status.c \
	fs/ext4/file.c \
	fs/ext4/fsmap.c \
	fs/ext4/fsync.c \
	fs/ext4/hash.c \
	fs/ext4/ialloc.c \
	fs/ext4/indirect.c \
	fs/ext4/inline.c \
	fs/ext4/inode.c \
	fs/ext4/ioctl.c \
	fs/ext4/mballoc.c \
	fs/ext4/migrate.c \
	fs/ext4/mmp.c \
	fs/ext4/move_extent.c \
	fs/ext4/namei.c \
	fs/ext4/page-io.c \
	fs/ext4/readpage.c \
	fs/ext4/resize.c \
	fs/ext4/super.c \
	fs/ext4/symlink.c \
	fs/ext4/sysfs.c \
	fs/ext4/xattr.c \
	fs/ext4/xattr_hurd.c \
	fs/ext4/xattr_trusted.c \
	fs/ext4/xattr_user.c \
	fs/ext4/xattr_security.c \
	fs/ext4/fast_commit.c \
	fs/ext4/acl.c \
	fs/ext4/orphan.c \
	fs/iomap/trace.c \
	fs/iomap/iter.c \
	fs/iomap/buffered-io.c \
	fs/iomap/direct-io.c \
	fs/iomap/fiemap.c \
	fs/iomap/seek.c \
	fs/iomap/swapfile.c \
	fs/overlayfs/super.c \
	fs/overlayfs/namei.c \
	fs/overlayfs/util.c \
	fs/overlayfs/inode.c \
	fs/overlayfs/file.c \
	fs/overlayfs/dir.c \
	fs/overlayfs/readdir.c \
	fs/overlayfs/copy_up.c \
	fs/overlayfs/export.c \
	fs/overlayfs/params.c \
	fs/overlayfs/xattrs.c \
	io_uring/io_uring.c \
	io_uring/opdef.c \
	io_uring/kbuf.c \
	io_uring/rsrc.c \
	io_uring/notif.c \
	io_uring/tctx.c \
	io_uring/filetable.c \
	io_uring/rw.c \
	io_uring/net.c \
	io_uring/poll.c \
	io_uring/eventfd.c \
	io_uring/uring_cmd.c \
	io_uring/openclose.c \
	io_uring/sqpoll.c \
	io_uring/xattr.c \
	io_uring/nop.c \
	io_uring/fs.c \
	io_uring/splice.c \
	io_uring/sync.c \
	io_uring/msg_ring.c \
	io_uring/advise.c \
	io_uring/epoll.c \
	io_uring/statx.c \
	io_uring/timeout.c \
	io_uring/fdinfo.c \
	io_uring/cancel.c \
	io_uring/waitid.c \
	io_uring/register.c \
	io_uring/truncate.c \
	io_uring/memmap.c \
	io_uring/io-wq.c \
	io_uring/futex.c \
	io_uring/napi.c \
	net/devres.c \
	net/socket.c \
	net/sysctl_net.c \
	net/core/sock.c \
	net/core/request_sock.c \
	net/core/skbuff.c \
	net/core/datagram.c \
	net/core/stream.c \
	net/core/scm.c \
	net/core/gen_stats.c \
	net/core/gen_estimator.c \
	net/core/net_namespace.c \
	net/core/secure_seq.c \
	net/core/flow_dissector.c \
	net/core/sysctl_net_core.c \
	net/core/dev.c \
	net/core/dev_addr_lists.c \
	net/core/dst.c \
	net/core/netevent.c \
	net/core/neighbour.c \
	net/core/rtnetlink.c \
	net/core/utils.c \
	net/core/link_watch.c \
	net/core/filter.c \
	net/core/sock_diag.c \
	net/core/dev_ioctl.c \
	net/core/tso.c \
	net/core/sock_reuseport.c \
	net/core/fib_notifier.c \
	net/core/xdp.c \
	net/core/flow_offload.c \
	net/core/gro.c \
	net/core/netdev-genl.c \
	net/core/netdev-genl-gen.c \
	net/core/gso.c \
	net/core/net-sysfs.c \
	net/core/hotdata.c \
	net/core/netdev_rx_queue.c \
	net/core/net-procfs.c \
	net/core/of_net.c \
	net/core/dst_cache.c \
	net/core/gro_cells.c \
	net/netlink/af_netlink.c \
	net/netlink/genetlink.c \
	net/netlink/policy.c \
	net/ethernet/eth.c \
	net/sched/sch_generic.c \
	net/sched/sch_mq.c \
	net/sched/sch_frag.c \
	net/ethtool/ioctl.c \
	net/ethtool/common.c \
	net/ethtool/netlink.c \
	net/ethtool/bitset.c \
	net/ethtool/strset.c \
	net/ethtool/linkinfo.c \
	net/ethtool/linkmodes.c \
	net/ethtool/rss.c \
	net/ethtool/linkstate.c \
	net/ethtool/debug.c \
	net/ethtool/wol.c \
	net/ethtool/features.c \
	net/ethtool/privflags.c \
	net/ethtool/rings.c \
	net/ethtool/channels.c \
	net/ethtool/coalesce.c \
	net/ethtool/pause.c \
	net/ethtool/eee.c \
	net/ethtool/tsinfo.c \
	net/ethtool/cabletest.c \
	net/ethtool/tunnels.c \
	net/ethtool/fec.c \
	net/ethtool/eeprom.c \
	net/ethtool/stats.c \
	net/ethtool/phc_vclocks.c \
	net/ethtool/mm.c \
	net/ethtool/module.c \
	net/ethtool/cmis_fw_update.c \
	net/ethtool/cmis_cdb.c \
	net/ethtool/pse-pd.c \
	net/ethtool/plca.c \
	net/ethtool/phy.c \
	net/ipv4/route.c \
	net/ipv4/inetpeer.c \
	net/ipv4/protocol.c \
	net/ipv4/ip_input.c \
	net/ipv4/ip_fragment.c \
	net/ipv4/ip_forward.c \
	net/ipv4/ip_options.c \
	net/ipv4/ip_output.c \
	net/ipv4/ip_sockglue.c \
	net/ipv4/inet_hashtables.c \
	net/ipv4/inet_timewait_sock.c \
	net/ipv4/inet_connection_sock.c \
	net/ipv4/tcp.c \
	net/ipv4/tcp_input.c \
	net/ipv4/tcp_output.c \
	net/ipv4/tcp_timer.c \
	net/ipv4/tcp_ipv4.c \
	net/ipv4/tcp_minisocks.c \
	net/ipv4/tcp_cong.c \
	net/ipv4/tcp_metrics.c \
	net/ipv4/tcp_fastopen.c \
	net/ipv4/tcp_rate.c \
	net/ipv4/tcp_recovery.c \
	net/ipv4/tcp_ulp.c \
	net/ipv4/tcp_offload.c \
	net/ipv4/tcp_plb.c \
	net/ipv4/datagram.c \
	net/ipv4/raw.c \
	net/ipv4/udp.c \
	net/ipv4/udplite.c \
	net/ipv4/udp_offload.c \
	net/ipv4/arp.c \
	net/ipv4/icmp.c \
	net/ipv4/devinet.c \
	net/ipv4/af_inet.c \
	net/ipv4/igmp.c \
	net/ipv4/fib_frontend.c \
	net/ipv4/fib_semantics.c \
	net/ipv4/fib_trie.c \
	net/ipv4/fib_notifier.c \
	net/ipv4/inet_fragment.c \
	net/ipv4/ping.c \
	net/ipv4/ip_tunnel_core.c \
	net/ipv4/gre_offload.c \
	net/ipv4/metrics.c \
	net/ipv4/netlink.c \
	net/ipv4/nexthop.c \
	net/ipv4/udp_tunnel_stub.c \
	net/ipv4/ip_tunnel.c \
	net/ipv4/sysctl_net_ipv4.c \
	net/ipv4/proc.c \
	net/ipv4/tunnel4.c \
	net/ipv4/inet_diag.c \
	net/ipv4/tcp_diag.c \
	net/ipv4/tcp_cubic.c \
	net/unix/af_unix.c \
	net/unix/garbage.c \
	net/unix/sysctl_net_unix.c \
	net/ipv6/af_inet6.c \
	net/ipv6/anycast.c \
	net/ipv6/ip6_output.c \
	net/ipv6/ip6_input.c \
	net/ipv6/addrconf.c \
	net/ipv6/addrlabel.c \
	net/ipv6/route.c \
	net/ipv6/ip6_fib.c \
	net/ipv6/ipv6_sockglue.c \
	net/ipv6/ndisc.c \
	net/ipv6/udp.c \
	net/ipv6/udplite.c \
	net/ipv6/raw.c \
	net/ipv6/icmp.c \
	net/ipv6/mcast.c \
	net/ipv6/reassembly.c \
	net/ipv6/tcp_ipv6.c \
	net/ipv6/ping.c \
	net/ipv6/exthdrs.c \
	net/ipv6/datagram.c \
	net/ipv6/ip6_flowlabel.c \
	net/ipv6/inet6_connection_sock.c \
	net/ipv6/udp_offload.c \
	net/ipv6/seg6.c \
	net/ipv6/fib6_notifier.c \
	net/ipv6/rpl.c \
	net/ipv6/ioam6.c \
	net/ipv6/sysctl_net_ipv6.c \
	net/ipv6/proc.c \
	net/ipv6/sit.c \
	net/ipv6/addrconf_core.c \
	net/ipv6/exthdrs_core.c \
	net/ipv6/ip6_checksum.c \
	net/ipv6/ip6_icmp.c \
	net/ipv6/output_core.c \
	net/ipv6/protocol.c \
	net/ipv6/ip6_offload.c \
	net/ipv6/tcpv6_offload.c \
	net/ipv6/exthdrs_offload.c \
	net/ipv6/inet6_hashtables.c \
	net/ipv6/mcast_snoop.c \
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
	fs/proc/task_mmu.c \
	fs/proc/base.c \
	fs/proc/array.c \
	fs/proc/fd.c \
	fs/proc/cmdline.c \
	fs/proc/consoles.c \
	fs/proc/cpuinfo.c \
	fs/proc/devices.c \
	fs/proc/interrupts.c \
	fs/proc/loadavg.c \
	fs/proc/meminfo.c \
	fs/proc/stat.c \
	fs/proc/uptime.c \
	fs/proc/version.c \
	fs/proc/softirqs.c \
	fs/proc/namespaces.c \
	fs/proc/self.c \
	fs/proc/thread_self.c \
	fs/proc/proc_sysctl.c \
	fs/proc/proc_net.c \
	fs/proc/proc_tty.c \
	fs/proc/kmsg.c \
	fs/proc/page.c \
	fs/proc/root.c \
	fs/proc/util.c \
	fs/seq_file.c \
	fs/exportfs/expfs.c \
	lib/fdt.c \
	lib/fdt_ro.c \
	lib/fdt_wip.c \
	lib/fdt_sw.c \
	lib/fdt_rw.c \
	lib/fdt_strerror.c \
	lib/fdt_empty_tree.c \
	lib/fdt_addresses.c
ORLIX_KUNIT_BUILD_DIR := $(CURDIR)/Build/OrlixKernel/kunit/$(PROFILE)
ORLIX_MLIBC_KERNEL_HEADERS_DIR := $(CURDIR)/Build/OrlixMLibC/kernel-headers/$(PROFILE)
ORLIX_MLIBC_SYSROOT ?= Build/OrlixMLibC/sysroot/$(PROFILE)
ORLIX_MLIBC_KSELFTEST_INSTALL_DIR := $(CURDIR)/Build/OrlixMLibC/kselftest/$(PROFILE)

ORLIX_KSELFTEST_ARCH ?= arm64
ORLIX_HOSTED_USER_BASE_ADDRESS ?= 0x0000600000000000

ORLIX_XCODE_PROJECT ?= OrlixSystem.xcodeproj
ORLIX_IOS_SIMULATOR_NAME ?= iPhone 17 Pro
ORLIX_IOS_SIMULATOR_ID ?=
ORLIX_IOS_SIMULATOR_DERIVED_DATA ?= $(CURDIR)/.deriveddata/OrlixSystem-sim
ORLIX_IOS_SIMULATOR_FRAMEWORK := $(ORLIX_IOS_SIMULATOR_DERIVED_DATA)/Build/Products/Debug-iphonesimulator/OrlixKernel.framework
ORLIX_IOS_SIMULATOR_RUN_LOG_DIR ?= $(CURDIR)/Build/OrlixKernel/run/$(PROFILE)
ORLIX_TERMINAL_BUNDLE_ID ?= org.orlix.OrlixTerminal
ORLIX_KERNEL_RUN_UNTIL_MARKER ?=
ORLIX_KERNEL_RUN_TIMEOUT_SECONDS ?= 120
ORLIX_KERNEL_RUN_STARTUP_TIMEOUT_SECONDS ?= 30
ORLIX_OS_TARGET_SETTINGS ?= OrlixOS/Sources/distribution/target-settings.xcconfig
ifneq ($(wildcard $(ORLIX_OS_TARGET_SETTINGS)),)
include $(ORLIX_OS_TARGET_SETTINGS)
endif
ORLIX_MLIBC_TEST_INITRAMFS_BUNDLE_NAME ?= $(ORLIX_OS_KSELFTEST_INITRAMFS_BUNDLE_NAME)
ORLIX_MLIBC_TEST_INITRAMFS_BUNDLE_EXTENSION ?= $(ORLIX_OS_TEST_INITRAMFS_BUNDLE_EXTENSION)
ORLIX_MLIBC_TEST_INITRAMFS_BUNDLE_IDENTIFIER ?= $(ORLIX_OS_KSELFTEST_INITRAMFS_BUNDLE_IDENTIFIER)
ORLIX_MLIBC_TEST_INITRAMFS_DIR := $(CURDIR)/Build/OrlixMLibC/test-initramfs/$(PROFILE)/$(ORLIX_MLIBC_TEST_INITRAMFS_BUNDLE_NAME).$(ORLIX_MLIBC_TEST_INITRAMFS_BUNDLE_EXTENSION)
ORLIX_KERNEL_PAYLOAD_BUNDLE_NAME ?= $(ORLIX_OS_PAYLOAD_SOURCE_BUNDLE_NAME)
ORLIX_KERNEL_PAYLOAD_BUNDLE_EXTENSION ?= $(ORLIX_OS_PAYLOAD_BUNDLE_EXTENSION)
ORLIX_KERNEL_PAYLOAD_BUNDLE_IDENTIFIER ?= $(ORLIX_OS_PAYLOAD_SOURCE_BUNDLE_IDENTIFIER)
ORLIX_KERNEL_PAYLOAD_SELECTED_PROFILE_INFO_KEY ?= $(ORLIX_OS_PAYLOAD_SELECTED_PROFILE_INFO_KEY)
ORLIX_KERNEL_PAYLOAD_KERNEL_COMMAND_LINE_INFO_KEY ?= $(ORLIX_OS_PAYLOAD_KERNEL_COMMAND_LINE_INFO_KEY)
ORLIX_KERNEL_PAYLOAD_ROOT_INITRAMFS_INFO_KEY ?= $(ORLIX_OS_PAYLOAD_ROOT_INITRAMFS_INFO_KEY)
ORLIX_KERNEL_PAYLOAD_BASE_ROOT_IMAGE_INFO_KEY ?= $(ORLIX_OS_PAYLOAD_BASE_ROOT_IMAGE_INFO_KEY)
ORLIX_KERNEL_PAYLOAD_STATE_ROOT_IMAGE_INFO_KEY ?= $(ORLIX_OS_PAYLOAD_STATE_ROOT_IMAGE_INFO_KEY)
ORLIX_KERNEL_PAYLOAD_BASE_ROOT_DEVICE_INFO_KEY ?= $(ORLIX_OS_PAYLOAD_BASE_ROOT_DEVICE_INFO_KEY)
ORLIX_KERNEL_PAYLOAD_STATE_ROOT_DEVICE_INFO_KEY ?= $(ORLIX_OS_PAYLOAD_STATE_ROOT_DEVICE_INFO_KEY)
ORLIX_KERNEL_PAYLOAD_BASE_ROOT_HOST_BLOCK_DEVICE_INFO_KEY ?= $(ORLIX_OS_PAYLOAD_BASE_ROOT_HOST_BLOCK_DEVICE_INFO_KEY)
ORLIX_KERNEL_PAYLOAD_STATE_ROOT_HOST_BLOCK_DEVICE_INFO_KEY ?= $(ORLIX_OS_PAYLOAD_STATE_ROOT_HOST_BLOCK_DEVICE_INFO_KEY)
ORLIX_KERNEL_PAYLOAD_STATE_ROOT_MINIMUM_BYTES_INFO_KEY ?= $(ORLIX_OS_PAYLOAD_STATE_ROOT_MINIMUM_BYTES_INFO_KEY)
ORLIX_KERNEL_LINUX_PAGE_SIZE ?= $(ORLIX_OS_LINUX_PAGE_SIZE)
ORLIX_KERNEL_ROOT_INITRAMFS_RESOURCE ?= $(ORLIX_OS_ROOT_INITRAMFS_RESOURCE)
ORLIX_KERNEL_BASE_ROOT_IMAGE_RESOURCE ?= $(ORLIX_OS_BASE_ROOT_IMAGE_RESOURCE)
ORLIX_KERNEL_STATE_ROOT_IMAGE_RESOURCE ?= $(ORLIX_OS_STATE_ROOT_IMAGE_RESOURCE)
ORLIX_KERNEL_PAYLOAD_DIR := $(CURDIR)/Build/OrlixKernel/payload/$(ORLIX_KERNEL_PAYLOAD_BUNDLE_NAME).$(ORLIX_KERNEL_PAYLOAD_BUNDLE_EXTENSION)
ORLIX_KERNEL_ROOTFS_BUILD_DIR := $(CURDIR)/Build/OrlixKernel/rootfs/$(PROFILE)
ORLIX_KERNEL_BASE_ROOT_IMAGE := $(ORLIX_KERNEL_ROOTFS_BUILD_DIR)/$(notdir $(ORLIX_KERNEL_BASE_ROOT_IMAGE_RESOURCE))
ORLIX_KERNEL_STATE_ROOT_IMAGE := $(ORLIX_KERNEL_ROOTFS_BUILD_DIR)/$(notdir $(ORLIX_KERNEL_STATE_ROOT_IMAGE_RESOURCE))
ORLIX_KERNEL_BASE_ROOT_TREE_INPUT ?=
ORLIX_KERNEL_STATE_ROOT_TREE_INPUT ?=
ORLIX_KERNEL_ROOT_INITRAMFS_INPUT ?=
ORLIX_KERNEL_ROOT_MODES ?= $(ORLIX_OS_ROOT_MODES)
ifeq ($(PROFILE),development)
ORLIX_KERNEL_SELECTED_ROOT_MODE ?= $(ORLIX_OS_DEVELOPMENT_ROOT_MODE)
ORLIX_KERNEL_COMMAND_LINE ?= $(ORLIX_OS_DEVELOPMENT_KERNEL_COMMAND_LINE)
ORLIX_KERNEL_TEST_INITRAMFS_COMMAND_LINE ?= $(ORLIX_OS_DEVELOPMENT_TEST_INITRAMFS_KERNEL_COMMAND_LINE)
else
ORLIX_KERNEL_SELECTED_ROOT_MODE ?= $(ORLIX_OS_RELEASE_ROOT_MODE)
ORLIX_KERNEL_COMMAND_LINE ?= $(ORLIX_OS_RELEASE_KERNEL_COMMAND_LINE)
ORLIX_KERNEL_TEST_INITRAMFS_COMMAND_LINE ?= $(ORLIX_OS_RELEASE_TEST_INITRAMFS_KERNEL_COMMAND_LINE)
endif
ORLIX_KERNEL_BASE_ROOT_DEVICE ?= $(ORLIX_OS_BASE_ROOT_DEVICE)
ORLIX_KERNEL_STATE_ROOT_DEVICE ?= $(ORLIX_OS_STATE_ROOT_DEVICE)
ORLIX_KERNEL_BASE_ROOT_HOST_BLOCK_DEVICE ?= $(ORLIX_OS_BASE_ROOT_HOST_BLOCK_DEVICE)
ORLIX_KERNEL_STATE_ROOT_HOST_BLOCK_DEVICE ?= $(ORLIX_OS_STATE_ROOT_HOST_BLOCK_DEVICE)
ORLIX_KERNEL_BASE_ROOT_IMAGE_SIZE ?= $(ORLIX_OS_BASE_ROOT_IMAGE_SIZE)
ORLIX_KERNEL_STATE_ROOT_IMAGE_SIZE ?= $(ORLIX_OS_STATE_ROOT_IMAGE_SIZE)
ORLIX_KERNEL_STATE_ROOT_MINIMUM_BYTES ?= $(ORLIX_OS_STATE_ROOT_MINIMUM_BYTES)
ORLIX_KERNEL_XCFRAMEWORK ?= $(CURDIR)/Build/OrlixKernel/xcframework/OrlixKernel.xcframework
XCODEGEN ?= xcodegen
XCODEBUILD_MCP ?= xcodebuildmcp

LINUX_MAKE ?=
LINUX_SED ?=
LINUX_LLVM_BIN ?= $(shell if command -v llvm-ar >/dev/null 2>&1; then dirname "$$(command -v llvm-ar)"; elif [ -x /opt/homebrew/opt/llvm/bin/llvm-ar ]; then printf '%s\n' /opt/homebrew/opt/llvm/bin; fi)
LINUX_HOST_COMPAT_INCLUDE_ROOT := $(CURDIR)/tools/linux_host_compat/include
ORLIX_KERNEL_CC ?= clang
ORLIX_KERNEL_HOSTCC ?= cc
ORLIX_KERNEL_AR ?= llvm-ar
ORLIX_MKE2FS ?= $(shell if command -v mke2fs >/dev/null 2>&1; then command -v mke2fs; elif [ -x /opt/homebrew/opt/e2fsprogs/sbin/mke2fs ]; then printf '%s\n' /opt/homebrew/opt/e2fsprogs/sbin/mke2fs; else printf '%s\n' mke2fs; fi)
ORLIX_KERNEL_NM ?= nm
ORLIX_KERNEL_OTOOL ?= otool

TEST_TYPES := $(strip $(subst $(comma),$(space),$(type)))

ifeq ($(libc),orlixmlibc)
KSELFTEST_SYSROOT := $(ORLIX_MLIBC_SYSROOT)
KSELFTEST_INSTALL_DIR := $(ORLIX_MLIBC_KSELFTEST_INSTALL_DIR)
KSELFTEST_INITRAMFS_DIR := $(ORLIX_MLIBC_TEST_INITRAMFS_DIR)
KSELFTEST_HEADER_FLAGS := -isystem $(ORLIX_MLIBC_KERNEL_HEADERS_DIR)/include
KSELFTEST_PROOF_LABEL := orlixmlibc-kselftest-syscall-uapi
KSELFTEST_PREREQS := __orlixmlibc-sysroot
else
KSELFTEST_SYSROOT :=
KSELFTEST_INSTALL_DIR :=
KSELFTEST_INITRAMFS_DIR :=
KSELFTEST_HEADER_FLAGS :=
KSELFTEST_PROOF_LABEL :=
KSELFTEST_PREREQS :=
endif

ORLIX_KERNEL_TEST_INITRAMFS_INPUT :=
ORLIX_KERNEL_PAYLOAD_PREREQS := __prepare-kbuild
ifneq (,$(filter kselftest,$(TEST_TYPES)))
ORLIX_KERNEL_TEST_INITRAMFS_INPUT := $(KSELFTEST_INITRAMFS_DIR)/rootfs/initramfs.cpio.gz
ORLIX_KERNEL_PAYLOAD_PREREQS := __prepare-kbuild kselftest
endif

include OrlixKernel/Sources/ports/orlix/kbuild/product-compile-adapter.mk

.PHONY: all setup-env build test clean mrproper help prepare scripts dtbs headers_install kunit kselftest kselftest-install xcodeproj run __xcodeproj-generate __bootstrap-linux-upstream __validate-linux-abi __validate-profile __prepare-port __prepare-kbuild __headers-install __kunit __kernel-archive __verify-xcodegen-boundary __verify-framework-symbols __orlixmlibc-sysroot __kselftest-install __kselftest-initramfs __kernel-payload __ios-simulator-framework __ios-simulator-xcframework
all: build

help:
	@printf '%s\n' 'Targets:'
	@printf '%s\n' '  setup-env                 fetch upstream Linux and generate the Xcode project'
	@printf '%s\n' '  build                     build the app-hosted OrlixKernel iOS artifact'
	@printf '%s\n' '  test                      run test type(s), default: type=kunit'
	@printf '%s\n' '  test type=kunit           build Linux KUnit-selected Orlix tests'
	@printf '%s\n' '  test type=kunit,kselftest build KUnit and Linux kselftest artifacts'
	@printf '%s\n' '  kselftest                 install OrlixMLibC-built kselftests'
	@printf '%s\n' '  headers_install           install Linux UAPI headers for OrlixMLibC'
	@printf '%s\n' '  clean                     remove normal generated outputs'
	@printf '%s\n' '  mrproper                  remove all generated local outputs'

setup-env: __bootstrap-linux-upstream xcodeproj

build: clean __ios-simulator-xcframework

prepare scripts dtbs: __prepare-kbuild

headers_install: __headers-install

kunit: __kunit

kselftest-install: __kselftest-install

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

xcodeproj: __xcodeproj-generate __verify-xcodegen-boundary

__xcodeproj-generate:
	@set -euo pipefail; \
	command -v "$(XCODEGEN)" >/dev/null 2>&1 || { echo "XcodeGen is required; install xcodegen or set XCODEGEN=/path/to/xcodegen" >&2; exit 1; }; \
	"$(XCODEGEN)" generate --spec project.yml

run: __ios-simulator-framework xcodeproj
	@set -euo pipefail; \
	command -v "$(XCODEBUILD_MCP)" >/dev/null 2>&1 || { echo "XcodeBuildMCP is required; install xcodebuildmcp or set XCODEBUILD_MCP=/path/to/xcodebuildmcp" >&2; exit 1; }; \
	command -v xcrun >/dev/null 2>&1 || { echo "xcrun is required to launch OrlixTerminal in the simulator" >&2; exit 1; }; \
	expected_rootfs_input="$(ORLIX_KERNEL_TEST_INITRAMFS_INPUT)"; \
	expected_base_root_tree_input="$(ORLIX_KERNEL_BASE_ROOT_TREE_INPUT)"; \
	expected_state_root_tree_input="$(ORLIX_KERNEL_STATE_ROOT_TREE_INPUT)"; \
	if [ -z "$$expected_rootfs_input" ] && [ -n "$(ORLIX_KERNEL_ROOT_INITRAMFS_INPUT)" ]; then expected_rootfs_input="$(ORLIX_KERNEL_ROOT_INITRAMFS_INPUT)"; fi; \
	if [ -n "$$expected_rootfs_input" ]; then \
		$(MAKE) -f OrlixKernel/Makefile __kernel-payload \
			PROFILE="$(PROFILE)" \
			type="$(type)" \
			libc="$(libc)" \
			ORLIX_KERNEL_TEST_INITRAMFS_INPUT="$(ORLIX_KERNEL_TEST_INITRAMFS_INPUT)" \
			ORLIX_KERNEL_ROOT_INITRAMFS_INPUT="$(ORLIX_KERNEL_ROOT_INITRAMFS_INPUT)" \
			ORLIX_KERNEL_BASE_ROOT_TREE_INPUT="$(ORLIX_KERNEL_BASE_ROOT_TREE_INPUT)" \
			ORLIX_KERNEL_STATE_ROOT_TREE_INPUT="$(ORLIX_KERNEL_STATE_ROOT_TREE_INPUT)"; \
	fi; \
	selector=(); \
	install_selector=(); \
	simctl_device="booted"; \
	simctl_boot_device="$(ORLIX_IOS_SIMULATOR_NAME)"; \
	if [ -n "$(ORLIX_IOS_SIMULATOR_ID)" ]; then \
		selector=(--simulator-id "$(ORLIX_IOS_SIMULATOR_ID)"); \
		install_selector=(--simulator-id "$(ORLIX_IOS_SIMULATOR_ID)"); \
		simctl_device="$(ORLIX_IOS_SIMULATOR_ID)"; \
		simctl_boot_device="$(ORLIX_IOS_SIMULATOR_ID)"; \
	else \
		selector=(--simulator-name "$(ORLIX_IOS_SIMULATOR_NAME)" --use-latest-os); \
		install_selector=(--simulator-name "$(ORLIX_IOS_SIMULATOR_NAME)"); \
	fi; \
	ORLIX_PROFILE="$(PROFILE)" \
	ORLIX_OS_EXPECTED_ROOTFS_INPUT="$$expected_rootfs_input" \
	ORLIX_OS_EXPECTED_BASE_ROOT_TREE_INPUT="$$expected_base_root_tree_input" \
	ORLIX_OS_EXPECTED_STATE_ROOT_TREE_INPUT="$$expected_state_root_tree_input" \
	"$(XCODEBUILD_MCP)" simulator build \
		--project-path "$(CURDIR)/$(ORLIX_XCODE_PROJECT)" \
		--scheme "OrlixTerminal" \
		--configuration "Debug" \
		--derived-data-path "$(ORLIX_IOS_SIMULATOR_DERIVED_DATA)" \
		"$${selector[@]}" \
		--output json; \
	app_json="$$(ORLIX_PROFILE="$(PROFILE)" "$(XCODEBUILD_MCP)" simulator get-app-path \
		--project-path "$(CURDIR)/$(ORLIX_XCODE_PROJECT)" \
		--scheme "OrlixTerminal" \
		--configuration "Debug" \
		--platform "iOS Simulator" \
		--derived-data-path "$(ORLIX_IOS_SIMULATOR_DERIVED_DATA)" \
		"$${selector[@]}" \
		--output json)"; \
	app_path="$$(printf '%s\n' "$$app_json" | awk -F'"' '/appPath/ { print $$4; exit }')"; \
	[ -n "$$app_path" ] || { echo "missing OrlixTerminal app path" >&2; printf '%s\n' "$$app_json" >&2; exit 1; }; \
	xcrun simctl boot "$$simctl_boot_device" >/dev/null 2>&1 || true; \
	xcrun simctl bootstatus "$$simctl_boot_device" -b >/dev/null; \
	ORLIX_PROFILE="$(PROFILE)" "$(XCODEBUILD_MCP)" simulator install \
		"$${install_selector[@]}" \
		--app-path "$$app_path" \
		--output json; \
	mkdir -p "$(ORLIX_IOS_SIMULATOR_RUN_LOG_DIR)"; \
	runtime_log="$(ORLIX_IOS_SIMULATOR_RUN_LOG_DIR)/OrlixTerminal-runtime.log"; \
	os_log="$(ORLIX_IOS_SIMULATOR_RUN_LOG_DIR)/OrlixTerminal-os.log"; \
	: > "$$runtime_log"; \
	: > "$$os_log"; \
	xcrun simctl spawn "$$simctl_device" log stream --style compact --predicate 'process == "OrlixTerminal" || subsystem == "org.orlix.OrlixTerminal"' >> "$$runtime_log" 2>&1 & \
	log_pid="$$!"; \
	xcrun simctl launch --terminate-running-process --console "$$simctl_device" "$(ORLIX_TERMINAL_BUNDLE_ID)" >> "$$runtime_log" 2>&1 & \
	launch_pid="$$!"; \
	cleanup() { kill "$$log_pid" "$$launch_pid" >/dev/null 2>&1 || true; }; \
	trap cleanup EXIT INT TERM; \
	app_has_started() { grep -E -q 'OrlixTerminal\[|Starting Orlix bootloader|ORLIX-COREUTILS-TEST-INIT' "$$runtime_log" || kill -0 "$$launch_pid" >/dev/null 2>&1; }; \
	validate_runtime_log() { \
		tr -d '\r' < "$$runtime_log" | awk 'BEGIN { bad = 0 } /(^|[^[:alnum:]_])not ok[[:space:]]+[0-9]+([[:space:]-]|$$)/ { print "upstream failure marker: " $$0 > "/dev/stderr"; bad = 1 } /Kernel panic|kernel panic|panic:|Oops|BUG:|Out of memory|Killed process|Attempted to kill init/ { print "fatal runtime marker: " $$0 > "/dev/stderr"; bad = 1 } END { exit bad ? 1 : 0 }'; \
	}; \
	printf '{"runtimeLogPath":"%s","osLogPath":"%s","bundleId":"%s"}\n' "$$runtime_log" "$$os_log" "$(ORLIX_TERMINAL_BUNDLE_ID)"; \
	if [ -n "$(ORLIX_KERNEL_RUN_UNTIL_MARKER)" ]; then \
		for _ in $$(seq 1 "$(ORLIX_KERNEL_RUN_STARTUP_TIMEOUT_SECONDS)"); do \
			grep -F -q "$(ORLIX_KERNEL_RUN_UNTIL_MARKER)" "$$runtime_log" && break; \
			app_has_started && break; \
			sleep 1; \
		done; \
		if ! grep -F -q "$(ORLIX_KERNEL_RUN_UNTIL_MARKER)" "$$runtime_log" && ! app_has_started; then \
			echo "OrlixTerminal did not start before marker $(ORLIX_KERNEL_RUN_UNTIL_MARKER): $$runtime_log" >&2; \
			exit 1; \
		fi; \
		for _ in $$(seq 1 "$(ORLIX_KERNEL_RUN_TIMEOUT_SECONDS)"); do \
			grep -F -q "$(ORLIX_KERNEL_RUN_UNTIL_MARKER)" "$$runtime_log" && break; \
			if ! kill -0 "$$launch_pid" >/dev/null 2>&1; then \
				echo "OrlixTerminal exited before marker $(ORLIX_KERNEL_RUN_UNTIL_MARKER): $$runtime_log" >&2; \
				exit 1; \
			fi; \
			sleep 1; \
		done; \
		grep -F -q "$(ORLIX_KERNEL_RUN_UNTIL_MARKER)" "$$runtime_log" || { echo "timed out waiting for marker $(ORLIX_KERNEL_RUN_UNTIL_MARKER): $$runtime_log" >&2; exit 1; }; \
	else \
		sleep "$(ORLIX_KERNEL_RUN_TIMEOUT_SECONDS)"; \
	fi; \
	validate_runtime_log

clean:
	@set -euo pipefail; \
	$(call orlix_kernel_acquire_profile_lock); \
	for path in Build/OrlixKernel .deriveddata/OrlixSystem-sim; do \
		if [ -L "$$path" ]; then echo "refusing to clean symlinked path: $$path" >&2; exit 1; fi; \
		rm -rf "$$path"; \
	done; \
	echo "cleaned generated OrlixKernel outputs"

mrproper: clean
	@set -euo pipefail; \
	for path in .deriveddata OrlixSystem.xcodeproj; do \
		if [ -L "$$path" ]; then echo "refusing to remove symlinked path: $$path" >&2; exit 1; fi; \
		rm -rf "$$path"; \
	done; \
	echo "removed generated local outputs"

__bootstrap-linux-upstream:
	@set -euo pipefail; \
	linux_remote="$(LINUX_REMOTE)"; \
	linux_tag="$(LINUX_TAG)"; \
	upstream_dir="$(LINUX_UPSTREAM_DIR)"; \
	expected_upstream_dir="Build/OrlixKernel/upstream/linux-$(LINUX_VERSION).git"; \
	if [ "$$upstream_dir" != "$$expected_upstream_dir" ]; then \
		echo "Linux upstream directory must be $$expected_upstream_dir: $$upstream_dir" >&2; \
		exit 1; \
	fi; \
	for path in Build Build/OrlixKernel Build/OrlixKernel/upstream "$$upstream_dir"; do \
		if [ -L "$$path" ]; then echo "refusing to use symlinked path: $$path" >&2; exit 1; fi; \
	done; \
	mkdir -p "$$(dirname "$$upstream_dir")"; \
	if [ ! -d "$$upstream_dir/objects" ]; then \
		rm -rf "$$upstream_dir"; \
		git init --bare "$$upstream_dir" >/dev/null; \
		git -C "$$upstream_dir" remote add origin "$$linux_remote"; \
	else \
		git -C "$$upstream_dir" remote set-url origin "$$linux_remote"; \
	fi; \
	git -C "$$upstream_dir" fetch --force --depth 1 origin "refs/tags/$$linux_tag:refs/tags/$$linux_tag"; \
	tag_commit="$$(git -C "$$upstream_dir" rev-list -n1 "$$linux_tag")"; \
	git -C "$$upstream_dir" update-ref "refs/orlix/linux-$(LINUX_VERSION)" "$$tag_commit"; \
	git -C "$$upstream_dir" symbolic-ref HEAD "refs/orlix/linux-$(LINUX_VERSION)"; \
	checked_commit="$$(git -C "$$upstream_dir" rev-list -n1 "refs/orlix/linux-$(LINUX_VERSION)")"; \
	checked_tag="$$linux_tag"; \
	if [ "$$checked_tag" != "$$linux_tag" ] || [ "$$checked_commit" != "$$tag_commit" ]; then \
		echo "expected $$linux_tag at $$tag_commit but checked out $$checked_tag at $$checked_commit" >&2; \
		exit 1; \
	fi; \
	echo "upstream Linux bare clone ready: $$upstream_dir ($$checked_tag)"

__validate-linux-abi:
	@set -euo pipefail; \
	[ "$(LINUX_UAPI_ARCH)" = arm64 ] || { echo "LINUX_UAPI_ARCH must remain upstream Linux arm64, got: $(LINUX_UAPI_ARCH)" >&2; exit 1; }; \
	pattern='ARCH[[:space:]]*=[[:space:]]*"?or''lix'; \
	if rg -n "$$pattern" OrlixMLibC OrlixOS OrlixKernel/Sources/ports/orlix \
		--glob '!kbuild/kernel-rules.mk' \
		--glob 'Makefile' \
		--glob '*.mk' \
		--glob '*.sh' \
		--glob '*.cross' \
		--glob '*.pc'; then \
		echo "do not use an Orlix-specific Kbuild architecture as the Linux ABI; Orlix uses upstream Linux arm64 UAPI and aarch64-linux-gnu userspace" >&2; \
		exit 1; \
	fi

__validate-profile: __validate-linux-abi
	@set -euo pipefail; \
	profile="$(PROFILE)"; \
	case " $(ORLIX_PROFILES) " in \
		*" $$profile "*) ;; \
		*) echo "unsupported PROFILE=$$profile (expected one of: $(ORLIX_PROFILES))" >&2; exit 1 ;; \
	esac; \
	case "$(ORLIX_KERNEL_LINUX_PAGE_SIZE)" in \
		4096|16384) ;; \
		"") echo "missing OrlixOS target Linux page size: ORLIX_OS_LINUX_PAGE_SIZE" >&2; exit 1 ;; \
		*) echo "unsupported OrlixOS target Linux page size: $(ORLIX_KERNEL_LINUX_PAGE_SIZE)" >&2; exit 1 ;; \
	esac; \
	config="$(ORLIX_PROFILE_CONFIG)"; \
	[ -f "$$config" ] || { echo "missing profile defconfig: $$config" >&2; exit 1; }

__prepare-port: __validate-profile __bootstrap-linux-upstream
	@set -euo pipefail; \
	$(call orlix_kernel_acquire_profile_lock); \
	upstream_dir="$(LINUX_UPSTREAM_DIR)"; \
	port_dir="$(ORLIX_KERNEL_PORT_DIR)"; \
	expected_port_dir="Build/OrlixKernel/src/linux-$(LINUX_VERSION)-port"; \
	overlay_dir="$(ORLIX_LINUX_OVERLAY)"; \
	patch_dir="$(ORLIX_LINUX_PATCH_DIR)"; \
	profile_config="$(ORLIX_PROFILE_CONFIG)"; \
	linux_page_size="$(ORLIX_KERNEL_LINUX_PAGE_SIZE)"; \
	exception_dir="$$patch_dir/exceptions"; \
	port_filelist="$(ORLIX_KERNEL_BUILD_ROOT)/linux-$(LINUX_VERSION)-port-source-files.$$$$.txt"; \
	port_stamp="$$port_dir/.orlix-port-profile"; \
	port_tmp_dir="$$port_dir.tmp.$$$$"; \
	forbidden_re='^(fs|kernel|mm|ipc|net|include/linux|include/uapi)(/|$$)'; \
	trap 'rm -rf "$$port_tmp_dir"; rm -f "$$port_filelist"' EXIT; \
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
	port_inputs_changed=0; \
	if [ -s "$$port_stamp" ]; then \
		if find "$$overlay_dir" -type f -newer "$$port_stamp" -print -quit | grep -q .; then port_inputs_changed=1; fi; \
		if [ -d "$$patch_dir" ] && find "$$patch_dir" -type f -newer "$$port_stamp" -print -quit | grep -q .; then port_inputs_changed=1; fi; \
		if [ "$$profile_config" -nt "$$port_stamp" ]; then port_inputs_changed=1; fi; \
		if [ -f "$(ORLIX_OS_TARGET_SETTINGS)" ] && [ "$(ORLIX_OS_TARGET_SETTINGS)" -nt "$$port_stamp" ]; then port_inputs_changed=1; fi; \
		if [ "OrlixKernel/Sources/ports/orlix/kbuild/kernel-rules.mk" -nt "$$port_stamp" ]; then port_inputs_changed=1; fi; \
	fi; \
	if [ -s "$$port_stamp" ] && \
		[ "$$port_inputs_changed" -eq 0 ] && \
		grep -Fxq "linux_version=$(LINUX_VERSION)" "$$port_stamp" && \
		grep -Fxq "profile=$(PROFILE)" "$$port_stamp" && \
		grep -Fxq "linux_uapi_arch=$(LINUX_UAPI_ARCH)" "$$port_stamp" && \
		grep -Fxq "linux_page_size=$$linux_page_size" "$$port_stamp" && \
		[ -s "$$port_dir/Makefile" ] && \
		[ -s "$$port_dir/arch/$(ORLIX_PORT_ARCH)/configs/defconfig" ] && \
		[ -s "$$port_dir/include/linux/init.h" ] && \
		[ -s "$$port_dir/init/Kconfig" ] && \
		[ -s "$$port_dir/usr/Kconfig" ] && \
		[ -s "$$port_dir/scripts/Makefile.clang" ] && \
		[ -s "$$port_dir/kernel/fork.c" ] && \
		[ -s "$$port_dir/mm/page_alloc.c" ] && \
		[ -s "$$port_dir/tools/testing/selftests/lib.mk" ]; then \
		echo "prepared Orlix kernel port tree: $$port_dir (profile $(PROFILE))"; \
		exit 0; \
	fi; \
	for attempt in 1 2 3; do \
		rm -rf "$$port_dir" "$$port_tmp_dir" && break; \
		sleep 1; \
	done; \
	[ ! -e "$$port_dir" ] || { echo "failed to remove disposable Orlix kernel port tree: $$port_dir" >&2; exit 1; }; \
	[ ! -e "$$port_tmp_dir" ] || { echo "failed to remove disposable Orlix kernel port staging tree: $$port_tmp_dir" >&2; exit 1; }; \
	mkdir -p "$$port_tmp_dir" "$$(dirname "$$port_filelist")"; \
	rm -f "$$port_filelist"; \
	for archive_path in $(ORLIX_KERNEL_PORT_ARCHIVE_PATHS); do \
		printf 'checking out Orlix Linux port input: %s\n' "$$archive_path"; \
		git -C "$$upstream_dir" ls-tree -r --name-only "$(LINUX_TAG)" -- "$$archive_path" > "$$port_filelist"; \
		[ -s "$$port_filelist" ] || { echo "missing Orlix Linux port input: $$archive_path" >&2; exit 1; }; \
		git -C "$$upstream_dir" archive "$(LINUX_TAG)" "$$archive_path" | tar -xf - -C "$$port_tmp_dir"; \
	done; \
	cp -R "$$overlay_dir/." "$$port_tmp_dir"; \
	uapi_arch="$(LINUX_UAPI_ARCH)"; \
	uapi_source="$$port_tmp_dir/arch/$$uapi_arch/include/uapi/asm"; \
	uapi_target="$$port_tmp_dir/arch/$(ORLIX_PORT_ARCH)/include/uapi/asm"; \
	[ -d "$$uapi_source" ] || { echo "missing upstream Linux UAPI source for $$uapi_arch: $$uapi_source" >&2; exit 1; }; \
	rm -rf "$$uapi_target"; \
	mkdir -p "$$(dirname "$$uapi_target")"; \
	cp -R "$$uapi_source" "$$uapi_target"; \
	if [ ! -s "$$uapi_target/types.h" ]; then cp "$$port_tmp_dir/include/uapi/asm-generic/types.h" "$$uapi_target/types.h"; fi; \
	echo "using upstream Linux $$uapi_arch UAPI headers for arch/$(ORLIX_PORT_ARCH)"; \
	mkdir -p "$$port_tmp_dir/arch/$(ORLIX_PORT_ARCH)/configs"; \
	awk '!/^CONFIG_PAGE_SIZE_[0-9]+KB=y$$/' "$$profile_config" > "$$port_tmp_dir/arch/$(ORLIX_PORT_ARCH)/configs/defconfig"; \
	case "$$linux_page_size" in \
		4096) printf '%s\n' 'CONFIG_PAGE_SIZE_4KB=y' >> "$$port_tmp_dir/arch/$(ORLIX_PORT_ARCH)/configs/defconfig" ;; \
		16384) printf '%s\n' 'CONFIG_PAGE_SIZE_16KB=y' >> "$$port_tmp_dir/arch/$(ORLIX_PORT_ARCH)/configs/defconfig" ;; \
		*) echo "unsupported OrlixOS target Linux page size: $$linux_page_size" >&2; exit 1 ;; \
	esac; \
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
			patch -d "$$port_tmp_dir" -p1 < "$$patch_abs" >/dev/null; \
		done; \
	fi; \
	for required_port_file in \
		"$$port_tmp_dir/Makefile" \
		"$$port_tmp_dir/arch/$(ORLIX_PORT_ARCH)/configs/defconfig" \
		"$$port_tmp_dir/include/linux/init.h" \
		"$$port_tmp_dir/init/Kconfig" \
		"$$port_tmp_dir/usr/Kconfig" \
		"$$port_tmp_dir/scripts/Makefile.clang" \
		"$$port_tmp_dir/kernel/fork.c" \
		"$$port_tmp_dir/mm/page_alloc.c" \
		"$$port_tmp_dir/tools/testing/selftests/lib.mk"; do \
		[ -s "$$required_port_file" ] || { echo "missing materialized Orlix Linux port input: $$required_port_file" >&2; exit 1; }; \
	done; \
	{ printf 'linux_version=%s\n' "$(LINUX_VERSION)"; printf 'profile=%s\n' "$(PROFILE)"; printf 'linux_uapi_arch=%s\n' "$(LINUX_UAPI_ARCH)"; printf 'linux_page_size=%s\n' "$$linux_page_size"; } > "$$port_tmp_dir/.orlix-port-profile"; \
	mv "$$port_tmp_dir" "$$port_dir"; \
	echo "prepared Orlix kernel port tree: $$port_dir (profile $(PROFILE))"

__prepare-kbuild: __prepare-port
	@set -euo pipefail; \
	$(call orlix_kernel_acquire_profile_lock); \
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
	ready_stamp="$$build_dir/.orlix-kbuild-ready"; \
	if [ -s "$$ready_stamp" ] && \
		[ "$$ready_stamp" -nt "$(ORLIX_PROFILE_CONFIG)" ] && \
		[ "$$ready_stamp" -nt "$(ORLIX_KERNEL_PORT_DIR)/.orlix-port-profile" ] && \
		[ -s "$$build_dir/.config" ] && \
		[ -s "$$build_dir/include/generated/autoconf.h" ] && \
		[ -s "$$build_dir/arch/$(ORLIX_PORT_ARCH)/kernel/vmlinux.lds" ] && \
		[ -s "$$build_dir/arch/$(ORLIX_PORT_ARCH)/boot/dts/release.dtb" ] && \
		[ -s "$$build_dir/arch/$(ORLIX_PORT_ARCH)/boot/dts/development.dtb" ] && \
		[ -s "$$build_dir/drivers/of/empty_root.dtb" ] && \
		[ -s "$$build_dir/security/selinux/flask.h" ] && \
		[ -s "$$build_dir/security/selinux/av_permissions.h" ] && \
		[ -x "$$build_dir/usr/gen_init_cpio" ] && \
		[ -s "$$build_dir/usr/initramfs_inc_data" ]; then \
		echo "reusing prepared Orlix Kbuild output: $$build_dir (profile $(PROFILE))"; \
		exit 0; \
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
	if [ -s "$$build_dir/.config" ] && \
		[ "$$build_dir/.config" -nt "$(ORLIX_PROFILE_CONFIG)" ] && \
		[ "$$build_dir/.config" -nt "$(ORLIX_KERNEL_PORT_DIR)/.orlix-port-profile" ]; then \
		echo "resuming partial Orlix Kbuild output: $$build_dir (profile $(PROFILE))"; \
	else \
		for attempt in 1 2 3 4 5; do \
			rm -rf "$$build_dir" 2>/dev/null || true; \
			[ ! -e "$$build_dir" ] && break; \
			sleep 1; \
		done; \
		[ ! -e "$$build_dir" ] || { echo "failed to remove disposable Orlix Kbuild output: $$build_dir" >&2; exit 1; }; \
		mkdir -p "$$build_dir"; \
	fi; \
	"$$linux_make" -C "$(ORLIX_KERNEL_PORT_ABS)" O="$$build_dir" ARCH="$(ORLIX_PORT_ARCH)" LLVM=1 CLANG_TARGET_FLAGS=aarch64-linux-gnu HOSTCFLAGS="$(ORLIX_KERNEL_HOSTCFLAGS)" defconfig; \
	"$$linux_make" -C "$(ORLIX_KERNEL_PORT_ABS)" O="$$build_dir" ARCH="$(ORLIX_PORT_ARCH)" LLVM=1 CLANG_TARGET_FLAGS=aarch64-linux-gnu HOSTCFLAGS="$(ORLIX_KERNEL_HOSTCFLAGS)" prepare scripts dtbs arch/$(ORLIX_PORT_ARCH)/kernel/vmlinux.lds drivers/of/empty_root.dtb.o lib/crc32.o security/selinux/avc.o; \
	for dtb in release development; do \
		[ -f "$$build_dir/arch/$(ORLIX_PORT_ARCH)/boot/dts/$$dtb.dtb" ] || { echo "missing profile DTB: $$build_dir/arch/$(ORLIX_PORT_ARCH)/boot/dts/$$dtb.dtb" >&2; exit 1; }; \
	done; \
	[ -s "$$build_dir/drivers/of/empty_root.dtb" ] || { echo "missing generated empty-root DTB: $$build_dir/drivers/of/empty_root.dtb" >&2; exit 1; }; \
	mkdir -p "$$build_dir/usr"; \
	gen_init_cpio="$$build_dir/usr/gen_init_cpio"; \
	initramfs_list="$(ORLIX_KERNEL_PORT_ABS)/arch/$(ORLIX_PORT_ARCH)/boot/initramfs/no-init.list"; \
	"$(ORLIX_KERNEL_HOSTCC)" -O2 -Wall -Wmissing-prototypes -Wstrict-prototypes -o "$$gen_init_cpio" "$(ORLIX_KERNEL_PORT_ABS)/usr/gen_init_cpio.c"; \
	[ -x "$$gen_init_cpio" ] || { echo "missing executable Linux gen_init_cpio: $$gen_init_cpio" >&2; exit 1; }; \
	[ -s "$$initramfs_list" ] || { echo "missing Orlix no-init initramfs list: $$initramfs_list" >&2; exit 1; }; \
	grep -Fxq 'CONFIG_INITRAMFS_SOURCE="$$(srctree)/arch/$(ORLIX_PORT_ARCH)/boot/initramfs/no-init.list"' "$$build_dir/.config" || { echo "unexpected CONFIG_INITRAMFS_SOURCE; update Orlix initramfs generation policy" >&2; exit 1; }; \
	grep -Fxq 'CONFIG_INITRAMFS_COMPRESSION_GZIP=y' "$$build_dir/.config" || { echo "unexpected CONFIG_INITRAMFS_COMPRESSION policy; update Orlix initramfs generation policy" >&2; exit 1; }; \
	"$$gen_init_cpio" "$$initramfs_list" > "$$build_dir/usr/initramfs_data.cpio"; \
	gzip -n -c "$$build_dir/usr/initramfs_data.cpio" > "$$build_dir/usr/initramfs_inc_data"; \
	[ -s "$$build_dir/usr/initramfs_inc_data" ] || { echo "missing generated initramfs input: $$build_dir/usr/initramfs_inc_data" >&2; exit 1; }; \
	linker_script="$$build_dir/arch/$(ORLIX_PORT_ARCH)/kernel/vmlinux.lds"; \
	[ -s "$$linker_script" ] || { echo "missing generated Orlix Kbuild linker script: $$linker_script" >&2; exit 1; }; \
	printf 'profile=%s\nlinux_version=%s\n' "$(PROFILE)" "$(LINUX_VERSION)" > "$$ready_stamp"; \
	echo "verified Orlix Kbuild linker script: $$linker_script"; \
	echo "prepared Orlix Kbuild output without a standalone image: $$build_dir (profile $(PROFILE))"

__headers-install: __prepare-port
	@set -euo pipefail; \
	$(call orlix_kernel_acquire_profile_lock); \
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
	header_install_dir="$(ORLIX_MLIBC_KERNEL_HEADERS_DIR)"; \
	header_install_stamp="$$header_install_dir/.orlix-headers-ready"; \
	uapi_build_dir="$(ORLIX_KERNEL_BUILD_DIR)-uapi-$(LINUX_UAPI_ARCH)"; \
	if [ -L "$$header_install_dir" ]; then echo "refusing to clean symlinked OrlixMLibC kernel header path: $$header_install_dir" >&2; exit 1; fi; \
	for path in "$$uapi_build_dir" "$$(dirname "$$uapi_build_dir")"; do \
		if [ -e "$$path" ] && [ -L "$$path" ]; then echo "refusing to use symlinked Linux UAPI build path: $$path" >&2; exit 1; fi; \
	done; \
	header_staging_parent="$$uapi_build_dir/usr"; \
	header_staging="$$header_staging_parent/include"; \
	if [ -s "$$header_install_stamp" ] && \
		[ "$$header_install_stamp" -nt "$(ORLIX_KERNEL_PORT_DIR)/.orlix-port-profile" ] && \
		[ -d "$$header_install_dir/include" ]; then \
		echo "reusing installed Orlix UAPI headers: $$header_install_dir/include"; \
		exit 0; \
	fi; \
	resume_headers=0; \
	if [ -d "$$header_install_dir/include" ] && [ -d "$$header_staging" ]; then resume_headers=1; fi; \
	if [ "$$resume_headers" -eq 1 ]; then \
		echo "resuming partial Orlix UAPI header install: $$header_install_dir/include"; \
	else \
		for attempt in 1 2 3 4 5; do \
			rm -rf "$$header_install_dir" 2>/dev/null || true; \
			[ ! -e "$$header_install_dir" ] && break; \
			sleep 1; \
		done; \
		[ ! -e "$$header_install_dir" ] || { echo "failed to clean generated OrlixMLibC kernel header path: $$header_install_dir" >&2; exit 1; }; \
		mkdir -p "$$header_install_dir"; \
	fi; \
	for path in "$$header_staging_parent" "$$header_staging"; do \
		if [ -L "$$path" ]; then echo "refusing to clean symlinked Linux UAPI header staging path: $$path" >&2; exit 1; fi; \
	done; \
	if [ "$$resume_headers" -ne 1 ]; then \
		for attempt in 1 2 3 4 5; do \
			rm -rf "$$header_staging" 2>/dev/null || true; \
			[ ! -e "$$header_staging" ] && break; \
			sleep 1; \
		done; \
		[ ! -e "$$header_staging" ] || { echo "failed to clean generated Linux UAPI header staging path: $$header_staging" >&2; exit 1; }; \
	fi; \
	mkdir -p "$$header_staging"; \
	for uapi_root in \
		"$(ORLIX_KERNEL_PORT_ABS)/include/uapi" \
		"$(ORLIX_KERNEL_PORT_ABS)/arch/$(LINUX_UAPI_ARCH)/include/uapi" \
		"$$uapi_build_dir/include/generated/uapi" \
		"$$uapi_build_dir/arch/$(LINUX_UAPI_ARCH)/include/generated/uapi"; do \
		[ -d "$$uapi_root" ] || continue; \
		(cd "$$uapi_root" && find . -type d -print) | while IFS= read -r rel_dir; do \
			rel_dir="$${rel_dir#./}"; \
			[ -n "$$rel_dir" ] || continue; \
			mkdir -p "$$header_staging/$$rel_dir"; \
		done; \
	done; \
	mkdir -p "$$uapi_build_dir"; \
	env -u MAKEFLAGS -u MFLAGS -u GNUMAKEFLAGS "$$linux_make" -j"$(ORLIX_HEADERS_INSTALL_JOBS)" -C "$(ORLIX_KERNEL_PORT_ABS)" O="$$uapi_build_dir" ARCH="$(LINUX_UAPI_ARCH)" LLVM=1 INSTALL_HDR_PATH="$(ORLIX_MLIBC_KERNEL_HEADERS_DIR)" headers_install; \
	[ -d "$(ORLIX_MLIBC_KERNEL_HEADERS_DIR)/include" ] || { echo "missing installed Orlix UAPI headers: $(ORLIX_MLIBC_KERNEL_HEADERS_DIR)/include" >&2; exit 1; }; \
	printf 'profile=%s\nlinux_version=%s\nlinux_uapi_arch=%s\n' "$(PROFILE)" "$(LINUX_VERSION)" "$(LINUX_UAPI_ARCH)" > "$$header_install_stamp"; \
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
	"$$linux_make" -C "$(ORLIX_KERNEL_PORT_ABS)" O="$(ORLIX_KUNIT_BUILD_DIR)" ARCH="$(ORLIX_PORT_ARCH)" LLVM=1 CLANG_TARGET_FLAGS=aarch64-linux-gnu HOSTCFLAGS="$(ORLIX_KERNEL_HOSTCFLAGS)" defconfig; \
	"$(ORLIX_KERNEL_PORT_ABS)/scripts/kconfig/merge_config.sh" -m -O "$(ORLIX_KUNIT_BUILD_DIR)" "$(ORLIX_KUNIT_BUILD_DIR)/.config" "$(ORLIX_KERNEL_PORT_ABS)/arch/$(ORLIX_PORT_ARCH)/.kunitconfig"; \
	"$$linux_make" -C "$(ORLIX_KERNEL_PORT_ABS)" O="$(ORLIX_KUNIT_BUILD_DIR)" ARCH="$(ORLIX_PORT_ARCH)" LLVM=1 CLANG_TARGET_FLAGS=aarch64-linux-gnu HOSTCFLAGS="$(ORLIX_KERNEL_HOSTCFLAGS)" olddefconfig arch/$(ORLIX_PORT_ARCH)/boot/boot_test.o; \
	echo "built Orlix KUnit objects: $(ORLIX_KUNIT_BUILD_DIR)"

__kernel-archive: __prepare-kbuild
	@set -euo pipefail; \
	$(call orlix_kernel_acquire_profile_lock); \
	cc="$(ORLIX_KERNEL_CC)"; \
	hostcc="$(ORLIX_KERNEL_HOSTCC)"; \
	ar_cmd="$(ORLIX_KERNEL_AR)"; \
	nm_cmd="$(ORLIX_KERNEL_NM)"; \
	otool_cmd="$(ORLIX_KERNEL_OTOOL)"; \
	linux_llvm_bin="$(LINUX_LLVM_BIN)"; \
	PATH="$${linux_llvm_bin:+$$linux_llvm_bin:}$$PATH"; \
	export PATH; \
	command -v "$$cc" >/dev/null 2>&1 || { echo "clang is required for OrlixKernel archive builds; set ORLIX_KERNEL_CC=/path/to/clang" >&2; exit 1; }; \
	command -v "$$hostcc" >/dev/null 2>&1 || { echo "host C compiler is required for OrlixKernel archive builds; set ORLIX_KERNEL_HOSTCC=/path/to/cc" >&2; exit 1; }; \
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
	build_version="$${KBUILD_BUILD_VERSION:-$$(cd "$(ORLIX_KERNEL_BUILD_DIR)" && "$(ORLIX_KERNEL_PORT_ABS)/scripts/build-version")}"; \
	build_timestamp="$${KBUILD_BUILD_TIMESTAMP:-$$(LC_ALL=C date)}"; \
	smp_flag=""; \
	preempt_flag=""; \
	if grep -q '^CONFIG_SMP=y$$' "$(ORLIX_KERNEL_BUILD_DIR)/.config"; then smp_flag="SMP"; fi; \
	if grep -q '^CONFIG_PREEMPT_BUILD=y$$' "$(ORLIX_KERNEL_BUILD_DIR)/.config"; then preempt_flag="PREEMPT"; fi; \
	if grep -q '^CONFIG_PREEMPT_DYNAMIC=y$$' "$(ORLIX_KERNEL_BUILD_DIR)/.config"; then preempt_flag="PREEMPT_DYNAMIC"; fi; \
	if grep -q '^CONFIG_PREEMPT_RT=y$$' "$(ORLIX_KERNEL_BUILD_DIR)/.config"; then preempt_flag="PREEMPT_RT"; fi; \
	uts_version="$$(printf '#%s %s %s %s' "$$build_version" "$$smp_flag" "$$preempt_flag" "$$build_timestamp" | cut -b -64)"; \
	printf '#define UTS_VERSION "%s"\n' "$$uts_version" > "$(ORLIX_KERNEL_BUILD_DIR)/init/utsversion-tmp.h"; \
	{ for src_rel in $(ORLIX_KERNEL_LINUX_SOURCES); do printf '%s\n' "$(ORLIX_KERNEL_PORT_ABS)/$$src_rel"; done; } > "$(ORLIX_KERNEL_ARCHIVE_MANIFEST)"; \
	$(call orlix_product_adapter_verify_object_contract) \
	$(call orlix_product_adapter_source_resolver) \
	$(call orlix_product_adapter_generate_payloads) \
	$(call orlix_product_adapter_generate_boundaries) \
	$(call orlix_product_adapter_generate_kallsyms) \
	$(call orlix_product_adapter_finalize_archive) \
	compile_slice() { \
		platform="$$1"; \
		target="$$2"; \
		output_dir="$$root/$$platform"; \
		obj_dir="$$output_dir/objects"; \
		archive="$$output_dir/$(ORLIX_KERNEL_ARCHIVE_NAME)"; \
		archive_tmp="$$output_dir/.$(ORLIX_KERNEL_ARCHIVE_NAME).tmp.$$$$"; \
		symbols_tmp="$$output_dir/.symbols.txt.tmp.$$$$"; \
		mkdir -p "$$obj_dir"; \
		if [ -s "$$archive" ] && [ -s "$$output_dir/symbols.txt" ] && \
			grep -q '_arch_boot_entry' "$$output_dir/symbols.txt" && \
			grep -q '_arch_boot_params' "$$output_dir/symbols.txt"; then \
			archive_ready=1; \
			for dep in \
				OrlixKernel/Sources/ports/orlix/kbuild/kernel-rules.mk \
				OrlixKernel/Sources/ports/orlix/kbuild/product-compile-adapter.mk \
				"$(ORLIX_KERNEL_BUILD_DIR)/.config"; do \
				if [ ! -e "$$dep" ] || [ ! "$$archive" -nt "$$dep" ]; then archive_ready=0; break; fi; \
			done; \
			if [ "$$archive_ready" -eq 1 ]; then \
				for src_rel in $(ORLIX_KERNEL_LINUX_SOURCES); do \
					src="$$(orlix_product_adapter_source_for "$$src_rel")"; \
					if [ ! -s "$$src" ] || [ ! "$$archive" -nt "$$src" ]; then archive_ready=0; break; fi; \
					obj_name="$${src_rel//\//_}.o"; \
					obj="$$obj_dir/$$obj_name"; \
					if [ -e "$$obj" ] && [ ! "$$archive" -nt "$$obj" ]; then archive_ready=0; break; fi; \
				done; \
			fi; \
			if [ "$$archive_ready" -eq 1 ]; then \
				echo "reusing OrlixKernel archive: $$archive ($$target)"; \
				return 0; \
			fi; \
		fi; \
		rm -f "$$archive_tmp" "$$symbols_tmp"; \
		object_set_ready=1; \
		objects_probe=(); \
		for src_rel in $(ORLIX_KERNEL_LINUX_SOURCES); do \
			src="$$(orlix_product_adapter_source_for "$$src_rel")"; \
			obj_name="$${src_rel//\//_}.o"; \
			obj="$$obj_dir/$$obj_name"; \
			verified="$$obj.verified"; \
			if [ ! -s "$$obj" ] || [ ! -e "$$verified" ]; then object_set_ready=0; break; fi; \
			if [ ! -s "$$src" ] || [ ! "$$obj" -nt "$$src" ]; then object_set_ready=0; break; fi; \
			for dep in \
				OrlixKernel/Sources/ports/orlix/kbuild/kernel-rules.mk \
				OrlixKernel/Sources/ports/orlix/kbuild/product-compile-adapter.mk \
				"$(ORLIX_KERNEL_BUILD_DIR)/.config"; do \
				if [ ! -e "$$dep" ] || [ ! "$$obj" -nt "$$dep" ]; then object_set_ready=0; break 2; fi; \
			done; \
			objects_probe+=("$$obj"); \
		done; \
		if [ "$$object_set_ready" -eq 1 ]; then \
			objs=("$${objects_probe[@]}"); \
			printf '  ORLIXOBJS %s %s objects\n' "$$platform" "$${#objs[@]}" >&2; \
		else \
			objs=(); \
			for src_rel in $(ORLIX_KERNEL_LINUX_SOURCES); do \
			src="$$(orlix_product_adapter_source_for "$$src_rel")"; \
			[ -s "$$src" ] || { echo "missing Linux source: $$src" >&2; exit 1; }; \
			kbuild_name="$${src_rel##*/}"; \
			kbuild_name="$${kbuild_name%.c}"; \
			kbuild_name="$${kbuild_name//-/_}"; \
			obj_name="$${src_rel//\//_}.o"; \
			obj="$$obj_dir/$$obj_name"; \
			verified="$$obj.verified"; \
			dep="$$obj_dir/$${obj_name%.o}.d"; \
			src_dir="$${src_rel%/*}"; \
			src_local_dir="$${src%/*}"; \
			local_cflags="-I$$src_local_dir -I$(ORLIX_KERNEL_PORT_ABS)/$$src_dir -I$(ORLIX_KERNEL_BUILD_DIR)/$$src_dir"; \
			extra_cflags=""; \
			case "$$src_rel" in \
				init/version.c) extra_cflags="-include $(ORLIX_KERNEL_BUILD_DIR)/init/utsversion-tmp.h" ;; \
				drivers/of/of_reserved_mem.c) extra_cflags="-I$(ORLIX_KERNEL_PORT_ABS)/drivers/of" ;; \
				drivers/char/virtio_console.c) extra_cflags="-I$(ORLIX_KERNEL_PORT_ABS)/drivers/tty/hvc -I$(ORLIX_KERNEL_PORT_ABS)/drivers/tty" ;; \
				drivers/tty/hvc/*.c) extra_cflags="-I$(ORLIX_KERNEL_PORT_ABS)/drivers/tty/hvc -I$(ORLIX_KERNEL_PORT_ABS)/drivers/tty" ;; \
				drivers/tty/*.c) extra_cflags="-I$(ORLIX_KERNEL_PORT_ABS)/drivers/tty" ;; \
				fs/iomap/trace.c) extra_cflags="-I$(ORLIX_KERNEL_PORT_ABS)/fs/iomap" ;; \
				kernel/sched/*.c) extra_cflags="-I$(ORLIX_KERNEL_PORT_ABS)/kernel/sched" ;; \
				security/selinux/*.c|security/selinux/ss/*.c) extra_cflags="-I$(ORLIX_KERNEL_PORT_ABS)/security/selinux -I$(ORLIX_KERNEL_PORT_ABS)/security/selinux/include -I$(ORLIX_KERNEL_BUILD_DIR)/security/selinux" ;; \
				lib/crc32.c) extra_cflags="-I$(ORLIX_KERNEL_PORT_ABS)/lib -I$(ORLIX_KERNEL_BUILD_DIR)/lib" ;; \
				lib/fdt*.c) extra_cflags="-I$(ORLIX_KERNEL_PORT_ABS)/scripts/dtc/libfdt" ;; \
			esac; \
			needs_build=1; \
			if [ -s "$$obj" ] && \
				[ "$$obj" -nt "$$src" ] && \
				[ "$$obj" -nt "$(ORLIX_KERNEL_BUILD_DIR)/.config" ]; then \
				needs_build=0; \
			fi; \
			if [ "$$needs_build" -eq 1 ]; then \
				printf '  ORLIXCC %s %s\n' "$$platform" "$$src_rel" >&2; \
				/usr/bin/env -u SDKROOT "$$cc" -target "$$target" -isysroot / -x c -ffreestanding $(ORLIX_PRODUCT_ADAPTER_CFLAGS) -fno-builtin -fno-stack-protector -fno-objc-arc -fno-common -nostdinc -D__KERNEL__ -DORLIX_APP_HOSTED_BOOT=1 -DKBUILD_MODNAME=\"$$kbuild_name\" -DKBUILD_BASENAME=\"$$kbuild_name\" -DKBUILD_MODFILE=\"$$src_rel\" -include "$(ORLIX_KERNEL_PORT_ABS)/include/linux/compiler-version.h" -include "$(ORLIX_KERNEL_PORT_ABS)/include/linux/kconfig.h" $$local_cflags $$extra_cflags -I"$(ORLIX_KERNEL_PORT_ABS)/arch/$(ORLIX_PORT_ARCH)/include" -I"$(ORLIX_KERNEL_BUILD_DIR)/arch/$(ORLIX_PORT_ARCH)/include/generated" -I"$(ORLIX_KERNEL_PORT_ABS)/include" -I"$(ORLIX_KERNEL_BUILD_DIR)/include" -I"$(ORLIX_KERNEL_PORT_ABS)/arch/$(ORLIX_PORT_ARCH)/include/uapi" -I"$(ORLIX_KERNEL_BUILD_DIR)/arch/$(ORLIX_PORT_ARCH)/include/generated/uapi" -I"$(ORLIX_KERNEL_PORT_ABS)/include/uapi" -I"$(ORLIX_KERNEL_BUILD_DIR)/include/generated/uapi" -MMD -MF "$$dep" -c "$$src" -o "$$obj"; \
				if grep -E '(/Applications/|/Library/Developer/CommandLineTools/SDKs/|/System/Library/Frameworks|/usr/include)' "$$dep"; then \
					echo "Linux object included a host SDK or libc header: $$dep" >&2; \
					exit 1; \
				fi; \
				if grep -E '(OrlixHostAdapter/Sources|OrlixKernel/Sources/include|OrlixKernel/Sources/boot|OrlixMLibC/Sources|Build/OrlixMLibC/sysroot|/usr/local/include|/opt/homebrew/include)' "$$dep"; then \
					echo "Linux object included host, OrlixMLibC, or product headers: $$dep" >&2; \
					exit 1; \
				fi; \
				orlix_product_adapter_verify_object_contract "$$obj"; \
				: > "$$verified"; \
			else \
				if [ ! -e "$$verified" ] || [ "$$verified" -ot "$$obj" ]; then \
					orlix_product_adapter_verify_object_contract "$$obj"; \
					: > "$$verified"; \
				fi; \
			fi; \
			objs+=("$$obj"); \
			done; \
		fi; \
		orlix_product_adapter_generate_payloads "$$platform" "$$target"; \
		orlix_product_adapter_generate_boundaries "$$platform" "$$target" "$${objs[@]}"; \
		orlix_product_adapter_generate_kallsyms "$$platform" "$$target" "$${objs[@]}"; \
		orlix_product_adapter_finalize_archive "$$platform" "$$target" "$$archive_tmp" "$${objs[@]}"; \
		[ -s "$$archive_tmp" ] || { echo "missing staged OrlixKernel archive: $$archive_tmp" >&2; exit 1; }; \
		"$$nm_cmd" -gU "$$archive_tmp" > "$$symbols_tmp"; \
		grep -q '_arch_boot_entry' "$$symbols_tmp" || { echo "OrlixKernel archive missing _arch_boot_entry: $$archive_tmp" >&2; exit 1; }; \
		grep -q '_arch_boot_params' "$$symbols_tmp" || { echo "OrlixKernel archive missing _arch_boot_params: $$archive_tmp" >&2; exit 1; }; \
		mv -f "$$archive_tmp" "$$archive"; \
		mv -f "$$symbols_tmp" "$$output_dir/symbols.txt"; \
		echo "built OrlixKernel archive: $$archive ($$target)"; \
	}; \
	[ -n "$(strip $(ORLIX_KERNEL_ARCHIVE_PLATFORMS))" ] || { echo "ORLIX_KERNEL_ARCHIVE_PLATFORMS must not be empty" >&2; exit 1; }; \
	for archive_platform in $(ORLIX_KERNEL_ARCHIVE_PLATFORMS); do \
		case "$$archive_platform" in \
			iphoneos) compile_slice iphoneos "$(ORLIX_IOS_TARGET)" ;; \
			iphonesimulator) compile_slice iphonesimulator "$(ORLIX_IOS_SIMULATOR_TARGET)" ;; \
			*) echo "unsupported OrlixKernel archive platform: $$archive_platform" >&2; exit 1 ;; \
		esac; \
	done; \
	echo "wrote OrlixKernel archive manifest: $(ORLIX_KERNEL_ARCHIVE_MANIFEST)"

__verify-xcodegen-boundary:
	@set -euo pipefail; \
	project="$(ORLIX_XCODE_PROJECT)"; \
	[ -d "$$project" ] || { echo "missing generated Xcode project: $$project" >&2; exit 1; }; \
	if grep -R 'Build/OrlixKernel/upstream/linux-6.12.git' "$$project"; then \
		echo "generated Xcode project references upstream Linux source" >&2; \
		exit 1; \
	fi; \
	if grep -R 'Build/OrlixKernel/src/linux-6.12-port' "$$project"; then \
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
	if grep -R -n -E 'dlsym[[:space:]]*\([^;]*start_kernel|start_kernel[^;]*dlsym|RTLD_DEFAULT[^;]*start_kernel|start_kernel[^;]*RTLD_DEFAULT' \
		OrlixHostAdapter/Sources OrlixKernel/Sources/boot OrlixTerminal/Sources; then \
		echo "product boot path must not resolve start_kernel through dlsym or RTLD_DEFAULT" >&2; \
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

__orlixmlibc-sysroot: __validate-profile
	@set -euo pipefail; \
	selected_libc="$(libc)"; \
	case "$$selected_libc" in orlixmlibc) ;; *) echo "unsupported libc=$$selected_libc (expected orlixmlibc)" >&2; exit 1 ;; esac; \
	sysroot="$(KSELFTEST_SYSROOT)"; \
	sysroot_stamp="$$sysroot/.orlixmlibc-sysroot-ready"; \
	patches_newer=0; \
	if [ -d "OrlixMLibC/Sources/patches" ] && [ -s "$$sysroot_stamp" ] && \
		find "OrlixMLibC/Sources/patches" -name '*.patch' -newer "$$sysroot_stamp" -print -quit | grep -q .; then \
		patches_newer=1; \
	fi; \
	if [ -s "$$sysroot_stamp" ] && \
		[ "$$sysroot_stamp" -nt "OrlixMLibC/Makefile" ] && \
		[ "$$patches_newer" -eq 0 ] && \
		[ -d "$$sysroot/usr/include" ] && \
		[ -d "$$sysroot/usr/lib" ] && \
		[ -s "$$sysroot/usr/lib/libc.a" ]; then \
		echo "reusing OrlixMLibC sysroot: $$sysroot"; \
		exit 0; \
	fi; \
	$(MAKE) -f OrlixMLibC/Makefile build PROFILE="$(PROFILE)"

__kselftest-install: __prepare-kbuild $(KSELFTEST_PREREQS) __validate-profile
	@set -euo pipefail; \
	selected_libc="$(libc)"; \
	case "$$selected_libc" in orlixmlibc) ;; *) echo "unsupported libc=$$selected_libc (expected orlixmlibc)" >&2; exit 1 ;; esac; \
	install_dir="$(KSELFTEST_INSTALL_DIR)"; \
	proof_label="$(KSELFTEST_PROOF_LABEL)"; \
	linux_llvm_bin="$(LINUX_LLVM_BIN)"; \
	coreutils_dir=""; \
	if readlink -e / >/dev/null 2>&1; then coreutils_dir="$$(dirname "$$(command -v readlink)")"; \
	elif [ -x /opt/homebrew/opt/coreutils/libexec/gnubin/readlink ]; then coreutils_dir=/opt/homebrew/opt/coreutils/libexec/gnubin; \
	else echo "GNU readlink is required by kselftest install; install coreutils" >&2; exit 1; fi; \
	PATH="$$coreutils_dir:$${linux_llvm_bin:+$$linux_llvm_bin:}$$PATH"; \
	export PATH; \
	rm -rf "$$install_dir"; \
	mkdir -p "$$(dirname "$$install_dir")"; \
	sysroot="$(KSELFTEST_SYSROOT)"; \
	header_flags="$(KSELFTEST_HEADER_FLAGS)"; \
	case "$$sysroot" in /*) ;; *) sysroot="$(CURDIR)/$$sysroot" ;; esac; \
	[ -d "$$sysroot" ] || { echo "missing OrlixMLibC sysroot: $$sysroot; run make -f OrlixMLibC/Makefile build PROFILE=$(PROFILE)" >&2; exit 1; }; \
	hosted_user_base="$(ORLIX_HOSTED_USER_BASE_ADDRESS)"; \
	processor_header="$(ORLIX_KERNEL_PORT_ABS)/arch/$(ORLIX_PORT_ARCH)/include/asm/processor.h"; \
	grep -Eq "^[[:space:]]*#define[[:space:]]+ORLIX_HOSTED_USER_BASE[[:space:]]+\\($$hosted_user_base" "$$processor_header" || { echo "Orlix kselftest link base $$hosted_user_base does not match ORLIX_HOSTED_USER_BASE in $$processor_header" >&2; exit 1; }; \
	orlix_crt_flags="$$sysroot/usr/lib/crt1.o $$sysroot/usr/lib/crti.o"; \
	orlix_ldlibs="-Wl,--start-group $$sysroot/usr/lib/libc.a $$sysroot/usr/lib/libssp_nonshared.a $$sysroot/usr/lib/libssp.a -Wl,--end-group $$sysroot/usr/lib/crtn.o"; \
	linux_make="$(LINUX_MAKE)"; \
	if [ -z "$$linux_make" ]; then linux_make="$$(command -v gmake || true)"; fi; \
	if [ -z "$$linux_make" ]; then echo "GNU Make >= 4.0 is required by Linux kselftest; install gmake or set LINUX_MAKE=/path/to/gmake" >&2; exit 1; fi; \
	command -v clang >/dev/null 2>&1 || { echo "clang is required to build Linux kselftest artifacts" >&2; exit 1; }; \
	kselftest_build_dir="$(ORLIX_KERNEL_BUILD_DIR)/kselftest/orlix"; \
	if [ -L "$$kselftest_build_dir" ]; then echo "refusing to clean symlinked kselftest build path: $$kselftest_build_dir" >&2; exit 1; fi; \
	rm -rf "$$kselftest_build_dir"; \
	"$$linux_make" -C "$(ORLIX_KERNEL_PORT_ABS)/tools/testing/selftests" \
		O="$(ORLIX_KERNEL_BUILD_DIR)" \
		TARGETS=orlix \
		KSFT_INSTALL_PATH="$$install_dir" \
		ARCH="$(ORLIX_KSELFTEST_ARCH)" \
		LLVM=1 \
		FORCE_TARGETS=1 \
		USERCFLAGS="--sysroot=$$sysroot $$header_flags -fno-pie -DORLIX_HOSTED_USER_BASE_ADDRESS=$$hosted_user_base" \
		USERLDFLAGS="--sysroot=$$sysroot -static -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--image-base=$$hosted_user_base $$orlix_crt_flags" \
		LDLIBS="$$orlix_ldlibs" \
		install; \
	printf 'proof_lane=%s\n' "$$proof_label" > "$$install_dir/proof_lane.txt"; \
	[ -s "$$install_dir/run_kselftest.sh" ] || { echo "missing installed kselftest runner" >&2; exit 1; }; \
	echo "installed OrlixMLibC-built kselftests: $$install_dir"

__kselftest-initramfs:
	@set -euo pipefail; \
	selected_libc="$(libc)"; \
	case "$$selected_libc" in orlixmlibc) ;; *) echo "unsupported libc=$$selected_libc (expected orlixmlibc)" >&2; exit 1 ;; esac; \
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
	output_parent="$$(dirname "$$output")"; \
	rm -rf "$$output"; \
	mkdir -p "$$output_parent" "$$output/rootfs"; \
	gen_init_cpio="$(ORLIX_KERNEL_BUILD_DIR)/usr/gen_init_cpio"; \
	cpio_list="$$output/initramfs.list"; \
	init_binary="$$install_dir/orlix/kselftest_init"; \
	kselftest_list="$$install_dir/kselftest-list.txt"; \
	[ -s "$$init_binary" ] || { echo "missing OrlixMLibC-built kselftest init binary: $$init_binary" >&2; exit 1; }; \
	[ -s "$$kselftest_list" ] || { echo "missing installed kselftest list: $$kselftest_list" >&2; exit 1; }; \
	[ -x "$$gen_init_cpio" ] || { echo "missing executable Linux gen_init_cpio: $$gen_init_cpio" >&2; exit 1; }; \
	{ \
		printf '%s\n' 'dir /dev 755 0 0'; \
		printf '%s\n' 'dir /proc 555 0 0'; \
		printf '%s\n' 'dir /sys 555 0 0'; \
		printf '%s\n' 'dir /orlix 755 0 0'; \
		printf '%s\n' 'nod /dev/console 600 0 0 c 5 1'; \
		printf 'file /init %s 755 0 0\n' "$$init_binary"; \
		printf 'file /kselftest-list.txt %s 644 0 0\n' "$$kselftest_list"; \
		while IFS=: read -r collection test_name; do \
			if [ "$$collection" = orlix ] && [ -n "$$test_name" ]; then \
				test_binary="$$install_dir/orlix/$$test_name"; \
				[ -s "$$test_binary" ] || { echo "missing installed Orlix kselftest binary: $$test_binary" >&2; exit 1; }; \
				printf 'file /orlix/%s %s 755 0 0\n' "$$test_name" "$$test_binary"; \
			fi; \
		done < "$$kselftest_list"; \
	} > "$$cpio_list"; \
	"$$gen_init_cpio" "$$cpio_list" > "$$output/rootfs/initramfs.cpio"; \
	gzip -n -f "$$output/rootfs/initramfs.cpio"; \
	{ \
		printf '%s\n' '<?xml version="1.0" encoding="UTF-8"?>'; \
		printf '%s\n' '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">'; \
		printf '%s\n' '<plist version="1.0">'; \
		printf '%s\n' '<dict>'; \
		printf '%s\n' '    <key>CFBundleIdentifier</key>'; \
		printf '%s\n' '    <string>$(ORLIX_MLIBC_TEST_INITRAMFS_BUNDLE_IDENTIFIER)</string>'; \
		printf '%s\n' '    <key>CFBundleName</key>'; \
		printf '%s\n' '    <string>$(ORLIX_MLIBC_TEST_INITRAMFS_BUNDLE_NAME)</string>'; \
		printf '%s\n' '    <key>CFBundlePackageType</key>'; \
		printf '%s\n' '    <string>BNDL</string>'; \
		printf '%s\n' '    <key>CFBundleShortVersionString</key>'; \
		printf '%s\n' '    <string>0.1</string>'; \
		printf '%s\n' '    <key>CFBundleVersion</key>'; \
		printf '%s\n' '    <string>1</string>'; \
		printf '%s\n' '    <key>OrlixLinuxArch</key>'; \
		printf '%s\n' '    <string>$(ORLIX_PORT_ARCH)</string>'; \
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

__kernel-payload: $(ORLIX_KERNEL_PAYLOAD_PREREQS)
	@set -euo pipefail; \
	$(call orlix_kernel_acquire_profile_lock); \
	output="$(ORLIX_KERNEL_PAYLOAD_DIR)"; \
	required_settings=( \
		"ORLIX_KERNEL_PAYLOAD_BUNDLE_NAME=$(ORLIX_KERNEL_PAYLOAD_BUNDLE_NAME)" \
		"ORLIX_KERNEL_PAYLOAD_BUNDLE_EXTENSION=$(ORLIX_KERNEL_PAYLOAD_BUNDLE_EXTENSION)" \
		"ORLIX_KERNEL_PAYLOAD_BUNDLE_IDENTIFIER=$(ORLIX_KERNEL_PAYLOAD_BUNDLE_IDENTIFIER)" \
		"ORLIX_KERNEL_PAYLOAD_SELECTED_PROFILE_INFO_KEY=$(ORLIX_KERNEL_PAYLOAD_SELECTED_PROFILE_INFO_KEY)" \
		"ORLIX_KERNEL_PAYLOAD_ROOT_INITRAMFS_INFO_KEY=$(ORLIX_KERNEL_PAYLOAD_ROOT_INITRAMFS_INFO_KEY)" \
		"ORLIX_KERNEL_PAYLOAD_BASE_ROOT_IMAGE_INFO_KEY=$(ORLIX_KERNEL_PAYLOAD_BASE_ROOT_IMAGE_INFO_KEY)" \
		"ORLIX_KERNEL_PAYLOAD_STATE_ROOT_IMAGE_INFO_KEY=$(ORLIX_KERNEL_PAYLOAD_STATE_ROOT_IMAGE_INFO_KEY)" \
		"ORLIX_KERNEL_PAYLOAD_BASE_ROOT_DEVICE_INFO_KEY=$(ORLIX_KERNEL_PAYLOAD_BASE_ROOT_DEVICE_INFO_KEY)" \
		"ORLIX_KERNEL_PAYLOAD_STATE_ROOT_DEVICE_INFO_KEY=$(ORLIX_KERNEL_PAYLOAD_STATE_ROOT_DEVICE_INFO_KEY)" \
		"ORLIX_KERNEL_PAYLOAD_BASE_ROOT_HOST_BLOCK_DEVICE_INFO_KEY=$(ORLIX_KERNEL_PAYLOAD_BASE_ROOT_HOST_BLOCK_DEVICE_INFO_KEY)" \
		"ORLIX_KERNEL_PAYLOAD_STATE_ROOT_HOST_BLOCK_DEVICE_INFO_KEY=$(ORLIX_KERNEL_PAYLOAD_STATE_ROOT_HOST_BLOCK_DEVICE_INFO_KEY)" \
		"ORLIX_KERNEL_PAYLOAD_STATE_ROOT_MINIMUM_BYTES_INFO_KEY=$(ORLIX_KERNEL_PAYLOAD_STATE_ROOT_MINIMUM_BYTES_INFO_KEY)" \
		"ORLIX_KERNEL_LINUX_PAGE_SIZE=$(ORLIX_KERNEL_LINUX_PAGE_SIZE)" \
		"ORLIX_KERNEL_ROOT_INITRAMFS_RESOURCE=$(ORLIX_KERNEL_ROOT_INITRAMFS_RESOURCE)" \
		"ORLIX_KERNEL_BASE_ROOT_IMAGE_RESOURCE=$(ORLIX_KERNEL_BASE_ROOT_IMAGE_RESOURCE)" \
		"ORLIX_KERNEL_STATE_ROOT_IMAGE_RESOURCE=$(ORLIX_KERNEL_STATE_ROOT_IMAGE_RESOURCE)" \
		"ORLIX_KERNEL_BASE_ROOT_DEVICE=$(ORLIX_KERNEL_BASE_ROOT_DEVICE)" \
		"ORLIX_KERNEL_STATE_ROOT_DEVICE=$(ORLIX_KERNEL_STATE_ROOT_DEVICE)" \
		"ORLIX_KERNEL_BASE_ROOT_HOST_BLOCK_DEVICE=$(ORLIX_KERNEL_BASE_ROOT_HOST_BLOCK_DEVICE)" \
		"ORLIX_KERNEL_STATE_ROOT_HOST_BLOCK_DEVICE=$(ORLIX_KERNEL_STATE_ROOT_HOST_BLOCK_DEVICE)" \
		"ORLIX_KERNEL_BASE_ROOT_IMAGE_SIZE=$(ORLIX_KERNEL_BASE_ROOT_IMAGE_SIZE)" \
		"ORLIX_KERNEL_STATE_ROOT_IMAGE_SIZE=$(ORLIX_KERNEL_STATE_ROOT_IMAGE_SIZE)" \
		"ORLIX_KERNEL_STATE_ROOT_MINIMUM_BYTES=$(ORLIX_KERNEL_STATE_ROOT_MINIMUM_BYTES)" \
	); \
	for setting in "$${required_settings[@]}"; do \
		value="$${setting#*=}"; \
		[ -n "$$value" ] || { echo "missing OrlixOS payload target setting: $${setting%%=*}" >&2; exit 1; }; \
	done; \
	validate_payload_name() { \
		name="$$1"; \
		case "$$name" in ""|/*|*/*|.*|*..*) echo "invalid OrlixOS payload bundle name: $$name" >&2; exit 1 ;; esac; \
	}; \
	validate_payload_resource() { \
		resource="$$1"; \
		case "$$resource" in ""|/*|..|../*|*/..|*/../*) echo "invalid OrlixOS payload resource path: $$resource" >&2; exit 1 ;; esac; \
	}; \
	validate_unsigned_int() { \
		value="$$1"; \
		case "$$value" in ""|*[!0-9]*) echo "invalid unsigned OrlixOS payload setting: $$value" >&2; exit 1 ;; esac; \
	}; \
	validate_payload_name "$(ORLIX_KERNEL_PAYLOAD_BUNDLE_NAME)"; \
	validate_payload_name "$(ORLIX_KERNEL_PAYLOAD_BUNDLE_EXTENSION)"; \
	validate_payload_resource "$(ORLIX_KERNEL_ROOT_INITRAMFS_RESOURCE)"; \
	validate_payload_resource "$(ORLIX_KERNEL_BASE_ROOT_IMAGE_RESOURCE)"; \
	validate_payload_resource "$(ORLIX_KERNEL_STATE_ROOT_IMAGE_RESOURCE)"; \
	validate_unsigned_int "$(ORLIX_KERNEL_LINUX_PAGE_SIZE)"; \
	validate_unsigned_int "$(ORLIX_KERNEL_BASE_ROOT_HOST_BLOCK_DEVICE)"; \
	validate_unsigned_int "$(ORLIX_KERNEL_STATE_ROOT_HOST_BLOCK_DEVICE)"; \
	validate_unsigned_int "$(ORLIX_KERNEL_STATE_ROOT_MINIMUM_BYTES)"; \
	expected_output="$(CURDIR)/Build/OrlixKernel/payload/$(ORLIX_KERNEL_PAYLOAD_BUNDLE_NAME).$(ORLIX_KERNEL_PAYLOAD_BUNDLE_EXTENSION)"; \
	[ "$$output" = "$$expected_output" ] || { echo "OrlixKernel payload path must come from OrlixOS target metadata: $$output" >&2; exit 1; }; \
	for path in Build Build/OrlixKernel Build/OrlixKernel/payload "$$output"; do \
		if [ -L "$$path" ]; then echo "refusing to package OrlixKernel payload through symlinked path: $$path" >&2; exit 1; fi; \
	done; \
	rootfs_input="$(ORLIX_KERNEL_TEST_INITRAMFS_INPUT)"; \
	if [ -z "$$rootfs_input" ] && [ -n "$(ORLIX_KERNEL_ROOT_INITRAMFS_INPUT)" ]; then rootfs_input="$(ORLIX_KERNEL_ROOT_INITRAMFS_INPUT)"; fi; \
	if [ -z "$$rootfs_input" ]; then rootfs_input="$(ORLIX_KERNEL_BUILD_DIR)/usr/initramfs_inc_data"; fi; \
	payload_boot_profile="$(PROFILE)"; \
	selected_root_mode="$(ORLIX_KERNEL_SELECTED_ROOT_MODE)"; \
	payload_kernel_command_line="$(ORLIX_KERNEL_COMMAND_LINE)"; \
	if [ -n "$(ORLIX_KERNEL_TEST_INITRAMFS_INPUT)" ]; then \
		selected_root_mode="$$(printf '%s\n' "$(ORLIX_KERNEL_ROOT_MODES)" | tr ' ' '\n' | awk '/^initramfs($$|-)/ { print; exit }')"; \
		[ -n "$$selected_root_mode" ] || { echo "OrlixOS target metadata has no initramfs root mode in ORLIX_KERNEL_ROOT_MODES=$(ORLIX_KERNEL_ROOT_MODES)" >&2; exit 1; }; \
		payload_kernel_command_line="$(ORLIX_KERNEL_TEST_INITRAMFS_COMMAND_LINE)"; \
	fi; \
	[ -n "$$payload_kernel_command_line" ] || { echo "missing OrlixOS target kernel command line metadata" >&2; exit 1; }; \
	base_root_tree_input="$(ORLIX_KERNEL_BASE_ROOT_TREE_INPUT)"; \
	state_root_tree_input="$(ORLIX_KERNEL_STATE_ROOT_TREE_INPUT)"; \
	case "$$rootfs_input" in \
		"$(CURDIR)"/Build/OrlixKernel/test-initramfs/*/rootfs/initramfs.cpio.gz|"$(CURDIR)"/Build/OrlixMLibC/test-initramfs/*/rootfs/initramfs.cpio.gz|"$(CURDIR)"/Build/OrlixOS/test-initramfs/*/*/rootfs/initramfs.cpio.gz|"$(CURDIR)"/Build/OrlixOS/rootfs/*/rootfs/initramfs.cpio.gz|"$(ORLIX_KERNEL_BUILD_DIR)"/usr/initramfs_inc_data) ;; \
		*) echo "refusing to package root initramfs outside Orlix Build roots: $$rootfs_input" >&2; exit 1 ;; \
	esac; \
	if [ -n "$$base_root_tree_input" ]; then \
		case "$$base_root_tree_input" in \
			"$(CURDIR)"/Build/OrlixOS/rootfs/*/base-tree) ;; \
			*) echo "refusing to package base root tree outside Build/OrlixOS/rootfs: $$base_root_tree_input" >&2; exit 1 ;; \
		esac; \
		[ -d "$$base_root_tree_input" ] || { echo "missing OrlixOS base root tree: $$base_root_tree_input" >&2; exit 1; }; \
	fi; \
	if [ -n "$$state_root_tree_input" ]; then \
		case "$$state_root_tree_input" in \
			"$(CURDIR)"/Build/OrlixOS/rootfs/*/state-tree) ;; \
			*) echo "refusing to package state root tree outside Build/OrlixOS/rootfs: $$state_root_tree_input" >&2; exit 1 ;; \
		esac; \
		[ -d "$$state_root_tree_input" ] || { echo "missing OrlixOS state root tree: $$state_root_tree_input" >&2; exit 1; }; \
	fi; \
	[ -s "$$rootfs_input" ] || { echo "missing non-empty root initramfs: $$rootfs_input" >&2; exit 1; }; \
	command -v shasum >/dev/null 2>&1 || { echo "shasum is required to verify OrlixKernel payload rootfs input" >&2; exit 1; }; \
	rootfs_sha256="$$(shasum -a 256 "$$rootfs_input" | awk '{ print $$1 }')"; \
	base_root_tree_sha256=none; \
	if [ -n "$$base_root_tree_input" ]; then \
		base_root_tree_sha256="$$(cd "$$base_root_tree_input" && find . -mindepth 1 -print | LC_ALL=C sort | while IFS= read -r path; do \
			if [ -L "$$path" ]; then printf 'link %s %s\n' "$$path" "$$(readlink "$$path")"; \
			elif [ -f "$$path" ]; then printf 'file %s ' "$$path"; shasum -a 256 "$$path"; \
			elif [ -d "$$path" ]; then printf 'dir %s\n' "$$path"; \
			fi; \
		done | shasum -a 256 | awk '{ print $$1 }')"; \
	fi; \
	state_root_tree_sha256=none; \
	if [ -n "$$state_root_tree_input" ]; then \
		state_root_tree_sha256="$$(cd "$$state_root_tree_input" && find . -mindepth 1 -print | LC_ALL=C sort | while IFS= read -r path; do \
			if [ -L "$$path" ]; then printf 'link %s %s\n' "$$path" "$$(readlink "$$path")"; \
			elif [ -f "$$path" ]; then printf 'file %s ' "$$path"; shasum -a 256 "$$path"; \
			elif [ -d "$$path" ]; then printf 'dir %s\n' "$$path"; \
			fi; \
		done | shasum -a 256 | awk '{ print $$1 }')"; \
	fi; \
	payload_stamp="$$output/.orlix-payload-ready"; \
	if [ -s "$$payload_stamp" ] && \
		[ "$$payload_stamp" -nt "$(ORLIX_KERNEL_BUILD_DIR)/arch/$(ORLIX_PORT_ARCH)/boot/dts/release.dtb" ] && \
		[ "$$payload_stamp" -nt "$(ORLIX_KERNEL_BUILD_DIR)/arch/$(ORLIX_PORT_ARCH)/boot/dts/development.dtb" ] && \
		[ "$$payload_stamp" -nt "$$rootfs_input" ] && \
		[ "$$payload_stamp" -nt "OrlixKernel/Sources/ports/orlix/kbuild/kernel-rules.mk" ] && \
		[ "$$(sed -n 's/^rootfs_input=//p' "$$payload_stamp")" = "$$rootfs_input" ] && \
		[ "$$(sed -n 's/^rootfs_sha256=//p' "$$payload_stamp")" = "$$rootfs_sha256" ] && \
		[ "$$(sed -n 's/^base_root_tree_input=//p' "$$payload_stamp")" = "$$base_root_tree_input" ] && \
		[ "$$(sed -n 's/^base_root_tree_sha256=//p' "$$payload_stamp")" = "$$base_root_tree_sha256" ] && \
		[ "$$(sed -n 's/^state_root_tree_input=//p' "$$payload_stamp")" = "$$state_root_tree_input" ] && \
		[ "$$(sed -n 's/^state_root_tree_sha256=//p' "$$payload_stamp")" = "$$state_root_tree_sha256" ] && \
		[ "$$(sed -n 's/^payload_boot_profile=//p' "$$payload_stamp")" = "$$payload_boot_profile" ] && \
		[ "$$(sed -n 's/^kernel_command_line=//p' "$$payload_stamp")" = "$$payload_kernel_command_line" ] && \
		[ "$$(sed -n 's/^payload_bundle_name=//p' "$$payload_stamp")" = "$(ORLIX_KERNEL_PAYLOAD_BUNDLE_NAME)" ] && \
		[ "$$(sed -n 's/^payload_bundle_extension=//p' "$$payload_stamp")" = "$(ORLIX_KERNEL_PAYLOAD_BUNDLE_EXTENSION)" ] && \
		[ "$$(sed -n 's/^payload_bundle_identifier=//p' "$$payload_stamp")" = "$(ORLIX_KERNEL_PAYLOAD_BUNDLE_IDENTIFIER)" ] && \
		[ "$$(sed -n 's/^linux_page_size=//p' "$$payload_stamp")" = "$(ORLIX_KERNEL_LINUX_PAGE_SIZE)" ] && \
		[ "$$(sed -n 's/^root_initramfs_resource=//p' "$$payload_stamp")" = "$(ORLIX_KERNEL_ROOT_INITRAMFS_RESOURCE)" ] && \
		[ "$$(sed -n 's/^base_root_image_resource=//p' "$$payload_stamp")" = "$(ORLIX_KERNEL_BASE_ROOT_IMAGE_RESOURCE)" ] && \
		[ "$$(sed -n 's/^state_root_image_resource=//p' "$$payload_stamp")" = "$(ORLIX_KERNEL_STATE_ROOT_IMAGE_RESOURCE)" ] && \
		[ "$$(sed -n 's/^root_modes=//p' "$$payload_stamp")" = "$(ORLIX_KERNEL_ROOT_MODES)" ] && \
		[ "$$(sed -n 's/^selected_root_mode=//p' "$$payload_stamp")" = "$$selected_root_mode" ] && \
		[ "$$(sed -n 's/^base_root_device=//p' "$$payload_stamp")" = "$(ORLIX_KERNEL_BASE_ROOT_DEVICE)" ] && \
		[ "$$(sed -n 's/^state_root_device=//p' "$$payload_stamp")" = "$(ORLIX_KERNEL_STATE_ROOT_DEVICE)" ] && \
		[ "$$(sed -n 's/^base_root_host_block_device=//p' "$$payload_stamp")" = "$(ORLIX_KERNEL_BASE_ROOT_HOST_BLOCK_DEVICE)" ] && \
		[ "$$(sed -n 's/^state_root_host_block_device=//p' "$$payload_stamp")" = "$(ORLIX_KERNEL_STATE_ROOT_HOST_BLOCK_DEVICE)" ] && \
		[ "$$(sed -n 's/^base_root_image_size=//p' "$$payload_stamp")" = "$(ORLIX_KERNEL_BASE_ROOT_IMAGE_SIZE)" ] && \
		[ "$$(sed -n 's/^state_root_image_size=//p' "$$payload_stamp")" = "$(ORLIX_KERNEL_STATE_ROOT_IMAGE_SIZE)" ] && \
		[ "$$(sed -n 's/^state_root_minimum_bytes=//p' "$$payload_stamp")" = "$(ORLIX_KERNEL_STATE_ROOT_MINIMUM_BYTES)" ] && \
		[ -s "$$output/Info.plist" ] && \
		[ -s "$$output/arch/$(ORLIX_PORT_ARCH)/boot/dts/release.dtb" ] && \
		[ -s "$$output/arch/$(ORLIX_PORT_ARCH)/boot/dts/development.dtb" ] && \
		[ -s "$$output/$(ORLIX_KERNEL_ROOT_INITRAMFS_RESOURCE)" ] && \
		[ -s "$$output/$(ORLIX_KERNEL_BASE_ROOT_IMAGE_RESOURCE)" ] && \
		[ -s "$$output/$(ORLIX_KERNEL_STATE_ROOT_IMAGE_RESOURCE)" ]; then \
		echo "reusing OrlixKernel payload: $$output (profile $(PROFILE))"; \
		exit 0; \
	fi; \
	rm -rf "$$output"; \
	mkdir -p "$$output/arch/$(ORLIX_PORT_ARCH)/boot/dts"; \
	for dtb in release development; do \
		input="$(ORLIX_KERNEL_BUILD_DIR)/arch/$(ORLIX_PORT_ARCH)/boot/dts/$$dtb.dtb"; \
		[ -s "$$input" ] || { echo "missing non-empty profile DTB: $$input" >&2; exit 1; }; \
		cp "$$input" "$$output/arch/$(ORLIX_PORT_ARCH)/boot/dts/$$dtb.dtb"; \
	done; \
	copy_payload_resource() { \
		input="$$1"; \
		resource="$$2"; \
		target="$$output/$$resource"; \
		mkdir -p "$$(dirname "$$target")"; \
		cp "$$input" "$$target"; \
	}; \
	copy_payload_resource "$$rootfs_input" "$(ORLIX_KERNEL_ROOT_INITRAMFS_RESOURCE)"; \
	mke2fs_cmd="$(ORLIX_MKE2FS)"; \
	command -v "$$mke2fs_cmd" >/dev/null 2>&1 || { echo "mke2fs is required to generate OrlixKernel root block images; install e2fsprogs or set ORLIX_MKE2FS=/path/to/mke2fs" >&2; exit 1; }; \
	rootfs_build="$(ORLIX_KERNEL_ROOTFS_BUILD_DIR)"; \
	base_tree="$$rootfs_build/base-tree"; \
	base_image="$(ORLIX_KERNEL_BASE_ROOT_IMAGE)"; \
	state_tree="$$rootfs_build/state-tree"; \
	state_image="$(ORLIX_KERNEL_STATE_ROOT_IMAGE)"; \
	rm -rf "$$base_tree" "$$base_image" "$$state_tree" "$$state_image"; \
	mkdir -p "$$base_tree/dev" "$$base_tree/proc" "$$base_tree/root" "$$base_tree/run" "$$base_tree/sys" "$$base_tree/tmp"; \
	if [ -n "$$base_root_tree_input" ]; then \
		(cd "$$base_root_tree_input" && tar -cf - .) | (cd "$$base_tree" && tar -xf -); \
	fi; \
	chmod 0755 "$$base_tree" "$$base_tree/dev" "$$base_tree/proc" "$$base_tree/run" "$$base_tree/sys"; \
	[ -d "$$base_tree/root" ] && chmod 0700 "$$base_tree/root"; \
	[ -d "$$base_tree/tmp" ] && chmod 1777 "$$base_tree/tmp"; \
	truncate -s "$(ORLIX_KERNEL_BASE_ROOT_IMAGE_SIZE)" "$$base_image"; \
	"$$mke2fs_cmd" -q -t ext4 -F -m 0 -U clear -L ORLIXROOT -E root_owner=0:0 -d "$$base_tree" "$$base_image"; \
	copy_payload_resource "$$base_image" "$(ORLIX_KERNEL_BASE_ROOT_IMAGE_RESOURCE)"; \
	mkdir -p "$$state_tree/upper" "$$state_tree/work"; \
	if [ -n "$$state_root_tree_input" ]; then \
		(cd "$$state_root_tree_input" && tar -cf - .) | (cd "$$state_tree" && tar -xf -); \
	fi; \
	chmod 0755 "$$state_tree" "$$state_tree/upper" "$$state_tree/work"; \
	truncate -s "$(ORLIX_KERNEL_STATE_ROOT_IMAGE_SIZE)" "$$state_image"; \
	"$$mke2fs_cmd" -q -t ext4 -F -m 0 -U clear -L ORLIXSTATE -E root_owner=0:0 -d "$$state_tree" "$$state_image"; \
	copy_payload_resource "$$state_image" "$(ORLIX_KERNEL_STATE_ROOT_IMAGE_RESOURCE)"; \
	{ \
		printf '%s\n' '<?xml version="1.0" encoding="UTF-8"?>'; \
		printf '%s\n' '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">'; \
		printf '%s\n' '<plist version="1.0">'; \
		printf '%s\n' '<dict>'; \
		printf '%s\n' '    <key>CFBundleIdentifier</key>'; \
		printf '%s\n' '    <string>$(ORLIX_KERNEL_PAYLOAD_BUNDLE_IDENTIFIER)</string>'; \
		printf '%s\n' '    <key>CFBundleName</key>'; \
		printf '%s\n' '    <string>$(ORLIX_KERNEL_PAYLOAD_BUNDLE_NAME)</string>'; \
		printf '%s\n' '    <key>CFBundlePackageType</key>'; \
		printf '%s\n' '    <string>BNDL</string>'; \
		printf '%s\n' '    <key>CFBundleShortVersionString</key>'; \
		printf '%s\n' '    <string>0.1</string>'; \
		printf '%s\n' '    <key>CFBundleVersion</key>'; \
		printf '%s\n' '    <string>1</string>'; \
		printf '%s\n' '    <key>OrlixLinuxArch</key>'; \
		printf '%s\n' '    <string>$(ORLIX_PORT_ARCH)</string>'; \
		printf '%s\n' '    <key>OrlixLinuxVersion</key>'; \
		printf '%s\n' '    <string>$(LINUX_VERSION)</string>'; \
		printf '%s\n' '    <key>OrlixLinuxPageSize</key>'; \
		printf '%s\n' '    <integer>$(ORLIX_KERNEL_LINUX_PAGE_SIZE)</integer>'; \
		printf '    <key>%s</key>\n' "$(ORLIX_KERNEL_PAYLOAD_SELECTED_PROFILE_INFO_KEY)"; \
		printf '    <string>%s</string>\n' "$$payload_boot_profile"; \
		printf '    <key>%s</key>\n' "$(ORLIX_KERNEL_PAYLOAD_KERNEL_COMMAND_LINE_INFO_KEY)"; \
		printf '    <string>%s</string>\n' "$$payload_kernel_command_line"; \
		printf '%s\n' '    <key>OrlixRootModes</key>'; \
		printf '%s\n' '    <string>$(ORLIX_KERNEL_ROOT_MODES)</string>'; \
		printf '%s\n' '    <key>OrlixSelectedRootMode</key>'; \
		printf '    <string>%s</string>\n' "$$selected_root_mode"; \
		printf '    <key>%s</key>\n' "$(ORLIX_KERNEL_PAYLOAD_BASE_ROOT_DEVICE_INFO_KEY)"; \
		printf '%s\n' '    <string>$(ORLIX_KERNEL_BASE_ROOT_DEVICE)</string>'; \
		printf '    <key>%s</key>\n' "$(ORLIX_KERNEL_PAYLOAD_STATE_ROOT_DEVICE_INFO_KEY)"; \
		printf '%s\n' '    <string>$(ORLIX_KERNEL_STATE_ROOT_DEVICE)</string>'; \
		printf '    <key>%s</key>\n' "$(ORLIX_KERNEL_PAYLOAD_BASE_ROOT_HOST_BLOCK_DEVICE_INFO_KEY)"; \
		printf '%s\n' '    <integer>$(ORLIX_KERNEL_BASE_ROOT_HOST_BLOCK_DEVICE)</integer>'; \
		printf '    <key>%s</key>\n' "$(ORLIX_KERNEL_PAYLOAD_STATE_ROOT_HOST_BLOCK_DEVICE_INFO_KEY)"; \
		printf '%s\n' '    <integer>$(ORLIX_KERNEL_STATE_ROOT_HOST_BLOCK_DEVICE)</integer>'; \
		printf '    <key>%s</key>\n' "$(ORLIX_KERNEL_PAYLOAD_STATE_ROOT_MINIMUM_BYTES_INFO_KEY)"; \
		printf '%s\n' '    <integer>$(ORLIX_KERNEL_STATE_ROOT_MINIMUM_BYTES)</integer>'; \
		printf '    <key>%s</key>\n' "$(ORLIX_KERNEL_PAYLOAD_ROOT_INITRAMFS_INFO_KEY)"; \
		printf '%s\n' '    <string>$(ORLIX_KERNEL_ROOT_INITRAMFS_RESOURCE)</string>'; \
		printf '    <key>%s</key>\n' "$(ORLIX_KERNEL_PAYLOAD_BASE_ROOT_IMAGE_INFO_KEY)"; \
		printf '%s\n' '    <string>$(ORLIX_KERNEL_BASE_ROOT_IMAGE_RESOURCE)</string>'; \
		printf '    <key>%s</key>\n' "$(ORLIX_KERNEL_PAYLOAD_STATE_ROOT_IMAGE_INFO_KEY)"; \
		printf '%s\n' '    <string>$(ORLIX_KERNEL_STATE_ROOT_IMAGE_RESOURCE)</string>'; \
		printf '%s\n' '</dict>'; \
		printf '%s\n' '</plist>'; \
	} > "$$output/Info.plist"; \
	plutil -lint "$$output/Info.plist" >/dev/null; \
	printf 'profile=%s\nlinux_version=%s\nrootfs_input=%s\nrootfs_sha256=%s\nbase_root_tree_input=%s\nbase_root_tree_sha256=%s\nstate_root_tree_input=%s\nstate_root_tree_sha256=%s\npayload_boot_profile=%s\nkernel_command_line=%s\npayload_bundle_name=%s\npayload_bundle_extension=%s\npayload_bundle_identifier=%s\nlinux_page_size=%s\nroot_initramfs_resource=%s\nbase_root_image_resource=%s\nstate_root_image_resource=%s\nroot_modes=%s\nselected_root_mode=%s\nbase_root_device=%s\nstate_root_device=%s\nbase_root_host_block_device=%s\nstate_root_host_block_device=%s\nbase_root_image_size=%s\nstate_root_image_size=%s\nstate_root_minimum_bytes=%s\n' "$(PROFILE)" "$(LINUX_VERSION)" "$$rootfs_input" "$$rootfs_sha256" "$$base_root_tree_input" "$$base_root_tree_sha256" "$$state_root_tree_input" "$$state_root_tree_sha256" "$$payload_boot_profile" "$$payload_kernel_command_line" "$(ORLIX_KERNEL_PAYLOAD_BUNDLE_NAME)" "$(ORLIX_KERNEL_PAYLOAD_BUNDLE_EXTENSION)" "$(ORLIX_KERNEL_PAYLOAD_BUNDLE_IDENTIFIER)" "$(ORLIX_KERNEL_LINUX_PAGE_SIZE)" "$(ORLIX_KERNEL_ROOT_INITRAMFS_RESOURCE)" "$(ORLIX_KERNEL_BASE_ROOT_IMAGE_RESOURCE)" "$(ORLIX_KERNEL_STATE_ROOT_IMAGE_RESOURCE)" "$(ORLIX_KERNEL_ROOT_MODES)" "$$selected_root_mode" "$(ORLIX_KERNEL_BASE_ROOT_DEVICE)" "$(ORLIX_KERNEL_STATE_ROOT_DEVICE)" "$(ORLIX_KERNEL_BASE_ROOT_HOST_BLOCK_DEVICE)" "$(ORLIX_KERNEL_STATE_ROOT_HOST_BLOCK_DEVICE)" "$(ORLIX_KERNEL_BASE_ROOT_IMAGE_SIZE)" "$(ORLIX_KERNEL_STATE_ROOT_IMAGE_SIZE)" "$(ORLIX_KERNEL_STATE_ROOT_MINIMUM_BYTES)" > "$$payload_stamp"; \
	echo "packaged OrlixKernel payload: $$output (profile $(PROFILE))"

__ios-simulator-framework: xcodeproj
	@set -euo pipefail; \
	$(call orlix_kernel_acquire_profile_lock); \
	$(MAKE) -f OrlixKernel/Makefile __kernel-archive PROFILE="$(PROFILE)" type="$(type)" libc="$(libc)" ORLIX_KERNEL_ARCHIVE_PLATFORMS=iphonesimulator; \
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
	framework="$(ORLIX_IOS_SIMULATOR_FRAMEWORK)"; \
	xcframework="$(ORLIX_KERNEL_XCFRAMEWORK)"; \
	case "$$xcframework" in \
		"$(CURDIR)/Build/OrlixKernel/xcframework/OrlixKernel.xcframework"|Build/OrlixKernel/xcframework/OrlixKernel.xcframework) ;; \
		*) echo "refusing to write simulator XCFramework outside Build/OrlixKernel/xcframework: $$xcframework" >&2; exit 1 ;; \
	esac; \
	[ -d "$$framework" ] || { echo "missing simulator framework: $$framework" >&2; exit 1; }; \
	if [ -L Build ] || [ -L Build/OrlixKernel ] || [ -L Build/OrlixKernel/xcframework ] || [ -L "$$xcframework" ]; then \
		echo "refusing to package simulator XCFramework through symlinked Build path" >&2; \
		exit 1; \
	fi; \
	for required in "$$framework/OrlixKernel" "$$framework/Info.plist"; do \
		[ -s "$$required" ] || { echo "missing non-empty framework input: $$required" >&2; exit 1; }; \
	done; \
	xcframework_parent="$$(dirname "$$xcframework")"; \
	rm -rf "$$xcframework"; \
	mkdir -p "$$xcframework_parent" "$$xcframework/ios-arm64-simulator"; \
	cp -R "$$framework" "$$xcframework/ios-arm64-simulator/OrlixKernel.framework"; \
	{ \
		printf '%s\n' '<?xml version="1.0" encoding="UTF-8"?>'; \
		printf '%s\n' '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">'; \
		printf '%s\n' '<plist version="1.0">'; \
		printf '%s\n' '<dict>'; \
		printf '%s\n' '    <key>AvailableLibraries</key>'; \
		printf '%s\n' '    <array>'; \
		printf '%s\n' '        <dict>'; \
		printf '%s\n' '            <key>LibraryIdentifier</key>'; \
		printf '%s\n' '            <string>ios-arm64-simulator</string>'; \
		printf '%s\n' '            <key>LibraryPath</key>'; \
		printf '%s\n' '            <string>OrlixKernel.framework</string>'; \
		printf '%s\n' '            <key>SupportedArchitectures</key>'; \
		printf '%s\n' '            <array>'; \
		printf '%s\n' '                <string>arm64</string>'; \
		printf '%s\n' '            </array>'; \
		printf '%s\n' '            <key>SupportedPlatform</key>'; \
		printf '%s\n' '            <string>ios</string>'; \
		printf '%s\n' '            <key>SupportedPlatformVariant</key>'; \
		printf '%s\n' '            <string>simulator</string>'; \
		printf '%s\n' '        </dict>'; \
		printf '%s\n' '    </array>'; \
		printf '%s\n' '    <key>CFBundlePackageType</key>'; \
		printf '%s\n' '    <string>XFWK</string>'; \
		printf '%s\n' '    <key>XCFrameworkFormatVersion</key>'; \
		printf '%s\n' '    <string>1.0</string>'; \
		printf '%s\n' '</dict>'; \
		printf '%s\n' '</plist>'; \
	} > "$$xcframework/Info.plist"; \
	for required in \
		"$$xcframework/Info.plist" \
		"$$xcframework/ios-arm64-simulator/OrlixKernel.framework/Info.plist" \
		"$$xcframework/ios-arm64-simulator/OrlixKernel.framework/OrlixKernel"; do \
		[ -s "$$required" ] || { echo "missing non-empty XCFramework input: $$required" >&2; exit 1; }; \
	done; \
	plutil -lint \
		"$$xcframework/Info.plist" \
		"$$xcframework/ios-arm64-simulator/OrlixKernel.framework/Info.plist" >/dev/null; \
	echo "packaged and verified simulator OrlixKernel XCFramework: $$xcframework (profile $(PROFILE))"
