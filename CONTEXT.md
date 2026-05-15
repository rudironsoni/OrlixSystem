# Context Glossary

## Local Kernel Prototype

The current `OrlixKernel/fs`, `OrlixKernel/kernel`, and `OrlixKernel/runtime` implementation tree. It is not part of the target architecture. Its useful behavior must be migrated into upstream Linux-native extension points under `arch/orlix`, `Linux/ports/orlix/overlay`, and `drivers/orlix`; after migration, these directories should no longer exist.

## No-New-Local-Kernel-Work Rule

No new Linux subsystem behavior should be added to `OrlixKernel/fs`, `OrlixKernel/kernel`, or `OrlixKernel/runtime`. Migration may read from these directories for behavior reference, but target work belongs in upstream Linux, `arch/orlix`, Linux-native drivers, boot, or host-adapter seams.

## Test Migration Rule

Tests for the local kernel prototype are migration reference, not authoritative proof for the target architecture. New proof should focus on upstream Linux build behavior, Linux-native tests, boot/rootfs integration, Xcode packaging after real Linux artifacts exist, and `OrlixHostAdapter` host-mechanics tests.

## Ownership-Based Migration

The process for moving from the local kernel prototype to the target architecture. Existing behavior is kept only when it still belongs under the upstream-Linux iOS port model; behavior that upstream Linux owns is deleted rather than reimplemented locally.

## Orlix Kernel Port Tree

The disposable upstream-Linux source tree after applying the Orlix port overlay and patch set. Its path is `Build/OrlixKernel/linux-<version>-port`. Durable changes must move back to the committed port overlay, patch set, configs, or bootloader-facing product surface.

## Real Linux Build Proof

Evidence that upstream Linux is being built as Linux for `ARCH=orlix`, with `vmlinux` as the first honest proof artifact. Packaging `OrlixKernel.xcframework` is a later product step and must not substitute for this proof.

## App Store Profile

The default Orlix build profile. Normal build targets use this profile unless explicitly overridden, because it carries the strictest product constraints.

## Boot Profile

A closed product-profile choice exposed through the bootloader entrypoint. Supported profiles are App Store, development, and enterprise; arbitrary string-named profiles are not part of the public API.

## Profile Defconfig

A durable Orlix product-profile configuration stored under `Linux/ports/orlix/configs/`. During port-tree generation, the selected profile is materialized into Kbuild's expected architecture config location for the generated tree.

## Bootloader Entrypoint

The public way the host app starts Orlix. It is minimal and represents booting Linux, not calling a runtime management API. The public API receives a small app-level boot config; the bootloader derives Linux-shaped boot inputs from profile device trees and command-line defaults.

## Boot Config

The minimal app-level input to the bootloader entrypoint. The first public shape contains only a boot profile, a root image identifier, and a terminal identifier.

## Resource Identifier

An opaque app-level name for a host-backed boot resource. The bootloader resolves resource identifiers through `OrlixHostAdapter`; raw iOS paths and host handles are not Linux-visible truth.

## Root Device

The Linux-visible default root storage devices for Orlix. `/dev/vda` is the immutable bundled base image and `/dev/vdb` is the writable app-private state image; the mounted root is assembled above them with upstream Linux mechanisms.

## Root Filesystem

The main Linux filesystem for Orlix. It is assembled from virtio-blk-backed Linux filesystem images using upstream Linux mechanisms; external directory mechanisms such as virtio-fs or 9p are separate explicit mounts, not the root filesystem.

## App Store Root Storage

The App Store root storage model uses an immutable bundled base image plus writable app-private state or overlay storage. Persistent Linux state belongs in app-private storage, while caches remain recreatable and external documents are explicit mounts.

## Package State

Linux package databases and permitted installed package state live under normal Linux paths in writable state. Pre-bundled packages live in the immutable base image, while repositories, post-install behavior, and downloaded content are constrained by profile policy.

