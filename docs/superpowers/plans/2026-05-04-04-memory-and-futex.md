# Memory And Futex Implementation Plan

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

**Goal:** Complete the remaining VM and futex semantics needed for Linux-oriented libc and package progress beyond shell-base.

**Architecture:** Keep address-space, mapping, and fault semantics in the MM owner and grow futex coverage only through Linux-visible operations that userspace depends on. The tranche pairs VM contracts with procfs reporting checks so the kernel’s memory accounting, signal faults, and futex wait queues stay coherent from both syscall and `/proc` viewpoints.

**Tech Stack:** `kernel/mm.c`, `kernel/mm.h`, `kernel/futex.c`, `kernel/wait_queue.c`, procfs VM-reporting code, LinuxKernel MM, futex, and proc contracts.

---

## Tranche Scope

- file-backed mapping resize and replace paths
- COW and writeback edge cases
- guard-page and fault-signal fidelity
- `brk` and VMA accounting parity
- futex requeue, compare-requeue, and bitset paths
- proc `maps`, `smaps`, and `mincore` coherence

## Futex Boundary Rule

- Futex Linux-facing semantics live in Linux-owner sync code. If host primitives are required for waiting, quarantine host mechanics behind a narrow `internal/ios/**` seam; do not make pthread/Darwin behavior the semantic baseline.

### Task 1: Close The Remaining MM `ENOSYS` Paths

**Files:**
- Modify: `kernel/mm.c`
- Modify: `kernel/mm.h`
- Test: `IXLandSystemLinuxKernelTests/NativeSyscallContract.c`
- Test: `IXLandSystemLinuxKernelTests/NativeSyscallTests.m`

- [ ] Add failing syscall coverage for the known file-backed mapping resize or replace paths still returning `-ENOSYS`.
- [ ] Add assertions for `brk` growth and shrink accounting, and mapping replacement behavior across reopen or truncate scenarios.
- [ ] Run the focused VM tests and capture the current failure output.
- [ ] Implement the minimal MM-owner changes needed to close the targeted gaps.
- [ ] Re-run the focused VM suite until the new paths pass without breaking existing mapping behavior.

### Task 2: Tighten Fault And Signal Fidelity

**Files:**
- Modify: `kernel/mm.c`
- Modify: `kernel/signal.c`
- Test: `IXLandSystemLinuxKernelTests/SignalSyscallContract.c`
- Test: `IXLandSystemLinuxKernelTests/SignalTests.m`

- [ ] Add failing coverage for guard-page behavior, `SIGSEGV` versus `SIGBUS` selection, and task-visible fault metadata where the kernel currently blurs outcomes.
- [ ] Run the signal suite to verify the failing fault path before implementation.
- [ ] Implement the minimal MM and signal integration needed to preserve Linux-visible fault semantics.
- [ ] Re-run the signal suite until the new fault cases and existing signal behavior pass together.

### Task 3: Expand Futex Operations

**Files:**
- Modify: `kernel/futex.c`
- Modify: `kernel/futex.h`
- Modify: `runtime/syscall.c`
- Test: `IXLandSystemLinuxKernelTests/FutexContract.c`
- Test: `IXLandSystemLinuxKernelTests/FutexTests.m`

- [ ] Add failing futex coverage for requeue, compare-requeue, and bitset operations needed by libc progress, plus restartable wait regressions.
- [ ] Run the futex suite and capture the unsupported operation failures.
- [ ] Implement the new futex operations in the existing wait-queue model rather than introducing host-thread shortcuts.
- [ ] Re-run the futex suite until all added operations pass and prior wait or wake behavior remains green.

### Task 4: Keep Proc VM Reporting Coherent

**Files:**
- Modify: procfs VM-reporting source under `fs/`
- Modify: `kernel/mm.c`
- Test: `IXLandSystemLinuxKernelTests/ProcfsNamespaceContract.c`
- Test: `IXLandSystemLinuxKernelTests/ProcfsNamespaceTests.m`

- [ ] Add failing coverage for `maps`, `smaps`, or `mincore` views across remap, truncate, reopen, child creation, and task-targeted proc reads.
- [ ] Implement proc reporting updates from MM-owned state so proc output tracks the kernel model exactly.
- [ ] Re-run procfs VM-reporting tests and confirm there is no host-side accounting leakage.

### Task 5: Full Tranche Proof

**Files:**
- Reference: `docs/superpowers/plans/2026-05-04-00-kernel-completion-orchestration.md`

- [ ] Re-run lint, project generation, and the AGENTS-authoritative simulator `build-for-testing` flow.
- [ ] Run the focused VM or futex or proc simulator suites for tranche-local proof, then run the full shared-scheme simulator suite before any milestone-finished claim.
- [ ] Update `docs/syscall_gap_matrix_6.12_arm64.md` explicitly if the futex or VM syscall surface changed (there is no generator today).
- [ ] Commit and push only after the proof gate passes and branch synchronization is verified.
