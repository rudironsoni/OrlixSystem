# Orlix Harness Quality Pass Plan

## Task

Tighten the Codex harness across rules, hooks, agents, skills, plan templates, and docs so Orlix policy is enforced consistently.

## Context

The audit found that `rtk`-wrapped commands can bypass some exec policy rules, claim checks accept overly generic evidence, hook test coverage is narrow, and plan templates do not force Orlix owner/proof-lane decisions.

## Approach

- Treat `rtk` as a display wrapper: policy applies equally to bare commands and `rtk`-wrapped commands.
- Keep generated trees readable but not mutable.
- Make completion-claim warnings require task-relevant active evidence, not any generic evidence token.
- Tighten harness templates and agent instructions without duplicating architecture truth.

## Milestones

- [x] **M1: Exec policy parity** | verify: bare and `rtk` execpolicy checks agree
- [x] **M2: Hook coverage and claim checks** | verify: hook unit tests cover generated trees, output warnings, compact-plan warnings, and completion claims
- [x] **M3: Harness guidance tightened** | verify: inspect updated templates, agents, skills, and docs
- [ ] **Final: all required evidence collected** | verify: hook tests, execpolicy tests, JSON/TOML syntax, stale-reference scan, reviewer audit

## Scope Boundaries

In scope:

- `.codex/rules`, `.codex/hooks`, `.codex/agents`, `.agents/skills`, `docs/plans`, `docs/harness`, `AGENTS.md`, and README harness guidance.

Out of scope:

- Kernel, libc, OrlixOS runtime, package implementation, and product runtime proof changes.

## Directives

- Relevant docs: `AGENTS.md`, `docs/harness/README.md`, `docs/harness/MEMORY.md`
- Relevant ADRs: `docs/adr/0017-product-runtime-claim-promotion-order.md`, `docs/adr/0019-keep-make-targets-linux-shaped.md`, `docs/adr/0023-use-release-development-profiles-and-curated-orlixos-distribution.md`
- Relevant skills: `orlix-write-to-harness`, `orlix-runtime-claim-verification`

## Open Questions

- [ ] None.

## Risks

- Existing unrelated dirty worktree changes must remain untouched.
- Regex-based hooks can overblock reads or underblock mutation if not covered by tests.

## Notes

---

Created: 2026-06-07
