# Orlix Agent Harness

This directory describes how Codex agents should work in Orlix. Architecture truth lives in `docs/architecture`, `docs/adr`, and `docs/reference`; this directory stores harness memory and explains how agents use those sources.

## Native Codex Surfaces

- `AGENTS.md`: compact repo instructions and routing.
- `.codex/agents`: project agents for planner, implementer, and reviewer roles.
- `.agents/skills`: reusable Orlix workflows.
- `.codex/rules`: command approval policy.
- `.codex/hooks`: deterministic lifecycle checks.
- `docs/plans`: active and completed task plans.
- `docs/harness/MEMORY.md`: curated durable lessons for agents.

## Plan Layout

Use `docs/plans/active/<task>/PLAN.md` and `docs/plans/active/<task>/IMPLEMENT.md` for non-trivial work. Move the task directory to `docs/plans/completed/<task>/` only after the reviewer confirms that claimed evidence matches the plan.

`PLAN.md` owns intent, constraints, milestones, verification gates, and scope boundaries. `IMPLEMENT.md` is append-only and records what happened, why, deviations, commands, evidence, blockers, and next actions.

## Review Loop

Use the planner/implementer/reviewer split for long-horizon or high-risk work:

- Planner creates or updates the plan.
- Implementer executes the current milestone and appends to the implementation log.
- Reviewer challenges assumptions, checks Orlix directives, verifies upstream conformance, and audits evidence before claims.

Do not create extra reviewer personas unless a task genuinely needs parallel independent investigation.

## Patch Burden

Durable patches to upstream Linux, mlibc, packages, generated build inputs, or package feature/configure behavior require root-cause proof before implementation. The implementation log must show that the lower owning layers were checked first, including Linux/kernel syscall behavior, OrlixOS rootfs/package inputs, and package toolchain configuration. A patch is only acceptable when the evidence shows an upstream/target integration or libc/package semantic issue owned by that layer, not a hidden workaround for a lower-layer mismatch.

Durable patch stacks used by upstream conformance schemes must not modify upstream tests. If Orlix needs an extra regression, put it in an Orlix-owned test path and keep the upstream suite pristine so failure, skip, and total counts remain meaningful.
