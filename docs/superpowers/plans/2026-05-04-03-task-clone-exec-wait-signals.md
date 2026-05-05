# Task, Clone, Exec, Wait, And Signals Implementation Plan

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

**Goal:** Finish Linux-visible task lifecycle, signal-delivery, wait-status, and ptrace parity needed for process-heavy userspace beyond shell-base.

**Architecture:** Consolidate process semantics in the task, fork, wait, signal, and ptrace owners, with `runtime/syscall.c` exposing only Linux-shaped entry points. The tranche closes lifecycle and stop-state gaps by writing contract-first tests for thread-group behavior, wait encoding, restart semantics, and ptrace events, then filling the minimal Linux-owner implementations.

**Tech Stack:** `kernel/task.c`, `kernel/fork.c`, `kernel/exit.c`, `kernel/wait.c`, `kernel/signal.c`, `kernel/ptrace.c`, `runtime/syscall.c`, LinuxKernel task, signal, wait, and ptrace tests.

---

## Tranche Scope

- `clone3` edge cases beyond basic creation:
  - thread-group creation (`CLONE_THREAD`) and thread-group identity invariants
  - parent/child TID bookkeeping (`set_tid`, `clear_tid`, `parent_tid`, `child_tid`)
  - `pidfd` interactions (return value, lifetime, close semantics)
  - invalid flag combinations and Linux-shaped errno
- parent and child lifecycle separation
- wait status encoding and parent notifications
- stop, continue, and default-action parity
- `SA_RESTART` and restart metadata
- ptrace stop and event lifecycle
- exec lifecycle and ptrace-on-exec parity (Linux-owned exec state transitions; no host-shaped exec semantics)

## Linux-Shaped Runtime Model Notes

- PIDs/PGIDs/SIDs are IXLand task identities; do not bind Linux-visible identity to host processes.
- Signals are IXLand-owned queues/masks/delivery; do not delegate Linux signal semantics to Darwin signals.
- Ptrace supervision is over IXLand tasks only; it must not imply host-process ptrace behavior.

### Task 1: Tighten `clone3` And Thread-Group Parity

**Files:**
- Modify: `kernel/fork.c`
- Modify: `kernel/task.c`
- Modify: `runtime/syscall.c`
- Test: `IXLandSystemLinuxKernelTests/TaskExecContract.c`
- Test: `IXLandSystemLinuxKernelTests/TaskGroupTests.m`

- [ ] Add failing coverage for thread-group creation, parent and child TID bookkeeping, pidfd interactions, and invalid flag combinations.
- [ ] Run the focused task-group tests and capture current lifecycle mismatches.
- [ ] Implement the minimal task and fork changes needed for Linux-shaped thread-group semantics.
- [ ] Re-run task-group tests until clone, thread-group, and pidfd-adjacent behavior remain green.

**Acceptance (contract must fail if regressed):**
- thread-group IDs and per-thread TIDs remain Linux-shaped under `CLONE_THREAD`
- `clone3` rejects invalid flag combos with Linux-shaped errno (not “best effort”)
- `pidfd` behavior is stable enough for poll/close/lifetime tests (even if minimal)

### Task 2: Complete Wait Status And Parent Notification Rules

**Files:**
- Modify: `kernel/wait.c`
- Modify: `kernel/exit.c`
- Modify: `kernel/task.c`
- Test: `IXLandSystemLinuxKernelTests/WaitJobControlContract.c`
- Test: `IXLandSystemLinuxKernelTests/WaitJobControlTests.m`

- [ ] Add failing wait coverage for exit, signal death, stop, continue, and traced-stop status encoding, including `waitid` and any existing wait-family entry points.
- [ ] Add parent-notification assertions for thread-group exit, zombie state transitions, and reaping order.
- [ ] Implement wait-state and notification fixes in the owning kernel paths.
- [ ] Re-run the wait and job-control suite until encoding and lifecycle parity match the contract.

**Acceptance (contract must fail if regressed):**
- exit vs signal-death status encoding matches Linux bit layout expectations
- stop/continue and ptrace-stop statuses are unambiguous and round-trip in wait APIs
- parent notifications and reaping order produce deterministic, Linux-shaped results

### Task 3: Finish Signal Default Actions And Restart Semantics

**Files:**
- Modify: `kernel/signal.c`
- Modify: `kernel/task.c`
- Test: `IXLandSystemLinuxKernelTests/SignalSyscallContract.c`
- Test: `IXLandSystemLinuxKernelTests/SignalTests.m`

