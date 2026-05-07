# Sysroot And Build Truth Plan

## Status

Authoritative Milestone 1 plan.

This milestone follows the `IXLandKernel` and `IXLandHostAdapter` split and assumes that the physical ownership boundary is already in place.

## Purpose

Establish a correct package-facing build surface so Linux-oriented packages configure and compile unchanged against the IXLand stack.

Milestone 0 made subsystem ownership explicit in the repo.
Milestone 1 must now make header ownership and build truth equally explicit.

Without this milestone, the repo can still compile selected internal code while remaining an unreliable target for real package configure probes, libc expectations, and Linux ABI ownership.

## Product Goal For This Milestone

Make package-facing build truth deterministic enough that `IXLandMLibC` can present a Linux-oriented sysroot without `IXLandKernel` silently re-owning libc or Linux ABI surface area.

This milestone is about source-compatibility truth, not runtime completion.

## Goals

1. Make `IXLandMLibC` the sole owner of package-facing libc and sysroot surfaces.
2. Keep vendored Linux UAPI as the kernel-userspace contract truth.
3. Keep vendored `kheaders/source` and `kheaders/generated` as classified private kernel reference only.
4. Eliminate local Linux-like typedef reinvention inside `IXLandKernel` and exported host-adapter seam headers where vendored Linux truth or libc ownership should apply instead.
5. Make configure and compile probes reliable for real packages.

## Why This Milestone Exists

After the split, the next major failure mode is not host-leakage by directory visibility.

It is contract drift caused by repo-local reinvention of Linux-facing types, constants, and structs.

Typical drift patterns include:

- fixed-width aliases such as `linux_mode_t` and `linux_off_t` living in Linux-owner headers
- locally defined Linux-looking structs that should instead come from vendored Linux truth or libc-facing ownership
- exported seam headers that recreate Linux-shaped scalar types instead of depending on owned contract types
- build setups that accidentally make kheaders look package-facing when they are only kernel-reference material

If this drift remains, package probes may compile against a shape that is convenient for this repo but not faithful to Linux source expectations.

## Current Repo Context

Relevant current facts in this repo:

- Vendored Linux tuple layout is already normalized to:
  - `third_party/linux/6.12/arm64/uapi/include`
  - `third_party/linux/6.12/arm64/kheaders/source`
  - `third_party/linux/6.12/arm64/kheaders/generated`
- `IXLandKernelTests/LinuxUAPICompileSmoke.c` already proves canonical UAPI include-path resolution.
- `IXLandKernelTests/LinuxKHeadersCompileSmoke.c` already proves isolated kheaders include-path resolution.
- `project.yml` already separates UAPI include paths from kheaders compile-smoke include paths.
- The repo still contains local Linux-like scalar type recreation in Linux-owner and exported seam headers, including examples such as:
  - `IXLandKernel/fs/vfs.h`
  - `IXLandKernel/fs/fdtable.h`
  - `IXLandHostAdapter/include/IXLandHostAdapter/fs/backing_io_decls.h`
- The project completion matrix already marks Milestone 1 as partial for Linux UAPI truth, kheaders classification, and libc-owned type ownership.

This means the foundation for M1 exists, but the ownership cleanup and package-facing discipline are not complete.

## Required Decisions

### Header model

- `uapi/include` is the only production Linux ABI include truth in this repo.
- `kheaders/source` and `kheaders/generated` are private classified kernel reference only.
- `IXLandKernel` must not invent Linux ABI items already available in vendored Linux truth.
- `IXLandKernel` must not own libc-owned typedefs or APIs.
- Exported `IXLandHostAdapter` seam headers must not reintroduce Linux ABI ownership drift.

### Ownership model

- `IXLandMLibC` owns package-facing libc ABI headers, typedef surfaces, and sysroot installation shape.
- `IXLandMLibC` owns libc-facing types such as `pid_t`, `uid_t`, `gid_t`, `mode_t`, `dev_t`, `ino_t`, `socklen_t`, `sigval`, `sigevent`, `statvfs`, and related libc-facing API surface.
- `IXLandKernel` owns syscall/runtime semantics and private virtual-kernel state only.
- `IXLandHostAdapter` owns private mechanism seams only and must not become a second place where Linux or libc-facing type truth is recreated.

### Build-truth model

- XcodeGen and the generated Xcode project remain the only authoritative build truth for this repo.
- UAPI headers may be visible to Linux-owner compile units as Linux contract truth.
- Kheaders must be visible only to explicitly classified compile-smoke or private kernel-reference use, never as blanket production include roots.
- Package-facing sysroot assembly belongs to `IXLandMLibC`, not to ad hoc repo-local header search path convenience.

## Non-Negotiable Rules

1. `IXLandKernel` must not hand-own libc ABI.
2. Vendored Linux UAPI is the only Linux userspace ABI truth in this repo.
3. Kheaders are not package-facing ABI.
4. Exported adapter seams must not become a backdoor for Linux-like typedef recreation.
5. Configure-probe success must come from correct ownership and include surfaces, not local one-off compatibility hacks.
6. If a type or constant already has a real owner, this milestone must move the repo toward that owner instead of preserving local duplicates.

## Tranche Scope

Milestone 1 should be implemented as a build-truth and ownership tranche, not as a package-runtime tranche.

In scope:

- classify every Linux-facing header surface as UAPI truth, kheaders reference, `IXLandMLibC` ownership, or private kernel-only state
- remove or narrow repo-local Linux-like typedef recreation where ownership is already known
- keep production include roots narrow and explicit
- add proof that real configure-style compile checks resolve against the intended owners

