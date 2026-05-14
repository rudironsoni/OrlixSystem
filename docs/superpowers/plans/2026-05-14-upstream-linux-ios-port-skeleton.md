# Upstream Linux iOS Port Skeleton Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the local OrlixKernel rewrite architecture with an upstream-Linux `ARCH=orlix` bootstrap/build skeleton that materializes Linux locally, overlays Orlix port files, and exposes only a bootloader-shaped product surface.

**Architecture:** Upstream Linux is fetched locally by Makefile target and never committed. `Build/linux-work` is disposable and generated from `Linux/upstream/linux-6.12` plus `Linux/ports/orlix/overlay`, then built through Linux Kbuild with `ARCH=orlix LLVM=1`. `OrlixKernel/include/OrlixKernel.h` becomes the minimal product surface, while the existing local runtime code is removed from build truth rather than kept as compatibility.

**Tech Stack:** GNU Make, Git, rsync, POSIX shell inside `Makefile` recipes, Linux Kbuild, LLVM/Clang, XcodeGen, Xcode static-library/XCFramework packaging.

---

## Tasks

### Task 1: Ignore Generated Linux Trees

**Files:**
- Modify: `.gitignore`

- [ ] Add ignore rules for `Linux/upstream/linux-6.12/`, `Build/`, `DerivedData/`, and `*.xcframework/`.
- [ ] Run `git check-ignore Linux/upstream/linux-6.12/ Build/linux-work/ Build/OrlixKernel.xcframework/` and expect all three paths printed.
- [ ] Commit with `build: ignore generated Linux port artifacts`.

### Task 2: Add Makefile-Only Upstream Linux Bootstrap

**Files:**
- Modify: `Makefile`

- [ ] Add `LINUX_TAG`, `LINUX_REMOTE`, `LINUX_UPSTREAM_DIR`, `LINUX_WORK_DIR`, `ORLIX_LINUX_OVERLAY`, `ORLIX_LINUX_PATCH_DIR`, and `ORLIX_XCFRAMEWORK_DIR` variables.
- [ ] Add `bootstrap-linux-upstream` recipe directly in `Makefile`. It must clone/fetch `$(LINUX_REMOTE)`, checkout detached `$(LINUX_TAG)`, and verify the exact checked-out tag.
- [ ] Run `make bootstrap-linux-upstream` and expect `upstream Linux ready: Linux/upstream/linux-6.12 (v6.12)`.
- [ ] Run `git status --short Linux/upstream/linux-6.12` and expect no output.
- [ ] Commit with `build: bootstrap upstream Linux from Makefile`.

### Task 3: Add Makefile-Only Disposable Linux Worktree Preparation

**Files:**
- Modify: `Makefile`
- Create: `Linux/ports/orlix/patches/exceptions/README.md`

- [ ] Create patch exception policy documenting forbidden upstream paths: `fs/`, `kernel/`, `mm/`, `ipc/`, `net/`, `include/linux/`, and `include/uapi/`.
- [ ] Add `prepare-linux-worktree` recipe directly in `Makefile`. It must depend on `bootstrap-linux-upstream`, recreate `Build/linux-work`, archive upstream Linux into it, overlay `Linux/ports/orlix/overlay`, apply patches, and fail if a patch touches a forbidden path without `Linux/ports/orlix/patches/exceptions/<patch-name>.md`.
- [ ] Run `make prepare-linux-worktree` and expect `prepared Linux worktree: Build/linux-work`.
- [ ] Run `test -f Build/linux-work/Makefile && test -d Build/linux-work/arch && test -d Build/linux-work/drivers` and expect exit `0`.
- [ ] Commit with `build: prepare Linux worktree from Makefile`.

### Task 4: Add Minimal `arch/orlix` Kbuild Skeleton

