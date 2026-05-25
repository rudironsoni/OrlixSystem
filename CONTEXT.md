# Context Glossary

## Local Kernel Prototype

The quarantined implementation tree under `LegacyOrlix/OrlixKernel/`. It is not part of the target architecture. Its useful behavior must be migrated into upstream Linux-native extension points under `arch/orlix`, `OrlixKernel/Sources/ports/orlix/overlay`, and `drivers/orlix`; `OrlixKernel/fs`, `OrlixKernel/kernel`, and `OrlixKernel/runtime` must not reappear.

## No-New-Local-Kernel-Work Rule

No new Linux subsystem behavior should be added to `LegacyOrlix/` or restored under `OrlixKernel/fs`, `OrlixKernel/kernel`, or `OrlixKernel/runtime`. Migration may read from legacy directories for behavior reference, but target work belongs in upstream Linux, `arch/orlix`, Linux-native drivers, boot, or host-adapter seams.

## Test Migration Rule

Tests for the local kernel prototype are migration reference, not authoritative proof for the target architecture. New proof should be KUnit for kernel-internal Linux behavior, kselftest for Linux user-visible behavior, and XCTest for iOS-hosted launch, packaging, Linux test-output collection, and narrow `OrlixHostAdapter` host mechanics.

## Linux-Native Test Proof

KUnit and kselftest are the tests of record for Linux-owned behavior and provide dependency proof before product runtime proof. Building those artifacts is only preparatory evidence.

## Source-Layout Alignment

Evidence that repository source and test ownership paths match the documented architecture. It does not imply that Linux boot, KUnit, kselftest, OrlixMLibC, terminal, or package runtime proof has passed.

## Build Hook Alignment

Evidence that Make targets delegate to the correct project-owned build hooks. It does not imply that every component has a completed implementation or sysroot.

## App-Hosted Kernel Dependency/Build Proof

Evidence that the app-hosted OrlixKernel integration can be built or entered through the bootloader-shaped host path while remaining honest about missing dependencies such as real `start_kernel()`.

## Static Virtio Input Proof

Evidence that DTS, defconfig, or kselftest source inputs describe the intended virtio shape. It is preparatory input proof only, not proof that a running hosted kernel consumed device tree data or probed virtio devices.

## Temporary Start-Kernel Resolver

The bring-up-only host resolver for `start_kernel()`. It is acceptable only while the real Mach-O-linked upstream Linux provider is absent and the boot path returns an honest unavailable status.

## Kselftest Kernel-Interface Proof

Evidence from selected Linux-owned kselftests, executed from userspace against a running OrlixKernel, that kernel-exposed interfaces behave correctly.

## OrlixMLibC Kselftest Lane

The kselftest proof lane where selected Linux kselftests are built against OrlixMLibC and validate the Orlix syscall and userspace ABI path.

## Kselftest Promotion Rule

Selected kselftests must build against OrlixMLibC. Do not add a separate nolibc/raw-syscall lane for Orlix kselftest proof.

## Dependency Proof

Evidence that a required layer works without claiming Orlix can run real Linux userspace packages.

## Product Runtime Proof

Evidence that iOS-hosted Orlix Linux runs real Orlix Linux userspace packages built against OrlixMLibC.

## Package Runtime Proof

The first acceptable product runtime proof: iOS-hosted Orlix Linux provides a POSIX shell environment through terminal input and executes at least one third-party unpatched package built as an Orlix Linux userspace binary.

## Package Proof

Integration evidence that real packages such as Bash, jq, curl, or zsh build and run as Orlix Linux userspace binaries.

## Two-Tier Product Runtime Proof

The product runtime proof model that requires POSIX shell environment proof plus third-party unpatched package execution before claiming Orlix runs Linux userspace.

## Product Runtime Claim Promotion Order

The evidence order for product runtime claims: kernel dependency proof, OrlixMLibC-built kselftest kernel-interface proof, OrlixMLibC libc proof, OrlixMLibC-built kselftest syscall/UAPI proof, POSIX shell environment proof, then third-party package proof.

## Parallel Development, Ordered Claims

The rule that proof lanes may be implemented in parallel, but product claims must be promoted in dependency order so later integration success does not mask missing kernel, libc, syscall/UAPI, shell, or package-compatibility proof.

