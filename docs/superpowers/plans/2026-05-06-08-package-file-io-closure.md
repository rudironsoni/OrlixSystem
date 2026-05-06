# Package File I/O Closure Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the highest-value remaining package-priority file-I/O syscall gaps so Linux userspace can rely on vector I/O, truncation, and in-kernel file-copy behavior through the IXLandSystem Linux surface.

**Architecture:** Keep all Linux semantics in `fs/` and `runtime/`, reusing the existing fdtable, read/write, open-file-description, and VFS ownership already present in the repo. This tranche is intentionally narrow: it closes `truncate`, `preadv`, `pwritev`, `preadv2`, `pwritev2`, and `sendfile` as one coherent file-I/O surface instead of mixing in unrelated signal, mount, or namespace work.

**Tech Stack:** C kernel/runtime code, vendored generated Linux headers, `runtime/syscall.c`, `fs/read_write.c`, `fs/fdtable.c`, `fs/vfs.c`, LinuxKernel syscall contracts, XcodeGen, simulator proof on `iPhone 17` with `/Volumes/1TB/Xcode/DerivedData`.

---

## Why This Is The Next Plan

- The original numbered portfolio ends at `07`; there is no repo-authored `08` yet.
- The current gap matrix still marks these package-priority syscalls as missing:
  - `truncate` at `docs/syscall_gap_matrix_6.12_arm64.md:52`
  - `preadv` at `docs/syscall_gap_matrix_6.12_arm64.md:76`
  - `pwritev` at `docs/syscall_gap_matrix_6.12_arm64.md:77`
  - `sendfile` at `docs/syscall_gap_matrix_6.12_arm64.md:78`
  - `preadv2` at `docs/syscall_gap_matrix_6.12_arm64.md:277`
  - `pwritev2` at `docs/syscall_gap_matrix_6.12_arm64.md:278`
- These are one subsystem, package-facing, and materially useful for real Linux userspace without inventing a fake new milestone family.

## Tranche Scope

- `truncate(2)` path-based file size changes
- `preadv(2)` and `pwritev(2)` positional vectored I/O
- `preadv2(2)` and `pwritev2(2)` Linux flag policy over the same core implementation
- `sendfile(2)` file-to-fd transfer semantics
- LinuxKernel proof for offset rules, open-file-description behavior, and error cases
- explicit syscall-gap-matrix reclassification for the closed surface

## Out Of Scope

- mount or unmount depth
- signalfd or other signal-owner work
- netlink or further networking work
- io_uring, splice/vmsplice/tee, or AIO families

### Task 1: Close `truncate(2)` As A Linux VFS Surface

**Files:**
- Modify: `runtime/syscall.c`
- Modify: `fs/vfs.c`
- Modify: `fs/open.c` (only if path/open helpers are needed)
- Test: `IXLandSystemLinuxKernelTests/NativeSyscallContract.c`
- Test: `IXLandSystemLinuxKernelTests/NativeSyscallContract.h`
- Test: `IXLandSystemLinuxKernelTests/NativeSyscallTests.m`

- [ ] Add failing LinuxKernel coverage for `truncate` changing file length by pathname, preserving `ftruncate` ownership split, and rejecting invalid path conditions.
- [ ] Run focused proof for `NativeSyscallTests` and capture the current failing symptom.
- [ ] Implement `truncate` in Linux-owner code by resolving the pathname through existing VFS/open helpers and reusing the file-size mutation path instead of inventing a host shortcut.
- [ ] Re-run focused proof until the new `truncate` contract passes.

Notes:
- `ftruncate` is already implemented and must remain fd-owned.
- `truncate` must not move Linux pathname semantics into `internal/ios/**`.

### Task 2: Add Core Positional Vectored I/O (`preadv` / `pwritev`)

**Files:**
- Modify: `runtime/syscall.c`
- Modify: `fs/read_write.c`
- Modify: `fs/fdtable.h`
- Modify: `fs/fdtable.c` (only if shared offset helpers need extension)
- Test: `IXLandSystemLinuxKernelTests/NativeSyscallContract.c`
- Test: `IXLandSystemLinuxKernelTests/NativeSyscallContract.h`
- Test: `IXLandSystemLinuxKernelTests/NativeSyscallTests.m`

- [ ] Add failing LinuxKernel coverage for `preadv` and `pwritev` on regular files, proving that explicit offsets do not mutate the open-file-description offset.
- [ ] Add failing coverage for short vector behavior, invalid iov counts, and bad-fd/error paths.
- [ ] Implement shared Linux-owner vectored positional I/O helpers in `fs/read_write.c` that reuse existing single-buffer `pread`/`pwrite` behavior rather than duplicating host-facing mechanics.
- [ ] Wire `__NR_preadv` and `__NR_pwritev` in `runtime/syscall.c` to the new helpers and re-run focused proof until green.

Notes:
- Keep the implementation DRY with the existing `readv`/`writev` and `pread`/`pwrite` surfaces.
- Offset handling must stay Linux-shaped: no implicit file-position mutation on positional calls.

### Task 3: Extend To Linux `preadv2` / `pwritev2` Policy

**Files:**
- Modify: `runtime/syscall.c`
- Modify: `fs/read_write.c`
- Test: `IXLandSystemLinuxKernelTests/NativeSyscallContract.c`
- Test: `IXLandSystemLinuxKernelTests/NativeSyscallContract.h`
- Test: `IXLandSystemLinuxKernelTests/NativeSyscallTests.m`