## App Store Package Channel

The App Store profile may allow downloaded binary packages only through curated, signed, profile-approved repositories with App Store-safe disclosure and policy checks. It is not an unrestricted arbitrary Linux repository model.

## Package Policy Ownership

Repository trust, package signatures, metadata, and post-install policy are userspace package-policy responsibilities guided by the selected profile. Kernel and architecture code enforce hard execution constraints; `OrlixHostAdapter` does not become a package manager.

## Executable Memory Policy

The App Store profile follows normal Linux execution controls for file-backed executable mappings, including filesystem permissions, mount flags, memory-management behavior, and upstream security mechanisms. Unavoidable iOS host constraints such as writable-plus-executable denial are adapted through the architecture/MM boundary.

## Executable Content Trust

Executable content follows normal Linux package-manager and filesystem trust. Tools such as apt/dpkg verify packages and install files into the filesystem; after installation, execution is governed by normal filesystem permissions, mount flags, Linux security policy, and architecture/MM constraints.

## Execution Policy Rule

Orlix does not introduce a custom execution policy layer unless a concrete App Store or iOS host constraint cannot be represented with normal Linux package, mount, permission, memory-management, or upstream security mechanisms.

## Host Memory Adaptation

Unavoidable iOS memory mechanics are reached through narrow `arch/orlix`-owned seams into `OrlixHostAdapter`. Virtio is used for virtual devices, not for Linux MM or executable-memory decisions.

## Architecture Host Seams

Non-device host mechanics such as clocks, timers, execution substrate, low-level memory mapping, lifecycle notification, and very-early entropy may use narrow `arch/orlix`-owned seams into `OrlixHostAdapter`. Device-like runtime services should use virtio where possible.

## Lifecycle Ownership

App lifecycle handling is split between `arch/orlix` for unavoidable suspend/resume and timekeeping consequences, and standard Linux-visible power-management or event mechanisms for userspace observability when needed. A custom lifecycle character device is not the default target.

## Lifecycle Semantics

iOS backgrounding should map to Linux suspend/resume where feasible. App termination without an explicit saved image means the Linux instance ended and the next launch is a new boot with persistent filesystems intact; a future saved image should use Linux hibernation/resume semantics.

## Hibernation Scope

Hibernation/resume is deferred beyond the first architecture milestone. Early lifecycle plumbing should avoid blocking a future Linux-shaped hibernation path, but the first milestone only needs fresh boot plus suspend/resume hooks.

## Milestone Planning

The upstream-Linux iOS port should be planned as a sequence of focused milestones rather than one large migration. Each milestone must produce honest Linux-shaped proof before the next layer depends on it.

## Kbuild vmlinux Proof Milestone

The first milestone for the upstream-Linux iOS port. It is limited to source-tree generation, profile selection, Kbuild `vmlinux` proof for `ARCH=orlix`, and architecture documentation/instruction alignment. Required proof includes `make prepare-orlixkernel-port PROFILE=appstore`, `make build-linux-kernel PROFILE=appstore`, and `make build-linux-kernel PROFILE=development`. It does not implement virtio, boot API redesign, root filesystem assembly, or console behavior.

## Architecture Documentation Alignment

Milestone 1 must align `README.md`, `AGENTS.md`, and the canonical upstream-Linux iOS port specification with the target architecture. Documentation must stop presenting skeleton packaging or the local kernel prototype as the product direction.

## Canonical Spec Rewrite

`docs/UPSTREAM_LINUX_IOS_PORT_SPEC.md` should be created fresh from the resolved upstream-Linux architecture decisions rather than patched from stale architecture text.

## Canonical Spec Scope

The canonical architecture spec should be medium-detail: enough to prevent wrong architecture work, but not an exhaustive implementation manual. It should focus on rules, ownership, proof, and milestones.

## ADR Scope

