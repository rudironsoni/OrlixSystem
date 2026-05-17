# Orlix

Orlix is an iOS-hosted upstream Linux port. The product goal is to boot and package upstream Linux as `OrlixKernel.xcframework`, using Linux-native extension points instead of rewriting Linux core subsystems locally.

Think of the app as the host container. It does not become Linux and it does not manage Linux through a custom runtime API. It starts a bootloader, the bootloader prepares Linux-shaped boot inputs, and Linux owns Linux after boot.

## ELI5 Start

Orlix has three important source areas:

- `Linux/upstream/linux-6.12` is generated upstream Linux source. Treat it as read-only input.
- `Linux/ports/orlix` is where durable Orlix Linux port inputs live.
- `OrlixHostAdapter` is where private iOS and Darwin mechanics live.

The old local kernel implementation under `OrlixKernel/fs`, `OrlixKernel/kernel`, and `OrlixKernel/runtime` is not the target architecture. Do not add new Linux subsystem behavior there. Useful behavior should be migrated by ownership into upstream Linux-native paths, then those directories should disappear.

## Milestone 1 Proof Commands

Bootstrapping upstream Linux is the first source step:

```bash
make bootstrap-linux-upstream
```

Milestone 1 build work must make this the generated port-tree step:

```bash
make prepare-orlixkernel-port PROFILE=appstore
```

Milestone 1 build work must make these the first honest Linux proof targets:

```bash
make build-linux-kernel PROFILE=appstore
make build-linux-kernel PROFILE=development
```

`PROFILE=appstore` is the default profile. Pass another profile only when you intentionally need it.

The development profile should match the App Store profile except for explicit debug and testing affordances. Milestones that claim iOS packaging, boot, runtime, or Linux behavior should validate the same XCTest suite and assertions across App Store and development profiles on both `iphoneos` and `iphonesimulator`.

Milestone 2 boot-entrypoint proof is intentionally narrower than booting Linux. It verifies that profile DTS sources are materialized into the generated Linux port tree.

```bash
make prepare-orlixkernel-port PROFILE=appstore
```

Milestone 2 does not prove QEMU execution, iOS execution, task switching, MMU behavior, userspace access, device binding, or root filesystem assembly.

Milestone 3 XCFramework packaging proof establishes the iOS execution artifact early. It must package a real Orlix Linux artifact into `OrlixKernel.xcframework`; packaging boot stubs alone is not proof.

Prepare the iOS packaging inputs with:

```bash
make prepare-ios-packaging PROFILE=appstore
```

This builds the selected profile's Linux `vmlinux`, builds the closed profile DTBs, stages them as private framework payload resources, and generates the disposable Xcode project from `project.yml`.

For simulator-first development, run the current simulator packaging proof with:

```bash
make proof-ios-simulator-packaging PROFILE=appstore
```

This verifies the simulator XCFramework payload, runs the packaging XCTest, and launches `OrlixTerminal` against the packaged framework.

Run only the packaging XCTest with:

```bash
make test-ios-simulator-packaging PROFILE=appstore
```

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

Milestone 4 iOS-hosted Linux test-execution proof establishes the runtime proof path. It must launch packaged Orlix Linux through an iOS host app or test host, run Linux-native KUnit and kselftest inside that Linux instance, and collect KUnit KTAP plus kselftest TAP from the Linux side.

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

The bootstrap target creates a disposable Debian arm64 sysroot under `Build/OrlixKernel/linux-userspace-sysroot/aarch64`. The staging target uses upstream kselftest install shape and refuses to build with the Darwin host SDK. The staged resource is for the forthcoming XCTest proof host bundle, not `OrlixKernel.xcframework` product payload, and is not required for Milestone 3 packaging tests.

Build Orlix KUnit artifacts with Linux Kbuild and the Orlix KUnit config as preparatory evidence:

```bash
cd Build/OrlixKernel/linux-6.12-port
make O=../kunit-orlix ARCH=orlix defconfig
scripts/kconfig/merge_config.sh -m -O ../kunit-orlix ../kunit-orlix/.config arch/orlix/.kunitconfig
make O=../kunit-orlix ARCH=orlix olddefconfig arch/orlix/boot/boot_test.o
```

Do not run kselftest or KUnit on Darwin and do not use a VM as product proof. Do not add repo-local shell or standalone C contract tests as milestone proof. Linux behavior belongs in KUnit/kselftest, and iOS product integration belongs in XCTest that launches Orlix and collects Linux test output from inside Linux.

Both `iphoneos` and `iphonesimulator` are iOS proof destinations. Milestones must validate the same scope on both.

Milestone 5 boot-to-virtio-probe proof keeps the dependency chain honest. It verifies that generated profile DTS files describe virtio-mmio probe-shape devices, that the selected generated profile defconfig enables upstream OF, virtio-mmio, and virtio-block probe paths, and that Orlix architecture boot handoff state is covered by Linux-native tests running through the Milestone 4 iOS-hosted proof path.

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

The current blocking proof boundary is real-artifact XCFramework packaging.

Success means `OrlixKernel.xcframework` packages a real Orlix Linux artifact for the selected profile. Boot-stub XCFramework packaging is not product proof.

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
