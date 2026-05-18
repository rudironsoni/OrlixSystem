# ADR 0002: Ground Milestones In Linux-Native Proof

## Status

Accepted

## Context

The upstream-Linux iOS port spans source generation, Kbuild, packaging, boot, architecture glue, virtio transport, storage, console, package policy, lifecycle, tests, and cleanup of the local prototype.

Without milestone-level tests, architecture claims become documentation wishes and the project can drift back toward boot stubs, Darwin-hosted harnesses, or local-kernel prototype behavior.

Tests are not a late quality gate. They are how Orlix proves that each step still implements the right product direction.

## Decision

Every milestone must define its proof first and must be backed by tests appropriate to the behavior being claimed.

Linux-owned behavior is proved with Linux-native tests, primarily KUnit and kselftest. iOS-hosted product integration is proved with XCTest launching Orlix and collecting Linux test output. Build-only artifacts may be preparatory evidence, but they do not prove runtime behavior. iOS product/runtime claims require execution through an iOS-hosted Orlix Linux path with evidence collected from inside that Linux instance.

XCTest proof follows upstream Linux output shape: KUnit results are collected from the Linux kernel log path, and kselftest results are collected from the test-initramfs stdout stream. The host side captures and parses those streams privately; it must not expose public test-management APIs or depend on the interactive terminal byte stream being ready.

KUnit and kselftest raw outputs remain separate streams. XCTest parses each stream independently and reports a combined milestone verdict only after both pass.

The test initramfs collects KUnit output before running kselftest. Built-in KUnit runs during boot, so the initramfs captures kernel-log/debugfs KUnit results first, then invokes `run_kselftest.sh -c orlix` and captures stdout separately.

The iOS-hosted test kernel may enable Linux debugfs and `CONFIG_KUNIT_DEBUGFS` as explicit test affordances so the test initramfs can read per-suite KTAP from `/sys/kernel/debug/kunit/<suite>/results`. Kernel-log collection remains the primary boot-time KUnit path.

Orlix KUnit suite selection follows upstream KUnit surface conventions through the committed `OrlixKernel/Sources/ports/orlix/overlay/arch/orlix/.kunitconfig`. kselftest coverage lives under `OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix`.

kselftest binaries should be staged for the test initramfs through the upstream kselftest install shape, not by ad hoc copying, unless the upstream install path is temporarily blocked.

The test initramfs should invoke installed kselftests through `run_kselftest.sh`. Direct execution of individual selftest binaries bypasses upstream runner behavior and is not milestone proof.

The proof runner should select the Orlix collection explicitly with `run_kselftest.sh -c orlix`, even when only `TARGETS=orlix` is installed.

The initial kselftest proof scope is `TARGETS=orlix`. Broader upstream kselftest targets should be added milestone-by-milestone as the relevant Linux subsystems become available.

Orlix follows upstream kselftest timeout semantics. Do not add a `settings` file merely to restate the 45-second default; add one only for a concrete test-specific timeout. Timeouts remain visible in TAP output, and the XCTest proof runner decides whether a timeout is milestone-fatal for the proof being claimed.

The XCTest proof runner should not pass `run_kselftest.sh --override-timeout` initially. Add a runner override only when the controlled iOS proof environment shows a concrete need.

Repo-local shell scripts and standalone C contract tests are not milestone proof for Orlix. Existing local-kernel XCTest files under `LegacyOrlix/Tests/MigrationReference/LocalKernelPrototype/` are migration reference unless they are rewritten as an iOS-hosted Orlix Linux harness under an owning project `Tests` tree or narrow `OrlixHostAdapter/Tests` host-mechanics tests.

The port will be planned and executed as focused milestones because later layers must not depend on untested claims from earlier layers.

Durable milestone decisions belong in ADRs, the canonical architecture spec, and glossary terms. Task-by-task execution plans and brainstorming specs are not durable architecture records and should not be committed under `docs/superpowers/`.

## Clarification: Relationship To ADR 0017

This ADR establishes that Linux-facing behavior must be grounded in Linux-native proof, while iOS host integration is proved with XCTest. ADR 0017 refines how Linux-native proof is promoted into product runtime claims.

KUnit proves OrlixKernel internal behavior. kselftest run through a temporary foreign-libc, nolibc, or other temporary harness proves kernel-interface behavior only. These lanes are necessary dependency proof, but they do not by themselves prove the full Orlix userspace runtime.

Full product runtime claims must follow the claim promotion order in ADR 0017: kernel dependency proof, kselftest kernel-interface proof, OrlixMLibC libc proof, OrlixMLibC-built syscall/UAPI proof, POSIX shell environment proof, and third-party package proof.

## Consequences

Milestone 1 is app-hosted OrlixKernel build proof, not full boot or device support. ADR 0018 supersedes earlier wording that treated `vmlinux` as the canonical proof artifact.

Milestone 3 establishes app-hosted OrlixKernel packaging or linking because iOS-hosted execution cannot advance without the integration artifact the iOS app actually runs.

Milestone 4 establishes iOS-hosted kernel-interface test execution before later runtime milestones claim proof. KUnit proves kernel-internal behavior, and temporary-harness kselftests prove kernel-interface behavior only.

Later milestones cover boot entrypoint, XCFramework packaging, iOS-hosted kernel-interface test execution, boot to virtio probe, virtio root disks, root assembly, console, remaining virtio devices, OrlixMLibC, POSIX shell proof, package proof, and final prototype deletion.

Scope that does not belong to the current milestone should be deferred rather than partially implemented.

Committed implementation recipes and temporary specs should be deleted or replaced by durable decisions after they have served their purpose.
