# ADR 0017: Product Runtime Claim Promotion Order

## Status

Accepted

## Context

Orlix has multiple proof lanes: kernel boot and KUnit, Linux kselftest, OrlixMLibC tests, OrlixMLibC-built kselftests, shell behavior, and real package execution. These lanes can be implemented in parallel, but product claims become misleading if later integration success masks missing kernel, libc, syscall/UAPI, shell, or package-compatibility proof.

## Decision

Orlix product runtime claims must be promoted in dependency order:

1. Kernel dependency proof: OrlixKernel boots on iOS, reaches Linux init handoff, and fails Linux-accurately when no userspace exists; KUnit proves kernel-internal behavior.
2. Kselftest kernel-interface proof: selected Linux-owned kselftests run as OrlixMLibC-built Linux userspace where useful; this is kernel-interface proof only.
3. OrlixMLibC libc proof: mlibc's own tests pass for the Orlix sysdeps layer.
4. OrlixMLibC syscall/UAPI proof: selected kselftests are rebuilt and rerun against OrlixMLibC.
5. POSIX shell environment proof: Bash runs as normal Orlix Linux userspace through the terminal path with enough process, fd, tty, signal, path, environment, and exec behavior for an interactive shell.
6. Third-party package ladder: jq, then curl, then zsh prove increasingly realistic Linux package compatibility.

Development work may proceed in parallel, but claims, release gates, milestone summaries, and product-readiness statements must follow this order.

## Consequences

Later proof does not replace earlier proof. Package success does not replace libc proof. OrlixMLibC-built kselftest does not replace mlibc's own test suite. KUnit does not replace userspace syscall/UAPI proof.
