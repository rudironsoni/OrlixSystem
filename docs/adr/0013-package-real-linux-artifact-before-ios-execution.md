# ADR 0013: Package Real Linux Artifact Before iOS Execution

## Status

Accepted

## Context

Orlix cannot validate the product direction without building and testing through iOS. The iOS proof path needs `OrlixKernel.xcframework` earlier than originally planned, but the old boot-stub packaging path would make packaging look successful without proving Linux.

## Decision

`OrlixKernel.xcframework` packaging is Milestone 3 and must package or link the app-hosted OrlixKernel integration for the selected profile. Boot-stub packaging is not product proof.

Each XCFramework slice contains a real iOS Mach-O framework or static-library wrapper plus the private OrlixKernel runtime integration and boot resources needed by the iOS host path. A `vmlinux`-style artifact may exist only as an optional developer/debug artifact with a named consumer. It is not a milestone, not product proof, not runtime proof, and not libc proof.

Each `OrlixKernel` framework build packages one selected profile's kernel integration. Closed built-in profile DTBs may all be bundled with the kernel framework as machine-description resources. Product rootfs and distribution payload resources are delivered by the `OrlixOS` Kit/framework.

## Consequences

iOS-hosted kernel-interface execution and later product runtime proof depend on packaging or linking the app-hosted runtime integration.

Packaging proof is separate from execution proof: it proves the product artifact contains the hosted integration and required resources, not that Linux has booted, run KUnit, run kselftest, emitted Linux test output, or run product userspace.

Packaging multiple profile-specific hosted integrations into one framework would blur proof and increase product surface. Build separate framework artifacts when a different profile kernel integration is needed.

The lower-level boot entrypoint lives in the iOS wrapper header under `OrlixKernel/Sources/include`. Apps consume the `OrlixOS` Kit for the delivered OS session API and payload surface. Private kernel resources carry hosted kernel integration inputs and bundled built-in profile DTBs; curated OS payload resources are `OrlixOS` resources resolved from target metadata.

The test initramfs belongs to the XCTest host app bundle and owning project `Tests` tree. It may be addressed through an opaque resource identifier during tests, but it is not part of the product framework contract.

The product initramfs/rootfs payload is an OrlixOS root-assembly artifact and remains separate from the test initramfs. Do not introduce profile-specific initramfs variants unless a concrete future requirement forces them.

Narrow bootloader or host-adapter test binaries may exist only as internal test artifacts and must not be named or documented as `OrlixKernel.xcframework` product proof.
