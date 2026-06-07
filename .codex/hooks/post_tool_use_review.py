#!/usr/bin/env python3
from orlix_hook_common import BAD_OUTPUT_RE, flattened_text, parse_json, read_stdin_text, warn

payload = parse_json(read_stdin_text())
text = flattened_text(payload)

if BAD_OUTPUT_RE.search(text):
    warn("Tool output contains failure, skip, panic, crash, or missing-implementation signal. Do not claim completion without triage and evidence.")
