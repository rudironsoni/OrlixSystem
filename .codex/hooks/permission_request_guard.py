#!/usr/bin/env python3
from orlix_hook_common import block, flattened_text, generated_tree_write_violation, parse_json, read_stdin_text, warn

payload = parse_json(read_stdin_text())
text = flattened_text(payload)

if generated_tree_write_violation(payload):
    block("Do not request permission to mutate generated upstream/build trees.")

if 'prefix_rule": ["python3"]' in text or "prefix_rule = [\"python3\"]" in text:
    block("Do not request broad Python escalation rules. Request a narrow command prefix.")

if "rm -rf" in text:
    warn("Destructive deletion requires explicit task scope and should be as narrow as possible.")
