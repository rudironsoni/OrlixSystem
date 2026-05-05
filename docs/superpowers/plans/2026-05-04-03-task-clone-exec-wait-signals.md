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

- `clone3` edge cases beyond basic creation
- parent and child lifecycle separation
- wait status encoding and parent notifications
- stop, continue, and default-action parity
- `SA_RESTART` and restart metadata
- ptrace stop and event lifecycle

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

### Task 5: Full Tranche Proof

**Files:**
- Reference: `docs/superpowers/plans/2026-05-04-00-kernel-completion-orchestration.md`

- [ ] Re-run lint, project generation, and the AGENTS-authoritative simulator `build-for-testing` flow.
- [ ] Run the focused task or wait or signal or ptrace simulator suites for tranche-local proof, then run the full shared-scheme simulator suite before any milestone-finished claim.
- [ ] Confirm LinuxKernel tests still avoid `internal/ios/**`.
- [ ] Commit and push only after the proof gate and branch-synchronization checks succeed.
