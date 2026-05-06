# VFS And Runtime Environment Plan

## Purpose

Provide the Linux-shaped runtime environment needed by shells and package workflows.

## Goals

1. Make the Linux virtual filesystem hierarchy real enough for Version 1 package compatibility.
2. Keep host paths as private backing detail only.
3. Stabilize procfs and devfs surfaces used by real tools.

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

### Don't

- Do not leak raw host paths as Linux truth.
- Do not let host mechanics decide Linux path semantics.
- Do not claim VFS completion before procfs and shell-visible environment behavior are proven.

## Proof

1. Shell startup files are found and used in Linux-shaped locations.
2. `/proc/self/status` and `/proc/self/mountinfo` are sufficiently Linux-shaped for target packages.
3. `/tmp`, `/run`, `/var`, and `/etc` behavior supports shell and package workflows.
