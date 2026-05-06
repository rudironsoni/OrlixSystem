# Sysroot And Build Truth Plan

## Purpose

Establish a correct package-facing build surface so Linux-oriented packages configure and compile unchanged against the IXLand stack.

This milestone follows the `IXLandKernel` and `IXLandHostAdapter` split.

## Goals

1. Make `IXLandMLibC` the sole owner of package-facing libc and sysroot surfaces.
2. Keep Linux UAPI as the kernel-userspace contract truth.
3. Keep kheaders as classified private kernel reference only.
4. Eliminate local Linux-like typedef reinvention inside `IXLandKernel`.
5. Make configure and compile probes reliable for real packages.

## Required Decisions

### Header model

- `uapi/include` is the only production Linux ABI include truth.
- `kheaders/source` and `kheaders/generated` are private classified reference only.
- `IXLandKernel` must not invent Linux ABI items already available in vendored Linux truth.
- `IXLandKernel` must not own libc-owned typedefs or APIs.

### Ownership model

- `IXLandMLibC` owns `pid_t`, `uid_t`, `gid_t`, `mode_t`, `dev_t`, `ino_t`, `socklen_t`, `sigval`, `sigevent`, `statvfs`, and other libc-facing surfaces.
- `IXLandKernel` owns syscall/runtime semantics and private virtual-kernel state only.

## Deliverables

1. Document and enforce the UAPI plus kheaders model.
2. Remove or replace risky local typedef recreation in `IXLandKernel` and host adapter seam headers.
3. Keep kheaders out of blanket production include roots.
4. Add compile-smoke and configure-probe proof for real package expectations.

## Guardrails

### Do

- Treat package-facing header truth as a sysroot problem first.
- Validate constants, layouts, and typedef ownership against vendored Linux headers and libc ownership.
- Prefer generated or validated mirrors over ad hoc local recreation when something is needed outside installed UAPI.

### Don't

- Do not include libc headers directly in Linux-owner code as source of truth.
- Do not treat kheaders as package-facing ABI.
- Do not hand-define Linux ABI constants if vendored Linux truth already provides them.
- Do not let host seam headers recreate Linux-owned or libc-owned types casually.

## Proof

1. UAPI compile smoke remains green.
2. Kheaders compile smoke remains isolated and green.
3. Configure-style probe suite passes for `zsh` and `curl` prerequisites.
4. No local Linux ABI recreation remains for surfaces already provided by vendored truth.
