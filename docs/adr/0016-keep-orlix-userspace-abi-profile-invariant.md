# ADR 0016: Keep Orlix Userspace ABI Profile-Invariant

## Status

Accepted

## Context

Orlix needs an mlibc-based userspace C library while preserving the project invariant that Orlix follows Linux-shaped surfaces instead of inventing Orlix-specific ABI layers. Build profiles, simulator/device destinations, and test lanes may require separate artifacts, but package compatibility depends on one stable Linux userspace contract.

## Decision

Create `OrlixMLibC` as a top-level component with sources under `OrlixMLibC/Sources` and tests under `OrlixMLibC/Tests`. It tracks upstream mlibc through generated/read-only upstream input plus durable Orlix sysdeps, configs, and patches. OrlixMLibC may have an Orlix sysdeps identity only where mlibc needs an OS-port hook, but it must use Linux-shaped syscalls and expose glibc/musl source-compatible Linux behavior rather than an Orlix-specific application ABI.

OrlixMLibC consumes kernel UAPI only through the standard Linux `headers_install` output for `ARCH=orlix`, generated under `Build/OrlixMLibC/kernel-headers/<profile>/` and consumed from that artifact's `include/` tree. OrlixMLibC must not commit copied Linux syscall numbers, ioctl payloads, structs, constants, flags, or other Linux-owned UAPI definitions.

Orlix userspace ABI is profile-invariant. App Store, development, simulator, CI, and debug builds may produce separate artifacts, but installed UAPI headers, syscall numbers, errno values, signal ABI, ioctl payloads, userspace-visible struct layouts, OrlixMLibC ABI, dynamic-loader contract, package ABI, and observable Linux userspace behavior must remain the same. Profile-specific paths, signing, diagnostics, tracing, assertions, host mediation, and test knobs are allowed only when they do not alter product ABI.

## Consequences

The OrlixMLibC proof ladder is mlibc's own tests under `OrlixMLibC/Tests` first, selected Linux kselftests rebuilt against OrlixMLibC second, and real package proof third. Temporary kselftest harnesses may provide early kernel-interface coverage, but only OrlixMLibC-built kselftests count as Orlix syscall/userspace ABI proof.

Profile paths are allowed. Profile ABI drift is not.
