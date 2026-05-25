# Orlix Upstream Linux iOS Port Specification

## Purpose

Orlix compiles upstream Linux into an iOS-hosted kernel product named `OrlixKernel.xcframework` and pairs it with `OrlixMLibC`, an mlibc-based C library for Orlix Linux userspace.

Orlix does not imitate Linux by rewriting Linux core subsystems locally. It adapts Linux through Linux-native extension points such as boot, `arch/orlix`, drivers, Kconfig, device description, host backends, and build packaging.

When upstream Linux already has a surface, implementation convention, build/test flow, or ownership model for a problem, Orlix follows that Linux shape. Orlix-specific alternatives require a concrete iOS constraint and a documented exception.

The product goal is Linux userspace compatibility inside an iOS app boundary. OrlixKernel is Linux. OrlixMLibC fills the libc role for Orlix Linux userspace with glibc/musl source compatibility, not a new Orlix application ABI.

## Product Identity

Correct names:

- Project: Orlix
- Repository: OrlixSystem
- Product: `OrlixKernel.xcframework`
- Userspace libc: `OrlixMLibC`
- Host app: `OrlixTerminal`
- Architecture port: `arch/orlix`
- Orlix driver subtree: `drivers/orlix`
- Host mediation: `OrlixHostAdapter`
- Bootloader: `OrlixKernel/Sources/boot`

Do not invent names such as `orlixios`, `LinuxOrlix`, `OrlixRuntime`, `OrlixHostKit`, runtime facade, kernel API facade, or entry layer.

Inside already scoped directories, do not redundantly prefix filenames with `orlix_`.

## Core Architecture

The iOS app is the host container. It starts the Orlix bootloader. The bootloader prepares Linux-shaped boot inputs and transfers control into the Orlix Linux architecture port. After boot, upstream Linux owns Linux behavior.

The kernel does not provide a shell, package layer, libc surface, syscall facade, or runtime management API. Shells and packages are normal Orlix Linux userspace binaries linked against OrlixMLibC and executed through Linux mechanisms such as `execve()`.

Host reality is iOS-only, private, sandboxed mediation. The physical host is not a Linux kernel, but host limitations must not define the Linux-facing contract. Anything iOS cannot provide natively is virtualized inside Orlix through Linux-owned architecture or driver paths unless an unavoidable exception is documented.

High-level flow:

```text
iOS app
  -> bootloader
  -> Linux-shaped boot inputs
  -> arch/orlix
  -> start_kernel()
  -> upstream Linux subsystems
  -> init
  -> OrlixMLibC-backed Linux userspace
  -> shell and packages
```

Upstream Linux owns:

- VFS
- task and process model
- fd tables
- signals
- wait and reaping behavior
- procfs, sysfs, devtmpfs
- cgroups and namespaces
- sockets and networking core
- syscall semantics
- exec and interpreter behavior

Orlix-specific architecture code owns:

- boot handoff
- setup and machine description
- syscall entry mechanics
- task context substrate, including hosted process virtualization when required
- signal frame support and signal virtualization when required
- user access model
- MM and permission adaptation
- clock and timer glue
- early console
- lifecycle consequences

Orlix-specific drivers own transport and backend mechanics only where upstream Linux needs a device or platform implementation.

`OrlixHostAdapter` owns private iOS and Darwin mechanics only. It may use Apple SDK, Darwin, POSIX, and host libc APIs internally, but those dependencies must not leak through contracts consumed by `OrlixKernel`, `arch/orlix`, or `drivers/orlix`.

Kernel-visible HostAdapter contracts must remain freestanding and Linux-shaped. They must not require libc headers, Apple or Objective-C types, `FILE *`, `pthread_t`, `errno` contracts, POSIX fd contracts, `struct stat`, `DIR *`, malloc/free ownership conventions, dynamic-loader semantics, or host syscall ABI behavior.

`OrlixMLibC` owns the libc implementation for Orlix Linux userspace. It may have an Orlix sysdeps identity where mlibc requires one, but that sysdeps layer must call Linux-shaped syscalls and expose Linux-compatible behavior.

## Source And Build Model

Project-owned source and test roots are:

```text
OrlixKernel/Sources
OrlixKernel/Tests
OrlixHostAdapter/Sources
OrlixHostAdapter/Tests
OrlixMLibC/Sources
OrlixMLibC/Tests
OrlixTerminal/Sources
OrlixTerminal/Tests
```