## Third-Party Package Proof Ladder

The ordered compatibility proof path for unpatched third-party packages: jq first, curl second, and zsh third.

## POSIX Shell Environment

A Linux userspace environment where a POSIX-compliant shell runs as an Orlix Linux userspace binary over Linux syscalls, terminal I/O, process control, filesystem behavior, and signals.

## Initial Shell Proof Package

Bash is the first shell package used to prove the POSIX shell environment; `/bin/sh` is a rootfs policy link or selection, not kernel-owned shell behavior.

## Linux Init Contract

The kernel boot contract where Orlix follows upstream Linux userspace startup expectations by executing normal init or shell userspace paths rather than providing a kernel-owned shell.

## No-Init Kernel Boot Proof

A valid dependency proof where OrlixKernel reaches the normal Linux init handoff and fails like upstream Linux when no working init or shell userspace exists.

## Kernel Proof Boundary

Kernel proof validates OrlixKernel and upstream Linux-owned kernel behavior without depending on OrlixMLibC or shell/package userspace.

## Userspace Proof Boundary

Userspace proof validates OrlixMLibC, the Orlix Linux userspace ABI, shell behavior, and package execution above the kernel proof boundary.

## XCTest Proof Harness

The iOS-side proof harness that packages or launches `OrlixKernel.xcframework`, starts Orlix through the bootloader-shaped API, collects Linux test output from inside Orlix Linux, and verifies host-mechanics behavior without replacing KUnit or kselftest assertions.

## Linux Test Output Collection

A private host-side collection path for Linux-native test output. KUnit output is collected from the Linux kernel log path, kselftest output is collected from test-initramfs stdout, and XCTest parses those streams without becoming a public test-management API.

## Separate Linux Test Streams

The proof rule that KUnit and kselftest raw outputs remain separate streams. XCTest parses each independently and reports a combined verdict only after both pass.

## Test Initramfs Sequence

The final test initramfs sequence first collects KUnit output from the kernel log path and KUnit debugfs when enabled, then runs `run_kselftest.sh -c orlix` and captures kselftest stdout separately.

## KUnit DebugFS Affordance

A test-kernel option set that enables Linux debugfs plus KUnit debugfs and exposes per-suite KUnit KTAP under `/sys/kernel/debug/kunit/<suite>/results`. It is useful to the test initramfs and XCTest proof path, but it is not public `OrlixKernel` API.

## OrlixTerminal

The iOS terminal app that hosts `OrlixKernel.xcframework`, launches Orlix through the bootloader-shaped API, presents the terminal surface, and serves as the XCTest host for iOS-hosted proof.

## Terminal UI Surface

The libghostty-spm-provided terminal presentation layer used by `OrlixTerminal`. It renders terminal I/O but does not own Linux execution or shell semantics.

## Orlix Terminal Backend

The Orlix-owned terminal byte path between Linux console/terminal plumbing and the terminal UI surface. It replaces sandbox shell execution backends such as `ShellCraftKit`.

## No-Fake-Terminal Rule

`OrlixTerminal` must not use fake shells, sandbox shells, or local execution backends to simulate Linux terminal behavior. Before Linux console bytes exist, it may display only real Orlix boot or proof output.

## iOS-Targeted Build

An Xcode build for both `iphoneos` and `iphonesimulator` that packages or launches an `ARCH=orlix` Linux artifact through the iOS host surface. Both destinations are iOS proof destinations and must validate the same milestone scope; `iphoneos` targets physical devices, `iphonesimulator` targets Simulator, and neither means compiling Linux as an Apple iOS ABI.

## Orlix Linux Userspace Binary

An executable built for Orlix's Linux userspace ABI, linked against OrlixMLibC, and hosted by the iOS app only as packaging or storage mechanics rather than as a Darwin/iOS ABI executable.

## Host-Contained Orlix Linux Build

A build performed by an iOS/macOS-hosted toolchain that produces Orlix Linux userspace binaries for bundling or installation into Orlix Linux storage.

## Self-Hosted Orlix Linux Build

A future build mode where running Orlix Linux compiles Orlix Linux userspace binaries from inside the iOS-hosted Linux instance.

