# AGENTS.md

These rules apply to every task in this repository unless explicitly overridden by the user.

## Project Invariant

Orlix compiles upstream Linux into an iOS-hosted `OrlixKernel.xcframework` and pairs it with `OrlixMLibC`, an mlibc-based libc for Orlix Linux userspace. Orlix adapts Linux through Linux-native extension points, not by locally rewriting Linux core subsystems.

If a change makes Orlix less suitable for real Linux userspace, the change is wrong.

OrlixKernel is Linux. It does not own shell behavior, libc behavior, package management, public syscall APIs, or a custom runtime facade. Shells and packages are normal Orlix Linux userspace binaries linked against OrlixMLibC and executed through Linux mechanisms.

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

Project-local source and test roots are fixed:

- `OrlixKernel/Sources`
- `OrlixKernel/Tests`
- `OrlixHostAdapter/Sources`
- `OrlixHostAdapter/Tests`
- `OrlixMLibC/Sources`
- `OrlixMLibC/Tests`
- `OrlixTerminal/Sources`
- `OrlixTerminal/Tests`

Durable Orlix Linux port inputs live under:

- `OrlixKernel/Sources/ports/orlix/overlay/arch/orlix`
- `OrlixKernel/Sources/ports/orlix/overlay/drivers/orlix`
- `OrlixKernel/Sources/ports/orlix/configs`
- `OrlixKernel/Sources/ports/orlix/patches`

Generated upstream source is read-only input:

- `OrlixKernel/Sources/upstream/linux-6.12`

Generated Orlix port source is disposable:

- `Build/OrlixKernel/linux-<version>-port`

Host mechanics live only in:

- `OrlixHostAdapter/Sources`

Orlix userspace libc inputs live under:

- `OrlixMLibC/Sources`

Orlix userspace libc tests live under:

- `OrlixMLibC/Tests`

The iOS host app lives under:

- `OrlixTerminal/Sources`

The iOS host app tests live under:

- `OrlixTerminal/Tests`

Generated OrlixMLibC and userspace artifacts are disposable:

- `Build/OrlixMLibC`

The public product header lives in:

- `OrlixKernel/Sources/include`

## Local Kernel Prototype Rule

Legacy local-kernel prototype code lives under `LegacyOrlix/`. It is not a target implementation path.

Do not add new Linux subsystem behavior there. Read `LegacyOrlix/` only as migration reference. Useful behavior must move by ownership into upstream Linux, `arch/orlix`, Linux-native drivers, boot code, or host-adapter seams. `OrlixKernel/fs`, `OrlixKernel/kernel`, and `OrlixKernel/runtime` must not reappear.

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

## No Libc Leakage Across Kernel Boundary Rule

`OrlixKernel` must not own, embed, implement, expose, shim, or link a userspace libc. This includes OrlixMLibC, mlibc, musl, glibc, Darwin libc compatibility layers, libc sysdeps, libc startup code, dynamic-loader behavior, errno/signal wrappers, stdio, malloc-family userspace allocators, pthreads, resolver behavior, locale, or package/runtime facade behavior.

`OrlixKernel` is freestanding upstream Linux plus the Orlix Linux port. Linux-owned code must not include Apple SDK, Darwin, Foundation, POSIX host, libc, OrlixMLibC, mlibc, musl, or glibc headers. Do not add libc-shaped helper APIs to make kernel code compile.

`OrlixHostAdapter` may use Apple SDK, Darwin, POSIX, and host libc APIs internally when implementing private iOS mechanics. That dependency must never leak through headers, structs, callbacks, return conventions, ownership rules, or behavior contracts consumed by `OrlixKernel`, `arch/orlix`, or `drivers/orlix`.

Kernel-visible HostAdapter contracts must be freestanding and Linux-shaped: no libc headers, no Apple or Objective-C types, no `FILE *`, `pthread_t`, `errno` contract, POSIX fd contract, `struct stat`, `DIR *`, malloc/free ownership convention, dynamic-loader contract, or host syscall ABI. Use Orlix-defined opaque handles, primitive scalar values, and explicit status enums instead.

`OrlixHostAdapter` may provide only narrow host mechanics required by `arch/orlix` and `drivers/orlix`. It must not own Linux policy, translate Linux syscalls into host libc calls as the syscall implementation strategy, emulate libc services, provide an application ABI, or become a userspace runtime.

OrlixMLibC is the only libc component. It lives under `OrlixMLibC`, consumes Linux UAPI through `headers_install`, and communicates with OrlixKernel only through Linux-shaped syscall and UAPI behavior.

Linux kernel internal helper sources are not libc permission slips. Add them to product Mach-O builds only when they are required kernel dependencies with an audited symbol/export policy and no host-libc dependency in kernel-visible code.

## OrlixMLibC Rule

`OrlixMLibC` is a top-level component, not part of the Linux kernel port. It tracks upstream mlibc through generated/read-only upstream input plus durable Orlix sysdeps, configs, and patches under `OrlixMLibC/Sources`. Its tests live under `OrlixMLibC/Tests`.

OrlixMLibC may have an Orlix sysdeps identity only where mlibc needs an OS-port hook. It must call Linux-shaped syscalls and expose glibc/musl source-compatible Linux behavior. Do not add an Orlix-specific application ABI.

OrlixMLibC consumes kernel UAPI only through standard Linux `headers_install` output for `ARCH=orlix`, generated under `Build/OrlixMLibC/kernel-headers/<profile>/`. Do not commit copied Linux syscall numbers, ioctl payloads, structs, constants, flags, or UAPI definitions into OrlixMLibC.