Upstream Linux source is generated input:

```text
OrlixKernel/Sources/upstream/linux-6.12
```

The generated upstream tree is read-only. Durable Orlix changes do not go there.

Durable Orlix port inputs live under:

```text
OrlixKernel/Sources/ports/orlix/
  overlay/
  patches/
  configs/
```

The disposable upstream-plus-Orlix port tree is:

```text
Build/OrlixKernel/linux-<version>-port
```

If a change must survive regeneration, move it back to `OrlixKernel/Sources/ports/orlix`.

`OrlixMLibC` is a top-level component in this repository. It tracks upstream mlibc through generated/read-only upstream input under:

```text
Build/OrlixMLibC/upstream/mlibc
```

Durable Orlix sysdeps, configs, and patches live under `OrlixMLibC/Sources/`. Its tests live under `OrlixMLibC/Tests/`.

OrlixMLibC consumes kernel UAPI only through the standard Linux `headers_install` output for `ARCH=orlix`. That disposable artifact lives under:

```text
Build/OrlixMLibC/kernel-headers/<profile>/
```

The userspace-consumable include root is:

```text
Build/OrlixMLibC/kernel-headers/<profile>/include
```

Do not copy Linux syscall numbers, ioctl payloads, structs, constants, flags, or UAPI definitions into OrlixMLibC checked-in headers. Linux owns those definitions.

Normal builds default to:

```text
PROFILE=release
```

Profile defconfigs are durable product-profile configs under `OrlixKernel/Sources/ports/orlix/configs`. The selected profile is materialized into the generated port tree in the location Kbuild expects.

The repository Makefile is the command surface for repeatable local orchestration. It delegates to one Makefile per top-level project: `OrlixKernel/Makefile`, `OrlixHostAdapter/Makefile`, `OrlixMLibC/Makefile`, and `OrlixTerminal/Makefile`. The top-level public targets stay small and Linux-shaped: `all`, `build`, `setup-env`, `prepare`, `scripts`, `dtbs`, `headers_install`, `kunit`, `kselftest`, `kselftest-install`, `test`, `clean`, `mrproper`, `xcodeproj`, and `run`.

`make build` means orchestration of the current component build hooks. It first runs `make clean`, removing generated outputs including `OrlixKernel/Sources/upstream/linux-6.12`, then the OrlixKernel build reclones upstream Linux through the bootstrap path. OrlixMLibC builds materialize upstream mlibc under `Build/OrlixMLibC/upstream/mlibc` and apply durable OrlixMLibC inputs from `OrlixMLibC/Sources`. It must not be described as proof that every component is runtime-complete. Until OrlixTerminal is backed by a Linux console path, its build hook may be source-ownership or placeholder checks only.

When Linux has a conventional target name, use that name. Orlix-specific dimensions should be variables such as `PROFILE=release`, `type=kunit,kselftest`, and `libc=orlixmlibc` when the libc lane must be explicit, not new target names. Do not add milestone, proof-lane, or artifact-path names such as `build-temporary-*`, `stage-temporary-*`, `proof-kernel-*`, or `proof-ios-*` as normal user-facing targets.

Proof labels are artifact metadata and log markers, not public Make targets. Internal Make plumbing may use private implementation targets, but docs and normal workflows should point users at the Linux-shaped public targets.

Orlix does not require `vmlinux` as a canonical build, proof, or runtime artifact.

The canonical OrlixKernel proof artifact is the iOS app-hosted OrlixKernel integration that actually runs inside the Orlix app environment: OrlixKernel static library, framework, or object set plus `OrlixHostAdapter`, the iOS app host, and simulator/device execution.

A `vmlinux`-style artifact may exist only as an optional developer/debug artifact with a named consumer. It is not a milestone, not product proof, not runtime proof, not libc proof, and not required for installed UAPI headers.

`OrlixKernel.xcframework` packaging or app linking must happen early enough to support iOS-hosted kernel-interface proof and later product runtime proof, and must carry the app-hosted OrlixKernel integration that the iOS app will actually execute. Boot-stub packaging is not product proof.

The Linux compile lane emits per-profile, per-platform OrlixKernel static archives:

```text
Build/OrlixKernel/<profile>/<platform>/OrlixKernel.a
```

