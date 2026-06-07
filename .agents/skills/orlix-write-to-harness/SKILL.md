---
name: orlix-write-to-harness
description: Use before editing Orlix harness files such as AGENTS.md, docs/plans, docs/harness, ADR indexes, .agents/skills, .codex/agents, Codex rules, hooks, or harness-related repo guidance. Ensures changes preserve the repo as the source of truth, avoid duplicate architecture claims, and keep references current.
---

# Orlix Write To Harness

Use this skill before writing Codex harness files.

## Rules

- Keep `AGENTS.md` concise and route to canonical docs.
- Keep architecture truth in `docs/architecture`, `docs/adr`, and `docs/reference`.
- Keep task state in `docs/plans/active/<task>/`.
- Keep durable agent lessons in `docs/harness/MEMORY.md`.
- Avoid duplicate or competing architecture claims.
- Update stale references after moving docs or renaming skills/agents.
- Prefer Codex-native surfaces: `AGENTS.md`, `.codex/agents`, `.agents/skills`, `.codex/rules`, and `.codex/hooks`.

## Verification

After harness edits, run a stale-reference scan and inspect skill/agent trigger descriptions for clear, narrow scope.