**Files:**
- Create: `Linux/ports/orlix/overlay/arch/orlix/Kconfig`
- Create: `Linux/ports/orlix/overlay/arch/orlix/Makefile`
- Create: `Linux/ports/orlix/overlay/arch/orlix/boot/Makefile`
- Create: `Linux/ports/orlix/overlay/arch/orlix/boot/boot.c`
- Create: `Linux/ports/orlix/overlay/arch/orlix/kernel/Makefile`
- Create: `Linux/ports/orlix/overlay/arch/orlix/kernel/setup.c`
- Create: `Linux/ports/orlix/overlay/arch/orlix/include/asm/Kbuild`
- Create: `Linux/ports/orlix/overlay/arch/orlix/include/asm/boot.h`

- [ ] Create `arch/orlix/Kconfig` with `config ORLIX`, `config ARCH_DEFCONFIG`, and `source "init/Kconfig"`.
- [ ] Create `arch/orlix/Makefile` with `KBUILD_DEFCONFIG := defconfig`, `head-y := arch/orlix/kernel/setup.o`, and `core-y` entries for `kernel/` and `boot/`.
- [ ] Create `boot/Makefile` with `obj-y += boot.o`.
- [ ] Create `boot/boot.c` defining `void arch_boot_entry(const struct boot_params *params)`.
- [ ] Create `kernel/Makefile` with `obj-y += setup.o`.
- [ ] Create `kernel/setup.c` defining `void __init setup_arch(char **cmdline_p)` and logging `Orlix architecture setup`.
- [ ] Create `include/asm/Kbuild` forwarding generic asm headers.
- [ ] Create `include/asm/boot.h` defining `struct boot_params` and `arch_boot_entry`.
- [ ] Run `make prepare-linux-worktree && test -f Build/linux-work/arch/orlix/Kconfig && test -f Build/linux-work/arch/orlix/Makefile && test -f Build/linux-work/arch/orlix/boot/boot.c && test -f Build/linux-work/arch/orlix/kernel/setup.c` and expect exit `0`.
- [ ] Commit with `linux: add arch/orlix skeleton`.

### Task 5: Add Minimal `drivers/orlix` Kbuild Skeleton

**Files:**
- Create: `Linux/ports/orlix/overlay/drivers/orlix/Kconfig`
- Create: `Linux/ports/orlix/overlay/drivers/orlix/Makefile`
- Create: `Linux/ports/orlix/overlay/drivers/orlix/block/Kconfig`
- Create: `Linux/ports/orlix/overlay/drivers/orlix/block/Makefile`
- Create: `Linux/ports/orlix/overlay/drivers/orlix/block/file.c`
- Create: `Linux/ports/orlix/overlay/drivers/orlix/block/image.c`
- Create: `Linux/ports/orlix/overlay/drivers/orlix/tty/Kconfig`
- Create: `Linux/ports/orlix/overlay/drivers/orlix/tty/Makefile`
- Create: `Linux/ports/orlix/overlay/drivers/orlix/tty/console.c`
- Create: `Linux/ports/orlix/overlay/drivers/orlix/char/Kconfig`
- Create: `Linux/ports/orlix/overlay/drivers/orlix/char/Makefile`
- Create: `Linux/ports/orlix/overlay/drivers/orlix/char/random.c`

- [ ] Create Kconfig and Makefile files for top-level `drivers/orlix`, `block`, `tty`, and `char`.
- [ ] Create minimal Linux module-style sources for `block/file.c`, `block/image.c`, `tty/console.c`, and `char/random.c` with `module_init`, `module_exit`, `MODULE_LICENSE("GPL")`, and a descriptive `pr_info` registration message.
- [ ] Run `make prepare-linux-worktree && test -f Build/linux-work/drivers/orlix/Kconfig && test -f Build/linux-work/drivers/orlix/block/file.c && test -f Build/linux-work/drivers/orlix/tty/console.c && test -f Build/linux-work/drivers/orlix/char/random.c` and expect exit `0`.
- [ ] Commit with `linux: add drivers/orlix skeleton`.

