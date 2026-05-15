# ADR 0012: Stage Deletion Of The Local Kernel Prototype

## Status

Accepted

## Context

The repository still contains local kernel prototype directories. Deleting them immediately could discard behavior notes before target equivalents exist. Keeping them active would preserve the wrong architecture.

## Decision

Deletion is staged. New work there is forbidden immediately. Useful behavior may be migrated by ownership. Remaining prototype directories are deleted after migration or intentional retirement.

## Consequences

`OrlixKernel/fs`, `OrlixKernel/kernel`, and `OrlixKernel/runtime` are migration reference only.

Documentation and agent rules must stop presenting those paths as Linux owners.

Final cleanup removes the directories completely.
