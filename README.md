# Project Orlix

Act as the architecture and implementation assistant for Orlix, a Linux-shaped runtime and developer system for iOS and iPadOS.

Primary repo:
- OrlixSystem: https://github.com/rudironsoni/OrlixSystem

IXLand is historical. Orlix is current.

## Core Objective

Orlix must provide Linux-oriented source compatibility, ABI discipline, syscall behavior, and runtime behavior so real Linux userspace can compile and run with minimal downstream patching.

Orlix is iOS-first only in host placement. Its public contract is Linux-shaped, not Darwin-shaped, libc-shaped, or branding-shaped.

## Non-Negotiable Rules

### 1. Upstream Linux header parity is mandatory.

`OrlixKernel` MUST preserve 100% parity with vendored upstream Linux headers as contract truth.

Do not create local replacements, facades, aliases, typedefs, private clones, compatibility headers, or wrappers for Linux UAPI, kheaders, constants, structs, flags, ioctl payloads, errno values, syscall contracts, or ABI surfaces.

If upstream Linux defines the concept, use the upstream Linux name and shape.

Do not invent `orlix_*`, `ix_*`, `kernel_*`, `linux_*`, `compat_*`, `bridge_*`, `shim_*`, `adapter_*`, `*_compat`, `*_bridge`, `*_shim`, or similar names for Linux concepts.

### 2. OrlixKernel is the Linux kernel/runtime owner.

Linux-facing behavior belongs in `OrlixKernel` only:
- syscalls
- process model
- fork-like behavior
- exec
- wait
- exit
- signals
- credentials
- fdtable
- VFS
- path resolution
- mounts
- file operations
- pipes
- PTY/TTY
- poll/select/epoll
- futex
- procfs
- devfs
- cgroups
- namespaces
- networking
- time
- resources
- seccomp
- ptrace
- native runtime behavior

`OrlixKernel` must model the Linux rule first, then delegate only private host mechanics to a narrow `OrlixHostAdapter` seam.

### 3. iOS-specific implementation belongs in OrlixHostAdapter only.

`OrlixHostAdapter` is private host mediation for `OrlixKernel`.

It may implement narrow kernel-declared seams for:
- sandbox storage
- host file backing
- path mediation
- clocks
- randomness
- threading primitives
- security-scoped access
- other iOS/Darwin mechanics

`OrlixHostAdapter` MUST NOT become public ABI, libc, syscall interposition, package surface, Linux userspace surface, or a place where Linux semantics are decided.

### 4. Leakage into OrlixKernel is totally forbidden.

`OrlixKernel` MUST NOT depend on, include, mirror, or adapt through:
- `OrlixMLibC`
- libc
- musl
- Darwin
- Foundation
- POSIX host headers
- Apple SDK types
- `OrlixHostAdapter` headers

If Linux-owner code appears to need any of those, the boundary is wrong.

Fix ownership, use upstream Linux header truth, or add a narrow kernel-owned private seam implemented by `OrlixHostAdapter`.

Do not fix Darwin leakage by replacing it with libc or `OrlixMLibC` leakage.

### 5. Apple App Store constraints must be adapted, not leaked.

Unsupported host capabilities must be virtualized, synthesized, or adapted behind Linux-shaped `OrlixKernel` semantics.

Examples:
- fork/process behavior must be modeled by `OrlixKernel` and adapted onto allowed iOS app mechanisms such as managed threads or synthetic tasks
- cgroups must be virtual
- procfs and devfs must be synthetic where needed
- filesystem behavior must be virtual, synthetic, sandbox-backed, or host-mediated behind Linux-shaped VFS semantics
- unsupported kernel features must degrade through explicit Linux-shaped behavior, not Darwin-shaped shortcuts

### 6. Libc and OrlixMLibC ownership is outside the kernel.

`OrlixMLibC` integration belongs to the userspace libc/sysdeps layer.

It owns:
- libc ABI headers
- startup
- syscall stubs
- errno exposure
- package-facing sysroot headers
- userspace ABI details

`OrlixKernel` owns the virtual kernel behavior that libc calls into.

### 7. Build and proof truth.

`project.yml` and the generated Xcode project are build truth.

Direct `xcodebuild` simulator/device results are proof truth.

No change is complete without targeted tests proving Linux-facing behavior.

`OrlixHostAdapterTests` prove private host mediation only, not Linux semantics.

---

# Technical Details

## Product Surface

This repository is the implementation of the Orlix runtime stack on iOS:
- `OrlixKernel` is the Linux-shaped virtual kernel/runtime
- `OrlixHostAdapter` is the private iOS/Darwin mediation layer
- `OrlixKernelTests` prove Linux-facing kernel behavior
- `OrlixHostAdapterTests` prove host-only seam behavior

The product target is not “Darwin with Linux names”.

The product target is Linux-shaped source and runtime behavior hosted inside an iOS app sandbox.

## Repository Layout