Xcode links the matching archive into `OrlixKernel.framework`, and the framework slices are packaged into `OrlixKernel.xcframework`. The normal framework link input uses the product-named archive path.

The release and development profiles validate the same product scope. Release is the default because every shipped Orlix product is intended for App Store distribution. Development may enable explicit debug and testing affordances, but it must not drift into a broader Linux-visible product shape. Milestones that claim iOS packaging, boot, runtime, or Linux behavior must validate the same XCTest suite and assertions across release and development profiles on both `iphoneos` and `iphonesimulator`.

Orlix userspace ABI is profile-invariant. Release, development, simulator, CI, and debug builds may produce separate artifacts, but installed UAPI headers, syscall numbers, errno values, signal ABI, ioctl payloads, userspace-visible struct layouts, OrlixMLibC ABI, dynamic-loader contract, package ABI, and observable Linux userspace behavior must remain the same. Profile-specific paths, signing, diagnostics, tracing, assertions, host mediation, and test knobs are allowed only when they do not alter product ABI. Profile ABI drift is release-blocking.

Test-only kernel config overlays may enable KUnit, kselftest support, KUnit debugfs, and proof collection affordances for both release and development proof builds. Those overlays do not change the normal product profile configs.

## Boot Model

The public product API is bootloader-shaped. It must not expose Linux syscalls, file operations, mounts, exec, cgroups, tasks, or runtime management as public product APIs.

The public direction is:

```c
enum OrlixBootProfile {
    ORLIX_BOOT_PROFILE_RELEASE,
    ORLIX_BOOT_PROFILE_DEVELOPMENT,
};

struct OrlixBootConfig {
    enum OrlixBootProfile profile;
    const char *root_image_identifier;
    const char *terminal_identifier;
};

int OrlixBoot(const struct OrlixBootConfig *config);
```

The bootloader receives app-level identifiers, resolves them through `OrlixHostAdapter`, selects profile boot data, fills dynamic boot-time values, and hands Linux-shaped boot inputs to `arch/orlix`.

Profile boot data is device-tree-shaped. Static profile device trees live under:

```text
OrlixKernel/Sources/ports/orlix/overlay/arch/orlix/boot/dts/
```

Do not invent a custom Orlix `.boot` template format. Use device tree data, `/chosen`, kernel command line defaults, and normal Linux boot mechanisms.

Initramfs is supported through normal Linux behavior. The release profile defaults to an external initramfs bundled with the app, signed as app content, immutable at runtime, and loaded by the bootloader. Direct `root=/dev/vda` boot remains a Linux-shaped path when appropriate.

## Virtio-First Device Model

Orlix uses upstream Linux virtio device classes wherever they fit.

Use upstream Linux drivers for Linux-visible behavior:

- `virtio_blk` for root disks
- `virtio_console` for the main virtual console path
- `virtio-rng` for entropy
- `virtio_net` for networking
- virtio-fs first, or 9p over virtio when needed, for external directory mounts

Orlix-specific transport and backend code lives under `drivers/orlix`. Its internal shape should remain as close as possible to Linux virtio conventions.

The first transport model is virtio-mmio-shaped. Profile device trees should describe normal virtio-mmio-style devices where practical.

Use virtio as much as possible for device-like host mediation. Do not create custom Orlix block, network, random, console, or filesystem drivers when upstream virtio classes satisfy the requirement.

## Storage And Root Filesystem

The release root storage model uses Linux-visible storage roles:

- `/dev/vda`: immutable bundled base image
- `/dev/vdb`: writable app-private state image
- cache-backed storage for recreatable cache mounts such as `/var/cache`
- `tmpfs` for `/tmp`
- explicit virtio-fs or 9p mounts for external documents and user-selected directories

The root filesystem is assembled using upstream Linux mechanisms. The release profile may use initramfs to mount the base and writable state images, assemble root with upstream OverlayFS, and switch to the merged root. Direct immutable-root boot and initramfs-only proof boot remain valid Linux-shaped modes when selected intentionally.

Profile device trees must make the selected root mode and virtio storage roles explicit. The current release profile selects `direct`; the development profile selects `overlay` through initramfs-mediated root assembly. Both profiles advertise `initramfs-only`, `direct`, and `overlay` as distinct modes, label `/dev/vda` as immutable base storage, and label `/dev/vdb` as writable state storage.

Writable state mirrors normal Linux paths, especially:

