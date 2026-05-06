# Shell Process, Signal, And PTY Plan

## Purpose

Make `zsh` viable as the primary proof target by hardening the task, wait, signal, session, process-group, and PTY model.

## Goals

1. Provide Linux-shaped shell-critical process semantics.
2. Redesign signal ownership where needed to match Linux behavior.
3. Make interactive PTY and job control behavior reliable.

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

### Don't

- Do not leave signal state as a monolithic shallow structure if it diverges from Linux behavior.
- Do not rely on Darwin signal or wait behavior as proof.
- Do not present simulated `vfork` semantics as complete if they still diverge in shell-relevant ways.

## Proof

1. `zsh -c` command execution works.
2. `sleep 1 & wait` works.
3. Pipelines and redirections work.
4. Foreground and background PTY job control scenarios work.
5. Stop and continue behavior is wait-visible and shell-correct.
