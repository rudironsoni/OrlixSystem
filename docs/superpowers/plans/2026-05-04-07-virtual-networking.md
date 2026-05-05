# Virtual Networking Implementation Plan

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

**Goal:** Introduce Linux-owned virtual networking objects and syscall coverage so local userspace can rely on loopback, Unix-socket-style communication, and pollable socket semantics.

**Architecture:** Build sockets as kernel-owned virtual objects under `kernel/net/` and integrate them with fd and readiness infrastructure before considering any narrow outbound host bridge. The milestone deliberately prioritizes local userspace compatibility, socket readiness, and Linux-visible addressing rules over host-transparent networking, with any unavoidable host mediation isolated under a subsystem-specific `internal/ios/**` seam.

**Tech Stack:** `kernel/net/network.c`, new `kernel/net/*` owners as needed, `runtime/syscall.c`, `fs/fdtable.c`, readiness integration under `fs/`, LinuxKernel syscall, readiness, and procfd tests.

---

## Tranche Scope

- virtual socket object model
- loopback and local addressing rules
- `socketpair` and Unix-domain-like semantics
- readiness and epoll integration
- `bind`, `listen`, `accept`, and `connect`
- datagram and stream send or receive paths
- netlink-style kernel or userspace messaging

### Task 1: Establish The Socket Object Model

**Files:**
- Modify: `kernel/net/network.c`
- Modify: `kernel/net/socket.c`
- Modify: `kernel/net/socket.h`
- Modify: `fs/fdtable.c`
- Modify: `runtime/syscall.c`
- Test: `IXLandSystemLinuxKernelTests/NativeSyscallContract.c`
- Test: `IXLandSystemLinuxKernelTests/NativeSyscallTests.m`

- [ ] Add failing syscall coverage for `socket` and `socketpair` classification, fd ownership, and close semantics.
- [ ] Run the focused syscall suite and confirm the current future-backend or missing behavior.
- [ ] Implement the minimal virtual socket object model and fd registration needed for local kernel ownership.
- [ ] Re-run syscall tests until basic socket allocation and teardown are green.

Notes:
- Local Unix-domain-like and loopback semantics are IXLand-owned. Any outbound host networking (if ever required) must be a later, explicit, narrow `internal/ios/**` seam and must not become the baseline.

### Task 2: Add Local Communication And Readiness

**Files:**
- Modify: `kernel/net/socket.c`
- Modify: `fs/poll.c`
- Modify: `fs/eventpoll.c`
- Test: `IXLandSystemLinuxKernelTests/ReadinessContract.c`
- Test: `IXLandSystemLinuxKernelTests/ReadinessTests.m`
- Test: `IXLandSystemLinuxKernelTests/EpollContract.c`
- Test: `IXLandSystemLinuxKernelTests/EpollTests.m`

- [ ] Add failing coverage for `socketpair` data transfer, readiness transitions, hangup, and epoll registration.
- [ ] Run readiness and epoll suites to capture current socket integration gaps.
- [ ] Implement the minimal readiness hooks and local data-path state needed for pollable sockets.
- [ ] Re-run readiness and epoll suites until socket-backed readiness is consistent with pipes and PTYs.

### Task 3: Implement Bind, Listen, Accept, And Connect

**Files:**
- Modify: `kernel/net/socket.c`
- Modify: `runtime/syscall.c`
- Test: `IXLandSystemLinuxKernelTests/NativeSyscallContract.c`
- Test: `IXLandSystemLinuxKernelTests/NativeSyscallTests.m`

- [ ] Add failing coverage for address binding, passive open, accept queues, active connect, and Linux-visible address reporting.
- [ ] Run focused syscall tests and capture the exact missing transitions.
- [ ] Implement the minimal local loopback and Unix-domain-like connection semantics needed for the milestone.
- [ ] Re-run the focused socket suite until connection lifecycle behavior passes.

### Task 4: Complete Send Or Receive Paths And Kernel Messaging

**Files:**
- Modify: `kernel/net/socket.c`
- Modify: `kernel/net/network.c`
- Modify: `runtime/syscall.c`
- Test: `IXLandSystemLinuxKernelTests/NativeSyscallContract.c`
- Test: `IXLandSystemLinuxKernelTests/NativeSyscallTests.m`
- Test: `IXLandSystemLinuxKernelTests/ProcfsNamespaceContract.c`

- [ ] Add failing coverage for datagram and stream send or receive syscalls, socket fd visibility, and any netlink-style kernel or userspace messaging the milestone claims to own.
- [ ] Run the focused networking tests and confirm remaining gaps before implementation.
- [ ] Implement the minimal data-path and proc-reporting changes needed for the claimed socket surface.
- [ ] Re-run the focused suite until send or receive paths and proc visibility remain green.

### Task 5: Boundary Audit And Full Tranche Proof

**Files:**
- Reference: `docs/superpowers/plans/2026-05-04-00-kernel-completion-orchestration.md`

- [ ] Audit networking files for accidental host vocabulary leakage; if outbound bridging is truly required, isolate it under a new subsystem-specific `internal/ios/**` seam and add HostBridge proof for that seam only.
- [ ] Update `docs/syscall_gap_matrix_6.12_arm64.md` explicitly so socket-related entries move from `future backend:*` or `missing:*` to accurate final classifications (there is no generator today).
- [ ] Re-run lint, project generation, and the AGENTS-authoritative simulator `build-for-testing` flow.
- [ ] Run the focused networking or readiness simulator suites for tranche-local proof, then run the full shared-scheme simulator suite before any milestone-finished claim.
- [ ] Commit and push only after the proof gate passes and branch synchronization is verified.
