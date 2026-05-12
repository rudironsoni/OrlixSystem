# OrlixKernel Substrate Contract

## Scope

OrlixKernel is the Linux-shaped runtime substrate for Orlix on iOS.
This repository owns virtual kernel/runtime behavior inside one iOS app sandbox.
It is not public drop-in proof for arbitrary Linux userspace yet.

## Platform

- host platform: iOS only
- minimum deployment target: iOS 16.0+
- supported SDKs: `iphonesimulator`, `iphoneos`
- authoritative validation: iOS Simulator or device build/test through XcodeGen + Xcodebuild
- authoritative milestone completion: AGENTS-required iOS Simulator `build-for-testing`, focused simulator proof for the active tranche, then the full shared-scheme simulator suite

## Architectural Contract

Priority order:

1. Linux-shaped exported contract
2. Correct subsystem ownership
3. Xcode project and XcodeGen as build truth
4. Internalized iOS mediation
5. Deterministic subsystem behavior
6. Fewer downstream compatibility patches
7. Local implementation convenience

Decision rule:

If a change makes OrlixKernel less suitable as a Linux-oriented syscall, header, or runtime target, it is the wrong change.

## Linux Header Truth

Vendored generated Linux headers from upstream Linux are the only Linux header truth in this repo.

Location:

- tuple root: `third_party/linux/6.12/arm64/`
- UAPI: `third_party/linux/6.12/arm64/uapi/include`

Allowed vendored include forms:

- `#include <linux/...>`
- `#include <asm/...>`
- `#include <asm-generic/...>`

Forbidden vendored include forms:

- includes containing `third_party/linux/`
- includes containing `6.12`
- includes containing `arm64`
- `../` traversal into vendored headers

Private internal headers are allowed only for private subsystem state, owner declarations, helper prototypes, and host-bridge declarations.
They must not define Linux ABI by hand.

## Build Truth

XcodeGen and the generated Xcode project are the only build truth.

Canonical project surface:

- Targets:
  - `OrlixKernel`
  - `OrlixHostAdapter`
  - `OrlixKernelTests`
  - `OrlixHostAdapterTests`
- Scheme:
  - `OrlixKernel-6.12-arm64`

Canonical authoritative flow:

```bash
rtk xcodegen generate --project .
rtk xcodebuild -list -project OrlixKernel.xcodeproj
rtk xcodebuild build-for-testing -project OrlixKernel.xcodeproj -scheme OrlixKernel-6.12-arm64 -sdk iphonesimulator -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17'
rtk xcodebuild test-without-building -project OrlixKernel.xcodeproj -scheme OrlixKernel-6.12-arm64 -sdk iphonesimulator -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17' -only-testing:OrlixKernelTests
rtk xcodebuild test-without-building -project OrlixKernel.xcodeproj -scheme OrlixKernel-6.12-arm64 -sdk iphonesimulator -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17' -only-testing:OrlixHostAdapterTests
```

Proof environment rule:
- the direct `xcodebuild` result is authoritative
- if a sandboxed shell, formatter, or wrapper loses CoreSimulator visibility and falls back to placeholder-only destinations, rerun the same simulator proof outside the sandbox and use the direct `xcodebuild` result as source of truth

The split target invocations above are the authoritative proof commands for this repo.
The unsplit umbrella `test-without-building` invocation may still run the same simulator tests, but it is not the required closeout command because Xcode can hang afterward on unrelated locked-device notification traffic outside the simulator target.

`swift build`, CMake, Make, package manifests, and other build systems are non-authoritative drift for this repo.

## Darwin Quarantine

Darwin and iOS headers are private host implementation details.

Rules:

- Darwin host headers may appear only in private host-bridge files
- Darwin host headers must not define the Linux-facing contract
- public Linux-facing ownership must live in canonical owner files, not in Darwin bridge files

## Canonical Ownership

Current subsystem ownership in this repository:

- process/task lifecycle: `OrlixKernel/kernel/task.c`, `OrlixKernel/kernel/fork.c`, `OrlixKernel/kernel/exit.c`, `OrlixKernel/kernel/wait.c`, `OrlixKernel/kernel/pid.c`
- credentials: `OrlixKernel/kernel/cred.c`, `OrlixKernel/kernel/cred.h`
- signals: `OrlixKernel/kernel/signal.c`, `OrlixKernel/kernel/signal.h`
- time and sync: `OrlixKernel/kernel/time.c`, `OrlixKernel/kernel/sync.c`
- init/sys/resource/random: `OrlixKernel/kernel/init.c`, `OrlixKernel/kernel/sys.c`, `OrlixKernel/kernel/resource.c`, `OrlixKernel/kernel/random.c`
- networking owner surface: `OrlixKernel/kernel/net/network.c`
- VFS and fdtable: `OrlixKernel/fs/vfs.c`, `OrlixKernel/fs/vfs.h`, `OrlixKernel/fs/fdtable.c`, `OrlixKernel/fs/fdtable.h`
- file operation owners: `OrlixKernel/fs/open.c`, `OrlixKernel/fs/read_write.c`, `OrlixKernel/fs/stat.c`, `OrlixKernel/fs/fcntl.c`, `OrlixKernel/fs/ioctl.c`, `OrlixKernel/fs/namei.c`, `OrlixKernel/fs/readdir.c`, `OrlixKernel/fs/eventpoll.c`, `OrlixKernel/fs/mount.c`, `OrlixKernel/fs/inode.c`, `OrlixKernel/fs/super.c`, `OrlixKernel/fs/path.c`, `OrlixKernel/fs/exec.c`
- native runtime registry: `OrlixKernel/runtime/native/registry.c`, `OrlixKernel/runtime/native/registry.h`
- private Darwin bridge surface: `OrlixHostAdapter/kernel/signal.c`
- package-facing libc header surface

## Test Layering

This repo currently contains two valid proof layers in XCTest plus Linux UAPI compile smoke:

1. LinuxKernel proof
   - exercises syscall-facing Linux-visible behavior
   - uses C contract files for Linux UAPI constants, structs, and payload truth
   - does not include `OrlixHostAdapter/**`

2. HostBridge proof
   - verifies private `OrlixHostAdapter/**` seams only
   - may use host APIs when proving host mechanics
   - does not prove Linux semantics

3. Linux UAPI / ABI compile smoke
   - may use only `<linux/...>`, `<asm/...>`, `<asm-generic/...>` includes
   - proves vendored UAPI resolution only
   - does not prove runtime behavior

Current test files:

- `OrlixKernelTests/SignalTests.m` — LinuxKernel semantic test
- `OrlixKernelTests/TaskGroupTests.m` — LinuxKernel semantic test
- `OrlixKernelTests/CredentialTests.m` — LinuxKernel semantic test
- `OrlixKernelTests/LinuxUAPICompileSmoke.c` — Linux UAPI / ABI compile smoke
- `OrlixHostAdapterTests/HostBridgeSmokeTests.m` — HostBridge seam test

True public drop-in Linux userspace compatibility proof is outside this XCTest tranche.

## Current Truth Boundaries

What this repo can currently prove authoritatively:

- canonical project generation through `xcodegen`
- canonical iOS Simulator `build-for-testing` plus focused and full shared-scheme execution through `xcodebuild`
- Linux UAPI header resolution through canonical include paths
- selected Linux-facing semantics via LinuxKernel tests and private seam behavior via HostBridge tests

What this repo does not currently prove:

- arbitrary unmodified Linux userspace compiling and linking against a stable public surface
- finished Linux compatibility across all subsystems
- a production-ready, fully complete runtime

## Documentation Rule

Documents in normal repo paths must describe current repo truth.
Historical migration plans and stale status documents should not remain as misleading authoritative documentation.