## Orlix Linux Syscall ABI

The upstream Linux syscall ABI consumed by mlibc `sysdeps/linux`, exposed by OrlixKernel for `ARCH=orlix`: Linux syscall numbers, argument shapes, negative errno behavior, signal and task semantics, futexes, ioctls, file-descriptor behavior, and installed Linux UAPI layouts. It is not an Orlix-specific ABI.

## OrlixMLibC

The mlibc-based C library for Orlix Linux userspace that uses Linux sysdeps and the Orlix Linux syscall ABI while aiming for glibc and musl source compatibility on AArch64 Linux applications.

## OrlixMLibC Component

A durable top-level OrlixSystem component, separate from the Linux kernel port, that owns Orlix's mlibc-based userspace C library inputs.

## OrlixMLibC Upstream Model

The libc source model where upstream mlibc is generated read-only input under `Build/OrlixMLibC/upstream/mlibc` and durable Orlix sysdeps, configs, and patches live under the OrlixMLibC component.

## OrlixMLibC Sysdeps Rule

OrlixMLibC uses upstream mlibc `sysdeps/linux` as the libc syscall and ABI semantics. Orlix patches are allowed only when an iOS-hosted execution constraint makes the physical syscall transport impossible upstream, and those patches must preserve the Linux syscall ABI rather than create an Orlix application ABI.

## OrlixMLibC Kernel Header Source

OrlixMLibC consumes the standard upstream Linux `headers_install` UAPI output for `ARCH=orlix`, not duplicated or Orlix-recreated kernel ABI headers.

## Installed Orlix UAPI Headers

The disposable Linux `headers_install` artifact generated under `Build/OrlixMLibC/kernel-headers/<profile>/`, with `include/` as the userspace-consumable kernel header root for OrlixMLibC and toolchain builds.

## No Copied UAPI Rule

OrlixMLibC must not commit copied Linux syscall numbers, ioctl payloads, structs, constants, flags, or UAPI definitions that Linux owns.

## OrlixMLibC Libc Proof

Evidence from mlibc's own test suite, configured for the Orlix sysdeps layer, that OrlixMLibC implements libc, POSIX, GNU, Linux-extension, header, ABI, loader, malloc, pthread, locale, errno, and syscall-wrapper behavior.

## OrlixMLibC Proof Ladder

The promotion order for libc confidence: mlibc tests first, OrlixMLibC-built kselftests second, and package proof third.

## Kernel Shell

Avoid this term; shells are normal Linux userspace processes, not kernel-owned behavior.

## Glibc/Musl Source Compatibility

The OrlixMLibC goal that unpatched AArch64 Linux applications normally built against glibc or musl can instead build against the Orlix toolchain and sysroot while producing Orlix Linux userspace binaries.

## Native ELF Execution

The future Linux `execve()` path where upstream Linux maps AArch64 ELF binaries and an OrlixMLibC dynamic linker as native CPU code rather than emulating another instruction set.

## Foreign ELF Compatibility Deferral

The rule that compatibility with existing non-Orlix glibc or musl ELF binaries is deferred while native Orlix ELF execution remains required before package runtime proof.

## Native Orlix ELF Execution Proof

Evidence that iOS-hosted Orlix Linux executes an OrlixMLibC-built Orlix Linux userspace binary through the Linux `execve()` path.

## Unpatched Linux Application Build

A build of an existing Linux application that requires no application source patches and targets Orlix only through the supplied kernel headers, OrlixMLibC headers, libraries, and toolchain configuration.

## XcodeGen Project Manifest

The committed `project.yml` file that defines Orlix's iOS packaging and XCTest proof topology. It is durable source; the generated `.xcodeproj` is disposable output unless a future toolchain constraint requires otherwise.

## iOS Proof Parity

The requirement that the same XCTest suite and assertions pass on both `iphoneos` and `iphonesimulator`, with destination-specific wiring allowed only for mechanics such as signing, resource lookup, transport, or host-adapter details.

## Simulator-First Implementation

A development order that brings up the iOS harness on `iphonesimulator` first for speed. It does not reduce the milestone proof requirement; completion still requires the same proof on both `iphoneos` and `iphonesimulator`.

## App-Hosted OrlixKernel Runtime

