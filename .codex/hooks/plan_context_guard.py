#!/usr/bin/env python3
from orlix_hook_common import (
    active_plan_dirs,
    hook_tool_name,
    is_git_commit_or_push,
    load_plan_context_state,
    parse_json,
    plan_context_loaded,
    plan_context_post_update,
    read_stdin_text,
    repo_root,
    required_plan_context_paths,
    tool_requires_plan_context,
    tool_mutates_workspace,
    warn,
    block,
)


payload = parse_json(read_stdin_text())
event = payload.get("hook_event_name", "")
root = repo_root()

if not active_plan_dirs(root):
    raise SystemExit(0)

state = load_plan_context_state(root)

if event == "SessionStart":
    warn("Read AGENTS.md and active GOAL.md, PLAN.md, and latest IMPLEMENT.md before implementation or status claims.")
    raise SystemExit(0)

if event == "PreToolUse":
    if not plan_context_loaded(root, state) and tool_requires_plan_context(payload):
        required = ", ".join(str(path.relative_to(root)) for path in required_plan_context_paths(root))
        block(f"Active plan context must be read before mutation: {required}.")
    if is_git_commit_or_push(payload):
        mutation_time = float(state.get("mutation_time", 0.0) or 0.0)
        implement_update_time = float(state.get("implement_update_time", 0.0) or 0.0)
        if mutation_time and implement_update_time < mutation_time:
            block("Active IMPLEMENT.md must be updated after mutation before git commit or push.")
    raise SystemExit(0)

if event == "PostToolUse":
    plan_context_post_update(payload)
    raise SystemExit(0)

if event == "Stop":
    if not plan_context_loaded(root, state):
        warn("Final status without loaded active plan context. Read AGENTS.md plus active GOAL.md, PLAN.md, and IMPLEMENT.md.")
    mutation_time = float(state.get("mutation_time", 0.0) or 0.0)
    implement_update_time = float(state.get("implement_update_time", 0.0) or 0.0)
    if mutation_time and implement_update_time < mutation_time:
        warn("Mutation occurred after the latest active IMPLEMENT.md update.")
    raise SystemExit(0)

if hook_tool_name(payload):
    raise SystemExit(0)
