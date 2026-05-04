# IXLandSystem

IXLandSystem is the Linux-shaped runtime substrate for IXLand on iOS.
It is a virtual kernel/runtime inside one iOS app sandbox whose job is to present Linux-oriented source and runtime semantics while keeping iOS host mediation private.

## Status

This repository is under active virtualization and proof work.
It is not public drop-in compatibility proof for arbitrary Linux userspace yet.
Current proof in this repo is limited to:

- LinuxKernel syscall-facing semantic tests for selected subsystems
- HostBridge seam tests for private `internal/ios/**` mediation
- Linux UAPI / ABI compile smoke tests for vendored headers
- canonical iOS Simulator build-for-testing and shared-scheme XCTest execution through XcodeGen + Xcodebuild

## Build Truth

XcodeGen and the generated Xcode project are the only build truth.

Canonical flow:

```bash
rtk xcodegen generate --project .
rtk xcodebuild -list -project IXLandSystem.xcodeproj
rtk xcodebuild build-for-testing -project IXLandSystem.xcodeproj -scheme IXLandSystem-6.12-arm64 -sdk iphonesimulator -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17'
rtk xcodebuild test-without-building -project IXLandSystem.xcodeproj -scheme IXLandSystem-6.12-arm64 -sdk iphonesimulator -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17'
```

Canonical project surface:

- Targets:
  - `IXLandSystem`
  - `IXLandSystemLinuxKernelTests`
  - `IXLandSystemHostBridgeTests`
- Scheme:
  - `IXLandSystem-6.12-arm64`

`swift build`, CMake, Make, package manifests, and ad hoc build flows are not authoritative for this repo.

## Repository Layout

Current top-level ownership is organized around the implemented subsystems in this tree:

- `kernel/` — process/task, signal, pid, wait, cred, time, sync, init, resource, random, sys, network ownership
- `fs/` — VFS, fdtable, open/read/write, stat, fcntl, ioctl, exec, path, mount, inode, readdir, eventpoll ownership
- `runtime/native/` — native command registry surface
- `internal/ios/` — private host mediation seams only
- `IXLandSystemLinuxKernelTests/` — Linux-facing syscall and contract proof
- `IXLandSystemHostBridgeTests/` — private host-bridge seam proof
- `third_party/linux/uapi/6.12/arm64/include/` — vendored Linux UAPI truth
- `project.yml` — authoritative XcodeGen project specification

## Architecture Direction

IXLandSystem does not define its Linux-facing contract by delegating host semantics directly.
Its direction is a Linux-shaped virtual runtime with private iOS mediation.
That includes:

- virtual task/process identity
- virtual process groups and sessions
- virtual credentials
- Linux-shaped signal ownership and semantics
- Linux-shaped VFS, fdtable, and exec ownership
- virtualization of unsupported host behavior where possible inside IXLand boundaries

Darwin and iOS APIs are private implementation details and must not define the Linux-facing contract.

## Linux ABI and Header Policy

Vendored Linux 6.12 arm64 exported UAPI is the only Linux ABI truth in this repo.

Location:

- `third_party/linux/uapi/6.12/arm64/include`

Allowed include forms:

- `#include <linux/...>`
- `#include <asm/...>`
- `#include <asm-generic/...>`

Forbidden include forms:

- path traversal into vendored headers
- includes containing `third_party/linux/uapi`
- includes containing `6.12`
- includes containing `arm64`

## Test Layering

This repo currently contains two XCTest proof layers plus Linux UAPI compile smoke:

1. LinuxKernel proof
   - uses syscall-facing contracts and Linux-visible runtime assertions
   - keeps Linux semantics proof out of `internal/ios/**`
   - is the primary subsystem proof for Linux-facing behavior

2. HostBridge proof
   - verifies private `internal/ios/**` mediation seams only
   - may use host APIs to prove host-mechanics contracts
   - does not count as Linux semantics proof

3. Linux UAPI / ABI compile smoke
   - uses vendored Linux UAPI headers through canonical include paths
   - proves header/constants/types resolution only
   - does not prove runtime behavior

Current files:

- `IXLandSystemLinuxKernelTests/SignalTests.m` — LinuxKernel semantic test
- `IXLandSystemLinuxKernelTests/TaskGroupTests.m` — LinuxKernel semantic test
- `IXLandSystemLinuxKernelTests/CredentialTests.m` — LinuxKernel semantic test
- `IXLandSystemLinuxKernelTests/LinuxUAPICompileSmoke.c` — Linux UAPI / ABI compile smoke
- `IXLandSystemHostBridgeTests/HostBridgeSmokeTests.m` — HostBridge seam test

True public drop-in Linux userspace compatibility proof is not part of this XCTest tranche.

## Current Constraints

IXLandSystem runs inside one iOS app sandbox.
Host limitations must be virtualized where possible rather than leaked into the Linux-facing contract.
Some subsystems remain incomplete or partial; incomplete behavior should be treated as implementation work in progress, not as proof of finished Linux compatibility.

## Primary References

- `AGENTS.md` — development rules for syscall ownership, `_impl()` usage, build proof, and error handling
- `docs/SUBSTRATE_CONTRACT.md` — current architecture and ownership contract
