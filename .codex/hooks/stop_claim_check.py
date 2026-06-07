#!/usr/bin/env python3
from orlix_hook_common import (
    CLAIM_RE,
    active_plan_dirs,
    claim_evidence_terms,
    flattened_text,
    has_claim_relevant_evidence,
    has_implementation_evidence,
    parse_json,
    read_stdin_text,
    repo_root,
    warn,
)

payload = parse_json(read_stdin_text())
text = flattened_text(payload)

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