- [ ] Add failing LinuxKernel coverage for `preadv2` / `pwritev2` with supported zero-flag behavior first.
- [ ] Add failing coverage for explicit Linux flag policy: reject unsupported flags with Linux errno instead of silently ignoring them.
- [ ] Implement `preadv2` / `pwritev2` as thin Linux-owner wrappers over the Task 2 core helpers, only admitting the actually supported flag set for this tranche.
- [ ] Re-run focused proof until the `preadv2` / `pwritev2` contracts pass.

Notes:
- If the repo already has a stable mutable-status or rwf flag policy pattern, follow it.
- Do not claim unsupported RWF behavior as implemented if this tranche only supports `flags == 0`.

### Task 4: Implement `sendfile(2)` Using Linux FD Semantics

**Files:**
- Modify: `runtime/syscall.c`
- Modify: `fs/read_write.c`
- Modify: `fs/fdtable.c` (only if offset/update helpers are needed)
- Test: `IXLandSystemLinuxKernelTests/NativeSyscallContract.c`
- Test: `IXLandSystemLinuxKernelTests/NativeSyscallContract.h`
- Test: `IXLandSystemLinuxKernelTests/NativeSyscallTests.m`

- [ ] Add failing LinuxKernel coverage for `sendfile` from regular-file input to writable output, proving return count, source offset behavior, and destination write effects.
- [ ] Add failing coverage for the Linux `offset == NULL` versus `offset != NULL` distinction and for invalid descriptor class / access-mode failures.
- [ ] Implement the minimal Linux-owner `sendfile` data pump in `fs/read_write.c`, reusing existing read/write primitives and preserving Linux open-file-description offset rules.
- [ ] Re-run focused proof until `sendfile` passes without regressing adjacent fd behavior.

Notes:
- Keep this tranche scoped to the minimal file-backed behavior the contracts prove.
- Do not expand into splice-style pipe semantics here.

### Task 5: Matrix Update, Boundary Audit, And Full Proof

**Files:**
- Modify: `docs/syscall_gap_matrix_6.12_arm64.md`
- Reference: `scripts/lint_linux_surface.sh`
- Reference: `IXLandSystem.xcodeproj`
- Reference: `docs/superpowers/plans/2026-05-04-00-kernel-completion-orchestration.md`

- [ ] Reclassify `truncate`, `preadv`, `pwritev`, `preadv2`, `pwritev2`, and `sendfile` in `docs/syscall_gap_matrix_6.12_arm64.md` to repo truth with no stale `missing:*` markers left for the implemented surface.
- [ ] Audit only the touched Linux-owner files for forbidden Darwin/iOS leakage and host vocabulary drift.
- [ ] Run `rtk bash ./scripts/lint_linux_surface.sh`.
- [ ] Run `rtk xcodegen generate --project .`.
- [ ] Run the AGENTS proof build:

```bash
rtk xcodebuild build-for-testing \
  -project IXLandSystem.xcodeproj \
  -scheme IXLandSystem-6.12-arm64 \
  -sdk iphonesimulator \
  -configuration Debug \
  -destination 'platform=iOS Simulator,name=iPhone 17' \
  -derivedDataPath /Volumes/1TB/Xcode/DerivedData 2>&1 | rtk xcsift -f toon
```

Expected: `status: success`, `errors: 0`, `warnings: 0`.

- [ ] Run focused LinuxKernel proof:

```bash
rtk xcodebuildmcp simulator test \
  --project-path /Users/rudironsoni/src/github/rudironsoni/ixland/IXLandSystem/IXLandSystem.xcodeproj \
  --scheme IXLandSystem-6.12-arm64 \
  --simulator-id 63FEBB50-E358-47C6-A8C2-C77E2A391BB2 \
  --configuration Debug \
  --derived-data-path /Volumes/1TB/Xcode/DerivedData \
  --json '{"extraArgs":["-only-testing:IXLandSystemLinuxKernelTests/NativeSyscallTests"]}' \
  --output json
```

Expected: the new file-I/O contracts pass inside `NativeSyscallTests`.

- [ ] Run the full shared-scheme proof:

```bash
rtk xcodebuildmcp simulator test \
  --project-path /Users/rudironsoni/src/github/rudironsoni/ixland/IXLandSystem/IXLandSystem.xcodeproj \
  --scheme IXLandSystem-6.12-arm64 \
  --simulator-id 63FEBB50-E358-47C6-A8C2-C77E2A391BB2 \
  --configuration Debug \
  --derived-data-path /Volumes/1TB/Xcode/DerivedData \
  --output json
```

Expected: full suite green before commit/push.

### Task 6: Scope-Closure Audit Before Publish

**Files:**
- Reference: `docs/superpowers/plans/2026-05-06-08-package-file-io-closure.md`
- Reference: touched implementation and LinuxKernel contract files

- [ ] For each `Tranche Scope` bullet above, confirm there is explicit LinuxKernel contract coverage that fails if the behavior regresses.
- [ ] Grep only touched owning files for shallow stubs before claiming completion:

```bash
rtk rg -n "ENOSYS|TODO|unimplemented|future backend" runtime/syscall.c fs/read_write.c fs/vfs.c IXLandSystemLinuxKernelTests/NativeSyscallContract.c
```

Expected: no touched implementation for this tranche remains stubbed.

- [ ] Commit with a milestone-scoped message such as:

```bash
git commit -m "feat(fs): close package file io syscall gaps"
```

- [ ] Push immediately after proof-backed commit and verify synchronization if merged on `main`.
