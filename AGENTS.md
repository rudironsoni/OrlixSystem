# AGENTS.md

These rules apply to every task in this repository unless the user explicitly overrides them.

## Project Invariant

Orlix compiles upstream Linux into an iOS-hosted `OrlixKernel.xcframework`, pairs it with `OrlixMLibC`, and delivers the curated OS as the `OrlixOS` Kit/framework.

If a change makes Orlix less suitable for real Linux userspace, the change is wrong.

OrlixKernel is Linux. It does not own shell behavior, libc behavior, package management, public syscall APIs, or a custom runtime facade. Shells and packages are normal Orlix Linux userspace binaries linked against OrlixMLibC and executed through Linux mechanisms.

Apps consume `OrlixOS` for the delivered OS session and payload surface. Do not recreate a separate `OrlixKit` module or move OS delivery into `OrlixTerminal` or `OrlixHostAdapter`.

## First Reads

- Architecture: `docs/architecture/ORLIX_UPSTREAM_LINUX_IOS_PORT.md`
- Glossary: `docs/reference/ORLIX_GLOSSARY.md`
- ADR index: `docs/adr/README.md`
- Harness guide: `docs/harness/README.md`
- Agent memory: `docs/harness/MEMORY.md`
- Active plans: `docs/plans/active/`

## Ownership

Upstream Linux owns Linux behavior: VFS, tasks, fd tables, signals, wait/reaping, procfs, sysfs, devtmpfs, cgroups, namespaces, sockets, syscall semantics, exec, and interpreter behavior.

Durable Orlix Linux port inputs live under:

- `OrlixKernel/Sources/ports/orlix/overlay/arch/orlix`
- `OrlixKernel/Sources/ports/orlix/overlay/drivers/orlix`
- `OrlixKernel/Sources/ports/orlix/configs`
- `OrlixKernel/Sources/ports/orlix/patches`

`OrlixHostAdapter/Sources` owns private iOS and Darwin mechanics only. It must not own Linux policy or public Linux ABI.

`OrlixMLibC/Sources` owns OrlixMLibC sysdeps, configs, and patches. OrlixMLibC consumes Linux UAPI through `headers_install` for upstream `ARCH=arm64` and calls Linux-shaped syscalls.

`OrlixOS` is the Kit: it owns curated distribution policy, package/rootfs assembly, product payload packaging, target-derived payload metadata, and the app-facing Linux session API. It must not own kernel semantics, libc semantics, syscall ABI, private iOS host mechanics, or terminal UI rendering.

## Generated Trees

Generated upstream and disposable build trees are read-only inputs for agents:

- `Build/OrlixKernel/upstream/linux-*.git`
- `Build/OrlixKernel/src/linux-*-port`
- `Build/OrlixMLibC`
- `Build/OrlixMLibC/upstream/mlibc-*.git`
- `Build/OrlixMLibC/src/mlibc-*`
- `Build/OrlixOS`

Do not edit generated upstream trees, adapted upstream tests, generated package sources, or disposable build output to make tests pass.

## Retired Prototype

The local kernel prototype is retired. Do not restore `LegacyOrlix/`, `OrlixKernel/fs`, `OrlixKernel/kernel`, or `OrlixKernel/runtime`. Useful behavior must move by ownership into upstream Linux, `arch/orlix`, Linux-native drivers, boot code, or narrow host-adapter seams.

## Codex Harness

Use Codex-native surfaces deliberately:

- `.codex/agents/orlix-planner.toml` for non-trivial planning and `docs/plans/active/<task>/PLAN.md`.
- `.codex/agents/orlix-implementer.toml` for executing an active plan and maintaining `IMPLEMENT.md`.
- `.codex/agents/orlix-reviewer.toml` for skeptical review of assumptions, directives, upstream conformance, and evidence.
- `.agents/skills/orlix-implementation-boundaries/SKILL.md` before deciding which layer owns a fix.
- `.agents/skills/orlix-upstream-conformance/SKILL.md` for upstream Linux, mlibc, Coreutils, or other upstream test suites.
- `.agents/skills/orlix-runtime-claim-verification/SKILL.md` before claiming fixed, green, complete, runtime-ready, package-ready, or upstream-test success.
- `.agents/skills/orlix-write-to-harness/SKILL.md` before editing this harness, plans, skills, agents, hooks, rules, ADR indexes, or agent memory.

`rtk` only shrinks command output. Harness rules and hooks must treat `rtk <command>` as equivalent to `<command>` for approval and block decisions.

## Proof Rules

Define success criteria before implementation and verify them before claiming completion.

Product runtime claims follow ADR 0017's promotion order:

1. Kernel dependency proof
2. Kselftest kernel-interface proof
3. OrlixMLibC libc proof
4. OrlixMLibC-built syscall/UAPI proof
5. POSIX shell environment proof
6. Third-party package ladder: jq, curl, zsh

Do not claim product runtime readiness from KUnit, kselftest, no-init boot logs, packaging, simulator launch, host-side harnesses, or partial upstream test runs.

For upstream conformance work, upstream sources and tests are authoritative. Fix Orlix to match upstream Linux/mlibc/package behavior; do not edit generated upstream sources or reinterpret failures as success.

## Working Rules

1. Read owning files, callers, docs, and ADRs before writing.
2. Prefer the smallest correct change; avoid speculative abstractions.
3. Touch only what the task requires.
4. Surface ownership conflicts directly.
5. Use `rtk` for shell commands in this workspace.
6. Use `rg`/`rg --files` for searches.
7. Preserve unrelated dirty worktree changes.
8. Fail loud when evidence is missing, partial, skipped, or stale.
9. Check simulator/app crash reports after app-hosted test failures or crashes.
10. Commit and push after a coherent verified checkpoint when implementation work is complete.

## XcodeBuildMCP

If using XcodeBuildMCP, use the installed XcodeBuildMCP skill before calling XcodeBuildMCP tools.
