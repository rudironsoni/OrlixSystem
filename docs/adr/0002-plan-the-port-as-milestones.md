# ADR 0002: Plan The Upstream Linux Port As Milestones

## Status

Accepted

## Context

The upstream-Linux iOS port spans source generation, Kbuild, boot, architecture glue, virtio transport, storage, console, package policy, lifecycle, tests, and cleanup of the local prototype.

A single implementation plan would hide dependencies and encourage fake proof.

## Decision

The port will be planned and executed as focused milestones. Each milestone must produce honest proof before later layers depend on it.

## Consequences

Milestone 1 is Kbuild `vmlinux` proof, not full boot or device support.

Later milestones cover boot entrypoint, virtio root disks, root assembly, console, remaining virtio devices, and final prototype deletion.

Scope that does not belong to the current milestone should be deferred rather than partially implemented.
