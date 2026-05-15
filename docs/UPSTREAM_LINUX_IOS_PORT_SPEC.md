# Orlix Upstream Linux iOS Port Specification

## Purpose

Orlix compiles upstream Linux into an iOS-hosted kernel product named `OrlixKernel.xcframework`.

Orlix does not imitate Linux by rewriting Linux core subsystems locally. It adapts Linux through Linux-native extension points such as boot, `arch/orlix`, drivers, Kconfig, device description, host backends, and build packaging.

## Product Identity

Correct names:

- Project: Orlix
- Repository: OrlixSystem
- Product: `OrlixKernel.xcframework`
- Architecture port: `arch/orlix`
- Orlix driver subtree: `drivers/orlix`
- Host mediation: `OrlixHostAdapter`
- Bootloader: `boot`

Do not invent names such as `orlixios`, `LinuxOrlix`, `OrlixRuntime`, `OrlixHostKit`, runtime facade, kernel API facade, or entry layer.

Inside already scoped directories, do not redundantly prefix filenames with `orlix_`.

## Core Architecture

The iOS app is the host container. It starts the Orlix bootloader. The bootloader prepares Linux-shaped boot inputs and transfers control into the Orlix Linux architecture port. After boot, upstream Linux owns Linux behavior.

High-level flow:

```text
iOS app
  -> bootloader
  -> Linux-shaped boot inputs
  -> arch/orlix
  -> start_kernel()
  -> upstream Linux subsystems
  -> init
  -> Linux userspace
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
- task context substrate
- signal frame support
- user access model
- MM and permission adaptation
- clock and timer glue
- early console
- lifecycle consequences

Orlix-specific drivers own transport and backend mechanics only where upstream Linux needs a device or platform implementation.

`OrlixHostAdapter` owns private iOS and Darwin mechanics only.

## Source And Build Model

Upstream Linux source is generated input:

```text
Linux/upstream/linux-6.12
```

The generated upstream tree is read-only. Durable Orlix changes do not go there.

Durable Orlix port inputs live under:

```text
Linux/ports/orlix/
  overlay/
  patches/
  configs/