- `/etc`
- `/var/lib`
- `/home`
- package database locations

OverlayFS technical directories are early-boot implementation details, not the conceptual storage model.

Raw iOS paths are never Linux-visible truth.

## Console Model

Orlix supports both serial-style console behavior and upstream virtio-console.

The release profile enables both. Normal boot-time console selection follows Linux `console=` behavior.

The serial-style console is available for early, debug, or fallback use. The virtio-console path is the normal interactive direction where upstream Linux provides the closest virtual-console behavior.

`arch/orlix` may provide a minimal early console for earliest diagnostics. The early console must hand off to registered Linux console drivers and must not become the main terminal implementation.

## Package And Execution Model

Use normal Linux package and execution mechanisms first.

Orlix Linux userspace binaries are built for the upstream Linux AArch64 ABI consumed by mlibc `sysdeps/linux`, linked against OrlixMLibC, and hosted by the iOS app only as packaging or storage mechanics. They are not Darwin/iOS ABI executables and are not launched by iOS as app binaries.

The near-term build mode is host-contained: an iOS/macOS-hosted toolchain produces Orlix Linux userspace binaries for bundling or installation into Orlix Linux storage. Self-hosted builds inside running Orlix Linux are a future capability after enough userspace exists.

OrlixMLibC aims for glibc/musl source compatibility. Existing AArch64 Linux applications should build unpatched when pointed at the Orlix toolchain and sysroot, but the resulting binaries have OrlixMLibC/Orlix runtime identity.

Native ELF execution means Linux `execve()` maps AArch64 ELF binaries and an OrlixMLibC dynamic linker as native CPU code. It is not CPU emulation. Existing non-Orlix glibc or musl binary compatibility is a future compatibility track; native Orlix ELF execution is required before product runtime proof.

Because the physical host is Apple arm64, Orlix-built Linux user code reserves `x18`, the host platform register. This is a hosted execution code generation rule for Orlix-produced binaries, not an Orlix-specific syscall or libc ABI.

Package managers such as apt/dpkg verify packages and install files into the filesystem. After installation, execution is governed by normal Linux mechanisms:

- filesystem permissions
- mount flags such as `noexec`
- memory-management behavior
- upstream security mechanisms when configured

The release profile bundles curated OrlixOS distribution content as signed app resources and updates that executable content through app releases first. Downloaded binary package repositories are deferred until a curated, signed, profile-approved channel with App Store-safe disclosure and policy checks is explicitly designed and reviewed. It is not an unrestricted arbitrary repository model.

Bash is the first shell proof package. `/bin/sh` is rootfs policy, not kernel behavior. The third-party package proof ladder is jq, then curl, then zsh, but those packages are acceptance gates for an OrlixOS distribution model rather than the architecture itself.

Do not introduce a custom Orlix execution policy layer unless a concrete App Store or iOS host constraint cannot be represented with normal Linux package, mount, permission, MM, or upstream security mechanisms.

Unavoidable iOS memory mechanics are adapted through narrow `arch/orlix` seams into `OrlixHostAdapter`. Virtio is for virtual devices, not Linux MM policy.

## Host Adapter Boundary

`OrlixHostAdapter` owns private host mechanics such as:

- app-private path discovery
- security-scoped access mechanics
- host file backing
- host clocks
- host entropy
- host allocation and mapping mechanics
- host synchronization mechanics
- app lifecycle notifications
- terminal and transport backends

HostAdapter implementation files under `OrlixHostAdapter/Sources` may use host libc and platform APIs privately. Headers and contracts visible to Linux-owned code must expose only Orlix-defined opaque handles, primitive scalar values, and explicit status enums.

`OrlixHostAdapter` must not become a public libc surface, syscall interposition layer, package manager, Linux policy layer, or Linux ABI provider.

Linux semantics stay in upstream Linux, `arch/orlix`, and Linux-native driver paths.

Processes, signals, process groups, sessions, mounts, namespaces, cgroups, seccomp, ptrace, and related Linux facilities are Linux-facing behavior. If iOS cannot supply a matching native primitive privately, Orlix virtualizes the behavior inside Linux-owned Orlix kernel paths rather than leaking the host limitation to userspace.

## Lifecycle Model

iOS lifecycle is mapped to Linux-shaped lifecycle behavior.

Backgrounding maps to Linux suspend/resume where feasible.