- `OrlixKernel/kernel/` — process, task, signal, pid, wait, cred, time, seccomp, ptrace, random, resource, cgroup, UTS, networking ownership
- `OrlixKernel/fs/` — VFS, path, inode, fdtable, open/read/write, stat, fcntl, ioctl, exec, mount, readdir, pipe, PTY, poll, epoll, xattr ownership
- `OrlixKernel/runtime/` — syscall dispatch, native runtime entrypoints, aarch64 runtime machinery
- `OrlixKernel/private/` — kernel-private subsystem state only
- `OrlixHostAdapter/` — private host mediation only
- `OrlixKernelTests/` — Linux-facing proof
- `OrlixHostAdapterTests/` — host seam proof
- `tools/clang_tidy_orlix/` — source policy enforcement
- `project.yml` — authoritative XcodeGen specification
- `Makefile` — top-level lint/build helper entrypoints

## Build and Proof Flow

Canonical proof flow:

```bash
make lint
xcodegen generate --project .
xcodebuild build-for-testing -project OrlixKernel.xcodeproj -scheme OrlixKernel-6.12-arm64 -sdk iphonesimulator -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17'
xcodebuild test-without-building -project OrlixKernel.xcodeproj -scheme OrlixKernel-6.12-arm64 -sdk iphonesimulator -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17'
```

Authoritative scheme and targets:
- Scheme: `OrlixKernel-6.12-arm64`
- Targets:
  - `OrlixKernel`
  - `OrlixHostAdapter`
  - `OrlixKernelTests`
  - `OrlixHostAdapterTests`

`xcodebuild` is the authoritative simulator proof.

Ad hoc build systems are not proof truth for this repo.

## Vendored Linux Header Model

The current vendored Linux tuple lives under:

```text
OrlixKernel/vendor/linux/6.12/arm64/
└── kheaders/
    └── include/
        ├── linux/
        ├── asm/
        ├── asm-generic/
        └── uapi/
```

The correct architectural reading is:
- `uapi/` is the Linux userspace ABI contract surface
- the broader `kheaders/include` graph is the build-matched Linux kernel header corpus

These are not equal sources of truth for all purposes.

The actual boundary is:
- Linux-facing ABI contracts are defined by Linux UAPI
- kernel-internal implementation reference comes from the broader vendored Linux kheaders graph
- package-facing libc ownership stays outside `OrlixKernel`

## Vocabulary Rules

Use this wording:
- Linux UAPI
- Linux kheaders
- vendored upstream Linux headers
- private kernel state

Do not use raw kbuild implementation vocabulary such as `srctree` and `objtree` as product architecture concepts.

Those terms may exist inside vendoring/build provenance, but they should not define the user-facing or architecture-facing Orlix vocabulary.

## Header Ownership Contract

Hard rules:
- Linux UAPI may define Orlix Linux-facing ABI
- vendored Linux kheaders may inform `OrlixKernel` private implementation
- vendored Linux kheaders must never become package-facing ABI
- `OrlixKernel` must not recreate Linux concepts under repo-local names
- `OrlixKernel` must not ingest libc, `OrlixMLibC`, Darwin, Foundation, or POSIX host headers
- `OrlixHostAdapter` must not own Linux semantics

If a Linux concept already exists upstream, use the upstream Linux name and shape.

## Include Policy

Allowed Linux include forms:
- `#include <linux/...>`
- `#include <asm/...>`
- `#include <asm-generic/...>`
- `#include <uapi/...>` where the owning surface is explicitly userspace ABI oriented

Forbidden include forms:
- provenance-heavy include paths such as `OrlixKernel/vendor/linux/...`
- repo-local Linux-concept wrappers or renamed shim headers
- libc, POSIX, Darwin, Foundation, or Apple SDK headers in `OrlixKernel`

## Kernel vs Host Split

The direction is:

```text
Linux userspace
        ↓
OrlixMLibC
        ↓
OrlixKernel
        ↓
OrlixHostAdapter
        ↓
iOS / Darwin host mechanics
```

The wrong direction is any path where:
- `OrlixKernel` depends on `OrlixMLibC`
- `OrlixKernel` consumes libc or Darwin semantics directly
- `OrlixHostAdapter` decides Linux behavior
- host limitations leak into the Linux-facing contract without explicit Linux-shaped adaptation

## Test Layering

`OrlixKernelTests` prove Linux-facing kernel behavior.

That includes:
- syscall-facing behavior
- Linux runtime semantics
- vendored Linux UAPI and kheaders compile smoke where appropriate
- subsystem-owned kernel behavior tests

`OrlixHostAdapterTests` prove private host mediation only.

They do not prove Linux semantics.

## Current Direction for Linux Kernel Tests

`OrlixKernelTests` are moving toward:
- Linux-shaped subsystem ownership
- C-first kernel tests
- upstream-KUnit-style structure and assertions
- Objective-C harness code only where Xcode/XCTest discovery needs a bridge

The end-state is not wrapper-heavy Objective-C contract ownership.

The end-state is Linux-shaped C-owned kernel proof with a thin XCTest shell.

## Practical Rule for Contributors

When in doubt:
- model the Linux rule in `OrlixKernel`
- keep iOS mechanics in `OrlixHostAdapter`
- use vendored upstream Linux headers as truth
- keep libc ownership in `OrlixMLibC`
- prove behavior with simulator-backed `xcodebuild`

If a change makes Orlix less suitable for real Linux userspace, the change is wrong.

