# Orlix

Orlix is a Linux-shaped runtime and developer system for iOS and iPadOS.

Primary repository:
- `OrlixSystem`: https://github.com/rudironsoni/OrlixSystem

IXLand is the historical name. Orlix is current.

## What Orlix Is

Orlix exists to make iPhone and iPad capable of hosting real Linux-oriented software and workflows without redefining that software around Apple-native assumptions.

The goal is not to make Linux tools feel “sort of portable” on iOS.

The goal is to provide a Linux-shaped environment so Linux userspace can compile and run with minimal downstream patching.

Orlix is iOS-first only in host placement. Its contract is Linux-shaped.

## Why Orlix Exists

iOS and iPadOS are powerful platforms, but their native application model does not expose a Linux runtime model directly.

That leaves a large gap for developers who want:
- Linux command-line workflows
- Linux process and syscall semantics
- Linux-oriented source compatibility
- a serious development environment on mobile Apple hardware
- a path to run real Linux userspace instead of app-specific rewrites

Orlix exists to close that gap with a runtime that is hosted on iOS but shaped for Linux userspace.

## What Users Should Expect

From a user perspective, Orlix aims to provide:
- Linux-oriented source compatibility for real userspace code
- Linux ABI discipline instead of ad hoc Darwin-flavored compatibility
- Linux-shaped syscall and runtime behavior
- shell and terminal workflows that behave like Linux workflows
- virtual kernel subsystems such as VFS, signals, PTY, poll, epoll, futex, procfs, devfs, namespaces, and cgroups where the host does not provide them directly
- adaptation of App Store and iOS platform constraints without leaking those constraints into the user-facing Linux contract

The product target is not “POSIX on iOS”.

The product target is Linux-shaped developer capability on iOS and iPadOS.

## Why OrlixKernel Exists

That goal requires a Linux-shaped runtime owner.

`OrlixKernel` exists because the host operating system does not natively provide Linux semantics.

So Orlix needs a kernel/runtime layer that owns:
- Linux syscall behavior
- Linux task and process modeling
- Linux signals and wait semantics
- Linux file-descriptor and VFS behavior
- Linux PTY, readiness, and event semantics
- Linux-oriented virtual subsystems when iOS has no direct equivalent

Without `OrlixKernel`, Orlix would collapse into a pile of host-specific exceptions instead of a coherent Linux-shaped system.

## Product Direction

Orlix should feel like a serious Linux-oriented environment that happens to be hosted on iOS, not like a Darwin app with Linux branding on top.

That means:
- Linux rules come first
- host constraints are adapted behind the runtime
- user-facing behavior stays Linux-shaped
- compatibility work is judged by whether real Linux userspace becomes easier to build and run

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
