# Exec, Shebang, And Native Execution Plan

## Status

This milestone builds on the `IXLandKernel` and `IXLandHostAdapter` split.

It assumes the old branded seam is gone, and it must preserve that by keeping exec-facing contracts kernel-owned and behaviorally Linux-shaped under simulator proof.

## Purpose

Build the Version 1 execution path around native iOS execution only, while preserving a backend-neutral architecture for future ELF and WASM support.

## Goals

1. Implement Linux-shaped `execve` semantics for native IXLand programs.
2. Make shebang and script execution mandatory and correct.
3. Keep the execution-image architecture open for future ELF and WASM backends.
4. Keep host execution mechanics private to `IXLandHostAdapter` and non-authoritative for Linux-visible exec semantics.
5. Use kernel-owned private contracts if exec needs cross-target declarations.
6. Treat successful structure as insufficient without Linux-visible runtime proof on the booted simulator.

## Required Model

Execution image kinds in architecture:

- native image, implemented now
- script image, implemented now
- ELF image, deferred
- WASM image, deferred

Common exec pipeline must handle:

- path resolution
- open and identify image
- shebang parsing
- argv and envp rewriting rules
- permission checks
- fd inheritance and `FD_CLOEXEC`
- signal reset-on-exec behavior
- thread-group collapse on exec
- backend handoff

## Deliverables

1. Native image execution backend only.
2. Shebang parser and interpreter chaining.
3. Linux-shaped `execve` and `execveat` semantics for native binaries and scripts.
4. Architecture-neutral execution image plumbing reserved for future ELF and WASM work.

## Guardrails

### Do

- Treat script execution as part of the core shell product.
- Preserve Linux-visible exec semantics even though the actual image is native iOS code.
- Reserve clear extension points for ELF and WASM.
- Keep host mediation behind kernel-owned private contracts rather than adapter-owned kernel-facing contracts.

### Don't

- Do not add ELF loading or emulation in this milestone.
- Do not bake native-only assumptions into process semantics that will block future backends.
- Do not fork a separate ad hoc script path outside the main exec pipeline.
- Do not treat branded `IXLandHostAdapter` vocabulary or adapter-owned headers as the completed Linux-facing exec contract.

## Proof

1. Native commands run through the common exec path.
2. Shebang scripts execute correctly.
3. Nested interpreter behavior is Linux-shaped within supported scope.
4. `FD_CLOEXEC`, argv, envp, and signal-reset behavior are covered by `IXLandKernelTests`.
5. Completion claims for this milestone do not rely on branded host-adapter vocabulary as the normal kernel-facing seam.
