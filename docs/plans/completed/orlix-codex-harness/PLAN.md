# Orlix Codex Harness Plan

## Task

Implement a Codex-native harness for Orlix using repo instructions, Codex agents, skills, rules, hooks, plan templates, and durable harness memory.

## Context

The old root `AGENTS.md` was a long architecture document. The repo also contained unrelated simulator-control local skills that did not match Orlix. The user requested a harness that uses Codex-native surfaces, avoids fake status dashboards and hidden internal doc buckets, uses `docs/plans`, and keeps upstream conformance and runtime-claim behavior operational through skills/reviewer agents rather than standalone verification docs.

## Approach

- Keep architecture truth in `docs/architecture`, `docs/reference`, and `docs/adr`.
- Make `AGENTS.md` a compact router.
- Add planner, implementer, and reviewer custom agents.
- Add Orlix-specific skills for boundaries, upstream conformance, claim verification, and harness writes.
- Add mechanical Codex rules and hooks.
- Retire unrelated local skill surfaces.

## Milestones

- [x] **Move canonical docs** | verify: stale-reference scan
- [x] **Replace root instructions** | verify: inspect `AGENTS.md`
- [x] **Add plan and harness docs** | verify: inspect `docs/harness` and `docs/plans/templates`
- [x] **Add skills and agents** | verify: inspect `.agents/skills` and `.codex/agents`
- [x] **Add rules and hooks** | verify: JSON/TOML checks, rule checks, hook smoke tests
- [x] **Remove stale unrelated skill surface** | verify: `.agents` file inventory

## Scope Boundaries

In scope:

- Harness docs, plans, Codex agents, Codex rules/hooks, local skills, stale references.

Out of scope:

- Coreutils implementation.
- Kernel/libc/OrlixOS behavior changes.
- Runtime proof claims.

## Directives

- Relevant docs: `AGENTS.md`, `docs/harness/README.md`, `docs/harness/MEMORY.md`
- Relevant ADRs: `docs/adr/0017-product-runtime-claim-promotion-order.md`, `docs/adr/0019-keep-make-targets-linux-shaped.md`
- Relevant skills: `orlix-write-to-harness`, `orlix-runtime-claim-verification`

## Open Questions

- [ ] None.

## Risks

- Project-local hooks must be reviewed and trusted through Codex before they run automatically.
- Existing unrelated dirty worktree changes remain outside this harness checkpoint.

---

Created: 2026-06-07