### Task 6: Add Profile Defconfigs

**Files:**
- Create: `Linux/ports/orlix/configs/appstore_defconfig`
- Create: `Linux/ports/orlix/configs/development_defconfig`
- Create: `Linux/ports/orlix/configs/enterprise_defconfig`

- [ ] Create all three defconfig files with Orlix block, TTY, char, initrd, devtmpfs, procfs, sysfs, cgroups, namespaces, tmpfs, ext4, inet, unix sockets, script and ELF binary format support, and modules disabled. Keep seccomp disabled until `arch/orlix` provides the required Linux seccomp hooks.
- [ ] Ensure `appstore_defconfig` disables `CONFIG_BPF_JIT`.
- [ ] Ensure `development_defconfig` enables `CONFIG_KALLSYMS` and `CONFIG_DEBUG_KERNEL`.
- [ ] Run `test -f Linux/ports/orlix/configs/appstore_defconfig && test -f Linux/ports/orlix/configs/development_defconfig && test -f Linux/ports/orlix/configs/enterprise_defconfig` and expect exit `0`.
- [ ] Commit with `linux: add Orlix build profile defconfigs`.

### Task 7: Add Product Header and Bootloader Sources

**Files:**
- Create: `OrlixKernel/include/OrlixKernel.h`
- Create: `boot/loader.c`
- Create: `boot/params.c`
- Create: `boot/image.c`
- Create: `boot/initrd.c`
- Create: `boot/rootfs.c`
- Create: `boot/dtb.c`

- [ ] Create `OrlixKernel/include/OrlixKernel.h` with `struct boot_params`, `OrlixPrepareBootParams`, and `OrlixBoot` only as public API.
- [ ] Create boot sources implementing `OrlixBoot`, `OrlixPrepareBootParams`, `OrlixLoadKernelImage`, `OrlixLoadInitrd`, `OrlixSelectRootImage`, and `OrlixLoadDeviceTree` with concrete `-1` failure behavior for invalid inputs.
- [ ] Run `grep -R "OrlixKernelSyscall\|OrlixKernelOpen\|OrlixKernelRead\|OrlixKernelMount\|OrlixKernelExecve" OrlixKernel boot` and expect no matches.
- [ ] Commit with `kernel: expose bootloader-shaped product surface`.

### Task 8: Replace XcodeGen Build Truth

**Files:**
- Modify: `project.yml`

- [ ] Replace `project.yml` with a project named `Orlix`, with `OrlixKernel` building only `boot/*.c` and `OrlixHostAdapter` remaining as a private static library. Do not include `OrlixKernelTests`, `OrlixHostAdapterTests`, `OrlixKernel/fs`, `OrlixKernel/kernel`, or `OrlixKernel/runtime`.
- [ ] Run `xcodegen generate --project .` and expect `Generating project...`.
- [ ] Run `grep -R "OrlixKernel/fs\|OrlixKernel/kernel\|OrlixKernel/runtime" OrlixKernel.xcodeproj/project.pbxproj` and expect no matches.
- [ ] Run `xcodebuild build -project OrlixKernel.xcodeproj -scheme OrlixKernel-6.12-arm64 -sdk iphonesimulator -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17'` and expect `** BUILD SUCCEEDED **`.
- [ ] Commit with `build: switch Xcode project to boot product surface`.

### Task 9: Add Makefile-Only Linux Build and Package Targets

**Files:**
- Modify: `Makefile`

