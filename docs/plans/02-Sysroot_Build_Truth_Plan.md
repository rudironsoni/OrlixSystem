# Sysroot And Build Truth Plan

## Status

Authoritative Milestone 1 plan.

This milestone follows the delivered `IXLandKernel` and `IXLandHostAdapter` split and assumes that the physical ownership boundary and kernel-owned active seam are now in place.

It must still account for the remaining Milestone 0 learning:

- structure alone is not enough
- host-backed runtime primitives can still violate Linux-visible expectations after the seam is cleaned up
- package-facing truth must therefore stay strict about ownership and proof rather than relying on convenient local shapes

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
4. Eliminate local Linux-like typedef reinvention inside `IXLandKernel` and kernel-private backing contracts where vendored Linux truth or libc ownership should apply instead.
5. Make configure and compile probes reliable for real packages.
6. Ensure build-truth cleanup does not bless host-private adapter declarations as Linux-facing ownership surfaces.

## Why This Milestone Exists

After the split, the next major failure mode is not host-leakage by directory visibility.

It is contract drift caused by repo-local reinvention of Linux-facing types, constants, and structs.

Typical drift patterns include:

- fixed-width aliases such as `linux_mode_t` and `linux_off_t` living in Linux-owner headers
- locally defined Linux-looking structs that should instead come from vendored Linux truth or libc-facing ownership
- kernel-private backing contracts that recreate Linux-shaped scalar types instead of depending on owned contract types
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
- The repo still contains local Linux-like scalar type recreation in Linux-owner and kernel-private contract-adjacent headers, including examples such as:
  - `IXLandKernel/fs/vfs.h`
  - `IXLandKernel/fs/fdtable.h`
  - current kernel-owned backing contract surfaces
- The project completion matrix already marks Milestone 1 as partial for Linux UAPI truth, kheaders classification, and libc-owned type ownership.

This means the foundation for M1 exists, but the ownership cleanup and package-facing discipline are not complete.

## Delivered Milestone 1 Slices So Far

Milestone 1 is no longer theoretical in this repo. The following slices are already delivered and simulator-proofed:

- `IXLandMLibC` bootstrap headers now exist for:
  - `fcntl.h`
  - `poll.h`
  - `signal.h`
  - `sys/stat.h`
  - `sys/types.h`
  - `sys/uio.h`
  - `unistd.h`
  - `time.h`
  - `sys/time.h`
  - `sys/socket.h`
  - `sys/select.h`
- compile-smoke proof now covers:
  - vendored UAPI truth
  - kheaders classification
  - package-facing libc header bootstrap
  - package-style configure probe bootstrap
- `IXLandKernel/fs/readdir.c` no longer owns host directory iteration directly; that host mechanism now sits behind a kernel-owned private `backing_dir` contract

Those changes matter because they prove Milestone 1 can advance in bounded slices without pretending the entire libc/sysroot problem is already complete.

## Latest Milestone 1 Learning

The current repo teaches an important tranche rule:

- package-facing bootstrap ownership and configure-probe truth can land cleanly in one tranche
- some deeper kernel-owner header cutovers cannot be mixed into that tranche safely just because they touch the same conceptual area

The attempted kernel cutover for `time`, `sys/time`, `sys/socket`, and `sys/select` exposed mixed simulator ABI conflicts when forced into the same slice as package-facing bootstrap work.

That does not make those kernel cutovers wrong.
It means they must be treated as a separate, explicit kernel-owner cleanup tranche with their own proof and compatibility strategy.

## Required Decisions

### Header model

- `uapi/include` is the only production Linux ABI include truth in this repo.
- `kheaders/source` and `kheaders/generated` are private classified kernel reference only.
- `IXLandKernel` must not invent Linux ABI items already available in vendored Linux truth.
- `IXLandKernel` must not own libc-owned typedefs or APIs.
- Kernel-owned private backing contracts must not reintroduce Linux ABI ownership drift.
- The end-state is kernel-owned private contracts plus `IXLandMLibC` sysroot truth, not convenience leakage through host-private declarations.

### Ownership model

- `IXLandMLibC` owns package-facing libc ABI headers, typedef surfaces, and sysroot installation shape.
- `IXLandMLibC` owns libc-facing types such as `pid_t`, `uid_t`, `gid_t`, `mode_t`, `dev_t`, `ino_t`, `socklen_t`, `sigval`, `sigevent`, `statvfs`, and related libc-facing API surface.
- `IXLandKernel` owns syscall/runtime semantics and private virtual-kernel state only.
- `IXLandHostAdapter` owns private mechanism implementation only and must not become a second place where Linux or libc-facing type truth is recreated.

### Build-truth model

- XcodeGen and the generated Xcode project remain the only authoritative build truth for this repo.
- UAPI headers may be visible to Linux-owner compile units as Linux contract truth.
- Kheaders must be visible only to explicitly classified compile-smoke or private kernel-reference use, never as blanket production include roots.
- Package-facing sysroot assembly belongs to `IXLandMLibC`, not to ad hoc repo-local header search path convenience.

## Non-Negotiable Rules

1. `IXLandKernel` must not hand-own libc ABI.
2. Vendored Linux UAPI is the only Linux userspace ABI truth in this repo.
3. Kheaders are not package-facing ABI.
4. Adapter-owned seams must not become a backdoor for Linux-like typedef recreation.
5. This milestone must not convert branded host-adapter seam headers into accepted Linux-facing build truth.
6. Configure-probe success must come from correct ownership and include surfaces, not local one-off compatibility hacks.
7. If a type or constant already has a real owner, this milestone must move the repo toward that owner instead of preserving local duplicates.

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

