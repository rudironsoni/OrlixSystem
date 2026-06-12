---
name: orlix-dynamic-workflows
description: Use when Orlix work may benefit from explicit workflow packets, subagent orchestration, or independent research, implementation, test, and review tracks.
---

# Orlix Dynamic Workflows

Use orchestration only when it reduces risk or drift.

## Trigger

Use this skill when at least two are true:

- The work has independent research, code, test, or review tracks.
- The work is high risk or crosses OrlixKernel, OrlixMLibC, OrlixOS, OrlixHostAdapter, tests, or harness.
- Separate verification would catch wrong-layer or proof drift.
- The workflow will be reused.
- The user explicitly asks for orchestration, swarm, subagents, or dynamic workflow.

## Orlix Placement

- Use `docs/plans/active/<task>/WORKFLOW.md` only when an explicit workflow artifact is needed.
- Do not create a new `.workflow/` root by default.
- Do not spawn subagents unless the current environment exposes subagent tooling and the user authorized delegated or parallel agent work.
- If no subagent runner is available, treat workflow packets as isolated planning or review passes inside the active plan.

## Packet Template

Each packet must include:

- Packet ID.
- Objective.
- Required context and files.
- Owning Orlix layer.
- Do.
- Do not.
- Expected output.
- Verification.
- Integration point.

## Rules

- Define success criteria before splitting work.
- Keep packets disjoint enough that one packet cannot silently undo another.
- Integrate results explicitly in the active plan or implementation log.
- Verify the integrated result against the original user goal, not only packet-local success.
