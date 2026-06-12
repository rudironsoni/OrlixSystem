#!/usr/bin/env python3
from orlix_hook_common import (
    active_plan_dirs,
    block,
    flattened_text,
    generated_tree_write_violation,
    is_git_commit_or_push,
    load_plan_context_state,
    parse_json,
    plan_context_loaded,
    read_stdin_text,
    repo_root,
    required_plan_context_paths,
    tool_requires_plan_context,
    tool_mutates_workspace,
)

payload = parse_json(read_stdin_text())
text = flattened_text(payload)
root = repo_root()
state = load_plan_context_state(root)

if active_plan_dirs(root):
    if not plan_context_loaded(root, state) and tool_requires_plan_context(payload):
        required = ", ".join(str(path.relative_to(root)) for path in required_plan_context_paths(root))
        block(f"Active plan context must be read before mutation: {required}.")
    if is_git_commit_or_push(payload):
        mutation_time = float(state.get("mutation_time", 0.0) or 0.0)
        implement_update_time = float(state.get("implement_update_time", 0.0) or 0.0)
        if mutation_time and implement_update_time < mutation_time:
            block("Active IMPLEMENT.md must be updated after mutation before git commit or push.")

if generated_tree_write_violation(payload):
    block("Generated upstream/build trees are read-only for agents. Move the fix to the owning Orlix layer.")

if "RUN_VERY_EXPENSIVE_TESTS=no" in text or "RUN_EXPENSIVE_TESTS=no" in text:
    block("Do not disable upstream expensive tests when upstream conformance is the claim.")
