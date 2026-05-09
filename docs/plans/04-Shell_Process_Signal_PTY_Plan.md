# Shell Process, Signal, And PTY Plan

## Status

This milestone depends on the split plan and inherits its stricter condition:

- Linux semantics must remain Linux-owner
- structural target separation alone is not enough if host realization still diverges from Linux-visible shell behavior under simulator proof

Current status:

- active milestone
- depends on the now-delivered Milestone 2 exec baseline

Current delivered learning:

- the public shell-facing foreground-process-group surface is now covered, not just the PTY ioctl seam
- `tcgetpgrp()` and `tcsetpgrp()` now ride the kernel-owned PTY job-control machinery and are simulator-proven against Linux-shaped `SIGTTOU` behavior
- public `killpg()` now lives in `OrlixKernel`, not `OrlixHostAdapter`, and is simulator-proven against process-group signal delivery and `ESRCH` failure behavior
- public `tcgetsid()` and `TIOCGSID` now ride the kernel-owned PTY/session model and are simulator-proven against controlling-session identity and `ENOTTY` failure behavior
- public `isatty()` now rides the kernel-owned PTY/ioctl model and is simulator-proven against PTY truth, `ENOTTY`, and `EBADF` behavior
- public `wait4()` and `waitid()` now ride the kernel-owned wait model and are simulator-proven against exited-child status reporting and `WNOWAIT` preservation behavior

## Purpose

Make `zsh` viable as the primary proof target by hardening the task, wait, signal, session, process-group, and PTY model.

## Goals

1. Provide Linux-shaped shell-critical process semantics.
2. Redesign signal ownership where needed to match Linux behavior.
3. Make interactive PTY and job control behavior reliable.
4. Keep host realization private and prevent shell semantics from being encoded through branded host-adapter contracts.
5. Use kernel-owned private contracts wherever shell-critical subsystems need host realization hooks.
6. Reject host primitives that are structurally private but semantically wrong for Linux shell behavior.

## Required Subsystems

- task and process model
- fork, vfork, clone
- exec interactions with thread groups
- waitpid, waitid, wait4
- sessions and process groups
- controlling tty model
- PTY master/slave model
- signal disposition, mask, pending, delivery, and default actions

## Deliverables

1. Correct task lifecycle semantics for shell workflows.
2. Correct wait and zombie semantics for shell workflows.
3. Signal subsystem shaped around shared dispositions and per-task mask and pending state.
4. PTY and job-control correctness for foreground and background shell behavior.

## Guardrails

### Do

- Use `zsh` behavior as the real compatibility target.
- Treat process groups, sessions, and PTY rules as must-have shell semantics.
- Preserve Linux signal reset and pending semantics across exec.
- Keep Darwin and host realization mechanics behind kernel-owned private contracts, not as the kernel-facing shell model.

### Don't

- Do not leave signal state as a monolithic shallow structure if it diverges from Linux behavior.
- Do not rely on Darwin signal or wait behavior as proof.
- Do not present simulated `vfork` semantics as complete if they still diverge in shell-relevant ways.
- Do not call this milestone complete if shell-critical behavior still relies on a host-private shortcut that has not been proven Linux-shaped on the simulator.

## Proof

1. `zsh -c` command execution works.
2. `sleep 1 & wait` works.
3. Pipelines and redirections work.
4. Foreground and background PTY job control scenarios work.
5. Stop and continue behavior is wait-visible and shell-correct.
6. The milestone closeout does not preserve branded host-adapter vocabulary as the accepted kernel-facing shell seam.
