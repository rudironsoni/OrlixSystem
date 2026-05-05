# Syscall Surface Closure Implementation Plan

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

**Goal:** Classify the milestone-01 audited process-adjacent syscall subset and close the specific dispatch and policy gaps that block later milestones from staying Linux-shaped.

**Architecture:** Treat `runtime/syscall.c` as the public Linux syscall gate and make the matrix authoritative for the audited milestone-01 subset instead of claiming repo-wide syscall closure. Finish the small but high-leverage process and pidfd gaps first, then tighten explicit unsupported or future-backend policy so later milestones inherit a clean syscall boundary.

**Tech Stack:** `runtime/syscall.c`, `kernel/task.c`, `kernel/fork.c`, `kernel/signal.c`, `kernel/ptrace.c`, `fs/fdtable.c`, LinuxKernel syscall contracts, syscall gap matrix maintenance.

---

## Tranche Scope

- `pidfd_send_signal`
- `pidfd_getfd`
- `clone3` `set_tid`
- `unshare(CLONE_FS)` policy
- syscall gap matrix updates and audited inventory-contract updates

### Task 1: Audit And Reclassify The Syscall Matrix

**Files:**
- Modify: `runtime/syscall.c`
- Modify: `docs/syscall_gap_matrix_6.12_arm64.md`
- Test: `IXLandSystemLinuxKernelTests/NativeSyscallContract.c`
- Test: `IXLandSystemLinuxKernelTests/NativeSyscallTests.m`

- [ ] Audit only the milestone-01 process-adjacent syscall inventory proven by this task: `unshare`, `pidfd_send_signal`, and `pidfd_getfd`. Reclassify that audited set into repo-truth buckets: `implemented:*`, `kernel-owned next:*`, `libc-owned:*`, or `explicit unsupported policy:*`.
- [ ] Extend `NativeSyscallContract.c` with both compile-time and runtime inventory expectations for the audited syscall set so milestone 01 proves the classification at both levels.
- [ ] Run the focused inventory proof:

```bash
xcodebuild test-without-building \
  -project IXLandSystem.xcodeproj \
  -scheme IXLandSystem-6.12-arm64 \
  -sdk iphonesimulator \
  -configuration Debug \
  -destination 'platform=iOS Simulator,name=iPhone 17' \
  -only-testing:IXLandSystemLinuxKernelTests/NativeSyscallTests
```

Expected: after the milestone-00 `build-for-testing` step, the new inventory assertions fail before dispatch changes land.

- [ ] Update `runtime/syscall.c` classification comments and dispatch tables so `docs/syscall_gap_matrix_6.12_arm64.md` can be updated to non-stale classification for the audited syscall set without claiming full-matrix or full-subsystem closure in milestone 01.

### Task 2: Close `pidfd_send_signal`

**Files:**
- Modify: `runtime/syscall.c`
- Modify: `kernel/signal.c`
- Modify: `kernel/task.c`
- Test: `IXLandSystemLinuxKernelTests/SignalSyscallContract.c`
- Test: `IXLandSystemLinuxKernelTests/SignalTests.m`

- [ ] Add a failing contract that covers sending a signal through a pidfd, invalid pidfd handling, permission checks, and thread-group versus task targeting semantics.
- [ ] Run the focused signal tests and confirm `-ENOSYS` or current policy failure is visible.
- [ ] Implement the Linux-owner pidfd signal path using existing task lookup and signal-delivery rules instead of duplicating host concepts.
- [ ] Re-run the signal suite until the pidfd path passes and ordinary `kill`/`tgkill` behavior remains green.

### Task 3: Close `pidfd_getfd`

**Files:**
- Modify: `runtime/syscall.c`
- Modify: `fs/fdtable.c`
- Modify: `kernel/task.c`
- Test: `IXLandSystemLinuxKernelTests/FcntlContract.c`
- Test: `IXLandSystemLinuxKernelTests/FcntlTests.m`

- [ ] Add a failing contract for `pidfd_getfd` that checks target-task descriptor lookup, permission rejection, close-on-exec propagation, and bad-target error paths.
- [ ] Run the focused fd suite and capture the failing status.
- [ ] Implement descriptor duplication through the existing fdtable semantics so the result behaves like a Linux-shaped duplicate, not a host fd alias.
- [ ] Re-run the fd suite and verify no regression in `dup`, `dup3`, or `fcntl` duplication behavior.

### Task 4: Finish `clone3` `set_tid` And `unshare(CLONE_FS)` Policy

**Files:**
- Modify: `runtime/syscall.c`
- Modify: `kernel/fork.c`
- Modify: `kernel/task.c`
- Modify: `fs/path.c`
- Test: `IXLandSystemLinuxKernelTests/TaskExecContract.c`
- Test: `IXLandSystemLinuxKernelTests/TaskGroupTests.m`

- [ ] Add failing `clone3` coverage for `set_tid` validation, parent and child bookkeeping, and incompatible flag combinations.
- [ ] Add failing `unshare(CLONE_FS)` coverage that makes the intended policy explicit instead of leaving the syscall unclassified.
- [ ] Implement `clone3` `set_tid` in the task and fork paths with Linux-visible validation rules.
- [ ] Implement either Linux-owner `CLONE_FS` unshare support or a deliberate `-EINVAL` or `-EOPNOTSUPP` policy that is documented in the matrix and tests.
- [ ] Re-run the task and exec suite until both the new behavior and existing clone and exec paths remain green.

### Task 5: Update Matrix And Run Two-Tier Proof

**Files:**
- Modify: `docs/syscall_gap_matrix_6.12_arm64.md`

- [ ] Update `docs/syscall_gap_matrix_6.12_arm64.md` explicitly (there is no generator today). Each updated classification must cite:
  - the syscall number/name in `runtime/syscall.c`, and
  - the LinuxKernel test(s) that prove the new classification.

- [ ] Re-run the standard proof gate from the orchestration plan.
- [ ] Run the focused syscall simulator tests for milestone-01 first, then run the full shared-scheme simulator suite before any milestone-finished claim.
- [ ] Commit and push only after `HEAD` and `origin/main` match on the verified branch tip.
