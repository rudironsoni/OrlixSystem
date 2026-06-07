---
name: orlix-upstream-conformance
description: Use when building, running, triaging, or claiming success for upstream Linux, upstream mlibc, Coreutils, or other upstream project test suites in Orlix. Requires pristine upstream sources and upstream tests; failures must be routed to the owning Orlix layer instead of editing generated upstream trees or adapted tests.
---

# Orlix Upstream Conformance

Upstream conformance work proves that Orlix matches upstream-owned behavior. The upstream source and upstream tests are authoritative.

## Rules

- Use pristine upstream sources and tests.
- Do not edit, filter, adapt, or reinterpret upstream tests as success.
- Do not modify upstream tests through durable patch stacks used by conformance schemes. Extra regressions belong in Orlix-owned tests, not in patched upstream test trees.
- Do not patch generated upstream trees such as `Build/OrlixMLibC`, `Build/OrlixOS`, or `Build/OrlixKernel/src/linux-*-port`. Read them freely for diagnosis; route durable changes to owning Orlix inputs.
- Compare failures against upstream Linux, upstream mlibc, or the relevant package behavior before changing Orlix.
- Treat every durable upstream patch and package feature override as suspect until root cause is proven. Before adding or changing one, prove the lower owning layers are correct: Linux/kernel syscall and filesystem behavior, OrlixOS rootfs/package inputs, and reviewed package toolchain configuration.
- Route fixes by ownership: kernel semantics to `OrlixKernel`, libc behavior to `OrlixMLibC/Sources`, upstream package/rootfs assembly and delivered OS payload/session wiring to `OrlixOS`, and private iOS mechanics to `OrlixHostAdapter`.
- Test runners may consume the `OrlixOS` session surface to collect upstream output, but they must not patch, filter, skip, or reinterpret upstream tests.
- Do not disable upstream package features, suppress upstream tests, or force `configure`/libtool with ad hoc `LD` overrides to pass cross builds; fix reviewed OrlixOS package-toolchain inputs and prove the unchanged upstream behavior.

## Evidence

Record exact commands, upstream versions/commits, logs, failures, skips, crash checks, final markers, and the patch/override root-cause chain in `docs/plans/active/<task>/IMPLEMENT.md`.

For Coreutils full-suite claims, success requires the upstream suite marker with zero failures and zero skips for the expected test count. Partial or targeted runs are not full proof.