The canonical OrlixKernel product/proof artifact: the OrlixKernel static library, framework, or object set linked with `OrlixHostAdapter` and executed by the iOS app host on Simulator or device. This hosted runtime path is the source of kernel proof.

## Canonical Kernel Proof Artifact

The iOS app-hosted OrlixKernel integration that actually runs inside the Orlix app environment. Orlix does not require `vmlinux` as a canonical build, proof, or runtime artifact.

## Optional vmlinux Tooling Artifact

A `vmlinux`-style artifact that may exist only as an optional developer/debug artifact with a named consumer. It is not a milestone, not product proof, not runtime proof, not libc proof, and not required for installed UAPI headers.

## App-Hosted Proof Question

The canonical proof question for OrlixKernel work: did the Orlix app-hosted runtime execute the Linux-shaped behavior on iOS?

## OrlixKernel XCFramework Slice Set

The initial `OrlixKernel.xcframework` platform slices are `ios-arm64` for physical iOS devices and `ios-arm64-simulator` for Apple Silicon Simulator. Intel Simulator support is not part of the initial slice set.

## Profile Linux Artifact Parity

For a selected Orlix profile, `iphoneos` and `iphonesimulator` slices wrap the same bit-identical `ARCH=orlix` Linux artifact. Destination differences belong in the iOS host wrapper or `OrlixHostAdapter`, not in the Linux kernel artifact.

## Selected Profile Framework Build

An `OrlixKernel.xcframework` build that packages or links exactly one selected profile's app-hosted OrlixKernel integration while also bundling the closed built-in profile DTBs where needed.

## OrlixKernel Wrapper

The iOS Mach-O framework or static-library surface inside each `OrlixKernel.xcframework` slice. It exposes the bootloader-shaped public API while hosting the OrlixKernel integration and private boot resources needed by the iOS runtime path.

## Linux Payload Artifact

Private Linux-shaped boot data or generated kernel inputs consumed by the app-hosted OrlixKernel runtime. Payload data is not canonical proof by itself; proof comes from executing the hosted runtime on iOS.

## Local-Kernel XCTest Reference

The quarantined coverage under `LegacyOrlix/Tests/MigrationReference/LocalKernelPrototype/` for the old local kernel prototype. It is migration reference only; Linux subsystem assertions should move to KUnit or kselftest, while retained XCTest should cover iOS-hosted Orlix launch, Linux test-output collection, packaging, or narrow host mechanics.

## iOS-Hosted Kernel-Interface Test Execution

The dependency-proof path where an iOS host app or XCTest target launches `OrlixKernel.xcframework`, boots Orlix Linux with test resources, and captures Linux-native test output without claiming product runtime proof.

## OrlixMLibC Kselftest Syscall/UAPI Lane

The kselftest lane installed under `Build/OrlixMLibC/kselftest/<profile>/` and staged with `proof_lane=orlixmlibc-kselftest-syscall-uapi`. It requires an OrlixMLibC sysroot plus installed Orlix UAPI headers and proves selected OrlixMLibC-to-OrlixKernel syscall/UAPI behavior.

## XCTest Proof Topology

The iOS proof harness under project-local `Tests/XCTest/` trees: `OrlixKernelHostProofTests`, `OrlixLinuxProofOutputParserTests`, and `OrlixHostAdapterTests`. XCTest launches or observes the hosted runtime, validates packaging and host mechanics, and parses Linux-native output. It does not own Linux subsystem assertions.

## Ownership-Based Migration

The process for moving from the local kernel prototype to the target architecture. Existing behavior is kept only when it still belongs under the upstream-Linux iOS port model; behavior that upstream Linux owns is deleted rather than reimplemented locally.

## Linux Surface Rule

When upstream Linux has a user-visible surface, implementation convention, build/test flow, or ownership model for a problem, Orlix follows that Linux shape instead of inventing an Orlix-specific equivalent unless a concrete iOS constraint forces a documented exception.

## Orlix Kernel Port Tree

The disposable upstream-Linux source tree after applying the Orlix port overlay and patch set. Its path is `Build/OrlixKernel/linux-<version>-port`. Durable changes must move back to the committed port overlay, patch set, configs, or bootloader-facing product surface.

