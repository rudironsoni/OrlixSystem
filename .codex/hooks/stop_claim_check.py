#!/usr/bin/env python3
from orlix_hook_common import (
    CLAIM_RE,
    active_plan_dirs,
    claim_evidence_terms,
    flattened_text,
    has_claim_relevant_evidence,
    has_implementation_evidence,
    invented_mechanism_without_scope,
    macos_runtime_wording,
    parse_json,
    read_stdin_text,
    repo_root,
    vague_container_support,
    warn,
    workflow_without_authorization,
    oci_runtime_claim_without_lifecycle,
    orlix_run_claim_without_lifecycle,
    oci_compatible_from_image_only,
    stale_remaining_task_list,
    load_plan_context_state,
    plan_context_loaded,
)

payload = parse_json(read_stdin_text())
text = flattened_text(payload)
root = repo_root()
state = load_plan_context_state(root)

if active_plan_dirs(root):
    if not plan_context_loaded(root, state):
        warn("Final status without loaded active plan context. Read AGENTS.md plus active GOAL.md, PLAN.md, and IMPLEMENT.md.")
    mutation_time = float(state.get("mutation_time", 0.0) or 0.0)
    implement_update_time = float(state.get("implement_update_time", 0.0) or 0.0)
    if mutation_time and implement_update_time < mutation_time:
        warn("Mutation occurred after the latest active IMPLEMENT.md update.")

if macos_runtime_wording(text):
    warn("macOS runtime/user wording detected. Initial Orlix runtime proof target is iOS Simulator unless explicitly verified otherwise.")
if vague_container_support(text):
    warn("Generic Docker/container support wording detected. Use OCI-derived Orlix environments and state non-goals unless Docker semantics are explicitly in scope.")
if invented_mechanism_without_scope(text):
    warn("Plan text appears to introduce a named tool, workflow root, or mechanism-specific proof target without marking it existing, user-requested, or optional.")
if workflow_without_authorization(text):
    warn("Subagent, swarm, or delegated workflow wording detected without explicit user authorization.")
if oci_runtime_claim_without_lifecycle(text):
    warn("OCI Runtime support claim detected without create/start/state/kill/delete lifecycle evidence.")
if orlix_run_claim_without_lifecycle(text):
    warn("`orlix run` completion claim detected without create/start/state/kill/delete lifecycle evidence.")
if oci_compatible_from_image_only(text):
    warn("OCI compatibility claim appears based only on image/import proof. OCI Runtime compliance needs config, lifecycle, Linux defaults, and feature evidence.")
if stale_remaining_task_list(text):
    warn("Stale remaining-task list wording detected. Reconcile active plan state against source, tests, commits, and IMPLEMENT.md.")

if not CLAIM_RE.search(text):
    raise SystemExit(0)

root = repo_root()
plans = active_plan_dirs(root)
if not plans:
    warn("Completion-like claim detected, but no active plan directory exists under docs/plans/active.")
    raise SystemExit(0)

if not any(has_implementation_evidence(plan) for plan in plans):
    warn("Completion-like claim detected, but active IMPLEMENT.md files do not contain command/log evidence.")
    raise SystemExit(0)

terms = claim_evidence_terms(text)
if terms and not any(has_claim_relevant_evidence(plan, terms) for plan in plans):
    warn("Completion-like claim detected, but active IMPLEMENT.md files do not contain claim-relevant evidence.")
