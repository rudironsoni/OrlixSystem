#!/usr/bin/env python3
"""Generate the IXLandSystem Linux 6.12 arm64 syscall coverage matrix."""

from __future__ import annotations

import argparse
import re
import subprocess
from pathlib import Path


CAPABILITY_LABELS = {
    "SYSCALL_CAPABILITY_FD": "implemented:fd",
    "SYSCALL_CAPABILITY_PROCESS": "implemented:process",
    "SYSCALL_CAPABILITY_SIGNAL": "implemented:signal",
    "SYSCALL_CAPABILITY_VM": "implemented:vm",
    "SYSCALL_CAPABILITY_READINESS": "implemented:readiness",
    "SYSCALL_CAPABILITY_MOUNT": "implemented:mount",
    "SYSCALL_CAPABILITY_XATTR": "implemented:xattr",
    "SYSCALL_CAPABILITY_TIME": "implemented:time",
    "SYSCALL_CAPABILITY_RESOURCE": "implemented:resource",
    "SYSCALL_CAPABILITY_RANDOM": "implemented:random",
}

GAP_LABELS = {
    "SYSCALL_GAP_BOOT": "kernel-owned missing:boot",
    "SYSCALL_GAP_SHELL": "kernel-owned missing:shell",
    "SYSCALL_GAP_PACKAGE": "kernel-owned missing:package",
    "SYSCALL_GAP_NETWORK": "future backend:virtual-network",
}

OVERRIDE_LABELS = {
    "SYSCALL_MATRIX_OVERRIDE_KERNEL_OWNED_NEXT_PROCESS": (
        "kernel-owned next:process",
        "next",
    ),
}

SHELL_BASE_SYSCALLS = {
    "close_range",
    "copy_file_range",
    "eventfd2",
    "execveat",
    "fchmod",
    "fchmodat",
    "fchmodat2",
    "fchown",
    "fchownat",
    "fdatasync",
    "flock",
    "fstatfs",
    "fsync",
    "getgroups",
    "getitimer",
    "getrandom",
    "getresgid",
    "getresuid",
    "getrusage",
    "gettimeofday",
    "lseek",
    "memfd_create",
    "newfstatat",
    "openat2",
    "prlimit64",
    "setgid",
    "setgroups",
    "setitimer",
    "setregid",
    "setresgid",
    "setresuid",
    "setreuid",
    "setuid",
    "statfs",
    "sync",
    "syncfs",
    "timerfd_create",
    "timerfd_gettime",
    "timerfd_settime",
    "times",
    "utimensat",
}


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def syscall_macros(root: Path) -> list[tuple[str, int]]:
    include = root / "third_party/linux/uapi/6.12/arm64/include"
    output = subprocess.check_output(
        [
            "clang",
            "-E",
            "-dM",
            "-I",
            str(include),
            "-include",
            "asm/unistd.h",
            "-x",
            "c",
            "/dev/null",
        ],
        text=True,
    )
    syscalls: list[tuple[str, int]] = []
    for line in output.splitlines():
        match = re.match(r"#define __NR_([A-Za-z0-9_]+)\s+([0-9]+)$", line)
        if not match or match.group(1) == "syscalls":
            continue
        syscalls.append((match.group(1), int(match.group(2))))
    return sorted(set(syscalls), key=lambda item: (item[1], item[0]))


def switch_cases(source: str, function_name: str) -> dict[str, str]:
    start = source.index(function_name)
    switch_start = source.index("switch (number)", start)
    body_start = source.index("{", switch_start)
    depth = 1
    pos = body_start + 1
    while depth > 0 and pos < len(source):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
        pos += 1
    body = source[body_start + 1 : pos - 1]

    result: dict[str, str] = {}
    pending: list[str] = []
    for line in body.splitlines():
        case_match = re.match(r"\s*case __NR_([A-Za-z0-9_]+):", line)
        if case_match:
            pending.append(case_match.group(1))
            continue
        return_match = re.match(r"\s*return\s+(SYSCALL_[A-Z_]+);", line)
        if return_match and pending:
            for name in pending:
                result[name] = return_match.group(1)
            pending = []
    return result


def classifications(root: Path) -> tuple[dict[str, str], dict[str, str], dict[str, str]]:
    source = (root / "runtime/syscall.c").read_text()
    return (
        switch_cases(source, "syscall_capability_class_impl"),
        switch_cases(source, "syscall_gap_priority_impl"),
        switch_cases(source, "syscall_matrix_override_class_impl"),
    )


def row_for(
    name: str,
    number: int,
    implemented: dict[str, str],
    gaps: dict[str, str],
    overrides: dict[str, str],
) -> str:
    if name in implemented and name in overrides:
        raise ValueError(
            f"syscall {name} is both implemented and override-classified; "
            "narrow the classification machinery"
        )
    if name in overrides:
        classification, priority = OVERRIDE_LABELS.get(
            overrides[name],
            ("kernel-owned missing:unclassified", "package"),
        )
        return f"| {number} | `{name}` | {classification} | {priority} |"
    if name in implemented:
        classification = CAPABILITY_LABELS.get(implemented[name], "implemented:unknown")
        priority = "none"
    elif name in gaps:
        classification = GAP_LABELS.get(gaps[name], "kernel-owned missing:unclassified")
        priority = gaps[name].removeprefix("SYSCALL_GAP_").lower()
    else:
        classification = "kernel-owned missing:unclassified"
        priority = "shell" if name in SHELL_BASE_SYSCALLS else "package"
    return f"| {number} | `{name}` | {classification} | {priority} |"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=repo_root())
    args = parser.parse_args()

    root = args.root.resolve()
    implemented, gaps, overrides = classifications(root)
    syscalls = syscall_macros(root)

    print("# Linux 6.12 arm64 Syscall Gap Matrix")
    print()
    print("Generated from vendored Linux UAPI and `runtime/syscall.c`.")
    print()
    print("| nr | syscall | classification | priority |")
    print("| ---: | --- | --- | --- |")
    for name, number in syscalls:
        print(row_for(name, number, implemented, gaps, overrides))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