Out of scope:

- full package runtime support
- shell semantics
- shebang and exec pipeline changes
- network runtime behavior
- broad subsystem redesign unrelated to build truth

## Existing Risky Surfaces To Address Inside Milestone 1

These are part of M1, not optional later cleanup.

### 1. Linux-like scalar typedef recreation in kernel headers

Current examples include Linux-owner headers that locally define names such as `linux_off_t`, `linux_mode_t`, `linux_uid_t`, and `linux_gid_t`.

Current examples to audit first:

- `IXLandKernel/fs/vfs.h`
- `IXLandKernel/fs/fdtable.h`

Milestone direction:

- decide whether each surface is truly private kernel state, vendored Linux ABI truth, or libc-facing ownership
- remove local reinvention where a real owner already exists

### 2. Linux-like scalar typedef recreation in exported host-adapter seam headers

Current examples include exported adapter declarations that define Linux-looking scalar names in the seam itself.

Current example to audit first:

- `IXLandHostAdapter/include/IXLandHostAdapter/fs/backing_io_decls.h`

Milestone direction:

- keep exported seam headers mechanism-only
- avoid making the seam the source of Linux or libc-facing type truth

### 3. Freestanding repo-local integer typedef surfaces used as Linux contract stand-ins

Current repo headers include freestanding builtin-based typedef blocks to avoid host headers.

Some of those are private implementation choices, but some may be standing in for surfaces that should be owned elsewhere.

Milestone direction:

- keep only what is truly private implementation scaffolding
- move Linux-facing ABI and libc-facing ownership to the correct surface instead of preserving convenience typedef packs in public-facing kernel headers

### 4. kheaders visibility drift

Current project state already keeps kheaders isolated in `LinuxKHeadersCompileSmoke.c` compiler flags.

Milestone direction:

- preserve that isolation
- fail any regression that broadens kheaders into production include visibility

### 5. configure-probe truth gaps

Internal compile-smoke is necessary but insufficient.

Milestone direction:

- add proof that package-style compile checks for representative `zsh` and `curl` prerequisites resolve through the intended UAPI and libc-owned surfaces
- classify failures by ownership instead of masking them locally

## Deliverables

1. Document and enforce the UAPI plus kheaders model.
2. Produce an ownership inventory for Linux-facing headers and typedef surfaces that distinguishes:
   - vendored UAPI truth
   - vendored kheaders reference only
   - `IXLandMLibC` ownership
   - `IXLandKernel` private-only state
   - `IXLandHostAdapter` private mechanism-only seams
3. Remove or replace risky local typedef recreation in `IXLandKernel` and exported host-adapter seam headers.
4. Keep kheaders out of blanket production include roots.
5. Add compile-smoke and configure-probe proof for real package expectations.
6. Update repo docs and guardrails so future work cannot casually reintroduce ownership drift.

## Implementation Rules

### Do

- Treat package-facing header truth as a sysroot problem first.
- Validate constants, layouts, and typedef ownership against vendored Linux headers and libc ownership.
- Prefer generated or validated mirrors over ad hoc local recreation when something is needed outside installed UAPI.
- Keep Linux-owner files dependent on canonical include roots rather than path-specific vendored hacks.
- Make proof files intentionally separate for UAPI truth, kheaders truth, and configure-probe truth.

### Don't

- Do not include libc headers directly in Linux-owner code as source of truth.
- Do not treat kheaders as package-facing ABI.
- Do not hand-define Linux ABI constants if vendored Linux truth already provides them.
- Do not let exported host seam headers recreate Linux-owned or libc-owned types casually.
- Do not preserve local typedefs just because they currently unblock one compile unit.
- Do not hide sysroot defects behind broader Xcode header search paths.

## Acceptance Gates

Milestone 1 is not complete until all are true:

1. UAPI include truth is documented and enforced as the only production Linux ABI source.
2. Kheaders remain isolated to explicit classified use and compile-smoke proof.
3. The repo no longer contains known local Linux-like typedef recreation for surfaces already owned by vendored Linux truth or `IXLandMLibC`.
4. Exported `IXLandHostAdapter` seam headers do not define Linux-facing ownership casually.
5. Configure-style compile probes for representative `zsh` and `curl` prerequisites pass through the intended owners.
6. Project build settings do not grant blanket kheaders visibility to production targets.
7. Repo docs describe the resulting ownership model clearly enough to keep future tranches aligned.

## Proof

Required proof for this milestone:

1. `IXLandKernelTests/LinuxUAPICompileSmoke.c` remains green.
2. `IXLandKernelTests/LinuxKHeadersCompileSmoke.c` remains isolated and green.
3. New configure-style compile probes cover representative package prerequisites for `zsh` and `curl`.
4. The authoritative simulator build remains green through the canonical XcodeGen plus Xcodebuild flow.
5. Lint and repo checks fail if kheaders or local typedef ownership drift reappears.

## Definition Of Success

Milestone 1 succeeds when the repo stops improvising Linux-facing header ownership locally and instead presents a clean build story:

- Linux UAPI truth comes from vendored `uapi/include`
- private kernel reference comes from classified `kheaders/source` and `kheaders/generated`
- libc-facing sysroot truth belongs to `IXLandMLibC`
- `IXLandKernel` keeps only private runtime state and syscall semantics
- `IXLandHostAdapter` remains mechanism-only

That is the minimum build-surface truth required before the roadmap can honestly claim package configure and compile transparency.