- [ ] Add `build-linux-simulator`, `build-linux-iphoneos`, and `package-orlixkernel-xcframework` recipes directly in `Makefile`.
- [ ] `build-linux-simulator` must depend on `prepare-linux-worktree`, use `Build/linux-simulator`, and run Linux Kbuild with `ARCH=orlix LLVM=1 mrproper defconfig`.
- [ ] `build-linux-iphoneos` must depend on `prepare-linux-worktree`, use `Build/linux-iphoneos`, and run Linux Kbuild with `ARCH=orlix LLVM=1 mrproper defconfig`.
- [ ] `package-orlixkernel-xcframework` must depend on both build targets and create `Build/OrlixKernel.xcframework` with copied `OrlixKernel.h` headers for device and simulator slices.
- [ ] Run `make build-linux-simulator` and expect `simulator Linux configuration ready: Build/linux-simulator`.
- [ ] Run `make build-linux-iphoneos` and expect `iphoneos Linux configuration ready: Build/linux-iphoneos`.
- [ ] Run `make package-orlixkernel-xcframework` and expect `OrlixKernel.xcframework skeleton ready: Build/OrlixKernel.xcframework`.
- [ ] Commit with `build: add Linux port build targets to Makefile`.

### Task 10: Rewrite README for Upstream Linux Direction

**Files:**
- Modify: `README.md`
- Create: `docs/UPSTREAM_LINUX_IOS_PORT_SPEC.md`

- [ ] Replace `README.md` with documentation for the upstream Linux port, generated local upstream tree, committed Orlix port overlay, disposable worktree, bootloader-facing product surface, and Makefile proof targets.
- [ ] Create `docs/UPSTREAM_LINUX_IOS_PORT_SPEC.md` with the approved architecture rules: Orlix compiles upstream Linux, upstream Linux source is read-only local input, Linux core subsystems are not locally rewritten, `arch/orlix` owns architecture glue, `drivers/orlix` owns Orlix Linux drivers, `boot/` owns boot preparation, and `OrlixHostAdapter` owns private iOS/Darwin mechanics only.
- [ ] Run `grep -n "OrlixKernel/fs\|OrlixKernel/kernel\|OrlixKernel/runtime" README.md` and expect no matches.
- [ ] Commit with `docs: define upstream Linux port architecture`.

### Task 11: Run Final Proof

**Files:**
- No file changes.

- [ ] Run `git status --short` and expect no output.
- [ ] Run `make bootstrap-linux-upstream` and expect `upstream Linux ready: Linux/upstream/linux-6.12 (v6.12)`.
- [ ] Run `make prepare-linux-worktree` and expect `prepared Linux worktree: Build/linux-work`.
- [ ] Run `make build-linux-simulator` and expect `simulator Linux configuration ready: Build/linux-simulator`.
- [ ] Run `make build-linux-iphoneos` and expect `iphoneos Linux configuration ready: Build/linux-iphoneos`.
- [ ] Run `make package-orlixkernel-xcframework` and expect `OrlixKernel.xcframework skeleton ready: Build/OrlixKernel.xcframework`.
- [ ] Run `xcodegen generate --project .` and expect `Generating project...`.
- [ ] Run `xcodebuild build -project OrlixKernel.xcodeproj -scheme OrlixKernel-6.12-arm64 -sdk iphonesimulator -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17'` and expect `** BUILD SUCCEEDED **`.
- [ ] Run `git status --short` and expect no output. If only `OrlixKernel.xcodeproj` changed, commit it with `build: refresh generated Xcode project`.

## Self-Review

Covered: upstream Linux is local-only and generated by `Makefile`; no committed upstream Linux source; no scattered shell scripts; `Linux/ports/orlix/overlay`, `arch/orlix`, `drivers/orlix`, `boot/`, `OrlixHostAdapter`, and boot-shaped product header exist; local `OrlixKernel/fs`, `kernel`, and `runtime` are removed from build truth; profile defconfigs and patch exception policy exist; XcodeGen no longer builds old local subsystem implementation.

Not covered in this first skeleton: booting to `start_kernel`, real memory management, real syscall entry, real drivers, App Store execution policy, rootfs mount, bundled userspace boot, and procfs/sysfs/devfs/cgroupfs runtime proof.