Architecture Decision Records should capture durable architecture decisions from the upstream-Linux port design, not minor wording or command confirmations. ADRs are appropriate for choices that are hard to reverse, surprising without context, and based on real trade-offs.

## ADR Timing

Architecture Decision Records for the upstream-Linux port should be created during Milestone 1 documentation work after the shared design decisions are stable, not one-by-one during discovery.

## AGENTS Role

`AGENTS.md` is the concise strict rule set for agents working in the new upstream-Linux architecture. Broader rationale, milestones, and narrative belong in the canonical architecture specification.

## Agent Test Rules Cleanup

Old detailed `OrlixKernelTests` and KUnit/XCTest migration rules should be removed from `AGENTS.md` during the architecture rewrite. The retained rule is that local-kernel tests are migration reference, not proof for the target upstream-Linux architecture.

## README Role

`README.md` is the product overview and beginner-friendly working guide for the repository. It should explain what Orlix is and provide an ELI5 path for starting work without replacing the full architecture specification.

## README Flow

The README should explain concepts before commands: what Orlix is, what the repo owns, the directories contributors must know, the first commands to run, current milestone success criteria, and where to read deeper.

## Build Target Compatibility

Old build target names such as `prepare-linux-worktree` are not preserved as compatibility aliases. Build targets should be renamed to match the upstream-Linux OrlixKernel architecture directly.

## XCFramework Packaging Rule

`OrlixKernel.xcframework` packaging must depend on a real Linux build artifact for the selected profile. Boot-stub packaging must not masquerade as product proof.

## Boot-Stub Packaging

Packaging `OrlixKernel.xcframework` from boot stubs alone is not a valid product target and should be removed in the first milestone. Narrow bootloader tests may remain only if they do not claim product packaging proof.

## Bootloader Contract Test

A narrow test for the public bootloader entrypoint direction. It should not preserve raw boot parameters as the product API or fake kernel image loading from memory fields; it should be rewritten around the minimal boot config and profile-based boot input generation.

## Boot Entrypoint Milestone

The second milestone for the upstream-Linux iOS port. It introduces the minimal bootloader entrypoint, closed boot profile selection, profile device trees, and Linux-shaped boot input generation while avoiding raw boot parameters as the product API.

## Virtio Root Disk Milestone

The third milestone for the upstream-Linux iOS port. It introduces Orlix's virtio-mmio-shaped transport under `drivers/orlix`, binds upstream `virtio_blk`, and exposes `/dev/vda` and `/dev/vdb` as the immutable base and writable state disks through `OrlixHostAdapter` backing.

## Root Assembly Milestone

The fourth milestone for the upstream-Linux iOS port. It loads the bundled immutable initramfs, mounts virtio-blk-backed base and writable state disks, assembles the root with upstream OverlayFS, and preserves Linux-shaped writable state paths.

## Console Milestone

The fifth milestone for the upstream-Linux iOS port. It provides minimal early console diagnostics, serial-style console support, upstream virtio-console selection, and host terminal byte I/O needed for early interactive boot.

## Virtio Devices Milestone

The sixth milestone for the upstream-Linux iOS port. It adds remaining virtio-first devices such as virtio-rng, virtio-net, and external directory mounts through virtio-fs or 9p where feasible. It may be split if the scope becomes too large.

## Root Overlay

The App Store root filesystem is assembled with upstream Linux OverlayFS when supported by the lower and upper filesystems. Initramfs mounts the immutable base image and writable state image, then switches to the merged root.

## Writable State Layout

The persistent writable root state mirrors normal Linux paths, especially `/etc`, `/var/lib`, `/home`, and package database locations. OverlayFS technical directories are early-boot implementation details, not the conceptual storage model.

## Cache Storage

Recreatable Linux cache data is separated from persistent writable state. The App Store profile should expose cache-backed storage as a distinct Linux-visible mount, such as `/var/cache`, so host cache loss cannot corrupt persistent identity.

## Temporary Storage

