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
)

payload = parse_json(read_stdin_text())
text = flattened_text(payload)

if macos_runtime_wording(text):
    warn("macOS runtime/user wording detected. Initial Orlix runtime proof target is iOS Simulator unless explicitly verified otherwise.")
if vague_container_support(text):
    warn("Generic Docker/container support wording detected. Use OCI-derived Orlix environments and state non-goals unless Docker semantics are explicitly in scope.")
if invented_mechanism_without_scope(text):
    warn("Plan text appears to introduce a named tool, workflow root, or mechanism-specific proof target without marking it existing, user-requested, or optional.")
if workflow_without_authorization(text):
    warn("Subagent, swarm, or delegated workflow wording detected without explicit user authorization.")

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
