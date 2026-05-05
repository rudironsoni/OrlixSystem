# VFS, Mounts, Procfs, And Devfs Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

## Product North Star (Non-Negotiable)

- IXLandSystem is a Linux-shaped kernel/runtime substrate hosted inside an iOS app sandbox.
- Public contracts are Linux-shaped; iOS is private host environment only.
- Linux semantics live in `fs/`, `kernel/`, `runtime/`, `include/`.
- Host mechanics live only in `internal/ios/**`, behind narrow subsystem-owned seams.
- Do not treat Darwin behavior as Linux truth; do not invent Linux-looking headers/constants/types.
- Linux header truth comes only from vendored generated Linux headers:
  - tuple root: `third_party/linux/<version>/<arch>/`
  - surfaces: `uapi/include`, `srctree`, `objtree`

**Goal:** Replace the remaining VFS and mount `-ENOSYS` gaps with Linux-shaped behavior and deepen `/proc` and `/dev` coverage so real userspace sees stable filesystem-facing kernel state.

**Architecture:** Finish missing VFS entrypoints in the Linux-owner filesystem stack, then extend mount lifecycle and namespace visibility without moving semantic decisions into host mediation. Procfs and devfs work follows the same rule: Linux-visible node shape and metadata live in `fs/` and supporting kernel code, while any host storage mediation stays narrow and private.

**Tech Stack:** `fs/vfs.c`, `fs/path.c`, `fs/namei.c`, `fs/mount.c`, `fs/super.c`, `fs/fdtable.c`, `fs/pty.c`, proc and dev helpers, LinuxKernel VFS/proc/dev contracts.

---

## Repo Truth Preflight (Must Match This Checkout)

- [ ] Confirm the canonical public mount syscalls live in `fs/mount.c` (`mount()`, `umount()`, `umount2()`), and that `runtime/syscall.c` does not dispatch `__NR_mount` / `__NR_umount2` today.
- [ ] Locate the current VFS “dead API” stubs in `fs/vfs.c`:
  - `vfs_close`, `vfs_lookup`, `vfs_path_walk`, `vfs_mkdir`, `vfs_unlink`, `vfs_rmdir` currently return `-ENOSYS`.
- [ ] Decide (in this tranche) whether those stubs will:
  - be wired into syscall-facing or libc-facing entrypoints (so implementing them changes Linux-visible behavior), or
  - be deleted and removed from headers if they are not part of the intended syscall-facing surface.

Rationale: implementing unused stubs is test-theater. This tranche must change Linux-visible behavior or tighten explicit unsupported policy with contracts.

## Tranche Scope (Linux-Visible Outcomes)

- syscall-facing VFS closure (only where it is actually used by syscalls / libc-facing wrappers)
- mount and `umount2` lifecycle parity for supported mount modes
- procfs task + fd/fdinfo/stat/status stability (Linux-visible, not host-derived)
- devfs + `devpts`, `/dev/tty`, and controlling-terminal visibility
- explicit unsupported policy remains contracted (no silent “holes”)

## Storage And Mount Policy (Product Contract)

- `/etc`, `/usr`, `/var/lib`, and `/home` are Linux-visible paths backed by explicit persistent app-private storage (or an explicit bundled-image backend where intended).
- `/var/cache` is cache-backed.
- `/tmp` is temporary-directory-backed.
- The app Documents directory is a mounted subtree (if present), not the Linux root truth.
- External/security-scoped folders enter only via explicit virtual mountpoints.
- Raw host paths must not become canonical Linux-visible paths.

## Explicit Unsupported Policy (Must Be Contracted)

IXLandSystem does not need to “pretend Linux supports everything”. But any unsupported mode must be:
- rejected explicitly with stable Linux errno, and
- covered by LinuxKernel C contracts (so we do not regress into accidental host behavior).

Current repo truth: `mount()` rejects non-bind/non-remount/non-move (and non-`cgroup2`) with `ENOSYS`, and this is already asserted in `IXLandSystemLinuxKernelTests/VFSAtContract.c`.

### Task 1: Close Syscall-Facing VFS Gaps (No Dead-API Theater)

**Files:**
- Modify: `fs/vfs.c` (only along actually-used entrypoints)
- Modify: `fs/path.c` / `fs/namei.c` (only if used by the syscall-facing path)
- Test: `IXLandSystemLinuxKernelTests/VFSAtContract.c`
- Test: `IXLandSystemLinuxKernelTests/VFSPathTests.m`

- [ ] Identify the syscall-facing / libc-facing entrypoints that should own the missing behavior (for example `openat*`, `mkdirat`, `unlinkat`, `renameat*`, `getdents64`, `readlinkat`, `statx`, etc.), and map each missing behavior to its owning function.
- [ ] Add failing contract coverage that proves Linux-visible behavior changes (not just that a stub returns something):
  - absolute vs `dirfd`-relative paths
  - `AT_*` flags behavior where applicable
  - error returns and errno stability
  - “no host path leakage” assertions for paths exposed back to userspace
- [ ] Implement the missing behavior in Linux-owner code, using existing VFS routing + mount namespace state.
- [ ] If `vfs_close/lookup/path_walk/mkdir/unlink/rmdir` are intended to be part of the actual surface:
  - wire them into the syscall-facing call path and prove via contracts that those functions are now exercised
  - otherwise delete them (and their declarations) in the same tranche rather than “completing” unused code.
