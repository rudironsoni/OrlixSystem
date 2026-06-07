---
name: orlix-implementation-boundaries
description: Use before implementing or triaging Orlix changes when ownership could involve OrlixKernel, OrlixMLibC, OrlixOS, OrlixHostAdapter, OrlixTerminal, upstream Linux, upstream mlibc, packages, product payloads, session APIs, or generated trees. Routes fixes to the correct layer and blocks wrong-layer shortcuts.
---

# Orlix Implementation Boundaries

Use this skill before changing code for non-trivial Orlix behavior, especially kernel/libc/package/runtime issues.

## Routing

- Linux semantics: `OrlixKernel/Sources/ports/orlix`, upstream Linux overlay paths, Linux-native drivers, boot, KUnit, or kselftest.
- Libc behavior: `OrlixMLibC/Sources` and `OrlixMLibC/Tests`.
- Delivered OS Kit, app-facing Linux session API, target-derived payload metadata, curated distribution policy, and package/rootfs assembly: `OrlixOS`.
- Private iOS mechanics: `OrlixHostAdapter/Sources`.
- iOS host UI, Ghostty rendering, and terminal presentation: `OrlixTerminal`.
- Architecture truth: `docs/architecture`, `docs/adr`, and `docs/reference`.

## Refusals

Do not:

- edit generated upstream trees or disposable build output; reading generated trees for diagnosis is allowed;
- move Linux policy into `OrlixHostAdapter`;
- move libc behavior into `OrlixKernel`;
- move syscall semantics into `OrlixOS`;
- recreate a separate `OrlixKit` target/module; `OrlixOS` is the Kit;
- hardcode product bundle identifiers or payload resource names in runtime code when they belong in `project.yml` or target metadata;
- disable upstream package capabilities or invent package-specific linker/tool wrappers instead of fixing OrlixOS package-toolchain inputs;
- add public runtime facades or shell/package management APIs;
- restore retired local-kernel prototype paths.

## Output

Before implementation, state the owning layer, why that layer owns the behavior, the files to inspect first, and the verification gate.
