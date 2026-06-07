# Orlix Agent Memory

This file is curated repo-local memory for coding agents. Keep entries short, stable, and linked to canonical docs or ADRs where possible. Do not use this file as a task status dashboard; active task state belongs in `docs/plans/active/<task>/`.

## Durable Rules

- Follow upstream Linux first. If upstream Linux has a behavior, API, test pattern, or ownership model, Orlix should match it unless a documented iOS constraint requires an exception.
- OrlixKernel is Linux. Do not implement shell, libc, package, syscall facade, or runtime-management behavior in the kernel component.
- OrlixHostAdapter owns private iOS mechanics only. It must not own Linux policy or public Linux ABI.
- OrlixMLibC is the only libc layer. Do not patch generated upstream mlibc trees; durable libc changes belong under `OrlixMLibC/Sources`.
- OrlixOS owns package and rootfs assembly only. It must not own kernel or libc semantics.
- Upstream tests are authoritative for upstream conformance. Do not edit, filter, adapt, or reinterpret upstream tests as success.
- Completion is evidence-based. Do not claim fixed, green, complete, runtime-ready, or package-ready without exact command output, logs, failure/skip accounting, and crash-report checks where applicable.
- Keep Makefile interfaces Linux-shaped and small. Prefer variables over new milestone or proof-target names.

## Recurring Failure Modes

- Patching generated upstream trees instead of fixing the owning Orlix layer.
- Treating partial package tests as full upstream conformance.
- Declaring success before checking app crashes or simulator crash reports.
- Creating custom diagnostics, tracing, dashboards, or status files where Codex hooks, logs, and plan evidence are enough.
- Splitting review into too many agents instead of using the single `orlix-reviewer` role.
