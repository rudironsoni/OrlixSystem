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
    "Build/OrlixMLibC",
    "Build/OrlixOS",
)
GENERATED_PATH_RE = re.compile(
    r"(?:\./)?(?:" + "|".join(re.escape(pattern) for pattern in GENERATED_PATTERNS) + r")"
)
WRITE_TOOL_NAMES = {"apply_patch", "edit", "write", "multiedit"}
BASH_TOOL_NAMES = {"bash"}
READ_TOOL_NAMES = {"read", "grep", "glob", "ls"}
BASH_COMMAND_PREFIX = r"(^|[;&|]\s*)(?:rtk\s+)?(?:(?:timeout|gtimeout)\s+\d+\s+)?(?:sudo\s+)?"

BASH_MUTATING_COMMAND_RE = re.compile(
    BASH_COMMAND_PREFIX +
    r"("
    r"apply_patch|"
    r"dd|"
    r"make|"
    r"cmake|"
    r"ninja|"
    r"patch|"
    r"rm|"
    r"rmdir|"
    r"mkdir|"
    r"mv|"
    r"cp|"
    r"install|"
    r"rsync|"
    r"tee|"
    r"touch|"
    r"truncate|"
    r"vim|"
    r"vi|"
    r"nano"
    r")\b"
)
BASH_SED_IN_PLACE_RE = re.compile(BASH_COMMAND_PREFIX + r"(?:g?sed)\b[^;&|]*\s-i(?:\b|[A-Za-z])")
BASH_PERL_IN_PLACE_RE = re.compile(BASH_COMMAND_PREFIX + r"perl\b[^;&|]*\s-[^\s]*i[^\s]*")
BASH_GIT_MUTATION_RE = re.compile(
    BASH_COMMAND_PREFIX + r"git\b[^;&|]*\b"
    r"(apply|am|checkout|clean|restore|reset)\b"
)
BASH_REDIRECT_TO_GENERATED_RE = re.compile(
    r"(?<!<)(?:^|\s)(?:[12]?>|>>|&>)\s*['\"]?" + GENERATED_PATH_RE.pattern
)
BASH_SCRIPT_WRITE_RE = re.compile(
    r"\b(?:python3?|node|ruby|perl)\b.*"
    r"(?:write_text|write_bytes|open\([^)]*['\"](?:w|a|x)\b|writeFile|fs\.write|File\.write)",
    re.DOTALL,
)

CLAIM_RE = re.compile(
    r"\b(complete|completed|done|fixed|green|passes|passing|runtime-ready|package-ready|upstream-test successful|success)\b",
    re.IGNORECASE,
)
CLAIM_EVIDENCE_TERMS = (
    (
        re.compile(r"\b(package-ready|jq|curl|zsh|third-party package|package proof)\b", re.IGNORECASE),
        ("package", "jq", "curl", "zsh", "coreutils", "orlixos", "failures=", "skips="),
    ),
    (
        re.compile(r"\b(runtime-ready|runtime|boot|pty|shell|bash|posix shell)\b", re.IGNORECASE),
        ("runtime", "boot", "pty", "shell", "bash", "orlixos", "crash", "failures=", "skips="),
    ),
    (
        re.compile(r"\b(upstream-test successful|upstream|kselftest|kunit|mlibc|coreutils)\b", re.IGNORECASE),
        ("upstream", "kselftest", "kunit", "mlibc", "coreutils", "failures=", "skips=", "not ok"),
    ),
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


def _normalise_escaped_newlines(text):
    return text.replace("\\n", "\n")


def repo_root():
    try:
        out = subprocess.check_output(["git", "rev-parse", "--show-toplevel"], text=True).strip()
        return Path(out)
    except Exception:
        return Path.cwd()


def mentions_generated_tree(text):
    text = _normalise_escaped_newlines(text)
    if "*** Begin Patch" in text:
        for line in text.splitlines():
            for marker in ("*** Add File: ", "*** Update File: ", "*** Delete File: "):
                if line.startswith(marker):
                    target = line[len(marker):]
                    if any(pattern in target for pattern in GENERATED_PATTERNS):
                        return True
        return False
    return any(pattern in text for pattern in GENERATED_PATTERNS)


def hook_tool_name(payload):
    if not isinstance(payload, dict):
        return ""
    for key in ("tool_name", "tool", "toolName", "name"):
        value = payload.get(key)
        if isinstance(value, str):
            return value.lower()
    tool = payload.get("tool")
    if isinstance(tool, dict):
        value = tool.get("name")
        if isinstance(value, str):
            return value.lower()
    return ""


def hook_tool_input(payload):
    if not isinstance(payload, dict):
        return {}
    for key in ("tool_input", "input", "parameters", "args", "arguments"):
        value = payload.get(key)
        if isinstance(value, dict):
            return value
    return {}


def hook_command(payload):
    tool_input = hook_tool_input(payload)
    for key in ("command", "cmd", "script"):
        value = tool_input.get(key)
        if isinstance(value, str):
            return value
    return ""


def bash_command_mutates_generated_tree(command):
    command = _normalise_escaped_newlines(command)
    if not any(pattern in command for pattern in GENERATED_PATTERNS):
        return False
    if mentions_generated_tree(command) and "*** Begin Patch" in command:
        return True
    if BASH_REDIRECT_TO_GENERATED_RE.search(command):
        return True
    if BASH_SED_IN_PLACE_RE.search(command):
        return True
    if BASH_PERL_IN_PLACE_RE.search(command):
        return True
    if BASH_GIT_MUTATION_RE.search(command):
        return True
    if BASH_MUTATING_COMMAND_RE.search(command):
        return True
    if BASH_SCRIPT_WRITE_RE.search(command):
        return True
    return False


def generated_tree_write_violation(payload):
    text = flattened_text(payload)
    if not any(pattern in _normalise_escaped_newlines(text) for pattern in GENERATED_PATTERNS):
        return False

    tool_name = hook_tool_name(payload)
    if tool_name in READ_TOOL_NAMES:
        return False
    if tool_name in WRITE_TOOL_NAMES:
        return mentions_generated_tree(text)
    if tool_name in BASH_TOOL_NAMES:
        return bash_command_mutates_generated_tree(hook_command(payload))

    return mentions_generated_tree(text)


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


def claim_evidence_terms(claim_text):
    terms = []
    for pattern, pattern_terms in CLAIM_EVIDENCE_TERMS:
        if pattern.search(claim_text):
            terms.extend(pattern_terms)
    return tuple(dict.fromkeys(terms))


def has_claim_relevant_evidence(path, terms):
    if not terms:
        return has_implementation_evidence(path)
    impl = path / "IMPLEMENT.md"
    if not impl.exists():
        return False
    text = impl.read_text(errors="replace").lower()
    if "evidence:" not in text:
        return False
    return any(term.lower() in text for term in terms)


def warn(message):
    print(f"ORLIX-HARNESS-WARN: {message}", file=sys.stderr)


def block(message):
    print(f"ORLIX-HARNESS-BLOCK: {message}", file=sys.stderr)
    sys.exit(2)
