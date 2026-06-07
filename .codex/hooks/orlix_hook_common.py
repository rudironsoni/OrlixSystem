#!/usr/bin/env python3
import json
import os
import re
import subprocess
import sys
from pathlib import Path

GENERATED_PATTERNS = (
    "Build/OrlixKernel/upstream/linux-",
    "Build/OrlixKernel/src/linux-",
    "Build/OrlixMLibC/upstream/mlibc-",
    "Build/OrlixMLibC/src/mlibc-",
    "Build/OrlixOS/upstream/coreutils-",
    "Build/OrlixOS/src/",
)

CLAIM_RE = re.compile(
    r"\b(complete|completed|done|fixed|green|passes|passing|runtime-ready|package-ready|upstream-test successful|success)\b",
    re.IGNORECASE,
)

BAD_OUTPUT_RE = re.compile(
    r"(Kernel panic|user fault|app crash|crash report|FAIL:|not ok|SKIP:|Operation not implemented|ORLIX-COREUTILS-TEST-END failures=[1-9]|skips=[1-9])",
    re.IGNORECASE,
)


def read_stdin_text():
    try:
        return sys.stdin.read()
    except Exception:
        return ""


def parse_json(text):
    try:
        return json.loads(text) if text.strip() else {}
    except Exception:
        return {}


def flattened_text(value):
    if isinstance(value, str):
        return value
    try:
        return json.dumps(value, sort_keys=True)
    except Exception:
        return str(value)


def repo_root():
    try:
        out = subprocess.check_output(["git", "rev-parse", "--show-toplevel"], text=True).strip()
        return Path(out)
    except Exception:
        return Path.cwd()


def mentions_generated_tree(text):
    if "*** Begin Patch" in text:
        for line in text.splitlines():
            for marker in ("*** Add File: ", "*** Update File: ", "*** Delete File: "):
                if line.startswith(marker):
                    target = line[len(marker):]
                    if any(pattern in target for pattern in GENERATED_PATTERNS):
                        return True
        return False
    return any(pattern in text for pattern in GENERATED_PATTERNS)


def active_plan_dirs(root):
    active = root / "docs" / "plans" / "active"
    if not active.exists():
        return []
    return [p for p in active.iterdir() if p.is_dir()]


def has_implementation_evidence(path):
    impl = path / "IMPLEMENT.md"
    if not impl.exists():
        return False
    text = impl.read_text(errors="replace")
    return "Evidence:" in text and ("rtk " in text or "make " in text or ".log" in text or "xcodebuild" in text)


def warn(message):
    print(f"ORLIX-HARNESS-WARN: {message}", file=sys.stderr)


def block(message):
    print(f"ORLIX-HARNESS-BLOCK: {message}", file=sys.stderr)
    sys.exit(2)
