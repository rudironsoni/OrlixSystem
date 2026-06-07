# ADR 0004: Use A Bootloader-Only Product API

## Status

Accepted

## Context

The product API could expose raw boot parameters, syscall wrappers, runtime management functions, or a minimal bootloader entrypoint.

Exposing Linux management APIs from the product surface would confuse kernel, libc, userspace, and host boundaries.

## Decision

The app-facing product API lives in `OrlixOS` and is bootloader/session-shaped only. It exposes a minimal Linux session surface with closed profile selection and opaque app-level resource identifiers. The lower-level boot entrypoint remains under `OrlixKernel/Sources/include` for kernel integration, but apps should consume the `OrlixOS` Kit rather than target kernel headers directly.

## Consequences

Raw `struct boot_params` is not the main public API.

Public syscall, file, mount, exec, task, cgroup, and runtime management APIs are forbidden.

`OrlixOS` resolves target-owned payload metadata and registers private HostAdapter resource paths before boot. The bootloader under `OrlixKernel/Sources/boot` translates app-level inputs into Linux-shaped boot data.
