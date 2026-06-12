#!/usr/bin/env python3
import json
import hashlib
import os
import re
import subprocess
import sys
import tempfile
import time
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
BASH_GIT_COMMIT_PUSH_RE = re.compile(
    BASH_COMMAND_PREFIX + r"git\b[^;&|]*\b(commit|push)\b"
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
MACOS_RUNTIME_RE = re.compile(
    r"\b(?:aligned|alligned)\s+with\s+macOS\b|\bmacOS\s+(?:user|runtime|runtime target|target runtime|execution surface)\b",
    re.IGNORECASE,
)
CONTAINER_SUPPORT_RE = re.compile(r"\b(?:Docker support|container support|containers support)\b", re.IGNORECASE)
OCI_ORLIX_RE = re.compile(r"\bOCI-derived Orlix environments?\b|\bOCI-rooted Orlix environments?\b", re.IGNORECASE)
INVENTED_MECHANISM_RE = re.compile(
    r"\b(?:add|create|build|introduce|implement)\s+(?:Tools/)?(?:OrlixImageBuilder|OrlixLinuxOracle|pivot_root|\.workflow/|workflow root)\b",
    re.IGNORECASE,
)
EXISTING_OPTIONAL_RE = re.compile(r"\b(?:existing|already exists|user-requested|requested by the user|optional|future-only)\b", re.IGNORECASE)
WORKFLOW_DELEGATION_RE = re.compile(r"\b(?:subagent|subagents|swarm|parallel agents?|delegated agents?|dynamic workflow)\b", re.IGNORECASE)
WORKFLOW_AUTH_RE = re.compile(r"\b(?:authorized|authorised|explicit user authorization|explicit user authorisation|user authorized|user authorised)\b", re.IGNORECASE)
CURRENT_STATUS_RE = re.compile(r"^Current status:\s*$", re.MULTILINE)
STALE_STATUS_RE = re.compile(r"\b(?:pending|blocked|interrupted)\b", re.IGNORECASE)
GREEN_STATUS_RE = re.compile(r"\b(?:green|passed|passes|succeeded|success|complete|completed)\b", re.IGNORECASE)
HANDOFF_RE = re.compile(r"\b(?:handoff|reactivation prompt|suggested skills)\b", re.IGNORECASE)
NEGATED_GUARDRAIL_RE = re.compile(
    r"\b(?:no|not|do not|don't|must not|never|without|forbid|forbidden|non-goal|non-goals)\b",
    re.IGNORECASE,
)
OCI_RUNTIME_CLAIM_RE = re.compile(
    r"\b(?:OCI Runtime support|OCI runtime support|OCI Runtime compliant|OCI runtime compliant|OCI compatible|OCI-compatible)\b",
    re.IGNORECASE,
)
ORLIX_RUN_COMPLETE_RE = re.compile(r"\borlix run\b.*\b(?:complete|completed|done|works|green|passing|passes)\b", re.IGNORECASE)
IMAGE_ONLY_OCI_RE = re.compile(r"\b(?:image import|OCI image import|OCI layout import|materialized root|rootfs import)\b", re.IGNORECASE)
OCI_LIFECYCLE_EVIDENCE_RE = re.compile(r"\bcreate\b.*\bstart\b.*\bstate\b.*\bkill\b.*\bdelete\b", re.IGNORECASE | re.DOTALL)
STALE_REMAINING_LIST_RE = re.compile(r"\b(?:stale|superseded)?\s*(?:remaining[- ]work|remaining[- ]task|remaining list|14-item remaining)\b", re.IGNORECASE)


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


def required_plan_context_paths(root):
    paths = [root / "AGENTS.md"]
    for plan in active_plan_dirs(root):
        for name in ("GOAL.md", "PLAN.md", "IMPLEMENT.md"):
            path = plan / name
            if path.exists():
                paths.append(path)
    return paths


def _state_dir():
    override = os.environ.get("ORLIX_PLAN_GUARD_STATE_DIR")
    if override:
        return Path(override)
    return Path(tempfile.gettempdir()) / "orlix-plan-context-guard"


def _repo_state_key(root):
    resolved = str(root.resolve())
    digest = hashlib.sha256(resolved.encode("utf-8")).hexdigest()[:24]
    return digest


def plan_context_state_path(root):
    return _state_dir() / f"{_repo_state_key(root)}.json"


def load_plan_context_state(root):
    path = plan_context_state_path(root)
    try:
        return json.loads(path.read_text())
    except Exception:
        return {"read_paths": [], "mutation_time": 0.0, "implement_update_time": 0.0}


def save_plan_context_state(root, state):
    path = plan_context_state_path(root)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(state, sort_keys=True))


def plan_context_loaded(root, state=None):
    if state is None:
        state = load_plan_context_state(root)
    read_paths = set(state.get("read_paths", []))
    required = {str(path.resolve()) for path in required_plan_context_paths(root)}
    return bool(required) and required.issubset(read_paths)


def _payload_path_mentions(payload):
    text = _normalise_escaped_newlines(flattened_text(payload))
    mentions = set()
    root = repo_root()
    for path in required_plan_context_paths(root):
        path_str = str(path)
        rel = str(path.relative_to(root)) if path.is_relative_to(root) else path.name
        if path_str in text or rel in text:
            mentions.add(str(path.resolve()))
    return mentions


