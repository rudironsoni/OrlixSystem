# Orlix Agent Memory

This file is curated repo-local memory for coding agents. Keep entries short, stable, and linked to canonical docs or ADRs where possible. Do not use this file as a task status dashboard; active task state belongs in `docs/plans/active/<task>/`.

## Durable Rules

- Follow upstream Linux first. If upstream Linux has a behavior, API, test pattern, or ownership model, Orlix should match it unless a documented iOS constraint requires an exception.
- OrlixKernel is Linux. Do not implement shell, libc, package, syscall facade, or runtime-management behavior in the kernel component.
- OrlixHostAdapter owns private iOS mechanics only. It must not own Linux policy or public Linux ABI.
- OrlixMLibC is the only libc layer. Do not patch generated upstream mlibc trees; durable libc changes belong under `OrlixMLibC/Sources`.
- OrlixOS is the Kit: it owns curated distribution policy, package/rootfs assembly, product payload packaging, target-derived payload metadata, and the app-facing Linux session API. It must not own kernel semantics, libc semantics, syscall ABI, private iOS host mechanics, or terminal UI rendering.
- Upstream tests are authoritative for upstream conformance. Do not edit, filter, adapt, or reinterpret upstream tests as success.
- Durable patches to upstream Linux, mlibc, or packages are suspicious until proven otherwise. Before adding or changing one, first prove the lower owning layers are compliant, compare upstream behavior, and record why the patch is an upstream/target integration fix rather than a workaround for an OrlixKernel, OrlixOS, or toolchain mismatch.
- Durable patch stacks used by conformance schemes must not modify upstream tests. Move extra Orlix regression tests to Orlix-owned test paths instead of changing upstream failure, skip, or total counts.
- Upstream package builds are package-conformance work. Do not disable upstream package features or force `configure`/libtool through ad hoc `LD` overrides to make cross builds pass; route toolchain discovery through reviewed OrlixOS package-toolchain inputs and prove the unmodified upstream suite.
- Completion is evidence-based. Do not claim fixed, green, complete, runtime-ready, or package-ready without exact command output, logs, failure/skip accounting, and crash-report checks where applicable.
- Keep Makefile interfaces Linux-shaped and small. Prefer variables over new milestone or proof-target names.

## Recurring Failure Modes

- Patching generated upstream trees instead of fixing the owning Orlix layer.
- Treating a durable upstream patch as the default fix before proving the Linux/kernel surface, OrlixOS rootfs input, package toolchain input, and upstream project behavior.
- Modifying upstream tests through a durable patch and then treating the patched suite as exact upstream conformance.
- Treating partial package tests as full upstream conformance.
- Declaring success before checking app crashes or simulator crash reports.
- Creating custom diagnostics, tracing, dashboards, or status files where Codex hooks, logs, and plan evidence are enough.
- Splitting review into too many agents instead of using the single `orlix-reviewer` role.
- Recreating `OrlixKit` or hardcoding product bundle identifiers/resource names in runtime code instead of reading OrlixOS target metadata and registering private HostAdapter resource paths.
- Disabling upstream package capabilities or inventing one-off linker/tool wrappers instead of fixing the OrlixOS package toolchain and proving the package with upstream tests.
