# Readiness And Terminal Runtime Implementation Plan

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

**Goal:** Harden readiness and terminal semantics until interactive shells and job-control userspace can rely on consistent Linux-shaped behavior rather than narrow ioctl-only success.

**Architecture:** Treat readiness as one shared kernel service across fd classes and treat terminal control as an interaction between PTY state, sessions, process groups, and signal delivery. The tranche first closes cross-fd polling inconsistencies, then completes controlling-tty and job-control semantics so PTYs behave like Linux terminals under shell workloads.

**Tech Stack:** `fs/poll.c`, `fs/eventpoll.c`, `fs/pipe.c`, `fs/pty.c`, `fs/poll.h`, `fs/eventpoll.h`, `kernel/wait.c`, `kernel/signal.c`, LinuxKernel readiness and PTY tests.

---

## Tranche Scope

- `poll`, `select`, `pselect6`, `ppoll`, and `epoll` consistency
- readiness across pipes, PTYs, proc/dev nodes, timerfd, eventfd, pidfd, and future sockets
- controlling-tty and foreground-process-group semantics
- `TIOCSPGRP`, `TIOCGPGRP`, `TIOCNOTTY`, hangup, and restart interaction

## Readiness Invariants (Required)

- Readiness must be one shared Linux-owned service across fd classes (pipes, PTYs, proc/dev nodes, timerfd/eventfd/pidfd, and sockets).
- Virtual sockets exist; socketpair semantics are first-class readiness targets (poll/epoll must treat them like Linux, not host poll).
- Socket fds are full-duplex and must be accepted by generic read/write dispatch (fd access mode must not cause EBADF for valid sockets).

### Task 1: Normalize Readiness Across Fd Classes

**Files:**
- Modify: `fs/poll.c`
- Modify: `fs/eventpoll.c`
- Modify: `fs/pipe.c`
- Modify: `fs/fdtable.c`
- Test: `IXLandSystemLinuxKernelTests/ReadinessContract.c`
- Test: `IXLandSystemLinuxKernelTests/ReadinessTests.m`
- Test: `IXLandSystemLinuxKernelTests/EpollContract.c`
- Test: `IXLandSystemLinuxKernelTests/EpollTests.m`

- [ ] Add failing coverage for readiness consistency across pipes, PTYs, proc or dev nodes, timerfd, eventfd, and pidfds.
- [ ] Run readiness and epoll suites to capture current cross-fd divergence.
- [ ] Implement the minimal readiness-engine fixes so level and edge semantics align across existing fd types.
- [ ] Re-run readiness and epoll suites until the cross-fd contracts pass together.

### Task 2: Complete Controlling-TTY And Foreground-Group Semantics

**Files:**
- Modify: `fs/pty.c`
- Modify: `kernel/task.c`
- Modify: `kernel/signal.c`
- Test: `IXLandSystemLinuxKernelTests/PTYSessionContract.c`
- Test: `IXLandSystemLinuxKernelTests/PTYSessionTests.m`
- Test: `IXLandSystemLinuxKernelTests/PTYJobControlContract.c`
- Test: `IXLandSystemLinuxKernelTests/PTYJobControlTests.m`

- [ ] Add failing coverage for controlling-tty acquisition and release, foreground group changes, and session visibility.
- [ ] Add failing ioctl coverage for `TIOCSPGRP`, `TIOCGPGRP`, and `TIOCNOTTY`.
- [ ] Run the PTY session and PTY job-control suites to capture the current mismatches.
- [ ] Implement the minimal PTY and task-owner changes needed for Linux-shaped controlling-terminal state.
- [ ] Re-run the PTY suites until session and foreground-group contracts remain green.

### Task 3: Deliver Job-Control Signals And Hangup Correctly

**Files:**
- Modify: `fs/pty.c`
- Modify: `kernel/wait.c`
- Modify: `kernel/signal.c`
- Test: `IXLandSystemLinuxKernelTests/WaitJobControlContract.c`
- Test: `IXLandSystemLinuxKernelTests/WaitJobControlTests.m`
- Test: `IXLandSystemLinuxKernelTests/PTYJobControlTests.m`

- [ ] Add failing coverage for background read or write signal delivery, hangup policy, and restart interaction after stop or continue.
- [ ] Run the wait and PTY job-control suites to confirm the failing behavior before implementation.
- [ ] Implement the minimal signal and PTY integration needed to match Linux job-control expectations.
- [ ] Re-run the wait and PTY suites until stop, continue, and hangup behavior pass together.

### Task 4: Keep The Readiness Engine Future-Socket Ready

**Files:**
- Modify: `fs/poll.h`
- Modify: `fs/eventpoll.h`
- Modify: readiness-related kernel interfaces in `fs/`
- Test: `IXLandSystemLinuxKernelTests/ReadinessContract.c`
- Test: `IXLandSystemLinuxKernelTests/EpollContract.c`

- [ ] Audit readiness interfaces for assumptions that only pipes or PTYs can register waiters.
- [ ] Tighten internal readiness hooks so the later virtual networking milestone can adopt them without leaking host vocabulary into the API shape.
- [ ] Re-run readiness and epoll tests to ensure interface cleanup did not change visible semantics.

### Task 5: Full Tranche Proof

**Files:**
- Reference: `docs/superpowers/plans/2026-05-04-00-kernel-completion-orchestration.md`

- [ ] Re-run lint, project generation, and the AGENTS-authoritative simulator `build-for-testing` flow.
- [ ] Run the focused readiness or PTY or wait simulator suites for tranche-local proof, then run the full shared-scheme simulator suite before any milestone-finished claim.
- [ ] Commit and push only after proof passes and branch synchronization is verified.