def plan_read_paths_from_payload(payload):
    tool_name = hook_tool_name(payload)
    if tool_name in WRITE_TOOL_NAMES:
        return set()
    if tool_name in BASH_TOOL_NAMES:
        command = hook_command(payload)
        if is_bash_mutating_command(command):
            return set()
    return _payload_path_mentions(payload)


def is_bash_mutating_command(command):
    command = _normalise_escaped_newlines(command)
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
    if re.search(r"(?<!<)(?:^|\s)(?:[12]?>|>>|&>)\s*", command):
        return True
    return False


def tool_mutates_workspace(payload):
    tool_name = hook_tool_name(payload)
    if tool_name in WRITE_TOOL_NAMES:
        return True
    if tool_name in BASH_TOOL_NAMES:
        return is_bash_mutating_command(hook_command(payload))
    return False


def tool_requires_plan_context(payload):
    return tool_mutates_workspace(payload) or is_git_commit_or_push(payload)


def is_git_commit_or_push(payload):
    tool_name = hook_tool_name(payload)
    if tool_name != "bash":
        return False
    return bool(BASH_GIT_COMMIT_PUSH_RE.search(_normalise_escaped_newlines(hook_command(payload))))


def implementation_update_paths_from_payload(payload):
    tool_name = hook_tool_name(payload)
    if tool_name not in WRITE_TOOL_NAMES and tool_name not in BASH_TOOL_NAMES:
        return set()
    text = _normalise_escaped_newlines(flattened_text(payload))
    root = repo_root()
    updates = set()
    for plan in active_plan_dirs(root):
        impl = plan / "IMPLEMENT.md"
        if not impl.exists():
            continue
        rel = str(impl.relative_to(root))
        if str(impl) in text or rel in text:
            updates.add(str(impl.resolve()))
    return updates


def plan_context_post_update(payload):
    root = repo_root()
    if not active_plan_dirs(root):
        return
    state = load_plan_context_state(root)
    read_paths = set(state.get("read_paths", []))
    read_paths.update(plan_read_paths_from_payload(payload))
    state["read_paths"] = sorted(read_paths)
    now = time.time()
    if tool_mutates_workspace(payload):
        state["mutation_time"] = now
    if implementation_update_paths_from_payload(payload):
        state["implement_update_time"] = now
    save_plan_context_state(root, state)


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


def _has_non_negated_match(pattern, text):
    normalized = _normalise_escaped_newlines(text)
    for line in normalized.splitlines():
        if pattern.search(line) and not NEGATED_GUARDRAIL_RE.search(line):
            return True
    return False


def macos_runtime_wording(text):
    return _has_non_negated_match(MACOS_RUNTIME_RE, text)


def vague_container_support(text):
    normalized = _normalise_escaped_newlines(text)
    return _has_non_negated_match(CONTAINER_SUPPORT_RE, normalized) and not OCI_ORLIX_RE.search(normalized)


def invented_mechanism_without_scope(text):
    normalized = _normalise_escaped_newlines(text)
    return _has_non_negated_match(INVENTED_MECHANISM_RE, normalized) and not EXISTING_OPTIONAL_RE.search(normalized)


def workflow_without_authorization(text):
    normalized = _normalise_escaped_newlines(text)
    return _has_non_negated_match(WORKFLOW_DELEGATION_RE, normalized) and not WORKFLOW_AUTH_RE.search(normalized)


def oci_runtime_claim_without_lifecycle(text):
    normalized = _normalise_escaped_newlines(text)
    return OCI_RUNTIME_CLAIM_RE.search(normalized) and not OCI_LIFECYCLE_EVIDENCE_RE.search(normalized)


def orlix_run_claim_without_lifecycle(text):
    normalized = _normalise_escaped_newlines(text)
    return ORLIX_RUN_COMPLETE_RE.search(normalized) and not OCI_LIFECYCLE_EVIDENCE_RE.search(normalized)


def oci_compatible_from_image_only(text):
    normalized = _normalise_escaped_newlines(text)
    return (
        re.search(r"\bOCI compatible\b|\bOCI-compatible\b", normalized, re.IGNORECASE)
        and IMAGE_ONLY_OCI_RE.search(normalized)
        and not OCI_LIFECYCLE_EVIDENCE_RE.search(normalized)
    )


def stale_remaining_task_list(text):
    return bool(STALE_REMAINING_LIST_RE.search(_normalise_escaped_newlines(text)))


def implementation_status_contradiction(path):
    impl = path / "IMPLEMENT.md"
    if not impl.exists():
        return False
    text = impl.read_text(errors="replace")
    sections = [section for section in text.split("\n## ") if "Current status:" in section]
    if len(sections) < 2:
        return False
    earlier = "\n".join(sections[:-1])
    latest = sections[-1]
    return bool(STALE_STATUS_RE.search(earlier) and GREEN_STATUS_RE.search(latest))


def has_recent_handoff_or_status(path):
    impl = path / "IMPLEMENT.md"
    if impl.exists():
        text = impl.read_text(errors="replace")
        tail = text[-4000:]
        if CURRENT_STATUS_RE.search(tail) or HANDOFF_RE.search(tail):
            return True
    handoff_root = path.parents[2] / "codex-handoffs"
    if handoff_root.exists():
        plan_name = path.name.lower()
        for handoff in handoff_root.glob("*.md"):
            if plan_name in handoff.name.lower():
                return True
    return False


def warn(message):
    print(f"ORLIX-HARNESS-WARN: {message}", file=sys.stderr)


def block(message):
    print(f"ORLIX-HARNESS-BLOCK: {message}", file=sys.stderr)
    sys.exit(2)
