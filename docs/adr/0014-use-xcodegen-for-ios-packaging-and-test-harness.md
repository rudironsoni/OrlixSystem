# ADR 0014: Use XcodeGen For iOS Packaging And Test Harness

## Status

Accepted

## Context

Orlix needs an Xcode project for iOS packaging, XCTest launch, resources, and `OrlixKernel.xcframework` creation. The durable XcodeGen input is `project.yml`; the generated `.xcodeproj` is disposable. The canonical proof artifact is the app-hosted OrlixKernel integration that the iOS app actually runs.

Tuist and XcodeGen could both generate the Xcode surface. Tuist would add a larger app/module build abstraction, while Orlix needs Xcode to remain a thin iOS packaging and test harness around the app-hosted OrlixKernel runtime.

## Decision

Use XcodeGen for the iOS project generation surface. XcodeGen describes the iOS packaging, resource bundling, and XCTest host targets that consume or link the app-hosted OrlixKernel integration. `vmlinux` is not an XcodeGen packaging input and is not a proof artifact.

Commit `project.yml` as the durable XcodeGen source of truth. Do not commit the generated `.xcodeproj` unless a concrete future toolchain constraint requires it.

XcodeGen schemes should make local Xcode use clear, but repository `make` targets own non-interactive proof orchestration for the full App Store/development by `iphoneos`/`iphonesimulator` matrix.

The generated project includes `OrlixTerminal/Sources` as the iOS app host and XCTest host application.

The generated project may depend on `libghostty-spm` for terminal UI packages needed by `OrlixTerminal`, but Linux execution remains owned by Orlix.

The full iOS proof matrix should run through repository automation using the Linux-shaped Make surface and XcodeBuildMCP, not implicitly through a generic fast local test target or a separate public command for every milestone.

The generated Xcode project must support both `iphoneos` and `iphonesimulator` destinations. Both are iOS proof destinations and must validate the same milestone scope. They package and launch an `ARCH=orlix` Linux artifact; they do not redefine the Linux build target as an Apple iOS ABI.

The initial `OrlixKernel.xcframework` slice set is `ios-arm64` for physical devices and `ios-arm64-simulator` for Apple Silicon Simulator. Intel Simulator support is not part of the initial target set.

For a selected Orlix profile, both slices must run the same Linux-visible kernel behavior. Destination-specific differences belong in the iOS host wrapper or `OrlixHostAdapter`, not in the Linux semantics exposed by the hosted kernel integration.

## Consequences

`project.yml` must not recreate boot-stub product proof. Its packaging target must depend on the app-hosted OrlixKernel integration for the selected profile.

Xcode targets may run build phases that invoke repository build targets through the top-level Makefile or project Makefiles, but they must not become the source of Linux semantics or replace Kbuild proof.

XCTest targets live under project-local test roots such as `OrlixKernel/Tests/XCTest` and `OrlixHostAdapter/Tests/XCTest`. They are for iOS-hosted Orlix launch, Linux test-output collection, packaging checks, and narrow `OrlixHostAdapter` host mechanics. Linux subsystem assertions remain in KUnit or kselftest.

Milestone proof should not treat Simulator as a lighter preflight or physical device as a different scope. The same XCTest suite and assertions must pass on both destinations. Destination-specific wiring is allowed for signing, bundle/resource lookup, simulator/device transport, or host-adapter mechanics, but not for skipping milestone scope.

Implementation may bring up the Simulator path first for speed, but this is sequencing only. It does not lower the proof bar for milestone completion.