- [ ] Re-run focused VFS suites until contracts pass without weakening policy.

### Task 2: Stabilize Procfs Task And Fd Views (Linux-Owned, Not Host-Derived)

**Files:**
- Modify: proc-related source under `fs/`
- Modify: `fs/fdtable.c` (only where proc output depends on fd state)
- Test: `IXLandSystemLinuxKernelTests/ProcfsNamespaceContract.c`
- Test: `IXLandSystemLinuxKernelTests/ProcfsNamespaceTests.m`

- [ ] Add failing procfs coverage for `/proc/<pid>/fd`, `/proc/<pid>/fdinfo`, `/proc/<pid>/stat`, `/proc/<pid>/status` around:
  - task exit and zombie/reap transitions
  - exec transitions (where applicable)
  - mount-namespace rebasing (proc path and mount info should match IXLand mount graph)
- [ ] Implement procfs record generation from IXLand task/fdtable/mount namespace state (never raw host metadata).
- [ ] Add explicit “no host path leakage” assertions for procfs where it would be tempting to print host paths.
- [ ] Re-run procfs tests and confirm stability under lifecycle transitions.

### Task 3: Deepen Mount And Unmount Lifecycle (Supported Modes Only)

**Files:**
- Modify: `fs/super.c`
- Modify: `fs/mount.c` (canonical owner for `mount()/umount()/umount2()` wrappers)
- Modify: `fs/vfs.c` (mount namespace + propagation bookkeeping)
- Test: `IXLandSystemLinuxKernelTests/ProcfsNamespaceContract.c`
- Test: `IXLandSystemLinuxKernelTests/ProcfsNamespaceTests.m`

- [ ] Add failing coverage for `mount`, `umount2` variants, and namespace-visible mount teardown:
  - bind mounts (`MS_BIND`)
  - remount attribute changes (`MS_REMOUNT` + attribute flags)
  - move mounts (`MS_MOVE`)
  - `umount2` flags: `MNT_DETACH`, `MNT_FORCE`, `MNT_EXPIRE`, and `UMOUNT_NOFOLLOW`
  - propagation flags and `MS_REC` behavior for tree operations (where supported)
- [ ] Keep “non-bind mount types unsupported” explicit:
  - confirm contracts still assert `ENOSYS` for a non-bind mount request (for example tmpfs)
  - do not expand to broad filesystemtype handling in this tranche
- [ ] Tighten mount lifecycle bookkeeping in Linux-owner code and ensure procfs mount views match the IXLand mount graph.
- [ ] Decide `statvfs/fstatvfs` policy:
  - implement Linux-shaped `statvfs` over IXLand’s virtual mount stats, or
  - add explicit contract(s) that these remain `ENOSYS` (so it is a deliberate hole, not accidental).

### Task 4: Complete Devfs And `devpts` Coverage

**Files:**
- Modify: `fs/pty.c`
- Modify: dev-related source under `fs/`
- Test: `IXLandSystemLinuxKernelTests/DevfsContract.c`
- Test: `IXLandSystemLinuxKernelTests/DevfsTests.m`
- Test: `IXLandSystemLinuxKernelTests/PTYSessionContract.c`
- Test: `IXLandSystemLinuxKernelTests/PTYSessionTests.m`

- [ ] Add failing coverage for `devpts` node creation, `/dev/tty` resolution, PTY control nodes, and controlling-tty visibility from session state.
- [ ] Implement missing device-node exposure in devfs without introducing branded device names or host-derived node conventions.
- [ ] Re-run the devfs and PTY session suites until device visibility, open behavior, and session interactions remain green.

### Task 5: Full Proof And Boundary Audit

**Files:**
- Reference: `docs/superpowers/plans/2026-05-04-00-kernel-completion-orchestration.md`

- [ ] Re-run lint, project generation, and the AGENTS-authoritative simulator `build-for-testing` flow.
- [ ] Run the focused VFS and proc or dev simulator suites for tranche-local proof, then run the full shared-scheme simulator suite before any milestone-finished claim.
- [ ] Run a boundary grep for host vocabulary in touched filesystem files.
- [ ] Run the orchestration plan’s scope-closure audit: for each `Tranche Scope` bullet, open the owning `fs/` implementation and confirm there is an explicit LinuxKernel contract assertion for it (not just incidental coverage).
- [ ] Commit and push only after the full proof gate passes and branch synchronization is verified.

## ABI / Type Hygiene Checkpoint (Must Be Addressed In This Tranche)

The VFS headers currently define repo-local “linux_*” typedefs in `fs/vfs.h` to avoid importing Darwin headers.

- [ ] Confirm these typedefs do not leak into public ABI headers under `include/` and are not used as public contract types.
- [ ] If any of these types are used as a public contract, reclassify ownership:
  - Linux UAPI types must come from vendored generated Linux headers.
  - libc-owned typedef surfaces must live in IXLandMLibC, not IXLandSystem.
- [ ] If the typedefs remain internal, keep them strictly private to the VFS implementation surface and prove contracts use real Linux UAPI constants/layouts (C contract files), not repo-local constants/types.