- [ ] Add failing coverage for default actions, blocked and pending signal transitions, `SA_RESTART`, interrupted syscalls, and restart metadata handoff.
- [ ] Run the focused signal suite to confirm the current failure mode before implementation.
- [ ] Implement the minimal signal-owner changes needed to preserve Linux restart and mask semantics.
- [ ] Re-run the signal tests and confirm existing `rt_sigaction`, `rt_sigprocmask`, `tgkill`, and `sigaltstack` paths stay green.

**Acceptance (contract must fail if regressed):**
- pending vs blocked delivery ordering remains Linux-shaped
- default actions do not silently “do nothing” when Linux requires a state change
- `SA_RESTART` changes observable syscall interruption outcomes (where applicable)

### Task 4: Expand Ptrace Event Lifecycle

**Files:**
- Modify: `kernel/ptrace.c`
- Modify: `kernel/ptrace.h`
- Modify: `kernel/wait.c`
- Modify: `runtime/syscall.c`
- Test: `IXLandSystemLinuxKernelTests/PtraceContract.c`
- Test: `IXLandSystemLinuxKernelTests/PtraceTests.m`

- [ ] Add failing ptrace coverage for syscall-stop restart, signal-delivery stops, inherited options, clone or exec or exit events, and wait-status parity.
- [ ] Run the ptrace suite and capture the exact missing lifecycle transitions.
- [ ] Implement ptrace events and wait integration in Linux-owner code only.
- [ ] Re-run the ptrace suite until the new event lifecycle and old ptrace behavior both pass.

**Acceptance (contract must fail if regressed):**
- ptrace events affect wait status exactly (not “side-channel” state)
- exec/clone/exit ptrace events fire only for IXLand tasks (never host processes)

### Task 5: Add Explicit Exec Lifecycle Coverage (Linux-Owned)

**Why:** The tranche title includes **Exec**, and ptrace event parity depends on a correct, Linux-owned exec lifecycle. This task pins down exec invariants before any ptrace-on-exec claim.

**Files:**
- Modify: `kernel/task.c`
- Modify: `runtime/syscall.c`
- Modify: (as needed) `kernel/exec.c` (or the owning exec implementation file if named differently)
- Test: `IXLandSystemLinuxKernelTests/TaskExecContract.c`
- Test: `IXLandSystemLinuxKernelTests/TaskExecTests.m` (or extend existing exec tests if present)

- [ ] Add failing coverage for exec lifecycle invariants: thread-group exec rules, signal disposition behavior across exec, and wait/ptrace observable state transitions.
- [ ] Run focused exec tests and capture exact mismatches before implementation.
- [ ] Implement minimal Linux-owner exec lifecycle fixes (no `internal/ios/**` dependencies).
- [ ] Re-run exec tests until green and confirm ptrace-on-exec tests depend on these invariants (no duplication).

**Acceptance (contract must fail if regressed):**
- exec transitions update the task/thread-group state in Linux-shaped ways observable via wait/ptrace
- exec does not “leak” host concepts into Linux-owner task identity or signal semantics

### Task 6: Full Tranche Proof (Required Before Commit/Push)

**Files:**
- Reference: `docs/superpowers/plans/2026-05-04-00-kernel-completion-orchestration.md`

- [ ] Re-run lint, project generation, and the authoritative simulator `build-for-testing` flow:
  - `rtk bash ./scripts/lint_linux_surface.sh`
  - `rtk xcodegen generate --project .`
  - `rtk proxy /bin/zsh -lc "xcodebuild build-for-testing -project IXLandSystem.xcodeproj -scheme IXLandSystem-6.12-arm64 -sdk iphonesimulator -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17' -derivedDataPath /Volumes/1TB/Xcode/DerivedData 2>&1 | xcsift -f toon"`
- [ ] Run the focused simulator suites for tranche-local proof (clone/wait/signal/ptrace/exec), then run the full shared-scheme simulator suite before any tranche-finished claim.
- [ ] Confirm LinuxKernel tests still avoid `internal/ios/**`.
- [ ] Run the orchestration plan’s scope-closure audit: verify each `Tranche Scope` bullet has both (a) an owning implementation in `kernel/` / `runtime/` and (b) an explicit contract assertion that would fail if the behavior disappeared.
- [ ] Commit and push only after the proof gate and branch-synchronization checks succeed.