Orlix userspace ABI is profile-invariant. App Store, development, simulator, CI, and debug builds may produce separate artifacts, but installed UAPI headers, syscall numbers, errno values, signal ABI, ioctl payloads, userspace-visible struct layouts, OrlixMLibC ABI, dynamic-loader contract, package ABI, and observable Linux userspace behavior must remain the same. Profile ABI drift is a release-blocking error.

## Build And Proof Rule

Orlix does not require `vmlinux` as a canonical build, proof, or runtime artifact.

The canonical OrlixKernel proof artifact is the iOS app-hosted OrlixKernel integration that actually runs inside the Orlix app environment: OrlixKernel static library, framework, or object set plus `OrlixHostAdapter`, the iOS app host, and simulator/device execution.

A `vmlinux`-style artifact may exist only as an optional developer/debug artifact with a named consumer. It is not a milestone, not product proof, not runtime proof, not libc proof, and not required for installed UAPI headers. If no concrete workflow consumes it, do not generate it.

`PROFILE=appstore` is the default normal profile.

Packaging boot stubs alone is not product proof. Packaging is useful only when it packages or links the app-hosted OrlixKernel integration that is later launched in the iOS proof path.

Work may happen in parallel, but product runtime claims must follow ADR 0017's promotion order:

1. Kernel dependency proof
2. Kselftest kernel-interface proof
3. OrlixMLibC libc proof
4. OrlixMLibC-built syscall/UAPI proof
5. POSIX shell environment proof
6. Third-party package ladder: jq, curl, zsh

Do not claim product runtime readiness from KUnit, temporary kselftest, no-init boot logs, packaging, simulator launch, or a host-side harness.

## Make Target Rule

There is one Makefile per project: `OrlixKernel/Makefile`, `OrlixHostAdapter/Makefile`, `OrlixMLibC/Makefile`, and `OrlixTerminal/Makefile`. The top-level `Makefile` orchestrates those project Makefiles and keeps its public interface small and Linux-shaped. Prefer conventional targets such as `build`, `prepare`, `headers_install`, `kunit`, `kselftest`, `kselftest-install`, and `test`.

Select Orlix-specific scope with variables such as `PROFILE=appstore`, `type=kunit,kselftest`, and `libc=linux` or `libc=orlixmlibc`. Do not add user-facing milestone, proof-lane, or artifact-path targets such as `proof-*`, `build-temporary-*`, `stage-temporary-*`, or `test-all`; proof labels belong in artifact metadata and logs.

## Test Rule

Tests for the old local kernel prototype are migration reference only. They are not authoritative proof for the target architecture.

Target test ownership is split by proof lane:

- KUnit lives in Linux-owned overlay paths such as `OrlixKernel/Sources/ports/orlix/overlay/arch/orlix/**/<owner>_test.c` and `OrlixKernel/Sources/ports/orlix/overlay/drivers/orlix/**/<owner>_test.c`, selected by `OrlixKernel/Sources/ports/orlix/overlay/arch/orlix/.kunitconfig`.
- kselftest lives under `OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/` and must run through upstream kselftest install plus `run_kselftest.sh -c orlix`.
- XCTest lives under project-local `Tests/XCTest/` trees: `OrlixKernel/Tests/XCTest`, `OrlixHostAdapter/Tests/XCTest`, `OrlixMLibC/Tests/XCTest` when needed, and `OrlixTerminal/Tests/XCTest` when needed. XCTest owns iOS host launch, packaging, output collection, parser behavior, and narrow `OrlixHostAdapter` mechanics only.
- Local prototype tests live only under `LegacyOrlix/Tests/MigrationReference/LocalKernelPrototype/` until migrated or deleted.

Temporary kselftests use `Build/OrlixKernel/kselftest/temporary/<profile>/` and the proof label `temporary-kselftest-kernel-interface`. OrlixMLibC-built kselftests use `Build/OrlixMLibC/kselftest/<profile>/` and the proof label `orlixmlibc-kselftest-syscall-uapi`.

New proof must be labeled by what it proves:

- App-hosted OrlixKernel build proof proves the kernel integration artifact that the iOS app will run can be built for the selected profile and destination.
- XCFramework or app packaging proof proves the hosted integration is packaged or linked into the iOS host, not that Linux booted or ran userspace.
- KUnit proves OrlixKernel internal behavior.
- Linux-accurate no-init behavior proves kernel dependency boot reached the normal init handoff without requiring libc.
- kselftests through a temporary foreign-libc, nolibc, or other temporary harness prove kernel-interface behavior only.
- mlibc's own tests prove OrlixMLibC libc behavior.
- OrlixMLibC-built kselftests prove the OrlixMLibC-to-OrlixKernel syscall/UAPI path.
- Bash proves the first interactive POSIX shell environment.
- jq, curl, and zsh prove increasingly realistic third-party package compatibility.
- XCTest proves iOS host integration, packaging, launch, proof-output collection, and narrow `OrlixHostAdapter` host mechanics.

Do not use Darwin-hosted execution, VM/QEMU execution, repo-local shell harnesses, standalone C contract binaries, fake shells, sandbox shells, or local terminal backends as product proof.

## Documentation Rule

Architecture rules live in:

- `docs/UPSTREAM_LINUX_IOS_PORT_SPEC.md`

Durable architecture decisions live in:

- `docs/adr/`

Resolved glossary terms live in:

- `CONTEXT.md`
- If using XcodeBuildMCP, use the installed XcodeBuildMCP skill before calling XcodeBuildMCP tools.
