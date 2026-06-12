# Orlix Agent Harness

This directory describes how Codex agents should work in Orlix. Architecture truth lives in `docs/architecture`, `docs/adr`, and `docs/reference`; this directory stores harness memory and explains how agents use those sources.

## Native Codex Surfaces

- `AGENTS.md`: compact repo instructions and routing.
- `.codex/agents`: project agents for planner, implementer, and reviewer roles.
- `.agents/skills`: reusable Orlix workflows.
- `.codex/rules`: command approval policy.
- `.codex/hooks`: deterministic lifecycle checks.
- `docs/plans`: active and completed task plans.
- `docs/codex-handoffs`: durable handoffs for long sessions or compaction risk.
- `docs/harness/MEMORY.md`: curated durable lessons for agents.

## Plan Layout

Use `docs/plans/active/<task>/PLAN.md` and `docs/plans/active/<task>/IMPLEMENT.md` for non-trivial work. Move the task directory to `docs/plans/completed/<task>/` only after the reviewer confirms that claimed evidence matches the plan.

`PLAN.md` owns intent, constraints, milestones, verification gates, and scope boundaries. `IMPLEMENT.md` is append-only and records what happened, why, deviations, commands, evidence, blockers, and next actions.

## Plan Context Discipline

Start Orlix sessions by reading `AGENTS.md`, the active `GOAL.md`, the active
`PLAN.md`, and the latest active `IMPLEMENT.md` entry before implementing or
making status claims. Reconcile old remaining-work lists against current source,
tests, and commits before selecting a task. After each coherent checkpoint,
append to `IMPLEMENT.md` with exact evidence and the remaining work that still
matches the current repo.

The hooks may block mutations before active plan context is loaded. After a
mutation, commits and pushes may be blocked until the active implementation log
is updated.

## OCI Runtime Claims

OCI image import is not OCI Runtime compliance. OCI Runtime support requires a
pinned runtime-spec input, schema validation, config and Linux config handling,
runtime lifecycle proof for `state`, `create`, `start`, `kill`, and `delete`,
Linux runtime defaults such as fd and `/dev/fd` behavior, and truthful feature
reporting.

Feature reports must not overclaim support for cgroups, seccomp, AppArmor,
SELinux, netDevices, idmapped mounts, or user namespace mappings until Orlix has
focused proof for those features.

## Review Loop

Use the planner/implementer/reviewer split for long-horizon or high-risk work:

- Planner creates or updates the plan.
- Implementer executes the current milestone and appends to the implementation log.
- Reviewer challenges assumptions, checks Orlix directives, verifies upstream conformance, and audits evidence before claims.

Do not create extra reviewer personas unless a task genuinely needs parallel independent investigation.

## Handoffs

Use a handoff when a session is long, compaction is likely, work is paused, or another agent must resume. Durable Orlix handoffs belong under `docs/codex-handoffs/YYYY-MM-DD-topic.md`; ephemeral handoffs may use the operating system temporary directory.

A handoff should reference the active plan, implementation log, commits, diffs, and test logs instead of duplicating them. Include proof status, known failures, constraints, open decisions, next steps, suggested skills, and a reactivation prompt. Do not archive, delete, prune, or mutate Codex local state as part of an Orlix handoff.

## Dynamic Workflows

Use explicit workflow packets only when the task has independent tracks, high risk, reusable orchestration value, or user-requested delegation. If needed, place workflow state in `docs/plans/active/<task>/WORKFLOW.md`. Do not create a separate `.workflow/` root by default.

Subagents require available tooling and explicit user authorization. Without that, treat packets as isolated planning, implementation, or review passes inside the active plan.

## Patch Burden

Durable patches to upstream Linux, mlibc, packages, generated build inputs, or package feature/configure behavior require root-cause proof before implementation. The implementation log must show that the lower owning layers were checked first, including Linux/kernel syscall behavior, OrlixOS rootfs/package inputs, and package toolchain configuration. A patch is only acceptable when the evidence shows an upstream/target integration or libc/package semantic issue owned by that layer, not a hidden workaround for a lower-layer mismatch.

Durable patch stacks used by upstream conformance schemes must not modify upstream tests. If Orlix needs an extra regression, put it in an Orlix-owned test path and keep the upstream suite pristine so failure, skip, and total counts remain meaningful.
