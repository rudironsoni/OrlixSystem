# AGENTS.md

These rules apply to every task in this repository unless explicitly overridden by the user.

## Project Invariant

Orlix compiles upstream Linux into an iOS-hosted `OrlixKernel.xcframework`. Orlix adapts Linux through Linux-native extension points, not by locally rewriting Linux core subsystems.

If a change makes Orlix less suitable for real Linux userspace, the change is wrong.

## Working Rules

1. Think before coding. State assumptions, surface ambiguity, and ask when ownership is unclear.
2. Prefer the smallest correct change. Do not add speculative abstractions or convenience layers.
3. Touch only what the task requires. Do not refactor unrelated code.
4. Define success criteria and verify them before claiming completion.
5. If code or build output can answer a question, inspect that before asking the user.
6. Surface conflicts directly. Do not average incompatible architecture patterns.
7. Read owning files, callers, and docs before writing.
8. Tests must verify intent and must match the target architecture.
9. Checkpoint after significant steps.
10. Follow repository conventions unless they conflict with the new upstream-Linux architecture.
11. Fail loud when proof is missing, partial, or skipped.
12. Do not preserve backward compatibility with wrong architecture unless the user explicitly asks for it.

## Architecture Ownership

Upstream Linux owns Linux core behavior:

- VFS
- tasks and process model
- fd tables
- signals
- wait and reaping
- procfs, sysfs, devtmpfs
- cgroups and namespaces
- sockets and networking core
- syscall semantics
- exec and interpreter behavior

Durable Orlix Linux port inputs live under:

- `Linux/ports/orlix/overlay/arch/orlix`
- `Linux/ports/orlix/overlay/drivers/orlix`
- `Linux/ports/orlix/configs`
- `Linux/ports/orlix/patches`

Generated upstream source is read-only input:

- `Linux/upstream/linux-6.12`

Generated Orlix port source is disposable:

- `Build/OrlixKernel/linux-<version>-port`

Host mechanics live only in:

- `OrlixHostAdapter`

The public product header lives in:

- `OrlixKernel/include`

## Local Kernel Prototype Rule

`OrlixKernel/fs`, `OrlixKernel/kernel`, and `OrlixKernel/runtime` are not target implementation paths.

Do not add new Linux subsystem behavior there. Read those directories only as migration reference. Useful behavior must move by ownership into upstream Linux, `arch/orlix`, Linux-native drivers, boot code, or host-adapter seams. The end state has no `OrlixKernel/fs`, `OrlixKernel/kernel`, or `OrlixKernel/runtime` directories.

## Virtio-First Rule

Use upstream Linux virtio device classes wherever they fit.

Linux-visible behavior should be owned by upstream drivers such as `virtio_blk`, `virtio_console`, `virtio-rng`, `virtio_net`, virtio-fs, or 9p over virtio. Orlix-specific code supplies transport and backend mechanics under `drivers/orlix`, shaped as close to Linux virtio conventions as possible.

Do not write custom Orlix block, network, random, console, or filesystem drivers when an upstream virtio class satisfies the requirement.

## Boot And Product API Rule

The product API is bootloader-shaped only.

Allowed direction:

- a minimal `OrlixBoot` entrypoint
- a closed boot profile selection
- opaque app-level resource identifiers

Forbidden direction:

- public syscall APIs
- public file, mount, exec, task, cgroup, or runtime management APIs
- raw Linux boot parameters as the main public API
- fake kernel management facades

Boot data should be Linux-shaped. Prefer profile device trees and kernel command-line defaults over custom Orlix boot-template formats.

## Host Boundary Rule

`OrlixHostAdapter` owns private iOS and Darwin mechanics only. It must not own Linux policy or public Linux ABI.

Use narrow owner seams:

- `arch/orlix` may use host seams for clocks, timers, execution substrate, low-level memory mapping, lifecycle notification, and very-early entropy.
- `drivers/orlix` may use host seams for virtio transport and backend mechanics.

Virtio is for virtual devices. It is not the path for Linux MM policy, syscall semantics, task model, or executable-memory decisions.

## Build And Proof Rule

The first honest proof target is upstream Linux `vmlinux` for `ARCH=orlix`.

Required Milestone 1 proof commands are:

```bash
make prepare-orlixkernel-port PROFILE=appstore
make build-linux-kernel PROFILE=appstore
make build-linux-kernel PROFILE=development
```

`PROFILE=appstore` is the default normal profile.

`OrlixKernel.xcframework` packaging must depend on a real Linux build artifact for the selected profile. Packaging boot stubs alone is not product proof.

## Test Rule

Tests for the old local kernel prototype are migration reference only. They are not authoritative proof for the target architecture.

New proof should focus on:

- upstream Linux Kbuild behavior
- Linux-native tests where applicable
- boot and rootfs integration
- Xcode packaging after a real Linux artifact exists
- `OrlixHostAdapter` host-mechanics tests only for host mechanics

## Documentation Rule

Architecture rules live in:

- `docs/UPSTREAM_LINUX_IOS_PORT_SPEC.md`

Durable architecture decisions live in:

- `docs/adr/`

Resolved glossary terms live in:

- `CONTEXT.md`
