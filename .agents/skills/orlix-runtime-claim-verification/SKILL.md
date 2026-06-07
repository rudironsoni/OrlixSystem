---
name: orlix-runtime-claim-verification
description: Use before claiming Orlix work is fixed, green, complete, runtime-ready, package-ready, or upstream-test successful. Verifies that exact commands, logs, failure/skip accounting, crash reports, and ADR proof order support the claim.
---

# Orlix Runtime Claim Verification

Use this skill before any completion claim.

## Claim Check

Identify:

- the exact claim being made;
- the proof lane it belongs to;
- the required command or runtime evidence;
- the observed command output and logs;
- failure and skip counts;
- simulator or app crash reports checked;
- missing evidence.

## Promotion Order

Product runtime claims must follow ADR 0017:

1. Kernel dependency proof
2. Kselftest kernel-interface proof
3. OrlixMLibC libc proof
4. OrlixMLibC-built syscall/UAPI proof
5. POSIX shell environment proof
6. Third-party package ladder: jq, curl, zsh

## Output

Say what is proven, what is not proven, and what command or log would close the gap. Do not soften missing proof into success.
