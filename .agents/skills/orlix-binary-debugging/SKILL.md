---
name: orlix-binary-debugging
description: Use when Orlix diagnosis may require Hopper, LLDB, disassembly, symbol inspection, crash frames, or binary-level validation after source and logs are insufficient.
---

# Orlix Binary Debugging

Use binary tooling only after cheaper evidence is exhausted.

## Entry Criteria

- Source, logs, focused repros, and crash reports did not identify the cause.
- The exact binary, architecture, build configuration, and symbol state are known.
- The question cannot be answered from upstream source or generated build logs.

## Rules

- Inspect one binary at a time.
- Do not run parallel Hopper or binary-debugger sessions.
- Validate disassembly hypotheses against source, LLDB, logs, or a focused runtime repro before editing.
- Do not infer Linux semantics from Darwin frames. Route ownership through Orlix docs and upstream Linux behavior.
- Record what was inspected and what remains unverified.
