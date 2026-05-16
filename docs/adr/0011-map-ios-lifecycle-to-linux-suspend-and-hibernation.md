# ADR 0011: Map iOS Lifecycle To Linux Suspend And Hibernation

## Status

Accepted

## Context

iOS backgrounding, termination, and relaunch do not map directly to Linux server assumptions.

Inventing custom lifecycle semantics would make Linux-visible behavior hard to reason about.

## Decision

iOS backgrounding maps to Linux suspend/resume where feasible. App termination without an explicit saved image means the Linux instance ended. Future saved-image support should use Linux hibernation/resume semantics.

## Consequences

Relaunch without a saved image is a fresh boot with persistent filesystems intact.

Hibernation is deferred beyond the first milestones.

Early lifecycle plumbing should avoid blocking a future Linux-shaped hibernation path.
