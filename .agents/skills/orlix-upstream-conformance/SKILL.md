---
name: orlix-upstream-conformance
description: Use when building, running, triaging, or claiming success for upstream Linux, upstream mlibc, Coreutils, or other upstream project test suites in Orlix. Requires pristine upstream sources and upstream tests; failures must be routed to the owning Orlix layer instead of editing generated upstream trees or adapted tests.
---

# Orlix Upstream Conformance

Upstream conformance work proves that Orlix matches upstream-owned behavior. The upstream source and upstream tests are authoritative.

## Rules

- Use pristine upstream sources and tests.
- Do not edit, filter, adapt, or reinterpret upstream tests as success.
- Do not patch generated upstream trees such as `Build/OrlixMLibC/upstream/mlibc`, `Build/OrlixOS/src/*`, or `Build/OrlixKernel/linux-*-port`.
- Compare failures against upstream Linux, upstream mlibc, or the relevant package behavior before changing Orlix.
- Route fixes by ownership: kernel semantics to `OrlixKernel`, libc behavior to `OrlixMLibC/Sources`, package assembly to `OrlixOS`, and iOS mechanics to `OrlixHostAdapter`.

## Evidence

Record exact commands, upstream versions/commits, logs, failures, skips, crash checks, and final markers in `docs/plans/active/<task>/IMPLEMENT.md`.

For Coreutils full-suite claims, success requires the upstream suite marker with zero failures and zero skips for the expected test count. Partial or targeted runs are not full proof.