## Real Linux Build Proof

Evidence that the app-hosted OrlixKernel integration for `ARCH=orlix` can be built for the selected profile and iOS destination. `vmlinux` is not a milestone artifact.

## XCFramework Packaging Milestone

The third milestone for the upstream-Linux iOS port. It packages or links the app-hosted OrlixKernel integration into the iOS host path before iOS-hosted kernel-interface proof and later runtime proof; boot-stub packaging is not proof.

## Release Profile

The default Orlix build profile. Normal build targets use this profile unless explicitly overridden, because every shipped Orlix product targets App Store distribution constraints.

## Development Profile

An Orlix build profile that should match the release profile except for explicit debug and testing affordances. It must not become a noisier or broader product shape.

## Profile Parity

The rule that release and development profile differences are limited to explicit debug and testing affordances. All product behavior, Linux-visible device shape, boot resource roles, and milestone proof scope should otherwise remain equal.

## Profile-Invariant Userspace ABI

The rule that product-relevant profiles may produce separate artifacts but must expose the same installed UAPI headers, syscall numbers, errno values, signal ABI, ioctl payloads, userspace-visible struct layouts, OrlixMLibC ABI, dynamic-loader contract, package ABI, and observable Linux userspace behavior.

## Profile ABI Drift

A release-blocking difference where profiles expose different Linux userspace ABI contracts; profile-specific paths, signing, diagnostics, tracing, assertions, host mediation, and test-only knobs are allowed only when they do not alter product ABI.

## Test Build Overlay

A test-only kernel configuration layer applied to both release and development proof builds to enable KUnit, kselftest support, KUnit debugfs, and related proof affordances without changing the normal product profile configs.

## Orlix KUnit Config

A committed `.kunitconfig` under the Orlix architecture overlay that selects Orlix KUnit suites and their KUnit-specific dependencies, matching upstream KUnit practice.

## KUnit Proof Merge

The proof-build step that merges the selected profile defconfig with `arch/orlix/.kunitconfig` for both release and development proof kernels. Normal product builds use only the selected profile defconfig.

## Profile Proof Parity

The requirement that milestones claiming iOS packaging, boot, runtime, or Linux behavior validate both release and development profiles against the same XCTest scope.

## iOS Proof Matrix

The required four-cell XCTest proof matrix for milestones claiming iOS packaging, boot, runtime, or Linux behavior: release on `iphoneos`, release on `iphonesimulator`, development on `iphoneos`, and development on `iphonesimulator`.

## Linux-Shaped Make Surface

The top-level Makefile's public command surface stays small and follows conventional names such as `build`, `prepare`, `headers_install`, `kunit`, `kselftest`, and `test`. Orlix-specific scope is selected with variables rather than target-name sprawl.

## Make Scope Variables

Variables such as `PROFILE=release`, `type=kunit,kselftest`, and `libc=orlixmlibc` select profile, test class, and kselftest libc lane without creating new public target names for each proof lane or artifact path.

## Proof Label Metadata

Proof labels name what an artifact or log stream proves, such as `orlixmlibc-kselftest-syscall-uapi`. They are metadata and log markers, not public Make targets.

## Boot Profile

A closed product-profile choice exposed through the bootloader entrypoint. Supported profiles are release and development; arbitrary string-named profiles are not part of the public API.

## Profile Defconfig

A durable Orlix product-profile configuration stored under `OrlixKernel/Sources/ports/orlix/configs/`. During port-tree generation, the selected profile is materialized into Kbuild's expected architecture config location for the generated tree.

## Bootloader Entrypoint

The public way the host app starts Orlix. It is minimal and represents booting Linux, not calling a runtime management API. The public API receives a small app-level boot config; the bootloader derives Linux-shaped boot inputs from profile device trees and command-line defaults.

## Boot Config

The minimal app-level input to the bootloader entrypoint. The first public shape contains only a boot profile, a root image identifier, and a terminal identifier.

## Resource Identifier

An opaque app-level name for a host-backed boot resource. The bootloader resolves resource identifiers through `OrlixHostAdapter`; raw iOS paths and host handles are not Linux-visible truth.

## Root Device

