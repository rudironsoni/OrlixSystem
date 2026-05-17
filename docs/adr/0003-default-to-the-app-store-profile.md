# ADR 0003: Default To The App Store Profile

## Status

Accepted

## Context

Orlix has App Store and development profiles. The App Store profile carries the strictest product constraints.

Defaulting to a broader profile risks depending on behavior that the real product cannot use.

## Decision

Normal builds default to `PROFILE=appstore`. The development profile is an explicit choice.

The development profile must stay equivalent to the App Store profile except for explicit debug and testing affordances. It must not introduce broader product behavior, different Linux-visible device shape, or noisier boot/resource semantics.

## Consequences

Milestone 1 proof requires App Store and development profile app-hosted OrlixKernel builds.

Build targets and documentation must not imply development behavior is the normal product contract.

Milestones that claim iOS packaging, boot, runtime, or Linux behavior must validate the same XCTest suite and assertions across four cells: App Store on `iphoneos`, App Store on `iphonesimulator`, development on `iphoneos`, and development on `iphonesimulator`. Development-only differences should be explainable as debug or test enablement.

Test-only configuration overlays may be applied to both App Store and development proof builds to enable KUnit, kselftest support, KUnit debugfs, and related proof affordances. These overlays must not redefine the normal product profile configs.

Orlix KUnit selections and KUnit-specific affordances live in the committed `arch/orlix/.kunitconfig`, matching upstream KUnit practice. Broader proof-only config fragments may be added later only when a non-KUnit proof affordance needs one.

Proof builds for both App Store and development merge the selected profile defconfig with `arch/orlix/.kunitconfig`. Normal product builds do not include that merge.

Only App Store and development profiles are supported.
