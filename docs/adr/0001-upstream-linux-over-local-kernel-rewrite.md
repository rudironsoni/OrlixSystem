# ADR 0001: Build Upstream Linux Instead Of Rewriting Linux Locally

## Status

Accepted

## Context

The repository historically contained an active local kernel prototype under `OrlixKernel/fs`, `OrlixKernel/kernel`, and `OrlixKernel/runtime`. That material is now quarantined under `LegacyOrlix/`, because that direction requires Orlix to reimplement Linux core subsystems locally.

The product goal is real Linux userspace compatibility inside an iOS app boundary.

## Decision

Orlix will compile upstream Linux and adapt it through Linux-native extension points. Upstream Linux owns core Linux subsystems. The local kernel prototype is migration reference only and is not a target implementation path.

When upstream Linux already provides the surface or implementation approach for a problem, Orlix follows that Linux approach rather than inventing a parallel Orlix-specific surface.

## Consequences

New Linux subsystem behavior must not be added to the local prototype tree.

Useful behavior from the prototype may be migrated by ownership into upstream Linux-native paths under `OrlixKernel/Sources/ports/orlix/overlay`, `OrlixKernel/Sources/boot`, Linux drivers, or `OrlixHostAdapter/Sources` seams.

The active product tree has no `OrlixKernel/fs`, `OrlixKernel/kernel`, or `OrlixKernel/runtime` directories. Legacy reference material is quarantined under `LegacyOrlix/` until migrated or intentionally retired.
