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
- Keep the `OrlixOS`-is-the-Kit decision consistent across ADRs, architecture docs, glossary, skills, agents, and AGENTS.md.
- Remove stale `OrlixKit` routing unless it is explicitly documented as retired/forbidden.
- Prefer Codex-native surfaces: `AGENTS.md`, `.codex/agents`, `.agents/skills`, `.codex/rules`, and `.codex/hooks`.
- Treat `rtk` as an output wrapper in rules and hooks. Guarded bare commands and `rtk`-wrapped equivalents must have the same policy.

## Verification

After harness edits, run a stale-reference scan, inspect skill/agent trigger descriptions for clear narrow scope, and verify paired bare/`rtk` command policy where rules are touched.