The Linux-visible default root storage devices for Orlix. `/dev/vda` is the immutable bundled base image and `/dev/vdb` is the writable app-private state image; the profile device tree labels these storage roles, and the mounted root is assembled above them with upstream Linux mechanisms.

## Root Filesystem

The main Linux filesystem for Orlix. It is assembled from virtio-blk-backed Linux filesystem images using upstream Linux mechanisms; external directory mechanisms such as virtio-fs or 9p are separate explicit mounts, not the root filesystem.

## Release Root Storage

The release root storage model uses an immutable bundled base image plus writable app-private state or overlay storage. Persistent Linux state belongs in app-private storage, while caches remain recreatable and external documents are explicit mounts.

## Package State

Linux package databases and permitted installed package state live under normal Linux paths in writable state. Pre-bundled packages live in the immutable base image, while repositories, post-install behavior, and downloaded content are constrained by profile policy.

## Release Package Channel

The release profile bundles curated OrlixOS distribution content as signed app resources and updates executable content through app releases first. Downloaded binary package repositories are deferred until a curated, signed, profile-approved channel with App Store-safe disclosure and policy checks is explicitly designed and reviewed. It is not an unrestricted arbitrary Linux repository model.

## Package Policy Ownership

Repository trust, package signatures, metadata, and post-install policy are userspace package-policy responsibilities guided by the selected profile. Kernel and architecture code enforce hard execution constraints; `OrlixHostAdapter` does not become a package manager.

## Executable Memory Policy

The release profile follows normal Linux execution controls for file-backed executable mappings, including filesystem permissions, mount flags, memory-management behavior, and upstream security mechanisms. Unavoidable iOS host constraints such as writable-plus-executable denial are adapted through the architecture/MM boundary.

## Executable Content Trust

Executable content follows normal Linux package-manager and filesystem trust. Tools such as apt/dpkg verify packages and install files into the filesystem; after installation, execution is governed by normal filesystem permissions, mount flags, Linux security policy, and architecture/MM constraints.

## Execution Policy Rule

Orlix does not introduce a custom execution policy layer unless a concrete App Store or iOS host constraint cannot be represented with normal Linux package, mount, permission, memory-management, or upstream security mechanisms.

## Host Memory Adaptation

Unavoidable iOS memory mechanics are reached through narrow `arch/orlix`-owned seams into `OrlixHostAdapter`. Virtio is used for virtual devices, not for Linux MM or executable-memory decisions.

## Architecture Host Seams

Non-device host mechanics such as clocks, timers, execution substrate, low-level memory mapping, lifecycle notification, and very-early entropy may use narrow `arch/orlix`-owned seams into `OrlixHostAdapter`. Device-like runtime services should use virtio where possible.

## Host Reality Rule

The physical host is iOS only, private, and sandboxed. Host limits are implementation constraints, not Linux-facing behavior. Processes, signals, process groups, sessions, mounts, namespaces, cgroups, seccomp, ptrace, and related facilities are virtualized inside Orlix when iOS cannot supply matching private mechanics, and device-like mediation uses virtio wherever upstream Linux has a fitting class.

## Lifecycle Ownership

App lifecycle handling is split between `arch/orlix` for unavoidable suspend/resume and timekeeping consequences, and standard Linux-visible power-management or event mechanisms for userspace observability when needed. A custom lifecycle character device is not the default target.

## Lifecycle Semantics

iOS backgrounding should map to Linux suspend/resume where feasible. App termination without an explicit saved image means the Linux instance ended and the next launch is a new boot with persistent filesystems intact; a future saved image should use Linux hibernation/resume semantics.

## Hibernation Scope

Hibernation/resume is deferred beyond the first architecture milestone. Early lifecycle plumbing should avoid blocking a future Linux-shaped hibernation path, but the first milestone only needs fresh boot plus suspend/resume hooks.

## Milestone Planning

The upstream-Linux iOS port should be planned as a sequence of focused milestones rather than one large migration. Each milestone must produce honest Linux-shaped proof before the next layer depends on it.

## App-Hosted Kernel Build Proof Milestone

The first milestone for the upstream-Linux iOS port. It is limited to source-tree generation, profile selection, app-hosted OrlixKernel build proof for the selected iOS destinations, and architecture documentation/instruction alignment. It does not implement virtio, boot API redesign, root filesystem assembly, console behavior, or product runtime.

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

