# VFS And Runtime Environment Plan

## Status

This milestone depends on the split plan and must preserve its Linux-versus-host boundary rules.

Structural target separation is already present, but this milestone is not allowed to treat host-private backing choices as correct without Linux-visible proof.

## Purpose

Provide the Linux-shaped runtime environment needed by shells and package workflows.

## Goals

1. Make the Linux virtual filesystem hierarchy real enough for Version 1 package compatibility.
2. Keep host paths as private backing detail only.
3. Stabilize procfs and devfs surfaces used by real tools.
4. Prevent host discovery and backing semantics from becoming branded Linux-facing contract surface.
5. Ensure host-backed regular-file primitives used by VFS-facing features preserve Linux expectations under simulator proof.

## Required Runtime Environment

- `/`
- `/etc`
- `/usr`
- `/var`
- `/tmp`
- `/run`
- `/proc`
- `/dev`

## Deliverables

1. Linux-shaped virtual mount-backed filesystem environment.
2. Correct `openat` and `*at` path resolution behavior.
3. Synthetic procfs and devfs surfaces used by shells and proc-aware tools.
4. Stable mount visibility and procfs views needed by proof packages.

## Guardrails

### Do

- Keep Linux path behavior in `IXLandKernel`.
- Keep host root discovery and host backing paths inside `IXLandHostAdapter` only.
- Use explicit backend objects for runtime storage classes.
- Treat host-private VFS backing code as mechanism only, and force Linux-visible behavior through proof.
- Move any required cross-target VFS declarations under kernel-owned private contracts.

### Don't

- Do not leak raw host paths as Linux truth.
- Do not let host mechanics decide Linux path semantics.
- Do not claim VFS completion before procfs and shell-visible environment behavior are proven.
- Do not assume Darwin host primitives already behave like Linux regular files, anonymous files, or tmp semantics without proof.

## Proof

1. Shell startup files are found and used in Linux-shaped locations.
2. `/proc/self/status` and `/proc/self/mountinfo` are sufficiently Linux-shaped for target packages.
3. `/tmp`, `/run`, `/var`, and `/etc` behavior supports shell and package workflows.
4. Host backing details remain private and are not promoted into the accepted Linux-facing VFS contract.
