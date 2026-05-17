# Orlix

Orlix is an iOS-hosted upstream Linux port. The product goal is to run Linux userspace inside an iOS app by packaging upstream Linux as `OrlixKernel.xcframework` and pairing it with `OrlixMLibC`, an mlibc-based libc for Orlix Linux userspace.

Think of the app as the host container. It does not become Linux and it does not manage Linux through a custom runtime API. It starts a bootloader, the bootloader prepares Linux-shaped boot inputs, and Linux owns Linux after boot.

OrlixKernel is Linux. It does not provide a shell, libc, package manager, public syscall API, or fake runtime facade. Shells and packages are normal Orlix Linux userspace binaries linked against OrlixMLibC and executed through Linux mechanisms.

## ELI5 Start

Orlix has four important source areas:

- `Linux/upstream/linux-6.12` is generated upstream Linux source. Treat it as read-only input.
- `Linux/ports/orlix` is where durable Orlix Linux port inputs live.
- `OrlixMLibC` is the top-level component for the mlibc-based Orlix userspace C library.
- `OrlixHostAdapter` is where private iOS and Darwin mechanics live.

The old local kernel implementation under `OrlixKernel/fs`, `OrlixKernel/kernel`, and `OrlixKernel/runtime` is not the target architecture. Do not add new Linux subsystem behavior there. Useful behavior should be migrated by ownership into upstream Linux-native paths, then those directories should disappear.

## Proof Model

Proof is claim-promoted, not flat. Work may happen in parallel, but product claims must follow ADR 0017:

1. Kernel dependency proof
2. Kselftest kernel-interface proof
3. OrlixMLibC libc proof
4. OrlixMLibC-built syscall/UAPI proof
5. POSIX shell environment proof
6. Third-party package ladder: jq, curl, zsh

The canonical OrlixKernel proof artifact is the app-hosted OrlixKernel integration that actually runs in the Orlix app environment. A standalone `vmlinux` image is optional tooling output only for a named non-product consumer. KUnit proves kernel-internal behavior. Temporary, nolibc, or foreign-libc kselftest proves kernel-interface behavior only. mlibc tests prove OrlixMLibC. OrlixMLibC-built kselftests prove the libc-to-kernel syscall/UAPI path. Bash proves the first interactive POSIX shell environment. jq, curl, and zsh prove increasingly realistic third-party package compatibility.

Do not claim product runtime readiness from KUnit, temporary kselftest, boot logs, packaging, or a host-side harness.

## Build And Proof Commands

Bootstrapping upstream Linux is the first source step:

```bash
make bootstrap-linux-upstream
```

Milestone 1 build work must make this the generated port-tree step:

```bash
make prepare-orlixkernel-port PROFILE=appstore
```

Milestone 1 build work must make the app-hosted OrlixKernel integration build for the iOS destinations. Building a `vmlinux`-style image is optional and only valid when a named tooling/debug workflow consumes it.

`PROFILE=appstore` is the default profile. Pass another profile only when you intentionally need it.

The development profile should match the App Store profile except for explicit debug and testing affordances. Milestones that claim iOS packaging, boot, runtime, or Linux behavior should validate the same XCTest suite and assertions across App Store and development profiles on both `iphoneos` and `iphonesimulator`.

App Store, development, simulator, CI, and debug builds may produce separate artifacts, but they must not expose different Linux userspace ABIs. Installed UAPI headers, syscall numbers, struct layouts, OrlixMLibC ABI, dynamic-loader contract, package ABI, and observable Linux userspace behavior are profile-invariant.

Milestone 2 boot-entrypoint proof is intentionally narrower than booting Linux. It verifies that profile DTS sources are materialized into the generated Linux port tree.

```bash
make prepare-orlixkernel-port PROFILE=appstore
```

Milestone 2 does not prove QEMU execution, iOS execution, task switching, MMU behavior, userspace access, device binding, or root filesystem assembly.

Milestone 3 XCFramework packaging proof establishes the iOS execution artifact early. It must package or link the app-hosted OrlixKernel integration into the iOS host path; packaging boot stubs alone is not proof.

Prepare the iOS packaging inputs with:

```bash
make prepare-ios-packaging PROFILE=appstore
```

This generates the disposable Xcode project from `project.yml`. It must not make `vmlinux` a required proof artifact unless a named optional tooling workflow consumes it.

For simulator-first development, run the current simulator packaging proof with:

```bash
make proof-ios-simulator-packaging PROFILE=appstore
```

This verifies the simulator XCFramework wrapper shape and launches `OrlixTerminal` against the packaged framework. It is sequencing evidence only, not kernel runtime proof.

Build a simulator-only `OrlixKernel.xcframework` package with:

```bash
make package-ios-simulator-xcframework PROFILE=appstore
```

Verify that simulator-only package with:

```bash
make verify-ios-simulator-xcframework PROFILE=appstore
```

Launch the simulator host app with:

```bash
make run-ios-simulator-terminal PROFILE=appstore
```

These simulator targets are sequencing aids. They do not replace the eventual `iphoneos` proof cell.

