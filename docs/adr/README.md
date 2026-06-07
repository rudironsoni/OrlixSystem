# ADR Index

Durable architecture decisions live in this directory. `AGENTS.md` and skills may route to ADRs, but they must not duplicate full ADR content.

## Architecture Direction

- `0001-upstream-linux-over-local-kernel-rewrite.md`
- `0004-use-a-bootloader-only-product-api.md`
- `0012-stage-deletion-of-the-local-kernel-prototype.md`
- `0020-build-linux-as-mach-o-native-orlixkernel-framework.md`
- `0021-keep-libc-out-of-orlixkernel-boundaries.md`
- `0022-use-hosted-linux-elf-execution.md`

## Proof And Runtime Claims

- `0002-ground-milestones-in-linux-native-proof.md`
- `0017-product-runtime-claim-promotion-order.md`
- `0018-ground-kernel-proof-in-app-hosted-runtime.md`

## Build And Packaging

- `0003-default-to-the-app-store-profile.md`
- `0013-package-real-linux-artifact-before-ios-execution.md`
- `0014-use-xcodegen-for-ios-packaging-and-test-harness.md`
- `0015-build-orlixterminal-as-the-ios-host-app.md`
- `0016-keep-orlix-userspace-abi-profile-invariant.md`
- `0019-keep-make-targets-linux-shaped.md`
- `0023-use-release-development-profiles-and-curated-orlixos-distribution.md`

## Boot, Devices, And Lifecycle

- `0005-use-profile-device-trees-for-boot-data.md`
- `0006-use-a-virtio-first-device-model.md`
- `0007-place-orlix-virtio-transport-under-drivers-orlix.md`
- `0008-use-virtio-block-and-overlayfs-for-root-storage.md`
- `0009-support-serial-and-virtio-console-selection.md`
- `0010-use-normal-linux-package-and-execution-policy.md`
- `0011-map-ios-lifecycle-to-linux-suspend-and-hibernation.md`