Termination without an explicit saved Linux image means the Linux instance ended. A later launch is a fresh boot with persistent filesystems intact.

Future saved-image support should use Linux hibernation/resume semantics. Hibernation is deferred beyond the first milestones, but early plumbing should not block it.

## Local Kernel Prototype Migration

`LegacyOrlix/` contains the quarantined local kernel prototype and old migration-reference tests. It is not a target architecture path.

No new Linux subsystem behavior belongs there.

Use it only as migration reference. Migrate useful behavior by ownership into upstream Linux-native paths, `arch/orlix`, `drivers/orlix`, boot code, or host-adapter seams. Delete remaining prototype material when equivalent target paths exist or the behavior is intentionally dropped. `OrlixKernel/fs`, `OrlixKernel/kernel`, and `OrlixKernel/runtime` must not reappear.

## Proof Model

Proof must remain honest about what it proves.

App-hosted OrlixKernel build proof means the kernel integration that the iOS app will execute can be built for the selected profile and iOS destination. A simulator-hosted result proves only the simulator destination unless the same scope has also passed on `iphoneos`.

XCFramework or app packaging proof means the product packages or links the hosted OrlixKernel integration for the selected profile. This proof is required before iOS-hosted kernel-interface execution can advance, but it does not prove Linux booted or ran userspace.

Boot proof means the bootloader hands Linux-shaped boot inputs into `arch/orlix` and reaches the intended Linux boot stage.

Device proof means upstream Linux device classes bind and operate through Orlix transport and backend mechanics.

KUnit proves OrlixKernel internal behavior when it runs in the hosted Linux proof path and emits Linux-owned KUnit output. Building KUnit-selected objects is useful dependency evidence, but it is not iOS-hosted KUnit execution proof. Linux boot/no-init behavior proves that OrlixKernel reaches the normal Linux init handoff and fails Linux-accurately when no userspace exists.

kselftest is Linux-owned source-tree test code executed from userspace against a running kernel. Orlix kselftests are built against OrlixMLibC and installed through the upstream kselftest flow; do not add a separate nolibc/raw-syscall `/init` proof lane.

OrlixMLibC libc proof comes from mlibc's own test suite configured for the Orlix sysdeps layer. Orlix syscall/UAPI proof comes from selected Linux kselftests rebuilt and rerun against OrlixMLibC.

Product runtime proof begins only when OrlixMLibC-backed Linux userspace executes through OrlixKernel. Bash proves the first interactive POSIX shell environment. jq, curl, and zsh form the third-party package ladder.

ADR 0017 governs product runtime claim promotion. Development may proceed in parallel, but claims must move in order: kernel dependency proof, kselftest kernel-interface proof, OrlixMLibC libc proof, OrlixMLibC-built syscall/UAPI proof, POSIX shell environment proof, and third-party package proof. Later proof does not replace earlier proof.

Darwin-hosted execution, VM/QEMU execution, repo-local shell harnesses, standalone C contract binaries, fake shells, and local terminal backends are not product proof. XCTest may launch Orlix, verify iOS packaging and host mechanics, and collect Linux proof output on both `iphoneos` and `iphonesimulator`; both destinations must validate the same claimed scope.

The test kernel may enable Linux debugfs and `CONFIG_KUNIT_DEBUGFS` for per-suite KUnit KTAP retrieval inside the test initramfs. This is a test affordance, not public product API.

Old local-kernel tests are migration reference. They are not proof of the target architecture.

XCTest files quarantined under `LegacyOrlix/Tests/MigrationReference/LocalKernelPrototype/` are local-kernel migration reference. Retained XCTest should either launch packaged Orlix Linux and collect Linux test output or test narrow `OrlixHostAdapter` host mechanics. Linux subsystem assertions belong in KUnit or kselftest.

Durable KUnit tests live in the Linux port overlay next to Linux-owned code and are selected by `OrlixKernel/Sources/ports/orlix/overlay/arch/orlix/.kunitconfig`. The repository entry point is `make kunit` or `make test type=kunit`. Those targets may build KUnit-selected objects before hosted execution exists; do not promote that to hosted KUnit proof.

Durable kselftests live under `OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/`. Selected kselftests run through upstream kselftest install plus `run_kselftest.sh -c orlix`. The repository entry point is `make kselftest`, `make kselftest-install`, or `make test type=kselftest`.