Milestone 4 iOS-hosted kernel-interface test execution proves dependency behavior, not the final Orlix product runtime. KUnit proves kernel-internal behavior. Selected Linux kselftests may run through a temporary, nolibc, or foreign-libc harness as kernel-interface proof, but that temporary lane is not OrlixMLibC proof, final Orlix userspace ABI proof, shell proof, or package proof.

Build Orlix kselftest artifacts with Linux's kselftest build shape as preparatory evidence:

```bash
make prepare-orlixkernel-port PROFILE=appstore
make -C Build/OrlixKernel/linux-6.12-port/tools/testing/selftests TARGETS=orlix
```

Stage the Linux-shaped test initramfs resource for the XCTest proof host with:

```bash
make bootstrap-orlix-linux-userspace-sysroot
make stage-orlix-test-initramfs PROFILE=appstore ORLIX_LINUX_USERSPACE_SYSROOT=Build/OrlixKernel/linux-userspace-sysroot/aarch64
```

The bootstrap target creates a disposable Debian arm64 sysroot under `Build/OrlixKernel/linux-userspace-sysroot/aarch64`. The staging target uses upstream kselftest install shape and refuses to build with the Darwin host SDK. This is a temporary kernel-interface harness, not Orlix userspace ABI proof. The staged resource is for the forthcoming XCTest proof host bundle, not `OrlixKernel.xcframework` product payload, and is not required for Milestone 3 packaging tests.

Build Orlix KUnit artifacts with Linux Kbuild and the Orlix KUnit config as preparatory evidence:

```bash
cd Build/OrlixKernel/linux-6.12-port
make O=../kunit-orlix ARCH=orlix defconfig
scripts/kconfig/merge_config.sh -m -O ../kunit-orlix ../kunit-orlix/.config arch/orlix/.kunitconfig
make O=../kunit-orlix ARCH=orlix olddefconfig arch/orlix/boot/boot_test.o
```

Do not run kselftest or KUnit on Darwin and do not use a VM as product proof. Do not add repo-local shell or standalone C contract tests as milestone proof. Linux kernel-internal behavior belongs in KUnit. Linux kernel-interface behavior belongs in selected kselftests. Orlix userspace ABI and product runtime claims require the later ADR 0017 proof lanes.

Both `iphoneos` and `iphonesimulator` are iOS proof destinations. Milestones must validate the same scope on both.

Milestone 5 boot-to-virtio-probe proof keeps the dependency chain honest. It verifies that generated profile DTS files describe virtio-mmio probe-shape devices, that the selected generated profile defconfig enables upstream OF, virtio-mmio, and virtio-block probe paths, and that Orlix architecture boot handoff state is covered by kernel dependency or kernel-interface proof.

Milestone 5 does not prove `/dev/vda`, `/dev/vdb`, virtio-block request I/O, host-backed disk persistence, initramfs loading, OverlayFS root assembly, or general userspace boot.

## Generated Trees

The pristine upstream source is generated at:

```text
Linux/upstream/linux-6.12
```

The disposable upstream-plus-Orlix port tree is generated at:

```text
Build/OrlixKernel/linux-6.12-port
```

If a change should survive regeneration, put it in `Linux/ports/orlix`, not in a generated tree.

## Port Inputs

Durable Orlix Linux port inputs live under:

```text
Linux/ports/orlix/
  overlay/
  patches/
  configs/
```

`overlay` contains files copied into Linux-native paths such as `arch/orlix` and `drivers/orlix`.

`patches` contains minimal upstream-tree deltas that cannot be represented as overlay files.

`configs` contains product profile defconfigs such as `appstore_defconfig` and `development_defconfig`.

## Product Surface

The public product surface is bootloader-shaped. It should expose a minimal boot entrypoint such as `OrlixBoot` with an app-level boot config. It must not expose syscall, file, mount, exec, task, cgroup, or runtime management APIs.

## Current Proof Boundary

The current blocking proof boundary is iOS-hosted kernel-interface execution. Real-artifact XCFramework packaging is a prerequisite, not product runtime proof.

Success at this boundary means the iOS host launches packaged OrlixKernel and collects dependency proof from the running kernel path, such as KUnit output, Linux-accurate no-init behavior, or selected kselftests through a temporary harness. It does not mean OrlixMLibC, Bash, jq, curl, zsh, or product runtime compatibility is proved.

## Device Direction

Orlix is virtio-first where Linux already has upstream device classes.

Use upstream Linux behavior for Linux-visible devices:

- `virtio_blk` for root disks
- `virtio_console` for the main console path
- `virtio-rng` for entropy
- `virtio_net` for networking
- virtio-fs first, or 9p over virtio if needed, for external directory mounts

Orlix-specific code supplies transport and backend mechanics under `drivers/orlix`, shaped as close to Linux virtio conventions as possible.

## Read Deeper

The canonical architecture specification is:

```text
docs/UPSTREAM_LINUX_IOS_PORT_SPEC.md
```

Architecture decisions are recorded under:

```text
docs/adr/
```

Glossary terms resolved during design live in:

```text
CONTEXT.md
```
