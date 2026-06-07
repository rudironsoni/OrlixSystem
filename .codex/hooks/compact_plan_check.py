#!/usr/bin/env python3
from orlix_hook_common import active_plan_dirs, has_implementation_evidence, repo_root, warn

root = repo_root()
for plan_dir in active_plan_dirs(root):
    if not (plan_dir / "PLAN.md").exists():
        warn(f"{plan_dir.relative_to(root)} is missing PLAN.md")
    if not (plan_dir / "IMPLEMENT.md").exists():
        warn(f"{plan_dir.relative_to(root)} is missing IMPLEMENT.md")
    elif not has_implementation_evidence(plan_dir):
        warn(f"{plan_dir.relative_to(root)}/IMPLEMENT.md has no recorded evidence yet")