```

The disposable upstream-plus-Orlix port tree is:

```text
Build/OrlixKernel/linux-<version>-port
```

If a change must survive regeneration, move it back to `Linux/ports/orlix`.

Normal builds default to:

```text
PROFILE=appstore
```

Profile defconfigs are durable product-profile configs under `Linux/ports/orlix/configs`. The selected profile is materialized into the generated port tree in the location Kbuild expects.

The first honest proof target is upstream Linux `vmlinux` for `ARCH=orlix`.

Required first proof commands:

```bash
make prepare-orlixkernel-port PROFILE=appstore
make build-linux-kernel PROFILE=appstore
make build-linux-kernel PROFILE=development
```

`OrlixKernel.xcframework` packaging must depend on a real Linux build artifact. Boot-stub packaging is not product proof.

## Boot Model

The public product API is bootloader-shaped. It must not expose Linux syscalls, file operations, mounts, exec, cgroups, tasks, or runtime management as public product APIs.

The public direction is:

```c
enum OrlixBootProfile {
    ORLIX_BOOT_PROFILE_APPSTORE,
    ORLIX_BOOT_PROFILE_DEVELOPMENT,
    ORLIX_BOOT_PROFILE_ENTERPRISE,
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
Linux/ports/orlix/overlay/arch/orlix/boot/dts/
```

Do not invent a custom Orlix `.boot` template format. Use device tree data, `/chosen`, kernel command line defaults, and normal Linux boot mechanisms.

Initramfs is supported through normal Linux behavior. The App Store profile defaults to an external initramfs bundled with the app, signed as app content, immutable at runtime, and loaded by the bootloader. Direct `root=/dev/vda` boot remains a Linux-shaped path when appropriate.

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

Do not create custom Orlix block, network, random, console, or filesystem drivers when upstream virtio classes satisfy the requirement.

## Storage And Root Filesystem

The App Store root storage model uses Linux-visible storage roles:

- `/dev/vda`: immutable bundled base image
- `/dev/vdb`: writable app-private state image
- cache-backed storage for recreatable cache mounts such as `/var/cache`
- `tmpfs` for `/tmp`
- explicit virtio-fs or 9p mounts for external documents and user-selected directories

The root filesystem is assembled using upstream Linux mechanisms. The App Store profile uses initramfs to mount the base and writable state images, assemble root with upstream OverlayFS, and switch to the merged root.

Writable state mirrors normal Linux paths, especially:

- `/etc`
- `/var/lib`
- `/home`
- package database locations

OverlayFS technical directories are early-boot implementation details, not the conceptual storage model.

Raw iOS paths are never Linux-visible truth.

## Console Model

Orlix supports both serial-style console behavior and upstream virtio-console.

The App Store profile enables both. Normal boot-time console selection follows Linux `console=` behavior.

The serial-style console is available for early, debug, or fallback use. The virtio-console path is the normal interactive direction where upstream Linux provides the closest virtual-console behavior.

`arch/orlix` may provide a minimal early console for earliest diagnostics. The early console must hand off to registered Linux console drivers and must not become the main terminal implementation.

## Package And Execution Model

Use normal Linux package and execution mechanisms first.

Package managers such as apt/dpkg verify packages and install files into the filesystem. After installation, execution is governed by normal Linux mechanisms:

- filesystem permissions
- mount flags such as `noexec`
- memory-management behavior
- upstream security mechanisms when configured

The App Store profile may allow downloaded binary packages only through curated, signed, profile-approved repositories with App Store-safe disclosure and policy checks. It is not an unrestricted arbitrary repository model.

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

`OrlixHostAdapter` must not become a public libc surface, syscall interposition layer, package manager, Linux policy layer, or Linux ABI provider.

Linux semantics stay in upstream Linux, `arch/orlix`, and Linux-native driver paths.

## Lifecycle Model

iOS lifecycle is mapped to Linux-shaped lifecycle behavior.

Backgrounding maps to Linux suspend/resume where feasible.

Termination without an explicit saved Linux image means the Linux instance ended. A later launch is a fresh boot with persistent filesystems intact.

Future saved-image support should use Linux hibernation/resume semantics. Hibernation is deferred beyond the first milestones, but early plumbing should not block it.

## Local Kernel Prototype Migration

`OrlixKernel/fs`, `OrlixKernel/kernel`, and `OrlixKernel/runtime` are the local kernel prototype. They are not target architecture paths.

No new Linux subsystem behavior belongs there.

Use them only as migration reference. Migrate useful behavior by ownership into upstream Linux-native paths, `arch/orlix`, `drivers/orlix`, boot code, or host-adapter seams. Delete remaining prototype directories when equivalent target paths exist or the behavior is intentionally dropped.

## Proof Model

Proof must remain honest about what it proves.

Kbuild `vmlinux` proof means upstream Linux is being built as Linux for `ARCH=orlix`.

XCFramework packaging proof means the product packages a real Linux build artifact for the selected profile.

Boot proof means the bootloader hands Linux-shaped boot inputs into `arch/orlix` and reaches the intended Linux boot stage.

Device proof means upstream Linux device classes bind and operate through Orlix transport and backend mechanics.

Old local-kernel tests are migration reference. They are not proof of the target architecture.

## Milestones

Milestone 1: Kbuild `vmlinux` Proof

Align `README.md`, `AGENTS.md`, the canonical spec, ADRs, source-tree generation, profile selection, and real Kbuild `vmlinux` proof for `ARCH=orlix`.

Milestone 2: Boot Entrypoint

Introduce the minimal bootloader entrypoint, closed profile selection, profile device trees, and Linux-shaped boot input generation.

Milestone 3: Virtio Root Disks

Introduce Orlix's virtio-mmio-shaped transport under `drivers/orlix`, bind upstream `virtio_blk`, and expose `/dev/vda` and `/dev/vdb` through host-backed storage.

Milestone 4: Root Assembly

Load the bundled immutable initramfs, mount virtio-blk-backed base and writable state disks, assemble root with upstream OverlayFS, and preserve Linux-shaped writable state paths.

Milestone 5: Console

Provide minimal early console diagnostics, serial-style console support, upstream virtio-console selection, and host terminal byte I/O needed for early interactive boot.

Milestone 6: Remaining Virtio Devices

Add virtio-rng, virtio-net, and external directory mounts through virtio-fs or 9p where feasible. Split this milestone if it becomes too large.

Final cleanup:

Delete remaining `OrlixKernel/fs`, `OrlixKernel/kernel`, and `OrlixKernel/runtime` after migration or intentional retirement.
