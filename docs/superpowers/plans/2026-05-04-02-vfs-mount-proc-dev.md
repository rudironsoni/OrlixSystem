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

## Tranche Scope

- `vfs_close`
- `vfs_lookup`
- `vfs_path_walk`
- `vfs_mkdir`
- `vfs_unlink`
- `vfs_rmdir`
- mount and `umount2` lifecycle parity
- procfs task, fd, fdinfo, stat, and status stability
- `devpts`, `/dev/tty`, and controlling-terminal device visibility

## Storage And Mount Policy (Product Contract)

- `/etc`, `/usr`, `/var/lib`, and `/home` are Linux-visible paths backed by explicit persistent app-private storage (or an explicit bundled-image backend where intended).
- `/var/cache` is cache-backed.
- `/tmp` is temporary-directory-backed.
- The app Documents directory is a mounted subtree (if present), not the Linux root truth.
- External/security-scoped folders enter only via explicit virtual mountpoints.
- Raw host paths must not become canonical Linux-visible paths.

### Task 1: Close The Missing VFS Entry Points

**Files:**
- Modify: `fs/vfs.c`
- Modify: `fs/path.c`
- Modify: `fs/namei.c`
- Test: `IXLandSystemLinuxKernelTests/VFSAtContract.c`
- Test: `IXLandSystemLinuxKernelTests/VFSPathTests.m`

- [ ] Add failing contract coverage for lookup, path walk, mkdir, unlink, rmdir, and close semantics across absolute paths, dirfd-relative paths, and error returns.
- [ ] Run the VFS path suite and capture the current `-ENOSYS` or incorrect errno failures.
- [ ] Implement the missing VFS functions through existing inode, dentry, and path-resolution ownership rather than shortcutting through host path APIs.
- [ ] Re-run the focused VFS suite until the new operations and existing `openat`, `renameat`, `mkdirat`, and `unlinkat` behavior all pass.

### Task 2: Deepen Mount And Unmount Lifecycle

**Files:**
- Modify: `fs/mount.c`
- Modify: `fs/super.c`
- Modify: `runtime/syscall.c`
- Test: `IXLandSystemLinuxKernelTests/ProcfsNamespaceContract.c`
- Test: `IXLandSystemLinuxKernelTests/ProcfsNamespaceTests.m`

- [ ] Add failing coverage for `mount`, `umount2`, detach, lazy, expire, and recursive bind or move visibility where the current milestone boundary expects support or explicit policy.
- [ ] Add namespace-facing assertions for detached mount lifecycle and per-namespace mount views under `/proc`.
- [ ] Implement or tighten mount lifecycle bookkeeping in Linux-owner code and classify unsupported submodes explicitly instead of leaving syscall holes.
- [ ] Re-run the procfs namespace suite until mount visibility and teardown behavior match the plan’s scope.

### Task 3: Stabilize Procfs Task And Fd Views

**Files:**
- Modify: `fs/fdtable.c`
- Modify: proc-related source under `fs/`
- Test: `IXLandSystemLinuxKernelTests/ProcfsNamespaceContract.c`
- Test: `IXLandSystemLinuxKernelTests/ProcfsNamespaceTests.m`

- [ ] Add failing procfs coverage for `/proc/<pid>/fd`, `/proc/<pid>/fdinfo`, `/proc/<pid>/stat`, and `/proc/<pid>/status` around task exit, exec, and namespace rebasing.
- [ ] Implement procfs record generation from task and fdtable state so proc output reflects Linux-owned kernel state rather than host paths or host metadata.
- [ ] Re-run procfs tests and confirm no host path leakage appears in task-facing proc views.

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
- [ ] Commit and push only after the full proof gate passes and branch synchronization is verified.