### 2. Linux-like scalar typedef recreation in kernel-private backing contracts

Current risk is no longer exported adapter seam headers. The risk is now that kernel-private backing contracts or adjacent headers could casually re-own Linux-looking scalar types.

Current example to audit first:

- current kernel-owned backing I/O contract surfaces

Milestone direction:

- keep seam declarations kernel-owned and narrow
- avoid making kernel-private contract headers a second source of Linux or libc-facing type truth

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

### 6. Linux-owner host-header drift that still needs separate cleanup

Current repo state still includes Linux-owner code that reaches for host libc headers in some subsystems, especially around:

- `time.h`
- `sys/time.h`
- `sys/socket.h`
- `sys/select.h`

Milestone direction:

- do not normalize those includes as acceptable end-state ownership
- do not force them all out in the same tranche as package bootstrap additions
- plan them as a separate kernel-owner cleanup step with simulator ABI proof

## Deliverables

1. Document and enforce the UAPI plus kheaders model.
2. Produce an ownership inventory for Linux-facing headers and typedef surfaces that distinguishes:
   - vendored UAPI truth
   - vendored kheaders reference only
   - `IXLandMLibC` ownership
   - `IXLandKernel` private-only state
   - `IXLandHostAdapter` private mechanism-only implementation
3. Remove or replace risky local typedef recreation in `IXLandKernel` and keep kernel-private backing contracts narrow and ownership-correct.
4. Keep kheaders out of blanket production include roots.
5. Add compile-smoke and configure-probe proof for real package expectations.
6. Update repo docs and guardrails so future work cannot casually reintroduce ownership drift.
7. Move obvious host-owned mechanics such as directory iteration behind kernel-owned private contracts instead of leaving Linux-owner code to include host directory APIs directly.

## Implementation Rules

### Do

- Treat package-facing header truth as a sysroot problem first.
- Validate constants, layouts, and typedef ownership against vendored Linux headers and libc ownership.
- Prefer generated or validated mirrors over ad hoc local recreation when something is needed outside installed UAPI.
- Keep Linux-owner files dependent on canonical include roots rather than path-specific vendored hacks.
- Make proof files intentionally separate for UAPI truth, kheaders truth, and configure-probe truth.
- Split package-facing bootstrap work from deeper kernel-owner header cutovers when simulator ABI conflicts show they are not safely the same tranche.

### Don't

- Do not include libc headers directly in Linux-owner code as source of truth.
- Do not treat kheaders as package-facing ABI.
- Do not hand-define Linux ABI constants if vendored Linux truth already provides them.
- Do not let kernel-private backing contracts recreate Linux-owned or libc-owned types casually.
- Do not preserve local typedefs just because they currently unblock one compile unit.
- Do not hide sysroot defects behind broader Xcode header search paths.
- Do not present a failed or reverted kernel-owner cutover as Milestone 1 progress just because the package-facing bootstrap part of the experiment was valid.

## Acceptance Gates

Milestone 1 is not complete until all are true:

1. UAPI include truth is documented and enforced as the only production Linux ABI source.
2. Kheaders remain isolated to explicit classified use and compile-smoke proof.
3. The repo no longer contains known local Linux-like typedef recreation for surfaces already owned by vendored Linux truth or `IXLandMLibC`.
4. Adapter-owned seam headers are removed from the accepted contract surface.
5. Configure-style compile probes for representative `zsh` and `curl` prerequisites pass through the intended owners.
6. Project build settings do not grant blanket kheaders visibility to production targets.
7. Repo docs describe the resulting ownership model clearly enough to keep future tranches aligned.
8. No Milestone 1 outcome is described as complete if it still depends on branded host-adapter vocabulary as normal kernel-facing contract surface.
9. Linux-owner code does not keep ambient host directory iteration APIs such as `dirent.h`, `fdopendir()`, `readdir()`, `seekdir()`, `telldir()`, or `closedir()` as part of normal kernel ownership.
10. Remaining Linux-owner `time` / `sys/time` / `sys/socket` / `sys/select` cleanup is tracked honestly as separate follow-on work if it has not actually landed green.

## Proof

Required proof for this milestone:

1. `IXLandKernelTests/LinuxUAPICompileSmoke.c` remains green.
2. `IXLandKernelTests/LinuxKHeadersCompileSmoke.c` remains isolated and green.
3. New configure-style compile probes cover representative package prerequisites for `zsh` and `curl`.
4. The authoritative simulator build remains green through the canonical XcodeGen plus Xcodebuild flow.
5. Lint and repo checks fail if kheaders or local typedef ownership drift reappears.
6. Lint fails if Linux-owner code reintroduces ambient host directory-iteration APIs instead of using the private backing contract.

## Definition Of Success

Milestone 1 succeeds when the repo stops improvising Linux-facing header ownership locally and instead presents a clean build story:

- Linux UAPI truth comes from vendored `uapi/include`
- private kernel reference comes from classified `kheaders/source` and `kheaders/generated`
- libc-facing sysroot truth belongs to `IXLandMLibC`
- `IXLandKernel` keeps only private runtime state and syscall semantics
- `IXLandHostAdapter` remains mechanism-only implementation

It does not require pretending that every remaining host-libc-shaped kernel include has already been solved.
Milestone 1 is still partial until those kernel-owner follow-on cutovers are delivered green in their own bounded tranches.

That is the minimum build-surface truth required before the roadmap can honestly claim package configure and compile transparency.