The default `/tmp` storage for Orlix is upstream Linux `tmpfs`. Host temporary directories are not Linux-visible truth unless a specific backend need is later justified.

## Initramfs Policy

Orlix supports normal Linux initramfs/initrd behavior. The App Store profile defaults to initramfs for early policy and root setup, but direct `root=/dev/vda` boot remains a supported Linux-shaped path.

## App Store Initramfs

The App Store profile uses an external initramfs artifact that is bundled with the app, signed as app content, immutable at runtime, and loaded by the bootloader through normal Linux boot data.

## Virtio-Block Semantics

The root block path must honor the Linux virtio-block contract rather than only using virtio-style device names. Host-backed storage is a mechanism behind the virtio/block implementation, not the Linux-visible interface.

## Virtio-Block Ownership

Upstream Linux `virtio` and `virtio_blk` own the Linux-visible root disk behavior. Orlix-specific code supplies the transport and host-backed backend needed to make that upstream driver work inside the iOS app boundary.

## Virtio-First Devices

Orlix should use upstream Linux virtio device classes wherever they fit, including block, console, entropy, and networking. Orlix-specific code supplies virtio transport and backend mechanics; upstream Linux drivers own Linux-visible behavior.

## Network Device Ownership

Linux-visible networking should use upstream Linux networking and upstream `virtio_net` where host-backed network access is needed. Loopback is upstream Linux loopback, and Orlix-specific code stays behind transport, backend, and policy seams.

## Custom Orlix Network Driver

A non-virtio Orlix network device driver. It is not part of the current target layout and should only be introduced if upstream Linux plus `virtio_net` cannot satisfy a concrete requirement.

## External Directory Mounts

Linux-visible mounts for user-selected or document-backed host directories. These should use upstream Linux filesystem mechanisms first, preferably virtio-fs and otherwise 9p over virtio; custom Orlix filesystems are not the first target.

## Orlix Virtio Transport

The Orlix-specific virtio transport lives under `drivers/orlix`, while its internal structure and contracts should stay as close as possible to Linux virtio transport conventions. Upstream virtio device drivers remain the Linux-visible owners of block, network, console, and entropy behavior when applicable.

## Virtio-MMIO Shape

The first Orlix virtio transport model is virtio-mmio shaped. Profile device trees should describe normal virtio-mmio-style devices where practical, while Orlix-specific code under `drivers/orlix` supplies the backend mechanics.

## Console Device

The Linux-visible boot console for Orlix. Orlix should support both a serial-style console and a virtio-console path, with normal Linux boot-time console selection deciding which console or consoles are active.

## Console Selection

The Linux-shaped process of selecting active boot consoles through kernel boot arguments and registered console drivers. Orlix should allow choosing between serial-style console behavior and virtio-console behavior at boot, matching regular Linux expectations.

## App Store Console Profile

The default console policy for the App Store profile. Both virtio-console and serial-style console support are enabled; virtio-console is the normal interactive path, and the serial-style console remains available for early, debug, or fallback use.

## Early Console

A minimal `arch/orlix` diagnostic path used before normal Linux console drivers are ready. It must hand off to registered Linux console drivers and must not become the main terminal implementation.

## Console Parity

The staged path for making the Orlix console Linux-correct. The driver should first register a real Linux console/TTY device, then add bidirectional byte I/O, termios basics, blocking and nonblocking behavior, window-size propagation, and later PTY/session/job-control integration.

## Boot Template

A profile-selected set of Linux-shaped boot artifacts, especially device tree data and kernel command-line defaults. It is not a custom Orlix file format; the bootloader uses it to produce normal Linux boot inputs.

## Profile Device Tree

The static Linux-shaped device tree source for an Orlix profile. Durable profile device trees live under the Orlix architecture overlay, for example `Linux/ports/orlix/overlay/arch/orlix/boot/dts/appstore.dts`, and the bootloader supplies dynamic boot-time values.