Old detailed local-prototype test and KUnit/XCTest migration rules should be removed from `AGENTS.md` during the architecture rewrite. The retained rule is that local-kernel tests are migration reference, not proof for the target upstream-Linux architecture.

## README Role

`README.md` is the product overview and beginner-friendly working guide for the repository. It should explain what Orlix is and provide an ELI5 path for starting work without replacing the full architecture specification.

## README Flow

The README should explain concepts before commands: what Orlix is, what the repo owns, the directories contributors must know, the first commands to run, current milestone success criteria, and where to read deeper.

## Build Target Compatibility

Old build target names such as `prepare-linux-worktree`, `build-temporary-*`, `stage-temporary-*`, `proof-*`, and `test-all` are not preserved as compatibility aliases. Public Make targets should stay Linux-shaped; use variables for profile, test type, and libc lane selection.

## XCFramework Packaging Rule

`OrlixKernel.xcframework` or equivalent app-hosted packaging is required before iOS-hosted kernel-interface proof and later runtime proof can advance and must package or link the hosted OrlixKernel integration for the selected profile. Boot-stub packaging must not masquerade as product proof.

## Boot-Stub Packaging

Packaging `OrlixKernel.xcframework` from boot stubs alone is not a valid product target and should be removed in the first milestone. Narrow bootloader tests may remain only if they do not claim product packaging proof.

## Boot Entrypoint Proof

Proof that the public boot entrypoint remains bootloader-shaped and derives Linux-shaped boot input from closed profiles and resource identifiers. It must migrate to KUnit, kselftest, and XCTest rather than repo-local shell scripts or standalone C contract binaries.

## Boot Entrypoint Milestone

The second milestone for the upstream-Linux iOS port. It introduces the minimal bootloader entrypoint, closed boot profile selection, profile device trees, and Linux-shaped boot input generation while avoiding raw boot parameters as the product API.

## iOS-Hosted Kernel-Interface Test Execution Milestone

The fourth milestone for the upstream-Linux iOS port. It launches packaged OrlixKernel from an iOS host app or test host and collects dependency proof from the running kernel path, such as KUnit output, Linux-accurate no-init behavior, or selected OrlixMLibC-built kselftests. It does not prove POSIX shell behavior, package compatibility, or product runtime readiness.

## Boot To Virtio Probe Milestone

The fifth milestone for the upstream-Linux iOS port. It carries boot beyond prepared inputs so Linux can consume profile device tree data and reach the point where upstream virtio-mmio probing can be attempted. It depends on kernel dependency or kernel-interface proof and does not prove virtio-block device creation, block I/O, root assembly, userspace ABI, POSIX shell behavior, or product runtime readiness.

## Virtio Root Disk Milestone

The sixth milestone for the upstream-Linux iOS port. It introduces Orlix's virtio-mmio-shaped transport under `drivers/orlix`, binds upstream `virtio_blk`, and exposes `/dev/vda` and `/dev/vdb` as the immutable base and writable state disks through `OrlixHostAdapter` backing.

## Root Assembly Milestone

The seventh milestone for the upstream-Linux iOS port. It loads the bundled immutable initramfs, mounts virtio-blk-backed base and writable state disks, assembles the root with upstream OverlayFS, and preserves Linux-shaped writable state paths.

## Console Milestone

The eighth milestone for the upstream-Linux iOS port. It provides minimal early console diagnostics, serial-style console support, upstream virtio-console selection, and host terminal byte I/O needed for early interactive boot.

## Virtio Devices Milestone

The ninth milestone for the upstream-Linux iOS port. It adds remaining virtio-first devices such as virtio-rng, virtio-net, and external directory mounts through virtio-fs or 9p where feasible. The virtio-rng slice uses upstream `virtio-rng` and Linux hwrng; virtio-net and external directory mounts remain later slices. It may be split if the scope becomes too large.

## Root Overlay

The root filesystem may be assembled with upstream Linux OverlayFS when writable-root mode is selected and supported by the lower and upper filesystems. The product initramfs mounts the immutable base image and writable state image, then switches to the merged root. Direct immutable-root boot and initramfs-only proof boot remain separate, intentional Linux-shaped modes. The release profile currently selects `direct`; the development profile selects `overlay` to keep writable-root assembly continuously exercised.

