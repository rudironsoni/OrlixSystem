# ADR 0001: Build Upstream Linux Instead Of Rewriting Linux Locally

## Status

Accepted

## Context

The repository contains a local kernel prototype under `OrlixKernel/fs`, `OrlixKernel/kernel`, and `OrlixKernel/runtime`. That direction requires Orlix to reimplement Linux core subsystems locally.

The product goal is real Linux userspace compatibility inside an iOS app boundary.

## Decision

Orlix will compile upstream Linux and adapt it through Linux-native extension points. Upstream Linux owns core Linux subsystems. The local kernel prototype is migration reference only and is not a target implementation path.

## Consequences

New Linux subsystem behavior must not be added to the local prototype tree.

Useful behavior from the prototype may be migrated by ownership into upstream Linux-native paths, `arch/orlix`, Linux drivers, boot code, or host-adapter seams.

The end state has no `OrlixKernel/fs`, `OrlixKernel/kernel`, or `OrlixKernel/runtime` directories.
