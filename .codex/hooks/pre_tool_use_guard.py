#!/usr/bin/env python3
from orlix_hook_common import block, flattened_text, generated_tree_write_violation, parse_json, read_stdin_text

payload = parse_json(read_stdin_text())
text = flattened_text(payload)

if generated_tree_write_violation(payload):
    block("Generated upstream/build trees are read-only for agents. Move the fix to the owning Orlix layer.")

if "RUN_VERY_EXPENSIVE_TESTS=no" in text or "RUN_EXPENSIVE_TESTS=no" in text:
    block("Do not disable upstream expensive tests when upstream conformance is the claim.")
