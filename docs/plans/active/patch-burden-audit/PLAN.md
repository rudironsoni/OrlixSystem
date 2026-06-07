# Orlix Patch Burden Audit Plan

## Task

Audit every durable patch under `OrlixOS/Sources/patches` and `OrlixMLibC/Sources/patches` with the default assumption that each patch is suspicious until proven necessary.

## Context

Orlix aims to deliver a Linux-compliant userspace surface. If `OrlixKernel`, Orlix Linux UAPI headers, OrlixOS package/rootfs inputs, and the OrlixOS package toolchain are correct, most upstream userspace projects should not need Orlix-specific source patches. The only expected long-term mlibc exception class is target integration that preserves Linux ABI semantics while adapting physical hosted execution mechanics.

## Approach

- Inventory all durable patches in both directories.
- Classify each patch by owner and risk:
  - `target-integration`: physical hosted execution or target ABI plumbing that cannot be expressed by upstream Linux userspace alone.
  - `libc-semantics`: mlibc behavior needed for Linux/glibc/musl source compatibility.
  - `package-portability`: upstream package or package build-host portability issue.
  - `test-impurity`: patch changes upstream test content or test selection.
  - `workaround-suspect`: likely masking an OrlixKernel, OrlixOS, rootfs, package-toolchain, or target-metadata bug.
- For each patch, define the proof required before keeping it.
- Identify patches that should be deleted, moved to Orlix-owned regression tests, upstreamed, or replaced by schema/target/toolchain configuration.

## Milestones

- [x] **M1: Patch inventory** | verify: list all files under both patch directories.
- [x] **M2: Initial classification** | verify: inspect patch bodies and application points.
- [ ] **M3: Root-cause proof backlog** | verify: each high-risk patch has a concrete proof command or owner.
- [ ] **M4: Cleanup implementation** | verify: remove or replace patches only after the relevant proof is collected.
- [ ] **Final: conformance proof restored** | verify: upstream conformance schemes use unmodified upstream tests and report exact failure/skip counts.

## Scope Boundaries

In scope:

- `OrlixOS/Sources/patches`
- `OrlixMLibC/Sources/patches`
- OrlixOS package toolchain/configuration that explains why those patches exist
- Orlix-owned docs/harness guidance for future patch burden

Out of scope for this audit pass:

- Editing generated trees under `Build/`
- Deleting or changing a runtime patch before root cause is proven
- Reinterpreting upstream test failures as success

## Directives

- `OrlixOS` is the Kit and owns package/rootfs/toolchain inputs.
- `OrlixMLibC` owns libc behavior and consumes Linux-shaped syscalls.
- `OrlixKernel` owns Linux syscall, VFS, fd, process, signal, and filesystem semantics.
- Upstream tests are authoritative; durable patch stacks used for upstream conformance must not alter upstream test content.

## Risks

- Removing a patch without proof can regress package build/runtime behavior.
- Keeping a patch without proof can hide a non-compliant Linux surface.
- Patches that modify upstream tests can make conformance results look stronger than they are.

## Notes

---

Created: 2026-06-07