## Writable State Layout

The persistent writable root state mirrors normal Linux paths, especially `/etc`, `/var/lib`, `/home`, and package database locations. OverlayFS technical directories are early-boot implementation details, not the conceptual storage model.

## Cache Storage

Recreatable Linux cache data is separated from persistent writable state. The release profile should expose cache-backed storage as a distinct Linux-visible mount, such as `/var/cache`, so host cache loss cannot corrupt persistent identity.

## Temporary Storage

The default `/tmp` storage for Orlix is upstream Linux `tmpfs`. Host temporary directories are not Linux-visible truth unless a specific backend need is later justified.

## Initramfs Policy

Orlix supports normal Linux initramfs/initrd behavior. Development currently uses a bundled product initramfs for OverlayFS root setup. Release currently uses direct `root=/dev/vda` immutable-root boot; initramfs-only proof boot remains a separate Linux-shaped mode.

## Release Initramfs

When a profile selects initramfs-mediated root setup, the initramfs artifact is bundled with the app, signed as app content, immutable at runtime, and loaded by the bootloader through normal Linux boot data.

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

## Release Console Profile

The default console policy for the release profile. Both virtio-console and serial-style console support are enabled; virtio-console is the normal interactive path, and the serial-style console remains available for early, debug, or fallback use.

## Early Console

A minimal `arch/orlix` diagnostic path used before normal Linux console drivers are ready. It must hand off to registered Linux console drivers and must not become the main terminal implementation.

## Console Parity

The staged path for making the Orlix console Linux-correct. The driver should first register a real Linux console/TTY device, then add bidirectional byte I/O, termios basics, blocking and nonblocking behavior, window-size propagation, and later PTY/session/job-control integration.

## Boot Template

A profile-selected set of Linux-shaped boot artifacts, especially device tree data and kernel command-line defaults. It is not a custom Orlix file format; the bootloader uses it to produce normal Linux boot inputs.

## Profile Device Tree

The static Linux-shaped device tree source for an Orlix profile. Durable profile device trees live under the Orlix architecture overlay, for example `OrlixKernel/Sources/ports/orlix/overlay/arch/orlix/boot/dts/release.dts`, and the bootloader supplies dynamic boot-time values.

## Bundled Profile DTB

The compiled device tree blob for a closed built-in Orlix profile, shipped inside `OrlixKernel.xcframework` as part of the kernel port machine description. App Store constraints require built-in profile DTBs to be bundled rather than arbitrary host-supplied files.

## Test Initramfs Resource

A Linux-shaped test payload bundled with the XCTest host app, not with the product `OrlixKernel.xcframework`. It contains the minimal userspace needed to run kselftest and emit TAP without becoming part of the product framework contract.

## kselftest Install Shape

The upstream kselftest build/install flow used to stage Orlix kselftest binaries for the test initramfs. Manual copying from the build tree is only a temporary fallback when upstream install flow is blocked.

## kselftest Proof Runner

The installed `run_kselftest.sh` script used inside the final test initramfs to execute Orlix kselftests. Direct binary execution is debugging only, not milestone proof.

## Orlix kselftest Collection

The installed kselftest collection selected with `run_kselftest.sh -c orlix` inside the final test initramfs, even when only `TARGETS=orlix` is installed.

## Orlix kselftest Target Scope

The initial kselftest proof scope is `TARGETS=orlix`. Other upstream kselftest targets are added intentionally as the relevant Linux subsystems become available.

## kselftest Timeout Policy

The upstream timeout model where selftests default to 45 seconds per test. Orlix adds a test-local `settings` file only when a concrete test needs a non-default timeout; milestone fatality is decided by the XCTest proof runner, not hidden inside the test binary.

## kselftest Timeout Override

An explicit `run_kselftest.sh --override-timeout` value supplied by the XCTest proof runner. It is not used initially and should be added only for a concrete iOS proof-runner need.

## Product Initramfs

The later product/root-assembly boot payload for normal Orlix startup. It is separate from the test initramfs and is not designed by the early iOS-hosted test-execution milestone.
