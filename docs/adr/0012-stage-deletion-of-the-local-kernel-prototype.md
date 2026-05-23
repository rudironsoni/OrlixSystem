# ADR 0012: Stage Deletion Of The Local Kernel Prototype

## Status

Accepted

## Context

The repository still contains local kernel prototype directories. Deleting them immediately could discard behavior notes before target equivalents exist. Keeping them active would preserve the wrong architecture.

## Decision

Deletion is staged. New work there is forbidden immediately. Useful behavior may be migrated by ownership. Remaining prototype directories are deleted after migration or intentional retirement.

## Consequences

Legacy prototype material is migration reference only and is quarantined under `LegacyOrlix/`. `OrlixKernel/fs`, `OrlixKernel/kernel`, and `OrlixKernel/runtime` should stay absent from the active product tree; active OrlixKernel work belongs under `OrlixKernel/Sources`.

XCTest coverage that targets those local-kernel directories is also migration reference only. Linux subsystem assertions should move to KUnit or kselftest under `OrlixKernel/Sources/ports/orlix/overlay`, while retained XCTest should cover iOS-hosted Orlix launch, Linux test-output collection, packaging, or narrow host mechanics under the owning project's `Tests` tree.

Documentation and agent rules must stop presenting those paths as Linux owners.

Final cleanup removes the directories completely.
