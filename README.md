# OrlixKernel

OrlixKernel is the Linux-shaped runtime substrate for Orlix on iOS.
It is a virtual kernel/runtime inside one iOS app sandbox whose job is to present Linux-oriented source and runtime semantics while keeping iOS host mediation private.

## Status

This repository is under active virtualization and proof work.
It is not public drop-in compatibility proof for arbitrary Linux userspace yet.
Current proof in this repo is limited to:

- LinuxKernel syscall-facing semantic tests for selected subsystems
- HostBridge seam tests for private `OrlixHostAdapter/internal/ios/**` mediation
- Linux UAPI / ABI compile smoke tests for vendored headers
- canonical iOS Simulator build-for-testing and shared-scheme XCTest execution through XcodeGen + Xcodebuild

## Build Truth

XcodeGen and the generated Xcode project are the only build truth.

Canonical flow:

```bash
rtk xcodegen generate --project .
rtk xcodebuild -list -project OrlixKernel.xcodeproj
rtk xcodebuild build-for-testing -project OrlixKernel.xcodeproj -scheme OrlixKernel-6.12-arm64 -sdk iphonesimulator -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17'
rtk xcodebuild test-without-building -project OrlixKernel.xcodeproj -scheme OrlixKernel-6.12-arm64 -sdk iphonesimulator -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17'

Important:
- direct `xcodebuild` is the authoritative simulator proof
- if a sandboxed shell or output wrapper loses CoreSimulator visibility, rerun the same `xcodebuild` command outside the sandbox and trust the direct `xcodebuild` result
```

Canonical project surface:

- Targets:
  - `OrlixKernel`
  - `OrlixHostAdapter`
  - `OrlixKernelTests`
  - `OrlixHostAdapterTests`
- Scheme:
  - `OrlixKernel-6.12-arm64`

`swift build`, CMake, Make, package manifests, and ad hoc build flows are not authoritative for this repo.

## Repository Layout

Current top-level ownership is organized around the implemented subsystems in this tree:

- `OrlixKernel/kernel/` — process/task, signal, pid, wait, cred, time, sync, init, resource, random, sys, network ownership
- `OrlixKernel/fs/` — VFS, fdtable, open/read/write, stat, fcntl, ioctl, exec, path, mount, inode, readdir, eventpoll ownership
- `OrlixKernel/runtime/native/` — native command registry surface
- `OrlixHostAdapter/internal/ios/` — private host mediation seams only
- `OrlixKernelTests/` — Linux-facing syscall and contract proof
- `OrlixHostAdapterTests/` — private host-bridge seam proof
- `third_party/linux/6.12/arm64/uapi/include/` — vendored Linux UAPI truth
- `project.yml` — authoritative XcodeGen project specification

## Architecture Direction

OrlixKernel does not define its Linux-facing contract by delegating host semantics directly.
Its direction is a Linux-shaped virtual runtime with private iOS mediation.
That includes:

- virtual task/process identity
- virtual process groups and sessions
- virtual credentials
- Linux-shaped signal ownership and semantics
- Linux-shaped VFS, fdtable, and exec ownership
- virtualization of unsupported host behavior where possible inside Orlix boundaries

Darwin and iOS APIs are private implementation details and must not define the Linux-facing contract.

## Linux Header Policy

Vendored generated Linux headers from upstream Linux are the only Linux header truth in this repo.

Location:

- tuple root: `third_party/linux/6.12/arm64/`
- UAPI: `third_party/linux/6.12/arm64/uapi/include`

Allowed include forms:

- `#include <linux/...>`
- `#include <asm/...>`
- `#include <asm-generic/...>`

Forbidden include forms:

- path traversal into vendored headers
- includes containing `third_party/linux/`
- includes containing `6.12`
- includes containing `arm64`

## Test Layering

This repo currently contains two XCTest proof layers plus Linux UAPI compile smoke:

1. LinuxKernel proof
   - uses syscall-facing contracts and Linux-visible runtime assertions
   - keeps Linux semantics proof out of `OrlixHostAdapter/internal/ios/**`
   - is the primary subsystem proof for Linux-facing behavior

2. HostBridge proof
   - verifies private `OrlixHostAdapter/internal/ios/**` mediation seams only
   - may use host APIs to prove host-mechanics contracts
   - does not count as Linux semantics proof

3. Linux UAPI / ABI compile smoke
   - uses vendored Linux UAPI headers through canonical include paths
   - proves header/constants/types resolution only
   - does not prove runtime behavior

Current files:

- `OrlixKernelTests/SignalTests.m` — LinuxKernel semantic test
- `OrlixKernelTests/TaskGroupTests.m` — LinuxKernel semantic test
- `OrlixKernelTests/CredentialTests.m` — LinuxKernel semantic test
- `OrlixKernelTests/LinuxUAPICompileSmoke.c` — Linux UAPI / ABI compile smoke
- `OrlixHostAdapterTests/HostBridgeSmokeTests.m` — HostBridge seam test

True public drop-in Linux userspace compatibility proof is not part of this XCTest tranche.

## Current Constraints

OrlixKernel runs inside one iOS app sandbox.
Host limitations must be virtualized where possible rather than leaked into the Linux-facing contract.
Some subsystems remain incomplete or partial; incomplete behavior should be treated as implementation work in progress, not as proof of finished Linux compatibility.

## Primary References

- `AGENTS.md` — development rules for syscall ownership, `_impl()` usage, build proof, and error handling
- `docs/SUBSTRATE_CONTRACT.md` — current architecture and ownership contract