OrlixMLibC-built kselftests install under `Build/OrlixMLibC/kselftest/<profile>/` and carry `proof_lane=orlixmlibc-kselftest-syscall-uapi` metadata.

XCTest targets live under project-local `Tests/XCTest/` trees such as `OrlixKernel/Tests/XCTest` and `OrlixHostAdapter/Tests/XCTest`. Future OrlixMLibC and OrlixTerminal XCTest targets belong under `OrlixMLibC/Tests/XCTest` and `OrlixTerminal/Tests/XCTest`. XCTest is limited to app-hosted launch, packaging, proof-output collection, parser behavior, and narrow host-adapter mechanics. XCTest must not replace KUnit, kselftest, OrlixMLibC tests, or package proof as the owner of Linux-visible assertions.

## Milestones

Milestone 1: App-Hosted OrlixKernel Build Proof

Align `README.md`, `AGENTS.md`, the canonical spec, ADRs, source-tree generation, profile selection, and app-hosted OrlixKernel build proof for the iOS destinations.

Milestone 2: Boot Entrypoint

Introduce the minimal bootloader entrypoint, closed profile selection, profile device trees, and Linux-shaped boot input generation.

Milestone 3: XCFramework Packaging

Package or link the app-hosted OrlixKernel integration into the iOS host path for the selected profile. Packaging boot stubs alone is not proof.

Milestone 4: iOS-Hosted Kernel-Interface Test Execution

Launch packaged OrlixKernel from an iOS host app or test host and collect dependency proof from the running kernel path. This milestone may include KUnit output, Linux-accurate no-init boot failure, and selected OrlixMLibC-built Linux kselftests where useful.

Milestone 4 does not prove OrlixMLibC correctness, final Orlix userspace ABI, POSIX shell behavior, third-party package compatibility, or product runtime readiness.

Milestone 5: Boot To Virtio Probe

Carry boot beyond prepared inputs so Linux consumes profile device tree data and reaches the point where upstream virtio-mmio probing can be attempted. Static DTS nodes, defconfig enablement, and kselftest source are input proof only. Do not claim virtio probe proof until the running hosted kernel consumes the DT and attempts the upstream virtio-mmio path. Do not claim block-device creation, block I/O, root assembly, or userspace boot.

Milestone 6: Virtio Root Disks

Introduce Orlix's virtio-mmio-shaped transport under `drivers/orlix`, bind upstream `virtio_blk`, and expose `/dev/vda` and `/dev/vdb` through host-backed storage.

Milestone 7: Root Assembly

Load the bundled immutable initramfs, mount virtio-blk-backed base and writable state disks, assemble root with upstream OverlayFS, and preserve Linux-shaped writable state paths.

Milestone 8: Console

Provide minimal early console diagnostics, serial-style console support, upstream virtio-console selection, and host terminal byte I/O needed for early interactive boot.

Milestone 9: Remaining Virtio Devices

Add virtio-rng, virtio-net, and external directory mounts through virtio-fs or 9p where feasible. Split this milestone if it becomes too large.

Milestone 10: OrlixMLibC Libc Proof

Build OrlixMLibC from `OrlixMLibC/Sources`, consume installed Orlix UAPI headers generated by Linux `headers_install`, and pass mlibc's own tests under `OrlixMLibC/Tests` for the Orlix sysdeps layer. This milestone does not prove package compatibility by itself.

Milestone 11: OrlixMLibC Syscall/UAPI Proof

Rebuild and rerun selected Linux kselftests against OrlixMLibC. This proves the Orlix syscall and userspace ABI path for the selected kselftest coverage.

Milestone 12: POSIX Shell Environment Proof

Run Bash as normal Orlix Linux userspace through the terminal path. This milestone proves enough process, fd, tty, signal, path, environment, dynamic-loader, and exec behavior for an interactive POSIX shell environment.

Milestone 13: Third-Party Package Ladder

Build and run unpatched third-party packages as Orlix Linux userspace binaries in order: jq, then curl, then zsh. This is the package-compatibility proof ladder above kernel, libc, filesystem, terminal, networking, and process-model layers.

Final cleanup:

Delete remaining `LegacyOrlix/` material after migration or intentional retirement. `OrlixKernel/fs`, `OrlixKernel/kernel`, and `OrlixKernel/runtime` should stay absent.
