---
name: orlix-session-lessons
description: Use when deriving lessons from an Orlix conversation, correcting agent behavior, improving harness reliability, or turning a derailed session into durable repo-local guidance.
---

# Orlix Session Lessons

Use this skill before changing Orlix harness behavior from a conversation history.

## Required Grounding

- Read the current conversation context that is available in the session.
- Read `AGENTS.md`, `docs/harness/README.md`, `docs/harness/MEMORY.md`, and relevant active plans under `docs/plans/active/`.
- Read the latest relevant `IMPLEMENT.md` entries and recent commits before claiming a lesson is current.
- If the lesson concerns architecture, also read the owning architecture, ADR, glossary, and source files.
- If a fact cannot be verified from the repo or transcript, label it as unverified.

## Output Shape

For each proposed harness lesson, provide:

- Session evidence: what actually happened in the conversation or implementation log.
- Failure mode: the agent behavior that must change.
- Harness surface: the file, skill, agent, hook, or memory entry that should change.
- Mechanical check: the hook, test, scan, or review question that can enforce it.

## Rules

- Do not turn one-off frustration into broad policy unless the repo evidence supports it.
- Do not duplicate architecture truth from canonical docs into harness memory.
- Keep durable lessons stable and short in `docs/harness/MEMORY.md`.
- Keep task status in active plans and implementation logs, not memory.
- Prefer mechanical warnings for repeated failures such as stale status, wrong target wording, invented runtime mechanisms, or unsupported completion claims.
