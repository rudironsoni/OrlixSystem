# IMPLEMENT.md

Implementation log. Append-only. Capture decisions, deviations from the plan, evidence, blockers, and open questions that arose during execution.

## Task Reference

`PLAN.md`

---

## Log

### 2026-06-12 Named session state-image reuse prerequisite proof

**What happened:**

Added an OrlixOS session/storage proof that a fresh named environment session
construction resolves the same persisted base and state image paths for the
same environment ID after the state image has changed.

This is a prerequisite for cross-boot environment persistence. It proves the
OrlixOS session binding does not allocate or copy a different state image when
the same named environment is selected again. It does not encode Linux
filesystem behavior in XCTest; Linux mount/write/fsync/remount behavior remains
covered by the Orlix-owned kselftest probe.

**Changes:**

- `OrlixOS/Sources/Session/OrlixOS.swift`
  - Added SPI-only `materializedRootImageForTesting` on `OrlixLinuxSession` so
    OrlixOS tests can inspect the selected materialized image binding without
    exposing a public product API.
- `OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
  - Added
    `testNamedEnvironmentSessionReusesPersistedStateImageAfterMutation`.
- `docs/plans/active/oci-derived-environments-virtio-plane/PLAN.md`
  - Recorded the narrower proved state.

**Evidence:**

- RED proof:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testNamedEnvironmentSessionReusesPersistedStateImageAfterMutation test`
  - Result: failed at compile time because `OrlixLinuxSession` had no
    `materializedRootImageForTesting` member.
  - Result bundle:
    `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_17-59-53-+0200.xcresult`.
- GREEN proof:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testNamedEnvironmentSessionReusesPersistedStateImageAfterMutation test`
  - Result: `** TEST SUCCEEDED **`.
  - Executed 1 test, 1 passed, 0 failed, 0 skipped.
  - Result bundle:
    `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_18-03-43-+0200.xcresult`.
- Focused regression proof:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testLinuxSessionCanBindMaterializedEnvironmentRootImage -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testLinuxSessionCanSelectNamedEnvironmentRootFromRegistry -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testNamedEnvironmentSessionReusesPersistedStateImageAfterMutation test`
  - Result: `** TEST SUCCEEDED **`.
  - Executed 3 tests, 3 passed, 0 failed, 0 skipped.
  - Result bundle:
    `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_18-04-56-+0200.xcresult`.

**Explicit non-claims:**

- This does not prove a Linux process was restarted across a fresh host process.
- This does not prove the full cross-boot persistence checkpoint.
- This does not prove multiple live environments inside one already-running
  OrlixKernel.
- This does not prove product import-to-enter flow, OCI Runtime Spec lifecycle,
  truthful feature reporting, product `orlix run`, host-folder mounts,
  virtio-fs, networking, cgroups, native performance, real-device behavior, or
  App Store acceptance.

**Current conclusion:**

- OrlixOS now has a focused proof that named session construction preserves the
  state-image identity needed for a later full cross-boot Linux restart proof.
- The full cross-boot persistence checkpoint remains open.

### 2026-06-12 Linux-owned environment state writeback prerequisite proof

**What happened:**

Replaced the earlier wrong-shaped XCTest-only persistence attempt with a
Linux-owned kselftest-style probe. The durable behavior now lives in the
upstream Linux overlay under `tools/testing/selftests/orlix`; XCTest only boots
the iOS Simulator OrlixOS path and selects that probe.

This checkpoint proves a prerequisite for environment persistence, not the full
cross-boot product claim. The probe mounts `/dev/vdb` as ext4 through Linux,
writes and fsyncs a marker, unmounts and remounts the filesystem, rereads the
marker, verifies `/dev/vda` still rejects writes, and verifies `/dev/vdb` still
flushes writes.

**Changes:**

- `OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/environment_state_writeback_probe.c`
  adds the Linux-owned writeback probe.
- `OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/Makefile`
  installs the probe with the Orlix selftest set.
- `OrlixTestRunner/Sources/OrlixUpstreamTestRunner.swift` adds the focused
  `orlix.kselftest=environment_state_writeback_probe` run spec.
- `OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift`
  asserts the app-hosted probe output.
- `docs/plans/active/oci-derived-environments-virtio-plane/PLAN.md` records
  the narrower proved state.

**Evidence:**

- Initial RED proof:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixKernelUpstreamTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixKernelUpstreamTests/OrlixKernelUpstreamTests/testEnvironmentStateWritebackProbeCompletesThroughOrlixOSTerminalSession test`
  - Result: failed at compile time because `OrlixUpstreamTestRunSpec` had no
    `kernelEnvironmentStateWriteback` member.
- Wrong first implementation shape:
  - The first probe wrote under `/etc` while the focused kselftest root used
    `orlix.root=initramfs-only`.
  - Result: marker write, sync, and reread failed, while `/dev/vda` write
    rejection and `/dev/vdb` flush still passed.
- Corrected focused iOS Simulator proof:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixKernelUpstreamTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixKernelUpstreamTests/OrlixKernelUpstreamTests/testEnvironmentStateWritebackProbeCompletesThroughOrlixOSTerminalSession test`
  - Result: `** TEST SUCCEEDED **`.
  - Executed 1 test, 1 passed, 0 failed, 0 skipped.
  - Result bundle:
    `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_17-54-46-+0200.xcresult`.
  - Probe output included:
    - `ok 1 - writable state block mounts as ext4`
    - `ok 2 - environment state marker write succeeds`
    - `ok 3 - environment state marker sync succeeds`
    - `ok 4 - writable state block remounts after sync`
    - `ok 5 - environment state marker reread succeeds`
    - `ok 6 - immutable base block still rejects writes`
    - `ok 7 - writable state block still flushes writes`

**Explicit non-claims:**

- This does not prove cross-boot persistence of the same mutated environment
  state image across a fresh host process.
- This does not prove multiple live environments inside one already-running
  OrlixKernel.
- This does not prove product import-to-enter flow.
- This does not prove OCI Runtime Spec config parsing, lifecycle compliance,
  Linux runtime defaults, feature reporting, product `orlix run`, host-folder
  mounts, virtio-fs, networking, cgroups, or native performance.

**Current conclusion:**

- The next persistence step should build on this Linux substrate proof rather
  than encode Linux filesystem semantics in XCTest.
- The full cross-boot persistence checkpoint remains open.

### 2026-06-12 Current status reconciliation after active plan drift

**What happened:**

Reconciled the active plan against current source and test evidence. The stale
14-item remaining-work shape is superseded by the corrected execution order
below. Agents must not resume from older plan text that says no tar importer,
OCI importer, whiteout handling, overlay proof, materialized-root proof, or
oracle scaffold exists without checking the current repo.

This is plan and harness state only. It does not add OCI Runtime Spec behavior,
does not add product `orlix run`, does not add registry pull, and does not prove
multiple live environments inside one already-running OrlixKernel.

**Repo evidence paths:**

- `OrlixOS/Sources/Session/OrlixEnvironment.swift`
- `OrlixOS/Sources/Session/OrlixRootfsImport.swift`
- `OrlixOS/Sources/Session/OrlixOCIImageLayout.swift`
- `OrlixTestRunner/Tests/XCTest/OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests.swift`
- `tools/orlix-linux-oracle/README.md`

**Corrected remaining execution order:**

1. Reconcile active plan status.
   - Proof: `PLAN.md`, `IMPLEMENT.md`, and `GOAL.md` no longer send agents to already-proved work.
2. Refresh baseline iOS Simulator proofs.
   - Proof: focused OrlixOS and OrlixPTYRuntime tests for current materialized tar and OCI roots pass.
3. Add mandatory plan-context harness gate.
   - Proof: mutation before plan read is blocked, mutation after plan read is allowed.
4. Add active-plan update gate.
   - Proof: commit/push after mutation is blocked until active `IMPLEMENT.md` is updated.
5. Product-shaped named environment session API.
   - Proof: OrlixOS can select a named environment root and descriptor through a public app-facing session path.
6. Cross-boot writable state persistence.
   - Proof: mutate environment state, shut down, restart same environment, observe mutation persisted while base image stayed unchanged.
7. Product import-to-enter flow.
   - Proof: tar import and OCI layout import both create environments that can be entered without test-only wiring.
8. OCI Runtime config parser and schema validation.
   - Proof: minimal Linux `config.json` validates against pinned schemas and converts to Orlix descriptors.
9. OCI Runtime lifecycle model.
   - Proof: `create` prepares resources without executing, `start` executes, `state` reports correct status, `kill` sends Linux signal, `delete` removes created resources only.
10. OCI Linux runtime defaults.
    - Proof: fd policy and `/dev/fd`, `/dev/stdin`, `/dev/stdout`, `/dev/stderr` match `runtime-linux.md`.
11. OCI feature report.
    - Proof: generated feature JSON validates and does not overclaim.
12. Runtime host-folder mount backend.
    - Proof: Documents and security-scoped folders enter only as Linux mounts, not raw host paths.
13. virtio-fs.
    - Proof: host-backed folder appears through Linux-owned mount behavior and passes path/stat/open/rename/unlink tests.
14. Linux oracle expansion.
    - Proof: same fixture produces real-Linux and Orlix JSON results, comparator catches drift.
15. Native performance benchmark suite.
    - Proof: ELF launch, syscall round trip, file IO, pipe, PTY, futex, and process lifecycle benchmarks have repeatable iOS Simulator baselines.
16. Product `orlix run`.
    - Proof: argv/env/cwd/user/stdio/lifecycle/exit status work through Linux exec, not HostAdapter command execution.
17. Registry pull tooling.
    - Proof: registry pull produces the same verified OCI layout input as local layout import, with no OrlixKernel or iOS runtime dependency on Apple container.
18. Networking and namespace expansion.
    - Proof: virtio-net, `/proc/net`, rtnetlink, and staged network namespace behavior have focused tests.
19. Virtual cgroup v2 and resource accounting.
    - Proof: synthetic cgroup v2 tree and resource behavior match declared feature support.

**Current conclusion:**

- The active plan now separates proved image/import work from unproved OCI
  Runtime Spec config, feature, lifecycle, and Linux runtime behavior.
- The next implementation checkpoint is harness enforcement, followed by
  refreshed focused iOS Simulator proofs.

**Harness enforcement evidence:**

- Hook test command:
  - `rtk python3 -m unittest discover .codex/hooks/tests`
- Hook test result:
  - Ran 31 tests.
  - Result: OK.
- Rules test command:
  - `rtk python3 -m unittest discover .codex/rules/tests`
- Rules test result:
  - Ran 5 tests.
  - Result: OK.
- Compact plan check:
  - `rtk python3 .codex/hooks/compact_plan_check.py`
  - Result: exit code 0.
- Whitespace check:
  - `rtk git diff --check`
  - Result: exit code 0.
- Publish checkpoint:
  - `rtk git fetch --prune`
  - Result: fetched successfully after sandbox escalation for `.git/FETCH_HEAD`.
  - `rtk git log --oneline --decorate --left-right --graph HEAD...@{u}`
  - Result: no local/remote commit divergence before commit.
- Publish-gate regression:
  - Initial push after commit was blocked because the new guard treated the
    successful commit itself as a fresh mutation requiring another
    `IMPLEMENT.md` update before push.
  - Fixed the guard so `git commit` and `git push` still require loaded active
    plan context and still enforce the post-mutation `IMPLEMENT.md` gate, but
    a successful commit does not create a new implementation-log debt before
    push.
  - `rtk python3 -m unittest discover .codex/hooks/tests`
  - Result: ran 32 tests, OK.
  - `rtk python3 -m unittest discover .codex/rules/tests`
  - Result: ran 5 tests, OK.
  - `rtk python3 .codex/hooks/compact_plan_check.py`
  - Result: exit code 0.
  - `rtk git diff --check`
  - Result: exit code 0.

### 2026-06-12 File-backed rootfs tar import refuses overwrite before archive read

**What happened:**

Tightened file-backed rootfs tar import atomicity. The file-backed importer now
checks whether the destination environment already exists before reading
`archiveURL`. This keeps overwrite protection independent of external archive
availability and prevents a missing or unreadable archive from masking
`destinationExists`.

This is OrlixOS importer and environment-storage protection. It does not change
OrlixKernel Linux semantics, does not add OCI layout behavior, does not add
registry pull, and does not introduce Docker daemon behavior, runc, Apple
container/containerization, Virtualization.framework, or a VM runtime. The proof
target is iOS Simulator.

**Changes:**

- `OrlixOS/Sources/Session/OrlixRootfsImport.swift` shares destination
  availability validation between file-backed and data-backed tar imports.
- `OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift` adds
  `testRootfsTarImporterRefusesExistingEnvironmentBeforeReadingArchive`.

**Evidence:**

- Initial sandboxed focused iOS Simulator proof command:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarImporterReadsArchiveFromImportPlanURL -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarImporterRefusesExistingEnvironmentBeforeReadingArchive -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarImporterDoesNotOverwriteExistingEnvironment test`
- Initial sandboxed result:
  - Failed before test execution because CoreSimulator and SwiftPM cache access
    were denied.
- Elevated focused iOS Simulator proof command:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarImporterReadsArchiveFromImportPlanURL -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarImporterRefusesExistingEnvironmentBeforeReadingArchive -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarImporterDoesNotOverwriteExistingEnvironment test`
- Runtime result:
  - `** TEST SUCCEEDED **`
  - Executed 3 tests, 3 passed, 0 failed, 0 skipped.
  - Result bundle:
    `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_09-07-15-+0200.xcresult`.
- Exact result summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_09-07-15-+0200.xcresult`
- Result summary:
  - `result: Passed`
  - `totalTestCount: 3`
  - `passedTests: 3`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5, arm64.
- Crash check:
  - Latest `OrlixTestRunner` diagnostic report remains
    `/Users/rudironsoni/Library/Logs/DiagnosticReports/OrlixTestRunner-2026-06-12-032807.ips`.
  - No newer `OrlixTestRunner` crash report appeared after this focused proof.
- Whitespace check:
  - `rtk git diff --check -- OrlixOS/Sources/Session/OrlixRootfsImport.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
  - Result: exit code 0.
- Forbidden runtime dependency and wording scan:
  - `rtk rg -n "AppleContainer|apple/container|Containerization|Virtualization\\.framework|Virtualization|Process\\(|NSTask|Foundation\\.Process|aligned with macOS|aligned with macos|alligned with macos|alligned with macOS|macOS user|macOS runtime|IXLand|OrlixKit" OrlixOS/Sources/Session/OrlixRootfsImport.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift --glob '!Build/**'`
  - Result: no matches.

**Current conclusion:**

- File-backed tar import now preserves existing environment state before any
  archive read and proves that overwrite protection wins even when the planned
  archive path is missing.
- This does not prove command-line import UX, host-tool ext4 materialization as
  part of one import command, OCI layout import, registry pull, live runtime boot
  from a newly imported archive in the same command, or arbitrary imported binary
  compatibility.

### 2026-06-12 Tar-derived materialized root fixture proof refreshed

**What happened:**

Refreshed the proof that the environment image materialization path produces
current tar-derived root images and that OrlixOS can boot one through the
app-hosted iOS Simulator runtime.

No code change was made in this checkpoint. The useful evidence is that the
Make-owned ext4 fixture path and the OrlixOS runtime path are still connected:
the fixture gate reported current images, the iOS Simulator test booted the
tar-derived root, Linux mounted the base image read-only as `/dev/vda`, mounted
the state image read-write as `/dev/vdb`, reported `ORLIX-ROOT-OVERLAY-READY`,
started `/bin/sh`, and read `/etc/os-release` from the tar-derived environment.

This remains iOS Simulator runtime proof only. macOS is the development host for
Xcode, fixture generation, host-tool execution, and result-bundle inspection. It
is not the initial runtime target, not the Linux user model, and not a product
runtime surface.

**Evidence:**

- Fixture materialization gate:
  - `rtk timeout 1200 make -f OrlixOS/Makefile environment-runtime-proof-fixtures PROFILE=release`
  - Result: `Orlix environment runtime proof fixtures ready: /Users/rudironsoni/src/github/rudironsoni/orlix/OrlixSystem/Build/OrlixOS/environment-runtime-proof`.
- Initial sandboxed runtime proof command:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests/testTarDerivedMaterializedRootBootsAndExposesOSRelease test`
  - Result: failed before test execution because CoreSimulator and SwiftPM cache
    access were denied.
- Elevated focused iOS Simulator runtime proof command:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests/testTarDerivedMaterializedRootBootsAndExposesOSRelease test`
- Runtime result:
  - `** TEST SUCCEEDED **`
  - Executed 1 test, 1 passed, 0 failed, 0 skipped.
  - Result bundle:
    `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.12_09-03-55-+0200.xcresult`.
- Runtime evidence observed in the simulator output:
  - `virtio_blk virtio0: [vda] 131072 512-byte logical blocks`
  - `virtio_blk virtio1: [vdb] 65536 512-byte logical blocks`
  - `EXT4-fs (vda): mounted filesystem ... ro`
  - `EXT4-fs (vdb): mounted filesystem ... r/w`
  - `ORLIX-ROOT-OVERLAY-READY`
  - `orlix-init: runtime filesystems mounted`
  - `ORLIX_ENV_OS_RELEASE_BEGIN`
  - `ID=orlix-tar-runtime-proof`
  - `ORLIX_ENV_OS_RELEASE_DONE`
- Exact result summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.12_09-03-55-+0200.xcresult`
- Result summary:
  - `result: Passed`
  - `totalTestCount: 1`
  - `passedTests: 1`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5, arm64.
- Crash check:
  - Latest `OrlixTestRunner` diagnostic report remains
    `/Users/rudironsoni/Library/Logs/DiagnosticReports/OrlixTestRunner-2026-06-12-032807.ips`.
  - No newer `OrlixTestRunner` crash report appeared after this focused proof.

**Current conclusion:**

- The current tar-derived materialized-root fixture path is still live: generated
  environment images are current, and OrlixOS can boot a tar-derived environment
  through one upstream-Linux-based OrlixKernel on iOS Simulator.
- This does not prove command-line import UX, fresh file-backed tar import
  directly followed by host-tool ext4 materialization in one command, OCI layout
  runtime boot in this checkpoint, registry pull, real device behavior,
  namespace-local PID behavior, cgroups, networking, or arbitrary imported binary
  compatibility.

### 2026-06-12 File-backed rootfs tar import entrypoint

**What happened:**

Added the missing file-backed rootfs tar import entrypoint for the product-shaped
`orlix env import-tar <archive> <environment>` path. The existing importer
already accepted archive bytes and performed staging, manifest extraction,
metadata command generation, and descriptor persistence. The new entrypoint reads
the archive from the `OrlixRootfsTarImportPlan.archiveURL` and then reuses the
existing data-backed import path.

This is OrlixOS image-input and environment-materialization behavior. It does not
change OrlixKernel Linux semantics, does not add OCI layout import behavior, does
not add registry pull, and does not introduce Docker daemon behavior, runc,
Apple container/containerization, Virtualization.framework, or a VM runtime. The
proof target is iOS Simulator.

**Changes:**

- `OrlixOS/Sources/Session/OrlixRootfsImport.swift` adds
  `OrlixRootfsTarImporter.importArchive(using:registry:fileManager:)`.
- `OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift` adds
  `testRootfsTarImporterReadsArchiveFromImportPlanURL`.

**Evidence:**

- Initial sandboxed focused iOS Simulator proof command:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarImporterReadsArchiveFromImportPlanURL -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarImporterStagesRootfsAndPersistsEnvironmentDescriptor test`
- Initial sandboxed result:
  - Failed before test execution because CoreSimulator and SwiftPM cache access
    were denied.
- First elevated focused run:
  - Same command as above.
  - Result bundle:
    `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_08-58-05-+0200.xcresult`.
  - Result: `** TEST FAILED **`.
  - Failure was in the new test setup: the test attempted to write
    `alpine-rootfs.tar` under a temporary root directory before creating that
    directory.
- Corrected focused iOS Simulator proof command:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarImporterReadsArchiveFromImportPlanURL -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarImporterStagesRootfsAndPersistsEnvironmentDescriptor test`
- Corrected result:
  - `** TEST SUCCEEDED **`
  - Executed 2 tests, 2 passed, 0 failed, 0 skipped.
  - Result bundle:
    `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_08-59-58-+0200.xcresult`.
- Exact result summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_08-59-58-+0200.xcresult`
- Result summary:
  - `result: Passed`
  - `totalTestCount: 2`
  - `passedTests: 2`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5, arm64.
- Crash check:
  - Latest `OrlixTestRunner` diagnostic report remains
    `/Users/rudironsoni/Library/Logs/DiagnosticReports/OrlixTestRunner-2026-06-12-032807.ips`.
  - No newer `OrlixTestRunner` crash report appeared after this focused proof.
- Whitespace check:
  - `rtk git diff --check -- OrlixOS/Sources/Session/OrlixRootfsImport.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
  - Result: exit code 0.
- Forbidden runtime dependency and wording scan:
  - `rtk rg -n "AppleContainer|apple/container|Containerization|Virtualization\\.framework|Virtualization|Process\\(|NSTask|Foundation\\.Process|aligned with macOS|aligned with macos|alligned with macos|alligned with macOS|macOS user|macOS runtime|IXLand|OrlixKit" OrlixOS/Sources/Session/OrlixRootfsImport.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift --glob '!Build/**'`
  - Result: no matches.

**Current conclusion:**

- Rootfs tar import now has a file-backed API matching the plan shape. A named
  environment can be imported from a tar archive URL, staged under the import
  scratch layout, persisted under the environment descriptor registry, and
  materialized through the existing ext4 metadata-preparation path.
- This does not prove command-line UX, ext4 image command execution, live shell
  boot from a freshly imported tar in the same test, OCI layout import, OCI
  registry pull, or arbitrary imported binary compatibility.

### 2026-06-12 OCI User mixed group forms proof

**What happened:**

Added focused OCI layout importer tests for additional `config.User` forms that matter for running common images as-is:

- numeric UID plus named group, for example `1000:staff`;
- named user plus missing named group, for example `app:missing`.

The existing importer path already resolved the valid mixed form and rejected the invalid form, so no production code change was needed.

This is OrlixOS image-input and descriptor preparation behavior only. It does not change OrlixKernel Linux semantics, does not add registry pull, and does not introduce Docker daemon behavior, runc, Apple container/containerization, Virtualization.framework, or a VM runtime. The proof target is iOS Simulator.

**Changes:**

- `OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift` adds:
  - `testOCIImageLayoutImporterResolvesNamedGroupForNumericUser`;
  - `testOCIImageLayoutImporterRejectsMissingNamedGroup`.

**Evidence:**

- Initial focused iOS Simulator proof command for the two new tests:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterResolvesNamedGroupForNumericUser -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRejectsMissingNamedGroup test`
- Result:
  - `** TEST SUCCEEDED **`
  - Executed 2 tests, 2 passed, 0 failed, 0 skipped.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_06-45-56-+0200.xcresult`.
- Full focused OCI `User` import proof command:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterBindsNumericUserAndGroup -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterResolvesNamedUserFromImportedPasswd -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterResolvesNamedGroupFromImportedGroup -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterResolvesNamedGroupForNumericUser -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRejectsMissingNamedUser -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRejectsMissingNamedGroup test`
- Result:
  - `** TEST SUCCEEDED **`
  - Executed 6 tests, 6 passed, 0 failed, 0 skipped.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_06-46-53-+0200.xcresult`.

**Current conclusion:**

- OCI `config.User` descriptor binding now has iOS Simulator proof for numeric `uid:gid`, named user, named user plus named group, numeric UID plus named group, missing named user, and missing named group.
- This still does not prove supplementary groups, user namespaces, capabilities, registry pull, or full OCI Runtime Spec lifecycle behavior.

### 2026-06-12 OCI User metadata to descriptor proof

**What happened:**

Verified the current OCI layout importer maps OCI `config.User` into Orlix environment descriptor credentials before runtime launch. The importer supports numeric `uid:gid`, named users resolved from imported `/etc/passwd`, named groups resolved from imported `/etc/group`, and explicit failure for missing named users.

This is OrlixOS image-input and descriptor preparation behavior. It does not change OrlixKernel Linux semantics, does not add registry pull, and does not introduce Docker daemon behavior, runc, Apple container/containerization, Virtualization.framework, or a VM runtime. The proof target is iOS Simulator.

**Evidence:**

- Code path inspected:
  - `OrlixOS/Sources/Session/OrlixOCIImageLayout.swift` resolves OCI `User` values through `userAndGroup(...)`.
  - `OrlixLinuxAccountDatabase` reads imported rootfs `etc/passwd` and `etc/group`.
  - The result is written into `OrlixEnvironmentDescriptor.defaultUserID` and `defaultGroupID`.
- Focused iOS Simulator proof command:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterBindsNumericUserAndGroup -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterResolvesNamedUserFromImportedPasswd -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterResolvesNamedGroupFromImportedGroup -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRejectsMissingNamedUser test`
- Result:
  - `** TEST SUCCEEDED **`
  - Executed 4 tests, 4 passed, 0 failed, 0 skipped.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_06-43-42-+0200.xcresult`.

**Current conclusion:**

- OCI `User` metadata now has fresh proof for descriptor binding and fresh proof, from the adjacent runtime entry, that descriptor UID/GID reaches Linux-visible `/proc/self/status` after `setgid`, `setuid`, and `execve`.
- This does not prove supplementary groups, `/etc/shadow`, NSS behavior, user namespace mapping, capabilities, registry pull, or full OCI Runtime Spec lifecycle behavior.

### 2026-06-12 Descriptor UID/GID runtime proof stays iOS Simulator first

**What happened:**

Verified that OCI-derived environment descriptor UID/GID fields reach the Linux-facing runtime surface in iOS Simulator. The descriptor values are emitted through the OrlixOS kernel command line, read by guest-side `/init`, applied with Linux `setgid` and `setuid`, then observed from `/proc/self/status` inside the executed userspace command.

This is iOS Simulator runtime proof only. macOS is the development host for Xcode, fixture generation, and result-bundle inspection. It is not the initial runtime target, not the Linux user model, and not a product runtime surface.

**Evidence:**

- Code path inspected:
  - `OrlixOS/Sources/Session/OrlixEnvironment.swift` emits `orlix.uid` and `orlix.gid` into the materialized kernel command line.
  - `OrlixOS/Sources/init/init.c` reads those keys from `/proc/cmdline`, then calls `setgid`, `setuid`, `chdir`, and `execve` before running the selected command.
  - `OrlixTestRunner/Tests/XCTest/OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests.swift` asserts `/proc/self/status` contains `Uid:\t1000` and `Gid:\t100`.
- Initial sandboxed command failed before build/test execution because CoreSimulator and SwiftPM cache access were denied:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests/testOCIDerivedMaterializedRootBindsDescriptorExecutionDefaults -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests/testOCIDerivedMaterializedRootBindsLongDescriptorExecutionDefaults test`
- Re-run with required simulator access:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests/testOCIDerivedMaterializedRootBindsDescriptorExecutionDefaults -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests/testOCIDerivedMaterializedRootBindsLongDescriptorExecutionDefaults test`
  - Result: `** TEST SUCCEEDED **`
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.12_06-40-13-+0200.xcresult`.
  - Executed 2 tests, 1 passed, 1 skipped. The long descriptor case skipped because this harness permits one Orlix boot per XCTest app process.
- Long descriptor re-run as the only selected runtime proof:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests/testOCIDerivedMaterializedRootBindsLongDescriptorExecutionDefaults test`
  - Result: `** TEST SUCCEEDED **`
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.12_06-41-33-+0200.xcresult`.
  - Executed 1 test, 1 passed, 0 failed, 0 skipped.
- Observed Linux-facing markers in simulator output:
  - `Kernel command line: ... orlix.uid=1000 orlix.gid=100`
  - `argv0=orlix-descriptor-...`
  - `argv1=argument with spaces`
  - `env=descriptor value with spaces`
  - `pwd=/tmp`
  - `Uid:	1000	1000	1000	1000`
  - `Gid:	100	100	100	100`
  - `ORLIX_ENV_EXEC_DONE`

**Current conclusion:**

- Descriptor credential defaults are proven through Linux-visible state in iOS Simulator.
- This does not claim namespace-local user mapping, user namespaces, capability modeling, setgroups behavior, supplementary groups, OCI username resolution beyond the existing descriptor fields, or device runtime proof.

### 2026-06-12 Security-scoped external mount descriptor metadata

**What happened:**

Added explicit environment descriptor metadata for security-scoped external folder mounts. The source is an opaque bookmark identifier, not a host path, and the Linux-visible target remains an absolute Linux mount target.

This advances the storage-policy requirement that external folders must enter through explicit virtual mountpoints rather than raw host paths. It does not implement runtime host-folder mounting, virtio-fs, security-scoped bookmark resolution, HostAdapter filesystem operations, or OrlixKernel mount behavior. `OrlixEnvironmentRootImage.materialized(...)` still rejects explicit mounts until the Linux-compatible runtime mount path exists.

This is OrlixOS descriptor and validation behavior only. It does not change OrlixKernel Linux semantics, OrlixHostAdapter private host mechanics, OCI import behavior, Apple container usage, Virtualization.framework usage, or the iOS runtime dependency graph.

**Changes:**

- `OrlixOS/Sources/Session/OrlixEnvironment.swift` adds `OrlixEnvironmentMountSource.securityScopedExternal(bookmarkID:)`.
- `OrlixEnvironmentMount.securityScopedExternal(bookmarkID:targetPath:readOnly:)` validates:
  - bookmark identifiers are opaque storage IDs, not host paths;
  - Linux mount targets are absolute;
  - reserved Linux runtime targets such as `/`, `/proc`, `/dev`, `/sys`, `/run`, and `/tmp` are rejected.
- `OrlixEnvironmentMountError.invalidSourceIdentifier` records invalid external source identifiers.
- `OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift` adds focused tests for external mount metadata, invalid source identifiers, reserved Linux targets, Codable round-trip, and root-image materialization rejection.

**Evidence:**

- Exact focused iOS Simulator proof command:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testSecurityScopedExternalMountIsExplicitEnvironmentDescriptorMetadata -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testSecurityScopedExternalMountRejectsUnsafeSourceIdentifiers -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testSecurityScopedExternalMountRejectsReservedLinuxRuntimeTargets -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentRootImageRejectsUnimplementedExplicitMounts test`
- Result:
  - Completed with exit code 0.
  - `** TEST SUCCEEDED **`
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_06-36-07-+0200.xcresult`.
- Exact result summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_06-36-07-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 4`
  - `passedTests: 4`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5, arm64.
- Crash check:
  - Latest `OrlixTestRunner` diagnostic report remains `/Users/rudironsoni/Library/Logs/DiagnosticReports/OrlixTestRunner-2026-06-12-032807.ips`.
  - No newer `OrlixTestRunner` crash report appeared after this focused proof.
- Whitespace check:
  - `rtk git diff --check -- OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
  - Result: exit code 0.
- Runtime dependency and host-process scan:
  - `rtk rg -n "AppleContainer|apple/container|Containerization|Virtualization\\.framework|Virtualization|Process\\(|NSTask|Foundation\\.Process" OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift OrlixKernel/Sources/ports/orlix OrlixHostAdapter/Sources project.yml --glob '!Build/**'`
  - Result: no matches.

**Current conclusion:**

- Orlix environment descriptors can now represent both app Documents and external security-scoped folder mounts explicitly, without storing raw host paths as Linux truth.
- Runtime host-folder support remains pending. The next aligned step is to implement the Linux-compatible mount path below Linux VFS semantics, likely through a virtio-fs-shaped device/backend path, while keeping HostAdapter as private security-scoped host mediation.

### 2026-06-12 Linux-only oracle fixture runner guard

**What happened:**

Added a real-Linux fixture runner command to the Mac-only oracle tool while preserving the iOS runtime boundary. The new command executes a prebuilt fixture binary only when the tool is running inside Linux, converts the fixture stdout into the normal `OracleResult` JSON contract, and refuses to run on non-Linux hosts.

This is oracle tooling only. It does not change OrlixKernel, OrlixOS runtime behavior, OrlixHostAdapter, the iOS app target, Apple container usage, Virtualization.framework usage, OCI import, or Linux syscall behavior.

**Changes:**

- `tools/orlix-linux-oracle/orlix-linux-oracle.swift` adds:
  - `linux-result-from-fixture --case <case.json> --fixture <binary> --workdir <dir> --output <result.json>`
  - a Linux-host guard using Swift platform compilation.
  - shared path-errno JSON event parsing for Linux fixture stdout and Orlix kselftest logs.
- `tools/orlix-linux-oracle/README.md` documents the Linux-only fixture runner and states that macOS may use it only through an external Linux runner such as a developer VM, Linux CI job, or future external Apple-container-based oracle runner.

**Evidence:**

- Oracle case validation:
  - `rtk swift tools/orlix-linux-oracle/orlix-linux-oracle.swift validate-case tools/orlix-linux-oracle/cases/path-errno.json`
  - Result: `case path-errno is valid`.
- Static sample comparison:
  - `rtk swift tools/orlix-linux-oracle/orlix-linux-oracle.swift compare --case tools/orlix-linux-oracle/cases/path-errno.json --linux-result tools/orlix-linux-oracle/samples/path-errno.linux.json --orlix-result tools/orlix-linux-oracle/samples/path-errno.orlix.json`
  - Result: `case path-errno matches`.
- macOS refusal proof for the Linux-only runner:
  - `rtk swift tools/orlix-linux-oracle/orlix-linux-oracle.swift linux-result-from-fixture --case tools/orlix-linux-oracle/cases/path-errno.json --fixture /oracle/path_errno_probe --workdir /oracle/work --output /tmp/path-errno.linux.generated.json`
  - Result: exit code 2.
  - Output: `unsupported host: linux-result-from-fixture must run inside a real Linux environment`.
- Orlix sample log extraction after refactor:
  - `rtk swift tools/orlix-linux-oracle/orlix-linux-oracle.swift orlix-result-from-log --case tools/orlix-linux-oracle/cases/path-errno.json --log tools/orlix-linux-oracle/samples/path-errno.orlix-kselftest.log --output /tmp/path-errno.orlix.generated.json`
  - Result: `wrote Orlix result for case path-errno: /tmp/path-errno.orlix.generated.json`.
- Extracted Orlix result comparison:
  - `rtk swift tools/orlix-linux-oracle/orlix-linux-oracle.swift compare --case tools/orlix-linux-oracle/cases/path-errno.json --linux-result tools/orlix-linux-oracle/samples/path-errno.linux.json --orlix-result /tmp/path-errno.orlix.generated.json`
  - Result: `case path-errno matches`.

**Current conclusion:**

- The oracle tool now has the missing command shape for producing a fresh Linux result from a fixture, but it cannot be executed on this macOS development host by design.
- A later checkpoint still needs to run this command inside an actual Linux runner and compare that fresh result against an extracted iOS Simulator Orlix result.

### 2026-06-12 Full Orlix kselftest gate restored on iOS Simulator

**What happened:**

Rechecked the earlier full-suite blocker after the focused `clone_thread_probe` and path-errno work. The current app-hosted iOS Simulator full Orlix kselftest gate now runs through `ORLIX-KSELFTEST-END` and passes.

No code change was required in this checkpoint. The important correction is evidence: the prior log entry that recorded a full-suite crash at `clone_thread_probe` is now stale for the current worktree. The focused clone-thread proof passes, and the full kselftest rootfs proof passes in the same iOS Simulator target.

This remains iOS Simulator runtime proof only. macOS is the development host for Xcode, result-bundle inspection, and crash-report inspection. macOS is not the initial runtime target, not the Linux user model, and not a product runtime surface.

**Evidence:**

- Focused clone-thread proof command:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixKernelUpstreamTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixKernelUpstreamTests/OrlixKernelUpstreamTests/testCloneThreadProbeCompletesThroughOrlixOSTerminalSession test`
- Focused clone-thread result:
  - Completed with exit code 0.
  - `** TEST SUCCEEDED **`
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_06-27-13-+0200.xcresult`.
- Focused clone-thread runtime evidence observed in the simulator output:
  - `Kernel command line: console=ttyS0 console=hvc0 rdinit=/init orlix.root=initramfs-only orlix.profile=release orlix.kselftest=clone_thread_probe`
  - `# exec /orlix/clone_thread_probe`
  - `ok 1 - mlibc-shaped clone thread stack runs through Linux clone`
  - `ok 2 - mlibc-shaped clone TLS and futex join handshake completes`
  - `ok 3 - mlibc-shaped clone stack supports deep alloca faults`
  - `ok 5 - clone_thread_probe`
  - `ORLIX-KSELFTEST-END`
- Full kselftest proof command:
  - `rtk timeout 1200 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixKernelUpstreamTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixKernelUpstreamTests/OrlixKernelUpstreamTests/testKselftestRootfsCompletesThroughOrlixOSTerminalSession test`
- Full kselftest result:
  - Completed with exit code 0.
  - `** TEST SUCCEEDED **`
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_06-28-49-+0200.xcresult`.
- Runtime evidence observed in the full-suite simulator output:
  - `Kernel command line: console=ttyS0 console=hvc0 rdinit=/init orlix.root=initramfs-only orlix.profile=release`
  - `1..15`
  - `ok 5 - boot_profile_contract`
  - `ok 13 - tls_syscall_probe`
  - `ok 14 - virtio_blk_environment_probe`
  - `ok 15 - virtio_mmio_probe_contract`
  - `ORLIX-KSELFTEST-END`
- Exact result summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_06-28-49-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 1`
  - `passedTests: 1`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5, arm64.
- Crash check:
  - Latest `OrlixTestRunner` diagnostic report remains `/Users/rudironsoni/Library/Logs/DiagnosticReports/OrlixTestRunner-2026-06-12-032807.ips`.
  - No newer `OrlixTestRunner` crash report appeared after the focused clone-thread proof or the full kselftest proof.
- Generated output status:
  - `Build/OrlixOS`, `Build/OrlixKernel`, and `Build/OrlixMLibC` have no tracked git status after the proof run.

**Current conclusion:**

- The current worktree has a passing full app-hosted Orlix kselftest gate on iOS Simulator.
- This restores evidence for the shared Linux execution substrate needed by OCI/container-image-derived environments: clone-thread stack setup, TLS/futex join behavior, mount namespace proof, path errno proof, virtio block proof, virtio-mmio proof, and virtio-rng proof all appear in the current full kselftest run.
- This still does not prove live real-Linux oracle comparison, OCI registry pull, full OCI Runtime Spec lifecycle, networking, cgroups, security-scoped host-folder mounts, real-device iOS execution, or App Store acceptance.

### 2026-06-12 Oracle-marked path errno result extraction

**What happened:**

Added a deterministic bridge from the iOS Simulator kselftest output to the Mac-only Linux oracle comparator contract for the first path errno case.

The `path_errno_probe` kselftest now emits an oracle-marked JSON block around the errno observations it proves through OrlixKernel on iOS Simulator. The oracle tool can extract that block from an Orlix kselftest log and write a normal `OracleResult` JSON file for comparison against a real-Linux result file.

This remains an iOS Simulator runtime proof. macOS is only the development host for compiler checks, result-bundle inspection, sample oracle extraction, and future real-Linux oracle tooling. It is not the initial runtime target, not the Linux user model, and not a product runtime surface.

**Changes:**

- `OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/path_errno_probe.c` now emits:
  - `ORLIX-ORACLE-BEGIN path-errno`
  - JSON errno events for missing path, non-directory child lookup, symlink loop, and trailing slash on regular file.
  - `ORLIX-ORACLE-END path-errno`
- `OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift` now asserts the oracle markers and JSON fragments are present in the app-hosted iOS Simulator output.
- `tools/orlix-linux-oracle/orlix-linux-oracle.swift` now supports `orlix-result-from-log --case <case.json> --log <log.txt> --output <result.json>` for the `path-errno` case.
- `tools/orlix-linux-oracle/fixtures/path_errno_probe.c` and sample result files now use `stat("regular/")` for the trailing-slash ENOTDIR check, matching the Orlix kselftest probe.
- `tools/orlix-linux-oracle/samples/path-errno.orlix-kselftest.log` captures a minimal oracle-marked Orlix kselftest log sample.
- `tools/orlix-linux-oracle/README.md` documents the Orlix log extraction command.

**Evidence:**

- Host syntax check for the Orlix kselftest source:
  - `rtk xcrun --sdk macosx clang -fsyntax-only -Wall -Wextra -Werror -Wno-unused-function OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/path_errno_probe.c`
  - Result: exit code 0.
- Host syntax check for the Linux oracle fixture:
  - `rtk xcrun --sdk macosx clang -fsyntax-only -Wall -Wextra -Werror tools/orlix-linux-oracle/fixtures/path_errno_probe.c`
  - Result: exit code 0.
- Oracle case validation:
  - `rtk swift tools/orlix-linux-oracle/orlix-linux-oracle.swift validate-case tools/orlix-linux-oracle/cases/path-errno.json`
  - Result: `case path-errno is valid`.
- Orlix sample log extraction:
  - `rtk swift tools/orlix-linux-oracle/orlix-linux-oracle.swift orlix-result-from-log --case tools/orlix-linux-oracle/cases/path-errno.json --log tools/orlix-linux-oracle/samples/path-errno.orlix-kselftest.log --output /tmp/path-errno.orlix.generated.json`
  - Result: exit code 0.
- Static sample comparison:
  - `rtk swift tools/orlix-linux-oracle/orlix-linux-oracle.swift compare --case tools/orlix-linux-oracle/cases/path-errno.json --linux-result tools/orlix-linux-oracle/samples/path-errno.linux.json --orlix-result tools/orlix-linux-oracle/samples/path-errno.orlix.json`
  - Result: `case path-errno matches`.
- Extracted sample comparison:
  - `rtk swift tools/orlix-linux-oracle/orlix-linux-oracle.swift compare --case tools/orlix-linux-oracle/cases/path-errno.json --linux-result tools/orlix-linux-oracle/samples/path-errno.linux.json --orlix-result /tmp/path-errno.orlix.generated.json`
  - Result: `case path-errno matches`.
- Focused iOS Simulator proof command:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixKernelUpstreamTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixKernelUpstreamTests/OrlixKernelUpstreamTests/testPathErrnoProbeCompletesThroughOrlixOSTerminalSession test`
- Focused iOS Simulator result:
  - Completed with exit code 0.
  - `** TEST SUCCEEDED **`
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_06-22-06-+0200.xcresult`.
- Runtime evidence observed in the simulator output:
  - `ORLIX-ORACLE-BEGIN path-errno`
  - `{"operation":"open","path":"missing","errno":2,"name":"No such file or directory","expected":2}`
  - `{"operation":"open","path":"regular/child","errno":20,"name":"Not a directory","expected":20}`
  - `{"operation":"stat","path":"loop-a","errno":40,"name":"Too many levels of symbolic links","expected":40}`
  - `{"operation":"stat","path":"regular/","errno":20,"name":"Not a directory","expected":20}`
  - `ORLIX-ORACLE-END path-errno`
  - `ok 5 - path_errno_probe`
  - `ORLIX-KSELFTEST-END`
- Exact result summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_06-22-06-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 1`
  - `passedTests: 1`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5, arm64.
- Crash check:
  - Latest `OrlixTestRunner` diagnostic report remains `/Users/rudironsoni/Library/Logs/DiagnosticReports/OrlixTestRunner-2026-06-12-032807.ips`.
  - No newer `OrlixTestRunner` crash report appeared after this focused proof.
- Generated output status:
  - `Build/OrlixOS`, `Build/OrlixKernel`, and `Build/OrlixMLibC` have no tracked git status after the proof run.

**Current conclusion:**

- The first path errno proof now has an app-hosted iOS Simulator runtime signal and a deterministic extraction path into the oracle result contract.
- This still does not run the real-Linux fixture live. The current comparator evidence uses the checked-in Linux sample result and extracted Orlix sample log. The next oracle checkpoint should run the same fixture on a real Linux target and compare that live output against extracted iOS Simulator output.
- This does not prove OCI import, rootfs tar import, mount namespace isolation, overlay behavior, registry pull, networking, cgroups, real-device iOS execution, or App Store acceptance.

### 2026-06-12 Kselftest payload freshness guard for iOS Simulator proofs

**What happened:**

Added a freshness guard to the `OrlixTestRunner` upstream-test bundle embed path so focused iOS Simulator kselftest proofs do not silently reuse stale kselftest initramfs bundles after Orlix selftest source changes.

The prior `path_errno_probe` proof exposed this issue: the test source existed in the live tree, but the installed kselftest list and packaged initramfs were stale, so the first simulator run failed before the new probe executed. The guard now rebuilds the kselftest bundle when the kselftest initramfs archive is missing, when Orlix selftest `.c`, `.h`, or `Makefile` inputs are newer than the archive, or when the kbuild rule is newer than the archive.

This is an iOS Simulator proof-path fix only. It does not change OrlixKernel Linux semantics, OrlixOS runtime behavior, HostAdapter behavior, OCI import behavior, or the product runtime surface.

**Changes:**

- `project.yml` now makes the `OrlixTestRunner` upstream-test bundle embed script call `make -f "$SRCROOT/OrlixKernel/Makefile" kselftest PROFILE="$profile" libc=orlixmlibc` when the kselftest initramfs is missing or stale.
- Regenerated `OrlixSystem.xcodeproj` with `rtk xcodegen generate`.

**Evidence:**

- Exact project generation command:
  - `rtk xcodegen generate`
- Result:
  - Completed with exit code 0.
  - Output: `Created project at /Users/rudironsoni/src/github/rudironsoni/orlix/OrlixSystem/OrlixSystem.xcodeproj`.
- Exact focused iOS Simulator proof command:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixKernelUpstreamTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixKernelUpstreamTests/OrlixKernelUpstreamTests/testPathErrnoProbeCompletesThroughOrlixOSTerminalSession test`
- Result:
  - Completed with exit code 0.
  - `** TEST SUCCEEDED **`
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_06-15-10-+0200.xcresult`.
- Runtime evidence observed in the simulator output:
  - `Kernel command line: console=ttyS0 console=hvc0 rdinit=/init orlix.root=initramfs-only orlix.profile=release orlix.kselftest=path_errno_probe`
  - `ok 4 - installed Orlix kselftest list is readable`
  - `# exec /orlix/path_errno_probe`
  - `# child exec /orlix/path_errno_probe`
  - `ok 1 - path errno fixture created through Linux VFS`
  - `ok 2 - missing path returns ENOENT`
  - `ok 3 - non-directory child returns ENOTDIR`
  - `ok 4 - symlink loop returns ELOOP`
  - `ok 5 - trailing slash on regular file returns ENOTDIR`
  - `ok 6 - path errno fixture cleaned`
  - `ok 5 - path_errno_probe`
  - `ORLIX-KSELFTEST-END`
- Exact result summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_06-15-10-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 1`
  - `passedTests: 1`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5, arm64.
- Crash check:
  - Latest `OrlixTestRunner` diagnostic report remains `/Users/rudironsoni/Library/Logs/DiagnosticReports/OrlixTestRunner-2026-06-12-032807.ips`.
  - No newer `OrlixTestRunner` crash report appeared after this focused proof.
- Generated output status:
  - `Build/OrlixOS`, `Build/OrlixKernel`, and `Build/OrlixMLibC` have no tracked git status after the proof run.

**Current conclusion:**

- The focused kselftest proof path now has a durable guard against stale Orlix selftest source inputs in the iOS Simulator target.
- This does not prove all generated-test bundle freshness cases. It specifically guards Orlix kselftest source inputs and the kbuild rule used by the current upstream-kernel XCTest path.

### 2026-06-12 Focused Linux path errno selftest proof on iOS Simulator

**What happened:**

Added a focused Linux-owned kselftest for path-resolution errno behavior and proved it through the app-hosted iOS Simulator runner.

The test covers missing paths, non-directory traversal, symlink loop resolution, trailing slash on a regular file, and fixture cleanup through normal Linux syscalls. This targets Linux VFS/path-walk semantics that OCI-derived and tar-derived environments depend on.

This is an iOS Simulator runtime proof only. macOS is the development host for Xcode, fixture generation, result-bundle inspection, and future Linux oracle tooling. It is not the initial runtime target, not the Linux user model, and not a product runtime surface.

**Changes:**

- `OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/path_errno_probe.c` adds the Linux kselftest probe.
- `OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/Makefile` installs `path_errno_probe`.
- `OrlixTestRunner/Sources/OrlixUpstreamTestRunner.swift` exposes `kernelPathErrno` with `orlix.kselftest=path_errno_probe`.
- `OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift` adds `testPathErrnoProbeCompletesThroughOrlixOSTerminalSession`.

**Evidence:**

- Host syntax check:
  - `rtk xcrun --sdk macosx clang -fsyntax-only -Wall -Wextra -Werror -Wno-unused-function OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/path_errno_probe.c`
- Result:
  - Completed with exit code 0.
- Initial focused iOS Simulator proof command:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixKernelUpstreamTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixKernelUpstreamTests/OrlixKernelUpstreamTests/testPathErrnoProbeCompletesThroughOrlixOSTerminalSession test`
- Initial result:
  - Failed before `path_errno_probe` ran.
  - Failure marker: `not ok 4 - installed Orlix kselftest list is readable`.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_06-03-49-+0200.xcresult`.
- Diagnosis:
  - `Build/OrlixMLibC/kselftest/release/kselftest-list.txt` and `Build/OrlixMLibC/test-initramfs/release/OrlixTestInitramfs.bundle/initramfs.list` were stale and did not include `path_errno_probe`.
- Explicit kselftest rebuild command:
  - `rtk timeout 1200 make -f OrlixKernel/Makefile kselftest PROFILE=release libc=orlixmlibc`
- Rebuild result:
  - Completed with exit code 0.
  - Output included `CC       path_errno_probe`.
  - Output included `packaged kselftest initramfs: /Users/rudironsoni/src/github/rudironsoni/orlix/OrlixSystem/Build/OrlixMLibC/test-initramfs/release/OrlixTestInitramfs.bundle (libc orlixmlibc)`.
- Payload check:
  - `Build/OrlixMLibC/kselftest/release/kselftest-list.txt` contains `orlix:path_errno_probe`.
  - `Build/OrlixMLibC/test-initramfs/release/OrlixTestInitramfs.bundle/initramfs.list` contains `file /orlix/path_errno_probe ...`.
- Final focused iOS Simulator proof command:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixKernelUpstreamTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixKernelUpstreamTests/OrlixKernelUpstreamTests/testPathErrnoProbeCompletesThroughOrlixOSTerminalSession test`
- Final result:
  - Completed with exit code 0.
  - `** TEST SUCCEEDED **`
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_06-10-54-+0200.xcresult`.
- Runtime evidence observed in the simulator output:
  - `Kernel command line: console=ttyS0 console=hvc0 rdinit=/init orlix.root=initramfs-only orlix.profile=release orlix.kselftest=path_errno_probe`
  - `ok 4 - installed Orlix kselftest list is readable`
  - `# exec /orlix/path_errno_probe`
  - `# child exec /orlix/path_errno_probe`
  - `ok 1 - path errno fixture created through Linux VFS`
  - `ok 2 - missing path returns ENOENT`
  - `ok 3 - non-directory child returns ENOTDIR`
  - `ok 4 - symlink loop returns ELOOP`
  - `ok 5 - trailing slash on regular file returns ENOTDIR`
  - `ok 6 - path errno fixture cleaned`
  - `ok 5 - path_errno_probe`
  - `ORLIX-KSELFTEST-END`
- Exact result summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_06-10-54-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 1`
  - `passedTests: 1`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5, arm64.
- Crash check:
  - Latest `OrlixTestRunner` diagnostic report remains `/Users/rudironsoni/Library/Logs/DiagnosticReports/OrlixTestRunner-2026-06-12-032807.ips`.
  - No newer `OrlixTestRunner` crash report appeared after this focused proof.
- Generated output status:
  - `Build/OrlixOS`, `Build/OrlixKernel`, and `Build/OrlixMLibC` have no tracked git status after the proof run.

**Current conclusion:**

- Orlix now has a focused, app-hosted iOS Simulator proof for a first Linux path-resolution errno slice through OrlixKernel, using Linux syscalls and upstream-style kselftest packaging.
- The proof also exposed a stale kselftest payload dependency problem. The immediate proof used an explicit `make kselftest` rebuild. A later cleanup should make the Xcode proof path fail earlier or rebuild automatically when Orlix selftest sources change.
- This does not prove the full Linux path-walk matrix, `openat2`, `renameat`, `linkat`, imported OCI environment path behavior, real-Linux oracle comparison, registry pull, networking, cgroups, real device execution, App Store acceptance, or macOS as a runtime target.

### 2026-06-12 Mac-only Linux oracle comparator scaffold

**What happened:**

Added the first Mac-only Linux oracle artifact under `tools/orlix-linux-oracle`. This creates a concrete case/result contract and a deterministic comparator for real-Linux versus Orlix result files.

This is tooling only. It is not part of the iOS runtime, does not link Apple container, Apple containerization, or Virtualization.framework into Orlix, and does not change OrlixKernel, OrlixHostAdapter, OrlixOS runtime behavior, Linux syscall behavior, or VFS semantics.

**Changes:**

- `tools/orlix-linux-oracle/README.md` documents the oracle contract, commands, and dependency boundary.
- `tools/orlix-linux-oracle/orlix-linux-oracle.swift` validates case files and compares captured result files.
- `tools/orlix-linux-oracle/cases/path-errno.json` defines the first path-resolution errno comparison case.
- `tools/orlix-linux-oracle/fixtures/path_errno_probe.c` provides the C fixture for missing path, non-directory traversal, symlink loop, and trailing slash errno behavior.
- `tools/orlix-linux-oracle/samples/path-errno.linux.json` and `path-errno.orlix.json` provide matching sample captures.
- `tools/orlix-linux-oracle/samples/path-errno.orlix-drift.json` provides a deliberate drift sample for comparator failure proof.

**Evidence:**

- Exact case validation command:
  - `rtk swift tools/orlix-linux-oracle/orlix-linux-oracle.swift validate-case tools/orlix-linux-oracle/cases/path-errno.json`
- Result:
  - Completed with exit code 0.
  - Output: `case path-errno is valid`.
- Exact matching comparison command:
  - `rtk swift tools/orlix-linux-oracle/orlix-linux-oracle.swift compare --case tools/orlix-linux-oracle/cases/path-errno.json --linux-result tools/orlix-linux-oracle/samples/path-errno.linux.json --orlix-result tools/orlix-linux-oracle/samples/path-errno.orlix.json`
- Result:
  - Completed with exit code 0.
  - Output: `case path-errno matches`.
- Exact fixture syntax command:
  - `rtk xcrun --sdk macosx clang -fsyntax-only -Wall -Wextra -Werror tools/orlix-linux-oracle/fixtures/path_errno_probe.c`
- Result:
  - Completed with exit code 0.
- Exact drift comparison command:
  - `rtk swift tools/orlix-linux-oracle/orlix-linux-oracle.swift compare --case tools/orlix-linux-oracle/cases/path-errno.json --linux-result tools/orlix-linux-oracle/samples/path-errno.linux.json --orlix-result tools/orlix-linux-oracle/samples/path-errno.orlix-drift.json`
- Result:
  - Completed with exit code 2, as expected for semantic drift.
  - Output reported `stdout differs`, `exitStatus differs`, `errnoEvents differ`, and `mutations differ`.

**Current conclusion:**

- Orlix now has the first concrete Linux oracle comparison contract and a deterministic comparator artifact. The first case targets Linux path-resolution errno behavior, which is directly relevant to imported rootfs and OCI-derived environment correctness.
- This does not yet run Apple container, another real Linux runner, or Orlix automatically. It does not produce fresh real-Linux or Orlix captures. It does not prove current Orlix path errno behavior, OCI runtime behavior, registry pull, host-folder mounts, networking, cgroups, App Store acceptance, or macOS as a runtime target.

### 2026-06-12 Focused environment-entry selftest proof on iOS Simulator

**What happened:**

Added a focused upstream-style XCTest for the existing `environment_entry_probe` kselftest path and proved it through the app-hosted iOS Simulator runner.

The probe exercises Linux-owned environment entry primitives directly: `fork`, `unshare(CLONE_NEWNS)`, `mount("tmpfs")`, copied executable entry, `chroot`, `chdir`, `execve`, parent `waitpid`, and parent-visible isolation after the child exits. This is not an OCI image runtime by itself, but it is the Linux mechanism proof needed before treating named environments as more than boot-time root-image selection.

This is an iOS Simulator runtime proof only. macOS is the development host for Xcode, fixture generation, and result-bundle inspection. It is not the initial runtime target, not the Linux user model, and not a product runtime surface.

**Changes:**

- `OrlixTestRunner/Sources/OrlixUpstreamTestRunner.swift` now exposes a focused `kernelEnvironmentEntry` run spec with `orlix.kselftest=environment_entry_probe`.
- `OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift` now has `testEnvironmentEntryProbeCompletesThroughOrlixOSTerminalSession`.

**Evidence:**

- Exact iOS Simulator proof command:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixKernelUpstreamTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixKernelUpstreamTests/OrlixKernelUpstreamTests/testEnvironmentEntryProbeCompletesThroughOrlixOSTerminalSession test`
- Result:
  - Completed with exit code 0.
  - `** TEST SUCCEEDED **`
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_05-51-00-+0200.xcresult`.
- Runtime evidence observed in the simulator output:
  - `Kernel command line: console=ttyS0 console=hvc0 rdinit=/init orlix.root=initramfs-only orlix.profile=release orlix.kselftest=environment_entry_probe`
  - `# exec /orlix/environment_entry_probe`
  - `# child exec /orlix/environment_entry_probe`
  - `TAP version 13`
  - `ok 1 - environment entry parent marker created`
  - `ok 2 - environment entry child started`
  - `ok 3 - environment entry child exited cleanly`
  - `ok 4 - environment entry root is hidden from parent`
  - `ok 5 - environment entry parent marker cleaned`
  - `ok 5 - environment_entry_probe`
  - `ORLIX-KSELFTEST-END`
- Exact result summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_05-51-00-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 1`
  - `passedTests: 1`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5, arm64.
- Crash check:
  - Latest `OrlixTestRunner` diagnostic report remains `/Users/rudironsoni/Library/Logs/DiagnosticReports/OrlixTestRunner-2026-06-12-032807.ips`.
  - No newer `OrlixTestRunner` crash report appeared after this focused proof.
- Generated output status:
  - `Build/OrlixOS` and `Build/OrlixKernel` have no tracked git status after the proof run.

**Current conclusion:**

- Orlix now has a focused, app-hosted iOS Simulator proof that upstream Linux mechanisms can enter an isolated child root through mount namespace plus chroot and execute a Linux binary there, with parent isolation verified after wait.
- This does not prove named environment switching inside one already-running OrlixKernel, OCI-derived root binding through this exact path, host-folder mounts, network namespaces, cgroups, real device execution, registry pull, systemd images, App Store acceptance, or macOS as a runtime target.

### 2026-06-12 Refreshed copied named environment runtime binding proof on iOS Simulator

**What happened:**

Ran the focused app-hosted runtime proof that boots a copied named environment on iOS Simulator, reaches the copied tar-derived root through the normal OrlixOS session path, and verifies `/etc/os-release` from inside the Linux userspace shell.

This is an iOS Simulator runtime proof only. macOS is the development host for Xcode, fixture generation, and result-bundle inspection. It is not the initial runtime target, not the Linux user model, and not a product runtime surface.

**Evidence:**

- Exact iOS Simulator proof command:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests/testCopiedNamedEnvironmentMaterializedRootBootsAndExposesOSRelease test`
- Result:
  - Completed with exit code 0.
  - `** TEST SUCCEEDED **`
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.12_05-43-09-+0200.xcresult`.
- Runtime markers observed in the simulator output:
  - `ORLIX_ENV_OS_RELEASE_BEGIN`
  - `ID=orlix-tar-runtime-proof`
  - `ORLIX_ENV_OS_RELEASE_DONE`
- Runtime boot evidence observed in the simulator output:
  - `Kernel command line: console=ttyS0 console=hvc0 rdinit=/init orlix.root=overlay orlix.profile=development orlix.exec=/bin/sh orlix.argv0=/bin/sh orlix.env0=HOME=/root orlix.env1=PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin orlix.env2=TERM=xterm-256color orlix.cwd=/ orlix.uid=0 orlix.gid=0`
  - `virtio_blk virtio0: [vda] 131072 512-byte logical blocks (67.1 MB/64.0 MiB)`
  - `virtio_blk virtio1: [vdb] 65536 512-byte logical blocks (33.6 MB/32.0 MiB)`
  - `EXT4-fs (vda): mounted filesystem 00000000-0000-0000-0000-000000000000 ro with ordered data mode. Quota mode: disabled.`
  - `EXT4-fs (vdb): mounted filesystem 00000000-0000-0000-0000-000000000000 r/w with ordered data mode. Quota mode: disabled.`
  - `ORLIX-ROOT-OVERLAY-READY`
- Exact result summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.12_05-43-09-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 1`
  - `passedTests: 1`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5, arm64.
- Crash check:
  - Latest `OrlixTestRunner` diagnostic report remains `/Users/rudironsoni/Library/Logs/DiagnosticReports/OrlixTestRunner-2026-06-12-032807.ips`.
  - No newer `OrlixTestRunner` crash report appeared after this focused proof.
- Generated output status:
  - `Build/OrlixOS` has no tracked git status after the proof run.

**Current conclusion:**

- Copied named environment materialization has refreshed iOS Simulator proof that the copied tar-derived root is bound into the current OrlixOS runtime proof path and observed from Linux userspace through `/etc/os-release`.
- This does not prove one-kernel environment switching, namespace-local environment entry, multiple live environments in one kernel, real device execution, host-folder mounts, external security-scoped mounts, OCI registry pull, systemd images, App Store acceptance, or macOS as a runtime target.

### 2026-06-12 Refreshed OCI-derived pseudo-filesystem proof on iOS Simulator

**What happened:**

Ran the focused app-hosted runtime proof that boots the OCI-derived materialized environment on iOS Simulator, reaches the imported root through the normal OrlixOS session path, and verifies a Linux-visible pseudo-filesystem baseline from inside the shell.

This is an iOS Simulator runtime proof only. macOS is the development host for Xcode, fixture generation, and result-bundle inspection. It is not the initial runtime target, not the Linux user model, and not a product runtime surface.

**Evidence:**

- Exact iOS Simulator proof command:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests/testOCIDerivedMaterializedRootExposesLinuxPseudoFilesystems test`
- Result:
  - Completed with exit code 0.
  - `** TEST SUCCEEDED **`
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.12_05-37-29-+0200.xcresult`.
- Runtime markers observed in the simulator output:
  - `ORLIX_ENV_PSEUDOFS_BEGIN`
  - `ORLIX_ENV_PROC_DIR_OK`
  - `ORLIX_ENV_PROC_MOUNTS_OK`
  - `ORLIX_ENV_PROC_SELF_STATUS_OK`
  - `ORLIX_ENV_PROC_SELF_FD_OK`
  - `ORLIX_ENV_DEV_DIR_OK`
  - `ORLIX_ENV_DEV_NULL_OK`
  - `ORLIX_ENV_DEV_URANDOM_OK`
  - `ORLIX_ENV_DEV_TTY_OK`
  - `ORLIX_ENV_DEV_PTMX_OK`
  - `ORLIX_ENV_DEV_PTS_OK`
  - `ORLIX_ENV_SYS_DIR_OK`
  - `ORLIX_ENV_SYS_BLOCK_VDA_OK`
  - `ORLIX_ENV_SYS_BLOCK_VDB_OK`
  - `ORLIX_ENV_PSEUDOFS_DONE`
- Runtime `/proc/self/status` evidence observed in the simulator output:
  - `Name: cat`
  - `Uid: 0 0 0 0`
  - `Gid: 0 0 0 0`
  - `NSpid: 45`
- Runtime `/proc/mounts` lines observed in the simulator output:
  - `overlay / overlay rw,relatime,lowerdir=/lower,upperdir=/state/upper,workdir=/state/work,uuid=on 0 0`
  - `devtmpfs /dev devtmpfs rw,relatime,size=516272k,nr_inodes=129068,mode=755 0 0`
  - `proc /proc proc rw,relatime 0 0`
  - `sysfs /sys sysfs rw,relatime 0 0`
  - `devpts /dev/pts devpts rw,relatime,gid=5,mode=620,ptmxmode=666 0 0`
  - `tmpfs /dev/shm tmpfs rw,nosuid,nodev,relatime 0 0`
  - `tmpfs /run tmpfs rw,nosuid,nodev,relatime,mode=755 0 0`
  - `tmpfs /tmp tmpfs rw,nosuid,nodev,relatime 0 0`
- Exact result summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.12_05-37-29-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 1`
  - `passedTests: 1`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5, arm64.
- Crash check:
  - Latest `OrlixTestRunner` diagnostic report remains `/Users/rudironsoni/Library/Logs/DiagnosticReports/OrlixTestRunner-2026-06-12-032807.ips`.
  - No newer `OrlixTestRunner` crash report appeared after this focused proof.
- Generated output status:
  - `Build/OrlixOS` has no tracked git status after the proof run.

**Current conclusion:**

- OCI-derived environment execution has refreshed iOS Simulator proof for the minimum Linux-visible `/proc`, `/dev`, `/dev/pts`, `/sys`, `/proc/self/status`, and `/proc/mounts` baseline needed by imported rootfs shell work.
- This does not prove full procfs/devtmpfs/devpts/sysfs completeness, namespace-local procfs behavior, network namespace behavior, cgroup behavior, real device execution, multiple live environments in one kernel, OCI registry pull, systemd images, or App Store acceptance.

### 2026-06-12 OCI-derived runtime tmpfs proof on iOS Simulator

**What happened:**

Ran the focused app-hosted runtime proof that boots the OCI-derived materialized environment on iOS Simulator, reaches the imported root through the normal OrlixOS session path, verifies Linux-visible tmpfs mounts for `/tmp`, `/run`, and `/dev/shm`, writes files into each mount, and prints `/proc/mounts` from inside the Linux userspace shell.

This is an iOS Simulator runtime proof only. macOS is the development host for Xcode, fixture generation, and result-bundle inspection. It is not the initial runtime target, not the Linux user model, and not a product runtime surface.

**Evidence:**

- Exact iOS Simulator proof command:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests/testOCIDerivedMaterializedRootUsesLinuxRuntimeTmpfsMounts test`
- Result:
  - Completed with exit code 0.
  - `** TEST SUCCEEDED **`
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.12_05-31-33-+0200.xcresult`.
- Runtime markers observed in the simulator output:
  - `ORLIX_ENV_TMPFS_BEGIN`
  - `ORLIX_ENV_TMP_MOUNT_OK`
  - `ORLIX_ENV_RUN_MOUNT_OK`
  - `ORLIX_ENV_DEV_SHM_MOUNT_OK`
  - `ORLIX_ENV_TMP_WRITE_OK`
  - `ORLIX_ENV_RUN_WRITE_OK`
  - `ORLIX_ENV_DEV_SHM_WRITE_OK`
  - `ORLIX_ENV_TMPFS_DONE`
- Runtime `/proc/mounts` lines observed in the simulator output:
  - `overlay / overlay rw,relatime,lowerdir=/lower,upperdir=/state/upper,workdir=/state/work,uuid=on 0 0`
  - `tmpfs /dev/shm tmpfs rw,nosuid,nodev,relatime 0 0`
  - `tmpfs /run tmpfs rw,nosuid,nodev,relatime,mode=755 0 0`
  - `tmpfs /tmp tmpfs rw,nosuid,nodev,relatime 0 0`
- Exact result summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.12_05-31-33-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 1`
  - `passedTests: 1`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5, arm64.
- Crash check:
  - Latest `OrlixTestRunner` diagnostic report remains `/Users/rudironsoni/Library/Logs/DiagnosticReports/OrlixTestRunner-2026-06-12-032807.ips`.
  - No newer `OrlixTestRunner` crash report appeared after this focused proof.
- Generated output status:
  - `Build/OrlixOS` has no tracked git status after the proof run.

**Current conclusion:**

- OCI-derived environment execution now has focused iOS Simulator proof that `/tmp`, `/run`, and `/dev/shm` are Linux-visible tmpfs mounts inside the imported environment and support writes from Linux userspace.
- This does not prove temp lifetime across environment restarts, real device execution, host-folder mounts, external security-scoped mounts, network namespaces, cgroups, multiple live environments in one kernel, OCI registry pull, systemd images, or App Store acceptance.

### 2026-06-12 OCI-derived overlay copy-up and unlink proof on iOS Simulator

**What happened:**

Ran the focused app-hosted runtime proof that boots the OCI-derived materialized environment, reaches upstream Linux OverlayFS through the normal OrlixOS session path, writes over `/etc/os-release`, reads back the copied-up content, unlinks the file, and verifies the lower file is hidden from the merged root.

This is an iOS Simulator runtime proof only. macOS is the development host for Xcode, fixture generation, and result-bundle inspection. It is not the initial runtime target, not the Linux user model, and not a product runtime surface.

**Evidence:**

- Exact iOS Simulator proof command:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests/testOCIDerivedMaterializedRootUsesLinuxOverlayCopyUpAndWhiteout test`
- Result:
  - Completed with exit code 0.
  - `** TEST SUCCEEDED **`
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.12_05-25-52-+0200.xcresult`.
- Runtime markers observed in the simulator output:
  - `ORLIX_ENV_OVERLAY_MUTATION_BEGIN`
  - `overlay / overlay rw,relatime,lowerdir=/lower,upperdir=/state/upper,workdir=/state/work,uuid=on 0 0`
  - `ID=orlix-oci-runtime-proof`
  - `ORLIX_ENV_OVERLAY_COPYUP_WRITE_OK`
  - `ID=orlix-overlay-copyup-proof`
  - `ORLIX_ENV_OVERLAY_COPYUP_OK`
  - `ORLIX_ENV_OVERLAY_UNLINK_OK`
  - `ORLIX_ENV_OVERLAY_MUTATION_DONE`
- Exact result summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.12_05-25-52-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 1`
  - `passedTests: 1`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5, arm64.
- Crash check:
  - Latest `OrlixTestRunner` diagnostic report remains `/Users/rudironsoni/Library/Logs/DiagnosticReports/OrlixTestRunner-2026-06-12-032807.ips`.
  - No newer `OrlixTestRunner` crash report appeared after this focused proof.
- Generated output status:
  - `Build/OrlixOS` has no tracked git status after the proof run.

**Current conclusion:**

- OCI-derived environment execution now has focused iOS Simulator proof for Linux-visible OverlayFS copy-up and unlink hiding behavior over the generated read-only base and writable state images.
- This does not prove cross-boot persistence of the overlay mutation, multiple live environments in one kernel, OCI registry pull, real device execution, host-folder mounts, network namespaces, cgroups, systemd images, or App Store acceptance.

### 2026-06-12 Generated environment runtime fixtures boot on iOS Simulator

**What happened:**

Generated real tar-derived and OCI-derived environment runtime proof fixtures through the updated `OrlixOS/Makefile` path, verified the generated metadata command files, and ran a focused app-hosted iOS Simulator runtime proof against the OCI-derived materialized root.

This confirms that the Makefile path now consumes state metadata commands in the fixture generation path used by `OrlixPTYRuntimeTests`. It also keeps the initial proof target explicit: iOS Simulator only. macOS is the development host for Xcode, fixture generation, and result-bundle inspection. It is not the initial runtime target, not the Linux user model, and not a product runtime surface.

**Evidence:**

- Exact fixture generation command:
  - `rtk timeout 2400 make -f OrlixOS/Makefile environment-runtime-proof-fixtures PROFILE=release`
- Result:
  - Completed with exit code 0.
  - Generated fixture marker: `Build/OrlixOS/environment-runtime-proof/.fixtures-ready`.
  - Generated tar marker: `Build/OrlixOS/environment-runtime-proof/tar-imported/.ready`.
  - Generated OCI marker: `Build/OrlixOS/environment-runtime-proof/oci-imported/.ready`.
  - Generated OCI base image: `Build/OrlixOS/environment-runtime-proof/oci-imported/state/environments/oci-imported-runtime-proof/base.ext4`, 64.0M.
  - Generated OCI state image: `Build/OrlixOS/environment-runtime-proof/oci-imported/state/environments/oci-imported-runtime-proof/state.ext4`, 32.0M.
  - Generated tar base image: `Build/OrlixOS/environment-runtime-proof/tar-imported/state/environments/tar-imported-runtime-proof/base.ext4`, 64.0M.
  - Generated tar state image: `Build/OrlixOS/environment-runtime-proof/tar-imported/state/environments/tar-imported-runtime-proof/state.ext4`, 32.0M.
- Exact metadata verification commands:
  - `rtk sed -n '1,80p' Build/OrlixOS/environment-runtime-proof/oci-imported/work/image-materialization/state-debugfs-metadata.commands`
  - `rtk sed -n '1,80p' Build/OrlixOS/environment-runtime-proof/tar-imported/work/image-materialization/state-debugfs-metadata.commands`
- Result:
  - Both generated state metadata files contain `set_inode_field /upper uid 0`, `set_inode_field /upper gid 0`, `set_inode_field /upper mode 040755`, `set_inode_field /work uid 0`, `set_inode_field /work gid 0`, and `set_inode_field /work mode 040755`.
- Exact iOS Simulator proof command:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests/testOCIDerivedMaterializedRootBootsAndExposesOSRelease test`
- Result:
  - Completed with exit code 0.
  - `** TEST SUCCEEDED **`
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.12_05-19-47-+0200.xcresult`.
- Exact result summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.12_05-19-47-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 1`
  - `passedTests: 1`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5, arm64.
- Crash check:
  - Latest `OrlixTestRunner` diagnostic report remains `/Users/rudironsoni/Library/Logs/DiagnosticReports/OrlixTestRunner-2026-06-12-032807.ips`.
  - No newer `OrlixTestRunner` crash report appeared after this focused proof.

**Current conclusion:**

- The generated OCI-derived materialized environment boots through the app-hosted PTY runtime on iOS Simulator and exposes `ID=orlix-oci-runtime-proof` from `/etc/os-release`.
- The fixture path now proves the state metadata command file is generated and consumed before the runtime proof uses the ext4 pair.
- This does not prove full Docker behavior, registry pull, real device execution, host-folder mounts, network namespaces, cgroups, systemd images, or App Store acceptance.

### 2026-06-12 Environment materialization target consumes state metadata commands

**What happened:**

Closed the Makefile-side half of the state metadata command path.

The Swift importers already write `state-debugfs.commands`, and the Swift materialization command plan already applies that file to the state ext4 image. The existing `OrlixOS/Makefile environment-root-image` target accepted only `ORLIXOS_ENVIRONMENT_BASE_DEBUGFS_COMMANDS`, so an importer result could feed base metadata into the target but not the matching state metadata file.

Changes made during this checkpoint:

- `OrlixOS/Makefile` now accepts `ORLIXOS_ENVIRONMENT_STATE_DEBUGFS_COMMANDS`.
- The target fails loudly if that variable is set but the file is missing.
- The target applies the provided state debugfs commands to `ORLIXOS_ENVIRONMENT_STATE_IMAGE`.
- The built-in state debugfs command file now also sets `/upper` and `/work` mode to `040755`.
- `environment-runtime-proof-fixtures` now writes `state-debugfs-metadata.commands` and passes it into `environment-root-image`.
- `OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift` now asserts that the Makefile target consumes both base and state importer metadata command files.

This is OrlixOS image-materialization plumbing. It does not add registry pull, Docker, runc, Apple container/containerization, Virtualization.framework, VM lifecycle, or host-tool execution inside the iOS runtime. The proof target is iOS Simulator.

**Decision:**

The Makefile materialization target must accept every metadata command file produced by the OrlixOS tar and OCI import paths. Otherwise the import plan and the materialization target drift, and the later app-hosted environment proof depends on an incomplete host-side command contract.

**Evidence:**

- Exact command(s):
  - `rtk git diff --check -- OrlixOS/Makefile OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
  - `rtk env CLANG_MODULE_CACHE_PATH=.build/ModuleCache SWIFT_MODULE_CACHE_PATH=.build/ModuleCache swiftc -typecheck OrlixOS/Sources/Session/OrlixOS.swift OrlixOS/Sources/Session/OrlixStoragePolicy.swift OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Sources/Session/OrlixRootfsImport.swift OrlixOS/Sources/Session/OrlixOCIImageLayout.swift OrlixOS/Sources/Session/OrlixEnvironmentImageMaterialization.swift`
  - `rtk make -f OrlixOS/Makefile -n environment-root-image ORLIXOS_ENVIRONMENT_STAGING_ROOT=/tmp/orlix-staged-root ORLIXOS_ENVIRONMENT_BASE_IMAGE=/tmp/orlix-env/base.ext4 ORLIXOS_ENVIRONMENT_STATE_IMAGE=/tmp/orlix-env/state.ext4 ORLIXOS_ENVIRONMENT_BASE_DEBUGFS_COMMANDS=/tmp/orlix-env/base-debugfs.commands ORLIXOS_ENVIRONMENT_STATE_DEBUGFS_COMMANDS=/tmp/orlix-env/state-debugfs.commands`
  - `rtk rg -n 'state_metadata_commands|ORLIXOS_ENVIRONMENT_STATE_DEBUGFS_COMMANDS|set_inode_field %s mode 040755' OrlixOS/Makefile OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
  - `rtk timeout 600 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentRootImageMakeTargetConsumesImporterMetadataCommands -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentImageMaterializationCommandsUseCanonicalMke2fsFlags -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarImporterStagesRootfsAndPersistsEnvironmentDescriptor -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesLayerWhiteoutsIntoStagingRoot test`
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_05-09-10-+0200.xcresult`
- Result:
  - Path-scoped whitespace check passed.
  - OrlixOS session sources typechecked.
  - Make dry run expanded `ORLIXOS_ENVIRONMENT_STATE_DEBUGFS_COMMANDS`, checked that the file exists when supplied, applied it with `debugfs -w -f`, and emitted state mode commands for `/upper` and `/work`.
  - Source inspection confirmed `environment-runtime-proof-fixtures` writes `state-debugfs-metadata.commands` and passes it into `environment-root-image`.
  - Focused OrlixOS XCTest cluster passed on iPhone 17 Pro iOS Simulator, iOS 26.5, arm64: 4 passed, 0 failed, 0 skipped.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_05-09-10-+0200.xcresult`.
- Failed or rejected checks:
  - An earlier unbounded focused `xcodebuild` invocation remained silent for several polling intervals and its intermediate result bundle was not readable while running. It later returned, but the bounded rerun above is the authoritative evidence for this checkpoint.
  - A dry-run attempt for `environment-runtime-proof-fixtures` began traversing recursive package build paths, so it was rejected as proof. The deterministic source inspection and focused iOS Simulator test above are the accepted evidence.
  - No project-code failure occurred in this checkpoint.
  - The latest `OrlixTestRunner` diagnostic report remains `/Users/rudironsoni/Library/Logs/DiagnosticReports/OrlixTestRunner-2026-06-12-032807.ips`; no newer `OrlixTestRunner` crash report was present after this proof.
- Final marker(s):
  - `environment-root-image` accepts and applies `ORLIXOS_ENVIRONMENT_STATE_DEBUGFS_COMMANDS`.
  - `environment-runtime-proof-fixtures` now feeds state metadata commands into `environment-root-image`.
  - The target's built-in state debugfs command file sets `/upper` and `/work` UID, GID, and mode.
  - `testEnvironmentRootImageMakeTargetConsumesImporterMetadataCommands` protects the importer-to-Makefile metadata contract.

**Next:**

Continue toward real materialized environment boot proof on iOS Simulator. The next useful checkpoint is to feed an importer-produced base and state metadata command pair into `environment-root-image`, then register the generated ext4 pair through `OrlixLinuxSession(materializedRootImage:)`.

### 2026-06-12 Environment image command plan applies state metadata with debugfs

**What happened:**

Closed the state-image side of the materialization command-plan gap.

The previous Swift materialization plan wrote and applied base image metadata, but the state image command path did not have an explicit state debugfs metadata command file or debugfs application step. The Makefile path already normalizes `/upper` and `/work`; the Swift import/materialization path now carries equivalent explicit metadata.

Changes made during this checkpoint:

- `OrlixOS/Sources/Session/OrlixEnvironmentImageMaterialization.swift` now has `stateMetadataCommandsURL`.
- The materialization command list now runs `debugfs -w -f <state-debugfs.commands> <state.ext4>` after state mke2fs.
- `OrlixEnvironmentImageMaterializationPlan` now writes state metadata commands for `/upper` and `/work`.
- Rootfs tar import and OCI layout import now write the state metadata command file alongside base metadata.
- Focused OrlixOS tests now assert the state debugfs command and the state metadata files produced by tar and OCI import.

This remains OrlixOS image-materialization planning. It does not add registry pull, Docker, runc, Apple container/containerization, Virtualization.framework, a VM runtime, or OrlixKernel host-tool execution. The proof target is iOS Simulator.

**Decision:**

Both base and state ext4 image command plans must explicitly carry metadata normalization. Relying only on the host-created staging tree is too weak for reproducible environment images.

**Evidence:**

- Exact command(s):
  - `rtk git diff --check -- OrlixOS/Sources/Session/OrlixEnvironmentImageMaterialization.swift OrlixOS/Sources/Session/OrlixRootfsImport.swift OrlixOS/Sources/Session/OrlixOCIImageLayout.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
  - `rtk env CLANG_MODULE_CACHE_PATH=.build/ModuleCache SWIFT_MODULE_CACHE_PATH=.build/ModuleCache swiftc -typecheck OrlixOS/Sources/Session/OrlixOS.swift OrlixOS/Sources/Session/OrlixStoragePolicy.swift OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Sources/Session/OrlixRootfsImport.swift OrlixOS/Sources/Session/OrlixOCIImageLayout.swift OrlixOS/Sources/Session/OrlixEnvironmentImageMaterialization.swift`
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentImageMaterializationCommandsUseCanonicalMke2fsFlags -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentImageMaterializationRejectsUnsafeInputs -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarImporterStagesRootfsAndPersistsEnvironmentDescriptor -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesLayerWhiteoutsIntoStagingRoot test`
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_04-55-34-+0200.xcresult`
- Result:
  - Path-scoped whitespace check passed.
  - OrlixOS session sources typechecked.
  - Focused OrlixOS XCTest cluster passed on iPhone 17 Pro iOS Simulator, iOS 26.5, arm64: 4 passed, 0 failed, 0 skipped.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_04-55-34-+0200.xcresult`.
- Failed or rejected checks:
  - No project-code failure occurred in this checkpoint.
  - The latest `OrlixTestRunner` diagnostic report remains `/Users/rudironsoni/Library/Logs/DiagnosticReports/OrlixTestRunner-2026-06-12-032807.ips`; no newer `OrlixTestRunner` crash report was present after this proof.
- Final marker(s):
  - `OrlixEnvironmentImageMaterializationPlan.commands(...)` now includes base and state `debugfs -w -f` steps.
  - Tar and OCI import write `state-debugfs.commands`.
  - Focused tests verify `/upper` and `/work` state metadata command content.

**Next:**

Continue toward actual materialized-image execution and runtime proof. This checkpoint plans and writes the host-tool command inputs; it does not execute host tools inside the iOS runtime.

### 2026-06-12 Environment image command plan applies base metadata with debugfs

**What happened:**

Closed a materialization command-plan gap.

Swift-side environment imports already prepared base and state input trees and wrote the base debugfs metadata command file, but `OrlixEnvironmentImageMaterializationPlan.commands(...)` only returned truncate and mke2fs commands. That command list was not sufficient to produce a base ext4 image with imported UID, GID, mode, special-file, and xattr metadata applied.

Changes made during this checkpoint:

- `OrlixOS/Sources/Session/OrlixEnvironmentImageMaterialization.swift` now includes a `debugfsExecutable` parameter in `commands(...)`.
- The materialization command list now runs `debugfs -w -f <base-debugfs.commands> <base.ext4>` immediately after base mke2fs.
- `OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift` now expects the debugfs command in `testEnvironmentImageMaterializationCommandsUseCanonicalMke2fsFlags`.
- The unsafe-input test now validates the debugfs executable name too.

This remains OrlixOS image-materialization planning. It does not add registry pull, Docker, runc, Apple container/containerization, Virtualization.framework, a VM runtime, or OrlixKernel host-tool execution. The proof target is iOS Simulator.

**Decision:**

The materialization command plan must be self-contained for base image metadata. Producing an ext4 image from imported rootfs data without applying the generated metadata commands is not enough for OCI/rootfs fidelity.

**Evidence:**

- Exact command(s):
  - `rtk git diff --check -- OrlixOS/Sources/Session/OrlixEnvironmentImageMaterialization.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
  - `rtk env CLANG_MODULE_CACHE_PATH=.build/ModuleCache SWIFT_MODULE_CACHE_PATH=.build/ModuleCache swiftc -typecheck OrlixOS/Sources/Session/OrlixOS.swift OrlixOS/Sources/Session/OrlixStoragePolicy.swift OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Sources/Session/OrlixRootfsImport.swift OrlixOS/Sources/Session/OrlixOCIImageLayout.swift OrlixOS/Sources/Session/OrlixEnvironmentImageMaterialization.swift`
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentImageMaterializationCommandsUseCanonicalMke2fsFlags -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentImageMaterializationRejectsUnsafeInputs -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarImporterStagesRootfsAndPersistsEnvironmentDescriptor -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesLayerWhiteoutsIntoStagingRoot test`
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_04-52-12-+0200.xcresult`
- Result:
  - Path-scoped whitespace check passed.
  - OrlixOS session sources typechecked.
  - Focused OrlixOS XCTest cluster passed on iPhone 17 Pro iOS Simulator, iOS 26.5, arm64: 4 passed, 0 failed, 0 skipped.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_04-52-12-+0200.xcresult`.
- Failed or rejected checks:
  - No project-code failure occurred in this checkpoint.
  - The latest `OrlixTestRunner` diagnostic report remains `/Users/rudironsoni/Library/Logs/DiagnosticReports/OrlixTestRunner-2026-06-12-032807.ips`; no newer `OrlixTestRunner` crash report was present after this proof.
- Final marker(s):
  - `OrlixEnvironmentImageMaterializationPlan.commands(...)` now includes `debugfs -w -f`.
  - The materialization command-plan regression expects the debugfs step between base mke2fs and state image creation.

**Next:**

Continue toward actual materialized-image execution and runtime proof. This checkpoint plans the host-tool commands; it does not execute them in the iOS runtime.

### 2026-06-12 OCI layer manifest operations preserve tar order

**What happened:**

Fixed an OCI same-layer ordering bug in final manifest generation.

The importer previously collected applied entries, removed paths, and opaque directories into separate buckets, then replayed them by bucket type. That could invert the order of operations from the original layer tar. For example, a file followed by a whiteout in the same layer could be removed from the staged root but re-added to the final manifest because applied entries were replayed after removals.

Changes made during this checkpoint:

- `OrlixOS/Sources/Session/OrlixOCIImageLayout.swift` now records layer application as an ordered operation stream.
- `applyManifestChanges(...)` now replays applied entries, whiteouts, and opaque directory whiteouts in tar order.
- `OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift` now includes `testOCIImageLayoutImporterHonorsWhiteoutOrderWithinLayer`.
- The regression proves a layer containing `etc/transient`, then `etc/.wh.transient`, then `etc/final` keeps only `etc` and `etc/final` in the final manifest and generated metadata commands.

This remains OrlixOS image-input and materialization behavior. It does not add registry pull, Docker, runc, Apple container/containerization, Virtualization.framework, or a VM runtime. The proof target is iOS Simulator.

**Decision:**

Final manifest mutation must preserve tar order within each OCI layer. Grouping mutations by operation type is incorrect because whiteout ordering is part of the layer application semantics.

**Evidence:**

- Exact command(s):
  - `rtk git diff --check -- OrlixOS/Sources/Session/OrlixOCIImageLayout.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
  - `rtk env CLANG_MODULE_CACHE_PATH=.build/ModuleCache SWIFT_MODULE_CACHE_PATH=.build/ModuleCache swiftc -typecheck OrlixOS/Sources/Session/OrlixOS.swift OrlixOS/Sources/Session/OrlixStoragePolicy.swift OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Sources/Session/OrlixRootfsImport.swift OrlixOS/Sources/Session/OrlixOCIImageLayout.swift OrlixOS/Sources/Session/OrlixEnvironmentImageMaterialization.swift`
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterHonorsWhiteoutOrderWithinLayer -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterPreservesChildrenWhenDirectoryEntryComesLater -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesLayerWhiteoutsIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRecordsImplicitOpaqueDirectoryInManifest test`
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_04-48-53-+0200.xcresult`
- Result:
  - Path-scoped whitespace check passed.
  - OrlixOS session sources typechecked.
  - Focused OrlixOS XCTest cluster passed on iPhone 17 Pro iOS Simulator, iOS 26.5, arm64: 4 passed, 0 failed, 0 skipped.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_04-48-53-+0200.xcresult`.
- Failed or rejected checks:
  - No project-code failure occurred in this checkpoint.
  - The latest `OrlixTestRunner` diagnostic report remains `/Users/rudironsoni/Library/Logs/DiagnosticReports/OrlixTestRunner-2026-06-12-032807.ips`; no newer `OrlixTestRunner` crash report was present after this proof.
- Final marker(s):
  - `applyManifestChanges(...)` now iterates `application.operations`.
  - The same-layer whiteout-order regression expects `result.manifest.map(\.path)` to equal `["etc", "etc/final"]`.
  - Generated metadata commands do not include `/etc/transient` or `.wh.transient`.

**Next:**

Continue toward materialized-image execution and overlay/snapshot proof. Do not treat this as registry support or Docker/runc compatibility.

### 2026-06-12 OCI directory metadata updates preserve existing children

**What happened:**

Fixed a final-manifest ordering bug for OCI layer application.

The previous manifest replacement rule removed descendants whenever a directory entry was applied. That could make the final manifest diverge from the staged filesystem when a layer listed a child before the parent directory entry, for example `etc/os-release` followed by `etc/`. The staged file stayed present, but the final manifest could lose the child metadata before ext4 image materialization.

Changes made during this checkpoint:

- `OrlixOS/Sources/Session/OrlixOCIImageLayout.swift` now preserves descendants when the applied entry is a directory.
- Non-directory entries still remove descendants so replacing a directory with a regular file, symlink, hardlink, device, or fifo does not keep stale child metadata.
- `OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift` now includes `testOCIImageLayoutImporterPreservesChildrenWhenDirectoryEntryComesLater`.
- The regression proves a layer with `etc/os-release` before `etc/` keeps both file and directory metadata in the final manifest and generated metadata commands.

This remains OrlixOS image-input and materialization behavior. It does not add registry pull, Docker, runc, Apple container/containerization, Virtualization.framework, or a VM runtime. The proof target is iOS Simulator.

**Decision:**

Directory metadata updates replace only the exact directory manifest entry. They must not erase already-applied children because OCI tar entry order can be imperfect while the final staged filesystem still contains those children.

**Evidence:**

- Exact command(s):
  - `rtk git diff --check -- OrlixOS/Sources/Session/OrlixOCIImageLayout.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
  - `rtk env CLANG_MODULE_CACHE_PATH=.build/ModuleCache SWIFT_MODULE_CACHE_PATH=.build/ModuleCache swiftc -typecheck OrlixOS/Sources/Session/OrlixOS.swift OrlixOS/Sources/Session/OrlixStoragePolicy.swift OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Sources/Session/OrlixRootfsImport.swift OrlixOS/Sources/Session/OrlixOCIImageLayout.swift OrlixOS/Sources/Session/OrlixEnvironmentImageMaterialization.swift`
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterPreservesChildrenWhenDirectoryEntryComesLater -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesLayerWhiteoutsIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRecordsImplicitOpaqueDirectoryInManifest test`
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_04-44-58-+0200.xcresult`
- Result:
  - Path-scoped whitespace check passed.
  - OrlixOS session sources typechecked.
  - Focused OrlixOS XCTest cluster passed on iPhone 17 Pro iOS Simulator, iOS 26.5, arm64: 3 passed, 0 failed, 0 skipped.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_04-44-58-+0200.xcresult`.
- Failed or rejected checks:
  - No project-code failure occurred in this checkpoint.
  - The latest `OrlixTestRunner` diagnostic report remains `/Users/rudironsoni/Library/Logs/DiagnosticReports/OrlixTestRunner-2026-06-12-032807.ips`; no newer `OrlixTestRunner` crash report was present after this proof.
- Final marker(s):
  - The late-directory regression expects `result.manifest.map(\.path)` to equal `["etc/os-release", "etc"]`.
  - Generated metadata commands include both `/etc/os-release` and `/etc` mode metadata.

**Next:**

Continue toward materialized-image execution and overlay/snapshot proof. Do not treat this as registry support or Docker/runc compatibility.

### 2026-06-12 OCI opaque-created directories enter final manifest metadata

**What happened:**

Fixed another OCI opaque-directory metadata gap.

When a layer contained an opaque whiteout such as `var/cache/.wh..wh..opq` without also carrying an explicit `var/cache/` directory entry, OrlixOS created the directory in the staging tree but did not include that directory in the final manifest. That left ext4 image metadata generation dependent on host-created defaults instead of the final imported image view.

Changes made during this checkpoint:

- `OrlixOS/Sources/Session/OrlixOCIImageLayout.swift` now adds a root-owned `0755` directory manifest entry for a non-root opaque-created directory when no exact directory entry already exists.
- `OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift` now includes `testOCIImageLayoutImporterRecordsImplicitOpaqueDirectoryInManifest`.
- The regression proves `var/cache/.wh..wh..opq` removes lower `var/cache/old`, records `var/cache` in the final manifest, keeps `var/cache/new`, and emits metadata commands for `/var/cache`.

This remains OrlixOS image-input and materialization behavior. It does not add registry pull, Docker, runc, Apple container/containerization, Virtualization.framework, or a VM runtime. The proof target is iOS Simulator.

**Decision:**

When OrlixOS creates an opaque directory while applying OCI layers, the final manifest must include that directory unless a real directory entry already exists. This keeps final manifest metadata aligned with the staged final root before ext4 materialization.

**Evidence:**

- Exact command(s):
  - `rtk git diff --check -- OrlixOS/Sources/Session/OrlixOCIImageLayout.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
  - `rtk env CLANG_MODULE_CACHE_PATH=.build/ModuleCache SWIFT_MODULE_CACHE_PATH=.build/ModuleCache swiftc -typecheck OrlixOS/Sources/Session/OrlixOS.swift OrlixOS/Sources/Session/OrlixStoragePolicy.swift OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Sources/Session/OrlixRootfsImport.swift OrlixOS/Sources/Session/OrlixOCIImageLayout.swift OrlixOS/Sources/Session/OrlixEnvironmentImageMaterialization.swift`
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesLayerWhiteoutsIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesRootOpaqueWhiteoutToManifest -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRecordsImplicitOpaqueDirectoryInManifest test`
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_04-41-57-+0200.xcresult`
- Result:
  - Path-scoped whitespace check passed.
  - OrlixOS session sources typechecked.
  - Focused OrlixOS XCTest cluster passed on iPhone 17 Pro iOS Simulator, iOS 26.5, arm64: 3 passed, 0 failed, 0 skipped.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_04-41-57-+0200.xcresult`.
- Failed or rejected checks:
  - No project-code failure occurred in this checkpoint.
  - The latest `OrlixTestRunner` diagnostic report remains `/Users/rudironsoni/Library/Logs/DiagnosticReports/OrlixTestRunner-2026-06-12-032807.ips`; no newer `OrlixTestRunner` crash report was present after this proof.
- Final marker(s):
  - The implicit opaque-directory regression expects `result.manifest.map(\.path)` to equal `["var/cache", "var/cache/new"]`.
  - Generated metadata commands include `/var/cache` mode metadata and do not include reset lower-layer `/var/cache/old`.

**Next:**

Continue toward materialized-image execution and overlay/snapshot proof. Do not treat this as registry support or Docker/runc compatibility.

### 2026-06-12 Root-level OCI opaque whiteouts clear final manifest metadata

**What happened:**

Fixed the root-directory variant of OCI opaque whiteout manifest pruning.

The prior whiteout metadata fix handled ordinary removals and non-root opaque directories, but root-level `.wh..wh..opq` still produced an empty opaque-directory marker that did not clear prior manifest entries. The staged root directory was cleared, but the metadata manifest could still contain lower-layer paths.

Changes made during this checkpoint:

- `OrlixOS/Sources/Session/OrlixOCIImageLayout.swift` now clears all prior manifest entries when an opaque whiteout targets the root directory.
- `OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift` now includes `testOCIImageLayoutImporterAppliesRootOpaqueWhiteoutToManifest`.
- The regression proves a root opaque whiteout removes prior `/etc` and `/usr` layer history from both the staged root and generated metadata commands before applying new upper-layer entries.

This remains OrlixOS image-input and materialization behavior. It does not add registry pull, Docker, runc, Apple container/containerization, Virtualization.framework, or a VM runtime. The proof target is iOS Simulator.

**Decision:**

Treat root-level opaque whiteout as a reset of the accumulated final manifest for the imported root. Later entries in the same layer still apply normally after the reset. This keeps the metadata used for ext4 materialization aligned with the final OCI root view.

**Evidence:**

- Exact command(s):
  - `rtk git diff --check -- OrlixOS/Sources/Session/OrlixOCIImageLayout.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
  - `rtk env CLANG_MODULE_CACHE_PATH=.build/ModuleCache SWIFT_MODULE_CACHE_PATH=.build/ModuleCache swiftc -typecheck OrlixOS/Sources/Session/OrlixOS.swift OrlixOS/Sources/Session/OrlixStoragePolicy.swift OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Sources/Session/OrlixRootfsImport.swift OrlixOS/Sources/Session/OrlixOCIImageLayout.swift OrlixOS/Sources/Session/OrlixEnvironmentImageMaterialization.swift`
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesLayerWhiteoutsIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesRootOpaqueWhiteoutToManifest test`
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_04-38-23-+0200.xcresult`
- Result:
  - Path-scoped whitespace check passed.
  - OrlixOS session sources typechecked.
  - Focused OrlixOS XCTest pair passed on iPhone 17 Pro iOS Simulator, iOS 26.5, arm64: 2 passed, 0 failed, 0 skipped.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_04-38-23-+0200.xcresult`.
- Failed or rejected checks:
  - No project-code failure occurred in this checkpoint.
  - The latest `OrlixTestRunner` diagnostic report remains `/Users/rudironsoni/Library/Logs/DiagnosticReports/OrlixTestRunner-2026-06-12-032807.ips`; no newer `OrlixTestRunner` crash report was present after this proof.
- Final marker(s):
  - Root-level `.wh..wh..opq` clears prior final-manifest entries.
  - The root opaque whiteout regression expects `result.manifest.map(\.path)` to equal `["etc", "etc/os-release"]`.
  - Generated metadata commands no longer include reset lower-layer paths such as `/etc/old`, `/usr`, or `/usr/bin/tool`.

**Next:**

Continue toward materialized-image execution and overlay/snapshot proof. Do not treat this as registry support or Docker/runc compatibility.

### 2026-06-12 OCI whiteouts update final import metadata manifest

**What happened:**

Fixed an OCI layout import mismatch between the staged filesystem and the metadata manifest used for ext4 image materialization.

Before this checkpoint, whiteouts removed lower-layer files from the staged root, but the importer still kept those removed lower-layer entries in `result.manifest`. That meant the generated debugfs metadata command file could still contain paths that no longer existed in the final imported root.

Changes made during this checkpoint:

- `OrlixOS/Sources/Session/OrlixOCIImageLayout.swift` now records each layer application as applied entries, removed paths, and opaque directories.
- OCI whiteouts now remove matching prior manifest entries.
- OCI directory whiteouts now remove prior child manifest entries for the opaque directory.
- Newly applied entries replace prior manifest entries for the same path.
- `OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift` now asserts that the whiteout test manifest and generated metadata commands match the final merged root, not layer history.

This keeps OCI images as inputs while preserving the final layer view more accurately for OrlixOS materialization. It does not add registry pull, Docker, runc, Apple container/containerization, Virtualization.framework, or a VM runtime. macOS is only the development/build host for Xcode and result bundle inspection. The proof target is iOS Simulator.

**Decision:**

Keep final OCI filesystem metadata aligned with final OCI layer semantics before ext4 image materialization. The importer may stage and materialize files for Orlix storage, but it must not preserve deleted lower-layer paths in the final environment manifest.

**Evidence:**

- Exact command(s):
  - `rtk git diff --check -- OrlixOS/Sources/Session/OrlixOCIImageLayout.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
  - `rtk env CLANG_MODULE_CACHE_PATH=.build/ModuleCache SWIFT_MODULE_CACHE_PATH=.build/ModuleCache swiftc -typecheck OrlixOS/Sources/Session/OrlixOS.swift OrlixOS/Sources/Session/OrlixStoragePolicy.swift OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Sources/Session/OrlixRootfsImport.swift OrlixOS/Sources/Session/OrlixOCIImageLayout.swift OrlixOS/Sources/Session/OrlixEnvironmentImageMaterialization.swift`
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesLayerWhiteoutsIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterDoesNotCommitPartialRootOnLayerFailure test`
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_04-33-18-+0200.xcresult`
- Result:
  - Path-scoped whitespace check passed.
  - OrlixOS session sources typechecked.
  - Focused OrlixOS XCTest pair passed on iPhone 17 Pro iOS Simulator, iOS 26.5, arm64: 2 passed, 0 failed, 0 skipped.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_04-33-18-+0200.xcresult`.
- Failed or rejected checks:
  - A sandboxed `xcodebuild` run failed before project code because CoreSimulator and SwiftPM cache access were blocked. The same focused command passed with required simulator/cache access.
  - A sandboxed `xcresulttool` summary read failed while writing temporary TestReport output. The same result bundle summary succeeded with required result access.
  - The latest `OrlixTestRunner` diagnostic report remains `/Users/rudironsoni/Library/Logs/DiagnosticReports/OrlixTestRunner-2026-06-12-032807.ips`; no newer `OrlixTestRunner` crash report was present after this proof.
- Final marker(s):
  - The OCI whiteout test now expects `result.manifest.map(\.path)` to equal `["etc", "etc/os-release", "etc/opaque", "etc/opaque/new"]`.
  - Generated metadata commands no longer include deleted lower-layer paths such as `/etc/remove-me` or `/etc/opaque/old`.

**Next:**

Continue toward materialized-image execution. Do not treat this as overlay snapshot completion, registry support, or Docker/runc compatibility.

### 2026-06-12 Explicit Documents mounts fail loud until host-folder mounting exists

**What happened:**

Prevented environment materialization from silently ignoring explicit mount metadata.

Changes made during this checkpoint:

- `OrlixOS/Sources/Session/OrlixEnvironment.swift` now validates descriptor mounts during `OrlixEnvironmentRootImage.materialized(...)`.
- Any current explicit mount throws `OrlixEnvironmentRootImageError.unsupportedMount(...)` because no Linux-compatible host-folder mount path is implemented yet.
- `OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift` now includes `testEnvironmentRootImageRejectsUnimplementedExplicitMounts`.

This preserves the existing rule that Documents may only appear as explicit environment mount metadata, while preventing a false-positive runtime where the descriptor says Documents is mounted but boot-time materialization drops it. It does not implement host-folder mounting, virtio-fs, or security-scoped external folder access yet.

**Decision:**

Fail loud until Orlix has a real Linux-compatible host-folder mount path. Documents metadata is valid as OrlixOS environment configuration, but materialized execution must reject it until the path is backed by Linux mount behavior. OrlixKernel still owns VFS and mount semantics. OrlixHostAdapter must not decide Linux pathname behavior.

**Evidence:**

- Exact command(s):
  - `rtk git diff --check -- OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
  - `rtk env CLANG_MODULE_CACHE_PATH=.build/ModuleCache SWIFT_MODULE_CACHE_PATH=.build/ModuleCache swiftc -typecheck OrlixOS/Sources/Session/OrlixOS.swift OrlixOS/Sources/Session/OrlixStoragePolicy.swift OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Sources/Session/OrlixRootfsImport.swift OrlixOS/Sources/Session/OrlixOCIImageLayout.swift OrlixOS/Sources/Session/OrlixEnvironmentImageMaterialization.swift`
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testDocumentsMountIsExplicitEnvironmentDescriptorMetadata -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentRootImageRejectsUnimplementedExplicitMounts -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsSourcesKeepTmpLinuxOwnedAndDocumentsOutOfRootTruth test`
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_04-28-05-+0200.xcresult`
- Result:
  - Path-scoped whitespace check passed.
  - OrlixOS session sources typechecked.
  - Focused OrlixOS XCTest cluster passed on iPhone 17 Pro iOS Simulator, iOS 26.5, arm64: 3 passed, 0 failed, 0 skipped.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_04-28-05-+0200.xcresult`.
- Failed or rejected checks:
  - A sandboxed `xcodebuild` run failed before project code because CoreSimulator and SwiftPM cache access were blocked. The same focused command passed with required simulator/cache access.
  - The latest `OrlixTestRunner` diagnostic report remains `/Users/rudironsoni/Library/Logs/DiagnosticReports/OrlixTestRunner-2026-06-12-032807.ips`; no newer `OrlixTestRunner` crash report was present after this proof.
- Final marker(s):
  - Explicit Documents mount descriptors still encode and decode as metadata.
  - Rootfs/init sources still keep Documents out of rootfs truth.
  - Materialized environment boot config rejects explicit mounts until real mount support exists.

**Next:**

Implement the real host-folder mount path later through Linux-compatible mount behavior. Do not add raw host path exposure or HostAdapter-owned Linux pathname policy.

### 2026-06-12 OCI layout import refuses existing named environments on iOS Simulator

**What happened:**

Closed the matching OCI layout import safety gap after rootfs tar import received the same protection. OCI layout import now refuses to import into an environment ID that already has an environment root.

Changes made during this checkpoint:

- `OrlixOS/Sources/Session/OrlixOCIImageLayout.swift` now checks `registry.layout(forEnvironmentID:)` before storage preparation and throws `OrlixOCIImageLayoutError.destinationExists(environmentID)` when the destination environment root already exists.
- `OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift` now includes `testOCIImageLayoutImporterDoesNotOverwriteExistingEnvironment`.
- The regression pre-creates an existing environment descriptor plus `base.ext4` and `state.ext4`, attempts to import an OCI layout into the same environment ID, and verifies the existing descriptor and both image files are unchanged.

This is OrlixOS import/session preparation behavior. It does not change OrlixKernel, OrlixHostAdapter, VFS, syscall, exec, or Linux runtime semantics. It does not add registry pull, Docker, runc, Apple container/containerization, Virtualization.framework, or a VM runtime. macOS is only the development/build host for Xcode and result bundle inspection. The proof target is iOS Simulator.

**Decision:**

Keep OCI layout import aligned with named-environment and rootfs tar import rules: importing an image creates a new named environment unless a future explicit replace operation is designed and tested. OCI images remain inputs for OrlixOS environment materialization and must not silently replace persistent Linux state under Application Support.

**Evidence:**

- Exact command(s):
  - `rtk git diff --check -- OrlixOS/Sources/Session/OrlixOCIImageLayout.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
  - `rtk env CLANG_MODULE_CACHE_PATH=.build/ModuleCache SWIFT_MODULE_CACHE_PATH=.build/ModuleCache swiftc -typecheck OrlixOS/Sources/Session/OrlixOS.swift OrlixOS/Sources/Session/OrlixStoragePolicy.swift OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Sources/Session/OrlixRootfsImport.swift OrlixOS/Sources/Session/OrlixOCIImageLayout.swift OrlixOS/Sources/Session/OrlixEnvironmentImageMaterialization.swift`
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesLayerWhiteoutsIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterDoesNotOverwriteExistingEnvironment test`
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_04-24-36-+0200.xcresult`
- Result:
  - Path-scoped whitespace check passed.
  - OrlixOS session sources typechecked.
  - Focused OrlixOS XCTest pair passed on iPhone 17 Pro iOS Simulator, iOS 26.5, arm64: 2 passed, 0 failed, 0 skipped.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_04-24-36-+0200.xcresult`.
- Failed or rejected checks:
  - A sandboxed `xcodebuild` run failed before project code because CoreSimulator and SwiftPM cache access were blocked. The same focused command passed with required simulator/cache access.
  - The latest `OrlixTestRunner` diagnostic report remains `/Users/rudironsoni/Library/Logs/DiagnosticReports/OrlixTestRunner-2026-06-12-032807.ips`; no newer `OrlixTestRunner` crash report was present after this proof.
- Final marker(s):
  - OCI layout import preserves existing environment descriptors and base/state images when the destination exists.
  - OCI layout import no longer silently writes over an existing named environment root.
  - Existing OCI layer whiteout import proof still passes.

**Next:**

Do not treat this as registry support or full OCI runtime support. The next OCI work should continue improving layout import and materialized-image execution without adding Apple container runtime dependencies or replacing Linux-owned runtime semantics.

### 2026-06-12 Rootfs tar import refuses existing named environments on iOS Simulator

**What happened:**

Closed a rootfs tar import safety gap: tar import now refuses to import into an environment ID that already has an environment root.

Changes made during this checkpoint:

- `OrlixOS/Sources/Session/OrlixRootfsImport.swift` now throws `OrlixRootfsTarImportError.destinationExists(environmentID)` before preparing storage when `plan.storageLayout.rootDirectory` already exists.
- `OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift` now includes `testRootfsTarImporterDoesNotOverwriteExistingEnvironment`.
- The regression pre-creates an existing environment descriptor plus `base.ext4` and `state.ext4`, attempts to import a tarball into the same environment ID, and verifies the existing descriptor and both image files are unchanged.

This is OrlixOS import/session preparation behavior. It does not change OrlixKernel, OrlixHostAdapter, VFS, syscall, exec, or Linux runtime semantics. macOS is only the development/build host for Xcode and result bundle inspection. The proof target is iOS Simulator.

**Decision:**

Make rootfs tar import match the named-environment no-overwrite rule already enforced by environment copy. Imported rootfs tarballs must create a new named environment unless a future explicit replace operation is designed and tested. This avoids accidental destruction of persistent Linux state under Application Support.

**Evidence:**

- Exact command(s):
  - `rtk git diff --check -- OrlixOS/Sources/Session/OrlixRootfsImport.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
  - `rtk env CLANG_MODULE_CACHE_PATH=.build/ModuleCache SWIFT_MODULE_CACHE_PATH=.build/ModuleCache swiftc -typecheck OrlixOS/Sources/Session/OrlixOS.swift OrlixOS/Sources/Session/OrlixStoragePolicy.swift OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Sources/Session/OrlixRootfsImport.swift OrlixOS/Sources/Session/OrlixOCIImageLayout.swift OrlixOS/Sources/Session/OrlixEnvironmentImageMaterialization.swift`
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarImporterStagesRootfsAndPersistsEnvironmentDescriptor -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarImporterDoesNotOverwriteExistingEnvironment test`
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_04-21-02-+0200.xcresult`
- Result:
  - Path-scoped whitespace check passed.
  - OrlixOS session sources typechecked.
  - Focused OrlixOS XCTest pair passed on iPhone 17 Pro iOS Simulator, iOS 26.5, arm64: 2 passed, 0 failed, 0 skipped.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_04-21-02-+0200.xcresult`.
- Failed or rejected checks:
  - A sandboxed `xcodebuild` run failed before project code because CoreSimulator and SwiftPM cache access were blocked. The same focused command passed with required simulator/cache access.
  - The latest `OrlixTestRunner` diagnostic report remains `/Users/rudironsoni/Library/Logs/DiagnosticReports/OrlixTestRunner-2026-06-12-032807.ips`; no newer `OrlixTestRunner` crash report was present after this proof.
- Final marker(s):
  - Rootfs tar import preserves existing environment descriptors and base/state images when the destination exists.
  - Rootfs tar import no longer silently writes over an existing named environment root.

**Next:**

Do not treat this as rootfs tar runtime completion. The next rootfs tar step should keep moving toward materialized-image execution and later OCI layout import while preserving the no-overwrite rule.

### 2026-06-12 Named environment state image copy persistence proof on iOS Simulator

**What happened:**

Added a focused OrlixOS proof that copied named environments keep independent persisted base and state image files even after the parent environment changes.

Changes made during this checkpoint:

- `OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift` now includes `testEnvironmentRegistryCopyPreservesStateAfterParentImageChanges`.
- The test creates a parent environment with materialized `base.ext4` and `state.ext4`, copies it to `default-copy`, mutates the parent images after the copy, and verifies the copied images still contain the pre-copy bytes.
- This complements the existing reverse-direction proof that mutating copied images does not change parent images.

This is OrlixOS storage-policy proof running in the iOS Simulator test target. macOS is only the development/build host for Xcode and result bundle inspection. It is not the initial runtime target or Linux user model.

**Decision:**

Keep named-environment image copying in OrlixOS because it is delivered OS/session storage policy. OrlixKernel still owns Linux VFS, overlayfs, block devices, and execution semantics. This proof must not become a HostAdapter-owned Linux state policy and does not claim two-boot runtime persistence yet.

**Evidence:**

- Exact command(s):
  - `rtk git diff --check -- OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentRegistryCopiesNamedEnvironmentImagesAndMetadata -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentRegistryCopyCreatesIndependentImageFiles -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentRegistryCopyPreservesStateAfterParentImageChanges -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentRegistryCopyRequiresMaterializedParentImages -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentRegistryCopyDoesNotOverwriteExistingEnvironment test`
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_04-16-44-+0200.xcresult`
- Result:
  - Path-scoped whitespace check passed.
  - Focused OrlixOS XCTest cluster passed on iPhone 17 Pro iOS Simulator, iOS 26.5, arm64: 5 passed, 0 failed, 0 skipped.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_04-16-44-+0200.xcresult`.
- Failed or rejected checks:
  - A sandboxed `xcodebuild` run failed before project code because CoreSimulator and SwiftPM cache access were blocked. The same focused command passed with required simulator/cache access.
  - The latest `OrlixTestRunner` diagnostic report remains `/Users/rudironsoni/Library/Logs/DiagnosticReports/OrlixTestRunner-2026-06-12-032807.ips`; no newer `OrlixTestRunner` crash report was present after the focused OrlixOS storage proof.
- Final marker(s):
  - Copied environment images preserve the parent base/state bytes captured at copy time.
  - Later parent base/state image writes do not alter the copied environment images.
  - Existing copy behavior still refuses missing parent images and existing destinations.

**Next:**

Do not treat this as runtime reboot persistence. The current app-hosted runtime path still allows only one `OrlixBoot` per XCTest process. A later proof must run writer and reader boots in separate process invocations or add a kernel-supported reboot/reset test path without weakening the one-kernel architecture.

### 2026-06-12 Focused virtio-blk environment proof on iOS Simulator

**What happened:**

Added a focused app-hosted upstream test gate for the existing `virtio_blk_environment_probe`.

Changes made during this checkpoint:

- `OrlixTestRunner/Sources/OrlixUpstreamTestRunner.swift` now has `.kernelVirtioBlockEnvironment`, which runs only `virtio_blk_environment_probe` through the existing `orlix.kselftest=` selector.
- `OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift` now asserts the Linux-visible facts from that probe instead of only relying on the full kselftest run to mention the probe name.
- The focused proof checks that `/dev/vda` and `/dev/vdb` are visible as Linux block devices, that sysfs reports `/dev/vda` as read-only and `/dev/vdb` as writable, that both devices serve sector reads through the Linux block layer, that `/dev/vda` rejects sector writes, and that `/dev/vdb` accepts sector writes.

This is iOS Simulator runtime proof. macOS is only the development/build host for Xcode, package generation, and result bundle inspection. It is not the initial runtime target or Linux user model.

**Decision:**

Keep this as a Linux-owned proof through kselftest plus OrlixTestRunner selection. The device remains Linux-visible through normal `/dev`, sysfs, and block-layer behavior. This does not introduce a public Orlix device API and does not move block semantics into OrlixHostAdapter.

**Evidence:**

- Exact command(s):
  - `rtk git diff --check -- OrlixTestRunner/Sources/OrlixUpstreamTestRunner.swift OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/virtio_blk_environment_probe.c OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/Makefile`
  - `rtk rg -n 'virtio_blk_environment_probe' Build/OrlixMLibC/kselftest/release/kselftest-list.txt Build/OrlixKernel/src/linux-6.12-port/tools/testing/selftests/orlix/Makefile`
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixKernelUpstreamTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixKernelUpstreamTests/OrlixKernelUpstreamTests/testVirtioBlockEnvironmentProbeCompletesThroughOrlixOSTerminalSession test`
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_04-12-07-+0200.xcresult`
- Result:
  - Path-scoped whitespace check passed.
  - Generated release kselftest list contains `orlix:virtio_blk_environment_probe`.
  - Focused `testVirtioBlockEnvironmentProbeCompletesThroughOrlixOSTerminalSession` passed on iPhone 17 Pro iOS Simulator, iOS 26.5, arm64: 1 passed, 0 failed, 0 skipped.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_04-12-07-+0200.xcresult`.
- Failed or rejected checks:
  - A sandboxed `xcodebuild` run failed before project code because CoreSimulator and SwiftPM cache access were blocked. The same focused command passed with required simulator/cache access.
  - The latest `OrlixTestRunner` diagnostic report remains `/Users/rudironsoni/Library/Logs/DiagnosticReports/OrlixTestRunner-2026-06-12-032807.ips`; no newer `OrlixTestRunner` crash report was present after the focused virtio-blk proof.
- Final marker(s):
  - `virtio_blk_environment_probe` is now selectable and proved independently from the full kselftest rootfs gate.
  - The current iOS Simulator proof covers Linux-visible base/state block device presence, sysfs flags, sector reads, and writable-state sector writes.

**Next:**

Do not treat this as full environment persistence or overlay completion. The next storage proof should bind this block-device behavior to environment-root persistence across a fresh boot or to the rootfs tar/OCI materialization path, while keeping `/tmp` as Linux tmpfs and avoiding raw host-path rootfs.

### 2026-06-12 Clone-thread exit handshake and full kselftest proof on iOS Simulator

**What happened:**

Fixed the clone-thread runtime failure that was blocking the full upstream-style Orlix kselftest rootfs gate after the named-environment and mount-namespace work.

Changes made during this checkpoint:

- `OrlixHostAdapter/Sources/OrlixHostAdapter/runtime/trap.c` now delivers Linux-active user instruction or memory faults outside the hosted aperture back to OrlixKernel as `ORLIX_HOST_USER_TRAP_MEMORY_FAULT` instead of letting the iOS Simulator app process crash at the Darwin signal layer.
- `OrlixKernel/Sources/ports/orlix/overlay/arch/orlix/mm/init.c` treats hosted user-stack pre-entry mirroring as best-effort. A host mirroring miss must not turn a legal Linux user stack state into a kernel panic; the later host fault path re-enters Linux page-fault handling if the task touches that page.
- `OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/clone_thread_probe.c` now uses the real Linux thread-exit handshake for `CLONE_VM` threads: clone calls pass a child TID pointer, set `CLONE_CHILD_CLEARTID`, wait for the kernel to clear the child TID, then unmap the shared user stack.

The original failure was not an OCI or environment-manager issue. The probe was unmapping a shared `CLONE_VM` stack after a child-side marker but before Linux had completed thread exit. The fixed probe now waits on the Linux clear-child-TID futex contract before stack teardown.

This is iOS Simulator runtime proof. macOS is only the development/build host for Xcode, kbuild host tools, package generation, and result bundle inspection. It is not the initial runtime target or Linux user model.

**Decision:**

Keep the ownership split strict. HostAdapter may classify and deliver host traps to OrlixKernel, but OrlixKernel remains responsible for Linux fault, signal, task, and exit behavior. The clone-thread proof must use upstream Linux synchronization contracts instead of a test-only shortcut.

**Evidence:**

- Exact command(s):
  - `rtk timeout 1200 make kselftest PROFILE=release`
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixKernelUpstreamTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixKernelUpstreamTests/OrlixKernelUpstreamTests/testCloneThreadProbeCompletesThroughOrlixOSTerminalSession test`
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixKernelUpstreamTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixKernelUpstreamTests/OrlixKernelUpstreamTests/testKselftestRootfsCompletesThroughOrlixOSTerminalSession test`
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_04-05-56-+0200.xcresult`
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_04-07-16-+0200.xcresult`
- Result:
  - Release kselftest payload regenerated successfully.
  - Focused `clone_thread_probe` XCTest passed on iPhone 17 Pro iOS Simulator, iOS 26.5, arm64: 1 passed, 0 failed, 0 skipped.
  - Full `testKselftestRootfsCompletesThroughOrlixOSTerminalSession` XCTest passed on iPhone 17 Pro iOS Simulator, iOS 26.5, arm64: 1 passed, 0 failed, 0 skipped.
  - Result bundles:
    - `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_04-05-56-+0200.xcresult`
    - `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_04-07-16-+0200.xcresult`
- Failed or rejected checks:
  - The latest pre-fix full-suite log still contains `not ok 6 - clone_thread_probe`; that log is stale failure evidence and must not be cited as a passing run.
  - A sandboxed rerun failed before project code because CoreSimulator and SwiftPM cache access were blocked. The same commands passed with required simulator/cache access.
  - The latest `OrlixTestRunner` diagnostic report remains `/Users/rudironsoni/Library/Logs/DiagnosticReports/OrlixTestRunner-2026-06-12-032807.ips`; no newer `OrlixTestRunner` crash report was present after the fix.
- Final marker(s):
  - `clone_thread_probe` no longer reports `not ok 6`.
  - The full app-hosted upstream-style kernel rootfs gate passed on iOS Simulator after the clone-thread clear-TID fix.

**Next:**

Do not treat this as OCI support completion. It is a kernel/runtime proof checkpoint for the Linux clone-thread, hosted stack, and user-fault path needed before continuing named environments, rootfs tar import, OCI layout import, and virtio-backed environment storage.

### 2026-06-12 Named environment copy materialization proof on iOS Simulator

**What happened:**

Added OrlixOS support for creating a named environment by copying an already materialized environment. This advances the required order of work: named environments before rootfs tar import and OCI behavior.

Changes made during this checkpoint:

- `OrlixEnvironmentRegistry.copyEnvironment(from:to:rootImageIdentifier:fileManager:)` loads the parent descriptor, requires the parent `base.ext4` and `state.ext4` images to exist, creates destination storage through the existing Orlix storage policy, copies both images into the destination environment root, and persists a descriptor with `.copiedEnvironment(parentID:)`.
- The copied descriptor preserves platform, command defaults, environment, cwd, uid/gid, root mount policy, and explicit mounts. It only changes the environment id, source, and root image identifier.
- The copy operation refuses to overwrite an existing destination environment.
- Focused OrlixOS tests prove the copied images and descriptor metadata live under the destination environment storage root and that missing parent images and existing destinations fail explicitly.

This is still OrlixOS named-environment materialization proof. It does not claim running environment switching inside one live kernel yet. macOS is only the development/build host for Xcode and result bundle inspection. The initial runtime proof target remains iOS Simulator.

**Decision:**

Keep named environment creation in OrlixOS because this is delivered OS Kit/session and storage policy. OrlixKernel still owns Linux execution, mount namespaces, VFS, overlayfs, procfs/devtmpfs/sysfs, credentials, and exec behavior. The copy operation must not become a HostAdapter-owned Linux policy path.

**Evidence:**

- Exact command(s):
  - `rtk git diff --check -- OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentRegistryCopiesNamedEnvironmentImagesAndMetadata -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentRegistryCopyRequiresMaterializedParentImages -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentRegistryCopyDoesNotOverwriteExistingEnvironment test`
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_03-10-00-+0200.xcresult`
- Result:
  - Path-scoped whitespace check passed.
  - Focused OrlixOS XCTest passed on iPhone 17 Pro iOS Simulator, iOS 26.5, arm64: 3 passed, 0 failed, 0 skipped.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_03-10-00-+0200.xcresult`.
- Failed or rejected checks:
  - A sandboxed `xcodebuild` run failed before project code because CoreSimulator and SwiftPM cache access were blocked. The same focused command passed with required simulator/cache access.
- Final marker(s):
  - `default-copy` gets `.copiedEnvironment(parentID: "default")`.
  - Destination `base.ext4` and `state.ext4` match the parent images.
  - Destination storage is under `Application Support/Orlix/environments/default-copy`.
  - Import scratch remains under `tmp/Orlix/imports/default-copy`.
  - Existing destination environments are not overwritten.

**Next:**

Use this as the OrlixOS creation primitive for the first persistent named environment path. The next runtime proof still needs one live OrlixKernel entering a named environment through Linux mount namespace and normal `execve`, not another boot-time-only root selection shortcut.

### 2026-06-12 Virtio-rng upstream gate and hosted stack sync proof on iOS Simulator

**What happened:**

Promoted the existing Linux-owned `virtio_mmio_probe_contract` output into the app-hosted upstream kernel XCTest gate and fixed the hosted execution issues that blocked the gate from reaching the virtio-rng proof.

Changes made during this checkpoint:

- `OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift` now asserts that the upstream run output contains `virtio_mmio_probe_contract` and `upstream hwrng device returns virtio-backed entropy`.
- `OrlixHostAdapter/Sources/OrlixHostAdapter/runtime/trap.c` now routes every valid Linux-TLS user resume through the TLS resume trampoline, not only syscall returns. This keeps host code on host TLS until the final branch back to Linux userspace.
- `OrlixKernel/Sources/ports/orlix/overlay/arch/orlix/mm/init.c` tolerates absent user-stack VMAs during hosted stack pre-sync where Linux has already unmapped the user stack.
- `OrlixKernel/Sources/ports/orlix/kbuild/kernel-rules.mk` now builds Linux kbuild host tools with an explicit macOS SDK and with iOS deployment-target variables removed. This prevents iOS Simulator Xcode build settings from producing an iOS `scripts/basic/fixdep` host tool.
- `OrlixHostAdapter/Sources/OrlixHostAdapter/memory/kernel_mapping.c` now host-page-aligns the raw Mach `vm_deallocate` range in `OrlixHostUnmapPages`. This fixed the deep clone stack host-shadow mapping failure that was still panicking after the kbuild boundary issue was fixed.

This remains iOS Simulator runtime proof. macOS is only the development/build host for Xcode, kbuild host tools, package generation, and result bundle inspection. It is not the initial runtime target or user model.

**Decision:**

Keep virtio-rng proof in the upstream-style Orlix kselftest lane. The Linux-visible proof remains `/dev/hwrng` returning data from the virtio-mmio-backed hwrng device. Host TLS, Mach page mapping, and kbuild host compiler selection remain private host mechanics and must not become Linux-facing ABI.

**Evidence:**

- Exact command(s):
  - `rtk git diff --check -- OrlixHostAdapter/Sources/OrlixHostAdapter/memory/kernel_mapping.c OrlixKernel/Sources/ports/orlix/kbuild/kernel-rules.mk OrlixKernel/Sources/ports/orlix/overlay/arch/orlix/mm/init.c OrlixHostAdapter/Sources/OrlixHostAdapter/runtime/trap.c OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift`
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixKernelUpstreamTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim test`
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_03-03-53-+0200.xcresult`
  - `rtk xcrun xcresulttool get test-results tests --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_03-03-53-+0200.xcresult`
- Result:
  - Path-scoped whitespace check passed.
  - Final upstream kernel XCTest passed on iPhone 17 Pro iOS Simulator, iOS 26.5, arm64: 1 passed, 0 failed, 0 skipped.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_03-03-53-+0200.xcresult`.
- Failed or rejected checks:
  - A sandboxed `xcodebuild` run failed before project code because CoreSimulator and SwiftPM cache access were blocked.
  - Two payload-build attempts failed before tests because Linux kbuild host tool `scripts/basic/fixdep` was built under an iOS Xcode SDK environment. One failure killed the generated iOS host tool with signal 9. Another failure exposed missing macOS headers after iOS SDK variables were removed without providing a macOS SDK.
  - One runtime attempt reached tests but failed with `Kernel panic - not syncing: Orlix: failed to synchronize hosted user stack access page ...` during the deep clone stack case. Host-page-aligning the raw Mach unmap fixed this without changing Linux stack semantics.
- Final marker(s):
  - `boot_profile_contract` completed before the upstream probe list.
  - `clone_thread_probe` completed through the deep stack alloca case.
  - `OrlixKernelUpstreamTests` passed with assertions for `virtio_mmio_probe_contract` and `upstream hwrng device returns virtio-backed entropy`.

**Next:**

Do not treat this as full virtio device-plane completion. It proves the current virtio-mmio hwrng path in the app-hosted upstream lane. Continue with the next device-plane proof only after preserving this gate and keeping the runtime target iOS Simulator-only for the initial stage.

### 2026-06-12 Linux path descriptor defaults proof on iOS Simulator

**What happened:**

Added and proved a focused app-hosted runtime check for Linux-shaped descriptor defaults in an OCI-derived materialized environment root.

The checkpoint specifically covers:

- descriptor executable path `/bin/../bin/sh`;
- descriptor working directory `/tmp/..`;
- an empty argv element after `argv[0]`;
- the following argv element staying in the correct position.

The initial focused run failed before Linux boot with `stale imported environment runtime fixture: fixture OCI-derived was generated from a stale init`. That was correct behavior. The runtime fixture `.ready` marker hash did not match the newly rebuilt packaged `sbin/init`, so the test refused to prove against stale generated ext4 images.

This remains iOS Simulator runtime proof. macOS is only the development/build host for package compilation, fixture materialization, derived data, and result bundle inspection. It is not the initial runtime target, not the Linux user model, and not part of the product contract.

**Decision:**

Keep Linux path normalization owned by Linux path resolution and userspace execution. OrlixOS descriptor validation must reject structurally invalid private boot configuration, but it must not reject Linux-valid absolute paths just because they contain `..`, and it must preserve empty argv strings.

**Evidence:**

- Exact command(s):
  - `rtk timeout 900 make -f OrlixOS/Makefile environment-runtime-proof-fixtures PROFILE=release`
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests/testOCIDerivedMaterializedRootRunsLinuxPathDescriptorDefaults test`
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.12_01-04-33-+0200.xcresult`
- Result:
  - Fresh `environment-runtime-proof-fixtures` rebuilt tar-imported and OCI-imported ext4 images from the current packaged `init`.
  - Focused OCI-derived Linux path descriptor proof passed on iPhone 17 Pro iOS Simulator: 1 passed, 0 failed, 0 skipped.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.12_01-04-33-+0200.xcresult`.
- Final marker(s):
  - The OCI-derived environment command executed through `/bin/../bin/sh`.
  - Shell output included `argv0=linux-path-argv0`.
  - Shell output included an empty `argv1=`.
  - Shell output included `argv2=argument after empty`.
  - Shell output included `pwd=/`.

**Next:**

Decide whether `OrlixPTYRuntimeTests` should invoke `environment-runtime-proof-fixtures` automatically during its build path or keep the current explicit fixture rebuild preflight. The stale-fixture guard is working, but a focused `xcodebuild` invocation alone can still fail before boot when package inputs were rebuilt separately.

### 2026-06-11 OCI-derived descriptor execution runtime proof on iOS Simulator

**What happened:**

Added focused app-hosted proof that an OCI-derived materialized environment root launches the descriptor-provided command through Linux userspace and observes descriptor-provided argv, environment, working directory, uid, and gid.

Fixes required before the proof could pass:

- `OrlixOS/Sources/init/init.c` now treats only spaces as `/proc/cmdline` token separators and strips a single trailing newline from `/proc/cmdline`. Percent-encoded newlines must stay inside values until decode.
- `ORLIX_INIT_VALUE_SIZE` was raised from 256 to 2048 so descriptor arguments and environment values are not silently rejected before `execve`.
- `OrlixOS/Makefile` normalizes generated runtime fixture ownership and modes inside the ext4 image with debugfs metadata commands. This prevents development-host metadata from leaking into the Linux fixture image.
- `OrlixTestRunner/Tests/XCTest/OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests.swift` constructs descriptor proof markers at shell runtime, not literally in the kernel command line. This prevents the boot log from satisfying completion before the descriptor command has executed.

This is iOS Simulator runtime proof. The host involved here is only the development/build host that creates fixture images and result bundles. It is not a macOS runtime target.

**Decision:**

Keep the descriptor execution path as OrlixOS boot/init configuration for materialized environment roots. This proves OCI-derived root execution fields are carried into Linux userspace. It still does not claim full OCI Runtime Spec lifecycle, registry pull, `orlix run`, or one-kernel named-environment switching.

**Evidence:**

- Exact command(s):
  - `rtk timeout 1200 make -f OrlixOS/Makefile kernel-payload PROFILE=release`
  - `rtk timeout 900 make -f OrlixOS/Makefile environment-runtime-proof-fixtures PROFILE=release`
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath /Volumes/1TB/Xcode/DerivedData/OrlixSystem-active -clonedSourcePackagesDirPath /Volumes/1TB/Xcode/SourcePackages -resultBundlePath /Volumes/1TB/Xcode/Results/OrlixPTYRuntimeTests-descriptor-execution-7.xcresult -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests/testOCIDerivedMaterializedRootBindsDescriptorExecutionDefaults test`
  - `rtk xcrun xcresulttool get test-results summary --path /Volumes/1TB/Xcode/Results/OrlixPTYRuntimeTests-descriptor-execution-7.xcresult`
- Result:
  - Release `kernel-payload` rebuilt and produced `Build/OrlixKernel/payload/OrlixKernelPayload.bundle`.
  - Fresh `environment-runtime-proof-fixtures` rebuilt both tar-imported and OCI-imported images.
  - Focused OCI-derived descriptor execution proof passed on iPhone 17 Pro iOS Simulator: 1 passed, 0 failed, 0 skipped.
  - Result bundle: `/Volumes/1TB/Xcode/Results/OrlixPTYRuntimeTests-descriptor-execution-7.xcresult`.
- Failed or rejected checks:
  - Earlier runtime proof attempts failed because `/bin/sh` and `/bin/bash` in the generated ext4 fixture had development-host uid/gid and mode metadata. The fixture generator now normalizes uid/gid/mode in the ext4 image.
  - Earlier attempts failed because `orlix.argv2` was rejected by the 256-byte init value buffer and `/bin/sh -c` received no script argument.
  - A long multi-line descriptor script exposed a remaining PID 1 SIGSEGV and kernel panic with `exitcode=0x0000000b`. The passing proof uses a shorter one-line descriptor script. The SIGSEGV is a real follow-up bug, not part of the proof claim.
  - One failed run was caused by the completion detector seeing `ORLIX_ENV_EXEC_DONE` in the kernel command line before the descriptor command executed. The marker is now assembled at shell runtime.
- Final marker(s):
  - In an OCI-derived materialized root on iOS Simulator, descriptor-provided `/bin/sh -c ...`, `argv[0]`, `argv[1]`, environment, cwd `/tmp`, uid `1000`, and gid `100` are observable from Linux userspace output.

**Next:**

File or implement a focused regression for the long descriptor argument/PID 1 SIGSEGV path before relying on large OCI command scripts. Then continue toward named environments through Linux mount namespaces rather than expanding boot-time root selection into the final environment model.

### 2026-06-11 Descriptor argv/env/cwd/user binding into OrlixOS init

**What happened:**

Extended the environment descriptor execution binding from executable path only to the process launch fields required by OCI-derived environments:

- `OrlixEnvironmentRootImage.materializedKernelCommandLine` now emits `orlix.argvN`, sorted `orlix.envN`, `orlix.cwd`, `orlix.uid`, and `orlix.gid` tokens in addition to `orlix.exec`;
- descriptor values are percent-encoded for the Linux init command line so arguments and environment values can contain spaces without changing the Linux-facing execution surface;
- unsafe argument, environment, and working-directory values are rejected in OrlixOS descriptor materialization before boot;
- `OrlixOS/Sources/init/init.c` decodes the private OrlixOS boot tokens and launches the PTY child through native Linux `chdir`, `setgid`, `setuid`, and `execve`;
- `/bin/sh -i` remains the fallback when no descriptor launch configuration is provided.

This is still boot-time root execution configuration. It is not one-kernel named-environment switching, full OCI Runtime Spec support, registry pull, or a Docker/runc-compatible lifecycle.

**Decision:**

Keep descriptor-to-init binding in OrlixOS. OCI image metadata may populate the Orlix environment descriptor, but OrlixKernel still owns the Linux behavior behind `chdir`, credentials, fd state, PTY, wait/exit, and `execve`.

**Evidence:**

- Exact command(s):
  - `rtk env CLANG_MODULE_CACHE_PATH=.build/ModuleCache SWIFT_MODULE_CACHE_PATH=.build/ModuleCache swiftc -typecheck OrlixOS/Sources/Session/OrlixOS.swift OrlixOS/Sources/Session/OrlixStoragePolicy.swift OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Sources/Session/OrlixRootfsImport.swift OrlixOS/Sources/Session/OrlixOCIImageLayout.swift OrlixOS/Sources/Session/OrlixEnvironmentImageMaterialization.swift`
  - `rtk git diff --check -- OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Sources/init/init.c OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
  - `rtk timeout 1200 make -f OrlixOS/Makefile kernel-payload PROFILE=release`
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath /Volumes/1TB/Xcode/DerivedData/OrlixSystem-active -clonedSourcePackagesDirPath /Volumes/1TB/Xcode/SourcePackages -resultBundlePath /Volumes/1TB/Xcode/Results/OrlixOSTests-execution-defaults.xcresult -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentRootImageBindsDefaultCommandExecutableToInit -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentRootImageBindsEncodedExecutionDefaultsToInit -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentRootImageRejectsUnsafeExecutionDefaults test`
  - `rtk xcrun xcresulttool get test-results summary --path /Volumes/1TB/Xcode/Results/OrlixOSTests-execution-defaults.xcresult`
- Result:
  - OrlixOS session sources typechecked.
  - Path-scoped whitespace check passed.
  - Release `kernel-payload` rebuilt and produced `Build/OrlixKernel/payload/OrlixKernelPayload.bundle`.
  - Focused descriptor execution-default tests passed on iPhone 17 Pro simulator: 3 passed, 0 failed, 0 skipped.
  - Result bundle: `/Volumes/1TB/Xcode/Results/OrlixOSTests-execution-defaults.xcresult`.
- Failed or rejected checks:
  - Sandbox-local `xcodebuild` failed before running project code because CoreSimulator, SwiftPM cache, and external result bundle access were blocked. The same command passed with required external access.
  - Direct Darwin-SDK syntax checking for `init.c` remains invalid proof for this file because it uses Linux userspace headers and Linux `mount`/PTY constants.
- Final marker(s):
  - OrlixOS descriptors can now carry executable, argv, environment, cwd, uid, and gid into the first Linux userspace process for a materialized environment root.
  - The change has build proof through packaged payload rebuild and focused Xcode proof for descriptor encoding/validation.

**Next:**

Add runtime proof that these fields affect the launched Linux process observable from userspace output, then move back to one-kernel named environments through Linux mount namespaces instead of expanding boot-time root selection.

### 2026-06-11 Environment entry probe added, runtime gate blocked by clone thread regression

**What happened:**

Added a Linux-owned kselftest probe for the next named-environment primitive:

- `environment_entry_probe` creates a child with `fork`;
- the child enters a new mount namespace with `unshare(CLONE_NEWNS)`;
- the child mounts a `tmpfs` root, writes a minimal `/etc/os-release`, copies `/proc/self/exe` into the new root, then uses `chroot` and `execve`;
- the re-execed process verifies it is running from the new root and cannot see a parent-root marker;
- the parent verifies the child exited cleanly and the child root is hidden from the parent namespace.

This is not a named environment manager yet. It is a kernel-owned proof of the Linux primitives that named environment entry must use later.

**Decision:**

Keep the proof in `tools/testing/selftests/orlix`, not in OrlixOS policy code. OrlixOS may later orchestrate environment entry, but the primitive behavior must remain upstream Linux syscall, VFS, mount namespace, chroot, and exec behavior.

**Evidence:**

- Exact command(s):
  - `rtk git diff --check -- OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/environment_entry_probe.c OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/Makefile OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift`
  - `rtk timeout 1200 make -f OrlixKernel/Makefile kselftest PROFILE=release`
  - `rtk sed -n '1,60p' Build/OrlixMLibC/kselftest/release/kselftest-list.txt`
  - `rtk xcrun simctl shutdown 29DFD45B-3C6B-4B3E-A5C6-565C1DD7B5CE`
  - `rtk xcrun simctl erase 29DFD45B-3C6B-4B3E-A5C6-565C1DD7B5CE`
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixKernelUpstreamTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath /Volumes/1TB/Xcode/DerivedData/OrlixSystem-oci-env-proof -only-testing:OrlixKernelUpstreamTests/OrlixKernelUpstreamTests/testKselftestRootfsCompletesThroughOrlixOSTerminalSession test`
  - `rtk xcrun xcresulttool get test-results summary --path /Volumes/1TB/Xcode/DerivedData/OrlixSystem-oci-env-proof/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.11_20-19-02-+0200.xcresult`
- Result:
  - Path-scoped whitespace check passed.
  - Kselftest compile and packaging passed after freeing simulator/build space.
  - `kselftest-list.txt` contains `orlix:environment_entry_probe`.
  - The app-hosted runtime test installed and launched after moving DerivedData to `/Volumes/1TB` and erasing the full simulator.
  - The runtime gate failed before proving the new environment-entry assertion because `clone_thread_probe` reported `not ok 6 - clone_thread_probe`.
  - `.xcresult` summary: 0 passed, 1 failed, 0 skipped.
- Environment cleanup needed during proof:
  - Initial `gen_init_cpio` packaging failed with `writing filebuf failed` because the main volume had about 103 MiB free.
  - Removed exact disposable `.deriveddata` files, then erased the test simulator to recover about 3 GiB.
  - Reran with `-derivedDataPath /Volumes/1TB/Xcode/DerivedData/OrlixSystem-oci-env-proof`.
- Final marker(s):
  - The new environment-entry probe is compiled and packaged.
  - Runtime proof is not green. The current blocker is the freshly rebuilt kernel upstream test lane failing at `clone_thread_probe`, before the new environment-entry proof can be accepted.

**Next:**

Fix or isolate the `clone_thread_probe` regression in the hosted Linux thread/syscall path. Do not weaken `OrlixUpstreamTestOutputParser`, do not ignore `not ok`, and do not claim environment-entry runtime proof until the upstream kernel test lane reaches and validates `environment_entry_probe` without earlier failures.

### 2026-06-11 Mount namespace proof promoted into kernel upstream runtime gate

**What happened:**

Promoted the existing Orlix kselftest mount namespace probe into an explicit app-hosted upstream kernel proof gate.

- `mount_namespace_probe` is present in the Orlix kselftest list and runs through the normal kselftest initramfs path;
- the probe exercises Linux `fork`, `unshare(CLONE_NEWNS)`, `mount("tmpfs")`, `umount`, and parent visibility through normal syscalls;
- `OrlixKernelUpstreamTests` now asserts that the app-hosted kselftest output contains both `mount_namespace_probe` and `child tmpfs mount is hidden from parent`;
- this is a Phase 2/4 proof prerequisite for named environments because environment entry must rely on upstream Linux mount namespaces, not boot-time root switching or HostAdapter path policy.

**Decision:**

Keep the proof in the upstream-style Orlix kselftest lane. Do not add OrlixOS-owned mount namespace semantics. OrlixOS may orchestrate sessions and fixtures, but the namespace behavior remains Linux-owned.

**Evidence:**

- Exact command(s):
  - `rtk git diff --check -- OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/mount_namespace_probe.c OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/Makefile`
  - `rtk ls Build/OrlixMLibC/kselftest/release/orlix`
  - `rtk sed -n '1,80p' Build/OrlixMLibC/kselftest/release/kselftest-list.txt`
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixKernelUpstreamTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixKernelUpstreamTests/OrlixKernelUpstreamTests/testKselftestRootfsCompletesThroughOrlixOSTerminalSession test`
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.11_20-07-40-+0200.xcresult`
- Result:
  - Path-scoped whitespace check passed.
  - Installed kselftest directory contains `mount_namespace_probe`.
  - Installed kselftest list contains `orlix:mount_namespace_probe`.
  - Focused app-hosted kernel upstream runtime proof passed: 1 passed, 0 failed, 0 skipped.
- Diagnostics note:
  - `xcodebuild` became silent while `simctl diagnose --timeout=600` was collecting diagnostics after the app test had ended. The diagnostic process was stopped by PID, after which `xcodebuild` returned exit code 0 and the `.xcresult` summary reported `Passed`.
- Final marker(s):
  - The app-hosted kernel upstream proof now fails if the mount namespace probe or its parent-hidden tmpfs result is missing from the terminal output.

**Next:**

Use this proof as the baseline before adding a Linux userspace environment-entry command or OrlixOS SPI that creates a mount namespace, selects an environment root, and launches through normal Linux `execve` inside the running kernel.

### 2026-06-11 Packaged descriptor binding runtime revalidation

**What happened:**

Revalidated the descriptor command binding against packaged payload and fresh environment runtime fixtures after changing `OrlixOS/Sources/init/init.c`.

The stale OCI runtime fixture failure was expected because the fixture metadata tracks the packaged init binary hash. Rebuilt the runtime proof fixtures before rerunning the OCI-derived overlay mutation proof.

**Decision:**

Keep the current descriptor execution binding scoped to OrlixOS boot/init configuration. This checkpoint proves the changed init source is present in rebuilt packaged fixtures and does not regress the focused OCI-derived overlay runtime proof. It still does not claim full OCI Runtime Spec execution, argv/env/cwd/user mapping, or one-kernel named-environment switching.

**Evidence:**

- Exact command(s):
  - `rtk timeout 1200 make -f OrlixOS/Makefile kernel-payload PROFILE=release`
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixPTYRuntimeTests/OrlixPTYRuntimeTests/testLinuxPTYCarriesInteractiveShellInputAndOutput test`
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.11_19-56-50-+0200.xcresult`
  - `rtk timeout 900 make -f OrlixOS/Makefile environment-runtime-proof-fixtures PROFILE=release`
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests/testOCIDerivedMaterializedRootUsesLinuxOverlayCopyUpAndWhiteout test`
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.11_20-04-23-+0200.xcresult`
- Result:
  - Full OrlixOS release `kernel-payload` rebuild passed after the earlier Coreutils source failure.
  - Default PTY runtime proof passed: 1 passed, 0 failed, 0 skipped.
  - Fresh `environment-runtime-proof-fixtures` rebuild passed and rematerialized both `tar-imported` and `oci-imported` root images.
  - Focused OCI-derived overlay copy-up and whiteout runtime proof passed: 1 passed, 0 failed, 0 skipped.
- Failed or rejected checks:
  - The first OCI-derived overlay proof after the init change failed before execution with `stale imported environment runtime fixture: fixture OCI-derived was generated from a stale init`. This was resolved by rebuilding `environment-runtime-proof-fixtures`.
- Final marker(s):
  - Packaged init command binding is present in fresh runtime fixtures.
  - The focused OCI-derived materialized root still boots through upstream Linux, uses the writable overlay state disk, performs copy-up, hides the lower file through unlink, and exits cleanly in a fresh XCTest app process.

**Next:**

Move from boot-time root selection toward one-kernel named environments only after mount namespace and environment entry are implemented through Linux-owned behavior. Do not expand this boot-time proof into a substitute for real environment switching.

### 2026-06-11 Descriptor command binding into OrlixOS init

**What happened:**

Added the first execution binding from an Orlix environment descriptor into Linux userspace init:

- `OrlixEnvironmentRootImage.materialized` now preserves explicit caller-supplied kernel command lines, but default materialized roots append `orlix.exec=<absolute-command-path>` from `descriptor.defaultCommand[0]`;
- unsafe command paths are rejected before boot if they are empty, relative, contain whitespace, contain NUL, or contain `..`;
- `OrlixOS/Sources/init/init.c` reads `orlix.exec=` from `/proc/cmdline` and uses that as the native Linux `execve` target for the PTY child;
- `/bin/sh` keeps the existing interactive `-i` argv behavior, while other executables are launched with argv containing only the executable path for this first narrow binding;
- no OrlixKernel syscall behavior, HostAdapter policy, Darwin API, Apple container runtime, or VM path was added.

This is intentionally not full OCI Runtime Spec argv/env/cwd/user handling yet. It is the first proof that imported image metadata can affect native Linux process execution through OrlixOS-owned boot/init configuration without changing the Linux surface.

**Decision:**

Keep this as a private OrlixOS init contract for now. OCI metadata remains input to the Orlix environment descriptor. OrlixKernel still owns `execve`, process state, fd tables, signals, wait/exit, credentials, VFS, and mount behavior.

**Evidence:**

- Exact command(s):
  - `rtk env CLANG_MODULE_CACHE_PATH=.build/ModuleCache SWIFT_MODULE_CACHE_PATH=.build/ModuleCache swiftc -typecheck OrlixOS/Sources/Session/OrlixOS.swift OrlixOS/Sources/Session/OrlixStoragePolicy.swift OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Sources/Session/OrlixRootfsImport.swift OrlixOS/Sources/Session/OrlixOCIImageLayout.swift OrlixOS/Sources/Session/OrlixEnvironmentImageMaterialization.swift`
  - `rtk git diff --check -- OrlixOS/Sources/init/init.c OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift docs/plans/active/oci-derived-environments-virtio-plane/IMPLEMENT.md`
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentRootImageBindsDefaultCommandExecutableToInit -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentRootImageRejectsUnsafeDefaultCommandExecutable -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentRootImageDefaultsToOverlayRootCommandLine test`
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.11_19-46-42-+0200.xcresult`
  - `rtk clang --target=aarch64-linux-gnu --sysroot=Build/OrlixMLibC/sysroot/release -isystem Build/OrlixMLibC/kernel-headers/release/include -D_GNU_SOURCE -std=c17 -O2 -fhosted -fno-builtin -ffixed-x18 -fno-pie -static -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--image-base=0x0000600000000000 Build/OrlixMLibC/sysroot/release/usr/lib/crt1.o Build/OrlixMLibC/sysroot/release/usr/lib/crti.o OrlixOS/Sources/init/init.c -Wl,--start-group Build/OrlixMLibC/sysroot/release/usr/lib/libc.a Build/OrlixMLibC/sysroot/release/usr/lib/libm.a Build/OrlixMLibC/sysroot/release/usr/lib/libpthread.a Build/OrlixMLibC/sysroot/release/usr/lib/libssp_nonshared.a Build/OrlixMLibC/sysroot/release/usr/lib/libssp.a Build/OrlixMLibC/compiler-rt/release/liborlix_compiler_rt.a -Wl,--end-group Build/OrlixMLibC/sysroot/release/usr/lib/crtn.o -o /private/tmp/orlix-init-command-binding-check`
  - `rtk file /private/tmp/orlix-init-command-binding-check`
- Result:
  - OrlixOS session sources typechecked.
  - Path-scoped whitespace check passed.
  - Focused `OrlixOSTests` command binding tests passed: 3 passed, 0 failed, 0 skipped.
  - `init.c` compiled against the OrlixMLibC Linux sysroot into `/private/tmp/orlix-init-command-binding-check`.
  - The compiled file is `ELF 64-bit LSB executable, ARM aarch64, statically linked`.
- Failed or rejected checks:
  - Direct Darwin-SDK `clang -fsyntax-only OrlixOS/Sources/init/init.c` failed because the file uses Linux userspace headers and constants such as `MS_NOSUID`, `TIOCGPTN`, and the Linux 5-argument `mount`; this was the wrong validation path for this file.
  - Broad `rtk timeout 1200 make -f OrlixOS/Makefile kernel-payload PROFILE=release` failed before packaging with generated Coreutils source error `config.status: error: cannot find input file: 'Makefile.in'`. That is not treated as evidence against the init change, but it means a full payload rebuild was not proven in this checkpoint.
  - Direct make target `Build/OrlixOS/packages/release/install/sbin/init` was blocked by the harness because generated build trees are read-only targets for agents.
- Failure/skip counts:
  - Focused command binding unit tests: 0 failures, 0 skips.
  - Full payload build: failed in Coreutils package preparation.
- Final marker(s):
  - Descriptor executable path is now bound into default materialized-root boot configuration.
  - The changed init source links as a native aarch64 Linux ELF against OrlixMLibC.

**Next:**

Extend the binding to the rest of the process model only after this stays green in packaged runtime proof:

- argv beyond `argv[0]`;
- environment variables;
- working directory;
- uid/gid;
- terminal versus non-interactive command mode;
- exit status propagation;
- one-kernel named-environment entry instead of boot-time root switching.

### 2026-06-11 OrlixBoot one-boot guard for runtime proof isolation

**What happened:**

Added an explicit process-lifetime boot guard at the public `OrlixBoot` boundary:

- `ORLIX_BOOT_STATUS_ALREADY_STARTED = -3` is now part of the OrlixKernel C boot status API;
- `OrlixBoot` validates input first, then atomically claims the process boot slot before entering the Linux handoff path;
- invalid boot handoff failures release the slot again, so rejected configs do not poison later tests;
- successful or unavailable Linux handoff attempts keep the slot claimed because the current in-process upstream Linux instance is not resettable;
- OrlixOS maps `-3` to `OrlixBootStatus.alreadyStarted`;
- PTY and environment-root XCTest runtime proof runners skip later same-process runtime boots with a direct message instead of entering a second kernel and crashing;
- upstream runtime runner reports a concrete `bootAlreadyStarted` error instead of a generic boot failure.

This does not make Orlix multi-environment runtime complete. It prevents a known invalid harness mode from corrupting evidence while the architecture is still one upstream-Linux kernel instance per app process.

**Decision:**

Treat one boot per process as the current runtime invariant until Orlix has a real resettable hosted kernel instance or a process-isolated runtime test launcher. This matches the target architecture better than pretending multiple boot-time root-image selections in one app process are named environments. Named environments still need to become Linux-level roots/mount namespaces inside one running kernel.

**Evidence:**

- Exact command(s):
  - `rtk git diff --check -- OrlixKernel/Sources/include/OrlixKernel.h OrlixKernel/Sources/boot/loader.c OrlixOS/Sources/Session/OrlixOS.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift OrlixTestRunner/Sources/OrlixUpstreamTestRunner.swift OrlixTestRunner/Tests/XCTest/OrlixPTYRuntimeTests/OrlixPTYRuntimeTests.swift OrlixTestRunner/Tests/XCTest/OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests.swift`
  - `rtk xcrun --sdk iphonesimulator clang -fsyntax-only -isysroot /Applications/Xcode.app/Contents/Developer/Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator26.5.sdk -I OrlixKernel/Sources/include -I OrlixKernel/Sources OrlixKernel/Sources/boot/loader.c`
  - `rtk env CLANG_MODULE_CACHE_PATH=.build/ModuleCache SWIFT_MODULE_CACHE_PATH=.build/ModuleCache swiftc -typecheck OrlixOS/Sources/Session/OrlixOS.swift OrlixOS/Sources/Session/OrlixStoragePolicy.swift OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Sources/Session/OrlixRootfsImport.swift OrlixOS/Sources/Session/OrlixOCIImageLayout.swift OrlixOS/Sources/Session/OrlixEnvironmentImageMaterialization.swift`
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testBootStatusMapsAlreadyStartedResult test`
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests test`
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.11_19-38-06-+0200.xcresult`
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests/testOCIDerivedMaterializedRootUsesLinuxOverlayCopyUpAndWhiteout test`
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixKernelHostProofTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim test`
- Result:
  - Path-scoped `git diff --check` passed.
  - `loader.c` C syntax check passed.
  - OrlixOS touched source typecheck passed.
  - Focused OrlixOS boot-status mapping unit test passed.
  - Whole `OrlixEnvironmentRootRuntimeTests` passed without process crash: 1 passed, 2 skipped, 0 failed.
  - Focused OCI-derived overlay mutation proof still passed after the guard.
  - `OrlixKernelHostProofTests` passed.
- Failure/skip counts:
  - Whole environment-root runtime class intentionally skips 2 same-process runtime boots.
  - Focused runtime proof invocations remain required for each environment/root behavior proof until process isolation or one-kernel named-environment execution exists.
- Crash reports checked:
  - No `OrlixTestRunner-2026-06-11-1938*.ips` crash report was produced by the guarded whole-class run.
- Final marker(s):
  - The crash mode from repeated same-process `OrlixBoot` calls is guarded.
  - Focused fresh-process runtime proofs remain the proof lane for boot-time root image execution.

**Next:**

Stop expanding boot-time root-image tests as if they are environment switching. The next architectural step should move named environments into one running Linux instance through Linux-owned root/mount namespace behavior, or add a process-isolated proof runner for independent boot-time root-image proofs.

### 2026-06-11 OCI-derived overlay runtime proof and virtio-blk writeback fix

**What happened:**

Finished the first runtime proof for OCI-derived materialized environment roots using the existing upstream-Linux OrlixKernel boot path:

- materialized OCI-derived roots now default to `rdinit=/init orlix.root=overlay orlix.profile=development`;
- the product initramfs always packages `/init`, so materialized overlay roots do not fall back to `/dev/root`;
- OrlixMLibC now carries the maintained Linux `chroot(2)` sysdep patch needed by the first-stage root init;
- the OCI-derived runtime proof copies fixture root images to a temporary mutable location before destructive overlay mutation checks;
- the overlay proof writes `/etc/os-release`, reads the copied-up file, unlinks it, and verifies the lower file is hidden through normal Linux path behavior;
- the Orlix virtio-mmio block device now normalizes upstream Linux `VIRTIO_BLK_T_BARRIER` on virtio-blk request type before dispatch, and reports host write failures as `VIRTIO_BLK_S_IOERR` instead of `VIRTIO_BLK_S_UNSUPP`.

The important runtime failure fixed in this checkpoint was Linux writeback against the writable state disk:

- before the fix, the overlay proof could reach `ORLIX-ROOT-OVERLAY-READY`, but Linux later reported `operation not supported error, dev vdb, ... op 0x1:(WRITE) flags 0x4800`;
- after the fix, the focused OCI-derived overlay proof passes in a fresh XCTest app process.

**Decision:**

Keep this fix in `OrlixKernel/Sources/ports/orlix/overlay/drivers/orlix/virtio/mmio.c`, because the broken behavior was in the kernel-owned virtio device model. The HostAdapter file writer remains private host I/O and still does not decide Linux block, VFS, overlayfs, or errno semantics.

Do not claim the whole `OrlixEnvironmentRootRuntimeTests` class is green. Running multiple `OrlixBoot` calls in one XCTest app process still crashes the second in-process Linux boot with `pc=0`. That is a separate boot lifecycle limitation. Current valid runtime proofs are focused one-test Xcode invocations, which give each proof a fresh app process.

**Evidence:**

- Exact command(s):
  - `rtk env CLANG_MODULE_CACHE_PATH=.build/ModuleCache SWIFT_MODULE_CACHE_PATH=.build/ModuleCache swiftc -typecheck OrlixOS/Sources/Session/OrlixOS.swift OrlixOS/Sources/Session/OrlixStoragePolicy.swift OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Sources/Session/OrlixRootfsImport.swift OrlixOS/Sources/Session/OrlixOCIImageLayout.swift OrlixOS/Sources/Session/OrlixEnvironmentImageMaterialization.swift`
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentRootImageDefaultsToOverlayRootCommandLine test`
  - `rtk timeout 2400 make -f OrlixMLibC/Makefile build PROFILE=release`
  - `rtk timeout 1200 make -f OrlixOS/Makefile kernel-payload PROFILE=release`
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests/testOCIDerivedMaterializedRootBootsAndExposesOSRelease test`
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests/testTarDerivedMaterializedRootBootsAndExposesOSRelease test`
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests/testOCIDerivedMaterializedRootUsesLinuxOverlayCopyUpAndWhiteout test`
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests test`
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.11_19-27-54-+0200.xcresult`
  - `rtk git diff --check -- OrlixKernel/Sources/ports/orlix/overlay/drivers/orlix/virtio/mmio.c OrlixTestRunner/Tests/XCTest/OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests.swift OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Sources/make/rootfs.mk OrlixMLibC/Sources/patches/0001-linux-implement-chroot-sysdep.patch`
- Result:
  - Swift typecheck passed for the touched OrlixOS session sources.
  - Focused OrlixOS unit test for materialized root overlay command line passed.
  - OrlixMLibC release build passed with the new chroot sysdep patch.
  - OrlixOS release kernel payload rebuild passed and repackaged `Build/OrlixKernel/payload/OrlixKernelPayload.bundle`.
  - Focused OCI-derived os-release runtime proof passed.
  - Focused tar-derived os-release runtime proof passed.
  - Focused OCI-derived overlay mutation runtime proof passed twice after the virtio-blk fix.
  - Path-scoped `git diff --check` passed.
- Failure/skip counts:
  - Whole-class `OrlixEnvironmentRootRuntimeTests` failed: 2 passed, 1 failed.
  - `.xcresult` failure text: `Test crashed with signal segv.`
  - Crash report: `OrlixTestRunner-2026-06-11-192835.ips`, `EXC_BAD_ACCESS`, `SIGSEGV`, faulting thread inside OrlixKernel with `pc=0`.
- Crash reports checked:
  - `~/Library/Logs/DiagnosticReports/OrlixTestRunner-2026-06-11-192835.ips`
- Final marker(s):
  - Fresh-process focused runtime proofs are green.
  - Whole-class multi-boot runtime proof remains blocked by restart-unsafe in-process `OrlixBoot`.

**Next:**

Treat `OrlixBoot` one-boot-per-process behavior as an explicit harness/runtime invariant until a real kernel reset or process-isolated test runner exists. Do not use whole-class runtime XCTest as a proof gate for multiple root environments in the same app process.

### 2026-06-11 Runtime proof blocker: hosted resume reaches init, then PC-zero fault

**What happened:**

Investigated the app-hosted runtime proof for materialized environment roots after Phase 5 root-image registration and ext4 materialization.

The focused imported-root proof now reaches the real Linux boot path:

- virtio-blk exposes the imported environment `base.ext4` as `vda` and `state.ext4` as `vdb`;
- ext4 mounts `vda` as the read-only Linux root;
- devtmpfs mounts;
- upstream Linux launches `/sbin/init` from the selected root image.

The test does not yet prove shell execution. The initial failure was a kernel panic after `/sbin/init` with:

- task `init`, pid `1`;
- `pc=0x0`;
- `addr=0x0`;
- `exitcode=0x0000000b`.

Added and then removed high-volume kernel resume diagnostics. Those diagnostics showed that the ELF entry and early hosted resume path were not inherently zero:

- `/sbin/init` `start_thread` used a valid entry near `0x600000018054`;
- hosted resume used that same valid PC;
- a later diagnostic run reached `sh pid=27`, meaning the imported-root ELF and first user entry can execute.

That diagnostic run changed timing and did not provide a clean proof. After removing the high-volume logs and rebuilding, the PC-zero init panic returned.

Tried a HostAdapter same-thread `setcontext` resume experiment to avoid the timer-thread `thread_suspend`/`thread_set_state` resume race. It compiled and `OrlixHostAdapterTests` passed, but both imported-root and default-root PTY runtime proofs then hung after `Run /sbin/init as init process`. Replaced that experiment with a signal-context resume attempt. That avoided the hang but caused the test runner to exit with code `127` before finishing. The signal-context experiment was reverted.

After reverting the failed resume experiment, `OrlixHostAdapterTests` pass again, but the default-root PTY proof also shows the same PC-zero init panic. That means the active blocker is not specific to OCI, tar import, rootfs contents, or app-private root image registration. It is a hosted Linux userspace resume/trap correctness issue that now gates both the default Orlix root and imported environment roots.

**Decision:**

Do not continue adding OCI/runtime surface on top of this. The next implementation step must fix hosted Linux userspace resume/trap correctness while preserving the upstream Linux ownership boundary:

- OrlixKernel owns `execve`, task register state, signal delivery, and Linux-visible fault semantics.
- OrlixHostAdapter owns only private Darwin/iOS mechanics for entering, trapping, and resuming hosted user code.
- The fix must not move Linux semantics into HostAdapter and must not introduce Darwin, Foundation, POSIX host headers, libc, or MLibC into OrlixKernel.

The failed HostAdapter resume experiments are not kept as implementation direction. They are evidence that `setcontext` from a normal host context and ad hoc signal-context resume are not currently sufficient replacements for the existing timer-thread redirect path.

**Evidence:**

- Exact command(s):
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixHostAdapterTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixHostAdapterTests/OrlixHostAdapterTests test`
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests/testTarDerivedMaterializedRootBootsAndExposesOSRelease test`
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixPTYRuntimeTests/OrlixPTYRuntimeTests/testLinuxPTYCarriesInteractiveShellInputAndOutput test`
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.11_02-19-40-+0200.xcresult`
- Result:
  - `OrlixHostAdapterTests` passed after reverting the failed resume experiment: 5 tests, 0 failures.
  - Imported-root PTY proof reached ext4 root mount and `/sbin/init`, then failed with `pc=0x0` kernel panic before shell marker output.
  - Direct `setcontext` resume experiment compiled and passed HostAdapter unit tests but caused imported-root and default-root PTY proofs to time out after `/sbin/init`.
  - Signal-context resume experiment caused the default-root PTY test runner to exit with code `127`; `.xcresult` reported: `The test runner exited with code 127 before finishing running tests.`
  - After reverting the resume experiment, default-root PTY proof failed with `fatal PTY runtime marker found: Kernel panic` and the kernel log showed `Orlix: user fault task=init pid=1 pc=0x0 ... exitcode=0x0000000b`.
- Failure/skip counts:
  - `OrlixHostAdapterTests`: 0 failures.
  - Imported-root PTY proof: failing, PC-zero init panic.
  - Default-root PTY proof: failing, PC-zero init panic.
- Crash reports checked:
  - No fresh simulator crash report was found in the expected CoreSimulator CrashReporter path for the signal-context failure. `.xcresult` was used as proof for the process exit code.
- Final marker(s):
  - No runtime readiness marker. The environment/rootfs work is blocked by hosted userspace resume/trap correctness.

**Next:**

Add targeted low-volume diagnostics around the existing hosted resume path only:

- last requested resume PC/SP/TLS/frame flags;
- timer-thread `thread_set_state` status;
- trap handler signal number, PC, fault address, and whether the PC-zero signal came from an active user frame;
- kernel-side saved `pt_regs` PC/SP immediately before hosted resume and at fault delivery.

Then fix the smallest ownership-correct layer. A valid fix must make the default PTY proof pass first, then the imported-root PTY proof.

---

### 2026-06-10 Phase 5 app-private root image registration seam

**What happened:**

Added the narrow HostAdapter seam needed for materialized imported environments:

- `orlix_host_resources_register_root_image_files` registers root images whose base/state block images are app-private files rather than payload-relative resources;
- HostAdapter root image records now track whether block images come from payload resources or app-private files;
- payload roots keep the existing behavior: payload base image plus Application Support state copy;
- app-private roots select the provided base/state files directly into the existing block device backend;
- app-private base images remain read-only through the block write path;
- app-private state images are opened writable and expanded to the configured minimum size;
- OrlixOS can register a materialized environment root image with HostAdapter after the existing payload metadata is registered;
- `OrlixEnvironmentRootImage.registerWithHostAdapter()` exposes that OrlixOS-owned registration step.

Added HostAdapter XCTest source coverage for app-private root image file selection, base read, state read/write, state size expansion, and rejection of relative or parent-containing paths.

**Decision:**

Keep the new HostAdapter API as private host mediation only. It accepts already-materialized app-private block image files and feeds the existing selected block-device path. It does not decide Linux VFS semantics, does not unpack rootfs data, and does not expose host paths as Linux paths. OrlixKernel still sees normal block devices.

**Deviation from plan:**

None. This is the HostAdapter storage primitive required before imported environment roots can be selected by `rootImageIdentifier`.

**Evidence:**

- Exact command(s):
  - `rtk env CLANG_MODULE_CACHE_PATH=.build/ModuleCache SWIFT_MODULE_CACHE_PATH=.build/ModuleCache swiftc -typecheck OrlixOS/Sources/Session/OrlixOS.swift OrlixOS/Sources/Session/OrlixStoragePolicy.swift OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Sources/Session/OrlixRootfsImport.swift OrlixOS/Sources/Session/OrlixOCIImageLayout.swift`
  - `rtk swiftc -parse -I OrlixOS/Sources/Session OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
  - `rtk xcrun --sdk iphonesimulator clang -fsyntax-only -isysroot /Applications/Xcode.app/Contents/Developer/Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator26.5.sdk -I OrlixHostAdapter/Sources -I OrlixKernel/Sources/ports/orlix/overlay/arch/orlix/include OrlixHostAdapter/Sources/OrlixHostAdapter/boot/resources.c`
  - `rtk xcrun --sdk iphonesimulator clang -fsyntax-only -fobjc-arc -isysroot /Applications/Xcode.app/Contents/Developer/Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator26.5.sdk -F /Applications/Xcode.app/Contents/Developer/Platforms/iPhoneSimulator.platform/Developer/Library/Frameworks -I OrlixHostAdapter/Sources -I OrlixKernel/Sources/ports/orlix/overlay/arch/orlix/include -DORLIX_HOST_ADAPTER_TEST_LINUX_PAGE_SIZE=4096 OrlixHostAdapter/Tests/XCTest/OrlixHostAdapterTests/OrlixHostAdapterTests.m`
  - `rtk git diff --check -- OrlixHostAdapter/Sources/OrlixHostAdapter/boot/resources.c OrlixHostAdapter/Sources/OrlixHostAdapter/boot/resources.h OrlixHostAdapter/Tests/XCTest/OrlixHostAdapterTests/OrlixHostAdapterTests.m OrlixOS/Sources/Session/OrlixOS.swift OrlixOS/Sources/Session/OrlixEnvironment.swift docs/plans/active/oci-derived-environments-virtio-plane`
  - `rtk rg -n "orlix_host_resources_register_root_image_files|block_images_are_files|OrlixHostEnsureExistingStateBlockFile|registerMaterializedRootImage|registerWithHostAdapter" OrlixHostAdapter/Sources OrlixHostAdapter/Tests OrlixOS/Sources/Session`
  - `rtk rg -n "AppleContainer|apple/container|Containerization|Virtualization\\.framework|Virtualization" OrlixOS/Sources OrlixKernel/Sources/ports/orlix OrlixHostAdapter/Sources project.yml`
  - `rtk rg -n "OrlixKit|IXLand|ixland|IXLAND" OrlixOS/Sources OrlixKernel/Sources/ports/orlix OrlixHostAdapter/Sources project.yml`
- Result:
  - OrlixOS session sources typechecked.
  - Updated OrlixOS XCTest source parsed.
  - HostAdapter `resources.c` syntax check passed against the iPhone Simulator SDK.
  - HostAdapter XCTest source syntax check passed with the simulator XCTest framework path.
  - Path-scoped `git diff --check` passed.
  - Static scan shows the new HostAdapter API, app-private block-image mode, state image sizing helper, and OrlixOS registration glue.
  - Runtime dependency scan returned no Apple container/containerization or Virtualization references in product sources or `project.yml`.
  - Legacy branding scan returned no OrlixKit or IXLand matches in product paths.
- Failure/skip counts:
  - XCTest execution remains pending.
  - No ext4 image creation from staged rootfs yet.
  - No Linux execution from imported environment yet.
- Crash reports checked:
  - Not applicable. No simulator app launched.
- Final marker(s):
  - Source typecheck, C syntax checks, Objective-C test syntax check, static scans, and path-scoped whitespace check only.

**Next:**

Create ext4 base/state images from staged rootfs data, then register and boot a materialized imported environment by root image identifier through the existing Orlix Linux boot path.

---

### 2026-06-10 Phase 4/5 materialized environment root image binding

**What happened:**

Added an OrlixOS-owned materialized environment root-image binding:

- `OrlixEnvironmentRootImage` binds an environment descriptor to its `base.ext4` and `state.ext4` files;
- `OrlixEnvironmentRegistry.materializedRootImage(forEnvironmentID:)` loads the descriptor and layout and returns the boot binding;
- the binding requires the descriptor id to match the storage layout environment id;
- the binding requires both image files to exist under the environment Application Support directory;
- directories are rejected as block image inputs;
- the returned `OrlixBootConfig` uses the environment's `rootImageIdentifier`.

Added tests proving that metadata-only environments are not boot-image materialized, that dummy base/state files under the environment root produce a root-image binding, and that mismatched layouts and directories are rejected.

**Decision:**

Keep this as OrlixOS metadata and readiness validation only. Do not call HostAdapter yet because the current HostAdapter registration path only accepts payload-relative resource names. Imported environment images are app-private files and need a later narrow HostAdapter seam for app-private block images. This step prevents scratch trees or raw host paths from being treated as Linux root truth.

**Deviation from plan:**

None. This strengthens Phase 4/5 before runtime mounting. It does not implement ext4 creation, HostAdapter registration, or Linux execution.

**Evidence:**

- Exact command(s):
  - `rtk env CLANG_MODULE_CACHE_PATH=.build/ModuleCache SWIFT_MODULE_CACHE_PATH=.build/ModuleCache swiftc -typecheck OrlixOS/Sources/Session/OrlixOS.swift OrlixOS/Sources/Session/OrlixStoragePolicy.swift OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Sources/Session/OrlixRootfsImport.swift OrlixOS/Sources/Session/OrlixOCIImageLayout.swift`
  - `rtk swiftc -parse -I OrlixOS/Sources/Session OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
  - `rtk rg -n "OrlixEnvironmentRootImage|materializedRootImage|baseImageURL|stateImageURL|imageIsDirectory|missingImage" OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
  - `rtk git diff --check -- OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift docs/plans/active/oci-derived-environments-virtio-plane`
  - `rtk rg -n "AppleContainer|apple/container|Containerization|Virtualization\\.framework|Virtualization" OrlixOS/Sources OrlixKernel/Sources/ports/orlix OrlixHostAdapter/Sources project.yml`
  - `rtk rg -n "OrlixKit|IXLand|ixland|IXLAND" OrlixOS/Sources OrlixKernel/Sources/ports/orlix OrlixHostAdapter/Sources project.yml`
- Result:
  - OrlixOS session sources typechecked.
  - Updated OrlixOS XCTest source parsed.
  - Static scan shows the root-image binding API and tests.
  - Path-scoped `git diff --check` passed.
  - Runtime dependency scan returned no Apple container/containerization or Virtualization references in product sources or `project.yml`.
  - Legacy branding scan returned no OrlixKit or IXLand matches in product paths.
- Failure/skip counts:
  - XCTest execution remains pending.
  - No ext4 image creation from staged rootfs yet.
  - No HostAdapter app-private root image registration yet.
  - No Linux execution from imported environment yet.
- Crash reports checked:
  - Not applicable. No simulator app launched.
- Final marker(s):
  - Source typecheck, test-source parse, static scans, and path-scoped whitespace check only.

**Next:**

Add ext4 image materialization from staged rootfs data or the narrow HostAdapter app-private block image registration seam, then prove the selected environment root through Linux execution.

---

### 2026-06-10 Phase 8 OCI layout staging importer

**What happened:**

Added the first OCI layout import path in OrlixOS:

- validates `oci-layout` version `1.0.0`;
- selects the requested platform, defaulting to `linux/arm64`;
- verifies manifest, config, and layer blobs by `sha256`;
- validates OCI manifest schema version `2`;
- derives an Orlix environment descriptor from OCI process defaults;
- applies verified uncompressed tar layers into the environment import staging root;
- implements OCI whiteout handling for `.wh.<name>` deletes and `.wh..wh..opq` opaque directories;
- rejects compressed layer media types for now instead of pretending they are supported.

The importer writes only to import scratch and persists environment metadata. It does not execute imported code, does not create ext4 images, does not mount the root, does not depend on Apple container/containerization, and does not move Linux semantics into OrlixOS.

**Decision:**

Keep OCI layout import as OrlixOS-owned image/environment preparation. Runtime binding still needs a Linux-mounted backend through the kernel-owned VFS path. Compressed OCI layers remain an explicit unsupported case until a sandbox-safe decompression path is selected and tested.

**Deviation from plan:**

None. This is Phase 8 scope. It does not add registry pull and does not bypass the earlier tar importer path.

**Evidence:**

- Exact command(s):
  - `rtk env CLANG_MODULE_CACHE_PATH=.build/ModuleCache SWIFT_MODULE_CACHE_PATH=.build/ModuleCache swiftc -typecheck OrlixOS/Sources/Session/OrlixOS.swift OrlixOS/Sources/Session/OrlixStoragePolicy.swift OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Sources/Session/OrlixRootfsImport.swift OrlixOS/Sources/Session/OrlixOCIImageLayout.swift`
  - `rtk swiftc -parse -I OrlixOS/Sources/Session OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
  - `rtk git diff --check -- OrlixOS/Sources/Session/OrlixRootfsImport.swift OrlixOS/Sources/Session/OrlixOCIImageLayout.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift docs/plans/active/oci-derived-environments-virtio-plane`
  - `rtk rg -n "AppleContainer|apple/container|Containerization|Virtualization\\.framework|Virtualization" OrlixOS/Sources OrlixKernel/Sources/ports/orlix OrlixHostAdapter/Sources project.yml`
  - `rtk xcodebuild -list -project OrlixSystem.xcodeproj`
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim test`
- Result:
  - OrlixOS session sources typechecked with module caches redirected into `.build/ModuleCache`.
  - Updated OrlixOS XCTest source parsed.
  - Path-scoped `git diff --check` passed.
  - Runtime dependency scan returned no Apple container/containerization or Virtualization references in product sources or `project.yml`.
  - `xcodebuild -list` succeeded outside the sandbox and confirmed the `OrlixOSTests` scheme exists.
  - The targeted `OrlixOSTests` run was rejected by execution policy before execution because it writes build artifacts and mutates simulator state.
- Failure/skip counts:
  - XCTest execution is pending because the targeted simulator run was rejected by policy before launch.
  - Compressed OCI layers are intentionally unsupported and covered by a test source assertion.
  - ext4 image creation, VFS mount binding, and Linux execution from the imported OCI root remain unimplemented.
- Crash reports checked:
  - Not applicable. The app-hosted test run did not start.
- Final marker(s):
  - Source typecheck, test-source parse, path-scoped whitespace check, and dependency scan only. Full `OrlixOSTests` proof remains pending.

**Next:**

Add the Linux-mounted backend path from staged rootfs data to environment root binding, then prove `/etc/os-release` through Linux execution when app-hosted tests are allowed.

---

### 2026-06-10 Phase 8 OCI gzip layer decode

**What happened:**

Extended OCI layer preparation so common as-is image layers can be consumed:

- added `OrlixOCILayerDecoder`;
- supports uncompressed OCI and Docker tar layer media types;
- supports OCI and Docker gzip layer media types through system `zlib`;
- keeps zstd as an explicit unsupported media type for now;
- routes layer decoding through the OCI layout importer before tar application;
- links `libz.tbd` from the OrlixOS target in `project.yml`;
- links `libz.tbd` from the OrlixOSTests target for deterministic gzip fixture generation;
- adds an importer-level test fixture that gzip-compresses a generated tar layer and proves `/etc/os-release` is staged from the decoded layer;
- regenerated the local disposable Xcode project with XcodeGen so the ignored local project contains the `libz.tbd` framework entry.

This remains OrlixOS import preparation only. No OrlixKernel code imports zlib, Apple SDK compression APIs, Apple container, Apple containerization, or Virtualization.framework.

**Decision:**

Use platform `zlib` in OrlixOS for gzip layer decode because gzip is an image artifact transport format, not Linux kernel semantics. Do not implement gzip in OrlixKernel and do not use host commands to unpack runtime layers.

**Deviation from plan:**

None. This sharpens Phase 8 so imported OCI layouts can use normal gzip-compressed layers instead of requiring modified plain tar layers.

**Evidence:**

- Exact command(s):
  - `rtk env CLANG_MODULE_CACHE_PATH=.build/ModuleCache SWIFT_MODULE_CACHE_PATH=.build/ModuleCache swift -e 'import zlib; print(ZLIB_VERSION)'`
  - `rtk env CLANG_MODULE_CACHE_PATH=.build/ModuleCache SWIFT_MODULE_CACHE_PATH=.build/ModuleCache swiftc -typecheck OrlixOS/Sources/Session/OrlixOS.swift OrlixOS/Sources/Session/OrlixStoragePolicy.swift OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Sources/Session/OrlixRootfsImport.swift OrlixOS/Sources/Session/OrlixOCIImageLayout.swift`
  - `rtk swiftc -parse -I OrlixOS/Sources/Session OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
  - `rtk xcodegen generate`
  - `rtk git diff --check -- project.yml OrlixOS/Sources/Session/OrlixOCIImageLayout.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift docs/plans/active/oci-derived-environments-virtio-plane`
  - `rtk rg -n "AppleContainer|apple/container|Containerization|Virtualization\\.framework|Virtualization" OrlixOS/Sources OrlixKernel/Sources/ports/orlix OrlixHostAdapter/Sources project.yml`
  - `rtk rg -n "libz|zlib|OrlixOCI|OrlixOS" OrlixSystem.xcodeproj/project.pbxproj project.yml`
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOS -configuration Debug -destination 'generic/platform=iOS Simulator' -derivedDataPath .deriveddata/OrlixSystem-sim ARCHS=arm64 ONLY_ACTIVE_ARCH=YES build`
- Result:
  - Swift can import system `zlib`, reporting version `1.2.12`.
  - OrlixOS session sources typechecked.
  - Updated OrlixOS XCTest source parsed.
  - XcodeGen completed after adding `libz.tbd` to both OrlixOS and OrlixOSTests.
  - Path-scoped `git diff --check` passed.
  - Runtime dependency scan returned no Apple container/containerization or Virtualization references in product sources or `project.yml`.
  - The ignored local generated project contains `libz.tbd` for OrlixOS and OrlixOSTests.
  - The arm64-only OrlixOS Xcode build compiled `OrlixOCIImageLayout.swift` and linked `OrlixOS.framework` with `-lz`.
  - The full OrlixOS scheme build still failed later in the `Embed OrlixOS Payload` phase while building payload/package inputs. The captured rtk log was truncated before the final package-script failure detail, but it preserved the successful OrlixOS Swift compile and link lines.
- Failure/skip counts:
  - Full OrlixOS scheme build is not green.
  - XCTest execution remains pending.
  - zstd OCI layers remain unsupported.
  - Runtime execution from the imported environment remains unimplemented.
- Crash reports checked:
  - Not applicable. No simulator app launched.
- Final marker(s):
  - Source typecheck plus Xcode compile/link evidence for `OrlixOS.framework` with `-lz`. Full scheme/test proof remains pending.

**Next:**

Move from import staging into a Linux-mounted backend so imported `/etc/os-release` can be observed through Orlix execution.

---

### 2026-06-10 Phase 5 rootfs tar staging importer

**What happened:**

Added `OrlixRootfsTarImporter` and `OrlixRootfsTarImportResult`:

- prepares environment registry storage;
- clears the environment import staging root;
- materializes validated tar content into `importScratchDirectory/rootfs`;
- persists the rootfs-tar environment descriptor;
- returns the descriptor, storage layout, staging root, and manifest.

Added a deterministic test that stages an Alpine-shaped tar fixture and proves `etc/os-release` lands under the import staging root, the environment descriptor is loadable, and `base.ext4` is still not created by this step.

**Decision:**

Keep tar import staging separate from runtime root binding. This phase creates host-side import input only. Runtime execution still requires a later Linux-mounted backend, likely ext4 image creation exposed through virtio-blk or another Linux mount path.

**Deviation from plan:**

None.

**Evidence:**

- Exact command(s):
  - `rtk swiftc -parse OrlixOS/Sources/Session/OrlixOS.swift OrlixOS/Sources/Session/OrlixStoragePolicy.swift OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Sources/Session/OrlixRootfsImport.swift`
  - `rtk swiftc -parse -I OrlixOS/Sources/Session OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
  - `rtk rg -n "OrlixRootfsTarImporter|OrlixRootfsTarImportResult|stagingRootDirectory|alpine-import|importArchiveData" OrlixOS/Sources OrlixOS/Tests`
  - `rtk git diff --check -- OrlixOS/Sources/Session/OrlixRootfsImport.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift docs/plans/active/oci-derived-environments-virtio-plane`
- Result:
  - Swift parse passed for OrlixOS session sources.
  - Swift parse passed for the updated OrlixOS XCTest source.
  - Static scan shows the tar importer, import result, staging root, and staged Alpine fixture test.
  - Path-scoped `git diff --check` passed for touched OrlixOS and plan files.
- Failure/skip counts:
  - XCTest execution is pending because simulator-backed `xcodebuild test` remains blocked by policy.
  - ext4 image creation from the staging tree is not implemented yet.
- Crash reports checked:
  - Not applicable. The app-hosted test run did not start.
- Final marker(s):
  - Source parse and static scans only. Full OrlixOSTests proof and ext4/environment execution proof remain pending.

**Next:**

Add ext4 image creation from the staged rootfs, register the resulting base/state images with the environment descriptor, and prove the imported `/etc/os-release` through Linux execution.

---

### 2026-06-10 Phase 5 rootfs tar staging materializer

**What happened:**

Added `OrlixRootfsTarMaterializer` to materialize validated tar records into an import staging tree:

- creates directories;
- writes regular file payloads;
- creates symbolic links without following their targets;
- creates hardlinks to previously materialized archive-relative targets;
- refuses destination paths that escape the staging root.

Added deterministic tests for staged `/etc/os-release`, Linux absolute symlink target preservation, hardlink inode identity, and unsafe entry rejection before writing.

**Decision:**

Materialization writes only to a host-side import staging tree. It is not a runtime root and must later feed ext4 image creation or another Linux-mounted backend before execution. The materializer does not execute imported payloads, does not mount anything, and does not depend on Apple container/containerization.

**Deviation from plan:**

None.

**Evidence:**

- Exact command(s):
  - `rtk swiftc -parse OrlixOS/Sources/Session/OrlixOS.swift OrlixOS/Sources/Session/OrlixStoragePolicy.swift OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Sources/Session/OrlixRootfsImport.swift`
  - `rtk swiftc -parse -I OrlixOS/Sources/Session OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
  - `rtk rg -n "OrlixRootfsTarMaterializer|destinationEscapesRoot|destinationOfSymbolicLink|systemFileNumber|os-release-copy" OrlixOS/Sources OrlixOS/Tests`
  - `rtk git diff --check -- OrlixOS/Sources/Session/OrlixRootfsImport.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift docs/plans/active/oci-derived-environments-virtio-plane`
- Result:
  - Swift parse passed for OrlixOS session sources.
  - Swift parse passed for the updated OrlixOS XCTest source.
  - Static scan shows the tar materializer, destination escape error, symlink test, and hardlink inode test.
  - Path-scoped `git diff --check` passed for touched OrlixOS and plan files.
- Failure/skip counts:
  - XCTest execution is pending because simulator-backed `xcodebuild test` remains blocked by policy.
  - ext4 image creation from the staging tree is not implemented yet.
- Crash reports checked:
  - Not applicable. The app-hosted test run did not start.
- Final marker(s):
  - Source parse and static scans only. Full OrlixOSTests proof and ext4/environment execution proof remain pending.

**Next:**

Add ext4 image creation or an equivalent Linux-mounted backend from the staged tree, then bind it to an environment descriptor and prove `/etc/os-release` through Linux execution.

---

### 2026-06-10 Phase 5 rootfs tar manifest reader

**What happened:**

Extended the rootfs tar import boundary into a manifest reader:

- parses ustar headers from `Data`;
- verifies tar header checksums;
- records regular files, directories, symlinks, and hardlinks;
- validates archive entry paths through the rootfs tar path policy;
- preserves symlink targets as payload metadata, including absolute Linux symlink targets;
- rejects unsupported entry types, unsafe archive paths, bad checksums, truncated headers, and truncated payloads.

Also tightened environment ID validation so slash, backslash, and NUL-containing IDs are rejected instead of normalized into a different storage identity.

**Decision:**

Keep this as manifest validation only. It does not unpack payloads, create files, create ext4 images, execute imported code, or depend on Apple container/containerization. Symlink targets are treated as Linux rootfs metadata and must not be followed by host-side import code.

**Deviation from plan:**

None.

**Evidence:**

- Exact command(s):
  - `rtk swiftc -parse OrlixOS/Sources/Session/OrlixOS.swift OrlixOS/Sources/Session/OrlixStoragePolicy.swift OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Sources/Session/OrlixRootfsImport.swift`
  - `rtk swiftc -parse -I OrlixOS/Sources/Session OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
  - `rtk rg -n "OrlixRootfsTarManifestReader|OrlixRootfsTarManifestEntry|invalidChecksum|unsupportedEntryType|TarFixtureEntry" OrlixOS/Sources OrlixOS/Tests`
  - `rtk git diff --check -- OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Sources/Session/OrlixRootfsImport.swift OrlixOS/Sources/Session/OrlixStoragePolicy.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift docs/plans/active/oci-derived-environments-virtio-plane`
- Result:
  - Swift parse passed for OrlixOS session sources.
  - Swift parse passed for the updated OrlixOS XCTest source.
  - Static scan shows the tar manifest reader, manifest entries, checksum errors, unsupported-type errors, and deterministic tar fixtures.
  - Path-scoped `git diff --check` passed for touched OrlixOS and plan files.
- Failure/skip counts:
  - XCTest execution is pending because simulator-backed `xcodebuild test` remains blocked by policy.
  - Rootfs tar materialization is not implemented yet.
- Crash reports checked:
  - Not applicable. The app-hosted test run did not start.
- Final marker(s):
  - Source parse and static scans only. Full OrlixOSTests proof and rootfs materialization proof remain pending.

**Next:**

Implement metadata-preserving materialization from a validated tar manifest into a staging tree or ext4 input tree, then prove `/etc/os-release` comes from the imported environment.

---

### 2026-06-10 Phase 5 rootfs tar import safety boundary start

**What happened:**

Added the first rootfs tar import boundary:

- `OrlixRootfsTarImportPlan` binds a tar archive URL to an Orlix environment descriptor and storage layout;
- the planned descriptor is linux/arm64, rootfs-tar sourced, and defaults to `/bin/sh` through normal Linux execution later;
- `OrlixRootfsTarEntryPathPolicy` normalizes safe relative tar paths;
- the path policy rejects empty, absolute, parent-traversing, and NUL-containing paths.

**Decision:**

Add tar-entry path validation before any unpacking or ext4 materialization. This gives the eventual importer a mandatory safety gate without executing imported payloads and without adding Apple container/containerization or HostAdapter-owned Linux semantics.

**Deviation from plan:**

None.

**Evidence:**

- Exact command(s):
  - `rtk swiftc -parse OrlixOS/Sources/Session/OrlixOS.swift OrlixOS/Sources/Session/OrlixStoragePolicy.swift OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Sources/Session/OrlixRootfsImport.swift`
  - `rtk swiftc -parse -I OrlixOS/Sources/Session OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
  - `rtk rg -n "OrlixRootfsTarImportPlan|OrlixRootfsTarEntryPathPolicy|unsafePath|rootfsTar|alpine-rootfs.tar" OrlixOS/Sources OrlixOS/Tests`
  - `rtk git diff --check -- OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Sources/Session/OrlixRootfsImport.swift OrlixOS/Sources/Session/OrlixStoragePolicy.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift docs/plans/active/oci-derived-environments-virtio-plane`
- Result:
  - Swift parse passed for OrlixOS session sources.
  - Swift parse passed for the updated OrlixOS XCTest source.
  - Static scan shows rootfs tar import planning, path validation, unsafe path errors, and tests.
  - Path-scoped `git diff --check` passed for touched OrlixOS and plan files.
- Failure/skip counts:
  - XCTest execution is pending because simulator-backed `xcodebuild test` remains blocked by policy.
  - Rootfs tar materialization is not implemented yet.
- Crash reports checked:
  - Not applicable. The app-hosted test run did not start.
- Final marker(s):
  - Source parse and static scans only. Full OrlixOSTests proof and rootfs materialization proof remain pending.

**Next:**

Implement tar archive reading and metadata-preserving materialization into an environment root image or staging tree. Then prove `/etc/os-release` comes from the imported environment before attempting OCI layout import.

---

### 2026-06-10 Phase 4 environment registry start

**What happened:**

Added OrlixOS environment registry persistence:

- `OrlixEnvironmentDescriptor` and `OrlixEnvironmentSource` are codable;
- `OrlixEnvironmentRegistry` persists `environment.json` under each environment state directory;
- registry storage roots are injectable for deterministic tests;
- saving metadata prepares environment state, import scratch, and download cache directories;
- saving metadata does not create `base.ext4` or `state.ext4`.

**Decision:**

Keep registry behavior in OrlixOS because it is delivered-OS/session metadata. The registry persists configuration and prepares app-private host storage locations only. It does not create mounts, launch tasks, execute binaries, or decide Linux semantics.

**Deviation from plan:**

None.

**Evidence:**

- Exact command(s):
  - `rtk swiftc -parse OrlixOS/Sources/Session/OrlixOS.swift OrlixOS/Sources/Session/OrlixStoragePolicy.swift OrlixOS/Sources/Session/OrlixEnvironment.swift`
  - `rtk swiftc -parse -I OrlixOS/Sources/Session OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
  - `rtk rg -n "OrlixEnvironmentRegistry|environment.json|base.ext4|state.ext4|copiedEnvironment" OrlixOS/Sources OrlixOS/Tests`
  - `rtk git diff --check -- OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Sources/Session/OrlixStoragePolicy.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift docs/plans/active/oci-derived-environments-virtio-plane`
- Result:
  - Swift parse passed for OrlixOS session sources.
  - Swift parse passed for the updated OrlixOS XCTest source.
  - Static scan shows registry persistence, descriptor JSON path, copied environment source, and base/state image references.
  - Path-scoped `git diff --check` passed for touched OrlixOS and plan files.
- Failure/skip counts:
  - XCTest execution is pending because simulator-backed `xcodebuild test` remains blocked by policy.
- Crash reports checked:
  - Not applicable. The app-hosted test run did not start.
- Final marker(s):
  - Source parse and static scans only. Full OrlixOSTests proof remains pending.

**Next:**

Run `OrlixOSTests` when simulator/build-artifact verification is allowed. Then implement the rootfs tar import validation/materialization path that creates environment base images without executing imported payloads.

---

### 2026-06-10 Phase 4 environment descriptor start

**What happened:**

Added the first OrlixOS-owned named-environment model:

- `OrlixEnvironmentDescriptor` for default, copied, tar-imported, and OCI-layout-derived environment sources;
- Linux-shaped default environment values for platform, command, cwd, uid/gid, and environment;
- `OrlixEnvironmentStorageLayout` for per-environment base/state images under Application Support, import scratch under host temp, and download cache under Caches;
- OrlixOS tests for default descriptor values, storage separation, and unsafe environment ID rejection.

**Decision:**

Keep this as metadata and storage layout only. It does not launch tasks, mount roots, or decide Linux semantics. The later environment entry path must still use Linux mount namespaces, mounts, credentials, fdtable, PTY, wait/exit, and `execve`.

**Deviation from plan:**

None.

**Evidence:**

- Exact command(s):
  - `rtk swiftc -parse OrlixOS/Sources/Session/OrlixOS.swift OrlixOS/Sources/Session/OrlixStoragePolicy.swift OrlixOS/Sources/Session/OrlixEnvironment.swift`
  - `rtk rg -n "OrlixEnvironmentDescriptor|OrlixEnvironmentStorageLayout|defaultEnvironment\\(|Application Support/Orlix/environments|Caches/Orlix/downloads" OrlixOS/Sources OrlixOS/Tests`
  - `rtk git diff --check -- OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Sources/Session/OrlixStoragePolicy.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/Makefile OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/mount_namespace_probe.c OrlixTestRunner/Tests/XCTest/OrlixTestRunnerTests/ArchitectureInvariantTests.swift docs/plans/active/oci-derived-environments-virtio-plane`
- Result:
  - Swift parse passed for the OrlixOS session sources.
  - Static scan shows the environment descriptor, storage layout, Application Support environment paths, and Caches download path tests.
  - Path-scoped `git diff --check` passed for touched files.
- Failure/skip counts:
  - XCTest execution is pending because simulator-backed `xcodebuild test` remains blocked by policy.
- Crash reports checked:
  - Not applicable. The app-hosted test run did not start.
- Final marker(s):
  - Source parse and static scans only. Full OrlixOSTests proof remains pending.

**Next:**

Run `OrlixOSTests` when simulator/build-artifact verification is allowed, then add the environment registry and Linux entry proof after mount namespace proof is green.

---

### 2026-06-10 Phase 2 mount namespace probe start

**What happened:**

Added `mount_namespace_probe` to the Orlix kselftest set. The probe forks a child, enters a new Linux mount namespace with `unshare(CLONE_NEWNS)`, mounts `tmpfs` at `/mnt`, writes a marker inside that child namespace, unmounts it, and verifies the parent never observes the child marker.

**Decision:**

Put the proof in OrlixKernel's upstream-style Linux selftest path because mount namespaces and mount visibility are Linux semantics owned by OrlixKernel/upstream Linux, not OrlixOS or HostAdapter.

**Deviation from plan:**

None.

**Evidence:**

- Exact command(s):
  - `rtk rg -n "mount_namespace_probe|CLONE_NEWNS|child tmpfs mount is hidden" OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix`
  - `rtk git diff --check -- OrlixOS/Sources/Session/OrlixStoragePolicy.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/Makefile OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/mount_namespace_probe.c OrlixTestRunner/Tests/XCTest/OrlixTestRunnerTests/ArchitectureInvariantTests.swift docs/plans/active/oci-derived-environments-virtio-plane`
- Result:
  - `mount_namespace_probe` is listed in `TEST_GEN_PROGS`.
  - Path-scoped `git diff --check` passed for the touched files.
- Failure/skip counts:
  - Kselftest execution is pending because simulator/build execution is still blocked by policy.
- Crash reports checked:
  - Not applicable. The app-hosted test run did not start.
- Final marker(s):
  - Source-level kselftest addition only. Runtime kselftest proof remains pending.

**Next:**

Run the kselftest proof lane when build/simulator verification is allowed and record exact TAP output and final markers.

---

### 2026-06-10 Phase 1 storage policy start

**What happened:**

Added an OrlixOS-owned storage policy surface and tests for the first storage invariants needed before named environments and OCI import:

- persistent Linux state is under Application Support;
- cache/download storage is under Caches;
- host scratch uses the host temporary directory;
- Linux-visible `/tmp` remains Linux `tmpfs`;
- Documents is explicit-mount-only and not root truth.

**Decision:**

Keep this policy in OrlixOS because it is delivered-OS/session metadata. Do not move Linux path semantics into HostAdapter and do not add OrlixKernel policy. HostAdapter continues to provide private host mechanics for block files.

**Deviation from plan:**

None.

**Evidence:**

- Exact command(s):
  - `rtk swiftc -parse OrlixOS/Sources/Session/OrlixStoragePolicy.swift`
  - `rtk rg -n "OrlixStoragePolicy|OrlixDocumentsMountPolicy|linuxTemporaryFilesystemType|explicitMountOnly" OrlixOS/Sources OrlixOS/Tests`
  - `rtk rg -n "mount_if_needed\\(\\\"tmpfs\\\", \\\"/tmp\\\"|Documents|Application Support|Caches|state.img" OrlixOS/Sources OrlixHostAdapter/Sources OrlixOS/Tests/XCTest --glob '!Build/**'`
- Result:
  - `swiftc -parse` passed for `OrlixStoragePolicy.swift`.
  - The storage policy symbols are present in OrlixOS source and OrlixOS tests.
  - Static scan confirms `/tmp` tmpfs mount in `OrlixOS/Sources/init/init.c`, Application Support state path in `OrlixHostAdapter/Sources/OrlixHostAdapter/boot/resources.c`, and no Documents reference in rootfs/init source.
- Failure/skip counts:
  - XCTest execution is still pending because simulator-backed `xcodebuild test` remains blocked by execution policy.
- Crash reports checked:
  - Not applicable. The app-hosted test run did not start.
- Final marker(s):
  - Source parse and static scans only. Full OrlixOSTests proof remains pending.

**Next:**

Run `OrlixOSTests` and `OrlixTestRunnerTests` when simulator/build-artifact verification is allowed, then record exact evidence.

---

### 2026-06-10 Phase 0 guardrail start

**What happened:**

Added the first source-level architecture invariant tests under the existing `OrlixTestRunnerTests` scheme.

**Decision:**

Use XCTest instead of a standalone script for the initial guardrails because `project.yml` already owns the scheme and the tests can inspect the repository from `#filePath` without adding a new target.

**Deviation from plan:**

None.

**Evidence:**

- Exact command(s):
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixTestRunnerTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim test`
  - `rtk rg -n "IXLand|ixland|IXLAND|OrlixKit" project.yml OrlixKernel/Sources OrlixOS/Sources OrlixHostAdapter/Sources OrlixTerminal/Sources OrlixTestRunner/Sources tools --glob '!Build/**'`
  - `rtk rg -n "^\\s*#\\s*include\\s*[<\\\"](CoreFoundation/|Foundation/|Darwin/|mach/|pthread\\.h|unistd\\.h|sys/|fcntl\\.h|errno\\.h|stdio\\.h|stdlib\\.h|string\\.h|OrlixHostAdapter)" OrlixKernel/Sources/ports/orlix/overlay/arch/orlix OrlixKernel/Sources/ports/orlix/overlay/drivers/orlix --glob '*.{c,h,S}'`
  - `rtk rg -n "AppleContainer|apple/container|Containerization|Virtualization\\.framework|Virtualization" project.yml OrlixKernel/Sources OrlixOS/Sources OrlixHostAdapter/Sources OrlixTerminal/Sources OrlixTestRunner/Sources --glob '!Build/**'`
- Result:
  - The Xcode test run was blocked by execution policy before launch because it writes build artifacts and may mutate simulator state.
  - The direct `rg` invariant scans returned no matches.
- Failure/skip counts:
  - XCTest run skipped because it was blocked before execution.
- Crash reports checked:
  - Not applicable. The app-hosted test run did not start.
- Final marker(s):
  - Static scan evidence only. Full XCTest proof remains pending.

**Next:**

Run `OrlixTestRunnerTests` when simulator/build-artifact verification is allowed, fix any false positives or compile issues, then record exact evidence.

---

### 2026-06-10 Phase 5 imported environment ext4 materialization contract

**What happened:**

Added an OrlixOS materialization contract for imported environment roots. It prepares the canonical base tree and overlay state tree and produces the exact `truncate` plus `mke2fs` command shape needed to create `base.ext4` and `state.ext4`.

**Decision:**

Keep ext4 image creation as build or Mac-side packaging work. The iOS OrlixOS runtime records and validates the materialized block images, while HostAdapter only registers the app-private files as private block backings. No formatter execution was added to OrlixKernel, OrlixHostAdapter, or the iOS runtime path.

**Deviation from plan:**

None.

**Evidence:**

- Exact command(s):
  - `rtk env CLANG_MODULE_CACHE_PATH=.build/ModuleCache SWIFT_MODULE_CACHE_PATH=.build/ModuleCache swiftc -typecheck OrlixOS/Sources/Session/OrlixOS.swift OrlixOS/Sources/Session/OrlixStoragePolicy.swift OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Sources/Session/OrlixRootfsImport.swift OrlixOS/Sources/Session/OrlixOCIImageLayout.swift OrlixOS/Sources/Session/OrlixEnvironmentImageMaterialization.swift`
  - `rtk swiftc -parse -I OrlixOS/Sources/Session OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
  - `rtk rg -n "Process\\(|NSTask|Foundation\\.Process|mke2fs\\(|Virtualization|AppleContainer|Containerization" OrlixOS/Sources/Session OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift OrlixKernel/Sources/ports/orlix OrlixHostAdapter/Sources project.yml`
  - `rtk git diff --check -- OrlixOS/Sources/Session/OrlixEnvironmentImageMaterialization.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
- Result:
  - Swift typecheck passed for OrlixOS session sources, including the new materialization file.
  - Swift parse passed for `OrlixTerminalSessionTests.swift`.
  - Static scan returned no matches for runtime `Process` execution, Apple container, Containerization, or Virtualization references in the checked runtime paths.
  - `git diff --check` passed for the touched materialization source and test file.
- Failure/skip counts:
  - XCTest execution remains pending because simulator-backed `xcodebuild test` was previously blocked by execution policy.
- Crash reports checked:
  - Not applicable. No app-hosted simulator test run was started.
- Final marker(s):
  - Source-level proof only. Full app-hosted XCTest proof remains pending.

**Next:**

Wire the materialization contract into the tar and OCI import flow so imports can hand back a materialization plan alongside the staged rootfs, then add a packaging or Mac-side execution path that creates real ext4 images before HostAdapter registration.

---

### 2026-06-10 Phase 5 and Phase 8 import-to-materialization binding

**What happened:**

Wired the ext4 materialization contract into rootfs tar import and OCI layout import results. Successful imports now prepare the base input tree and overlay state input tree and return the materialization plan needed to create app-private `base.ext4` and `state.ext4` files.

**Decision:**

The import flow now owns staging-to-image-input preparation because it already owns validated rootfs extraction and environment storage layout selection. It still does not create fake image files and does not execute `mke2fs` from the iOS framework.

**Deviation from plan:**

None.

**Evidence:**

- Exact command(s):
  - `rtk env CLANG_MODULE_CACHE_PATH=.build/ModuleCache SWIFT_MODULE_CACHE_PATH=.build/ModuleCache swiftc -typecheck OrlixOS/Sources/Session/OrlixOS.swift OrlixOS/Sources/Session/OrlixStoragePolicy.swift OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Sources/Session/OrlixRootfsImport.swift OrlixOS/Sources/Session/OrlixOCIImageLayout.swift OrlixOS/Sources/Session/OrlixEnvironmentImageMaterialization.swift`
  - `rtk swiftc -parse -I OrlixOS/Sources/Session OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
  - `rtk rg -n "Process\\(|NSTask|Foundation\\.Process|mke2fs\\(|Virtualization|AppleContainer|Containerization" OrlixOS/Sources/Session OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift OrlixKernel/Sources/ports/orlix OrlixHostAdapter/Sources project.yml`
  - `rtk git diff --check -- OrlixOS/Sources/Session/OrlixEnvironmentImageMaterialization.swift OrlixOS/Sources/Session/OrlixRootfsImport.swift OrlixOS/Sources/Session/OrlixOCIImageLayout.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
- Result:
  - Swift typecheck passed for OrlixOS session sources.
  - Swift parse passed for `OrlixTerminalSessionTests.swift`.
  - Static scan returned no matches for runtime process execution, Apple container, Containerization, or Virtualization references in the checked runtime paths.
  - `git diff --check` passed for the touched import and materialization source and test files.
- Failure/skip counts:
  - XCTest execution remains pending because simulator-backed `xcodebuild test` was previously blocked by execution policy.
- Crash reports checked:
  - Not applicable. No app-hosted simulator test run was started.
- Final marker(s):
  - Source-level proof only. Full app-hosted XCTest proof remains pending.

**Next:**

Add the build or Mac-side execution path that consumes `OrlixEnvironmentImageMaterializationPlan.commands(...)`, creates real ext4 image files, and then registers those files through the existing app-private HostAdapter block-image registration path.

---

### 2026-06-10 Phase 5 Mac-side environment ext4 image target

**What happened:**

Added `OrlixOS/Makefile` target `environment-root-image`. It consumes an already-staged rootfs directory and explicit base/state image output paths, then creates real ext4 images using the same `truncate` plus `mke2fs -d` shape as the product payload path.

**Decision:**

Keep the executable image-materialization path in OrlixOS packaging instead of a separate tool or iOS runtime API. The target requires explicit inputs and writes only the requested `base.ext4` and `state.ext4` outputs. Runtime code still only registers already-materialized app-private block images.

**Deviation from plan:**

None.

**Evidence:**

- Exact command(s):
  - `rtk make -f OrlixOS/Makefile -n environment-root-image ORLIXOS_ENVIRONMENT_STAGING_ROOT=/tmp/orlix-staged-root ORLIXOS_ENVIRONMENT_BASE_IMAGE=/tmp/orlix-env/base.ext4 ORLIXOS_ENVIRONMENT_STATE_IMAGE=/tmp/orlix-env/state.ext4`
  - `rtk mkdir -p /private/tmp/orlix-staged-root-proof`
  - `rtk mkdir -p /private/tmp/orlix-env-proof`
  - `rtk make -f OrlixOS/Makefile environment-root-image ORLIXOS_ENVIRONMENT_STAGING_ROOT=/private/tmp/orlix-staged-root-proof ORLIXOS_ENVIRONMENT_BASE_IMAGE=/private/tmp/orlix-env-proof/base.ext4 ORLIXOS_ENVIRONMENT_STATE_IMAGE=/private/tmp/orlix-env-proof/state.ext4`
  - `rtk ls -l /private/tmp/orlix-env-proof/base.ext4 /private/tmp/orlix-env-proof/state.ext4`
  - `rtk file /private/tmp/orlix-env-proof/base.ext4 /private/tmp/orlix-env-proof/state.ext4`
  - `rtk git diff --check -- OrlixOS/Makefile OrlixOS/Sources/Session/OrlixEnvironmentImageMaterialization.swift OrlixOS/Sources/Session/OrlixRootfsImport.swift OrlixOS/Sources/Session/OrlixOCIImageLayout.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift docs/plans/active/oci-derived-environments-virtio-plane/IMPLEMENT.md`
- Result:
  - Dry run expanded the expected command sequence with `truncate`, `mke2fs -t ext4`, `-U clear`, `-L ORLIXROOT`, `-L ORLIXSTATE`, and `root_owner=0:0`.
  - Real target run completed successfully.
  - `base.ext4` was created at 64.0M and recognized by `file` as Linux ext4 with volume name `ORLIXROOT`.
  - `state.ext4` was created at 32.0M and recognized by `file` as Linux ext4 with volume name `ORLIXSTATE`.
  - `git diff --check` passed for the touched make, source, test, and implementation-log files.
- Failure/skip counts:
  - The proof used an empty staged root. A non-empty imported Alpine or BusyBox rootfs proof remains pending.
  - App-hosted XCTest execution remains pending because simulator-backed `xcodebuild test` was previously blocked by execution policy.
- Crash reports checked:
  - Not applicable. No app-hosted simulator test run was started.
- Final marker(s):
  - Build-side ext4 materialization proof exists. Imported non-empty rootfs and app-hosted execution proof remain pending.

**Next:**

Run the same target against a rootfs tar or OCI layout staging result, then register the generated images through the HostAdapter file-backed root image path and prove OrlixKernel sees the selected block devices.

---

### 2026-06-10 Phase 5 environment image metadata and session binding

**What happened:**

Added a manifest-derived metadata command path for imported rootfs materialization. The tar and OCI importers now write `base-debugfs.commands` under the import scratch directory. The OrlixOS make target accepts `ORLIXOS_ENVIRONMENT_BASE_DEBUGFS_COMMANDS` and applies those commands to the generated base ext4 image.

Added `OrlixLinuxSession` construction from a materialized environment root image. Boot registration now uses the materialized environment image for that session instead of always clearing back to the bundled product roots.

Tightened the `environment-root-image` target so it refuses unsafe root and directory-shaped image output paths before the target reaches its `rm -rf` cleanup step.

**Decision:**

Use e2fsprogs `debugfs` as part of Mac-side materialization to restore Linux uid, gid, and mode metadata into the ext4 image after host-side staging. This keeps Linux metadata correction out of HostAdapter and out of OrlixKernel. The iOS runtime still consumes existing block images only.

**Deviation from plan:**

None.

**Evidence:**

- Exact command(s):
  - `rtk env CLANG_MODULE_CACHE_PATH=.build/ModuleCache SWIFT_MODULE_CACHE_PATH=.build/ModuleCache swiftc -typecheck OrlixOS/Sources/Session/OrlixOS.swift OrlixOS/Sources/Session/OrlixStoragePolicy.swift OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Sources/Session/OrlixRootfsImport.swift OrlixOS/Sources/Session/OrlixOCIImageLayout.swift OrlixOS/Sources/Session/OrlixEnvironmentImageMaterialization.swift`
  - `rtk swiftc -parse -I OrlixOS/Sources/Session OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
  - `rtk make -f OrlixOS/Makefile -n environment-root-image ORLIXOS_ENVIRONMENT_STAGING_ROOT=/tmp/orlix-staged-root ORLIXOS_ENVIRONMENT_BASE_IMAGE=/tmp/orlix-env/base.ext4 ORLIXOS_ENVIRONMENT_STATE_IMAGE=/tmp/orlix-env/state.ext4 ORLIXOS_ENVIRONMENT_BASE_DEBUGFS_COMMANDS=/tmp/orlix-env/base-debugfs.commands`
  - `rtk make -f OrlixOS/Makefile environment-root-image ORLIXOS_ENVIRONMENT_STAGING_ROOT=/private/tmp/orlix-staged-root-nonempty ORLIXOS_ENVIRONMENT_BASE_IMAGE=/private/tmp/orlix-env-nonempty/base.ext4 ORLIXOS_ENVIRONMENT_STATE_IMAGE=/private/tmp/orlix-env-nonempty/state.ext4 ORLIXOS_ENVIRONMENT_BASE_DEBUGFS_COMMANDS=/private/tmp/orlix-env-nonempty/base-debugfs.commands`
  - `rtk /opt/homebrew/opt/e2fsprogs/sbin/debugfs -R 'ls -p /' /private/tmp/orlix-env-nonempty/base.ext4`
  - `rtk /opt/homebrew/opt/e2fsprogs/sbin/debugfs -R 'ls -p /etc' /private/tmp/orlix-env-nonempty/base.ext4`
  - `rtk /opt/homebrew/opt/e2fsprogs/sbin/debugfs -R 'cat /etc/os-release' /private/tmp/orlix-env-nonempty/base.ext4`
  - `rtk /opt/homebrew/opt/e2fsprogs/sbin/debugfs -R 'ls -p /' /private/tmp/orlix-env-nonempty/state.ext4`
  - `rtk make -f OrlixOS/Makefile environment-root-image ORLIXOS_ENVIRONMENT_STAGING_ROOT=/private/tmp/orlix-staged-root-nonempty ORLIXOS_ENVIRONMENT_BASE_IMAGE=/ ORLIXOS_ENVIRONMENT_STATE_IMAGE=/private/tmp/orlix-env-nonempty/state.ext4`
  - `rtk git diff --check -- OrlixOS/Makefile OrlixOS/Sources/Session/OrlixOS.swift OrlixOS/Sources/Session/OrlixEnvironmentImageMaterialization.swift OrlixOS/Sources/Session/OrlixRootfsImport.swift OrlixOS/Sources/Session/OrlixOCIImageLayout.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
  - `rtk rg -n "Process\\(|NSTask|Foundation\\.Process|Virtualization|AppleContainer|Containerization" OrlixOS/Sources/Session OrlixOS/Makefile OrlixKernel/Sources/ports/orlix OrlixHostAdapter/Sources project.yml`
- Result:
  - Swift typecheck passed for OrlixOS session sources.
  - Swift parse passed for `OrlixTerminalSessionTests.swift`.
  - Make dry run showed optional metadata command application after base image creation.
  - Real non-empty proof image contains `/etc/os-release` with content `ID=orlix-nonempty`.
  - `debugfs` shows `/etc` as `040755/0/0` and `/etc/os-release` as `100644/0/0` inside the base ext4 image.
  - `debugfs` shows state image `/upper` and `/work` as `040755/0/0`.
  - Unsafe `ORLIXOS_ENVIRONMENT_BASE_IMAGE=/` was rejected with `refusing unsafe ORLIXOS_ENVIRONMENT_BASE_IMAGE=/` before cleanup or image creation.
  - `git diff --check` passed for the touched files.
  - Static scan returned no matches for runtime process execution, Apple container, Containerization, or Virtualization references in the checked runtime paths.
- Failure/skip counts:
  - The non-empty proof used a tiny staged root, not a full Alpine, BusyBox, or OCI rootfs.
  - Full XCTest execution remains pending.
  - App-hosted boot into a materialized imported environment remains pending.
- Crash reports checked:
  - Not applicable. No app-hosted simulator test run was started.
- Final marker(s):
  - Source-level and Mac-side ext4 materialization proof exist for non-empty content and manifest-applied metadata. App-hosted execution proof remains pending.

**Next:**

Use the written `base-debugfs.commands` from an actual tar or OCI importer result as the input to `environment-root-image`, then register and boot the generated `base.ext4` and `state.ext4` pair through `OrlixLinuxSession(materializedRootImage:)`.

---

### 2026-06-11 Phase 5 materialized root session registration proof

**What happened:**

Added an OrlixOS unit-test path for materialized environment root registration. The test creates an environment descriptor, writes app-private base and state image files, materializes the root-image descriptor, constructs the session binding, and calls `registerWithHostAdapter()` through the OrlixOS materialized root image path.

Regenerated `OrlixSystem.xcodeproj` from `project.yml` so the new OrlixOS session source file is included by the generated Xcode project.

**Decision:**

Do not expose the hidden HostAdapter boot-block selector as OrlixOS API. OrlixOS proves it can register a materialized environment root through the existing file-backed HostAdapter SPI. HostAdapter tests continue to own direct proof that file-backed roots can be selected as readable base and writable state block devices.

**Deviation from plan:**

None.

**Evidence:**

- Exact command(s):
  - `rtk env CLANG_MODULE_CACHE_PATH=.build/ModuleCache SWIFT_MODULE_CACHE_PATH=.build/ModuleCache swiftc -typecheck OrlixOS/Sources/Session/OrlixOS.swift OrlixOS/Sources/Session/OrlixStoragePolicy.swift OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Sources/Session/OrlixRootfsImport.swift OrlixOS/Sources/Session/OrlixOCIImageLayout.swift OrlixOS/Sources/Session/OrlixEnvironmentImageMaterialization.swift`
  - `rtk swiftc -parse -I OrlixOS/Sources/Session OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
  - `rtk git diff --check -- OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
  - `rtk xcodegen generate`
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testMaterializedEnvironmentRootImageRegistersWithHostAdapter test`
  - `rtk tail -n 220 '/Users/rudironsoni/Library/Application Support/rtk/tee/1781129146_xcodebuild_-project_OrlixSystem_xcodepro.log'`
  - `rtk rg -n "error:|missing|refusing|OrlixOS payload|Command PhaseScriptExecution|Embed OrlixOS Payload|mke2fs|required|exit 1" '/Users/rudironsoni/Library/Application Support/rtk/tee/1781129146_xcodebuild_-project_OrlixSystem_xcodepro.log'`
- Result:
  - Swift typecheck passed for OrlixOS session sources.
  - Swift parse passed for `OrlixTerminalSessionTests.swift`.
  - `git diff --check` passed for the touched test file.
  - `xcodegen generate` succeeded and regenerated `OrlixSystem.xcodeproj`.
  - The first targeted XCTest attempt reached Swift compilation and failed because the generated Xcode project did not yet include `OrlixEnvironmentImageMaterialization.swift`.
  - After regeneration, the targeted XCTest build compiled `OrlixEnvironmentImageMaterialization.swift`, `OrlixOS.swift`, `OrlixRootfsImport.swift`, and `OrlixEnvironment.swift`.
  - The second targeted XCTest attempt did not execute the test. It failed in the `Embed OrlixOS Payload` script while rebuilding rootfs inputs.
  - The captured script failure was `/bin/sh: line 1: 20384 Killed: 9 scripts/basic/fixdep ...`, followed by `make: *** [/Users/rudironsoni/src/github/rudironsoni/orlix/OrlixSystem/Build/OrlixOS/rootfs/release/rootfs/initramfs.cpio.gz] Error 2`.
- Failure/skip counts:
  - XCTest execution failed before test launch because payload build preparation failed.
  - App-hosted materialized-environment registration remains unverified by XCTest.
- Crash reports checked:
  - Not applicable. The test app did not launch.
- Final marker(s):
  - Source-level proof exists. Xcode build proof confirms the new source is now part of the generated project. App-hosted XCTest proof remains pending due payload build failure outside the new registration path.

**Next:**

Stabilize or bypass the payload rebuild gate for targeted OrlixOS session tests, then rerun `testMaterializedEnvironmentRootImageRegistersWithHostAdapter` and continue to a boot-selection proof with real materialized ext4 images.

---

### 2026-06-11 Phase 5 app-hosted materialized root registration proof

**What happened:**

Split materialized environment registration away from bundled-root registration. `OrlixOSPayload.registerMaterializedRootImage` now sets the payload root path, clears HostAdapter root images, and registers only the selected app-private `base.ext4` and `state.ext4` pair for that environment.

Added a private-testing registration path that supplies payload metadata directly for unit tests. Added a targeted OrlixOS test that creates app-private base/state image files and proves the materialized environment root registration path succeeds through HostAdapter.

Added `ORLIX_OS_SKIP_PAYLOAD_EMBED=YES` as an explicit test-only Xcode build setting for targeted OrlixOS unit tests that do not need the full packaged rootfs payload. Default product and test builds still embed the payload unless this setting is passed.

**Decision:**

Keep HostAdapter’s boot-block selector private. OrlixOS owns the session-facing materialized environment registration path, while HostAdapter owns private block file registration and selection. The test-only metadata path avoids forcing a full rootfs rebuild for a registration-only unit test.

**Deviation from plan:**

None.

**Evidence:**

- Exact command(s):
  - `rtk xcodegen generate`
  - `rtk env CLANG_MODULE_CACHE_PATH=.build/ModuleCache SWIFT_MODULE_CACHE_PATH=.build/ModuleCache swiftc -typecheck OrlixOS/Sources/Session/OrlixOS.swift OrlixOS/Sources/Session/OrlixStoragePolicy.swift OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Sources/Session/OrlixRootfsImport.swift OrlixOS/Sources/Session/OrlixOCIImageLayout.swift OrlixOS/Sources/Session/OrlixEnvironmentImageMaterialization.swift`
  - `rtk swiftc -parse -I OrlixOS/Sources/Session OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
  - `rtk git diff --check -- OrlixOS/Sources/Session/OrlixOS.swift OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift project.yml`
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testMaterializedEnvironmentRootImageRegistersWithHostAdapter ORLIX_OS_SKIP_PAYLOAD_EMBED=YES test`
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.11_00-15-15-+0200.xcresult`
- Result:
  - `xcodegen generate` succeeded.
  - Swift typecheck passed for OrlixOS session sources.
  - Swift parse passed for `OrlixTerminalSessionTests.swift`.
  - `git diff --check` passed for the touched source, test, and project files.
  - Targeted app-hosted XCTest passed on iPhone 17 Pro simulator.
  - `.xcresult` summary: result `Passed`, total tests `1`, passed `1`, failed `0`, skipped `0`.
- Failure/skip counts:
  - Full OrlixOSTests suite remains unrun.
  - Booting a materialized imported rootfs remains unproven.
  - Full payload rebuild still fails when it tries to rebuild rootfs and `scripts/basic/fixdep` is killed with signal 9.
- Crash reports checked:
  - Not applicable. The targeted test passed and no crash was observed.
- Final marker(s):
  - App-hosted materialized environment root registration proof exists for the OrlixOS to HostAdapter path.

**Next:**

Use a real materialized root image pair in an app-hosted boot-selection test, then move from registration proof to OrlixKernel-visible block device and mount proof.

---

### 2026-06-11 Phase 5 boot-path and HostAdapter block-selection proof

**What happened:**

Added an OrlixKernel host proof test that registers a file-backed environment root image, calls `OrlixBoot` with that environment root identifier, and proves the boot path reaches the registered materialized root image path far enough to fail later on absent boot resources.

Fixed the `OrlixHostAdapterTests` target to include `OrlixHostAdapter/Sources/OrlixHostAdapter/boot/resources.c`, then ran the existing file-backed root image selection test. That test proves HostAdapter selects app-private base/state image files as block devices, exposes the base block read-only, expands the writable state block to the required minimum, and preserves block read/write behavior.

**Decision:**

Do not export hidden HostAdapter block read/capacity symbols through the OrlixKernel framework for OrlixKernel tests. Keep private block-state assertions in `OrlixHostAdapterTests`; use OrlixKernel host proof tests for the public `OrlixBoot` path.

**Deviation from plan:**

None.

**Evidence:**

- Exact command(s):
  - `rtk xcrun --sdk iphonesimulator clang -fsyntax-only -fobjc-arc -isysroot /Applications/Xcode.app/Contents/Developer/Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator26.5.sdk -F /Applications/Xcode.app/Contents/Developer/Platforms/iPhoneSimulator.platform/Developer/Library/Frameworks -I OrlixKernel/Sources/include OrlixKernel/Tests/XCTest/OrlixKernelHostProofTests/OrlixKernelHostProofTests.m`
  - `rtk xcrun --sdk iphonesimulator clang -fsyntax-only -isysroot /Applications/Xcode.app/Contents/Developer/Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator26.5.sdk -I OrlixHostAdapter/Sources -I OrlixKernel/Sources/ports/orlix/overlay/arch/orlix/include OrlixHostAdapter/Sources/OrlixHostAdapter/boot/resources.c`
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixKernelHostProofTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixKernelHostProofTests/OrlixKernelHostProofTests/testBootloaderSelectsMaterializedEnvironmentBlockImagesBeforeInitrdLoad ORLIX_OS_SKIP_PAYLOAD_EMBED=YES test`
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelHostProofTests-2026.06.11_00-23-24-+0200.xcresult`
  - `rtk xcodegen generate`
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixHostAdapterTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixHostAdapterTests/OrlixHostAdapterTests/testAppPrivateRootImageFilesSelectReadableBaseAndWritableStateBlocks test`
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixHostAdapterTests-2026.06.11_00-24-35-+0200.xcresult`
- Result:
  - Objective-C syntax check passed for `OrlixKernelHostProofTests.m`.
  - C syntax check passed for `resources.c`.
  - Targeted OrlixKernel host proof XCTest passed on iPhone 17 Pro simulator.
  - OrlixKernel `.xcresult` summary: result `Passed`, total tests `1`, passed `1`, failed `0`, skipped `0`.
  - `xcodegen generate` succeeded after adding `resources.c` to the HostAdapter test target.
  - Targeted HostAdapter XCTest passed on iPhone 17 Pro simulator.
  - HostAdapter `.xcresult` summary: result `Passed`, total tests `1`, passed `1`, failed `0`, skipped `0`.
- Failure/skip counts:
  - Full OrlixKernelHostProofTests and full OrlixHostAdapterTests suites remain unrun.
  - The OrlixKernel proof intentionally stops before DTB/initrd load because test resources are absent.
  - OrlixKernel mounting the imported environment rootfs remains unproven.
- Crash reports checked:
  - Not applicable. Both targeted simulator tests passed and no crash was observed.
- Final marker(s):
  - App-hosted proof now covers OrlixOS materialized root registration, OrlixBoot root-image identifier routing, and HostAdapter file-backed block selection/read-write behavior.

**Next:**

Use real materialized ext4 images from tar/OCI staging in the boot-selection path, then prove Linux sees `/dev/vda` and `/dev/vdb` with the imported root contents.

---

### 2026-06-11 Phase 7 Linux-visible virtio-blk environment probe

**What happened:**

Added `virtio_blk_environment_probe` to the Orlix kselftest set. The probe stays above the device backend and checks Linux-visible behavior:

- `/dev/vda` exists as the immutable base block device;
- `/dev/vdb` exists as the writable state block device;
- `/sys/block/vda/ro` reports read-only;
- `/sys/block/vdb/ro` reports writable;
- `/sys/block/vda/size` and `/sys/block/vdb/size` report nonzero capacity;
- `/dev/vda` and `/dev/vdb` serve sector reads through normal Linux block-device file operations;
- `/dev/vda` rejects sector writes;
- `/dev/vdb` accepts a same-sector write.

Added the probe to `tools/testing/selftests/orlix/Makefile`.

**Decision:**

Keep this as a Linux-visible contract test. It does not call HostAdapter APIs, does not inspect Orlix private state, and does not treat host paths as Linux truth. The proof target is the normal Linux block layer receiving devices from the existing Orlix virtio-mmio block path.

**Deviation from plan:**

None. This strengthens Phase 7 proof coverage before claiming imported roots are visible through Linux.

**Evidence:**

- Exact command(s):
  - `rtk xcrun --sdk macosx clang -fsyntax-only -Wall -Wextra -Werror OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/virtio_blk_environment_probe.c`
  - `rtk xcrun --sdk macosx clang -fsyntax-only -Wall -Wextra -Werror -Wno-unused-function OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/virtio_blk_environment_probe.c`
  - `rtk git diff --check -- OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/virtio_blk_environment_probe.c OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/Makefile`
  - `rtk rg -n "AppleContainer|apple/container|Containerization|Virtualization\\.framework|Virtualization|\\bvminitd\\b|\\bvmnet\\b|\\bRosetta\\b|\\bDocker\\b|\\brunc\\b" OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/virtio_blk_environment_probe.c OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/Makefile`
- Result:
  - The first strict local syntax check failed because the shared `orlix_kselftest_user.h` helper header defines unused static helpers under `-Werror`.
  - The second syntax check passed with `-Wno-unused-function` while preserving other warnings as errors.
  - Path-scoped `git diff --check` passed.
  - Forbidden runtime dependency scan returned no matches.
- Failure/skip counts:
  - The new probe has not been executed inside the Orlix Linux kselftest environment yet.
  - Linux-visible `/dev/vda` and `/dev/vdb` behavior remains unproven at runtime until the kselftest runs.
  - Imported tar or OCI root contents mounted through `/dev/vda` remain unproven.
- Crash reports checked:
  - Not applicable. No simulator app launched.
- Final marker(s):
  - Source-level kselftest probe exists and syntax-checks. Runtime Linux proof remains pending.

**Next:**

Run the Orlix kselftest payload with `virtio_blk_environment_probe`, then bind the same proof to a materialized root image created from tar or OCI staging.

---

### 2026-06-11 Phase 7 kselftest install proof for virtio-blk environment probe

**What happened:**

Ran the standard repository `kselftest` target for the release profile. The upstream kselftest build flow compiled the Orlix collection against the reused OrlixMLibC sysroot, installed the collection under `Build/OrlixMLibC/kselftest/release`, and packaged the test initramfs bundle under `Build/OrlixMLibC/test-initramfs/release/OrlixTestInitramfs.bundle`.

The installed `kselftest-list.txt` includes `orlix:virtio_blk_environment_probe`, and the installed `orlix` collection contains the `virtio_blk_environment_probe` binary.

**Decision:**

Treat this as build/install proof only. It proves the new probe participates in the OrlixMLibC-built kselftest lane, but it does not prove Linux runtime block-device behavior until the initramfs runs under hosted OrlixKernel and emits TAP.

**Deviation from plan:**

None.

**Evidence:**

- Exact command(s):
  - `rtk timeout 1200 make kselftest PROFILE=release`
  - `rtk sed -n '1,120p' Build/OrlixMLibC/kselftest/release/kselftest-list.txt`
  - `rtk ls Build/OrlixMLibC/kselftest/release/orlix`
  - `rtk ls Build/OrlixMLibC/test-initramfs/release/OrlixTestInitramfs.bundle`
- Result:
  - `make kselftest` exited `0`.
  - The OrlixMLibC-built Orlix kselftest collection compiled `virtio_blk_environment_probe`.
  - The installed list contains `orlix:virtio_blk_environment_probe`.
  - The installed `orlix` directory contains `virtio_blk_environment_probe`.
  - The test initramfs bundle directory exists with `rootfs/`, `Info.plist`, and `initramfs.list`.
- Failure/skip counts:
  - Runtime execution remains pending.
  - No TAP output from `virtio_blk_environment_probe` has been collected yet.
  - No claim is made that `/dev/vda` or `/dev/vdb` works inside a running Orlix Linux instance.
- Crash reports checked:
  - Not applicable. No simulator app launched.
- Final marker(s):
  - `installed OrlixMLibC-built kselftests: /Users/rudironsoni/src/github/rudironsoni/orlix/OrlixSystem/Build/OrlixMLibC/kselftest/release`
  - `packaged kselftest initramfs: /Users/rudironsoni/src/github/rudironsoni/orlix/OrlixSystem/Build/OrlixMLibC/test-initramfs/release/OrlixTestInitramfs.bundle (libc orlixmlibc)`

**Next:**

Run the app-hosted kselftest proof path and inspect the captured TAP for `virtio_blk_environment_probe`.

---

### 2026-06-11 Phase 7 app-hosted kselftest execution proof for virtio-blk probe

**What happened:**

Tightened the app-hosted kernel upstream XCTest path so `OrlixUpstreamXCTest.run(_:)` returns the captured Linux output and `OrlixKernelUpstreamTests.testKselftestRootfsCompletesThroughOrlixOSTerminalSession()` asserts that the output contains `virtio_blk_environment_probe`.

Ran the targeted `OrlixKernelUpstreamTests` scheme against the iPhone 17 Pro simulator. The test passed after booting the kselftest rootfs through the OrlixOS terminal session. Because the parser already rejects `not ok` lines, fatal markers, missing TAP, and missing `ORLIX-KSELFTEST-END`, and the test now asserts the specific probe name, this is app-hosted proof that the installed Orlix kselftest collection included and completed the new virtio-blk environment probe.

**Decision:**

Keep the output-marker assertion in the kernel upstream test instead of relying on the installed kselftest list alone. Build/install proof is insufficient for Linux-visible behavior.

**Deviation from plan:**

None.

**Evidence:**

- Exact command(s):
  - `rtk swiftc -parse OrlixTestRunner/Sources/OrlixUpstreamTestRunner.swift OrlixTestRunner/Tests/XCTest/Support/OrlixUpstreamXCTest.swift OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift`
  - `rtk git diff --check -- OrlixTestRunner/Tests/XCTest/Support/OrlixUpstreamXCTest.swift OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift`
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixKernelUpstreamTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixKernelUpstreamTests/OrlixKernelUpstreamTests/testKselftestRootfsCompletesThroughOrlixOSTerminalSession test`
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.11_00-38-38-+0200.xcresult`
  - `rtk xcrun xcresulttool get test-results tests --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.11_00-38-38-+0200.xcresult`
- Result:
  - Swift parse passed.
  - Path-scoped `git diff --check` passed.
  - Targeted app-hosted kernel upstream XCTest exited `0`.
  - `.xcresult` summary: result `Passed`, total tests `1`, passed `1`, failed `0`, skipped `0`.
  - `.xcresult` test list shows `testKselftestRootfsCompletesThroughOrlixOSTerminalSession()` passed on iPhone 17 Pro simulator, iOS 26.5.
  - The test contains a direct assertion that captured Linux output includes `virtio_blk_environment_probe`.
- Failure/skip counts:
  - Full `OrlixKernelUpstreamTests` is effectively one selected test and it passed.
  - Full wider test suites remain unrun.
  - This proves the default kselftest rootfs block devices, not yet an imported tar or OCI environment root.
  - The test result includes an XCTest runtime warning about a QoS priority inversion in the synchronous proof helper.
- Crash reports checked:
  - Not applicable. The targeted simulator test passed and no crash was observed.
- Final marker(s):
  - App-hosted kselftest proof now covers Linux-visible virtio-blk base/state devices in the kernel test rootfs path.

**Next:**

Bind a materialized tar-imported environment root image to the same boot path and prove imported `/etc/os-release` from Linux userspace.

---

### 2026-06-11 Phase 5 app-hosted tar-derived materialized root boot attempt

**What happened:**

Added a guarded app-hosted runtime proof under `OrlixPTYRuntimeTests`:

- `OrlixEnvironmentRootRuntimeTests.testTarDerivedMaterializedRootBootsAndExposesOSRelease()`;
- resolves a prepared fixture under `Build/OrlixOS/environment-runtime-proof/tar-imported`;
- creates an `OrlixEnvironmentDescriptor` with `source: .rootfsTar`;
- binds `base.ext4` and `state.ext4` through `OrlixEnvironmentRootImage`;
- boots the materialized root through `OrlixLinuxSession(materializedRootImage:)`;
- waits for an interactive shell prompt;
- sends `cat /etc/os-release`;
- asserts `ID=orlix-tar-runtime-proof`.

Prepared a tar-derived fixture manually for the proof run:

- copied the current OrlixOS release base root tree to a staging source;
- replaced only `/etc/os-release` with the proof marker `ID=orlix-tar-runtime-proof`;
- archived the staged root as `rootfs.tar`;
- extracted that tarball into an imported staging root;
- materialized app-private `base.ext4` and `state.ext4` from the extracted root with `OrlixOS/Makefile environment-root-image`;
- wrote `.ready` so the guarded app-hosted test would run instead of skip.

The first app-hosted run skipped because the test resolved the repo root incorrectly as `OrlixTestRunner/Build/...`. Fixed the `#filePath` parent traversal and reran the proof.

**Decision:**

Keep this as an explicit failing runtime proof rather than hide the gap. The current path proves app-private file-backed block registration plus Linux ext4 root mount for a tar-derived root. It does not yet prove successful Linux userspace execution from that imported root.

**Deviation from plan:**

The runtime fixture is prepared by a local proof command rather than a durable Make target. This is acceptable for this slice because the durable implementation still needs a first-class tar-import materialization command. The app-hosted XCTest is fixture-gated so normal test runs without the generated fixture skip instead of failing.

**Evidence:**

- Exact command(s):
  - `rtk /bin/bash -lc 'set -euo pipefail ... make -f OrlixOS/Makefile environment-root-image ...'`
  - `rtk file Build/OrlixOS/environment-runtime-proof/tar-imported/state/environments/tar-imported-runtime-proof/base.ext4 Build/OrlixOS/environment-runtime-proof/tar-imported/state/environments/tar-imported-runtime-proof/state.ext4`
  - `rtk /opt/homebrew/opt/e2fsprogs/sbin/debugfs -R 'cat /etc/os-release' Build/OrlixOS/environment-runtime-proof/tar-imported/state/environments/tar-imported-runtime-proof/base.ext4`
  - `rtk xcodegen generate`
  - `rtk swiftc -parse OrlixTestRunner/Tests/XCTest/OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests.swift`
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests/testTarDerivedMaterializedRootBootsAndExposesOSRelease test`
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.11_00-46-45-+0200.xcresult`
  - `rtk rg -n "OrlixTestRunner|OrlixPTYRuntimeTests|Exception Type|Termination Reason|panic|crash" ~/Library/Logs/DiagnosticReports ~/Library/Developer/CoreSimulator/Devices/29DFD45B-3C6B-4B3E-A5C6-565C1DD7B5CE/data/Library/Logs/CrashReporter --glob '*.crash' --glob '*.ips'`
  - `rtk sed -n '1,180p' /Users/rudironsoni/Library/Logs/DiagnosticReports/OrlixTestRunner-2026-06-11-004741.ips`
  - `rtk shasum -a 256 Build/OrlixOS/rootfs/release/base-tree/sbin/init Build/OrlixOS/environment-runtime-proof/tar-imported/imported-root/sbin/init`
  - `rtk /opt/homebrew/opt/e2fsprogs/sbin/debugfs -R 'dump /sbin/init /private/tmp/orlix-env-init-dump' Build/OrlixOS/environment-runtime-proof/tar-imported/state/environments/tar-imported-runtime-proof/base.ext4`
  - `rtk shasum -a 256 /private/tmp/orlix-env-init-dump Build/OrlixOS/rootfs/release/base-tree/sbin/init`
- Result:
  - The fixture materialization command exited `0`.
  - `file` reports both generated images as Linux ext4 filesystems.
  - `debugfs cat /etc/os-release` from the generated base image prints `ID=orlix-tar-runtime-proof`.
  - `xcodegen generate` succeeded.
  - Swift parse passed for the new XCTest file.
  - The corrected app-hosted test ran and failed with signal `segv`.
  - `.xcresult` summary: result `Failed`, total tests `1`, failed `1`, passed `0`, skipped `0`.
  - Linux boot log in the xcodebuild output shows:
    - `Kernel command line: console=ttyS0 console=hvc0 root=/dev/vda rootfstype=ext4 ro orlix.profile=release`;
    - `virtio_blk virtio0: [vda] 262144 512-byte logical blocks`;
    - `virtio_blk virtio1: [vdb] 131072 512-byte logical blocks`;
    - `EXT4-fs (vda): mounted filesystem ... ro`;
    - `VFS: Mounted root (ext4 filesystem) readonly on device 254:0`;
    - `Run /sbin/init as init process`.
  - Crash report `OrlixTestRunner-2026-06-11-004741.ips` shows `EXC_BAD_ACCESS`, `SIGSEGV`, fault address `0`, faulting thread PC `0`, immediately after hosted Linux entered user execution.
  - The staged imported `/sbin/init`, the product root tree `/sbin/init`, and the `/sbin/init` dumped back out of the generated ext4 image have the same SHA-256.
- Failure/skip counts:
  - The first app-hosted attempt skipped because the test looked under `OrlixTestRunner/Build/...`; this path bug was fixed.
  - The corrected app-hosted proof failed with a hosted-user PC 0 crash.
  - Imported `/etc/os-release` is proven present in the ext4 image, but not yet proven readable from Linux userspace after `/sbin/init`.
- Crash reports checked:
  - Yes. `OrlixTestRunner-2026-06-11-004741.ips` confirms signal `SIGSEGV`, faulting thread PC `0`.
- Final marker(s):
  - Partial runtime proof: tar-derived app-private root image reaches Linux virtio-blk, ext4 mount, VFS root mount, and `/sbin/init` handoff.
  - Missing runtime proof: successful hosted execution of `/sbin/init` and shell command output from imported `/etc/os-release`.

**Next:**

Diagnose the hosted-user PC 0 crash after `Run /sbin/init` when the executable is served from the materialized app-private root image. Start by comparing product-root boot versus materialized-root boot at the hosted exec entry path and page-mapping refresh path.

---

## Deviations Summary

| Deviation | Reason | Plan updated? |
|---|---|---|
| | | |

## Open Questions

- [ ] None for the initial Phase 0 guardrail test shape.

---

## 2026-06-11 Hosted User Return Diagnosis

**Scope:**

Continue Phase 5 runtime proof by fixing the hosted Linux user return path that blocks shell execution from the current app-hosted root environment. This is still a prerequisite for OCI-derived environments because imported roots cannot be considered runnable until normal Linux `fork`/`execve`/syscall-return behavior survives an interactive shell.

**Evidence:**

- Exact command(s):
  - `rtk git diff --check -- OrlixKernel/Sources/ports/orlix/overlay/arch/orlix/mm/init.c OrlixHostAdapter/Sources/OrlixHostAdapter/runtime/trap.c OrlixHostAdapter/Sources/OrlixHostAdapter/memory/kernel_mapping.c OrlixKernel/Sources/ports/orlix/overlay/arch/orlix/kernel/hosted_exec.c`
  - `rtk timeout 1200 make -f OrlixKernel/Makefile PROFILE=release build`
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixHostAdapterTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixHostAdapterTests/OrlixHostAdapterTests test`
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixPTYRuntimeTests/OrlixPTYRuntimeTests/testLinuxPTYCarriesInteractiveShellInputAndOutput test`
- Result:
  - `git diff --check` passed for the active runtime files.
  - Kernel release build exited `0`, produced `Build/OrlixKernel/release/iphonesimulator/OrlixKernel.a`, wrote `Build/OrlixKernel/release/linux-object-manifest.txt`, and packaged `Build/OrlixKernel/xcframework/OrlixKernel.xcframework`.
  - `OrlixHostAdapterTests` passed: 5 tests, 0 failures.
  - PTY runtime proof failed with XCTest timeout after 600 seconds, exit code `65`.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.11_10-45-03-+0200.xcresult`.
  - Terminal log: `/Users/rudironsoni/Library/Developer/CoreSimulator/Devices/29DFD45B-3C6B-4B3E-A5C6-565C1DD7B5CE/data/Containers/Data/Application/6A451063-8D97-4BBA-8E24-2C9BB22376F8/tmp/orlix-pty-runtime-A6D02C72-0AC0-4DAA-9DAF-ABE7FF02F1DF.log`.
- Runtime movement:
  - The earlier `/sbin/init` PC 0 crash after `mkdirat` is no longer the active failure.
  - The app-hosted boot now reaches `/bin/sh` through the init PTY path.
  - The active failure is a `sh` PC 0 fault followed by stalled PTY proof output.
- Key log evidence:
  - `Run /sbin/init as init process`
  - `Orlix: user fault task=sh pid=27 pc=0x0 lr=0x0 sp=0x6fffffcb49f0 addr=0x0 flags=0x2`
  - Later: `Orlix: user fault task=sh pid=27 pc=0x0 lr=0x0 sp=0x0 addr=0x0`
  - Recent `sh` events before the fault include successful syscall-return/resume records for `openat`, `fcntl`, and `clock_gettime`.
  - Later init events show `ppoll` returning `1`, `wait4` returning `0`, then a suspicious `kind=syscall-return task=sh pid=27 pc=0x0 lr=0x0 sp=0x0 x0=0x0 x8=0x0 flags=-1`.

**Current blocker:**

The remaining blocker is not OCI import or storage. It is hosted Linux user return correctness under `fork`/`execve` and scheduler interaction. The most likely current failure class is stale `pt_regs` use after syscall exit work or timer polling changes `current`, because `orlix_hosted_handle_user_syscall()` resumes the local `regs` pointer after `orlix_syscall_dispatch(regs)` even though the timer/scheduler path may have switched to another task.

**Next:**

Inspect and correct the hosted syscall exit path so it resumes `task_pt_regs(current)` after any syscall-exit scheduling point, or prevent hosted syscall dispatch from switching tasks before the hosted return path has selected the correct current task registers. Keep the fix in `OrlixKernel`; do not move Linux scheduling or syscall semantics into `OrlixHostAdapter`.

**Follow-up evidence, 2026-06-11 11:24 CEST:**

- Exact command:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixPTYRuntimeTests/OrlixPTYRuntimeTests/testLinuxPTYCarriesInteractiveShellInputAndOutput test`
- Result:
  - Failed, exit code `65`.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.11_11-24-28-+0200.xcresult`.
  - Terminal log: `/Users/rudironsoni/Library/Developer/CoreSimulator/Devices/29DFD45B-3C6B-4B3E-A5C6-565C1DD7B5CE/data/Containers/Data/Application/834D0738-8C3E-4F40-8008-C5C4236B4659/tmp/orlix-pty-runtime-8A03EFD6-A707-4F87-BCE5-C37ABF805DE9.log`.
  - XCTest failed after `600.402` seconds waiting for PTY proof output.
- Key evidence:
  - The app-hosted kernel boots and mounts the ext4 root: `VFS: Mounted root (ext4 filesystem) readonly on device 254:0`.
  - Init still runs: `Run /sbin/init as init process`.
  - The old init `mkdirat` panic did not reproduce after restoring the full hosted mapping refresh path.
  - The active failure still reproduces in `sh`: `Orlix: user fault task=sh pid=27 pc=0x0 lr=0x0 sp=0x6fffffe8d9f0 addr=0x0 flags=0x2`.
  - A later fault has fully zeroed user registers: `Orlix: user fault task=sh pid=27 pc=0x0 lr=0x0 sp=0x0 addr=0x0 flags=0x2`.
  - The diagnostic event ring shows a scheduler/current mismatch class: `seq=1321 kind=syscall-enter task=init pid=1 ... x8=0x104`, then `seq=1322 kind=syscall-return task=sh pid=27 pc=0x0 lr=0x0 sp=0x0 x0=0x0 x8=0x0`.
- Updated blocker:
  - The hosted syscall return path still allows a syscall entered by one task to continue using `current` after Linux scheduling or task-exit work has made `current` name another task.
  - The inline syscall-gate path is also suspect: `orlix_hosted_syscall_enter_user()` resumes `task_pt_regs(current)` after returning through the hosted syscall assembly continuation. That must not use a different task's zeroed or unrelated `pt_regs`.

**Rejected diagnostic attempt, 2026-06-11 11:48 CEST:**

- Exact command:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixPTYRuntimeTests/OrlixPTYRuntimeTests/testLinuxPTYCarriesInteractiveShellInputAndOutput test`
- Result:
  - First sandboxed attempt failed before testing with CoreSimulator/SwiftPM cache access errors, exit code `74`.
  - Escalated retry failed, exit code `65`.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.11_11-48-58-+0200.xcresult`.
- Evidence:
  - A temporary pointer-expanded event-ring diagnostic compiled, but changed the runtime failure back to an early init panic.
  - The run reached `Run /sbin/init as init process`, then panicked with `Kernel panic - not syncing: Attempted to kill init! exitcode=0x0000000b`.
  - The last recorded init syscall before panic was `x8=0x22` with return `x0=0xffffffffffffffef`.
- Decision:
  - The pointer-expanded diagnostic was reverted. It is not a valid fix and is too disruptive for the current runtime proof.
  - The useful blocker remains the earlier stable failure: app-hosted Linux reaches `sh`, then the hosted user return path eventually records/resumes zeroed `sh` registers.

## 2026-06-11 OCI-Derived Materialized Root Runtime Proof

**Scope:**

Continue Phase 8 runtime proof by adding an app-hosted test for an OCI-layout-derived environment root. This does not add registry pull, Docker, runc, Apple container, Apple containerization, Virtualization.framework, or any VM runtime path.

**Code change:**

- Extended `OrlixTestRunner/Tests/XCTest/OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests.swift` so the runtime proof helper can run against fixture-specific environment sources.
- Added `testOCIDerivedMaterializedRootBootsAndExposesOSRelease()`.
- Kept the same app-hosted path:
  - `OrlixEnvironmentDescriptor`
  - `OrlixEnvironmentRootImage.materialized`
  - `OrlixLinuxSession(materializedRootImage:)`
  - HostAdapter file-backed block image registration
  - OrlixKernel normal Linux boot, ext4 root mount, `/sbin/init`, `/bin/sh`, and `/bin/cat /etc/os-release`

**Fixture preparation:**

- Created `Build/OrlixOS/environment-runtime-proof/oci-imported`.
- Built a local OCI image layout from the current `Build/OrlixOS/rootfs/release/base-tree`.
- Changed only `/etc/os-release` to:
  - `NAME=Orlix OCI Runtime Proof`
  - `ID=orlix-oci-runtime-proof`
  - `PRETTY_NAME=Orlix OCI Runtime Proof`
- Wrote valid `oci-layout`, `index.json`, manifest, config, and `blobs/sha256/...` entries.
- Materialized the imported root into real ext4 images through `OrlixOS/Makefile environment-root-image`.

**Evidence:**

- Exact command(s):
  - `rtk /bin/bash -lc 'set -euo pipefail ... Build/OrlixOS/environment-runtime-proof/oci-imported ...'`
  - `rtk /opt/homebrew/opt/e2fsprogs/sbin/debugfs -R 'cat /etc/os-release' Build/OrlixOS/environment-runtime-proof/oci-imported/state/environments/oci-imported-runtime-proof/base.ext4`
  - `rtk file Build/OrlixOS/environment-runtime-proof/oci-imported/state/environments/oci-imported-runtime-proof/base.ext4 Build/OrlixOS/environment-runtime-proof/oci-imported/state/environments/oci-imported-runtime-proof/state.ext4`
  - `rtk timeout 600 xcodebuild build-for-testing -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim`
  - `rtk timeout 240 xcodebuild test-without-building -xctestrun .deriveddata/OrlixSystem-sim/Build/Products/OrlixPTYRuntimeTests_iphonesimulator26.5-arm64.xctestrun -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests/testOCIDerivedMaterializedRootBootsAndExposesOSRelease`
- Result:
  - `debugfs cat /etc/os-release` from the generated OCI-derived base image printed `ID=orlix-oci-runtime-proof`.
  - `file` reported both generated images as Linux ext4 filesystems.
  - Sandboxed Xcode build failed before compilation with CoreSimulator and SwiftPM cache permission errors, exit code `74`.
  - Escalated build initially failed on a Swift static/instance validation bug in the new test helper. That bug was fixed.
  - Escalated rebuild succeeded: `** TEST BUILD SUCCEEDED **`.
  - OCI-derived runtime proof passed: selected test executed 1 test, 0 failures, final `xcodebuild` exit code `0`, `** TEST EXECUTE SUCCEEDED **`.
- Runtime proof covered:
  - app-private materialized base/state ext4 images;
  - HostAdapter file-backed root image registration;
  - OrlixKernel seeing `virtio_blk` devices as `/dev/vda` and `/dev/vdb`;
  - ext4 root mount through upstream Linux VFS;
  - `/sbin/init` launch;
  - interactive `/bin/sh`;
  - `/bin/cat /etc/os-release` reading `ID=orlix-oci-runtime-proof` from the OCI-derived root.

**Regression check:**

- Re-ran `testTarDerivedMaterializedRootBootsAndExposesOSRelease()` after refactoring the helper.
- The first tar rerun failed with `xcodebuild` timeout/interruption and a matching OrlixTestRunner crash report:
  - `/Users/rudironsoni/Library/Logs/DiagnosticReports/OrlixTestRunner-2026-06-11-154604.ips`
  - Crash report faulting thread: OrlixKernel `n_tty_write`.
- Regenerated the tar fixture from the current `Build/OrlixOS/rootfs/release/base-tree`.
- Verified regenerated tar base image with `debugfs cat /etc/os-release`, which printed `ID=orlix-tar-runtime-proof`.
- Re-ran the tar-derived test. It failed again with final `xcodebuild` exit code `65`.
- Latest tar log showed:
  - `Run /sbin/init as init process`
  - `orlix-init: poll error event`
  - no `ORLIX_ENV_OS_RELEASE_BEGIN` marker
- Current conclusion:
  - OCI-derived runtime proof is green.
  - Tar-derived runtime proof is not green after this run.
  - The tar failure is in the existing PTY/init/runtime path, not OCI parsing or image materialization.

**Next:**

Investigate the PTY/init poll error and `n_tty_write` crash path before treating the tar-derived environment proof as stable. Keep the fix in OrlixKernel or OrlixOS init depending on ownership. Do not move terminal, tty, poll, or Linux process semantics into OrlixHostAdapter.

## 2026-06-11 Tar And OCI Runtime Proof Stabilization

**Scope:**

Stabilize the app-hosted tar-derived and OCI-derived materialized root proofs after the OCI proof exposed a host-page-sized kernel mapping assumption in the app-hosted runtime path. This does not add Docker, runc, registry pull, Apple container, Apple containerization, Virtualization.framework, or a VM runtime path.

**Code changes:**

- Fixed HostAdapter kernel page mapping so Linux page mappings that land inside a larger host page use the shadow mapping path instead of assuming a host-page-sized mapping.
- Added a HostAdapter test for mapping multiple Linux pages inside one host page.
- Added a hosted kernel fault callback path so app-hosted faults on vmalloc-backed kernel text/data can ask OrlixKernel to sync the relevant host window and resume, instead of immediately reraising the host signal.
- Added OrlixKernel vmalloc host-window sync for present `init_mm` kernel PTEs.
- Removed temporary init-syscall and uaccess copy diagnostics after the fault path was proven.
- Fixed the Xcode OrlixOS payload embed script so edits to OrlixOS payload inputs force `OrlixOS/Makefile kernel-payload` instead of copying a stale payload.
- Added `OrlixOS/Makefile environment-runtime-proof-fixtures` so generated tar and OCI runtime proof images are rebuilt from durable OrlixOS inputs through the owning OrlixOS build path.

**Storage workaround used for proof:**

- Root volume was too full for the proof ladder.
- Xcode proof runs used:
  - `HOME=/Volumes/1TB/Xcode/Home`
  - `TMPDIR=/Volumes/1TB/Xcode/tmp/orlix-xcode-tmp`
  - `-derivedDataPath /Volumes/1TB/Xcode/DerivedData/OrlixSystem-active`
  - `-clonedSourcePackagesDirPath /Volumes/1TB/Xcode/SourcePackages`
  - result bundles under `/Volumes/1TB/Xcode/Results`
- The only simulator cleanup was targeted uninstall of `org.orlix.OrlixTestRunner` from simulator `29DFD45B-3C6B-4B3E-A5C6-565C1DD7B5CE`.

**Evidence:**

- Exact command:
  - `rtk timeout 1200 make -f OrlixKernel/Makefile __kernel-archive PROFILE=release ORLIX_KERNEL_ARCHIVE_PLATFORMS=iphonesimulator`
- Result:
  - Succeeded.
  - Built `Build/OrlixKernel/release/iphonesimulator/OrlixKernel.a`.
  - Wrote `Build/OrlixKernel/release/linux-object-manifest.txt`.

- Exact command:
  - `rtk xcodegen generate`
- Result:
  - Succeeded and regenerated `OrlixSystem.xcodeproj` from `project.yml`.

- Exact command:
  - `rtk env HOME=/Volumes/1TB/Xcode/Home TMPDIR=/Volumes/1TB/Xcode/tmp/orlix-xcode-tmp xcodebuild build-for-testing -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath /Volumes/1TB/Xcode/DerivedData/OrlixSystem-active -clonedSourcePackagesDirPath /Volumes/1TB/Xcode/SourcePackages -resultBundlePath /Volumes/1TB/Xcode/Results/OrlixPTYRuntimeTests-relinked-payload-freshness.xcresult`
- Result:
  - Succeeded with `** TEST BUILD SUCCEEDED **`.
  - Output showed `built OrlixOS first-stage init`.
  - Output showed `packaged OrlixKernel payload`.

- Exact command:
  - `rtk make -f OrlixOS/Makefile environment-runtime-proof-fixtures PROFILE=release`
- Result:
  - Succeeded.
  - Regenerated:
    - `Build/OrlixOS/environment-runtime-proof/tar-imported/state/environments/tar-imported-runtime-proof/base.ext4`
    - `Build/OrlixOS/environment-runtime-proof/tar-imported/state/environments/tar-imported-runtime-proof/state.ext4`
    - `Build/OrlixOS/environment-runtime-proof/oci-imported/state/environments/oci-imported-runtime-proof/base.ext4`
    - `Build/OrlixOS/environment-runtime-proof/oci-imported/state/environments/oci-imported-runtime-proof/state.ext4`

- Exact command:
  - `rtk rg -a "orlix-init: poll ready|poll error event" Build/OrlixOS/environment-runtime-proof -n`
- Result:
  - No matches, exit code `1`.

- Exact command:
  - `rtk /opt/homebrew/opt/e2fsprogs/sbin/debugfs -R 'cat /etc/os-release' Build/OrlixOS/environment-runtime-proof/oci-imported/state/environments/oci-imported-runtime-proof/base.ext4`
- Result:
  - Printed `ID=orlix-oci-runtime-proof`.

- Exact command:
  - `rtk /opt/homebrew/opt/e2fsprogs/sbin/debugfs -R 'cat /etc/os-release' Build/OrlixOS/environment-runtime-proof/tar-imported/state/environments/tar-imported-runtime-proof/base.ext4`
- Result:
  - Printed `ID=orlix-tar-runtime-proof`.

- Exact command:
  - `rtk env HOME=/Volumes/1TB/Xcode/Home TMPDIR=/Volumes/1TB/Xcode/tmp/orlix-xcode-tmp xcodebuild test-without-building -xctestrun /Volumes/1TB/Xcode/DerivedData/OrlixSystem-active/Build/Products/OrlixPTYRuntimeTests_iphonesimulator26.5-arm64.xctestrun -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests/testOCIDerivedMaterializedRootBootsAndExposesOSRelease -resultBundlePath /Volumes/1TB/Xcode/Results/OrlixPTYRuntimeTests-oci-regenerated-fixture.xcresult`
- Result:
  - Succeeded with `** TEST EXECUTE SUCCEEDED **`.
  - Executed 1 test, 0 failures.
  - Runtime output included `ID=orlix-oci-runtime-proof` and `ORLIX_ENV_OS_RELEASE_DONE`.
  - Runtime output did not include the stale `orlix-init: poll ready` diagnostic.

- Exact command:
  - `rtk env HOME=/Volumes/1TB/Xcode/Home TMPDIR=/Volumes/1TB/Xcode/tmp/orlix-xcode-tmp xcodebuild test-without-building -xctestrun /Volumes/1TB/Xcode/DerivedData/OrlixSystem-active/Build/Products/OrlixPTYRuntimeTests_iphonesimulator26.5-arm64.xctestrun -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests/testTarDerivedMaterializedRootBootsAndExposesOSRelease -resultBundlePath /Volumes/1TB/Xcode/Results/OrlixPTYRuntimeTests-tar-regenerated-fixture.xcresult`
- Result:
  - Succeeded with `** TEST EXECUTE SUCCEEDED **`.
  - Executed 1 test, 0 failures.
  - Runtime output included `ID=orlix-tar-runtime-proof` and `ORLIX_ENV_OS_RELEASE_DONE`.
  - Runtime output did not include the stale `orlix-init: poll ready` diagnostic.

- Exact command:
  - `rtk git diff --check -- project.yml OrlixOS/Makefile OrlixOS/Sources/init/init.c OrlixHostAdapter/Sources/OrlixHostAdapter/memory/kernel_mapping.c OrlixHostAdapter/Sources/OrlixHostAdapter/runtime/trap.c OrlixKernel/Sources/ports/orlix/overlay/arch/orlix/include/asm/pgtable.h OrlixKernel/Sources/ports/orlix/overlay/arch/orlix/mm/init.c OrlixKernel/Sources/ports/orlix/overlay/arch/orlix/kernel/hosted_exec.c OrlixKernel/Sources/ports/orlix/overlay/arch/orlix/mm/uaccess.c`
- Result:
  - Succeeded, no whitespace errors reported.

- Exact command:
  - `rtk rg -n "orlix-init: poll ready|poll error event|uaccess copy_to_user|init syscall" OrlixKernel/Sources/ports/orlix/overlay OrlixOS/Sources/init OrlixHostAdapter/Sources/OrlixHostAdapter`
- Result:
  - No matches, exit code `1`.

**Current conclusion:**

- The tar-derived materialized root runtime proof is green.
- The OCI-derived materialized root runtime proof is green.
- The vmalloc host-window fault path is required for this proof on the app-hosted iOS simulator runtime.
- The generated fixture path is now owned by `OrlixOS/Makefile` instead of ad hoc writes into `Build/`.
- This does not complete the full OCI/container-image-derived environments plan. Registry pull, OCI runtime bundle semantics, overlay copy-up, namespace expansion, networking expansion, cgroups, and broader conformance remain later phases.

## 2026-06-11 Runtime Fixture Freshness Guard

**Scope:**

Prevent the app-hosted tar-derived and OCI-derived runtime proofs from silently booting stale generated fixtures after the OrlixOS packaged init changes. This keeps generated image refresh under `OrlixOS/Makefile` and keeps the XCTest proof as a preflight guard before Linux boot.

**Code changes:**

- Extended `OrlixOS/Makefile environment-runtime-proof-fixtures` so each generated fixture `.ready` marker records:
  - `profile`
  - `fixture`
  - `environment`
  - `init_sha256`
- Added XCTest preflight validation in `OrlixEnvironmentRootRuntimeTests` so stale or mismatched fixtures fail before boot instead of producing misleading runtime proof output.
- The XCTest guard verifies:
  - marker `profile` is `release`
  - marker `fixture` matches the selected fixture directory
  - marker `environment` matches the selected environment id
  - marker `init_sha256` matches the current `Build/OrlixOS/packages/release/sbin/init` SHA-256
- Missing fixture markers still skip as unavailable fixtures. Present but stale markers now fail loudly.

**Evidence:**

- Exact command:
  - `rtk make -f OrlixOS/Makefile environment-runtime-proof-fixtures PROFILE=release`
- Result:
  - Succeeded.
  - Regenerated tar-derived and OCI-derived runtime proof fixtures.
  - Wrote `.ready` markers with `profile=release`, expected fixture/environment ids, and `init_sha256=1f68db2215919f3059cc7e0abaf76476e600dd7134f59cf1fb2e54cb21b38702`.

- Exact command:
  - `rtk git diff --check -- OrlixOS/Makefile OrlixTestRunner/Tests/XCTest/OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests.swift`
- Result:
  - Succeeded, no whitespace errors reported.

- Exact command:
  - `rtk env HOME=/Volumes/1TB/Xcode/Home TMPDIR=/Volumes/1TB/Xcode/tmp/orlix-xcode-tmp xcodebuild build-for-testing -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath /Volumes/1TB/Xcode/DerivedData/OrlixSystem-active -clonedSourcePackagesDirPath /Volumes/1TB/Xcode/SourcePackages -resultBundlePath /Volumes/1TB/Xcode/Results/OrlixPTYRuntimeTests-fixture-freshness-build.xcresult`
- Result:
  - Succeeded with `** TEST BUILD SUCCEEDED **`.

- Exact command:
  - `rtk env HOME=/Volumes/1TB/Xcode/Home TMPDIR=/Volumes/1TB/Xcode/tmp/orlix-xcode-tmp xcodebuild test-without-building -xctestrun /Volumes/1TB/Xcode/DerivedData/OrlixSystem-active/Build/Products/OrlixPTYRuntimeTests_iphonesimulator26.5-arm64.xctestrun -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests/testOCIDerivedMaterializedRootBootsAndExposesOSRelease -resultBundlePath /Volumes/1TB/Xcode/Results/OrlixPTYRuntimeTests-oci-fixture-freshness.xcresult`
- Result:
  - Succeeded with `** TEST EXECUTE SUCCEEDED **`.
  - Executed 1 test, 0 failures.
  - Runtime output included `ID=orlix-oci-runtime-proof` and `ORLIX_ENV_OS_RELEASE_DONE`.

- Exact command:
  - `rtk env HOME=/Volumes/1TB/Xcode/Home TMPDIR=/Volumes/1TB/Xcode/tmp/orlix-xcode-tmp xcodebuild test-without-building -xctestrun /Volumes/1TB/Xcode/DerivedData/OrlixSystem-active/Build/Products/OrlixPTYRuntimeTests_iphonesimulator26.5-arm64.xctestrun -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests/testTarDerivedMaterializedRootBootsAndExposesOSRelease -resultBundlePath /Volumes/1TB/Xcode/Results/OrlixPTYRuntimeTests-tar-fixture-freshness.xcresult`
- Result:
  - Succeeded with `** TEST EXECUTE SUCCEEDED **`.
  - Executed 1 test, 0 failures.
  - Runtime output included `ID=orlix-tar-runtime-proof` and `ORLIX_ENV_OS_RELEASE_DONE`.

**Current conclusion:**

- The tar-derived and OCI-derived runtime proofs now reject stale generated fixtures before boot.
- The focused tar and OCI runtime proofs remain green after the freshness guard.
- This still does not complete the full OCI/container-image-derived environments plan. Registry pull, OCI runtime bundle semantics, overlay copy-up, namespace expansion, networking expansion, cgroups, and broader conformance remain later phases.

## 2026-06-11 Transactional Rootfs Tar Materialization

**Scope:**

Make rootfs tar import fail without leaving a partially materialized environment root when a materialization-time error occurs after earlier valid entries have already been processed. This is OrlixOS import/session preparation only. It does not change OrlixKernel, OrlixHostAdapter, VFS, syscall, exec, or Linux runtime semantics.

**Code changes:**

- Changed `OrlixRootfsTarMaterializer.materialize(_:into:fileManager:)` to write into a temporary sibling directory named `.<root>.import-<uuid>`.
- Moved the temporary root to the requested target root only after the archive entries have been validated and materialized successfully.
- Removed the temporary root on failure before rethrowing the original error.
- Added `testRootfsTarMaterializerDoesNotLeavePartialRootOnMaterializationFailure`, covering a valid file followed by a hardlink to a missing source. The proof checks that the target root is absent and no temporary import directory remains.

**Evidence:**

- Exact command:
  - `rtk env HOME=/Volumes/1TB/Xcode/Home TMPDIR=/Volumes/1TB/Xcode/tmp/orlix-xcode-tmp xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath /Volumes/1TB/Xcode/DerivedData/OrlixSystem-active -clonedSourcePackagesDirPath /Volumes/1TB/Xcode/SourcePackages -resultBundlePath /Volumes/1TB/Xcode/Results/OrlixOSTests-before-atomic-import.xcresult test`
- Result:
  - Succeeded with `** TEST SUCCEEDED **`.
  - Executed 37 tests, 0 failures.

- Exact command:
  - `rtk git diff --check -- OrlixOS/Sources/Session/OrlixRootfsImport.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
- Result:
  - Succeeded, no whitespace errors reported.

- Exact command:
  - `rtk env CLANG_MODULE_CACHE_PATH=.build/ModuleCache SWIFT_MODULE_CACHE_PATH=.build/ModuleCache swiftc -typecheck OrlixOS/Sources/Session/OrlixOS.swift OrlixOS/Sources/Session/OrlixStoragePolicy.swift OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Sources/Session/OrlixRootfsImport.swift OrlixOS/Sources/Session/OrlixOCIImageLayout.swift OrlixOS/Sources/Session/OrlixEnvironmentImageMaterialization.swift`
- Result:
  - Succeeded, no typecheck errors reported.

- Exact command:
  - `rtk env HOME=/Volumes/1TB/Xcode/Home TMPDIR=/Volumes/1TB/Xcode/tmp/orlix-xcode-tmp xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath /Volumes/1TB/Xcode/DerivedData/OrlixSystem-active -clonedSourcePackagesDirPath /Volumes/1TB/Xcode/SourcePackages -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerDoesNotLeavePartialRootOnMaterializationFailure -resultBundlePath /Volumes/1TB/Xcode/Results/OrlixOSTests-atomic-rootfs-materializer.xcresult test`
- Result:
  - Succeeded with `** TEST SUCCEEDED **`.
  - Executed 1 test, 0 failures.

- Exact command:
  - `rtk env HOME=/Volumes/1TB/Xcode/Home TMPDIR=/Volumes/1TB/Xcode/tmp/orlix-xcode-tmp xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath /Volumes/1TB/Xcode/DerivedData/OrlixSystem-active -clonedSourcePackagesDirPath /Volumes/1TB/Xcode/SourcePackages -resultBundlePath /Volumes/1TB/Xcode/Results/OrlixOSTests-after-atomic-import.xcresult test`
- Result:
  - Succeeded with `** TEST SUCCEEDED **`.
  - Executed 38 tests, 0 failures.

**Current conclusion:**

- Rootfs tar materialization now has an atomic target-root commit point for this import path.
- The regression proves a materialization-time hardlink failure does not leave the target root or staging directory behind.
- This still does not complete the full OCI/container-image-derived environments plan. Registry pull, OCI runtime bundle semantics, overlay copy-up, namespace expansion, networking expansion, cgroups, and broader conformance remain later phases.

## 2026-06-11 Transactional OCI Layout Import Staging

**Scope:**

Make OCI layout import fail without replacing or contaminating the environment staging root when a later layer fails after earlier layers have already applied. This remains OrlixOS image/import handling only. It does not change OrlixKernel, OrlixHostAdapter, VFS, syscall, exec, or Linux runtime semantics.

**Code changes:**

- Changed `OrlixOCIImageLayoutImporter.importLayout(...)` to apply decoded OCI layers into a temporary sibling directory named `.rootfs.oci-import-<uuid>`.
- Replaced the target `rootfs` staging directory only after every selected layer decodes and applies successfully.
- Removed the temporary import root on failure before rethrowing the original error.
- Added `testOCIImageLayoutImporterDoesNotCommitPartialRootOnLayerFailure`, covering a valid lower layer followed by a failing hardlink in a later layer. The proof checks that a previous staging root remains intact, no failed hardlink appears, and no `.rootfs.oci-import-*` directory remains.

**Evidence:**

- Exact command:
  - `rtk git diff --check -- OrlixOS/Sources/Session/OrlixOCIImageLayout.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
- Result:
  - Succeeded, no whitespace errors reported.

- Exact command:
  - `rtk env CLANG_MODULE_CACHE_PATH=.build/ModuleCache SWIFT_MODULE_CACHE_PATH=.build/ModuleCache swiftc -typecheck OrlixOS/Sources/Session/OrlixOS.swift OrlixOS/Sources/Session/OrlixStoragePolicy.swift OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Sources/Session/OrlixRootfsImport.swift OrlixOS/Sources/Session/OrlixOCIImageLayout.swift OrlixOS/Sources/Session/OrlixEnvironmentImageMaterialization.swift`
- Result:
  - Succeeded, no typecheck errors reported.

- Exact command:
  - `rtk env HOME=/Volumes/1TB/Xcode/Home TMPDIR=/Volumes/1TB/Xcode/tmp/orlix-xcode-tmp xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath /Volumes/1TB/Xcode/DerivedData/OrlixSystem-active -clonedSourcePackagesDirPath /Volumes/1TB/Xcode/SourcePackages -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterDoesNotCommitPartialRootOnLayerFailure -resultBundlePath /Volumes/1TB/Xcode/Results/OrlixOSTests-oci-transactional-import.xcresult test`
- Result:
  - Initial sandboxed run failed before tests with CoreSimulator/package-lock/result-bundle access errors.
  - Escalated rerun succeeded with `** TEST SUCCEEDED **`.
  - Executed 1 test, 0 failures.

- Exact command:
  - `rtk env HOME=/Volumes/1TB/Xcode/Home TMPDIR=/Volumes/1TB/Xcode/tmp/orlix-xcode-tmp xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath /Volumes/1TB/Xcode/DerivedData/OrlixSystem-active -clonedSourcePackagesDirPath /Volumes/1TB/Xcode/SourcePackages -resultBundlePath /Volumes/1TB/Xcode/Results/OrlixOSTests-after-oci-transactional-import.xcresult test`
- Result:
  - Succeeded with `** TEST SUCCEEDED **`.
  - Executed 39 tests, 0 failures.

**Current conclusion:**

- OCI layout import now has an atomic staging-root commit point for decoded layer application.
- A failed later layer no longer replaces a prior staging root, leaves partial files in the target root, or leaves temporary OCI import directories behind.
- This still does not complete the full OCI/container-image-derived environments plan. Registry pull, OCI runtime bundle semantics, overlay copy-up, namespace expansion, networking expansion, cgroups, and broader conformance remain later phases.

## 2026-06-11 Environment Root Mount Metadata

**Scope:**

Persist the Linux root mount contract in OrlixOS environment descriptors so named environments explicitly record the boot-time base/state image layout already used by the upstream Linux init path. This is OrlixOS session/import metadata only. It does not change OrlixKernel, OrlixHostAdapter, VFS, syscall, exec, OverlayFS behavior, or Linux runtime semantics.

**Code changes:**

- Added `OrlixEnvironmentRootMount` with the default OverlayFS root layout:
  - base block device `/dev/vda`
  - writable state block device `/dev/vdb`
  - lower mount `/lower`
  - state mount `/state`
  - overlay mount `/newroot`
  - upper directory `/state/upper`
  - work directory `/state/work`
  - final root `/`
  - filesystem type `ext4`
  - overlay filesystem type `overlay`
  - read-only base image
- Added `rootMount` to `OrlixEnvironmentDescriptor`.
- Kept descriptor decoding backward-compatible by defaulting missing `rootMount` metadata to `.defaultOverlay`.
- Added tests proving default descriptors expose the expected Linux-shaped root mount metadata, saved descriptors persist root mount JSON, and legacy descriptor JSON still decodes with the default overlay layout.

**Evidence:**

- Exact command:
  - `rtk git diff --check -- OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
- Result:
  - Succeeded, no whitespace errors reported.

- Exact command:
  - `rtk env CLANG_MODULE_CACHE_PATH=.build/ModuleCache SWIFT_MODULE_CACHE_PATH=.build/ModuleCache swiftc -typecheck OrlixOS/Sources/Session/OrlixOS.swift OrlixOS/Sources/Session/OrlixStoragePolicy.swift OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Sources/Session/OrlixRootfsImport.swift OrlixOS/Sources/Session/OrlixOCIImageLayout.swift OrlixOS/Sources/Session/OrlixEnvironmentImageMaterialization.swift`
- Result:
  - Succeeded, no typecheck errors reported.

- Exact command:
  - `rtk env HOME=/Volumes/1TB/Xcode/Home TMPDIR=/Volumes/1TB/Xcode/tmp/orlix-xcode-tmp xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath /Volumes/1TB/Xcode/DerivedData/OrlixSystem-active -clonedSourcePackagesDirPath /Volumes/1TB/Xcode/SourcePackages -resultBundlePath /Volumes/1TB/Xcode/Results/OrlixOSTests-root-mount-metadata-focused.xcresult -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testDefaultEnvironmentDescriptorUsesLinuxShapedDefaults -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentRegistryPersistsDescriptorsUnderStateRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentDescriptorDecodesLegacyMetadataWithDefaultOverlayRootMount test`
- Result:
  - Initial sandboxed run failed before tests with CoreSimulator/package-lock/result-bundle access errors.
  - Escalated rerun executed the tests and failed only on brittle JSON spacing assertions.

- Exact command:
  - `rtk env HOME=/Volumes/1TB/Xcode/Home TMPDIR=/Volumes/1TB/Xcode/tmp/orlix-xcode-tmp xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath /Volumes/1TB/Xcode/DerivedData/OrlixSystem-active -clonedSourcePackagesDirPath /Volumes/1TB/Xcode/SourcePackages -resultBundlePath /Volumes/1TB/Xcode/Results/OrlixOSTests-root-mount-metadata-focused-2.xcresult -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testDefaultEnvironmentDescriptorUsesLinuxShapedDefaults -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentRegistryPersistsDescriptorsUnderStateRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentDescriptorDecodesLegacyMetadataWithDefaultOverlayRootMount test`
- Result:
  - Succeeded with `** TEST SUCCEEDED **`.
  - Executed 3 tests, 0 failures.

- Exact command:
  - `rtk env HOME=/Volumes/1TB/Xcode/Home TMPDIR=/Volumes/1TB/Xcode/tmp/orlix-xcode-tmp xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath /Volumes/1TB/Xcode/DerivedData/OrlixSystem-active -clonedSourcePackagesDirPath /Volumes/1TB/Xcode/SourcePackages -resultBundlePath /Volumes/1TB/Xcode/Results/OrlixOSTests-after-root-mount-metadata.xcresult test`
- Result:
  - Succeeded with `** TEST SUCCEEDED **`.
  - Executed 40 tests, 0 failures.

**Current conclusion:**

- OrlixOS environment descriptors now persist the current read-only base plus writable state root mount contract expected by the Linux init path.
- Old descriptor metadata remains decodable and defaults to the same overlay root mount contract.
- This still does not complete the full OCI/container-image-derived environments plan. Registry pull, OCI runtime bundle semantics, overlay copy-up validation, namespace expansion, networking expansion, cgroups, and broader conformance remain later phases.

## 2026-06-11 Hosted User Stack Copy-Back and Environment Entry Proof

**Scope:**

Fix the app-hosted Linux user-memory mirror regression that blocked `clone_thread_probe` before the new environment entry probe could be accepted. This is HostAdapter private mapping mechanics plus Orlix-owned kselftest coverage. It does not move Linux semantics out of OrlixKernel and does not introduce Apple container, Virtualization.framework, VM lifecycle, or registry/runtime dependencies.

**Code changes:**

- Added `environment_entry_probe` to the Orlix kselftest payload.
- The probe creates a child mount namespace, mounts a tmpfs root, writes a child-local `/etc/os-release`, copies its own executable into that root, `chroot`s, re-execs, and proves:
  - the re-execed process sees the child root;
  - the parent root marker is hidden inside the child root;
  - the child tmpfs root is hidden from the parent namespace.
- Extended `OrlixKernelUpstreamTests` to require the `environment_entry_probe` marker and success output.
- Fixed HostAdapter user window refresh so dirty writable user segments are copied back before tracked host segments are replaced. The observed failure was a user stack return address saved before `SYS_futex` being overwritten by stale kernel backing on syscall return, causing a host `pc=0` crash after the futex wake path.

**Evidence:**

- Exact command:
  - `rtk git diff --check -- OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/environment_entry_probe.c OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/Makefile OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift docs/plans/active/oci-derived-environments-virtio-plane/IMPLEMENT.md`
- Result:
  - Succeeded, no whitespace errors reported.

- Exact command:
  - `rtk timeout 1200 make -f OrlixKernel/Makefile kselftest PROFILE=release`
- Result:
  - Initial run failed because the main volume was nearly full and `gen_init_cpio` could not write the initramfs.
  - After freeing generated simulator/DerivedData space, rerun succeeded and produced `Build/OrlixMLibC/test-initramfs/release/OrlixTestInitramfs.bundle`.

- Exact command:
  - `rtk sed -n '1,60p' Build/OrlixMLibC/kselftest/release/kselftest-list.txt`
- Result:
  - Listed `orlix:environment_entry_probe`, `orlix:mount_namespace_probe`, and `orlix:virtio_blk_environment_probe`.

- Exact failing command before the HostAdapter copy-back fix:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixKernelUpstreamTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath /Volumes/1TB/Xcode/DerivedData/OrlixSystem-oci-env-proof -only-testing:OrlixKernelUpstreamTests/OrlixKernelUpstreamTests/testKselftestRootfsCompletesThroughOrlixOSTerminalSession test`
- Result:
  - Failed in app-hosted runtime before environment proof completed.
  - `clone_thread_probe` printed `ok 1` and `ok 2`, then the app crashed during the deep stack alloca/futex wake subtest.
  - Crash report `OrlixTestRunner-2026-06-11-203004.ips` showed `EXC_BAD_ACCESS`, `SIGSEGV`, faulting user execution thread with `pc=0`, `lr=0`, `x8=98`, and futex wake arguments still live.

- Exact command after the HostAdapter copy-back fix:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixKernelUpstreamTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath /Volumes/1TB/Xcode/DerivedData/OrlixSystem-oci-env-proof -only-testing:OrlixKernelUpstreamTests/OrlixKernelUpstreamTests/testKselftestRootfsCompletesThroughOrlixOSTerminalSession test`
- Result:
  - Sandboxed run failed before test execution because CoreSimulator and SwiftPM cache access were denied.
  - Escalated rerun succeeded with exit code 0.
  - Result bundle: `/Volumes/1TB/Xcode/DerivedData/OrlixSystem-oci-env-proof/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.11_20-36-57-+0200.xcresult`.
  - `xcresulttool get test-results summary` reported `result: Passed`, `totalTestCount: 1`, `passedTests: 1`, `failedTests: 0`, `skippedTests: 0`.

**Current conclusion:**

- The focused app-hosted upstream kernel gate now accepts the clone/thread stack probe and the environment entry probe through `OrlixOS` terminal session execution.
- This proves the current payload can execute the Orlix environment-entry kselftest after the hosted stack copy-back fix.
- This still does not complete the full OCI/container-image-derived environments plan. Registry pull, OCI runtime bundle semantics, overlay copy-up validation, namespace expansion, networking expansion, cgroups, and broader conformance remain later phases.

## 2026-06-11 Descriptor Execution Stack and Cmdline Proof

**Scope:**

Fix the OrlixOS first-stage init descriptor execution path that surfaced while proving OCI-derived materialized roots on iOS Simulator. This is private OrlixOS boot/session descriptor handling. It does not change OrlixKernel Linux semantics, does not introduce Apple container/containerization into the iOS runtime, and does not use a VM runtime.

**Code changes:**

- Moved `struct orlix_command_config` storage out of the child stack by allocating it after `fork()` in the command child.
- Avoided the failed static-storage variant. That variant enlarged `/init` `.bss` enough to fault before `main` in the current hosted user image path.
- Increased `/init` command-line read capacity from a fixed 4096-byte stack buffer to a 16384-byte heap buffer.
- Added descriptor execution runtime coverage for an OCI-derived materialized root.
- Added a long argv descriptor runtime proof that keeps the shell script small and puts the long payload in a normal argv slot.
- Changed the descriptor proof to validate virtual uid/gid through Linux `/proc/self/status` instead of `/bin/id`, because `/bin/id` currently faults in this runtime path and is a separate userspace conformance issue.
- Made the fatal marker check proof-aware so `orlix-init: PTY shell session ended` is allowed only when a noninteractive one-shot descriptor command already emitted its done marker.

**Evidence:**

- Exact command:
  - `rtk timeout 900 make -f OrlixOS/Makefile environment-runtime-proof-fixtures PROFILE=release`
- Result:
  - Succeeded.
  - Rebuilt OrlixOS first-stage init.
  - Re-materialized tar-imported and OCI-imported environment runtime proof ext4 images.

- Exact failing command before the heap allocation fix:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath /Volumes/1TB/Xcode/DerivedData/OrlixSystem-active -clonedSourcePackagesDirPath /Volumes/1TB/Xcode/SourcePackages -resultBundlePath /Volumes/1TB/Xcode/Results/OrlixPTYRuntimeTests-long-descriptor-execution.xcresult -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests/testOCIDerivedMaterializedRootBindsLongDescriptorExecutionDefaults test`
- Result:
  - Failed.
  - Result bundle reported `fatal imported environment runtime marker found: Kernel panic`.
  - Kernel log showed `Orlix: user fault task=init pid=1` followed by `Kernel panic - not syncing: Attempted to kill init! exitcode=0x0000000b`.

- Exact command after the heap allocation and procfs marker changes:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath /Volumes/1TB/Xcode/DerivedData/OrlixSystem-active -clonedSourcePackagesDirPath /Volumes/1TB/Xcode/SourcePackages -resultBundlePath /Volumes/1TB/Xcode/Results/OrlixPTYRuntimeTests-long-descriptor-execution-long-argv-assertions.xcresult -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests/testOCIDerivedMaterializedRootBindsLongDescriptorExecutionDefaults test`
- Result:
  - Succeeded with exit code 0.
  - `xcresulttool get test-results summary` reported `result: Passed`, `totalTestCount: 1`, `passedTests: 1`, `failedTests: 0`, `skippedTests: 0`.

- Exact command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath /Volumes/1TB/Xcode/DerivedData/OrlixSystem-active -clonedSourcePackagesDirPath /Volumes/1TB/Xcode/SourcePackages -resultBundlePath /Volumes/1TB/Xcode/Results/OrlixPTYRuntimeTests-descriptor-execution-argv16.xcresult -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests/testOCIDerivedMaterializedRootBindsDescriptorExecutionDefaults test`
- Result:
  - Succeeded with exit code 0.
  - `xcresulttool get test-results summary` reported `result: Passed`, `totalTestCount: 1`, `passedTests: 1`, `failedTests: 0`, `skippedTests: 0`.

- Exact command:
  - `rtk git diff --check -- OrlixOS/Sources/init/init.c OrlixTestRunner/Tests/XCTest/OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests.swift docs/plans/active/oci-derived-environments-virtio-plane/IMPLEMENT.md`
- Result:
  - Succeeded, no whitespace errors reported.

**Failed or rejected paths:**

- Static `struct orlix_command_config` storage was rejected because it moved the crash before `/init` reached `main`.
- Newline-heavy long `-c` scripts with 4, 16, and 64 padding lines were rejected because they exposed current shell/startup faults instead of proving descriptor binding.
- `/bin/id` was rejected for this proof because it faults in the current runtime path. UID/GID are now proven through Linux `/proc/self/status`.

**Open issues surfaced:**

- Current hosted `/bin/sh` execution can fault for some valid `-c` script shapes. That belongs in a separate Linux userspace conformance investigation.
- Very large kernel command lines can still fault PID 1 before `/init` reaches `main`. The 16384-byte heap read fixes `/init` parsing capacity, not the kernel/user startup limit.

**Current conclusion:**

- OCI-derived materialized roots can bind descriptor-provided argv, env, cwd, uid, and gid into a noninteractive Linux command on iOS Simulator.
- The proof uses one OrlixKernel and the OrlixOS environment descriptor path. It does not use Apple container/containerization as runtime, Virtualization.framework, a Linux VM, or Apple VM lifecycle.
- This still does not complete the full OCI/container-image-derived environments plan. Registry pull, OCI runtime bundle semantics, overlay copy-up validation, namespace expansion, networking expansion, cgroups, and broader conformance remain later phases.

## 2026-06-11 iOS Simulator-first fixture wording cleanup

**Scope:**

Keep the current stage framed as iOS Simulator runtime proof. The initial execution target is iOS Simulator only. macOS appears only as the development/build host for fixture production, result bundles, and derived data. Host paths and host users are not runtime targets, not Linux users, and not part of the product model.

**Code changes:**

- Changed the OrlixOS metadata-command test fixture path from a personal home path to `home/orlix/...` so the test stays framed as an Orlix Linux fixture.

**Evidence:**

- Exact command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentImageMaterializationGeneratesManifestMetadataCommands test`
- Result:
  - First sandboxed run failed before build because CoreSimulator and SwiftPM cache access under `~/Library` were denied.
  - Escalated rerun completed with exit code 0.
- Exact command:
  - `rtk git diff --check -- OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift docs/plans/active/oci-derived-environments-virtio-plane/IMPLEMENT.md`
- Result:
  - Succeeded, no whitespace errors reported.

## 2026-06-11 PAX tar header import fidelity

**Scope:**

Improve rootfs tar and OCI layer import fidelity for archives that use POSIX PAX extended headers. This is OrlixOS import behavior for environment image creation. It does not change OrlixKernel Linux semantics, does not introduce Apple container/containerization into the iOS runtime, and does not use a VM runtime.

**Code changes:**

- `OrlixRootfsTarManifestReader` now accepts per-entry PAX extended headers (`x`) and ignores global PAX headers (`g`) instead of rejecting the archive as an unsupported tar entry.
- Per-entry PAX headers now override the next archive entry's `path`, `linkpath`, `size`, `mode`, `uid`, and `gid`.
- Rootfs tar materialization now uses the PAX-overridden path when writing files into the staging root.
- Added OrlixOS tests for PAX path, uid/gid, mode, and symlink linkpath handling.

**Evidence:**

- Exact command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderAppliesPAXExtendedHeaders -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerAppliesPAXPathBeforeWriting test`
- Result:
  - First runs failed at Swift compile time because throwing `??` fallbacks were invalid. The parser was corrected to use explicit fallback branches.
  - Final focused rerun completed with exit code 0.
- Exact command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderParsesUstarEntries -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderAppliesPAXExtendedHeaders -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderRejectsUnsupportedTypesAndBadChecksums -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerWritesValidatedEntriesToStagingTree -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerAppliesPAXPathBeforeWriting -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesLayerWhiteoutsIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesGzipLayerIntoStagingRoot test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.11_22-48-39-+0200.xcresult`.
- Exact command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.11_22-48-39-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 7`
  - `passedTests: 7`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro.
- Exact command:
  - `rtk git diff --check -- OrlixOS/Sources/Session/OrlixRootfsImport.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift docs/plans/active/oci-derived-environments-virtio-plane/IMPLEMENT.md`
- Result:
  - Succeeded, no whitespace errors reported.

**Current conclusion:**

- Imported rootfs tarballs and OCI layers can now preserve common PAX-provided metadata for the next archive entry before staging and ext4 materialization.
- This is still not a complete OCI image/runtime implementation. Registry pull, OCI Runtime Spec behavior, broader archive formats, xattrs, device nodes, sparse files, namespaces, networking, cgroups, and real-Linux oracle comparison remain later requirements.

## 2026-06-11 OCI layer PAX metadata proof

**Scope:**

Extend the PAX tar proof through the OCI layout importer. This proves the container-image layer path consumes PAX metadata before staging and metadata-command generation. This remains OrlixOS import behavior and does not add Apple container/containerization, Virtualization.framework, Docker, runc, or a VM runtime dependency.

**Code changes:**

- Added `testOCIImageLayoutImporterAppliesPAXLayerMetadataIntoStagingRoot`.
- The fixture builds an OCI layout whose layer contains PAX `path`, `uid`, `gid`, `mode`, and symlink `linkpath` metadata.
- The test proves:
  - importer manifest paths use the PAX paths;
  - staging root contains the PAX path, not the fallback tar header path;
  - symlink target comes from PAX `linkpath`;
  - generated debugfs metadata commands carry the PAX uid, gid, and mode into later ext4 materialization.

**Evidence:**

- Exact failing command before fixture correction:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesPAXLayerMetadataIntoStagingRoot test`
- Result:
  - Failed with 1 XCTest assertion failure.
  - The fixture expected symlink mode `0120777` without providing a PAX mode for the symlink. The test fixture was corrected to include PAX `mode=0777`.
- Exact focused command after fixture correction:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesPAXLayerMetadataIntoStagingRoot test`
- Result:
  - Completed with exit code 0.
- Exact combined command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderParsesUstarEntries -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderAppliesPAXExtendedHeaders -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerAppliesPAXPathBeforeWriting -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesLayerWhiteoutsIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesGzipLayerIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesPAXLayerMetadataIntoStagingRoot test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.11_22-54-29-+0200.xcresult`.
- Exact summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.11_22-54-29-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 6`
  - `passedTests: 6`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro.
- Exact command:
  - `rtk git diff --check -- OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift docs/plans/active/oci-derived-environments-virtio-plane/IMPLEMENT.md`
- Result:
  - Succeeded, no whitespace errors reported.

**Current conclusion:**

- The OCI layout importer now has iOS Simulator XCTest proof that PAX metadata survives from an OCI layer into staging and ext4 metadata-command generation.
- This strengthens the "run images as-is" path for common tar metadata forms, but still does not cover xattrs, device nodes, sparse files, registry pull, OCI Runtime Spec lifecycle, namespaces, networking, cgroups, or real-Linux oracle comparison.

## 2026-06-11 GNU long tar header import fidelity

**Scope:**

Improve rootfs tar and OCI layer import fidelity for archives that use GNU tar long path (`L`) and long link (`K`) extension records. This remains OrlixOS import behavior for environment image creation. It does not change OrlixKernel Linux semantics, add Apple container/containerization, use Virtualization.framework, or introduce Docker/runc behavior.

**Code changes:**

- `OrlixRootfsTarManifestReader` now accepts GNU long path (`L`) and long link (`K`) records instead of rejecting them as unsupported tar entry types.
- GNU long path and link records now populate the same next-entry metadata state used by PAX handling.
- Added OrlixOS tests proving:
  - plain tar materialization writes the GNU long path and symlink target, not the fallback short header names;
  - OCI layout import carries GNU long layer names through staging and metadata-command generation.

**Evidence:**

- Exact focused command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerAppliesGNULongPathAndLinkBeforeWriting -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesGNULongLayerNamesIntoStagingRoot test`
- Result:
  - Completed with exit code 0.
- Exact combined command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderParsesUstarEntries -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderAppliesPAXExtendedHeaders -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerAppliesPAXPathBeforeWriting -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerAppliesGNULongPathAndLinkBeforeWriting -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesLayerWhiteoutsIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesGzipLayerIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesPAXLayerMetadataIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesGNULongLayerNamesIntoStagingRoot test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.11_22-59-01-+0200.xcresult`.
- Exact summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.11_22-59-01-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 8`
  - `passedTests: 8`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro.
- Exact command:
  - `rtk git diff --check -- OrlixOS/Sources/Session/OrlixRootfsImport.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift docs/plans/active/oci-derived-environments-virtio-plane/IMPLEMENT.md`
- Result:
  - Succeeded, no whitespace errors reported.

**Current conclusion:**

- Rootfs tar import and OCI layer import now have iOS Simulator XCTest proof for common PAX and GNU long-name tar metadata.
- This advances image-root fidelity, but still does not cover xattrs, device nodes, sparse files, registry pull, OCI Runtime Spec lifecycle, namespace expansion, networking, cgroups, or real-Linux oracle comparison.

## 2026-06-11 Base-256 tar numeric import fidelity

**Scope:**

Improve rootfs tar and OCI layer import fidelity for tar archives that encode numeric fields in base-256 form. This matters for rootfs archives with uid, gid, or size values outside the traditional octal field range. This remains OrlixOS import behavior and does not move Linux semantics into HostAdapter or use Apple container/containerization, Virtualization.framework, Docker, or runc.

**Code changes:**

- `OrlixRootfsTarManifestReader` now parses positive base-256 tar numeric fields for size, mode, uid, and gid.
- Octal parsing remains the default path for standard tar headers.
- Added OrlixOS tests proving:
  - rootfs tar manifest parsing preserves base-256 uid/gid/size values;
  - OCI layout import carries base-256 uid/gid metadata through staging and generated ext4 metadata commands.

**Evidence:**

- Exact focused command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderParsesBase256NumericFields -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterCarriesBase256NumericMetadata test`
- Result:
  - Completed with exit code 0.
- Exact combined command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderParsesUstarEntries -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderParsesBase256NumericFields -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderAppliesPAXExtendedHeaders -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerAppliesPAXPathBeforeWriting -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerAppliesGNULongPathAndLinkBeforeWriting -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesLayerWhiteoutsIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesGzipLayerIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesPAXLayerMetadataIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesGNULongLayerNamesIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterCarriesBase256NumericMetadata test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.11_23-03-40-+0200.xcresult`.
- Exact summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.11_23-03-40-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 10`
  - `passedTests: 10`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro.
- Exact command:
  - `rtk git diff --check -- OrlixOS/Sources/Session/OrlixRootfsImport.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift docs/plans/active/oci-derived-environments-virtio-plane/IMPLEMENT.md`
- Result:
  - Succeeded, no whitespace errors reported.

**Current conclusion:**

- Rootfs tar import and OCI layer import now have iOS Simulator XCTest proof for standard octal numeric fields, positive base-256 numeric fields, PAX metadata, GNU long path/link metadata, gzip layer decoding, digest verification, and whiteout application.
- This improves import fidelity for container-image-derived roots, but still does not cover xattrs, device nodes, sparse files, registry pull, OCI Runtime Spec lifecycle, namespace expansion, networking, cgroups, or real-Linux oracle comparison.

## 2026-06-11 Linux special-file import metadata

**Scope:**

Improve rootfs tar and OCI layer import fidelity for Linux character devices, block devices, and FIFOs. This remains OrlixOS import and image-materialization metadata behavior. It does not create host device nodes in the iOS Simulator staging tree, does not move Linux semantics into HostAdapter, and does not add Apple container/containerization, Virtualization.framework, Docker, or runc.

**Code changes:**

- `OrlixRootfsTarManifestReader` now parses tar type `3`, `4`, and `6` entries as Linux character devices, block devices, and FIFOs.
- Tar device major/minor fields are preserved in `OrlixRootfsTarManifestEntry` for character and block devices.
- `OrlixRootfsTarMaterializer` and the OCI layer applicator carry these special entries as manifest metadata while only creating parent directories in the host staging tree.
- `OrlixEnvironmentImageMaterializationPlan` now emits debugfs commands that `cd` into the parent directory, run `mknod` on the basename, return to `/`, and then apply uid, gid, and mode metadata with unquoted absolute debugfs paths.
- Added OrlixOS tests proving:
  - tar manifest parsing preserves character device, block device, and FIFO metadata;
  - tar materialization does not create host special files in staging but generates image metadata commands;
  - OCI layout import carries special-file metadata through staging and generated ext4 metadata commands.

**Evidence:**

- Exact focused command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderParsesLinuxSpecialFiles -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerCarriesSpecialFilesAsImageMetadataOnly -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterCarriesLinuxSpecialFilesAsImageMetadata test`
- Result:
  - Initial sandboxed run failed before build/test execution because CoreSimulatorService and SwiftPM diagnostics cache access were blocked.
  - Escalated rerun completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.11_23-20-33-+0200.xcresult`.
- Exact focused summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.11_23-20-33-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 3`
  - `passedTests: 3`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro.
- Exact broader command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderParsesUstarEntries -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderParsesLinuxSpecialFiles -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderParsesBase256NumericFields -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderAppliesPAXExtendedHeaders -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerAppliesPAXPathBeforeWriting -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerAppliesGNULongPathAndLinkBeforeWriting -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerCarriesSpecialFilesAsImageMetadataOnly -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesLayerWhiteoutsIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesGzipLayerIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesPAXLayerMetadataIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesGNULongLayerNamesIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterCarriesBase256NumericMetadata -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterCarriesLinuxSpecialFilesAsImageMetadata test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.11_23-21-51-+0200.xcresult`.
- Exact broader summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.11_23-21-51-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 13`
  - `passedTests: 13`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro.
- Exact e2fsprogs materialization command:
  - `rtk make -f OrlixOS/Makefile environment-root-image PROFILE=release ORLIXOS_ENVIRONMENT_STAGING_ROOT=/private/tmp/orlix-special-staging-3 ORLIXOS_ENVIRONMENT_BASE_IMAGE=/private/tmp/orlix-special-env-3/base.ext4 ORLIXOS_ENVIRONMENT_STATE_IMAGE=/private/tmp/orlix-special-env-3/state.ext4 ORLIXOS_ENVIRONMENT_IMAGE_WORK_DIR=/private/tmp/orlix-special-env-3/work ORLIXOS_ENVIRONMENT_BASE_DEBUGFS_COMMANDS=/private/tmp/orlix-special-env-3/base-debugfs.commands ORLIX_MKE2FS=/opt/homebrew/opt/e2fsprogs/sbin/mke2fs ORLIX_DEBUGFS=/opt/homebrew/opt/e2fsprogs/sbin/debugfs`
- Result:
  - Completed with exit code 0.
  - The earlier absolute-path `mknod /dev/null ...` command shape allocated an inode but did not link the node under `/dev`, so it was replaced with `cd /dev`, `mknod null ...`, `cd /`.
  - The corrected command completed without the previous `File not found by ext2_lookup` messages.
- Exact debugfs inspection commands:
  - `rtk /opt/homebrew/opt/e2fsprogs/sbin/debugfs -R 'stat /dev/null' /private/tmp/orlix-special-env-3/base.ext4`
  - `rtk /opt/homebrew/opt/e2fsprogs/sbin/debugfs -R 'stat /dev/vda' /private/tmp/orlix-special-env-3/base.ext4`
  - `rtk /opt/homebrew/opt/e2fsprogs/sbin/debugfs -R 'stat /run/initctl' /private/tmp/orlix-special-env-3/base.ext4`
- Result:
  - `/dev/null`: `Type: character special`, `Mode: 0666`, `Device major/minor number: 01:03`.
  - `/dev/vda`: `Type: block special`, `Mode: 0600`, `Device major/minor number: 254:00`.
  - `/run/initctl`: `Type: FIFO`, `Mode: 0600`.

**Current conclusion:**

- Rootfs tar import and OCI layer import now have iOS Simulator XCTest proof for Linux character-device, block-device, and FIFO metadata without creating host special files in staging.
- The generated special-file command shape has e2fsprogs `debugfs` proof against a real ext4 image. This remains build/materialization proof, not a macOS runtime target.
- This improves image import fidelity, but still does not cover xattrs, sparse files, registry pull, OCI Runtime Spec lifecycle, namespace expansion, networking, cgroups, or real-Linux oracle comparison.

## 2026-06-11 PAX xattr import metadata

**Scope:**

Improve rootfs tar and OCI layer import fidelity for PAX `SCHILY.xattr.*` and `LIBARCHIVE.xattr.*` extended attributes. This remains OrlixOS import and image-materialization metadata behavior. It does not rely on host filesystem xattrs in the staging tree, does not move Linux semantics into HostAdapter, and does not add Apple container/containerization, Virtualization.framework, Docker, or runc.

**Code changes:**

- `OrlixRootfsTarManifestEntry` now carries string extended attributes.
- `OrlixRootfsTarPAXExtendedHeader` now maps PAX keys under `SCHILY.xattr.` into manifest xattrs without the prefix.
- `OrlixRootfsTarPAXExtendedHeader` now maps PAX keys under `LIBARCHIVE.xattr.` by URL-decoding the xattr name and base64-decoding the value. The decoded value must currently be UTF-8 text.
- `OrlixEnvironmentImageMaterializationPlan` now emits debugfs `ea_set` commands after uid, gid, and mode commands for entries with xattrs.
- Added OrlixOS tests proving:
  - tar manifest parsing preserves `SCHILY.xattr.security.selinux`, `SCHILY.xattr.user.comment`, and `LIBARCHIVE.xattr.user.libarchive%2Ecomment`;
  - tar materialization carries xattrs as metadata and generates `ea_set` commands;
  - OCI layout import carries PAX xattrs through staging and generated ext4 metadata commands.

**Primary-source check:**

- Fetched libarchive source for research only:
  - `rtk curl -L https://raw.githubusercontent.com/libarchive/libarchive/master/libarchive/archive_write_set_format_pax.c -o /private/tmp/archive_write_set_format_pax.c`
  - `rtk curl -L https://raw.githubusercontent.com/libarchive/libarchive/master/libarchive/archive_read_support_format_tar.c -o /private/tmp/archive_read_support_format_tar.c`
- Relevant findings from libarchive source:
  - writer emits `LIBARCHIVE.xattr.` keys with URL-encoded names and base64-encoded values;
  - writer emits `SCHILY.xattr.` keys with the same URL-encoded name handling path but raw binary values;
  - reader decodes `LIBARCHIVE.xattr.` names with URL decode and values with base64 decode;
  - reader maps `SCHILY.xattr.` directly to xattr values.

**Evidence:**

- Exact focused command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderAppliesPAXExtendedHeaders -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerCarriesPAXExtendedAttributesIntoMetadata -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesPAXLayerMetadataIntoStagingRoot test`
- Result:
  - First focused run failed before tests because Swift rejected `] + try ...` expression syntax in `OrlixEnvironmentImageMaterialization.swift`.
  - After rewriting command assembly to append throwing xattr commands explicitly, the focused rerun completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.11_23-33-15-+0200.xcresult`.
- Exact focused summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.11_23-33-15-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 3`
  - `passedTests: 3`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro.
- Exact broader command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderParsesUstarEntries -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderParsesLinuxSpecialFiles -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderParsesBase256NumericFields -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderAppliesPAXExtendedHeaders -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerAppliesPAXPathBeforeWriting -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerCarriesPAXExtendedAttributesIntoMetadata -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerAppliesGNULongPathAndLinkBeforeWriting -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerCarriesSpecialFilesAsImageMetadataOnly -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesLayerWhiteoutsIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesGzipLayerIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesPAXLayerMetadataIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesGNULongLayerNamesIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterCarriesBase256NumericMetadata -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterCarriesLinuxSpecialFilesAsImageMetadata test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.11_23-34-39-+0200.xcresult`.
- Exact broader summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.11_23-34-39-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 14`
  - `passedTests: 14`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro.
- Exact e2fsprogs materialization command:
  - `rtk make -f OrlixOS/Makefile environment-root-image PROFILE=release ORLIXOS_ENVIRONMENT_STAGING_ROOT=/private/tmp/orlix-xattr-staging-2 ORLIXOS_ENVIRONMENT_BASE_IMAGE=/private/tmp/orlix-xattr-env-2/base.ext4 ORLIXOS_ENVIRONMENT_STATE_IMAGE=/private/tmp/orlix-xattr-env-2/state.ext4 ORLIXOS_ENVIRONMENT_IMAGE_WORK_DIR=/private/tmp/orlix-xattr-env-2/work ORLIXOS_ENVIRONMENT_BASE_DEBUGFS_COMMANDS=/private/tmp/orlix-xattr-env-2/base-debugfs.commands ORLIX_MKE2FS=/opt/homebrew/opt/e2fsprogs/sbin/mke2fs ORLIX_DEBUGFS=/opt/homebrew/opt/e2fsprogs/sbin/debugfs`
- Result:
  - Completed with exit code 0.
- Exact debugfs inspection commands:
  - `rtk /opt/homebrew/opt/e2fsprogs/sbin/debugfs -R 'ea_list /etc/os-release' /private/tmp/orlix-xattr-env-2/base.ext4`
  - `rtk /opt/homebrew/opt/e2fsprogs/sbin/debugfs -R 'ea_get /etc/os-release security.selinux' /private/tmp/orlix-xattr-env-2/base.ext4`
  - `rtk /opt/homebrew/opt/e2fsprogs/sbin/debugfs -R 'ea_get /etc/os-release user.comment' /private/tmp/orlix-xattr-env-2/base.ext4`
- Result:
  - `security.selinux (26) = "system_u:object_r:etc_t:s0"`
  - `user.comment (11) = "hello world"`

**Current conclusion:**

- Rootfs tar import and OCI layer import now have iOS Simulator XCTest proof for PAX `SCHILY.xattr.*` string attributes and `LIBARCHIVE.xattr.*` UTF-8 string attributes.
- Real e2fsprogs ext4 proof shows the generated `ea_set` command shape writes `security.selinux` and `user.comment` attributes into the base image.
- This improves image import fidelity, but still does not cover binary xattr payloads, sparse files, registry pull, OCI Runtime Spec lifecycle, namespace expansion, networking, cgroups, or real-Linux oracle comparison.

## 2026-06-11 GNU sparse PAX 0.1 import fidelity

**Scope:**

Improve rootfs tar and OCI layer import fidelity for GNU sparse PAX 0.1 files. This remains OrlixOS import and image-materialization behavior for the initial iOS Simulator stage. macOS is only the development host used to run Xcode, create fixtures, and inspect result bundles. It is not a runtime target, not a Linux user model, and not part of the product contract.

**Code changes:**

- `OrlixRootfsTarManifestEntry` now carries sparse extent metadata and optional logical size.
- `OrlixRootfsTarPAXExtendedHeader` now parses `GNU.sparse.map`, `GNU.sparse.size`, and optional `GNU.sparse.realsize`.
- `OrlixRootfsTarMaterializer` reconstructs sparse logical file contents by creating the destination file, seeking to each sparse extent offset, writing the corresponding payload segment, and truncating to the logical size.
- The OCI layer applicator applies the same sparse reconstruction path when a layer entry contains sparse extents.
- Added OrlixOS tests proving:
  - tar materialization reconstructs the logical bytes from a GNU sparse PAX 0.1 map;
  - OCI layout import reconstructs the logical bytes from a GNU sparse PAX 0.1 layer entry.

**Primary-source check:**

- Used the previously fetched libarchive source under `/private/tmp/archive_read_support_format_tar.c` for research only.
- Relevant findings from libarchive source:
  - GNU sparse PAX 0.1 uses `GNU.sparse.map` with comma-separated offset and size pairs;
  - `GNU.sparse.size` is the logical file size for sparse entries;
  - archive payload contains the concatenated sparse data blocks, while the reconstructed file has holes or zero-filled gaps between extents.

**Evidence:**

- Exact focused command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerAppliesGNUSparsePAXMap -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesGNUSparsePAXMap test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.11_23-39-12-+0200.xcresult`.
- Exact focused summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.11_23-39-12-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 2`
  - `passedTests: 2`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro.
- Exact broader command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderParsesUstarEntries -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderParsesLinuxSpecialFiles -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderParsesBase256NumericFields -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderAppliesPAXExtendedHeaders -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerAppliesPAXPathBeforeWriting -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerCarriesPAXExtendedAttributesIntoMetadata -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerAppliesGNULongPathAndLinkBeforeWriting -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerCarriesSpecialFilesAsImageMetadataOnly -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerAppliesGNUSparsePAXMap -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesLayerWhiteoutsIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesGzipLayerIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesPAXLayerMetadataIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesGNULongLayerNamesIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterCarriesBase256NumericMetadata -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterCarriesLinuxSpecialFilesAsImageMetadata -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesGNUSparsePAXMap test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.11_23-42-46-+0200.xcresult`.
- Exact broader summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.11_23-42-46-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 16`
  - `passedTests: 16`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro.

**Current conclusion:**

- Rootfs tar import and OCI layer import now have iOS Simulator XCTest proof for GNU sparse PAX 0.1 logical content reconstruction.
- This reconstructs the logical file bytes. It does not prove ext4 block sparseness or hole allocation preservation.
- This improves image import fidelity, but still does not cover GNU sparse 0.0 repeated attributes, GNU sparse 1.0 map-before-data, old GNU sparse headers, Solaris sparse formats, binary xattr payloads, registry pull, OCI Runtime Spec lifecycle, namespace expansion, networking, cgroups, or real-Linux oracle comparison.

## 2026-06-11 GNU sparse PAX validation hardening

**Scope:**

Harden malformed GNU sparse PAX handling in OrlixOS rootfs tar and OCI layer import. This remains import and image-materialization behavior for iOS Simulator proof. It does not change OrlixKernel Linux semantics, does not add Apple container/containerization, and does not introduce a VM runtime.

**Code changes:**

- Tightened sparse writer bounds checks in `OrlixRootfsImport.swift` and `OrlixOCIImageLayout.swift` so remaining payload length is checked before slicing archive data.
- Added tar materializer tests proving malformed sparse maps fail when an extent exceeds the logical size.
- Added tar materializer tests proving sparse payload length mismatches fail and leave no committed target root.
- Added OCI layout import tests proving sparse payload length mismatches fail and leave no committed staging root.

**Evidence:**

- Exact focused command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerAppliesGNUSparsePAXMap -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerRejectsGNUSparsePAXMapOutsideLogicalSize -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerRejectsGNUSparsePAXPayloadLengthMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesGNUSparsePAXMap -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRejectsGNUSparsePAXPayloadLengthMismatch test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.11_23-46-34-+0200.xcresult`.
- Exact focused summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.11_23-46-34-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 5`
  - `passedTests: 5`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro.
- Exact broader command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderParsesUstarEntries -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderParsesLinuxSpecialFiles -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderParsesBase256NumericFields -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderAppliesPAXExtendedHeaders -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerAppliesPAXPathBeforeWriting -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerCarriesPAXExtendedAttributesIntoMetadata -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerAppliesGNULongPathAndLinkBeforeWriting -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerCarriesSpecialFilesAsImageMetadataOnly -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerAppliesGNUSparsePAXMap -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerRejectsGNUSparsePAXMapOutsideLogicalSize -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerRejectsGNUSparsePAXPayloadLengthMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesLayerWhiteoutsIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesGzipLayerIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesPAXLayerMetadataIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesGNULongLayerNamesIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterCarriesBase256NumericMetadata -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterCarriesLinuxSpecialFilesAsImageMetadata -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesGNUSparsePAXMap -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRejectsGNUSparsePAXPayloadLengthMismatch test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.11_23-48-51-+0200.xcresult`.
- Exact broader summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.11_23-48-51-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 19`
  - `passedTests: 19`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro.

**Current conclusion:**

- Rootfs tar import and OCI layer import now reject malformed GNU sparse PAX metadata that would otherwise produce incorrect logical files.
- Failed sparse imports do not commit the tar materializer root or the OCI staging root.
- This improves import correctness, but still does not cover GNU sparse 0.0 repeated attributes, GNU sparse 1.0 map-before-data, old GNU sparse headers, Solaris sparse formats, binary xattr payloads, registry pull, OCI Runtime Spec lifecycle, namespace expansion, networking, cgroups, or real-Linux oracle comparison.

## 2026-06-11 OCI descriptor size validation

**Scope:**

Harden OCI image layout validation so descriptor `size` fields are enforced alongside digest verification. This remains OrlixOS image-input validation for the iOS Simulator stage. It does not change OrlixKernel Linux behavior, does not add registry pull, and does not introduce Apple container/containerization or a VM runtime.

**Code changes:**

- `OrlixOCIImageLayoutBlobStore.verifiedBlob` now requires the expected descriptor size and rejects blobs whose byte count does not match.
- Manifest descriptor size is verified when reading `index.json`.
- Config descriptor size is verified when reading the image config from the selected manifest.
- Layer descriptor size is verified both during layout reading and during import/materialization.
- Added OrlixOS tests proving manifest, config, and layer size mismatches are rejected independently while valid digest verification still passes.

**Evidence:**

- Exact focused command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderSelectsLinuxArm64AndVerifiesBlobs -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsDigestMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsManifestSizeMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsConfigSizeMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsLayerSizeMismatch test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.11_23-52-53-+0200.xcresult`.
- Exact focused summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.11_23-52-53-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 5`
  - `passedTests: 5`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro.
- Exact broader command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderParsesUstarEntries -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderParsesLinuxSpecialFiles -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderParsesBase256NumericFields -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderAppliesPAXExtendedHeaders -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerAppliesPAXPathBeforeWriting -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerCarriesPAXExtendedAttributesIntoMetadata -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerAppliesGNULongPathAndLinkBeforeWriting -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerCarriesSpecialFilesAsImageMetadataOnly -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerAppliesGNUSparsePAXMap -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerRejectsGNUSparsePAXMapOutsideLogicalSize -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerRejectsGNUSparsePAXPayloadLengthMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderSelectsLinuxArm64AndVerifiesBlobs -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsDigestMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsManifestSizeMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsConfigSizeMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsLayerSizeMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesLayerWhiteoutsIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesGzipLayerIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesPAXLayerMetadataIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesGNULongLayerNamesIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterCarriesBase256NumericMetadata -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterCarriesLinuxSpecialFilesAsImageMetadata -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesGNUSparsePAXMap -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRejectsGNUSparsePAXPayloadLengthMismatch test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.11_23-54-17-+0200.xcresult`.
- Exact broader summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.11_23-54-17-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 24`
  - `passedTests: 24`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro.

**Current conclusion:**

- OCI layout import now validates both digest and descriptor size for manifest, config, and layer blobs.
- This strengthens content-addressed image input validation before any environment root is materialized.
- This does not yet cover registry pull, zstd layers, OCI Runtime Spec lifecycle, namespace expansion, networking, cgroups, or real-Linux oracle comparison.

## 2026-06-11 OCI rootfs diff ID validation

**Scope:**

Validate OCI config `rootfs.diff_ids` against decoded layer bytes during OrlixOS OCI layout import. This remains image-input validation for the iOS Simulator stage. It does not change OrlixKernel Linux behavior, does not add registry pull, and does not introduce Apple container/containerization or a VM runtime.

**Code changes:**

- `OrlixOCIImageLayoutImport` now carries parsed `rootfs.diff_ids` when an OCI config provides them.
- `OrlixOCIImageLayoutReader` validates `rootfs.diff_ids` syntax and count when present.
- `OrlixOCIImageLayoutImporter` now verifies each decoded layer tar payload against the matching `rootfs.diff_ids` entry before applying it.
- Added OrlixOS tests proving:
  - valid diff IDs are parsed from the OCI config;
  - diff ID count mismatches are rejected;
  - decoded layer diff ID mismatches fail before committing an OCI staging root;
  - gzip layers are checked against the decoded tar bytes, not the compressed blob bytes.

**Evidence:**

- Exact focused command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderParsesRootfsDiffIDs -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsRootfsDiffIDCountMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRejectsRootfsDiffIDMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterVerifiesGzipLayerRootfsDiffID test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.11_23-58-21-+0200.xcresult`.
- Exact focused summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.11_23-58-21-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 4`
  - `passedTests: 4`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro.
- Exact broader command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderParsesUstarEntries -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderParsesLinuxSpecialFiles -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderParsesBase256NumericFields -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderAppliesPAXExtendedHeaders -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerAppliesPAXPathBeforeWriting -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerCarriesPAXExtendedAttributesIntoMetadata -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerAppliesGNULongPathAndLinkBeforeWriting -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerCarriesSpecialFilesAsImageMetadataOnly -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerAppliesGNUSparsePAXMap -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerRejectsGNUSparsePAXMapOutsideLogicalSize -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerRejectsGNUSparsePAXPayloadLengthMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderSelectsLinuxArm64AndVerifiesBlobs -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsDigestMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsManifestSizeMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsConfigSizeMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsLayerSizeMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderParsesRootfsDiffIDs -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsRootfsDiffIDCountMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRejectsRootfsDiffIDMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterVerifiesGzipLayerRootfsDiffID -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesLayerWhiteoutsIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesGzipLayerIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesPAXLayerMetadataIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesGNULongLayerNamesIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterCarriesBase256NumericMetadata -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterCarriesLinuxSpecialFilesAsImageMetadata -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesGNUSparsePAXMap -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRejectsGNUSparsePAXPayloadLengthMismatch test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.11_23-59-54-+0200.xcresult`.
- Exact broader summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.11_23-59-54-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 28`
  - `passedTests: 28`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro.

**Current conclusion:**

- OCI layout import now validates descriptor digest, descriptor size, and optional config `rootfs.diff_ids` before applying decoded layers.
- Gzip layer diff ID validation is against the decoded tar payload, matching OCI image semantics.
- This strengthens image-as-input correctness, but still does not cover registry pull, zstd layers, OCI Runtime Spec lifecycle, namespace expansion, networking, cgroups, or real-Linux oracle comparison.

## 2026-06-12 OCI descriptor media type validation

**Scope:**

Validate OCI manifest and config descriptor media types before using their blobs. This remains OrlixOS image-input validation for the iOS Simulator stage. It does not change OrlixKernel Linux behavior, does not add registry pull, and does not introduce Apple container/containerization or a VM runtime.

**Code changes:**

- `OrlixOCIImageLayoutReader` now rejects selected manifest descriptors whose media type is not `application/vnd.oci.image.manifest.v1+json`.
- `OrlixOCIImageLayoutReader` now rejects manifest config descriptors whose media type is not `application/vnd.oci.image.config.v1+json`.
- Added OrlixOS tests proving unsupported manifest and config media types fail while the valid OCI layout reader path still passes.

**Evidence:**

- Exact focused command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderSelectsLinuxArm64AndVerifiesBlobs -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsUnsupportedManifestMediaType -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsUnsupportedConfigMediaType test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_00-03-04-+0200.xcresult`.
- Exact focused summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_00-03-04-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 3`
  - `passedTests: 3`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro.
- Exact broader command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderParsesUstarEntries -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderParsesLinuxSpecialFiles -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderParsesBase256NumericFields -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderAppliesPAXExtendedHeaders -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerAppliesPAXPathBeforeWriting -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerCarriesPAXExtendedAttributesIntoMetadata -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerAppliesGNULongPathAndLinkBeforeWriting -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerCarriesSpecialFilesAsImageMetadataOnly -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerAppliesGNUSparsePAXMap -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerRejectsGNUSparsePAXMapOutsideLogicalSize -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerRejectsGNUSparsePAXPayloadLengthMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderSelectsLinuxArm64AndVerifiesBlobs -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsDigestMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsManifestSizeMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsConfigSizeMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsLayerSizeMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsUnsupportedManifestMediaType -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsUnsupportedConfigMediaType -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderParsesRootfsDiffIDs -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsRootfsDiffIDCountMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRejectsRootfsDiffIDMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterVerifiesGzipLayerRootfsDiffID -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesLayerWhiteoutsIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesGzipLayerIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesPAXLayerMetadataIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesGNULongLayerNamesIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterCarriesBase256NumericMetadata -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterCarriesLinuxSpecialFilesAsImageMetadata -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesGNUSparsePAXMap -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRejectsGNUSparsePAXPayloadLengthMismatch test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_00-04-33-+0200.xcresult`.
- Exact broader summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_00-04-33-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 30`
  - `passedTests: 30`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro.

**Current conclusion:**

- OCI layout import now validates selected manifest, config, blob digest, blob size, and optional config `rootfs.diff_ids` before applying decoded layers.
- This further narrows accepted image inputs to the OCI image shape Orlix currently supports.
- This does not yet cover Docker manifest compatibility, registry pull, zstd layers, OCI Runtime Spec lifecycle, namespace expansion, networking, cgroups, or real-Linux oracle comparison.

## 2026-06-12 Docker schema2 descriptor media type compatibility

**Scope:**

Accept Docker schema2 manifest and config descriptor media types in the OCI layout reader while keeping unsupported descriptor types rejected. This remains OrlixOS image-input compatibility for the iOS Simulator stage. It does not add Docker daemon behavior, runc, registry pull, OrlixKernel changes, Apple container/containerization, or a VM runtime.

**Code changes:**

- `OrlixOCIImageLayoutReader` now accepts selected manifest descriptors with media type `application/vnd.docker.distribution.manifest.v2+json` in addition to `application/vnd.oci.image.manifest.v1+json`.
- `OrlixOCIImageLayoutReader` now accepts config descriptors with media type `application/vnd.docker.container.image.v1+json` in addition to `application/vnd.oci.image.config.v1+json`.
- Unsupported manifest and config descriptor media types still fail explicitly.
- Added OrlixOS tests proving Docker schema2 descriptor media types are accepted while unsupported example media types remain rejected.

**Evidence:**

- Exact focused command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderSelectsLinuxArm64AndVerifiesBlobs -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderAcceptsDockerSchema2DescriptorMediaTypes -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsUnsupportedManifestMediaType -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsUnsupportedConfigMediaType test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_00-07-40-+0200.xcresult`.
- Exact focused summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_00-07-40-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 4`
  - `passedTests: 4`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro.
- Exact broader command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderParsesUstarEntries -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderParsesLinuxSpecialFiles -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderParsesBase256NumericFields -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderAppliesPAXExtendedHeaders -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerAppliesPAXPathBeforeWriting -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerCarriesPAXExtendedAttributesIntoMetadata -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerAppliesGNULongPathAndLinkBeforeWriting -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerCarriesSpecialFilesAsImageMetadataOnly -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerAppliesGNUSparsePAXMap -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerRejectsGNUSparsePAXMapOutsideLogicalSize -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerRejectsGNUSparsePAXPayloadLengthMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderSelectsLinuxArm64AndVerifiesBlobs -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsDigestMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsManifestSizeMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsConfigSizeMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsLayerSizeMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderAcceptsDockerSchema2DescriptorMediaTypes -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsUnsupportedManifestMediaType -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsUnsupportedConfigMediaType -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderParsesRootfsDiffIDs -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsRootfsDiffIDCountMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRejectsRootfsDiffIDMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterVerifiesGzipLayerRootfsDiffID -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesLayerWhiteoutsIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesGzipLayerIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesPAXLayerMetadataIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesGNULongLayerNamesIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterCarriesBase256NumericMetadata -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterCarriesLinuxSpecialFilesAsImageMetadata -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesGNUSparsePAXMap -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRejectsGNUSparsePAXPayloadLengthMismatch test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_00-09-11-+0200.xcresult`.
- Exact broader summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_00-09-11-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 31`
  - `passedTests: 31`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro.

**Current conclusion:**

- OCI layout import now accepts both OCI and Docker schema2 manifest/config descriptor media types for local image layouts.
- Unsupported manifest/config descriptor media types remain rejected.
- This improves compatibility with common registry-produced image layouts, but still does not cover Docker daemon behavior, registry pull, zstd layers, OCI Runtime Spec lifecycle, namespace expansion, networking, cgroups, or real-Linux oracle comparison.

## 2026-06-12 Zstd layer boundary hardening

**Scope:**

Keep zstd-compressed OCI layer media types explicitly unsupported until OrlixOS has an iOS-runtime-safe zstd decoder. This remains OrlixOS image-input validation for the iOS Simulator stage. It does not add Apple container/containerization, shelling out to host tools, OrlixKernel changes, Docker daemon behavior, runc, registry pull, or a VM runtime.

**Local SDK check:**

- Searched the local iPhone Simulator SDK and Xcode toolchain headers for zstd support:
  - `rtk rg -n "COMPRESSION_ZSTD|COMPRESSION_LZFSE|compression_decode_buffer|compression_stream" /Applications/Xcode.app/Contents/Developer/Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator26.5.sdk/usr/include /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/include`
- Result:
  - `compression.h` exposes Compression framework APIs such as `compression_decode_buffer` and algorithms such as `COMPRESSION_LZFSE`.
  - No `COMPRESSION_ZSTD` symbol was found in the local iPhone Simulator 26.5 SDK or Xcode toolchain headers.
- Searched the project for existing zstd userland/runtime support:
  - `rtk rg -n "zstd|ZSTD|Compression|compression_decode|libzstd" . project.yml OrlixOS OrlixKernel OrlixHostAdapter`
- Result:
  - OrlixKernel carries upstream Linux zstd decompressor sources for kernel-owned use.
  - OrlixOS does not have a userland zstd decoder dependency wired for iOS runtime layer import.

**Code changes:**

- Replaced the single zstd rejection test with coverage for all currently recognized zstd layer media type shapes:
  - `application/vnd.oci.image.layer.v1.tar+zstd`
  - `application/vnd.oci.image.layer.nondistributable.v1.tar+zstd`
  - `application/vnd.docker.image.rootfs.diff.tar.zstd`
- Kept `OrlixOCILayerDecoder` rejecting these media types through `unsupportedLayerMediaType` until a real decoder is added.

**Evidence:**

- Exact focused command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCILayerDecoderSupportsGzipMediaType -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRejectsZstdLayerMediaTypesUntilDecoderExists test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_00-12-57-+0200.xcresult`.
- Exact focused summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_00-12-57-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 2`
  - `passedTests: 2`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro.
- Exact broader command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderParsesUstarEntries -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderParsesLinuxSpecialFiles -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderParsesBase256NumericFields -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderAppliesPAXExtendedHeaders -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerAppliesPAXPathBeforeWriting -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerCarriesPAXExtendedAttributesIntoMetadata -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerAppliesGNULongPathAndLinkBeforeWriting -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerCarriesSpecialFilesAsImageMetadataOnly -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerAppliesGNUSparsePAXMap -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerRejectsGNUSparsePAXMapOutsideLogicalSize -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerRejectsGNUSparsePAXPayloadLengthMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderSelectsLinuxArm64AndVerifiesBlobs -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsDigestMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsManifestSizeMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsConfigSizeMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsLayerSizeMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderAcceptsDockerSchema2DescriptorMediaTypes -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsUnsupportedManifestMediaType -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsUnsupportedConfigMediaType -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderParsesRootfsDiffIDs -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsRootfsDiffIDCountMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRejectsRootfsDiffIDMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterVerifiesGzipLayerRootfsDiffID -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesLayerWhiteoutsIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesGzipLayerIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesPAXLayerMetadataIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesGNULongLayerNamesIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterCarriesBase256NumericMetadata -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterCarriesLinuxSpecialFilesAsImageMetadata -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesGNUSparsePAXMap -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRejectsGNUSparsePAXPayloadLengthMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCILayerDecoderSupportsGzipMediaType -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRejectsZstdLayerMediaTypesUntilDecoderExists test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_00-17-48-+0200.xcresult`.
- Exact broader summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_00-17-48-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 33`
  - `passedTests: 33`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro.

**Current conclusion:**

- Gzip layer decoding remains supported and tested.
- Zstd layer media types are explicitly rejected for OCI, OCI nondistributable, and Docker layer forms until OrlixOS has a real iOS-runtime-safe zstd decoder.
- This makes the unsupported boundary explicit, but zstd image import remains unimplemented.

## 2026-06-12 OCI rootfs type validation

**Scope:**

Validate OCI config `rootfs.type` when a rootfs object is present. OrlixOS currently supports the image-layer rootfs model only. This remains OrlixOS image-input validation for the iOS Simulator stage. The initial target is not macOS user runtime behavior. macOS is only the development host when a local tool is explicitly needed to produce or inspect fixtures. This does not change OrlixKernel Linux behavior, does not add registry pull, and does not introduce Docker daemon behavior, runc, Apple container/containerization, or a VM runtime.

**Code changes:**

- `OrlixOCIImageLayoutReader` now accepts absent `rootfs` config objects for existing local layout fixtures.
- `OrlixOCIImageLayoutReader` now accepts `rootfs.type == "layers"`.
- `OrlixOCIImageLayoutReader` now rejects unsupported non-`layers` rootfs types explicitly.
- Added OrlixOS tests proving:
  - the existing valid layout path without a rootfs object remains accepted;
  - `rootfs.type == "layers"` is accepted even when no diff IDs are present;
  - unsupported rootfs types fail with `unsupportedRootfsType`.

**Evidence:**

- Exact focused command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderSelectsLinuxArm64AndVerifiesBlobs -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderAcceptsRootfsLayersTypeWithoutDiffIDs -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsUnsupportedRootfsType test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_00-21-52-+0200.xcresult`.
- Exact focused summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_00-21-52-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 3`
  - `passedTests: 3`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro.
- Exact broader command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderParsesUstarEntries -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderParsesLinuxSpecialFiles -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderParsesBase256NumericFields -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarManifestReaderAppliesPAXExtendedHeaders -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerAppliesPAXPathBeforeWriting -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerCarriesPAXExtendedAttributesIntoMetadata -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerAppliesGNULongPathAndLinkBeforeWriting -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerCarriesSpecialFilesAsImageMetadataOnly -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerAppliesGNUSparsePAXMap -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerRejectsGNUSparsePAXMapOutsideLogicalSize -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarMaterializerRejectsGNUSparsePAXPayloadLengthMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderSelectsLinuxArm64AndVerifiesBlobs -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsDigestMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsManifestSizeMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsConfigSizeMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsLayerSizeMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderAcceptsDockerSchema2DescriptorMediaTypes -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsUnsupportedManifestMediaType -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsUnsupportedConfigMediaType -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderParsesRootfsDiffIDs -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderAcceptsRootfsLayersTypeWithoutDiffIDs -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsUnsupportedRootfsType -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderRejectsRootfsDiffIDCountMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRejectsRootfsDiffIDMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterVerifiesGzipLayerRootfsDiffID -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesLayerWhiteoutsIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesGzipLayerIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesPAXLayerMetadataIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesGNULongLayerNamesIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterCarriesBase256NumericMetadata -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterCarriesLinuxSpecialFilesAsImageMetadata -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesGNUSparsePAXMap -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRejectsGNUSparsePAXPayloadLengthMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCILayerDecoderSupportsGzipMediaType -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRejectsZstdLayerMediaTypesUntilDecoderExists test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_00-23-27-+0200.xcresult`.
- Exact broader summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_00-23-27-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 35`
  - `passedTests: 35`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro.

**Current conclusion:**

- OCI config `rootfs.type` is now validated when present.
- OrlixOS accepts absent rootfs config objects for current fixtures and accepts `rootfs.type == "layers"`.
- Unsupported rootfs types fail before image root materialization.
- This tightens image input validation, but does not cover registry pull, zstd import support, OCI Runtime Spec lifecycle, namespace expansion, networking, cgroups, or real-Linux oracle comparison.

## 2026-06-12 OCI platform variant validation

**Scope:**

Tighten OCI image-layout platform binding in OrlixOS so an index descriptor with `platform.variant` is not silently selected for a plain `os/architecture` request. This remains iOS Simulator stage image-input validation. It does not change OrlixKernel Linux behavior, does not add registry pull, and does not introduce Apple container/containerization, Docker daemon behavior, runc, or a VM runtime.

**Code changes:**

- `OrlixOCIPlatform` now parses `os/architecture[/variant]`.
- OCI index descriptor matching now includes `variant`.
- Added OrlixOS tests proving:
  - `linux/arm64/v8` selects a descriptor with `platform.variant == "v8"`;
  - a plain `linux/arm64` request does not silently select a `linux/arm64/v8` descriptor.
- Fixed a materialization bug exposed by the full OrlixOSTests run:
  - regular file metadata commands can use quoted debugfs paths with escaped quotes;
  - unquoted debugfs path restrictions remain limited to special-file `mknod` commands.
- Corrected a stale OrlixOS test expectation so a descriptor-provided environment with only `PATH` expects only the encoded `PATH` token.

**Evidence:**

- Initial sandbox-local focused attempt failed before project code ran because CoreSimulator and SwiftPM cache access were denied.
- First escalated focused attempt reached project compilation and failed on an unterminated Swift string literal in the new test helper. This was fixed before proof was claimed.
- Exact focused platform command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderSelectsLinuxArm64AndVerifiesBlobs -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderSelectsRequestedPlatformVariant -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderDoesNotSelectVariantForPlainPlatform test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_00-29-50-+0200.xcresult`.
- Exact focused platform summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_00-29-50-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 3`
  - `passedTests: 3`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro.
- Full OrlixOSTests regression attempt after platform validation initially failed in two existing tests:
  - `testEnvironmentImageMaterializationGeneratesManifestMetadataCommands` failed with `invalidDebugfsPath("home/orlix/has \"quotes\"")`.
  - `testEnvironmentRootImageDefaultsToOverlayRootCommandLine` had a stale expected command line for descriptor-provided environment variables.
- Exact focused regression command after fixes:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentImageMaterializationGeneratesManifestMetadataCommands -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentRootImageDefaultsToOverlayRootCommandLine -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderSelectsRequestedPlatformVariant -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderDoesNotSelectVariantForPlainPlatform test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_00-33-36-+0200.xcresult`.
- Exact focused regression summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_00-33-36-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 4`
  - `passedTests: 4`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro.
- Exact full OrlixOSTests command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_00-35-42-+0200.xcresult`.
- Exact full summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_00-35-42-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 76`
  - `passedTests: 76`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro.

**Current conclusion:**

- OCI layout selection is now exact for `os`, `architecture`, and optional `variant`.
- Plain `linux/arm64` requests no longer silently bind variant-qualified descriptors.
- This improves image-input fidelity for running OCI-derived environments as-is, but does not cover registry pull, zstd import support, OCI Runtime Spec lifecycle, namespace expansion, networking, cgroups, or real-Linux oracle comparison.

## 2026-06-12 OCI user binding boundary

**Scope:**

Stop silently mapping unsupported OCI `config.User` values to root during OrlixOS OCI layout import. Numeric `uid` and `uid:gid` forms remain supported as environment descriptor credentials. Named user and group resolution is not implemented yet because it requires resolving through the imported image rootfs, for example `/etc/passwd` and `/etc/group`. Until that exists, named users fail explicitly instead of running as uid 0 by accident.

This remains OrlixOS image-config to environment-descriptor binding for the iOS Simulator stage. It does not change OrlixKernel credential semantics, does not move Linux user behavior into HostAdapter, does not add registry pull, and does not introduce Apple container/containerization, Docker daemon behavior, runc, or a VM runtime.

**Code changes:**

- `OrlixOCIImageLayoutImporter` now throws during descriptor binding when OCI `config.User` is not numeric.
- Added `OrlixOCIImageLayoutError.unsupportedUser`.
- Numeric `User` values still bind:
  - `1000` maps to uid `1000`, gid `0`;
  - `1000:100` maps to uid `1000`, gid `100`.
- The OCI layout fixture helper now accepts a `User` override.
- Added OrlixOS tests proving:
  - numeric `1000:100` binds to environment descriptor uid/gid;
  - named `app` fails with `unsupportedUser("app")` even if the imported rootfs contains `/etc/passwd`.

**Evidence:**

- Exact focused command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutReaderSelectsLinuxArm64AndVerifiesBlobs -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterBindsNumericUserAndGroup -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRejectsNamedUserUntilPasswdResolutionExists test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_00-39-27-+0200.xcresult`.
- Exact focused summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_00-39-27-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 3`
  - `passedTests: 3`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro.
- Exact full OrlixOSTests command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_00-40-37-+0200.xcresult`.
- Exact full summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_00-40-37-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 78`
  - `passedTests: 78`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro.

**Current conclusion:**

- OCI imports no longer silently run unsupported named users as root.
- Numeric OCI user credentials bind into the Orlix environment descriptor.
- Named user and group resolution remains unimplemented and must be added against the imported rootfs before those images can run as-is with named `User` config.

## 2026-06-12 OCI rootfs-backed user resolution

**Scope:**

Resolve named OCI `config.User` values from the imported rootfs during OrlixOS OCI layout import. This supersedes the earlier explicit rejection boundary for named users. It still remains OrlixOS image-config to environment-descriptor binding for the iOS Simulator stage. OrlixKernel continues to own Linux credential behavior after process launch.

This does not move Linux semantics into HostAdapter, does not add registry pull, does not add Apple container/containerization, Docker daemon behavior, runc, or a VM runtime.

**Code changes:**

- `OrlixEnvironmentRegistry` can now prepare storage by environment id without requiring a premature descriptor.
- `OrlixOCIImageLayoutImporter` now:
  - prepares storage from the environment id;
  - applies OCI layers into a temporary root;
  - resolves OCI `User` against that temporary root;
  - moves the root into committed staging only after descriptor binding succeeds;
  - saves the final descriptor after the rootfs has been staged.
- Added a private staged-root account parser for:
  - `/etc/passwd`;
  - `/etc/group`.
- Supported OCI `User` forms now include:
  - numeric uid, for example `1000`;
  - numeric uid/gid, for example `1000:100`;
  - named user from `/etc/passwd`, for example `app`;
  - named user plus named group from `/etc/passwd` and `/etc/group`, for example `app:staff`.
- Missing named users or groups still fail with `unsupportedUser(...)`.

**Evidence:**

- Exact focused command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterBindsNumericUserAndGroup -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterResolvesNamedUserFromImportedPasswd -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterResolvesNamedGroupFromImportedGroup -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRejectsMissingNamedUser test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_00-45-31-+0200.xcresult`.
- Exact focused summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_00-45-31-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 4`
  - `passedTests: 4`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro.
- Exact full OrlixOSTests command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_00-46-40-+0200.xcresult`.
- Exact full summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_00-46-40-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 80`
  - `passedTests: 80`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro.

**Current conclusion:**

- Named OCI users now resolve from the imported rootfs before the environment descriptor is saved.
- Named OCI groups now resolve from the imported rootfs.
- Missing named users or groups fail explicitly instead of falling back to root.
- This improves OCI image-as-input fidelity, but does not prove runtime credential transitions inside OrlixKernel, registry pull, zstd import support, OCI Runtime Spec lifecycle, namespace expansion, networking, cgroups, or real-Linux oracle comparison.

## 2026-06-12 Linux-shaped execution default validation

**Scope:**

Remove OrlixOS host-path style filtering from Linux execution defaults. Absolute Linux paths containing `..` and empty argv strings are valid Linux userspace inputs. OrlixOS should encode them into the boot/session descriptor path and let OrlixKernel Linux path resolution and exec behavior decide runtime success or failure.

This remains OrlixOS descriptor-to-kernel-command-line encoding for the iOS Simulator stage. It does not move path resolution, exec semantics, or syscall behavior out of OrlixKernel.

**Code changes:**

- `OrlixEnvironmentRootImage` still rejects:
  - empty exec path;
  - relative exec path;
  - embedded NUL in exec path, argv, or cwd;
  - empty or relative cwd.
- `OrlixEnvironmentRootImage` no longer rejects:
  - absolute exec paths containing `..`;
  - absolute cwd paths containing `..`;
  - empty argv strings after argv0.
- Added OrlixOS test coverage proving these values are percent-encoded or passed through in the materialized environment command line rather than rejected before Linux sees them.

**Evidence:**

- Exact focused command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentRootImageEncodesLinuxPathDefaultsWithoutHostPathPolicy -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentRootImageRejectsUnsafeDefaultCommandExecutable -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentRootImageRejectsUnsafeExecutionDefaults test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_00-50-25-+0200.xcresult`.
- Exact focused summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_00-50-25-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 3`
  - `passedTests: 3`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro.
- Exact full OrlixOSTests command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_00-51-28-+0200.xcresult`.
- Exact full summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_00-51-28-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 81`
  - `passedTests: 81`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro.

**Current conclusion:**

- OrlixOS no longer applies host-path traversal policy to Linux exec/cwd defaults.
- Empty argv strings can now pass through descriptor encoding.
- This improves OCI image-as-input fidelity, but does not prove runtime exec path resolution, runtime cwd failure behavior, or imported binary execution inside OrlixKernel.

## 2026-06-12 Linux path descriptor defaults proof on iOS Simulator

**Scope:**

Prove that an OCI-derived materialized environment can boot on iOS Simulator with Linux-shaped descriptor defaults:

- exec path `/bin/../bin/sh`;
- cwd `/tmp/..`;
- `argv0` supplied by descriptor;
- an empty argv element after `argv0`.

This is still an iOS Simulator proof only. macOS is the development host used to build and run the simulator test, not the initial runtime target and not the Linux surface model.

**Code changes:**

- `OrlixOS/Sources/init/init.c` now reads empty command-line values, so `orlix.argvN=` is preserved instead of being treated as absent.
- `OrlixOS/Sources/init/init.c` no longer rejects absolute exec/cwd defaults because they contain `..`; OrlixKernel Linux path resolution remains the owner of runtime behavior.
- `OrlixTestRunner/Tests/XCTest/OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests.swift` adds focused iOS Simulator coverage for the OCI-derived materialized root using Linux path descriptor defaults.

**Evidence:**

- Initial focused run failed before boot because the fixture was stale:
  - `stale imported environment runtime fixture: fixture OCI-derived was generated from a stale init`
- Fixture recovery command:
  - `rtk timeout 900 make -f OrlixOS/Makefile environment-runtime-proof-fixtures PROFILE=release`
- Result:
  - Completed with exit code 0.
  - Rebuilt Coreutils package inputs, OrlixOS base root tree, tar-imported environment images, and OCI-imported environment images.
- Exact focused command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests/testOCIDerivedMaterializedRootRunsLinuxPathDescriptorDefaults test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.12_01-04-33-+0200.xcresult`.
- Exact summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.12_01-04-33-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 1`
  - `passedTests: 1`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro.

**Current conclusion:**

- The OCI-derived materialized environment can boot through the PTY runtime path on iOS Simulator with Linux path defaults and an empty argv element.
- This proves the descriptor-to-init path for this case. It does not prove generic ELF compatibility, OCI Runtime Spec lifecycle, registry pull, networking, namespaces, cgroups, or real-Linux oracle comparison.

## 2026-06-12 Environment fixture preflight for PTY runtime tests

**Scope:**

Add a deterministic fixture preflight for `OrlixPTYRuntimeTests` so stale tar-imported and OCI-imported runtime proof images are detected and rebuilt before focused iOS Simulator tests run.

**Code changes:**

- `OrlixOS/Makefile` adds `environment-runtime-proof-fixtures` as a stamp-backed target.
- The fixture target writes and checks:
  - profile;
  - init binary SHA-256;
  - rootfs proof SHA-256;
  - fixture readiness files for `tar-imported` and `oci-imported`.
- `project.yml` wires a generated `OrlixPTYRuntimeTests` post-build script that invokes:
  - `make -f "$SRCROOT/OrlixOS/Makefile" environment-runtime-proof-fixtures PROFILE="$profile"`
- `rtk make xcodeproj` regenerated `OrlixSystem.xcodeproj/project.pbxproj` with the post-build script.

**Evidence:**

- First incremental check before the fast-path fix:
  - `rtk timeout 120 make -f OrlixOS/Makefile environment-runtime-proof-fixtures PROFILE=release`
  - Result: exit code 124 after the target rebuilt package inputs. This was a Makefile dependency bug, not a runtime proof failure.
- Recovery command after fixing the fast-path validation:
  - `rtk timeout 900 make -f OrlixOS/Makefile environment-runtime-proof-fixtures PROFILE=release`
- Result:
  - Completed with exit code 0.
  - Rebuilt interrupted package proof outputs and regenerated both environment runtime proof fixtures.
- Incremental proof command:
  - `rtk timeout 120 make -f OrlixOS/Makefile environment-runtime-proof-fixtures PROFILE=release`
- Result:
  - Completed with exit code 0.
  - Output: `Orlix environment runtime proof fixtures ready: /Users/rudironsoni/src/github/rudironsoni/orlix/OrlixSystem/Build/OrlixOS/environment-runtime-proof`
- Focused iOS Simulator proof through generated Xcode project:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests/testOCIDerivedMaterializedRootRunsLinuxPathDescriptorDefaults test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.12_01-20-48-+0200.xcresult`.
- Exact summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.12_01-20-48-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 1`
  - `passedTests: 1`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5.

**Caveat:**

- The focused Xcode run still showed the separate OrlixOS payload embed phase rebuilding package inputs. That is outside the fixture preflight and remains a build-time cost to investigate separately.

**Current conclusion:**

- The PTY runtime test target now has an explicit fixture preflight, and the fixture target has a proven fast path.
- The iOS Simulator runtime proof passes with the generated Xcode build phase present.
- This does not prove full runtime readiness, native-performance ELF execution, OCI Runtime Spec process lifecycle, or container-image execution without the current fixture materialization path.

## 2026-06-12 Architecture invariant guardrails for forbidden runtime paths

**Scope:**

Strengthen Phase 0 source-level guardrails so active iOS runtime sources reject:

- historical `IXLand` branding and `OrlixKit` compatibility names;
- Darwin/Foundation/POSIX host headers in Linux-owner OrlixKernel overlay code;
- OrlixHostAdapter headers in Linux-owner OrlixKernel overlay code;
- Apple container, Apple containerization, and Virtualization runtime references in active iOS runtime targets;
- local Linux UAPI clone definitions for common syscall, futex, clone, open/path, address-family, and virtio prefixes in active OrlixKernel overlay code.

This is an invariant/proof step only. It does not add runtime behavior, does not claim container-image execution readiness, and does not move Linux semantics out of OrlixKernel.

**Code changes:**

- `OrlixTestRunner/Tests/XCTest/OrlixTestRunnerTests/ArchitectureInvariantTests.swift` now includes `testOrlixKernelOverlayDoesNotDefineLocalLinuxUAPIClones`.
- The new guard scans active OrlixKernel overlay `arch/orlix` and `drivers/orlix` source, excluding Linux userspace selftests, for local `#define` clones of common Linux UAPI prefixes.

**Evidence:**

- Precheck command:
  - `rtk rg -n '^\\s*#\\s*define\\s+(__NR_|SYS_|FUTEX_|CLONE_|O_[A-Z0-9_]+|AT_[A-Z0-9_]+|AF_[A-Z0-9_]+|VIRTIO_|VIRTIO_MMIO_|VIRTIO_BLK_)' OrlixKernel/Sources/ports/orlix/overlay/arch/orlix OrlixKernel/Sources/ports/orlix/overlay/drivers/orlix`
- Result:
  - Exit code 1, no matches.
- Exact iOS Simulator proof command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixTestRunnerTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixTestRunnerTests-2026.06.12_01-31-54-+0200.xcresult`.
- Exact summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixTestRunnerTests-2026.06.12_01-31-54-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 16`
  - `passedTests: 16`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5.

**Caveat:**

- The `OrlixTestRunnerTests` build still ran the separate `OrlixOS` payload embed script and rebuilt package inputs. That remains a build-time cost issue, not a failure of the architecture invariant proof.

**Current conclusion:**

- Phase 0 now has an iOS Simulator XCTest guard for legacy names, forbidden host includes in Linux-owner overlay code, forbidden Apple container or Virtualization runtime references, and local Linux UAPI clone definitions.
- This proof is source-level architecture enforcement. It does not prove ELF execution performance, environment namespace completeness, OCI Runtime Spec behavior, registry pull, networking, cgroups, or real-Linux oracle comparison.

## 2026-06-12 OCI-derived pseudo-filesystem baseline on iOS Simulator

**Scope:**

Add focused PTY runtime proof that an OCI-derived materialized Orlix environment exposes the minimum Linux pseudo-filesystem shape needed by imported rootfs shells:

- `/proc`
- `/proc/mounts`
- `/proc/self/status`
- `/proc/self/fd`
- `/dev`
- `/dev/null`
- `/dev/urandom`
- `/dev/tty`
- `/dev/ptmx`
- `/dev/pts`
- `/sys`
- `/sys/block/vda/ro`
- `/sys/block/vdb/ro`

This is iOS Simulator runtime proof only. macOS is the development host for Xcode, fixtures, and result-bundle inspection. It is not the initial runtime target, not the Linux surface model, and not the user identity model for this stage.

**Code changes:**

- `OrlixTestRunner/Tests/XCTest/OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests.swift` adds `testOCIDerivedMaterializedRootExposesLinuxPseudoFilesystems`.
- The new proof boots the OCI-derived materialized root, sends shell commands through the existing PTY path, checks the pseudo-filesystem paths from inside Linux userspace, records `/proc/self/status` and `/proc/mounts`, and fails loud on missing paths through explicit `ORLIX_ENV_PSEUDOFS_PROOF_FAILED_*` markers.

**Evidence:**

- Initial sandbox-local focused attempt failed before project code ran because CoreSimulator and SwiftPM cache access were denied.
- Exact iOS Simulator proof command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests/testOCIDerivedMaterializedRootExposesLinuxPseudoFilesystems test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.12_01-40-14-+0200.xcresult`.
- Exact summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.12_01-40-14-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 1`
  - `passedTests: 1`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5.

**Caveat:**

- This test proves selected Linux pseudo-filesystem visibility from an OCI-derived materialized environment through the current PTY runtime path. It does not prove full procfs/devfs/sysfs completeness, namespace isolation, network namespace behavior, cgroup behavior, registry pull, OCI Runtime Spec lifecycle, or real-Linux oracle equivalence.
- The focused Xcode run still showed the separate `OrlixOS` payload embed phase rebuilding package inputs. That remains a build-time cost issue.

**Current conclusion:**

- The OCI-derived environment runtime proof now covers a concrete pseudo-filesystem baseline on iOS Simulator.
- The proof remains limited to the initial iOS Simulator stage. No macOS user or macOS runtime assumption was added.

## 2026-06-12 OCI-derived runtime tmpfs mount proof on iOS Simulator

**Scope:**

Add focused PTY runtime proof that an OCI-derived materialized Orlix environment gets Linux runtime tmpfs mounts for:

- `/tmp`
- `/run`
- `/dev/shm`

This advances the storage-policy requirement that Linux `/tmp` is not a raw host path and not persistent rootfs truth. It remains iOS Simulator runtime proof only. macOS is the development host for Xcode, fixtures, and result-bundle inspection, not the initial runtime target and not the Linux surface model.

**Code changes:**

- `OrlixTestRunner/Tests/XCTest/OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests.swift` adds `testOCIDerivedMaterializedRootUsesLinuxRuntimeTmpfsMounts`.
- The new proof boots the OCI-derived materialized root, checks `/proc/mounts` from inside Linux userspace for `tmpfs` entries at `/tmp`, `/run`, and `/dev/shm`, writes test files into each mount, records `/proc/mounts`, and fails loud through explicit `ORLIX_ENV_TMPFS_PROOF_FAILED_*` markers.

**Evidence:**

- Exact iOS Simulator proof command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests/testOCIDerivedMaterializedRootUsesLinuxRuntimeTmpfsMounts test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.12_01-47-01-+0200.xcresult`.
- Exact summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.12_01-47-01-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 1`
  - `passedTests: 1`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5.

**Caveat:**

- This proves selected tmpfs mount visibility and writeability inside the OCI-derived environment. It does not prove cross-boot non-persistence, full mount namespace isolation, named-environment switching inside one running kernel, host Documents mount behavior, external security-scoped mount behavior, or real-Linux oracle equivalence.
- The focused Xcode run still showed the separate `OrlixOS` payload embed phase rebuilding package inputs. That remains a build-time cost issue.

**Current conclusion:**

- OCI-derived runtime proof now covers Linux-visible tmpfs mounts for `/tmp`, `/run`, and `/dev/shm` on iOS Simulator.
- This is still boot-time materialized-root proof. It is not a substitute for one-kernel named environments through Linux mount namespaces.

## 2026-06-12 Documents explicit-mount descriptor metadata

**Scope:**

Add OrlixOS environment descriptor metadata for explicit Documents mounts, without implementing host-folder mounting, HostAdapter behavior, or Linux VFS policy in OrlixOS.

This advances the storage-policy requirement that Documents must not become Linux rootfs truth. Default and imported environments still have no implicit Documents mount. A Documents mount must be represented explicitly as environment metadata with a Linux absolute target path, leaving later runtime behavior to the Linux mount path.

**Code changes:**

- `OrlixOS/Sources/Session/OrlixEnvironment.swift` adds `mounts` to `OrlixEnvironmentDescriptor`, defaulting to an empty list for existing descriptors and decoded older descriptors.
- `OrlixOS/Sources/Session/OrlixEnvironment.swift` adds `OrlixEnvironmentMountSource.documents` and `OrlixEnvironmentMount.documents(targetPath:readOnly:)`.
- `OrlixEnvironmentMount` no longer exposes a public unchecked memberwise initializer. Callers must use the explicit source helper so Documents targets are validated before metadata is built.
- `OrlixEnvironmentMount` validates persisted metadata during `Decodable` construction, so a stored `environment.json` cannot bypass the explicit Documents target checks.
- The explicit Documents helper rejects reserved Linux runtime targets such as `/`, `/proc`, `/dev`, `/sys`, `/run`, and `/tmp`.
- `OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift` adds focused tests for default empty mounts, explicit Documents mount metadata, Codable round-trip, reserved-target rejection, persisted invalid-target rejection, registry save/load persistence, and legacy descriptor decoding to empty mounts.

**Evidence:**

- First focused run failed as expected on the new reserved-target test because `/` was classified as `invalidTargetPath` before it could be classified as a reserved runtime target:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testDefaultEnvironmentDescriptorUsesLinuxShapedDefaults -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testDocumentsMountIsExplicitEnvironmentDescriptorMetadata -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testDocumentsMountRejectsReservedLinuxRuntimeTargets test`
  - Result: exit code 65, `OrlixTerminalSessionTests.testDocumentsMountRejectsReservedLinuxRuntimeTargets()` failed.
- Validator was corrected so `/` reaches the reserved-target path.
- First passing focused iOS Simulator proof command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testDefaultEnvironmentDescriptorUsesLinuxShapedDefaults -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testDocumentsMountIsExplicitEnvironmentDescriptorMetadata -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testDocumentsMountRejectsReservedLinuxRuntimeTargets test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_01-58-25-+0200.xcresult`.
- Exact summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_01-58-25-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 3`
  - `passedTests: 3`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5.
- Expanded focused iOS Simulator proof command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testDefaultEnvironmentDescriptorUsesLinuxShapedDefaults -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testDocumentsMountIsExplicitEnvironmentDescriptorMetadata -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testDocumentsMountRejectsReservedLinuxRuntimeTargets -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentRegistryPersistsDescriptorsUnderStateRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentDescriptorDecodesLegacyMetadataWithDefaultOverlayRootMount test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_02-04-27-+0200.xcresult`.
- Exact summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_02-04-27-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 5`
  - `passedTests: 5`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5.
- Decode-validation focused iOS Simulator proof command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testDocumentsMountIsExplicitEnvironmentDescriptorMetadata -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testDocumentsMountRejectsReservedLinuxRuntimeTargets -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentDescriptorRejectsPersistedInvalidDocumentsMountTargets -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentRegistryPersistsDescriptorsUnderStateRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentDescriptorDecodesLegacyMetadataWithDefaultOverlayRootMount test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_02-11-32-+0200.xcresult`.
- Exact summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_02-11-32-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 5`
  - `passedTests: 5`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5.

**Caveat:**

- This is descriptor metadata and iOS Simulator XCTest proof only. It does not implement host-folder mounting, virtio-fs, security-scoped external folder access, Linux mount namespace entry, or runtime mount dispatch.
- The focused Xcode run still showed the separate `OrlixOS` payload embed phase rebuilding package inputs. That remains a build-time cost issue.

**Current conclusion:**

- Documents now have an explicit descriptor representation instead of being implicit rootfs truth.
- Runtime host-folder mount implementation remains pending and must enter through Linux-shaped mount behavior rather than HostAdapter-owned Linux semantics.

## 2026-06-12 Copied named environment runtime binding proof on iOS Simulator

**Scope:**

Prove that a copied named Orlix environment can be materialized into the existing iOS Simulator runtime proof path with its own copied `base.ext4`, copied `state.ext4`, copied descriptor identity, and Linux-visible root contents.

This advances the named-environment stage before any OCI work in this checkpoint. It does not add Docker behavior, runc behavior, registry pull, Apple container/containerization, Virtualization.framework, a VM runtime, or live in-kernel environment switching.

This is still iOS Simulator runtime proof only. macOS is the development host for Xcode, fixture generation, and result-bundle inspection. macOS is not the initial runtime target, not the Linux user model, and not the product runtime contract.

**Code changes:**

- `OrlixTestRunner/Tests/XCTest/OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests.swift` adds `testCopiedNamedEnvironmentMaterializedRootBootsAndExposesOSRelease`.
- The runtime proof runner now creates a mutable copy of the tar-derived environment fixture, saves a parent descriptor, copies it through `OrlixEnvironmentRegistry.copyEnvironment(from:to:rootImageIdentifier:)`, and boots the copied descriptor.
- The copied runtime proof verifies the copied environment exposes the tar-derived Linux `/etc/os-release` markers through the PTY runtime path.

**Evidence:**

- Exact iOS Simulator proof command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests/testCopiedNamedEnvironmentMaterializedRootBootsAndExposesOSRelease test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.12_03-13-51-+0200.xcresult`.
- Exact summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.12_03-13-51-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 1`
  - `passedTests: 1`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5, arm64.

**Caveat:**

- This proves copied named-environment materialization through the current boot-time root image binding. It does not yet prove multiple live environments inside one running kernel, a runtime mount namespace switch, OCI layout import, overlay lower/upper dispatch, virtio-fs, or Linux namespace completion.

**Current conclusion:**

- The named-environment path now has a focused iOS Simulator runtime proof for copied root image binding.
- The next environment work should keep targeting iOS Simulator first and should not describe the initial runtime as macOS user behavior.

## 2026-06-12 Copied named environment image isolation proof on iOS Simulator

**Scope:**

Prove that copying a named environment creates independent `base.ext4` and `state.ext4` files, not shared aliases to the parent images.

This is Phase 4 storage isolation proof for named environments. It remains OrlixOS storage metadata and file ownership only. It does not implement live environment switching, OCI import, virtio-fs, Docker behavior, runc behavior, Apple container/containerization, Virtualization.framework, or a VM runtime.

This is still iOS Simulator proof. macOS is only the development host for Xcode and result-bundle inspection.

**Code changes:**

- `OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift` adds `testEnvironmentRegistryCopyCreatesIndependentImageFiles`.
- The test saves a parent environment descriptor, writes parent `base.ext4` and `state.ext4`, copies the environment, mutates only the copied image files, and asserts the parent image files retain their original bytes.

**Evidence:**

- Exact iOS Simulator proof command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentRegistryCopyCreatesIndependentImageFiles test`
- Result:
  - Completed with exit code 0 after rerunning with simulator/cache access. The first sandboxed attempt failed before project code on CoreSimulator and SwiftPM cache permissions.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_03-17-56-+0200.xcresult`.
- Exact summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_03-17-56-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 1`
  - `passedTests: 1`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5, arm64.
- Combined named-environment copy cluster proof command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentRegistryCopiesNamedEnvironmentImagesAndMetadata -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentRegistryCopyCreatesIndependentImageFiles -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentRegistryCopyRequiresMaterializedParentImages -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentRegistryCopyDoesNotOverwriteExistingEnvironment test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_03-19-35-+0200.xcresult`.
- Exact summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_03-19-35-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 4`
  - `passedTests: 4`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5, arm64.

**Caveat:**

- This proves file-level image independence after environment copy. It does not prove Linux-visible write isolation inside the kernel, cross-boot state persistence, mount namespace isolation, or one-kernel multi-environment switching.

**Current conclusion:**

- Named environment copy now has both runtime binding proof and independent image-file proof for the iOS Simulator stage.

## 2026-06-12 Focused mount namespace mountinfo proof on iOS Simulator

**Scope:**

Add an app-hosted upstream-style proof that a child process can create a Linux mount namespace, mount tmpfs inside it, observe that tmpfs mount through `/proc/self/mountinfo`, and keep the child mount hidden from the parent.

This advances the Phase 2 and Phase 4 requirement that environment work uses upstream Linux mount namespace and procfs-visible mount state. It does not add OCI behavior, Docker behavior, runc behavior, Apple container/containerization, Virtualization.framework, a VM runtime, or HostAdapter-owned Linux mount semantics.

This remains iOS Simulator proof. macOS is only the development host for Xcode, kselftest payload generation, crash-report inspection, and result-bundle inspection.

**Code changes:**

- `OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/mount_namespace_probe.c` now checks `/proc/self/mountinfo` after the child mounts tmpfs at `/mnt`.
- `OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/kselftest_init.c` accepts an optional `orlix.kselftest=<name>` kernel command-line selector for focused Orlix-owned kselftest execution. The default remains the full installed Orlix kselftest list.
- `OrlixTestRunner/Sources/OrlixUpstreamTestRunner.swift` adds an optional kernel command-line suffix to upstream test run specs and defines `.kernelMountNamespace`.
- `OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift` adds `testMountNamespaceProbeVerifiesMountinfoThroughOrlixOSTerminalSession`.

**Evidence:**

- Path-scoped whitespace check:
  - `rtk git diff --check -- OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/kselftest_init.c OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/mount_namespace_probe.c OrlixTestRunner/Sources/OrlixUpstreamTestRunner.swift OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift`
  - Result: exit code 0.
- Rebuilt kselftest payload command:
  - `rtk timeout 1200 make kselftest PROFILE=release`
- Result:
  - Completed with exit code 0.
  - Repackaged kselftest initramfs: `Build/OrlixMLibC/test-initramfs/release/OrlixTestInitramfs.bundle`.
- Exact iOS Simulator proof command:
  - `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixKernelUpstreamTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixKernelUpstreamTests/OrlixKernelUpstreamTests/testMountNamespaceProbeVerifiesMountinfoThroughOrlixOSTerminalSession test`
- Result:
  - Completed with exit code 0 after fixing optional `rootImage.kernelCommandLine` handling in the focused spec.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_03-32-13-+0200.xcresult`.
- Exact summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_03-32-13-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 1`
  - `passedTests: 1`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5, arm64.

**Failed proof captured during this checkpoint:**

- Full `testKselftestRootfsCompletesThroughOrlixOSTerminalSession` was rerun after rebuilding kselftest payloads and failed before reaching `mount_namespace_probe`.
- Evidence:
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_03-26-40-+0200.xcresult`.
  - Crash report: `/Users/rudironsoni/Library/Logs/DiagnosticReports/OrlixTestRunner-2026-06-12-032807.ips`.
  - Crash shape: `EXC_BAD_ACCESS`, `SIGSEGV`, faulting hosted user thread with `pc=0` while `clone_thread_probe` was running.
  - Kernel log reached `clone_thread_probe` and did not reach the updated `mount_namespace_probe` body in that full-suite run.

**Caveat:**

- The focused mount namespace proof is green. The full upstream kernel suite is not green in the current rebuilt payload because `clone_thread_probe` crashes the app-hosted runtime before later tests run.
- The kselftest selector is a test harness feature only. It is not a product runtime API and does not change Linux syscall, VFS, mount, namespace, or exec semantics.

**Current conclusion:**

- Orlix now has focused iOS Simulator proof that Linux mount namespace tmpfs state is visible through `/proc/self/mountinfo` and hidden from the parent namespace.
- Before claiming the full upstream kernel gate green again, the `clone_thread_probe` hosted user-thread `pc=0` crash must be fixed or otherwise explained with stronger evidence.

## 2026-06-12 OCI WorkingDir descriptor validation stays iOS Simulator first

**Correction and scope:**

The initial runtime proof target is iOS Simulator only. macOS is the development host for Xcode, fixture generation, local oracle tooling, and result-bundle inspection. The plan must not describe this work as aligned with a macOS user, macOS runtime, or macOS Linux surface.

This checkpoint is OrlixOS OCI image-config validation and descriptor preparation. It does not change OrlixKernel Linux semantics, does not add registry pull, and does not introduce Docker daemon behavior, runc, Apple container/containerization, Virtualization.framework, or a VM runtime.

**Code changes:**

- `OrlixOS/Sources/Session/OrlixOCIImageLayout.swift` validates OCI `config.WorkingDir` before building an `OrlixEnvironmentDescriptor`.
- Missing or empty `WorkingDir` maps to `/`.
- Absolute non-NUL `WorkingDir` values are accepted as Linux descriptor defaults.
- Relative or NUL-containing `WorkingDir` values fail import with `OrlixOCIImageLayoutError.invalidWorkingDirectory`.
- `OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift` adds focused coverage for accepted absolute `WorkingDir` and rejected relative `WorkingDir`.

**Evidence:**

- Focused iOS Simulator proof command:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterBindsAbsoluteWorkingDirectory -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRejectsRelativeWorkingDirectory test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_06-50-07-+0200.xcresult`.
  - `totalTestCount: 2`
  - `passedTests: 2`
  - `failedTests: 0`
  - `skippedTests: 0`
- Combined OCI launch-default import proof command:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterBindsNumericUserAndGroup -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterResolvesNamedUserFromImportedPasswd -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterResolvesNamedGroupFromImportedGroup -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterResolvesNamedGroupForNumericUser -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRejectsMissingNamedUser -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRejectsMissingNamedGroup -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterBindsAbsoluteWorkingDirectory -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRejectsRelativeWorkingDirectory test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_06-51-07-+0200.xcresult`.
  - `totalTestCount: 8`
  - `passedTests: 8`
  - `failedTests: 0`
  - `skippedTests: 0`

**Current conclusion:**

- OCI `config.WorkingDir` now has descriptor-validation proof for the iOS Simulator stage.
- No macOS runtime or macOS user-surface assumption is part of this checkpoint.

## 2026-06-12 OCI Env descriptor validation on iOS Simulator

**Scope:**

Make OCI `config.Env` import fail loud when an image contains process-default environment entries that cannot be represented as Linux `KEY=value` entries for the Orlix environment descriptor.

This is OrlixOS OCI image-config validation and descriptor preparation. OrlixKernel still owns the Linux process environment after launch through normal exec behavior. This does not add registry pull, Docker daemon behavior, runc, Apple container/containerization, Virtualization.framework, or a VM runtime.

The runtime proof target remains iOS Simulator. macOS appears only as the Xcode build/result-inspection host.

**Code changes:**

- `OrlixOS/Sources/Session/OrlixOCIImageLayout.swift` now rejects malformed OCI `Env` entries instead of silently dropping them.
- Accepted entries must contain `=`, must have a non-empty key, and must not contain NUL.
- Empty values remain valid, for example `EMPTY=`.
- Duplicate keys keep the last image-provided value through the existing descriptor dictionary behavior.
- `OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift` now has focused coverage for:
  - valid environment entry binding into `OrlixOCIProcessDefaults` and `OrlixEnvironmentDescriptor`;
  - missing `=`;
  - empty environment key;
  - NUL-bearing environment entry.
- The OCI test fixture writer now emits the `Env` array through `JSONEncoder` so control characters are represented as valid JSON escape sequences.

**Failed proof captured during this checkpoint:**

- First focused run failed because the new positive environment-binding test used the helper's default non-tar layer payload, then import correctly reached layer unpacking and failed with `truncatedHeader`.
- Failed result bundle:
  - `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_06-56-07-+0200.xcresult`
- The test fixture was corrected to use a valid tar layer.

**Evidence:**

- Passing focused iOS Simulator proof command:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterBindsEnvironmentEntries -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRejectsMalformedEnvironmentEntry -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRejectsEmptyEnvironmentKey -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRejectsNulEnvironmentEntry -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterBindsAbsoluteWorkingDirectory -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRejectsRelativeWorkingDirectory -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterBindsNumericUserAndGroup -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterResolvesNamedUserFromImportedPasswd -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterResolvesNamedGroupFromImportedGroup -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterResolvesNamedGroupForNumericUser -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRejectsMissingNamedUser -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRejectsMissingNamedGroup test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_06-57-48-+0200.xcresult`.
- Exact summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_06-57-48-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 12`
  - `passedTests: 12`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5, arm64.
- Path-scoped whitespace checks:
  - `rtk git diff --no-index --check /dev/null OrlixOS/Sources/Session/OrlixOCIImageLayout.swift`
  - `rtk git diff --no-index --check /dev/null OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
  - Both returned no output. The non-zero `--no-index` status is expected for compared files that differ from `/dev/null`.
- Runtime dependency scan:
  - `rtk rg -n "AppleContainer|apple/container|Containerization|Virtualization\\.framework|Virtualization|Process\\(|NSTask|Foundation\\.Process|aligned with macOS|aligned with macos|alligned with macos|alligned with macOS" OrlixOS/Sources/Session/OrlixOCIImageLayout.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift --glob '!Build/**'`
  - Result: no matches.

**Current conclusion:**

- OCI `config.Env` now has fail-loud descriptor-validation proof for the iOS Simulator stage.
- No macOS runtime or macOS user-surface assumption is part of this checkpoint.

## 2026-06-12 OCI Entrypoint and Cmd descriptor binding on iOS Simulator

**Scope:**

Prove and tighten OCI `config.Entrypoint` and `config.Cmd` import into the Orlix environment descriptor. OCI image config remains input metadata. OrlixKernel still owns Linux exec, argv delivery, path resolution, cwd behavior, and process lifecycle after launch.

This checkpoint does not add registry pull, Docker daemon behavior, runc, Apple container/containerization, Virtualization.framework, or a VM runtime. It does not introduce stricter path policy for command strings. NUL-bearing command entries are rejected because they cannot be faithfully represented as Linux C argv strings.

The runtime proof target remains iOS Simulator. macOS is only the Xcode build and result-inspection host.

**Code changes:**

- `OrlixOS/Sources/Session/OrlixOCIImageLayout.swift` now validates OCI `Entrypoint` and `Cmd` vectors for NUL before creating `OrlixOCIProcessDefaults`.
- `OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift` now covers:
  - `Entrypoint + Cmd` binding into `descriptor.defaultCommand`;
  - `Cmd`-only binding;
  - empty `Entrypoint` and empty `Cmd` defaulting to `/bin/sh`;
  - NUL rejection in `Entrypoint`;
  - NUL rejection in `Cmd`.
- The OCI fixture writer now emits `Entrypoint` and `Cmd` arrays through `JSONEncoder`, matching the prior `Env` handling for valid JSON escape behavior.

**Evidence:**

- Passing focused iOS Simulator proof command:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterBindsEntrypointAndCommand -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterBindsCommandOnly -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterDefaultsEmptyCommandToShell -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRejectsNulEntrypoint -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRejectsNulCommand -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterBindsEnvironmentEntries -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRejectsMalformedEnvironmentEntry -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRejectsEmptyEnvironmentKey -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRejectsNulEnvironmentEntry -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterBindsAbsoluteWorkingDirectory -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRejectsRelativeWorkingDirectory -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterBindsNumericUserAndGroup -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterResolvesNamedUserFromImportedPasswd -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterResolvesNamedGroupFromImportedGroup -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterResolvesNamedGroupForNumericUser -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRejectsMissingNamedUser -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRejectsMissingNamedGroup test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_07-02-33-+0200.xcresult`.
- Exact summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_07-02-33-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 17`
  - `passedTests: 17`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5, arm64.
- Path-scoped whitespace checks:
  - `rtk git diff --no-index --check /dev/null OrlixOS/Sources/Session/OrlixOCIImageLayout.swift`
  - `rtk git diff --no-index --check /dev/null OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
  - Both returned no output. The non-zero `--no-index` status is expected for compared files that differ from `/dev/null`.
- Runtime dependency scan:
  - `rtk rg -n "AppleContainer|apple/container|Containerization|Virtualization\\.framework|Virtualization|Process\\(|NSTask|Foundation\\.Process|aligned with macOS|aligned with macos|alligned with macos|alligned with macOS" OrlixOS/Sources/Session/OrlixOCIImageLayout.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift --glob '!Build/**'`
  - Result: no matches.

**Current conclusion:**

- OCI `config.Entrypoint` and `config.Cmd` now have descriptor-binding and NUL-rejection proof for the iOS Simulator stage.
- No macOS runtime or macOS user-surface assumption is part of this checkpoint.

## 2026-06-12 OCI whiteout removes dangling symlink on iOS Simulator

**Scope:**

Fix an OCI layer fidelity bug where an upper-layer whiteout could fail to remove a lower-layer dangling symlink in the staging root. This is OrlixOS image-layer application behavior only. It does not move Linux VFS semantics out of OrlixKernel and does not add registry pull, Docker daemon behavior, runc, Apple container/containerization, Virtualization.framework, or a VM runtime.

The runtime proof target remains iOS Simulator. macOS is only the Xcode build and result-inspection host.

**Code changes:**

- `OrlixOS/Sources/Session/OrlixOCIImageLayout.swift` now treats symlinks as existing for OCI whiteout deletion, even when the symlink target is missing.
- `OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift` adds `testOCIImageLayoutImporterWhiteoutRemovesDanglingSymlink`.
- The new test builds a lower OCI layer containing `etc/dangling -> /missing-target`, then applies an upper layer with `etc/.wh.dangling`.
- The proof checks that the dangling symlink is removed from the staging root and omitted from generated base metadata commands.

**Evidence:**

- Passing focused iOS Simulator proof command:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesLayerWhiteoutsIntoStagingRoot -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterAppliesRootOpaqueWhiteoutToManifest -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRecordsImplicitOpaqueDirectoryInManifest -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterPreservesChildrenWhenDirectoryEntryComesLater -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterHonorsWhiteoutOrderWithinLayer -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterWhiteoutRemovesDanglingSymlink test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_07-06-13-+0200.xcresult`.
- Exact summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_07-06-13-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 6`
  - `passedTests: 6`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5, arm64.
- Path-scoped whitespace checks:
  - `rtk git diff --no-index --check /dev/null OrlixOS/Sources/Session/OrlixOCIImageLayout.swift`
  - `rtk git diff --no-index --check /dev/null OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
  - Both returned no output. The non-zero `--no-index` status is expected for compared files that differ from `/dev/null`.
- Runtime dependency scan:
  - `rtk rg -n "AppleContainer|apple/container|Containerization|Virtualization\\.framework|Virtualization|Process\\(|NSTask|Foundation\\.Process|aligned with macOS|aligned with macos|alligned with macos|alligned with macOS" OrlixOS/Sources/Session/OrlixOCIImageLayout.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift --glob '!Build/**'`
  - Result: no matches.

**Current conclusion:**

- OCI whiteout application now removes dangling symlinks from imported image layers for the iOS Simulator stage.
- Existing whiteout, opaque directory, root opacity, implicit directory, and whiteout-order tests remain green in the focused cluster.
- No macOS runtime or macOS user-surface assumption is part of this checkpoint.

## 2026-06-12 OCI failed import cleans environment root before retry on iOS Simulator

**Scope:**

Fix an OCI layout import rollback bug where a failed import after environment storage preparation could leave the destination environment root behind. That stale root could make a retry with the same environment ID fail with `destinationExists` even after the original validation error was fixed.

This is OrlixOS image import and environment-storage preparation behavior only. It does not change OrlixKernel Linux semantics, does not add registry pull, Docker daemon behavior, runc, Apple container/containerization, Virtualization.framework, or a VM runtime.

The runtime proof target remains iOS Simulator. macOS is only the Xcode build and result-inspection host.

**Code changes:**

- `OrlixOS/Sources/Session/OrlixOCIImageLayout.swift` now treats storage preparation, layer application, materialization-plan creation, metadata generation, and descriptor save as one cleanup scope.
- On failure before descriptor save, the importer removes both the temporary rootfs staging directory and the prepared environment root directory.
- On success, the importer does not remove the environment root after descriptor save.
- `OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift` extends `testOCIImageLayoutImporterRejectsRootfsDiffIDMismatch` to prove that a failed diffID import removes the environment root and that a corrected retry with the same environment ID succeeds.

**Evidence:**

- Passing focused iOS Simulator proof command:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRejectsRootfsDiffIDMismatch -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterVerifiesGzipLayerRootfsDiffID -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCILayerDecoderSupportsGzipMediaType -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRejectsZstdLayerMediaTypesUntilDecoderExists test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_07-11-34-+0200.xcresult`.
- Exact summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_07-11-34-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 4`
  - `passedTests: 4`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5, arm64.
- Path-scoped whitespace checks:
  - `rtk git diff --no-index --check /dev/null OrlixOS/Sources/Session/OrlixOCIImageLayout.swift`
  - `rtk git diff --no-index --check /dev/null OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
  - Both returned no output. The non-zero `--no-index` status is expected for compared files that differ from `/dev/null`.
- Runtime dependency scan:
  - `rtk rg -n "AppleContainer|apple/container|Containerization|Virtualization\\.framework|Virtualization|Process\\(|NSTask|Foundation\\.Process|aligned with macOS|aligned with macos|alligned with macos|alligned with macOS" OrlixOS/Sources/Session/OrlixOCIImageLayout.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift --glob '!Build/**'`
  - Result: no matches.

**Current conclusion:**

- Failed OCI layout import now cleans the prepared environment root before retry for the iOS Simulator stage.
- The retry path is proved with the same environment ID after correcting the layer diffID.
- Gzip diffID verification and explicit zstd-unsupported behavior remain green in the focused cluster.
- No macOS runtime or macOS user-surface assumption is part of this checkpoint.

## 2026-06-12 Rootfs tar failed import cleans environment root before retry on iOS Simulator

**Scope:**

Make rootfs tar import use the same transaction shape as OCI layout import. A failed tar import after storage preparation must not leave a prepared environment root that blocks retry with the same environment ID.

This is OrlixOS rootfs import and environment-storage preparation behavior only. It does not change OrlixKernel Linux semantics, does not add OCI registry pull, Docker daemon behavior, runc, Apple container/containerization, Virtualization.framework, or a VM runtime.

The runtime proof target remains iOS Simulator. macOS is only the Xcode build and result-inspection host.

**Code changes:**

- `OrlixOS/Sources/Session/OrlixRootfsImport.swift` now materializes rootfs tar data into a UUID import scratch root first.
- On success, the importer moves the scratch root into the environment `rootfs` staging directory before image materialization metadata generation and descriptor save.
- On failure before descriptor save, the importer removes the temporary scratch root and the prepared environment root directory.
- `OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift` adds `testRootfsTarImporterCleansFailedImportBeforeRetry`.
- The regression uses a missing hardlink target to force tar materialization failure, checks that the environment root is gone, then retries a valid tar import with the same environment ID.

**Evidence:**

- Passing focused iOS Simulator proof command:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarImporterStagesRootfsAndPersistsEnvironmentDescriptor -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarImporterCleansFailedImportBeforeRetry -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsTarImporterDoesNotOverwriteExistingEnvironment test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_07-16-50-+0200.xcresult`.
- Exact summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_07-16-50-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 3`
  - `passedTests: 3`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5, arm64.
- Path-scoped whitespace checks:
  - `rtk git diff --no-index --check /dev/null OrlixOS/Sources/Session/OrlixRootfsImport.swift`
  - `rtk git diff --no-index --check /dev/null OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
  - Both returned no output. The non-zero `--no-index` status is expected for compared files that differ from `/dev/null`.
- Runtime dependency scan:
  - `rtk rg -n "AppleContainer|apple/container|Containerization|Virtualization\\.framework|Virtualization|Process\\(|NSTask|Foundation\\.Process|aligned with macOS|aligned with macos|alligned with macos|alligned with macOS" OrlixOS/Sources/Session/OrlixRootfsImport.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift --glob '!Build/**'`
  - Result: no matches.

**Current conclusion:**

- Rootfs tar import now cleans the prepared environment root before retry for the iOS Simulator stage.
- The retry path is proved with the same environment ID after a failed missing-hardlink tar import.
- Existing rootfs tar import success and existing-environment protection remain green in the focused cluster.
- No macOS runtime or macOS user-surface assumption is part of this checkpoint.

## 2026-06-12 OCI-derived overlay mutation preserves base image and changes state image on iOS Simulator

**Scope:**

Extend the app-hosted OCI-derived OverlayFS runtime proof to cover storage mutation direction. The previous proof showed Linux-visible copy-up and unlink hiding through the merged overlay. This checkpoint proves the lower base image stays byte-stable while the writable state image changes after the same Linux userspace mutation.

This is OrlixTestRunner runtime proof over the existing OrlixOS materialized environment path and upstream Linux OverlayFS. It does not change OrlixKernel Linux semantics, does not add a custom overlay implementation, does not add OCI registry pull, Docker daemon behavior, runc, Apple container/containerization, Virtualization.framework, or a VM runtime.

The runtime proof target remains iOS Simulator. macOS is only the Xcode build and result-inspection host.

**Code changes:**

- `OrlixTestRunner/Tests/XCTest/OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests.swift` adds `testOCIDerivedOverlayMutationLeavesBaseImageStableAndChangesStateImage`.
- The runtime proof copies the OCI-derived fixture, hashes the copied `base.ext4` and `state.ext4`, boots that copied environment, runs the existing Linux OverlayFS copy-up and unlink proof from the shell, then hashes both images again.
- The test asserts that the base image hash is unchanged and the state image hash changed.
- This keeps the proof below Linux userspace semantics: mutation still happens through `/etc/os-release` in the Linux shell, not by editing host files directly.

**Evidence:**

- Passing focused iOS Simulator proof command:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests/testOCIDerivedOverlayMutationLeavesBaseImageStableAndChangesStateImage test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.12_07-20-07-+0200.xcresult`.
- Exact summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.12_07-20-07-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 1`
  - `passedTests: 1`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5, arm64.
- Runtime output includes:
  - `overlay / overlay rw,relatime,lowerdir=/lower,upperdir=/state/upper,workdir=/state/work,uuid=on 0 0`
  - `ID=orlix-oci-runtime-proof`
  - `ORLIX_ENV_OVERLAY_COPYUP_WRITE_OK`
  - `ID=orlix-overlay-copyup-proof`
  - `ORLIX_ENV_OVERLAY_COPYUP_OK`
  - `ORLIX_ENV_OVERLAY_UNLINK_OK`
  - `ORLIX_ENV_OVERLAY_MUTATION_DONE`
- Path-scoped whitespace check:
  - `rtk git diff --no-index --check /dev/null OrlixTestRunner/Tests/XCTest/OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests.swift`
  - Returned no output. The non-zero `--no-index` status is expected for compared files that differ from `/dev/null`.
- Runtime dependency scan:
  - `rtk rg -n "AppleContainer|apple/container|Containerization|Virtualization\\.framework|Virtualization|Process\\(|NSTask|Foundation\\.Process|aligned with macOS|aligned with macos|alligned with macos|alligned with macOS" OrlixTestRunner/Tests/XCTest/OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests.swift --glob '!Build/**'`
  - Result: no matches.

**Current conclusion:**

- OCI-derived environment execution now has focused iOS Simulator proof that Linux OverlayFS copy-up and unlink mutate the writable state image while preserving the read-only lower base image.
- This strengthens the Phase 9 storage model proof but still does not prove a second boot observes the same upper-state mutation, because this XCTest app-hosted runtime path currently supports one Orlix boot per process.
- This does not prove multiple live environments in one kernel, OCI registry pull, real device execution, host-folder mounts, network namespaces, cgroups, systemd images, App Store acceptance, or real-Linux oracle comparison.
- No macOS runtime or macOS user-surface assumption is part of this checkpoint.

## 2026-06-12 Focused virtio-console boot profile contract proof on iOS Simulator

**Scope:**

Add a focused upstream-style OrlixKernel test entry for the existing Linux-owned `boot_profile_contract` selftest. The checkpoint proves the boot profile visible from Linux userspace selects the Orlix virtio console and exposes the expected immutable base and writable state block devices.

This is OrlixTestRunner proof over existing OrlixKernel Linux userspace-visible state. It does not change OrlixKernel Linux semantics, does not add a custom console ABI, and does not add Apple container/containerization, Virtualization.framework, Docker daemon behavior, runc, or a VM runtime.

The runtime proof target remains iOS Simulator. macOS is only the Xcode build and result-inspection host.

**Code changes:**

- `OrlixTestRunner/Sources/OrlixUpstreamTestRunner.swift` adds `kernelBootProfile`, which runs the kernel suite with `orlix.kselftest=boot_profile_contract`.
- `OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift` adds `testBootProfileContractVerifiesVirtioConsoleThroughOrlixOSTerminalSession`.
- The test asserts the output includes the boot profile selftest, Linux command line selection of `hvc0`, live `/proc/consoles` visibility for `hvc0`, device-tree storage-role labels for `vda` and `vdb`, and Linux block-device visibility for both roles.
- The test asserts the focused run does not accidentally execute the clone-thread probe.

**Evidence:**

- Passing focused iOS Simulator proof command:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixKernelUpstreamTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixKernelUpstreamTests/OrlixKernelUpstreamTests/testBootProfileContractVerifiesVirtioConsoleThroughOrlixOSTerminalSession test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_07-23-08-+0200.xcresult`.
- Exact summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_07-23-08-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 1`
  - `passedTests: 1`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5, arm64.
- Runtime output includes:
  - `Kernel command line: console=ttyS0 console=hvc0 rdinit=/init orlix.root=initramfs-only orlix.profile=release orlix.kselftest=boot_profile_contract`
  - `# exec /orlix/boot_profile_contract`
  - `ok 3 - cmdline selects the Orlix virtio console`
  - `ok 11 - live consoles include the Orlix virtio console`
  - `ok 14 - live device tree labels vda as immutable base storage`
  - `ok 15 - live device tree labels vdb as writable state storage`
  - `ok 16 - Linux exposes the immutable base block device`
  - `ok 17 - Linux exposes the writable state block device`
- Path-scoped whitespace checks:
  - `rtk git diff --no-index --check /dev/null OrlixTestRunner/Sources/OrlixUpstreamTestRunner.swift`
  - `rtk git diff --no-index --check /dev/null OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift`
  - Both returned no output. The non-zero `--no-index` status is expected for compared files that differ from `/dev/null`.
- Runtime dependency scan:
  - `rtk rg -n "AppleContainer|apple/container|Containerization|Virtualization\\.framework|Virtualization|Process\\(|NSTask|Foundation\\.Process|aligned with macOS|aligned with macos|alligned with macos|alligned with macOS" OrlixTestRunner/Sources/OrlixUpstreamTestRunner.swift OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift --glob '!Build/**'`
  - Result: no matches.

**Current conclusion:**

- Orlix now has a focused iOS Simulator proof that Linux-visible boot profile state selects the virtio console through `hvc0` and reports it in live `/proc/consoles`.
- The same proof confirms the current boot profile exposes the immutable base and writable state block-device roles through Linux-visible device-tree and block-device state.
- This strengthens the Phase 7 virtio-console visibility proof and Phase 7 virtio-blk storage-role proof. It does not prove the full virtio-console readiness matrix, PTY conformance, multiple live environments, registry pull, networking, cgroups, real device execution, App Store acceptance, or real-Linux oracle comparison.
- No macOS runtime or macOS user-surface assumption is part of this checkpoint.

## 2026-06-12 Focused random device proof on iOS Simulator

**Scope:**

Add a focused upstream-style OrlixKernel selftest for Linux-visible random interfaces needed by imported root environments: `getrandom()` and `/dev/urandom`. The existing `virtio_mmio_probe_contract` continues to prove the virtio-mmio hwrng path through `/dev/hwrng`; this checkpoint adds direct proof for the userland interfaces common Linux binaries expect.

This is OrlixKernel Linux userspace-visible behavior plus OrlixTestRunner selection proof. It does not move randomness semantics into OrlixOS or OrlixHostAdapter, does not add custom Orlix random APIs, and does not add Apple container/containerization, Virtualization.framework, Docker daemon behavior, runc, or a VM runtime.

The runtime proof target remains iOS Simulator. macOS is only the Xcode build and result-inspection host.

**Code changes:**

- `OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/random_device_probe.c` adds a Linux selftest that requires:
  - `getrandom(buffer, 32, 0)` returns the full requested byte count.
  - `/dev/urandom` opens and returns the full requested byte count.
- `OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/Makefile` adds `random_device_probe` to `TEST_GEN_PROGS`.
- `OrlixTestRunner/Sources/OrlixUpstreamTestRunner.swift` adds `kernelRandomDevice`, selected with `orlix.kselftest=random_device_probe`.
- `OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift` adds `testRandomDeviceProbeCompletesThroughOrlixOSTerminalSession` and extends the full kselftest assertions to include the random-device probe output.

**Evidence:**

- Passing focused iOS Simulator proof command:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixKernelUpstreamTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixKernelUpstreamTests/OrlixKernelUpstreamTests/testRandomDeviceProbeCompletesThroughOrlixOSTerminalSession test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_07-28-48-+0200.xcresult`.
- Exact summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_07-28-48-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 1`
  - `passedTests: 1`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5, arm64.
- Runtime output includes:
  - `Kernel command line: console=ttyS0 console=hvc0 rdinit=/init orlix.root=initramfs-only orlix.profile=release orlix.kselftest=random_device_probe`
  - `# exec /orlix/random_device_probe`
  - `ok 1 - Linux getrandom returns random bytes`
  - `ok 2 - Linux /dev/urandom returns random bytes`
  - `ok 5 - random_device_probe`
  - `ORLIX-KSELFTEST-END`
- Full kernel kselftest proof command:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixKernelUpstreamTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixKernelUpstreamTests/OrlixKernelUpstreamTests/testKselftestRootfsCompletesThroughOrlixOSTerminalSession test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_07-32-32-+0200.xcresult`.
- Exact summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_07-32-32-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 1`
  - `passedTests: 1`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5, arm64.
- Full kselftest runtime output includes:
  - `1..16`
  - `# exec /orlix/random_device_probe`
  - `ok 1 - Linux getrandom returns random bytes`
  - `ok 2 - Linux /dev/urandom returns random bytes`
  - `ok 13 - random_device_probe`
  - `ok 16 - virtio_mmio_probe_contract`
  - `ORLIX-KSELFTEST-END`
- Path-scoped whitespace checks:
  - `rtk git diff --no-index --check /dev/null OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/random_device_probe.c`
  - `rtk git diff --no-index --check /dev/null OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/Makefile`
  - `rtk git diff --no-index --check /dev/null OrlixTestRunner/Sources/OrlixUpstreamTestRunner.swift`
  - `rtk git diff --no-index --check /dev/null OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift`
  - All returned no output. The non-zero `--no-index` status is expected for compared files that differ from `/dev/null`.
- Runtime dependency scan:
  - `rtk rg -n "AppleContainer|apple/container|Containerization|Virtualization\\.framework|Virtualization|Process\\(|NSTask|Foundation\\.Process|aligned with macOS|aligned with macos|alligned with macos|alligned with macOS" OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/random_device_probe.c OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/Makefile OrlixTestRunner/Sources/OrlixUpstreamTestRunner.swift OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift --glob '!Build/**'`
  - Result: no matches.

**Current conclusion:**

- Orlix now has focused iOS Simulator proof that Linux `getrandom()` and `/dev/urandom` return data through the app-hosted upstream-style kernel lane.
- This complements the existing `/dev/hwrng` virtio-mmio entropy proof. It does not prove entropy quality, blocking behavior across all flags, `/dev/random`, real device execution, App Store acceptance, multiple live environments, networking, cgroups, or real-Linux oracle comparison.
- No macOS runtime or macOS user-surface assumption is part of this checkpoint.

## 2026-06-12 OCI-derived Linux PTY stdio proof on iOS Simulator

**Scope:**

Add a focused runtime proof that an OCI-derived materialized root uses Linux PTY stdio through the OrlixOS terminal session path. The proof checks stdin, stdout, and stderr as Linux ttys and verifies that the active tty resolves through devpts as `/dev/pts/0`.

This is an iOS Simulator runtime proof over OrlixOS, OrlixKernel, Linux PTY/devpts behavior, and the imported OCI-derived fixture root. macOS is only the Xcode build and result-inspection host for this checkpoint.

This does not add Apple container/containerization, Virtualization.framework, Docker daemon behavior, runc, a VM runtime, a custom Orlix API, or a macOS runtime target.

**Code changes:**

- `OrlixTestRunner/Tests/XCTest/OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests.swift` adds `testOCIDerivedMaterializedRootUsesLinuxPTYStdio`.
- The same file adds `RuntimeProof.ptyStdio`, runtime command generation for the PTY proof, and fail-fast markers for stdin, stdout, stderr, tty command discovery, and tty path validation failures.
- The command script verifies:
  - `/bin/test -t 0`
  - `/bin/test -t 1`
  - `/bin/test -t 2`
  - a `tty` command exists in the imported root environment
  - the tty path matches `/dev/pts/*`

**Evidence:**

- First focused run failed because the literal fatal marker `ORLIX_ENV_PTY_PROOF_FAILED_TTY_COMMAND` appeared in the echoed shell script before command completion. The same output later contained `/dev/pts/0`, so the failure was in the test marker design, not in the observed Linux PTY path. The marker was changed to emit only on the actual failure branch.
- Passing focused iOS Simulator proof command:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests/testOCIDerivedMaterializedRootUsesLinuxPTYStdio test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.12_07-38-35-+0200.xcresult`.
- Exact summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.12_07-38-35-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 1`
  - `passedTests: 1`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5, arm64.
- Runtime output includes:
  - `ORLIX_ENV_PTY_BEGIN`
  - `ORLIX_ENV_PTY_STDIN_OK`
  - `ORLIX_ENV_PTY_STDOUT_OK`
  - `ORLIX_ENV_PTY_STDERR_OK`
  - `/dev/pts/0`
  - `ORLIX_ENV_PTY_PATH_OK`
  - `ORLIX_ENV_PTY_DONE`
- Path-scoped whitespace check:
  - `rtk git diff --no-index --check /dev/null OrlixTestRunner/Tests/XCTest/OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests.swift`
  - Result: no output. The non-zero `--no-index` status is expected for compared files that differ from `/dev/null`.
- Runtime dependency and wording scan:
  - `rtk rg -n "AppleContainer|apple/container|Containerization|Virtualization\\.framework|Virtualization|Process\\(|NSTask|Foundation\\.Process|aligned with macOS|aligned with macos|alligned with macos|alligned with macOS|macOS user|macOS runtime" OrlixTestRunner/Tests/XCTest/OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests.swift --glob '!Build/**'`
  - Result: no matches.

**Current conclusion:**

- Orlix now has focused iOS Simulator proof that an OCI-derived environment shell has Linux tty-backed stdin, stdout, and stderr through the OrlixOS terminal session path.
- The proof confirms tty resolution through Linux devpts as `/dev/pts/0` in the imported OCI-derived root fixture.
- This strengthens the Phase 7 console/PTY interaction proof for OCI-derived environments. It does not prove full PTY conformance, termios behavior, job control, signal delivery through foreground process groups, multiple live environments, networking, cgroups, real device execution, App Store acceptance, or real-Linux oracle comparison.
- No macOS runtime or macOS user-surface assumption is part of this checkpoint.

## 2026-06-12 Tar-derived pseudo-filesystem and tmpfs runtime proof on iOS Simulator

**Scope:**

Close a Phase 5 proof gap by applying the existing imported-root pseudo-filesystem and runtime tmpfs proof scripts to the rootfs-tar-derived environment, not only to the later OCI-derived environment. This keeps the required implementation order honest: rootfs tar import must have the same basic Linux mount-shape proof before OCI-derived environments rely on the same runtime path.

This is an iOS Simulator runtime proof over OrlixOS, OrlixKernel, Linux procfs, devtmpfs, devpts, sysfs, tmpfs, and the tar-imported fixture root. macOS is only the Xcode build and result-inspection host for this checkpoint.

This does not add Apple container/containerization, Virtualization.framework, Docker daemon behavior, runc, a VM runtime, custom Orlix mount APIs, or a macOS runtime target.

**Code changes:**

- `OrlixTestRunner/Tests/XCTest/OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests.swift` adds:
  - `testTarDerivedMaterializedRootExposesLinuxPseudoFilesystems`
  - `testTarDerivedMaterializedRootUsesLinuxRuntimeTmpfsMounts`
- Both tests reuse the existing `RuntimeProof.pseudoFilesystems` and `RuntimeProof.runtimeTmpfs` scripts so the tar-derived and OCI-derived environment roots are checked through the same Linux userspace-visible behavior.

**Evidence:**

- Initial sandboxed `xcodebuild` attempt failed before build/test because CoreSimulator and SwiftPM cache access were blocked. The focused proof was rerun with approved simulator access.
- Passing tar-derived pseudo-filesystem proof command:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests/testTarDerivedMaterializedRootExposesLinuxPseudoFilesystems test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.12_07-43-44-+0200.xcresult`.
- Exact summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.12_07-43-44-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 1`
  - `passedTests: 1`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5, arm64.
- Runtime output includes:
  - `ORLIX_ENV_PSEUDOFS_BEGIN`
  - `ORLIX_ENV_PROC_MOUNTS_OK`
  - `ORLIX_ENV_PROC_SELF_STATUS_OK`
  - `ORLIX_ENV_PROC_SELF_FD_OK`
  - `ORLIX_ENV_DEV_NULL_OK`
  - `ORLIX_ENV_DEV_URANDOM_OK`
  - `ORLIX_ENV_DEV_TTY_OK`
  - `ORLIX_ENV_DEV_PTMX_OK`
  - `ORLIX_ENV_DEV_PTS_OK`
  - `ORLIX_ENV_SYS_BLOCK_VDA_OK`
  - `ORLIX_ENV_SYS_BLOCK_VDB_OK`
  - `ORLIX_ENV_PSEUDOFS_DONE`
- Passing tar-derived runtime tmpfs proof command:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests/testTarDerivedMaterializedRootUsesLinuxRuntimeTmpfsMounts test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.12_07-44-44-+0200.xcresult`.
- Exact summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.12_07-44-44-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 1`
  - `passedTests: 1`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5, arm64.
- Runtime output includes:
  - `ORLIX_ENV_TMPFS_BEGIN`
  - `ORLIX_ENV_TMP_MOUNT_OK`
  - `ORLIX_ENV_RUN_MOUNT_OK`
  - `ORLIX_ENV_DEV_SHM_MOUNT_OK`
  - `ORLIX_ENV_TMP_WRITE_OK`
  - `ORLIX_ENV_RUN_WRITE_OK`
  - `ORLIX_ENV_DEV_SHM_WRITE_OK`
  - `ORLIX_ENV_TMPFS_DONE`
- Path-scoped whitespace check:
  - `rtk git diff --no-index --check /dev/null OrlixTestRunner/Tests/XCTest/OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests.swift`
  - Result: no output. The non-zero `--no-index` status is expected for compared files that differ from `/dev/null`.
- Runtime dependency and wording scan:
  - `rtk rg -n "AppleContainer|apple/container|Containerization|Virtualization\\.framework|Virtualization|Process\\(|NSTask|Foundation\\.Process|aligned with macOS|aligned with macos|alligned with macos|alligned with macOS|macOS user|macOS runtime" OrlixTestRunner/Tests/XCTest/OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests.swift --glob '!Build/**'`
  - Result: no matches.

**Current conclusion:**

- Rootfs-tar-derived environments now have focused iOS Simulator proof for Linux-visible procfs, devtmpfs, devpts, sysfs block-device shape, and runtime tmpfs mounts through the same OrlixOS terminal session path used by OCI-derived runtime proofs.
- This strengthens Phase 5 before relying on Phase 8 and Phase 9 behavior. It does not prove OCI registry pull, real-Linux oracle comparison, host-folder mounts, multiple live environments, networking, cgroups, real device execution, App Store acceptance, or full arbitrary imported binary compatibility.
- No macOS runtime or macOS user-surface assumption is part of this checkpoint.

## 2026-06-12 Copied named environment overlay isolation proof on iOS Simulator

**Scope:**

Add an app-hosted runtime proof that mutating a copied named environment through Linux OverlayFS changes only the copied environment's writable state image. The proof verifies the parent environment's base and state images remain byte-stable, the copied environment's read-only base remains byte-stable, and the copied environment's writable state image changes after the Linux userspace mutation.

This strengthens the named-environment isolation requirement before relying on OCI-derived environment behavior. The proof uses OrlixOS environment copy and session launch, OrlixKernel Linux OverlayFS, and Linux userspace-visible file mutation. macOS is only the Xcode build and result-inspection host for this checkpoint.

This does not add Apple container/containerization, Virtualization.framework, Docker daemon behavior, runc, a VM runtime, custom Orlix filesystem APIs, or a macOS runtime target.

**Code changes:**

- `OrlixTestRunner/Tests/XCTest/OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests.swift` adds:
  - `testCopiedNamedEnvironmentOverlayMutationDoesNotChangeParent`
  - `runCopiedNamedEnvironmentValidatingParentIsolation()`
- The runner now:
  - saves the parent tar-derived environment descriptor;
  - records parent base and state image hashes;
  - creates a copied named environment through `OrlixEnvironmentRegistry.copyEnvironment`;
  - records copied base and state image hashes;
  - boots the copied environment through the normal OrlixOS terminal session path;
  - writes, reads, unlinks, and syncs `/etc/os-release` through Linux OverlayFS;
  - verifies the parent base and state images are unchanged;
  - verifies the copied base image is unchanged;
  - verifies the copied state image changed.

**External oracle feasibility check:**

- `rtk which container`
  - Result: `/opt/homebrew/bin/container`.
- `rtk container system status`
  - Result: `apiserver is not running and not registered with launchd`.
- `rtk container image list`
  - Result: `Ensure container system service has been started with container system start`.
- `rtk container system start`
  - Result: started `Launching container-apiserver...` and `Testing access to container-apiserver...`, but did not complete after about 90 seconds.
- `rtk killall container`
  - Result: completed with exit code 0 after the sandboxed kill attempt was blocked.
- Conclusion: Apple container is installed, but it was not usable as a live external Linux oracle runner in this checkpoint. No Apple container code or service dependency was added to Orlix runtime.

**Evidence:**

- Passing copied-environment overlay isolation proof command:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests/testCopiedNamedEnvironmentOverlayMutationDoesNotChangeParent test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.12_07-52-01-+0200.xcresult`.
- Exact summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.12_07-52-01-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 1`
  - `passedTests: 1`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5, arm64.
- Runtime output includes:
  - `ORLIX_ENV_OVERLAY_MUTATION_BEGIN`
  - `overlay / overlay rw,relatime,lowerdir=/lower,upperdir=/state/upper,workdir=/state/work,uuid=on 0 0`
  - `ID=orlix-tar-runtime-proof`
  - `ORLIX_ENV_OVERLAY_COPYUP_WRITE_OK`
  - `ID=orlix-overlay-copyup-proof`
  - `ORLIX_ENV_OVERLAY_COPYUP_OK`
  - `ORLIX_ENV_OVERLAY_UNLINK_OK`
  - `ORLIX_ENV_OVERLAY_MUTATION_DONE`
- Path-scoped whitespace check:
  - `rtk git diff --no-index --check /dev/null OrlixTestRunner/Tests/XCTest/OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests.swift`
  - Result: no output. The non-zero `--no-index` status is expected for compared files that differ from `/dev/null`.
- Runtime dependency and wording scan:
  - `rtk rg -n "AppleContainer|apple/container|Containerization|Virtualization\\.framework|Virtualization|Process\\(|NSTask|Foundation\\.Process|aligned with macOS|aligned with macos|alligned with macos|alligned with macOS|macOS user|macOS runtime|IXLand|OrlixKit" OrlixTestRunner/Tests/XCTest/OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests.swift --glob '!Build/**'`
  - Result: no matches.
- Existing oracle case validation:
  - `rtk swift tools/orlix-linux-oracle/orlix-linux-oracle.swift validate-case tools/orlix-linux-oracle/cases/path-errno.json`
  - Result: `case path-errno is valid`.

**Current conclusion:**

- Copied named environments now have focused iOS Simulator proof that Linux OverlayFS mutation of the copied environment is isolated from the parent environment's persisted base and state images.
- This strengthens named environment isolation and persistent state safety before broadening OCI-derived execution. It does not prove multiple live environments inside one running OrlixKernel, live real-Linux oracle comparison, OCI registry pull, host-folder mounts, networking, cgroups, real device execution, App Store acceptance, or arbitrary imported binary compatibility.
- No macOS runtime or macOS user-surface assumption is part of this checkpoint.

## 2026-06-12 Focused init/exec/wait process proof on iOS Simulator

**Scope:**

Expose the existing Orlix kselftest `init_exec_probe` as a focused OrlixOS terminal-session proof. The probe runs inside the upstream-Linux-based OrlixKernel on iOS Simulator and verifies Linux process basics that matter before broadening OCI-derived execution:

- `fork` creates a child task;
- `waitpid` returns the forked child;
- `waitpid` observes the child exit status;
- `mmap` returns writable memory;
- anonymous writable `mmap` stays inside the hosted user window;
- a forked child can `exec` the current image and exit.

macOS is only the Xcode build and result-inspection host for this checkpoint. The runtime proof target is iOS Simulator.

This does not add Apple container/containerization, Virtualization.framework, Docker daemon behavior, runc, a VM runtime, custom Orlix process APIs, custom Orlix exec APIs, or a macOS runtime target.

**Code changes:**

- `OrlixTestRunner/Sources/OrlixUpstreamTestRunner.swift` adds `OrlixUpstreamTestRunSpec.kernelInitExec`, selecting `orlix.kselftest=init_exec_probe`.
- `OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift` adds `testInitExecProbeCompletesThroughOrlixOSTerminalSession`, asserting the probe marker and each Linux-visible TAP check.

No OrlixKernel behavior, OrlixHostAdapter behavior, MLibC behavior, Apple container integration, or Virtualization.framework dependency was added.

**Evidence:**

- Initial sandboxed `xcodebuild` attempt failed before test execution because CoreSimulator and SwiftPM cache access were blocked. The focused proof was rerun with approved simulator access.
- Passing focused init/exec/wait proof command:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixKernelUpstreamTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixKernelUpstreamTests/OrlixKernelUpstreamTests/testInitExecProbeCompletesThroughOrlixOSTerminalSession test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_07-57-23-+0200.xcresult`.
- Exact summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_07-57-23-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 1`
  - `passedTests: 1`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5, arm64.
- Runtime output includes:
  - `Kernel command line: console=ttyS0 console=hvc0 rdinit=/init orlix.root=initramfs-only orlix.profile=release orlix.kselftest=init_exec_probe`
  - `# exec /orlix/init_exec_probe`
  - `# child exec /orlix/init_exec_probe`
  - `ORLIX-INIT-EXEC-PROBE`
  - `ok 1 - fork creates a child task`
  - `ok 2 - waitpid returns the forked child`
  - `ok 3 - waitpid observes the child exit status`
  - `ok 4 - mmap syscall returns writable memory`
  - `ok 5 - writable anonymous mmap stays inside hosted user window`
  - `ok 6 - forked child execs current image and exits`
  - `ok 5 - init_exec_probe`
  - `ORLIX-KSELFTEST-END`
- Path-scoped whitespace check:
  - `rtk git diff --check -- OrlixTestRunner/Sources/OrlixUpstreamTestRunner.swift OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift`
  - Result: no output.
- Runtime dependency and wording scan:
  - `rtk rg -n "AppleContainer|apple/container|Containerization|Virtualization\\.framework|Virtualization|Process\\(|NSTask|Foundation\\.Process|aligned with macOS|aligned with macos|alligned with macos|alligned with macOS|macOS user|macOS runtime|IXLand|OrlixKit" OrlixTestRunner/Sources/OrlixUpstreamTestRunner.swift OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift --glob '!Build/**'`
  - Result: no matches.

**Current conclusion:**

- Orlix now has a focused iOS Simulator proof for Linux fork, waitpid, exit-status observation, exec, and anonymous writable mmap through the normal OrlixOS terminal-session path.
- This strengthens the process/task proof surface before relying on OCI-derived environments. It does not prove signal masks, signal delivery, process groups, job control, FD inheritance, close-on-exec, pipe readiness, multiple live environments, registry pull, networking, cgroups, live real-Linux oracle comparison, real device execution, App Store acceptance, or arbitrary imported binary compatibility.
- No macOS runtime or macOS user-surface assumption is part of this checkpoint.

## 2026-06-12 Focused fd inheritance and close-on-exec proof on iOS Simulator

**Scope:**

Add a focused Orlix kselftest and OrlixOS terminal-session proof for Linux fd behavior across `exec`. This covers a narrow but necessary part of running OCI-derived userspace: descriptors that are not marked close-on-exec must survive `exec`, and descriptors marked `FD_CLOEXEC` must be closed by `exec`.

The probe runs inside the upstream-Linux-based OrlixKernel on iOS Simulator and exercises normal Linux syscalls:

- `pipe`;
- `fcntl(F_GETFD)`;
- `fcntl(F_SETFD, FD_CLOEXEC)`;
- `fork`;
- `execv`;
- `read`;
- `waitpid`;
- `EBADF` observation for the close-on-exec descriptor.

macOS is only the Xcode build and result-inspection host for this checkpoint. The runtime proof target is iOS Simulator.

This does not add Apple container/containerization, Virtualization.framework, Docker daemon behavior, runc, a VM runtime, custom Orlix fd APIs, custom Orlix exec APIs, or a macOS runtime target.

**Code changes:**

- `OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/fd_exec_probe.c` adds an Orlix-owned kselftest for fd inheritance and close-on-exec behavior through Linux `exec`.
- `OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/Makefile` adds `fd_exec_probe` to `TEST_GEN_PROGS`.
- `OrlixTestRunner/Sources/OrlixUpstreamTestRunner.swift` adds `OrlixUpstreamTestRunSpec.kernelFDExec`, selecting `orlix.kselftest=fd_exec_probe`.
- `OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift` adds `testFDExecProbeCompletesThroughOrlixOSTerminalSession`, asserting the probe marker, child marker, fd-read marker, close-on-exec `EBADF` marker, and TAP checks.

No OrlixHostAdapter behavior, MLibC behavior, Apple container integration, or Virtualization.framework dependency was added.

**Evidence:**

- Initial sandboxed `xcodebuild` attempt failed before test execution because CoreSimulator and SwiftPM cache access were blocked. The focused proof was rerun with approved simulator access.
- Passing focused fd/exec proof command:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixKernelUpstreamTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixKernelUpstreamTests/OrlixKernelUpstreamTests/testFDExecProbeCompletesThroughOrlixOSTerminalSession test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_08-02-41-+0200.xcresult`.
- Exact summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_08-02-41-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 1`
  - `passedTests: 1`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5, arm64.
- Runtime output includes:
  - `Kernel command line: console=ttyS0 console=hvc0 rdinit=/init orlix.root=initramfs-only orlix.profile=release orlix.kselftest=fd_exec_probe`
  - `# exec /orlix/fd_exec_probe`
  - `# child exec /orlix/fd_exec_probe`
  - `ORLIX-FD-EXEC-PROBE`
  - `ok 1 - pipe creates descriptor pairs`
  - `ok 2 - fcntl reports descriptor without close-on-exec`
  - `ok 3 - fcntl marks selected descriptor close-on-exec`
  - `ORLIX-FD-EXEC-CHILD`
  - `ORLIX-FD-INHERITED-READ-OK`
  - `ORLIX-FD-CLOEXEC-EBADF-OK`
  - `ok 4 - exec preserves non-close-on-exec descriptor`
  - `ok 5 - exec closes close-on-exec descriptor`
  - `ok 5 - fd_exec_probe`
  - `ORLIX-KSELFTEST-END`
- Path-scoped whitespace check:
  - `rtk git diff --check -- OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/fd_exec_probe.c OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/Makefile OrlixTestRunner/Sources/OrlixUpstreamTestRunner.swift OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift`
  - Result: no output.
- Runtime dependency and wording scan:
  - `rtk rg -n "AppleContainer|apple/container|Containerization|Virtualization\\.framework|Virtualization|Process\\(|NSTask|Foundation\\.Process|aligned with macOS|aligned with macos|alligned with macos|alligned with macOS|macOS user|macOS runtime|IXLand|OrlixKit" OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/fd_exec_probe.c OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/Makefile OrlixTestRunner/Sources/OrlixUpstreamTestRunner.swift OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift --glob '!Build/**'`
  - Result: no matches.
- Host-header leakage scan for the new OrlixKernel selftest source:
  - `rtk rg -n "#include <(Foundation|Darwin|CoreFoundation|pthread|mach|sys/sysctl|spawn|crt_externs)>" OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/fd_exec_probe.c`
  - Result: no matches.

**Current conclusion:**

- Orlix now has focused iOS Simulator proof that Linux fd inheritance and close-on-exec behavior work across `exec` through the normal OrlixOS terminal-session path.
- This strengthens the fdtable and exec proof surface before relying on OCI-derived environments. It does not prove signal masks, signal delivery, process groups, job control, full fdtable conformance, `dup` and `dup2` semantics, pipe readiness, poll/epoll readiness, multiple live environments, registry pull, networking, cgroups, live real-Linux oracle comparison, real device execution, App Store acceptance, or arbitrary imported binary compatibility.
- No macOS runtime or macOS user-surface assumption is part of this checkpoint.

## 2026-06-12 Focused signal delivery and wait-status proof on iOS Simulator

**Scope:**

Add a focused Orlix kselftest and OrlixOS terminal-session proof for basic Linux signal behavior before broadening OCI-derived environment execution. The probe runs inside the upstream-Linux-based OrlixKernel on iOS Simulator and exercises normal Linux userspace-visible behavior:

- `sigaction` handler installation;
- signal delivery through `kill(getpid(), SIGUSR1)`;
- signal blocking through `sigprocmask(SIG_BLOCK)`;
- pending signal observation through `sigpending`;
- pending signal delivery after `sigprocmask(SIG_UNBLOCK)`;
- child signal termination through `kill(child, SIGTERM)`;
- `waitpid` observation of `WIFSIGNALED` and `WTERMSIG`.

macOS is only the Xcode build and result-inspection host for this checkpoint. The runtime proof target is iOS Simulator.

This does not add Apple container/containerization, Virtualization.framework, Docker daemon behavior, runc, a VM runtime, custom Orlix signal APIs, custom Orlix wait APIs, or a macOS runtime target.

**Code changes:**

- `OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/signal_wait_probe.c` adds an Orlix-owned kselftest for signal delivery, blocked pending signal behavior, and signal-terminated wait status.
- `OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/Makefile` adds `signal_wait_probe` to `TEST_GEN_PROGS`.
- `OrlixTestRunner/Sources/OrlixUpstreamTestRunner.swift` adds `OrlixUpstreamTestRunSpec.kernelSignalWait`, selecting `orlix.kselftest=signal_wait_probe`.
- `OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift` adds `testSignalWaitProbeCompletesThroughOrlixOSTerminalSession`, asserting the probe marker and each Linux-visible TAP check.

No OrlixHostAdapter behavior, MLibC behavior, Apple container integration, or Virtualization.framework dependency was added.

**Evidence:**

- Initial sandboxed `xcodebuild` attempt failed before test execution because CoreSimulator and SwiftPM cache access were blocked. The focused proof was rerun with approved simulator access.
- Passing focused signal/wait proof command:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixKernelUpstreamTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixKernelUpstreamTests/OrlixKernelUpstreamTests/testSignalWaitProbeCompletesThroughOrlixOSTerminalSession test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_08-09-16-+0200.xcresult`.
- Exact summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_08-09-16-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 1`
  - `passedTests: 1`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5, arm64.
- Runtime output includes:
  - `Kernel command line: console=ttyS0 console=hvc0 rdinit=/init orlix.root=initramfs-only orlix.profile=release orlix.kselftest=signal_wait_probe`
  - `# exec /orlix/signal_wait_probe`
  - `# child exec /orlix/signal_wait_probe`
  - `ORLIX-SIGNAL-WAIT-PROBE`
  - `ok 1 - signal handler runs for delivered signal`
  - `ok 2 - blocked signal remains pending`
  - `ok 3 - unblocked pending signal runs handler`
  - `ok 4 - waitpid observes signal termination status`
  - `ok 5 - signal_wait_probe`
  - `ORLIX-KSELFTEST-END`
- Path-scoped whitespace check:
  - `rtk git diff --check -- OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/signal_wait_probe.c OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/Makefile OrlixTestRunner/Sources/OrlixUpstreamTestRunner.swift OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift`
  - Result: no output.
- Runtime dependency and wording scan:
  - `rtk rg -n "AppleContainer|apple/container|Containerization|Virtualization\\.framework|Virtualization|Process\\(|NSTask|Foundation\\.Process|aligned with macOS|aligned with macos|alligned with macos|alligned with macOS|macOS user|macOS runtime|IXLand|OrlixKit" OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/signal_wait_probe.c OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/Makefile OrlixTestRunner/Sources/OrlixUpstreamTestRunner.swift OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift --glob '!Build/**'`
  - Result: no matches.
- Host-header leakage scan for the new OrlixKernel selftest source:
  - `rtk rg -n "#include <(Foundation|Darwin|CoreFoundation|pthread|mach|sys/sysctl|spawn|crt_externs)>" OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/signal_wait_probe.c`
  - Result: no matches.

**Current conclusion:**

- Orlix now has focused iOS Simulator proof for Linux signal handler delivery, blocked pending signal observation, pending signal delivery after unblock, and `waitpid` signal-termination status through the normal OrlixOS terminal-session path.
- This strengthens the signal/wait proof surface before relying on OCI-derived environments. It does not prove alternate signal stacks, `siginfo_t`, `SA_RESTART`, terminal-generated signals, foreground process group delivery, job control, full process group behavior, full signal mask inheritance across `fork` and `exec`, multiple live environments, registry pull, networking, cgroups, live real-Linux oracle comparison, real device execution, App Store acceptance, or arbitrary imported binary compatibility.
- No macOS runtime or macOS user-surface assumption is part of this checkpoint.

## 2026-06-12 Focused pipe poll readiness proof on iOS Simulator

**Scope:**

Add a focused Orlix kselftest and OrlixOS terminal-session proof for basic Linux pipe readiness behavior before broadening OCI-derived environment execution. The probe runs inside the upstream-Linux-based OrlixKernel on iOS Simulator and exercises normal Linux userspace-visible behavior:

- `pipe` descriptor creation;
- `fcntl(F_GETFL)` and `fcntl(F_SETFL, O_NONBLOCK)`;
- nonblocking empty pipe `read` returning `EAGAIN`;
- `poll` timeout on an empty pipe read end;
- `poll` write readiness on the pipe write end;
- `poll` read readiness after writing data;
- payload preservation through `write` and `read`;
- `POLLHUP` observation after the writer closes.

macOS is only the Xcode build and result-inspection host for this checkpoint. The runtime proof target is iOS Simulator.

This does not add Apple container/containerization, Virtualization.framework, Docker daemon behavior, runc, a VM runtime, custom Orlix pipe APIs, custom Orlix poll APIs, or a macOS runtime target.

**Code changes:**

- `OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/pipe_poll_probe.c` adds an Orlix-owned kselftest for pipe creation, nonblocking read errno behavior, `poll` readiness, payload readback, and hangup readiness.
- `OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/Makefile` adds `pipe_poll_probe` to `TEST_GEN_PROGS`.
- `OrlixTestRunner/Sources/OrlixUpstreamTestRunner.swift` adds `OrlixUpstreamTestRunSpec.kernelPipePoll`, selecting `orlix.kselftest=pipe_poll_probe`.
- `OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift` adds `testPipePollProbeCompletesThroughOrlixOSTerminalSession`, asserting the probe marker and each Linux-visible TAP check.

No OrlixHostAdapter behavior, MLibC behavior, Apple container integration, or Virtualization.framework dependency was added.

**Evidence:**

- Initial sandboxed `xcodebuild` attempt failed before test execution because CoreSimulator and SwiftPM cache access were blocked. The focused proof was rerun with approved simulator access.
- Passing focused pipe/poll proof command:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixKernelUpstreamTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixKernelUpstreamTests/OrlixKernelUpstreamTests/testPipePollProbeCompletesThroughOrlixOSTerminalSession test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_08-15-02-+0200.xcresult`.
- Exact summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_08-15-02-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 1`
  - `passedTests: 1`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5, arm64.
- Runtime output includes:
  - `Kernel command line: console=ttyS0 console=hvc0 rdinit=/init orlix.root=initramfs-only orlix.profile=release orlix.kselftest=pipe_poll_probe`
  - `# exec /orlix/pipe_poll_probe`
  - `# child exec /orlix/pipe_poll_probe`
  - `ORLIX-PIPE-POLL-PROBE`
  - `ok 1 - pipe creates nonblocking read descriptor`
  - `ok 2 - empty nonblocking pipe read returns EAGAIN`
  - `ok 3 - empty pipe read poll times out`
  - `ok 4 - pipe write end polls writable`
  - `ok 5 - pipe read end polls readable after write`
  - `ok 6 - pipe read returns written payload`
  - `ok 7 - pipe read end polls hangup after writer closes`
  - `ok 5 - pipe_poll_probe`
  - `ORLIX-KSELFTEST-END`
- Path-scoped whitespace check:
  - `rtk git diff --check -- OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/pipe_poll_probe.c OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/Makefile OrlixTestRunner/Sources/OrlixUpstreamTestRunner.swift OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift`
  - Result: no output.
- Runtime dependency and wording scan:
  - `rtk rg -n "AppleContainer|apple/container|Containerization|Virtualization\\.framework|Virtualization|Process\\(|NSTask|Foundation\\.Process|aligned with macOS|aligned with macos|alligned with macos|alligned with macOS|macOS user|macOS runtime|IXLand|OrlixKit" OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/pipe_poll_probe.c OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/Makefile OrlixTestRunner/Sources/OrlixUpstreamTestRunner.swift OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift --glob '!Build/**'`
  - Result: no matches.
- Host-header leakage scan for the new OrlixKernel selftest source:
  - `rtk rg -n "#include <(Foundation|Darwin|CoreFoundation|pthread|mach|sys/sysctl|spawn|crt_externs)>" OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/pipe_poll_probe.c`
  - Result: no matches.

**Current conclusion:**

- Orlix now has focused iOS Simulator proof for Linux pipe creation, nonblocking empty-read errno behavior, `poll` timeout, write readiness, read readiness after write, payload preservation, and hangup readiness through the normal OrlixOS terminal-session path.
- This strengthens the fd readiness proof surface before relying on OCI-derived environments. It does not prove `select`, `epoll`, PTY readiness, socket readiness, signal-interrupted `poll`, nonzero timeout precision, pipe capacity behavior, partial writes, multiple live environments, registry pull, networking, cgroups, live real-Linux oracle comparison, real device execution, App Store acceptance, or arbitrary imported binary compatibility.
- No macOS runtime or macOS user-surface assumption is part of this checkpoint.

## 2026-06-12 Focused pipe select readiness proof on iOS Simulator

**Scope:**

Add a focused Orlix kselftest and OrlixOS terminal-session proof for basic Linux `select` readiness behavior before broadening OCI-derived environment execution. The probe runs inside the upstream-Linux-based OrlixKernel on iOS Simulator and exercises normal Linux userspace-visible behavior:

- `pipe` descriptor creation;
- `fcntl(F_GETFL)` and `fcntl(F_SETFL, O_NONBLOCK)`;
- nonblocking empty pipe `read` returning `EAGAIN`;
- `select` timeout on an empty pipe read end;
- `select` write readiness on the pipe write end;
- `select` read readiness after writing data;
- payload preservation through `write` and `read`;
- read readiness and EOF after the writer closes.

macOS is only the Xcode build and result-inspection host for this checkpoint. The runtime proof target is iOS Simulator.

This does not add Apple container/containerization, Virtualization.framework, Docker daemon behavior, runc, a VM runtime, custom Orlix pipe APIs, custom Orlix `select` APIs, or a macOS runtime target.

**Code changes:**

- `OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/pipe_select_probe.c` adds an Orlix-owned kselftest for pipe creation, nonblocking read errno behavior, `select` readiness, payload readback, and EOF readiness after writer close.
- `OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/Makefile` adds `pipe_select_probe` to `TEST_GEN_PROGS`.
- `OrlixTestRunner/Sources/OrlixUpstreamTestRunner.swift` adds `OrlixUpstreamTestRunSpec.kernelPipeSelect`, selecting `orlix.kselftest=pipe_select_probe`.
- `OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift` adds `testPipeSelectProbeCompletesThroughOrlixOSTerminalSession`, asserting the probe marker and each Linux-visible TAP check.

No OrlixHostAdapter behavior, MLibC behavior, Apple container integration, or Virtualization.framework dependency was added.

**Evidence:**

- Initial sandboxed `xcodebuild` attempt failed before test execution because CoreSimulator and SwiftPM cache access were blocked. The focused proof was rerun with approved simulator access.
- Passing focused pipe/select proof command:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixKernelUpstreamTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixKernelUpstreamTests/OrlixKernelUpstreamTests/testPipeSelectProbeCompletesThroughOrlixOSTerminalSession test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_08-22-04-+0200.xcresult`.
- Exact summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_08-22-04-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 1`
  - `passedTests: 1`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5, arm64.
- Runtime output includes:
  - `Kernel command line: console=ttyS0 console=hvc0 rdinit=/init orlix.root=initramfs-only orlix.profile=release orlix.kselftest=pipe_select_probe`
  - `# exec /orlix/pipe_select_probe`
  - `# child exec /orlix/pipe_select_probe`
  - `ORLIX-PIPE-SELECT-PROBE`
  - `ok 1 - pipe creates nonblocking read descriptor for select`
  - `ok 2 - empty nonblocking pipe read returns EAGAIN before select`
  - `ok 3 - empty pipe read select times out`
  - `ok 4 - pipe write end selects writable`
  - `ok 5 - pipe read end selects readable after write`
  - `ok 6 - pipe read returns selected payload`
  - `ok 7 - pipe read end selects readable after writer closes`
  - `ok 8 - pipe read returns EOF after selected writer close`
  - `ok 5 - pipe_select_probe`
  - `ORLIX-KSELFTEST-END`
- Path-scoped whitespace check:
  - `rtk git diff --check -- OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/pipe_select_probe.c OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/Makefile OrlixTestRunner/Sources/OrlixUpstreamTestRunner.swift OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift`
  - Result: no output.
- Runtime dependency and wording scan:
  - `rtk rg -n "AppleContainer|apple/container|Containerization|Virtualization\\.framework|Virtualization|Process\\(|NSTask|Foundation\\.Process|aligned with macOS|aligned with macos|alligned with macos|alligned with macOS|macOS user|macOS runtime|IXLand|OrlixKit" OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/pipe_select_probe.c OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/Makefile OrlixTestRunner/Sources/OrlixUpstreamTestRunner.swift OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift --glob '!Build/**'`
  - Result: no matches.
- Host-header leakage scan for the new OrlixKernel selftest source:
  - `rtk rg -n "#include <(Foundation|Darwin|CoreFoundation|pthread|mach|sys/sysctl|spawn|crt_externs)>" OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/pipe_select_probe.c`
  - Result: no matches.

**Current conclusion:**

- Orlix now has focused iOS Simulator proof for Linux pipe creation, nonblocking empty-read errno behavior, `select` timeout, write readiness, read readiness after write, payload preservation, and EOF readiness after writer close through the normal OrlixOS terminal-session path.
- This strengthens the fd readiness proof surface before relying on OCI-derived environments. It does not prove `epoll`, PTY readiness, socket readiness, signal-interrupted `select`, nonzero timeout precision, pipe capacity behavior, partial writes, multiple live environments, registry pull, networking, cgroups, live real-Linux oracle comparison, real device execution, App Store acceptance, or arbitrary imported binary compatibility.
- No macOS runtime or macOS user-surface assumption is part of this checkpoint.

## 2026-06-12 Focused pipe epoll readiness proof on iOS Simulator

**Scope:**

Add a focused Orlix kselftest and OrlixOS terminal-session proof for basic Linux `epoll` readiness behavior before broadening OCI-derived environment execution. The probe runs inside the upstream-Linux-based OrlixKernel on iOS Simulator and exercises normal Linux userspace-visible behavior:

- `pipe` descriptor creation;
- `fcntl(F_GETFL)` and `fcntl(F_SETFL, O_NONBLOCK)`;
- nonblocking empty pipe `read` returning `EAGAIN`;
- `epoll_create1`;
- `epoll_ctl` add, modify, and delete;
- empty pipe read-end timeout;
- write readiness on the pipe write end;
- read readiness after writing data;
- payload preservation through `write` and `read`;
- hangup readiness after the writer closes.

macOS is only the Xcode build and result-inspection host for this checkpoint. The runtime proof target is iOS Simulator.

This does not add Apple container/containerization, Virtualization.framework, Docker daemon behavior, runc, a VM runtime, custom Orlix pipe APIs, custom Orlix `epoll` APIs, or a macOS runtime target.

**Code changes:**

- `OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/pipe_epoll_probe.c` adds an Orlix-owned kselftest for pipe creation, nonblocking read errno behavior, `epoll` readiness, payload readback, and hangup readiness after writer close.
- `OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/Makefile` adds `pipe_epoll_probe` to `TEST_GEN_PROGS`.
- `OrlixTestRunner/Sources/OrlixUpstreamTestRunner.swift` adds `OrlixUpstreamTestRunSpec.kernelPipeEpoll`, selecting `orlix.kselftest=pipe_epoll_probe`.
- `OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift` adds `testPipeEpollProbeCompletesThroughOrlixOSTerminalSession`, asserting the probe marker and each Linux-visible TAP check.

No OrlixHostAdapter behavior, MLibC behavior, Apple container integration, or Virtualization.framework dependency was added.

**Evidence:**

- Initial sandboxed `xcodebuild` attempt failed before test execution because CoreSimulator and SwiftPM cache access were blocked. The focused proof was rerun with approved simulator access.
- The first escalated run reached the iOS Simulator runtime but failed because the probe kept the writable pipe write end registered while checking read readiness. Linux may legally report the still-writable write end first, so the probe design was corrected to delete the write end after proving `EPOLLOUT`.
  - Failed result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_08-30-52-+0200.xcresult`.
- Passing focused pipe/epoll proof command:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixKernelUpstreamTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixKernelUpstreamTests/OrlixKernelUpstreamTests/testPipeEpollProbeCompletesThroughOrlixOSTerminalSession test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_08-34-02-+0200.xcresult`.
- Exact summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_08-34-02-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 1`
  - `passedTests: 1`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5, arm64.
- Runtime output includes:
  - `Kernel command line: console=ttyS0 console=hvc0 rdinit=/init orlix.root=initramfs-only orlix.profile=release orlix.kselftest=pipe_epoll_probe`
  - `# exec /orlix/pipe_epoll_probe`
  - `# child exec /orlix/pipe_epoll_probe`
  - `ORLIX-PIPE-EPOLL-PROBE`
  - `ok 1 - pipe creates nonblocking read descriptor for epoll`
  - `ok 2 - epoll_create1 returns epoll descriptor`
  - `ok 3 - empty nonblocking pipe read returns EAGAIN before epoll`
  - `ok 4 - epoll_ctl adds pipe read end`
  - `ok 5 - empty pipe read epoll times out`
  - `ok 6 - pipe write end epolls writable`
  - `ok 7 - pipe read end epolls readable after write`
  - `ok 8 - pipe read returns epoll payload`
  - `ok 9 - pipe read end epolls hangup after writer closes`
  - `ok 5 - pipe_epoll_probe`
  - `ORLIX-KSELFTEST-END`
- Crash-report check:
  - Latest `OrlixTestRunner` diagnostic report remains `/Users/rudironsoni/Library/Logs/DiagnosticReports/OrlixTestRunner-2026-06-12-032807.ips`.
  - No newer `OrlixTestRunner` crash report appeared after this focused proof.
- Path-scoped whitespace check:
  - `rtk git diff --check -- OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/pipe_epoll_probe.c OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/Makefile OrlixTestRunner/Sources/OrlixUpstreamTestRunner.swift OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift`
  - Result: no output.
- Runtime dependency and wording scan:
  - `rtk rg -n "AppleContainer|apple/container|Containerization|Virtualization\\.framework|Virtualization|Process\\(|NSTask|Foundation\\.Process|aligned with macOS|aligned with macos|alligned with macos|alligned with macOS|macOS user|macOS runtime|IXLand|OrlixKit" OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/pipe_epoll_probe.c OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/Makefile OrlixTestRunner/Sources/OrlixUpstreamTestRunner.swift OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift --glob '!Build/**'`
  - Result: no matches.
- Host-header leakage scan for the new OrlixKernel selftest source:
  - `rtk rg -n "#include <(Foundation|Darwin|CoreFoundation|pthread|mach|sys/sysctl|spawn|crt_externs)>" OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/pipe_epoll_probe.c`
  - Result: no matches.

**Current conclusion:**

- Orlix now has focused iOS Simulator proof for Linux `epoll_create1`, `epoll_ctl` add, modify, and delete, empty-pipe timeout, pipe write readiness, pipe read readiness after write, payload preservation, and hangup readiness after writer close through the normal OrlixOS terminal-session path.
- This strengthens the fd readiness proof surface before relying on OCI-derived environments. It does not prove PTY readiness, socket readiness, `epoll_pwait`, signal-interrupted `epoll`, edge-triggered epoll, oneshot behavior, nonzero timeout precision, multiple live environments, registry pull, networking, cgroups, live real-Linux oracle comparison, real device execution, App Store acceptance, or arbitrary imported binary compatibility.
- No macOS runtime or macOS user-surface assumption is part of this checkpoint.

## 2026-06-12 Focused pseudo-filesystem baseline proof on iOS Simulator

**Scope:**

Add a focused Orlix kselftest and OrlixOS terminal-session proof for the minimal Linux pseudo-filesystem shape imported rootfs shells and package scripts expect before OCI-derived environment execution broadens. The probe runs inside the upstream-Linux-based OrlixKernel on iOS Simulator and exercises Linux-visible filesystem behavior:

- `/proc` appears as procfs in `/proc/self/mountinfo`;
- `/sys` appears as sysfs in `/proc/self/mountinfo`;
- `/dev` appears as devtmpfs in `/proc/self/mountinfo`;
- `/dev/pts` appears as devpts in `/proc/self/mountinfo`;
- `/tmp` appears as tmpfs in `/proc/self/mountinfo`;
- `/proc/self/status`, `/proc/self/fd`, and `/proc/mounts` are readable;
- `/dev/null`, `/dev/zero`, `/dev/random`, and `/dev/urandom` are Linux character devices;
- devpts can allocate a PTY master through the normal ptmx path;
- `/sys/bus/virtio/devices` exists for virtio device visibility.

macOS is only the Xcode build and result-inspection host for this checkpoint. The runtime proof target is iOS Simulator.

This does not add Apple container/containerization, Virtualization.framework, Docker daemon behavior, runc, a VM runtime, custom Orlix pseudo-filesystem APIs, or a macOS runtime target.

**Code changes:**

- `OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/kselftest_init.c` now mounts `devpts`, `/dev/shm` tmpfs, `/run` tmpfs, and `/tmp` tmpfs in the app-hosted kselftest initramfs environment, matching the minimal pseudo-filesystem baseline already expected by product `/sbin/init`.
- `OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/pseudo_fs_probe.c` adds an Orlix-owned kselftest for procfs, sysfs, devtmpfs, devpts, tmpfs, device nodes, ptmx allocation, and virtio sysfs directory visibility.
- `OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/Makefile` adds `pseudo_fs_probe` to `TEST_GEN_PROGS`.
- `OrlixTestRunner/Sources/OrlixUpstreamTestRunner.swift` adds `OrlixUpstreamTestRunSpec.kernelPseudoFS`, selecting `orlix.kselftest=pseudo_fs_probe`.
- `OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift` adds `testPseudoFSProbeCompletesThroughOrlixOSTerminalSession`, asserting the probe marker and each Linux-visible TAP check.

No OrlixHostAdapter behavior, MLibC behavior, Apple container integration, or Virtualization.framework dependency was added.

**Evidence:**

- Initial sandboxed `xcodebuild` attempt failed before test execution because CoreSimulator and SwiftPM cache access were blocked. The focused proof was rerun with approved simulator access.
- The first escalated run reached the iOS Simulator runtime and failed because the kselftest initramfs environment mounted procfs, sysfs, and devtmpfs, but did not mount devpts or `/tmp` tmpfs:
  - Failed result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_08-42-47-+0200.xcresult`.
  - Runtime failure lines included:
    - `not ok 4 - mountinfo exposes devpts at /dev/pts`
    - `not ok 5 - mountinfo exposes tmpfs at /tmp`
    - `not ok 8 - devpts mountpoint is a directory`
    - `not ok 9 - devpts allocates a PTY master through /dev/ptmx`
- Passing focused pseudo-filesystem proof command after fixing `kselftest_init.c`:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixKernelUpstreamTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixKernelUpstreamTests/OrlixKernelUpstreamTests/testPseudoFSProbeCompletesThroughOrlixOSTerminalSession test`
- Result:
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_08-47-05-+0200.xcresult`.
- Exact summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_08-47-05-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 1`
  - `passedTests: 1`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5, arm64.
- Runtime output includes kselftest-init setup:
  - `Kernel command line: console=ttyS0 console=hvc0 rdinit=/init orlix.root=initramfs-only orlix.profile=release orlix.kselftest=pseudo_fs_probe`
  - `ok 1 - procfs mounted for kselftest`
  - `ok 2 - sysfs mounted for kselftest`
  - `ok 3 - devtmpfs mounted for kselftest`
  - `ok 4 - devpts mounted for kselftest`
  - `ok 5 - dev shm tmpfs mounted for kselftest`
  - `ok 6 - run tmpfs mounted for kselftest`
  - `ok 7 - tmpfs mounted at /tmp for kselftest`
  - `ok 8 - installed Orlix kselftest list is readable`
- Runtime output includes the focused probe:
  - `# exec /orlix/pseudo_fs_probe`
  - `# child exec /orlix/pseudo_fs_probe`
  - `ORLIX-PSEUDO-FS-PROBE`
  - `ok 1 - mountinfo exposes procfs at /proc`
  - `ok 2 - mountinfo exposes sysfs at /sys`
  - `ok 3 - mountinfo exposes devtmpfs at /dev`
  - `ok 4 - mountinfo exposes devpts at /dev/pts`
  - `ok 5 - mountinfo exposes tmpfs at /tmp`
  - `ok 6 - proc self status fd and mounts are readable`
  - `ok 7 - core dev nodes are Linux character devices`
  - `ok 8 - devpts mountpoint is a directory`
  - `ok 9 - devpts allocates a PTY master through ptmx`
  - `ok 10 - sysfs exposes virtio device directory`
  - `ok 9 - pseudo_fs_probe`
  - `ORLIX-KSELFTEST-END`
- Full Orlix kernel kselftest gate after changing `kselftest_init.c`:
  - `rtk timeout 1200 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixKernelUpstreamTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixKernelUpstreamTests/OrlixKernelUpstreamTests/testKselftestRootfsCompletesThroughOrlixOSTerminalSession test`
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_08-52-01-+0200.xcresult`.
- Exact full-gate summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_08-52-01-+0200.xcresult`
- Full-gate result:
  - `result: Passed`
  - `totalTestCount: 1`
  - `passedTests: 1`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5, arm64.
- Full-gate runtime output includes:
  - `1..26`
  - `ok 1 - procfs mounted for kselftest`
  - `ok 4 - devpts mounted for kselftest`
  - `ok 7 - tmpfs mounted at /tmp for kselftest`
  - `ok 9 - boot_profile_contract`
  - `ok 24 - tls_syscall_probe`
  - `ok 25 - virtio_blk_environment_probe`
  - `ok 26 - virtio_mmio_probe_contract`
  - `ORLIX-KSELFTEST-END`
- Crash-report check:
  - Latest `OrlixTestRunner` diagnostic report remains `/Users/rudironsoni/Library/Logs/DiagnosticReports/OrlixTestRunner-2026-06-12-032807.ips`.
  - No newer `OrlixTestRunner` crash report appeared after the focused proof or the full kselftest gate.
- Path-scoped whitespace check:
  - `rtk git diff --check -- OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/kselftest_init.c OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/pseudo_fs_probe.c OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/Makefile OrlixTestRunner/Sources/OrlixUpstreamTestRunner.swift OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift`
  - Result: no output.
- Runtime dependency and wording scan:
  - `rtk rg -n "AppleContainer|apple/container|Containerization|Virtualization\\.framework|Virtualization|Process\\(|NSTask|Foundation\\.Process|aligned with macOS|aligned with macos|alligned with macos|alligned with macOS|macOS user|macOS runtime|IXLand|OrlixKit" OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/kselftest_init.c OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/pseudo_fs_probe.c OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/Makefile OrlixTestRunner/Sources/OrlixUpstreamTestRunner.swift OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift --glob '!Build/**'`
  - Result: no matches.
- Host-header leakage scan for the touched OrlixKernel selftest sources:
  - `rtk rg -n "#include <(Foundation|Darwin|CoreFoundation|pthread|mach|sys/sysctl|spawn|crt_externs)>" OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/kselftest_init.c OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/pseudo_fs_probe.c`
  - Result: no matches.

**Current conclusion:**

- Orlix now has focused iOS Simulator proof that the app-hosted Linux kselftest environment exposes procfs, sysfs, devtmpfs, devpts, `/tmp` tmpfs, readable proc self state, core Linux device nodes, ptmx-backed PTY allocation, and virtio sysfs device directory visibility through the normal OrlixOS terminal-session path.
- This strengthens the pseudo-filesystem baseline needed before relying on OCI-derived and tar-imported rootfs shells. It does not prove namespace-local `/proc`, PID namespace translation, cgroup v2 shape, `/proc/net`, imported rootfs package-manager expectations, multiple live environments, registry pull, networking, live real-Linux oracle comparison, real device execution, App Store acceptance, or arbitrary imported binary compatibility.
- No macOS runtime or macOS user-surface assumption is part of this checkpoint.

### 2026-06-12 OCI layout import refuses overwrite before source read

Moved OCI image layout import destination validation ahead of source layout parsing. Existing environment roots now fail with `destinationExists` before `OrlixOCIImageLayoutReader` reads `oci-layout`, `index.json`, config blobs, or layer blobs. This matches the file-backed tar import atomicity rule: destination protection is checked before touching potentially missing or malformed source input.

This is OrlixOS image-import policy only. It does not change OrlixKernel Linux semantics, does not add registry pull, does not add Apple container/containerization, does not add Virtualization.framework, and does not create Docker daemon, runc, VM, or macOS runtime behavior. The proof target remains iOS Simulator only. macOS is the Xcode build and result-inspection host for this checkpoint.

Changed files:

- `OrlixOS/Sources/Session/OrlixOCIImageLayout.swift`
- `OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`

Verification:

- Initial sandboxed focused iOS Simulator proof command:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRefusesExistingEnvironmentBeforeReadingLayout -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterDoesNotOverwriteExistingEnvironment test`
  - Result: failed before build/test because sandbox denied CoreSimulator and SwiftPM cache access.
- Elevated focused iOS Simulator proof command:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterRefusesExistingEnvironmentBeforeReadingLayout -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testOCIImageLayoutImporterDoesNotOverwriteExistingEnvironment test`
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_09-12-43-+0200.xcresult`.
- Exact result summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_09-12-43-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 2`
  - `passedTests: 2`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5, arm64.
- Path-scoped whitespace check:
  - `rtk git diff --check -- OrlixOS/Sources/Session/OrlixOCIImageLayout.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
  - Result: no output.
- Runtime dependency and wording scan:
  - `rtk rg -n "AppleContainer|apple/container|Containerization|Virtualization\\.framework|Virtualization|Process\\(|NSTask|Foundation\\.Process|aligned with macOS|aligned with macos|alligned with macos|alligned with macOS|macOS user|macOS runtime|IXLand|OrlixKit" OrlixOS/Sources/Session/OrlixOCIImageLayout.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift --glob '!Build/**'`
  - Result: no matches.
- Crash-report check:
  - Latest `OrlixTestRunner` diagnostic report remains `/Users/rudironsoni/Library/Logs/DiagnosticReports/OrlixTestRunner-2026-06-12-032807.ips`.
  - No newer `OrlixTestRunner` crash report appeared after the 09:13 focused proof.

Current conclusion:

- OCI layout import no longer allows malformed or missing source layout errors to mask an existing destination environment.
- Existing environment descriptor, base image, and state image preservation are covered by the new missing-layout regression and the existing valid-layout overwrite regression.
- This does not prove OCI registry pull, overlay snapshots, package-manager execution in imported images, arbitrary imported binary compatibility, namespace isolation, networking, cgroups, real device execution, or App Store acceptance.

### 2026-06-12 Path errno oracle path verified

Validated the first real-Linux oracle workflow for deterministic Linux path-resolution errno behavior. This checkpoint does not change product code. It proves the Mac-only oracle tool can validate the case definition, compare matching captured Linux and Orlix results, reject a drifted Orlix result, convert an Orlix kselftest log into oracle JSON, and compare that converted result against the Linux sample. It also refreshes the matching app-hosted iOS Simulator Orlix proof for the same `path_errno_probe`.

This is proof infrastructure and upstream-style OrlixKernel behavior evidence. The oracle runner remains tooling only. Its `Process` use is confined to `tools/orlix-linux-oracle/orlix-linux-oracle.swift` for external Linux fixture execution and is not part of OrlixKernel, OrlixOS, OrlixHostAdapter, or the iOS runtime. macOS is only the development host for running the comparator and inspecting results. The runtime proof target remains iOS Simulator.

Commands and results:

- Case validation:
  - `rtk swift tools/orlix-linux-oracle/orlix-linux-oracle.swift validate-case tools/orlix-linux-oracle/cases/path-errno.json`
  - Result: `case path-errno is valid`.
- Matching sample comparison:
  - `rtk swift tools/orlix-linux-oracle/orlix-linux-oracle.swift compare --case tools/orlix-linux-oracle/cases/path-errno.json --linux-result tools/orlix-linux-oracle/samples/path-errno.linux.json --orlix-result tools/orlix-linux-oracle/samples/path-errno.orlix.json`
  - Result: `case path-errno matches`.
- Drift sample comparison:
  - `rtk swift tools/orlix-linux-oracle/orlix-linux-oracle.swift compare --case tools/orlix-linux-oracle/cases/path-errno.json --linux-result tools/orlix-linux-oracle/samples/path-errno.linux.json --orlix-result tools/orlix-linux-oracle/samples/path-errno.orlix-drift.json`
  - Result: exited 2 with expected mismatches: stdout, exitStatus, errnoEvents, and mutations.
- Orlix log conversion:
  - `rtk swift tools/orlix-linux-oracle/orlix-linux-oracle.swift orlix-result-from-log --case tools/orlix-linux-oracle/cases/path-errno.json --log tools/orlix-linux-oracle/samples/path-errno.orlix-kselftest.log --output /tmp/path-errno.orlix.converted.json`
  - Result: wrote `/tmp/path-errno.orlix.converted.json`.
- Converted Orlix result comparison:
  - `rtk swift tools/orlix-linux-oracle/orlix-linux-oracle.swift compare --case tools/orlix-linux-oracle/cases/path-errno.json --linux-result tools/orlix-linux-oracle/samples/path-errno.linux.json --orlix-result /tmp/path-errno.orlix.converted.json`
  - Result: `case path-errno matches`.
- Linux fixture runner boundary check on macOS:
  - `rtk swift tools/orlix-linux-oracle/orlix-linux-oracle.swift linux-result-from-fixture --case tools/orlix-linux-oracle/cases/path-errno.json --fixture /oracle/path_errno_probe --workdir /oracle/work --output /tmp/path-errno.linux.current.json`
  - Result: exited 2 with `unsupported host: linux-result-from-fixture must run inside a real Linux environment`.
- Initial sandboxed iOS Simulator proof command:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixKernelUpstreamTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixKernelUpstreamTests/OrlixKernelUpstreamTests/testPathErrnoProbeCompletesThroughOrlixOSTerminalSession test`
  - Result: failed before build/test because sandbox denied CoreSimulator and SwiftPM cache access.
- Elevated focused iOS Simulator proof command:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixKernelUpstreamTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixKernelUpstreamTests/OrlixKernelUpstreamTests/testPathErrnoProbeCompletesThroughOrlixOSTerminalSession test`
  - Completed with exit code 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_09-17-52-+0200.xcresult`.
- Exact result summary command:
  - `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_09-17-52-+0200.xcresult`
- Result:
  - `result: Passed`
  - `totalTestCount: 1`
  - `passedTests: 1`
  - `failedTests: 0`
  - `skippedTests: 0`
  - Device platform: iOS Simulator, iPhone 17 Pro, iOS 26.5, arm64.
- Runtime output includes:
  - `Kernel command line: console=ttyS0 console=hvc0 rdinit=/init orlix.root=initramfs-only orlix.profile=release orlix.kselftest=path_errno_probe`
  - `# exec /orlix/path_errno_probe`
  - `# child exec /orlix/path_errno_probe`
  - `ok 1 - path errno fixture created through Linux VFS`
  - `ORLIX-ORACLE-BEGIN path-errno`
  - `{"operation":"open","path":"missing","errno":2,"name":"No such file or directory","expected":2}`
  - `ok 2 - missing path returns ENOENT`
  - `{"operation":"open","path":"regular/child","errno":20,"name":"Not a directory","expected":20}`
  - `ok 3 - non-directory child returns ENOTDIR`
  - `{"operation":"stat","path":"loop-a","errno":40,"name":"Too many levels of symbolic links","expected":40}`
  - `ok 4 - symlink loop returns ELOOP`
  - `{"operation":"stat","path":"regular/","errno":20,"name":"Not a directory","expected":20}`
  - `ok 5 - trailing slash on regular file returns ENOTDIR`
  - `ORLIX-ORACLE-END path-errno`
  - `ok 6 - path errno fixture cleaned`
  - `ok 9 - path_errno_probe`
  - `ORLIX-KSELFTEST-END`
- Path-scoped whitespace check:
  - `rtk git diff --check -- tools/orlix-linux-oracle docs/plans/active/oci-derived-environments-virtio-plane/IMPLEMENT.md OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/path_errno_probe.c OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift`
  - Result: no output.
- Runtime dependency and wording scan:
  - `rtk rg -n "AppleContainer|apple/container|Containerization|Virtualization\\.framework|Virtualization|Process\\(|NSTask|Foundation\\.Process|aligned with macOS|aligned with macos|alligned with macos|alligned with macOS|macOS user|macOS runtime|IXLand|OrlixKit" tools/orlix-linux-oracle OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/path_errno_probe.c OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift --glob '!Build/**'`
  - Result: expected Mac-only tooling matches in `tools/orlix-linux-oracle`: README guardrail references to Apple containerization and Virtualization.framework, and `Process()` in `orlix-linux-oracle.swift`. No runtime-code matches were found in the checked OrlixKernel selftest or XCTest files.
- Crash-report check:
  - Latest `OrlixTestRunner` diagnostic report remains `/Users/rudironsoni/Library/Logs/DiagnosticReports/OrlixTestRunner-2026-06-12-032807.ips`.
  - No newer `OrlixTestRunner` crash report appeared after the 09:18 focused proof.

Current conclusion:

- The path/errno oracle case now has live tool verification plus current app-hosted iOS Simulator Orlix proof.
- The external Linux result generation command correctly refuses to run on macOS; a fresh Linux result still requires a real Linux runner, such as Linux CI, a developer VM, or a future external Apple-container-based oracle runner.
- This does not prove live real-Linux comparison from a freshly generated Linux result, broad path-resolution conformance, imported-root package-manager behavior, networking, cgroups, real device execution, App Store acceptance, or arbitrary imported binary compatibility.

## Resolved Questions

| Question | Answer | Date |
|---|---|---|
| | | |

### 2026-06-12 Descriptor command names resolve through Linux PATH on iOS Simulator

Implemented descriptor command-name execution for OCI-derived and named-environment descriptors that specify a bare executable name, such as `sh`, instead of an absolute path. This keeps image config closer to how container images are authored while preserving the Linux-shaped execution boundary: OrlixOS prepares the descriptor and `/init` calls normal Linux `execve`; OrlixKernel still owns Linux exec behavior.

Changed files:

- `OrlixOS/Sources/Session/OrlixEnvironment.swift`
  - `validateExecCommand` now accepts absolute paths and bare command names.
  - Relative paths containing a slash, such as `bin/sh`, remain rejected.
- `OrlixOS/Sources/init/init.c`
  - `valid_exec_path` now accepts absolute paths and bare command names while rejecting relative paths with slashes.
  - Added PATH lookup in `/init` before command execution.
  - PATH comes from the descriptor environment, with `/bin:/usr/bin` fallback when PATH is absent.
  - Each candidate is still executed through `execve`.
- `OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
  - Added descriptor encoding coverage for `defaultCommand: ["sh", "-c", ...]`.
  - Existing unsafe relative path rejection remains covered.
- `OrlixTestRunner/Tests/XCTest/OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests.swift`
  - Added an OCI-derived materialized-root runtime proof where the descriptor uses bare `sh`.
  - The proof verifies shell argv propagation, descriptor environment propagation, canonical cwd behavior, procfs visibility through `/proc/self/status`, and completion marker output.

Boundary:

- No OrlixKernel behavior was changed.
- No OrlixHostAdapter behavior was changed.
- No MLibC behavior was changed.
- No Apple container/containerization, Virtualization.framework, Docker daemon, runc, VM lifecycle, or registry pull dependency was added.
- The proof target is iOS Simulator only. macOS is only the development host for Xcode, fixture generation, and result-bundle inspection.

Commands and results:

- Initial sandboxed focused OrlixOS descriptor test:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentRootImageEncodesPathLookupCommandName -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentRootImageRejectsUnsafeDefaultCommandExecutable test`
  - Result: failed before tests because the sandbox denied CoreSimulator and SwiftPM cache access.
- Elevated focused OrlixOS descriptor test:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentRootImageEncodesPathLookupCommandName -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentRootImageRejectsUnsafeDefaultCommandExecutable test`
  - Result: exited 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_09-23-59-+0200.xcresult`.
  - Output: 2 tests executed, 0 failures.
- Runtime fixture rebuild:
  - `rtk timeout 1200 make -f OrlixOS/Makefile environment-runtime-proof-fixtures PROFILE=release`
  - Result: exited 0.
  - Output includes regenerated tar-imported and OCI-imported runtime proof images under `Build/OrlixOS/environment-runtime-proof/`.
- Initial sandboxed focused runtime proof:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests/testOCIDerivedMaterializedRootResolvesDescriptorCommandThroughPath test`
  - Result: failed before tests because the sandbox denied CoreSimulator and SwiftPM cache access.
- Elevated focused iOS Simulator runtime proof:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests/testOCIDerivedMaterializedRootResolvesDescriptorCommandThroughPath test`
  - Result: exited 0.
  - Result bundle: `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.12_09-31-19-+0200.xcresult`.
  - Output: 1 test executed, 0 failures.

Runtime evidence from the passing iOS Simulator proof:

- Kernel command line includes:
  - `orlix.root=overlay`
  - `orlix.exec=sh`
  - `orlix.argv0=sh`
  - `orlix.argv3=path-lookup-argv0`
  - `orlix.argv4=argument%20after%20path%20lookup`
  - `orlix.env2=PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin`
  - `orlix.cwd=/tmp/..`
- Runtime output includes:
  - `ORLIX_ENV_EXEC_BEGIN`
  - `argv0=path-lookup-argv0`
  - `argv1=argument after path lookup`
  - `env=descriptor value with spaces`
  - `pwd=/`
  - `Name:	cat`
  - `Uid:	0	0	0	0`
  - `Gid:	0	0	0	0`
  - `ORLIX_ENV_EXEC_DONE`

Static checks:

- `rtk git diff --check -- OrlixOS/Sources/init/init.c OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift OrlixTestRunner/Tests/XCTest/OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests.swift docs/plans/active/oci-derived-environments-virtio-plane/IMPLEMENT.md`
  - Result: no output.
- `rtk rg -n "AppleContainer|apple/container|Containerization|Virtualization\\.framework|Virtualization|Process\\(|NSTask|Foundation\\.Process|aligned with macOS|aligned with macos|alligned with macos|alligned with macOS|macOS user|macOS runtime|IXLand|OrlixKit" OrlixOS/Sources/init/init.c OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift OrlixTestRunner/Tests/XCTest/OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests.swift docs/plans/active/oci-derived-environments-virtio-plane/IMPLEMENT.md --glob '!Build/**'`
  - Result: source files had no matches. Active implementation log had expected historical guardrail matches.
- `rtk rg -n "#include <(Foundation|Darwin|CoreFoundation|pthread|mach|sys/sysctl|spawn|crt_externs)>" OrlixOS/Sources/init/init.c`
  - Result: no output.
- Crash report check:
  - `rtk ls -t /Users/rudironsoni/Library/Logs/DiagnosticReports/OrlixTestRunner-*.ips`
  - Latest report remains `/Users/rudironsoni/Library/Logs/DiagnosticReports/OrlixTestRunner-2026-06-12-032807.ips`.
  - No newer `OrlixTestRunner` crash report appeared after the 09:32 focused runtime proof.

Current conclusion:

- OCI-derived environment descriptors can now keep bare command names such as `sh`, and the iOS Simulator runtime proof shows `/init` resolving that command through descriptor PATH before Linux `execve`.
- This removes one unnecessary divergence from normal OCI image config shape without adding Docker/runc semantics.
- This does not prove registry pull, broad imported-image compatibility, dynamic loader completeness for arbitrary images, namespace completeness, package-manager behavior, networking, cgroups, real device execution, or App Store acceptance.

### 2026-06-12 Descriptor command names resolve through fallback PATH on iOS Simulator

Added a focused app-hosted iOS Simulator runtime proof for OCI-derived
descriptor command resolution when the descriptor environment does not provide
`PATH`. This keeps the product behavior aligned with ordinary Linux command
execution: a bare command name such as `sh` is resolved by `/init` using the
fallback `/bin:/usr/bin`, then executed through `execve`.

This is a test-only checkpoint over the existing `/init` PATH fallback behavior.
No OrlixKernel, OrlixHostAdapter, OrlixMLibC, Apple container/containerization,
Virtualization.framework, Docker daemon, runc, VM lifecycle, or registry-pull
behavior was changed. The proof target is iOS Simulator only. macOS is only the
development host for Xcode and result-bundle inspection.

Changes:

- `OrlixTestRunner/Tests/XCTest/OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests.swift`
  - Added
    `testOCIDerivedMaterializedRootResolvesDescriptorCommandThroughFallbackPath`.
  - Added `pathLookupWithoutPATHDescriptorExecution` runtime proof mode.
  - The proof descriptor uses bare `sh`, passes shell argv through the
    descriptor command, intentionally omits `PATH` from descriptor environment,
    starts from `/tmp/..`, and expects canonical `pwd=/`.

Commands and results:

- Whitespace check:
  - `rtk git diff --check -- OrlixTestRunner/Tests/XCTest/OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests.swift`
  - Result: exited 0 with no output.
- Focused source check:
  - `rtk rg -n "pathLookupWithoutPATHDescriptorExecution|FallbackPath|descriptor value without path|path-fallback" OrlixTestRunner/Tests/XCTest/OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests.swift`
  - Result: expected new test/proof references were present.
- Initial sandboxed focused iOS Simulator runtime proof:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests/testOCIDerivedMaterializedRootResolvesDescriptorCommandThroughFallbackPath test`
  - Result: failed before tests because the sandbox denied CoreSimulator and
    SwiftPM cache access.
- Elevated focused iOS Simulator runtime proof:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests/testOCIDerivedMaterializedRootResolvesDescriptorCommandThroughFallbackPath test`
  - Result: exited 0.
  - Result bundle:
    `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.12_09-37-45-+0200.xcresult`.
  - Output: 1 test executed, 0 failures.

Runtime evidence from the passing iOS Simulator proof:

- Kernel command line includes:
  - `orlix.root=overlay`
  - `orlix.exec=sh`
  - `orlix.argv0=sh`
  - `orlix.argv3=path-fallback-argv0`
  - `orlix.argv4=argument%20after%20fallback%20path%20lookup`
  - `orlix.env0=HOME=/root`
  - `orlix.env1=ORLIX_DESCRIPTOR_MESSAGE=descriptor%20value%20without%20path`
  - `orlix.env2=TERM=xterm-256color`
  - `orlix.cwd=/tmp/..`
- Kernel command line does not include a descriptor `PATH=` environment token.
- Runtime output includes:
  - `ORLIX_ENV_EXEC_BEGIN`
  - `argv0=path-fallback-argv0`
  - `argv1=argument after fallback path lookup`
  - `argv2=`
  - `env=descriptor value without path`
  - `pwd=/`
  - `Name:	cat`
  - `Uid:	0	0	0	0`
  - `Gid:	0	0	0	0`
  - `ORLIX_ENV_EXEC_DONE`

Static checks:

- `rtk git diff --check -- OrlixTestRunner/Tests/XCTest/OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests.swift docs/plans/active/oci-derived-environments-virtio-plane/IMPLEMENT.md`
  - Result: no output.
- `rtk rg -n "AppleContainer|apple/container|Containerization|Virtualization\\.framework|Virtualization|Process\\(|NSTask|Foundation\\.Process|aligned with macOS|aligned with macos|alligned with macos|alligned with macOS|macOS user|macOS runtime|IXLand|OrlixKit" OrlixTestRunner/Tests/XCTest/OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests.swift --glob '!Build/**'`
  - Result: no matches.
- Crash report check:
  - `rtk ls -t /Users/rudironsoni/Library/Logs/DiagnosticReports/OrlixTestRunner-*.ips`
  - Latest report remains
    `/Users/rudironsoni/Library/Logs/DiagnosticReports/OrlixTestRunner-2026-06-12-032807.ips`.
  - No newer `OrlixTestRunner` crash report appeared after the 09:38 focused
    runtime proof.

Residual evidence:

- After the test passed, the simulator log emitted repeated block-device write
  warnings:
  - `operation not supported error, dev vdb, sector ... op 0x1:(WRITE) flags 0x4800 ...`
- This checkpoint does not resolve those write warnings. They need separate
  triage before claiming the environment image write path is clean under this
  runtime proof.

Current conclusion:

- OCI-derived environment descriptors can omit `PATH` and still execute bare
  command names through the `/bin:/usr/bin` fallback on iOS Simulator.
- This removes another avoidable descriptor-shape requirement without changing
  the Linux public surface or adding a non-Linux runtime path.
- This does not prove registry pull, broad imported-image compatibility,
  dynamic loader completeness for arbitrary images, namespace completeness,
  package-manager behavior, networking, cgroups, real device execution, clean
  writable-block behavior, or App Store acceptance.

### 2026-06-12 virtio-blk state flush support on iOS Simulator

Goal:

Remove the residual writable state-block warning seen after the OCI-derived
materialized-root fallback PATH proof:

- `operation not supported error, dev vdb, sector 790 op 0x1:(WRITE) flags 0x4800`
- repeated for adjacent sectors down to sector 718.

The earlier proof passed the Linux userspace behavior assertion, but the
post-test kernel log showed the writable `vdb` path was still rejecting a
write-class block operation. Treating that as clean would have been wrong.

Repository changes:

- `OrlixKernel/Sources/ports/orlix/overlay/drivers/orlix/virtio/mmio.c`
  - Advertise upstream `VIRTIO_BLK_F_FLUSH` on the Orlix virtio-mmio block
    devices.
  - Handle `VIRTIO_BLK_T_FLUSH` by calling the host-declared block flush seam.
  - Preserve Linux virtio-blk ownership in OrlixKernel. The host only receives
    a concrete flush request for the private backing file.
- `OrlixKernel/Sources/ports/orlix/overlay/arch/orlix/include/internal/asm/host_block.h`
  - Declare `orlix_host_block_flush(unsigned int device)`.
- `OrlixHostAdapter/Sources/OrlixHostAdapter/boot/resources.h`
  - Declare the hidden host flush primitive for OrlixHostAdapter internals.
- `OrlixHostAdapter/Sources/OrlixHostAdapter/boot/resources.c`
  - Implement `orlix_host_block_flush` by opening the selected private block
    backing file and calling `fsync`.
  - This is host-file durability plumbing only. It does not decide Linux block,
    filesystem, mount, or errno policy.
- `OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/virtio_blk_environment_probe.c`
  - Extend the probe plan from `1..10` to `1..11`.
  - Add `/dev/vdb flushes after sector writes`, which writes back the existing
    sector-0 bytes and requires `fsync` to succeed.
- `OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift`
  - Assert that the app-hosted upstream-test output contains
    `/dev/vdb flushes after sector writes`.

Boundary:

- OrlixKernel owns Linux-visible virtio-blk semantics.
- OrlixHostAdapter only performs private iOS Simulator host-file mechanics for
  the already selected block backing file.
- No Apple container/containerization, Virtualization.framework, Docker daemon,
  runc, VM lifecycle, registry pull, macOS runtime target, or macOS user surface
  was added.
- The proof target remains iOS Simulator only. macOS is only the Xcode build,
  simulator-control, and result-inspection host.

Commands and results:

- Whitespace check:
  - `rtk git diff --check -- OrlixKernel/Sources/ports/orlix/overlay/arch/orlix/include/internal/asm/host_block.h OrlixHostAdapter/Sources/OrlixHostAdapter/boot/resources.h OrlixHostAdapter/Sources/OrlixHostAdapter/boot/resources.c OrlixKernel/Sources/ports/orlix/overlay/drivers/orlix/virtio/mmio.c OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/virtio_blk_environment_probe.c OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift OrlixTestRunner/Tests/XCTest/OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests.swift docs/plans/active/oci-derived-environments-virtio-plane/IMPLEMENT.md`
  - Result: exited 0 with no output.
- Correct focused iOS Simulator kselftest proof:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixKernelUpstreamTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixKernelUpstreamTests/OrlixKernelUpstreamTests/testVirtioBlockEnvironmentProbeCompletesThroughOrlixOSTerminalSession test`
  - Result: exited 0.
  - Result bundle:
    `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_09-50-09-+0200.xcresult`.
  - Output: 1 XCTest executed, 0 failures, `** TEST SUCCEEDED **`.
  - Probe output included:
    - `1..11`
    - `ok 10 - /dev/vdb accepts sector writes`
    - `ok 11 - /dev/vdb flushes after sector writes`
    - `ok 9 - virtio_blk_environment_probe`
    - `ORLIX-KSELFTEST-END`
- Focused iOS Simulator OCI-derived fallback PATH runtime proof:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests/testOCIDerivedMaterializedRootResolvesDescriptorCommandThroughFallbackPath test`
  - Result: exited 0.
  - Result bundle:
    `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.12_09-53-59-+0200.xcresult`.
  - Output: 1 XCTest executed, 0 failures, `** TEST SUCCEEDED **`.
  - Output included:
    - `EXT4-fs (vdb): mounted filesystem ... r/w with ordered data mode`
    - `ORLIX-ROOT-OVERLAY-READY`
    - `ORLIX_ENV_EXEC_BEGIN`
    - `argv0=path-fallback-argv0`
    - `env=descriptor value without path`
    - `pwd=/`
    - `Name:	cat`
    - `ORLIX_ENV_EXEC_DONE`

Static checks:

- Forbidden runtime dependency and wording scan:
  - `rtk rg -n "AppleContainer|apple/container|Containerization|Virtualization\\.framework|Virtualization|Process\\(|NSTask|Foundation\\.Process|aligned with macOS|aligned with macos|alligned with macos|alligned with macOS|macOS user|macOS runtime|IXLand|OrlixKit" OrlixKernel/Sources/ports/orlix/overlay/arch/orlix/include/internal/asm/host_block.h OrlixHostAdapter/Sources/OrlixHostAdapter/boot/resources.h OrlixHostAdapter/Sources/OrlixHostAdapter/boot/resources.c OrlixKernel/Sources/ports/orlix/overlay/drivers/orlix/virtio/mmio.c OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/virtio_blk_environment_probe.c OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift OrlixTestRunner/Tests/XCTest/OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests.swift docs/plans/active/oci-derived-environments-virtio-plane/IMPLEMENT.md --glob '!Build/**'`
  - Result: matches were confined to the active implementation log's historical
    guardrail text and command records. No touched runtime source file matched.
- Crash report check:
  - `rtk ls -t /Users/rudironsoni/Library/Logs/DiagnosticReports/OrlixTestRunner-*.ips`
  - Latest report remains
    `/Users/rudironsoni/Library/Logs/DiagnosticReports/OrlixTestRunner-2026-06-12-032807.ips`.
  - No newer `OrlixTestRunner` crash report appeared after the 09:50 and 09:53
    focused iOS Simulator proofs.

Important non-proof:

- An earlier selector typo used:
  - `-only-testing:OrlixKernelUpstreamTests/OrlixKernelUpstreamTests/testVirtioBlkEnvironmentProbeCompletesThroughOrlixOSTerminalSession`
  - Result: 0 tests ran.
  - This is not proof and must not be cited as evidence.

Residual evidence:

- The prior `operation not supported error, dev vdb` warning did not appear in
  the current focused fallback PATH runtime output after the flush change.
- This proves only the focused iOS Simulator path checked here. It does not
  prove broad filesystem durability behavior, all virtio-blk request types,
  registry pull, arbitrary image compatibility, package-manager behavior,
  networking, cgroups, namespace completeness, or App Store acceptance.

### 2026-06-12 virtio-blk sysfs identifier proof on iOS Simulator

Goal:

Strengthen Phase 7 virtio-blk proof by checking that Orlix's two block devices
are not only readable, writable where appropriate, and flushable, but also
observable through Linux's normal block sysfs identity surface.

Repository changes:

- `OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/virtio_blk_environment_probe.c`
  - Extends the probe plan from `1..11` to `1..13`.
  - Adds checks that `/sys/block/vda/serial` starts with
    `orlix-base-block0`.
  - Adds checks that `/sys/block/vdb/serial` starts with
    `orlix-state-block1`.
  - Enlarges the selftest read buffer from 16 bytes to 64 bytes. The first
    attempted proof failed because the test buffer could not hold the expected
    identifier prefix, not because the backend lacked the value.
- `OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift`
  - Adds assertions for the existing size markers.
  - Adds assertions for both new sysfs serial identifier markers.

Boundary:

- This is OrlixKernel kselftest coverage and app-hosted iOS Simulator proof
  over upstream Linux virtio-blk behavior.
- No OrlixKernel runtime dependency on Apple container/containerization,
  Virtualization.framework, Docker daemon, runc, VM lifecycle, registry pull,
  macOS runtime target, or macOS user surface was added.
- The proof target remains iOS Simulator only. macOS is only the Xcode build,
  simulator-control, and result-inspection host.

Commands and results:

- Static check:
  - `rtk git diff --check -- OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/virtio_blk_environment_probe.c OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift`
  - Result: exited 0 with no output.
- First focused iOS Simulator proof attempt:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixKernelUpstreamTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixKernelUpstreamTests/OrlixKernelUpstreamTests/testVirtioBlockEnvironmentProbeCompletesThroughOrlixOSTerminalSession test`
  - Result: failed.
  - Result bundle:
    `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_09-58-43-+0200.xcresult`.
  - Failure output:
    - `not ok 7 - sysfs exposes /dev/vda virtio block identifier`
    - `not ok 8 - sysfs exposes /dev/vdb virtio block identifier`
  - Root cause: the selftest helper used a 16-byte read buffer, shorter than
    `orlix-base-block0`. The proof was invalid because the helper required
    `size >= strlen(expected)`.
- Corrected focused iOS Simulator proof:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixKernelUpstreamTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixKernelUpstreamTests/OrlixKernelUpstreamTests/testVirtioBlockEnvironmentProbeCompletesThroughOrlixOSTerminalSession test`
  - Result: exited 0.
  - Result bundle:
    `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_10-01-52-+0200.xcresult`.
  - Output: 1 XCTest executed, 0 failures, `** TEST SUCCEEDED **`.
  - Probe output included:
    - `1..13`
    - `ok 5 - sysfs reports nonzero /dev/vda size`
    - `ok 6 - sysfs reports nonzero /dev/vdb size`
    - `ok 7 - sysfs exposes /dev/vda virtio block identifier`
    - `ok 8 - sysfs exposes /dev/vdb virtio block identifier`
    - `ok 13 - /dev/vdb flushes after sector writes`
    - `ok 9 - virtio_blk_environment_probe`
    - `ORLIX-KSELFTEST-END`

Final checks:

- Whitespace:
  - `rtk git diff --check -- OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/virtio_blk_environment_probe.c OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift docs/plans/active/oci-derived-environments-virtio-plane/IMPLEMENT.md`
  - Result: exited 0 with no output.
- Forbidden runtime dependency and wording scan:
  - `rtk rg -n "AppleContainer|apple/container|Containerization|Virtualization\\.framework|Virtualization|Process\\(|NSTask|Foundation\\.Process|aligned with macOS|aligned with macos|alligned with macos|alligned with macOS|macOS user|macOS runtime|IXLand|OrlixKit" OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/virtio_blk_environment_probe.c OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift --glob '!Build/**'`
  - Result: no matches.
- Forbidden host include scan:
  - `rtk rg -n "#include <(Foundation|Darwin|CoreFoundation|pthread|mach|sys/sysctl|spawn|crt_externs)>" OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/virtio_blk_environment_probe.c`
  - Result: no matches.
- Crash report check:
  - `rtk ls -t /Users/rudironsoni/Library/Logs/DiagnosticReports/OrlixTestRunner-*.ips`
  - Latest report remains
    `/Users/rudironsoni/Library/Logs/DiagnosticReports/OrlixTestRunner-2026-06-12-032807.ips`.
  - No newer `OrlixTestRunner` crash report appeared after the 10:01 focused
    iOS Simulator proof.

Current conclusion:

- The app-hosted iOS Simulator virtio-blk proof now covers Linux-visible block
  device presence, read-only state, writable state, nonzero size, sysfs serial
  identity, sector reads, read-only write rejection, writable sector writes, and
  writable flush.
- This does not prove virtio-fs, host folder mounts, registry pull, arbitrary
  imported image compatibility, package-manager behavior, networking, cgroups,
  namespace completeness, all virtio-blk request types, or App Store acceptance.

### 2026-06-12 OCI-derived PTY delayed input proof on iOS Simulator

Goal:

- Strengthen the Phase 7 virtio-console and PTY proof by validating that an
  OCI-derived materialized root can receive a second host-to-Linux input event
  after the Linux shell is already blocked in `read`.
- Keep Linux-visible behavior owned by the running upstream Linux kernel path:
  PTY allocation, `/dev/pts`, shell `read`, stdin/stdout/stderr, and terminal
  output remain Linux runtime behavior. The test harness only schedules input.

Changes:

- `OrlixTestRunner/Tests/XCTest/OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests.swift`
  - Extends `testOCIDerivedMaterializedRootUsesLinuxPTYStdio` to require:
    - `ORLIX_ENV_PTY_WAITING_FOR_INPUT`
    - `ORLIX_ENV_PTY_DELAYED_INPUT_OK`
  - Sends the PTY proof in phases:
    - Initial PTY checks and a blocking shell `read`.
    - Delayed payload `orlix-pty-delayed-input`.
    - Final `ORLIX_ENV_PTY_DONE` command after the delayed input success
      marker appears.
  - Adds recorder state for delayed input and post-delayed completion dispatch.

Failed attempt and correction:

- The first proof attempt sent the whole script in one PTY payload. The shell
  had already consumed the scripted input stream, so `read` returned before the
  delayed payload arrived and emitted
  `ORLIX_ENV_PTY_PROOF_FAILED_DELAYED_INPUT`.
- That run was hung waiting for `ORLIX_ENV_PTY_DONE` and was stopped with:
  - `rtk killall xcodebuild`
- The corrected proof does not place any command after the blocking `read` in
  the initial input stream.

Boundary:

- This is OrlixTestRunner proof coverage over the iOS Simulator runtime path.
- No OrlixKernel runtime dependency on Apple container/containerization,
  Virtualization.framework, Docker daemon, runc, VM lifecycle, registry pull,
  macOS runtime target, or macOS user surface was added.
- No OrlixHostAdapter or MLibC behavior was changed.
- The proof target remains iOS Simulator only. macOS is only the Xcode build,
  simulator-control, and result-inspection host.

Commands and results:

- Static check before rerun:
  - `rtk git diff --check -- OrlixTestRunner/Tests/XCTest/OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests.swift`
  - Result: exited 0 with no output.
- Corrected focused iOS Simulator proof:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests/testOCIDerivedMaterializedRootUsesLinuxPTYStdio test`
  - Result: exited 0.
  - Result bundle:
    `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.12_10-13-25-+0200.xcresult`.
  - Output: 1 XCTest executed, 0 failures, `** TEST SUCCEEDED **`.
  - Runtime output included:
    - `ORLIX_ENV_PTY_BEGIN`
    - `ORLIX_ENV_PTY_STDIN_OK`
    - `ORLIX_ENV_PTY_STDOUT_OK`
    - `ORLIX_ENV_PTY_STDERR_OK`
    - `/dev/pts/0`
    - `ORLIX_ENV_PTY_PATH_OK`
    - `ORLIX_ENV_PTY_WAITING_FOR_INPUT`
    - `orlix-pty-delayed-input`
    - `ORLIX_ENV_PTY_DELAYED_INPUT_OK`
    - `ORLIX_ENV_PTY_DONE`

Final checks:

- Whitespace:
  - `rtk git diff --check -- OrlixTestRunner/Tests/XCTest/OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests.swift docs/plans/active/oci-derived-environments-virtio-plane/IMPLEMENT.md`
  - Result: exited 0 with no output before this log append.
- Source-file forbidden dependency and wording scan:
  - `rtk rg -n "AppleContainer|apple/container|Containerization|Virtualization\\.framework|Virtualization|Process\\(|NSTask|Foundation\\.Process|aligned with macOS|aligned with macos|alligned with macos|alligned with macOS|macOS user|macOS runtime|IXLand|OrlixKit" OrlixTestRunner/Tests/XCTest/OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests.swift --glob '!Build/**'`
  - Result: no matches.
- Crash report check:
  - `rtk ls -t /Users/rudironsoni/Library/Logs/DiagnosticReports/OrlixTestRunner-*.ips`
  - Latest report remains
    `/Users/rudironsoni/Library/Logs/DiagnosticReports/OrlixTestRunner-2026-06-12-032807.ips`.
  - No newer `OrlixTestRunner` crash report appeared after the 10:13 focused
    iOS Simulator proof.

Current conclusion:

- The OCI-derived PTY proof now covers Linux-visible PTY stdin/stdout/stderr,
  `/dev/pts` identity, and delayed interactive input delivery after the shell is
  already waiting for input.
- This does not prove full terminal job control, signal delivery through the
  terminal path, winsize/ioctl behavior, flow control, `poll`/`select` readiness
  across all PTY states, virtio-fs, registry pull, networking, cgroups,
  arbitrary image compatibility, package-manager behavior, or App Store
  acceptance.

### 2026-06-12 explicit host-folder mounts require Linux mount backend

Goal:

- Make the current fail-loud behavior for Documents and security-scoped external
  mounts more precise. Environment metadata may describe these mounts, but
  materialized execution must reject them until Orlix has a real
  Linux-compatible runtime mount backend.
- Prevent a future shortcut where OrlixOS silently accepts host-folder mount
  metadata and drops it, or where host paths become Linux rootfs truth.

Changes:

- `OrlixOS/Sources/Session/OrlixEnvironment.swift`
  - Replaces the generic `OrlixEnvironmentRootImageError.unsupportedMount` case
    with `missingLinuxMountBackend`.
  - `OrlixEnvironmentRootImage.materialized(...)` still rejects the first
    explicit mount, but now the error names the real missing component.
- `OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
  - Updates explicit Documents and security-scoped external mount
    materialization tests to expect `.missingLinuxMountBackend(...)`.

Boundary:

- This is OrlixOS environment materialization guard behavior only.
- No OrlixKernel VFS, mount namespace, syscall, devtmpfs, procfs, sysfs, or
  virtio behavior was changed.
- No OrlixHostAdapter behavior was changed.
- No Apple container/containerization, Virtualization.framework, Docker daemon,
  runc, VM lifecycle, registry pull, macOS runtime target, or macOS user surface
  was added.
- The proof target remains iOS Simulator only. macOS is only the Xcode build,
  simulator-control, and result-inspection host.

Commands and results:

- Static check:
  - `rtk git diff --check -- OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
  - Result: exited 0 with no output.
- Focused iOS Simulator proof:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testDocumentsMountIsExplicitEnvironmentDescriptorMetadata -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testSecurityScopedExternalMountIsExplicitEnvironmentDescriptorMetadata -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testEnvironmentRootImageRejectsUnimplementedExplicitMounts -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testRootfsSourcesKeepTmpLinuxOwnedAndDocumentsOutOfRootTruth test`
  - Result: exited 0.
  - Result bundle:
    `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_10-17-41-+0200.xcresult`.
  - Output: 4 XCTest cases executed, 0 failures, `** TEST SUCCEEDED **`.
  - Passed tests:
    - `testDocumentsMountIsExplicitEnvironmentDescriptorMetadata`
    - `testSecurityScopedExternalMountIsExplicitEnvironmentDescriptorMetadata`
    - `testEnvironmentRootImageRejectsUnimplementedExplicitMounts`
    - `testRootfsSourcesKeepTmpLinuxOwnedAndDocumentsOutOfRootTruth`

Final checks:

- Whitespace:
  - `rtk git diff --check -- OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift docs/plans/active/oci-derived-environments-virtio-plane/IMPLEMENT.md`
  - Result: exited 0 with no output before this log append.
- Source-file forbidden dependency and wording scan:
  - `rtk rg -n "AppleContainer|apple/container|Containerization|Virtualization\\.framework|Virtualization|Process\\(|NSTask|Foundation\\.Process|aligned with macOS|aligned with macos|alligned with macos|alligned with macOS|macOS user|macOS runtime|IXLand|OrlixKit" OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift --glob '!Build/**'`
  - Result: no matches.
- Mount materialization error scan:
  - `rtk rg -n "unsupportedMount|missingLinuxMountBackend" OrlixOS/Sources/Session/OrlixEnvironment.swift OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
  - Result: no `unsupportedMount` matches in runtime source or tests. Expected
    `missingLinuxMountBackend` matches remain in the materialization guard and
    focused tests.
- Crash report check:
  - `rtk ls -t /Users/rudironsoni/Library/Logs/DiagnosticReports/OrlixTestRunner-*.ips`
  - Latest report remains
    `/Users/rudironsoni/Library/Logs/DiagnosticReports/OrlixTestRunner-2026-06-12-032807.ips`.
  - No newer `OrlixTestRunner` crash report appeared after the 10:17 focused
    iOS Simulator proof.

Current conclusion:

- OrlixOS now distinguishes explicit host-folder mount metadata from a
  runnable materialized root. Documents and security-scoped external mounts
  remain valid descriptor inputs, but they fail materialization with a named
  missing Linux mount backend until the runtime path exists.
- This does not implement host-folder mounting, virtio-fs, FUSE, external
  security-scoped bookmark resolution, Linux mount namespace entry, runtime
  mount dispatch, package-manager behavior, registry pull, networking, cgroups,
  arbitrary image compatibility, or App Store acceptance.

### 2026-06-12 environment entry proof corrected back to root binding

Goal:

- Remove the mistaken environment-entry checkpoint that made a specific
  container-runtime operation a required milestone for OCI-derived Orlix
  environments.
- Restore the focused kselftest to prove only the root-binding behavior Orlix
  currently needs: child process enters a private environment root, executes a
  Linux binary there, and parent root visibility remains isolated.
- Keep the proof iOS Simulator runtime-only. macOS is only the Xcode build,
  simulator-control, and result-inspection host.

Changes made:

- `OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/environment_entry_probe.c`
  - Calls `unshare(CLONE_NEWNS)`.
  - Marks `/` private with `mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL)`.
  - Creates a tmpfs environment root under `/mnt`.
  - Copies `/proc/self/exe` into the environment root.
  - Enters the environment root with the existing root-binding probe path.
  - Verifies after re-exec that `/etc/os-release` and
    `/environment-entry-probe` come from the new root and that parent-root
    markers are not visible.
- `OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift`
  - Expects `environment entry child root entered`.

Incorrect intermediate evidence:

- A prior intermediate attempt made the environment-entry probe depend on a
  specific container-runtime root switch and failed in the iOS Simulator proof.
  That path is removed from the active plan and from the runnable proof.
- First failed focused iOS Simulator proof:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixKernelUpstreamTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixKernelUpstreamTests/OrlixKernelUpstreamTests/testEnvironmentEntryProbeCompletesThroughOrlixOSTerminalSession test`
  - Result bundle:
    `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_10-21-55-+0200.xcresult`.
  - Result: failed at runtime.
  - TAP output included:
    - `1..6`
    - `ok 1 - environment entry parent marker created`
    - `ok 2 - environment entry child started`
    - `not ok 3 - environment entry child root switch completed`
    - `not ok 4 - environment entry child exited cleanly`
    - `ok 5 - environment entry root is hidden from parent`
    - `ok 6 - environment entry parent marker cleaned`
- Second failed focused iOS Simulator proof after adding
  `MS_REC | MS_PRIVATE`:
  - Same command.
  - Result bundle:
    `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_10-25-15-+0200.xcresult`.
  - Result: failed at runtime with the same TAP failure.
- Diagnostic helper compile failure:
  - Result bundle:
    `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_10-28-16-+0200.xcresult`.
  - Result: build failed before runtime because the diagnostic helper used
    wrong local helper names.
  - The diagnostic helper has been removed with the mistaken checkpoint.

Current status:

- Pending a focused iOS Simulator rerun of the corrected environment-entry
  probe.
- The expected proof target is `environment entry child root entered`, not a
  container-runtime operation.

Boundary:

- This checkpoint touches a Linux kselftest and the XCTest assertion over the
  iOS Simulator runtime path.
- No OrlixKernel dependency on Apple container/containerization,
  Virtualization.framework, Docker daemon, runc, VM lifecycle, registry pull,
  macOS runtime target, or macOS user surface was added.
- No OrlixHostAdapter, MLibC, OrlixOS importer, OCI layout, registry,
  virtio-fs, networking, cgroups, or App Store behavior was changed.

Required next commands:

- `rtk git diff --check -- OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/environment_entry_probe.c OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift docs/plans/active/oci-derived-environments-virtio-plane/IMPLEMENT.md`
- `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixKernelUpstreamTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixKernelUpstreamTests/OrlixKernelUpstreamTests/testEnvironmentEntryProbeCompletesThroughOrlixOSTerminalSession test`
- After the rerun, record the result bundle and fix any actual Orlix Linux
  root-binding failure that remains.

Current conclusion:

- The mistaken container-runtime checkpoint is removed from the active path.
- The last completed green product checkpoints remain the OCI-derived PTY
  delayed-input proof and the explicit host-folder mount guard proof.

### 2026-06-12 environment entry root-binding correction passed on iOS Simulator

Goal:

- Correct the environment-entry proof so it validates only Orlix's current
  root-binding requirement for named environments.
- Remove the mistaken container-runtime operation from the kselftest, XCTest
  assertion, active plan, and active implementation log.

Changes:

- `OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix/environment_entry_probe.c`
  - Keeps the private mount namespace setup.
  - Keeps the tmpfs environment root fixture.
  - Enters the environment root through the existing root-binding probe path.
  - Verifies that the re-executed child sees `/etc/os-release` and
    `/environment-entry-probe` from the child root.
  - Verifies the parent marker is not visible inside the child root and that
    the environment root is hidden from the parent after wait.
- `OrlixTestRunner/Tests/XCTest/OrlixKernelUpstreamTests/OrlixKernelUpstreamTests.swift`
  - Expects `environment entry child root entered`.
- `docs/plans/active/oci-derived-environments-virtio-plane/PLAN.md`
  - Uses environment root binding and mount namespace proof language.

Evidence:

- Residual wrong-root-operation wording scan:
  - Result: no matches in the active plan, implementation log, kselftest, or
    XCTest assertion after this correction.
- Focused iOS Simulator proof:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixKernelUpstreamTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixKernelUpstreamTests/OrlixKernelUpstreamTests/testEnvironmentEntryProbeCompletesThroughOrlixOSTerminalSession test`
  - Result: exited 0.
  - Result bundle:
    `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixKernelUpstreamTests-2026.06.12_11-46-22-+0200.xcresult`.
  - Output: 1 XCTest executed, 0 failures, `** TEST SUCCEEDED **`.
  - Runtime output included:
    - `1..6`
    - `ok 1 - environment entry parent marker created`
    - `ok 2 - environment entry child started`
    - `ok 3 - environment entry child root entered`
    - `ok 4 - environment entry child exited cleanly`
    - `ok 5 - environment entry root is hidden from parent`
    - `ok 6 - environment entry parent marker cleaned`
    - `ok 9 - environment_entry_probe`
    - `ORLIX-KSELFTEST-END`

Current conclusion:

- Environment entry proof is back to the root-binding behavior Orlix needs for
  the next named-environment work.
- This does not implement live named-environment switching, OCI image execution,
  host-folder mounts, virtio-fs, registry pull, networking, cgroups, arbitrary
  image compatibility, real-device proof, or App Store acceptance.

### 2026-06-12 current plan status after root-binding correction

Current status:

- The active plan remains in progress.
- The mistaken root-switch milestone is removed from the active path.
- The corrected environment-entry proof is green on iOS Simulator.
- The next implementation checkpoint is OrlixOS-level named environment root
  binding, using the existing upstream-Linux OrlixKernel process launch and
  root-entry behavior.

Completed proof checkpoints recorded in this plan:

- Storage policy and environment descriptor groundwork.
- Rootfs tar import API and overwrite protection.
- OCI image layout parsing and selected OCI config metadata validation.
- OCI-derived materialized-root PTY proof with delayed interactive input.
- Virtio-blk base/state device proof, including read-only base, writable state,
  sysfs identity, sector write, and flush.
- Explicit host-folder mount metadata guard that fails until a real Linux mount
  backend exists.
- Corrected environment-entry root-binding proof on iOS Simulator.

Remaining execution order:

1. Prove entering a named environment selects the correct root and descriptor.
2. Connect rootfs tar import to named environment entry in one product-shaped
   path.
3. Implement OCI layout import binding to named environments.
4. Add OCI whiteout and opaque-directory import proof.
5. Add immutable image root plus writable environment state proof.
6. Add overlay/snapshot semantics only after the image-root binding proof is
   stable.
7. Implement host-folder mount backend through Linux-owned mount behavior.
8. Add virtio-fs for Documents and security-scoped external folders.
9. Add registry pull outside OrlixKernel and outside the iOS runtime substrate.
10. Add `orlix run` lifecycle only after persistent named environments are
    reliable.
11. Expand networking through virtio-net, `/proc/net`, and synthetic rtnetlink.
12. Add virtual cgroup v2 and resource-accounting behavior.
13. Expand the Linux oracle and benchmark coverage for imported binaries and
    container-image-derived environments.

Boundary:

- Initial proof target remains iOS Simulator.
- macOS is only the Xcode build, simulator-control, fixture-generation, local
  oracle, and result-inspection host.
- No Docker daemon, runc dependency, Linux VM on iOS, Virtualization.framework
  iOS runtime dependency, Apple container runtime dependency, vminitd, vmnet,
  Rosetta, raw host-path rootfs, local Linux UAPI clone, or Darwin/libc/MLibC
  leakage into OrlixKernel is part of the plan.

### 2026-06-12 Linux-substrate-first execution order correction

What changed:

- Corrected the active plan after review against Orlix architecture and ADR
  0017. The prior recommended sequence let OCI Runtime config, lifecycle, and
  feature-report work appear before the Linux substrate they depend on.
- The corrected order keeps OCI image import as OrlixOS data-input work, but
  moves OCI Runtime lifecycle, `orlix run`, compatibility language, and feature
  reporting behind Linux-owned root binding, mount, fd, `/dev`, signal, wait,
  PTY, host-folder mount, virtio-fs, networking, cgroup, oracle, and native
  performance proof as applicable.
- Constrained the older registry and `orlix run` phase notes so they do not
  read as an alternate implementation sequence ahead of the Linux substrate.

Corrected remaining execution order:

1. Prove cross-boot writable state persistence.
2. Connect rootfs tar import and OCI layout import to named environment entry.
3. Complete imported-root image fidelity before runtime claims.
4. Expand Linux substrate proof for entered environments.
5. Expand the Linux oracle for substrate behavior.
6. Implement host-folder mount backend through Linux-owned mount behavior.
7. Add virtio-fs for external folders.
8. Expand networking through upstream Linux networking paths.
9. Add virtual cgroup v2 and resource accounting behavior.
10. Add native performance benchmark suite for imported binaries.
11. Add OCI Runtime config parser and schema validation.
12. Add OCI Runtime lifecycle model.
13. Add OCI Linux runtime defaults.
14. Add OCI feature report.
15. Add product `orlix run`.
16. Add registry pull tooling.

Current status:

- OCI image import remains valid early input work.
- OCI Runtime support remains unproved until the required Linux substrate and
  lifecycle evidence exist.
- Agents must not implement or claim OCI Runtime lifecycle, `orlix run`, or
  feature-report support before the relevant Linux-owned behavior is proved.

### 2026-06-12 OrlixOS named environment session selection

Goal:

- Add the product-shaped OrlixOS session entry point for selecting a named
  environment root through the existing environment registry and materialized
  root-image path.
- Keep the change above OCI Runtime lifecycle work. This is OrlixOS session
  translation into existing root-image boot state, not `runc`, Docker, registry
  pull, OCI Runtime compliance, virtio-fs, networking, or cgroup support.

Changes:

- `OrlixOS/Sources/Session/OrlixOS.swift`
  - Added `OrlixLinuxSession(environmentID:terminal:)`, which uses the default
    `OrlixEnvironmentRegistry` and existing materialized-root binding.
  - Added SPI testing initializer
    `OrlixLinuxSession(environmentID:registry:kernelCommandLine:terminal:)`
    so tests can inject an isolated registry root.
- `OrlixOS/Tests/XCTest/OrlixOSTests/OrlixTerminalSessionTests.swift`
  - Added
    `testLinuxSessionCanSelectNamedEnvironmentRootFromRegistry`, proving a
    saved descriptor and base/state images can be selected by environment ID
    and translated into the session boot config.
- `docs/plans/active/oci-derived-environments-virtio-plane/GOAL.md`
  - Kept the active goal aligned with the Linux-substrate-first order and under
    4000 characters.

Evidence:

- RED proof:
  - Initial sandboxed run did not reach compilation because CoreSimulator and
    SwiftPM cache access were blocked.
  - Elevated focused run failed for the expected missing API:
    `No exact matches in call to initializer`.
  - Result bundle:
    `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_17-17-11-+0200.xcresult`.
- GREEN proof:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testLinuxSessionCanSelectNamedEnvironmentRootFromRegistry test`
  - Result: exited 0.
  - Result bundle:
    `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_17-17-48-+0200.xcresult`.
  - Output: 1 XCTest executed, 0 failures, `** TEST SUCCEEDED **`.
- Focused regression proof:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testLinuxSessionCanBindMaterializedEnvironmentRootImage -only-testing:OrlixOSTests/OrlixTerminalSessionTests/testLinuxSessionCanSelectNamedEnvironmentRootFromRegistry test`
  - Result: exited 0.
  - Result bundle:
    `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixOSTests-2026.06.12_17-18-55-+0200.xcresult`.
  - Output: 2 XTests executed, 0 failures, `** TEST SUCCEEDED **`.

Current conclusion:

- OrlixOS now has a named environment session selection path that maps an
  environment descriptor and stored base/state images into existing Orlix boot
  configuration.
- This does not prove cross-boot state persistence, import-to-enter product
  flow, OCI Runtime lifecycle compliance, truthful feature reporting, product
  `orlix run`, host-folder mounts, virtio-fs, networking, cgroups, native
  performance, real-device behavior, or App Store acceptance.

### 2026-06-12 Named environment session runtime entry proof

Goal:

- Prove the public OrlixOS named environment session path can boot a copied
  environment root and apply that environment descriptor's argv, env, cwd, uid,
  and gid defaults through the iOS-hosted Orlix runtime path.
- Keep the checkpoint below OCI Runtime lifecycle work. This is named
  environment session selection and descriptor translation into current Orlix
  execution state, not Docker, `runc`, registry pull, OCI Runtime compliance,
  virtio-fs, networking, cgroup support, or multiple live environments inside
  one already-running OrlixKernel.

Changes:

- `OrlixTestRunner/Tests/XCTest/OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests.swift`
  - Added
    `testCopiedNamedEnvironmentSessionSelectionEntersRootAndDescriptor`.
  - Added a runtime helper that copies a fixture environment, saves the copied
    descriptor in an isolated `OrlixEnvironmentRegistry`, enters it through
    `OrlixLinuxSession(environmentID:registry:terminal:)`, and verifies Linux
    userspace output.
  - Extended descriptor-execution proof output to include
    `/bin/cat /etc/os-release` so the selected root is observed from inside
    the entered environment.
- `OrlixOS/Sources/Session/OrlixOS.swift`
  - Changed the SPI named-environment session initializer default command line
    to `OrlixEnvironmentRootImage.defaultKernelCommandLine` so descriptor
    execution tokens are present unless a caller explicitly overrides them.
- `OrlixOS/Sources/Session/OrlixEnvironment.swift`
  - Changed registry materialized-root image creation to use
    `OrlixEnvironmentRootImage.defaultKernelCommandLine` by default for the
    same reason.

Evidence:

- RED proof:
  - Initial sandboxed focused run did not reach compilation because
    CoreSimulator and SwiftPM cache access were blocked.
  - Elevated focused run failed for the expected missing runtime helper:
    `Value of type 'OrlixEnvironmentRootRuntimeProofRunner' has no member 'runCopiedNamedEnvironmentThroughSessionSelection'`.
  - Result bundle:
    `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.12_17-26-36-+0200.xcresult`.
- Diagnosis after adding the test helper:
  - The focused runtime run booted the copied environment but dropped into the
    default shell path instead of executing descriptor defaults.
  - Runtime output showed the named-session path used the bundled command line:
    `console=ttyS0 console=hvc0 root=/dev/vda rootfstype=ext4 ro orlix.profile=release`.
  - The run was terminated with `rtk killall xcodebuild` after confirming the
    missing descriptor-token path.
- GREEN proof:
  - `rtk timeout 900 xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixPTYRuntimeTests/OrlixEnvironmentRootRuntimeTests/testCopiedNamedEnvironmentSessionSelectionEntersRootAndDescriptor test`
  - Result: exited 0.
  - Result bundle:
    `.deriveddata/OrlixSystem-sim/Logs/Test/Test-OrlixPTYRuntimeTests-2026.06.12_17-32-12-+0200.xcresult`.
  - Output: 1 XCTest executed, 0 failures, `** TEST SUCCEEDED **`.
  - Runtime output included:
    - `ORLIX_ENV_EXEC_BEGIN`
    - `argv0=orlix-descriptor-xxxxxxxxxxxxxxxx`
    - `argv1=argument with spaces`
    - `env=descriptor value with spaces`
    - `pwd=/tmp`
    - `ID=orlix-oci-runtime-proof`
    - `Uid:\t1000`
    - `Gid:\t100`
    - `ORLIX_ENV_EXEC_DONE`

Current conclusion:

- The named environment session path now has end-to-end iOS Simulator proof
  that a copied environment root is selected and descriptor execution defaults
  reach Linux userspace through the OrlixOS session API.
- The active plan state is reconciled so this checkpoint no longer appears as
  remaining work.
- This does not prove multiple live environments inside one already-running
  OrlixKernel, cross-boot state persistence, import-to-enter product flow, OCI
  Runtime lifecycle compliance, truthful feature reporting, product `orlix run`,
  host-folder mounts, virtio-fs, networking, cgroups, native performance,
  real-device behavior, or App Store acceptance.

Current status:

- The named environment session runtime entry checkpoint is complete on iOS
  Simulator.
- The active remaining order now starts with cross-boot writable state
  persistence and keeps OCI Runtime lifecycle, `orlix run`, and feature
  reporting behind the required Linux substrate proof.

Corrected remaining execution order:

1. Prove cross-boot writable state persistence.
2. Connect rootfs tar import and OCI layout import to named environment entry.
3. Complete imported-root image fidelity before runtime claims.
4. Expand Linux substrate proof for entered environments.
5. Expand the Linux oracle for substrate behavior.
6. Implement host-folder mount backend through Linux-owned mount behavior.
7. Add virtio-fs for external folders.
8. Expand networking through upstream Linux networking paths.
9. Add virtual cgroup v2 and resource accounting behavior.
10. Add native performance benchmark suite for imported binaries.
11. Add OCI Runtime config parser and schema validation.
12. Add OCI Runtime lifecycle model.
13. Add OCI Linux runtime defaults.
14. Add OCI feature report.
15. Add product `orlix run`.
16. Add registry pull tooling.
