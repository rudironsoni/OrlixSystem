# ADR 0004: Use A Bootloader-Only Product API

## Status

Accepted

## Context

The product API could expose raw boot parameters, syscall wrappers, runtime management functions, or a minimal bootloader entrypoint.

Exposing Linux management APIs from the product surface would confuse kernel, libc, userspace, and host boundaries.

## Decision

The public product API is bootloader-shaped only. It exposes a minimal boot entrypoint with a closed profile selection and opaque app-level resource identifiers through the product header under `OrlixKernel/Sources/include`.

## Consequences

Raw `struct boot_params` is not the main public API.

Public syscall, file, mount, exec, task, cgroup, and runtime management APIs are forbidden.

The bootloader under `OrlixKernel/Sources/boot` translates app-level inputs into Linux-shaped boot data.
