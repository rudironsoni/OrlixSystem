# Upstream Linux iOS Port Design

## Goal

Define the target Orlix architecture before implementation changes: upstream Linux compiled for `arch/orlix`, packaged as `OrlixKernel.xcframework`, with Orlix-specific adaptation through Linux-native extension points.

## Approved Approach

Use the full architecture spec first approach. Write the canonical specification, ADRs, README, and agent rules before changing build targets or source code.

## Core Decisions

Orlix builds upstream Linux rather than rewriting Linux core subsystems locally.

The local `OrlixKernel/fs`, `OrlixKernel/kernel`, and `OrlixKernel/runtime` tree is migration reference only. New Linux subsystem behavior must not be added there.

The first proof milestone is Kbuild `vmlinux` for `ARCH=orlix`, not XCFramework packaging from boot stubs.

The public product API is a minimal bootloader entrypoint. It takes app-level boot config and selects Linux-shaped boot inputs.

Profile boot data uses device tree and kernel command-line defaults, not a custom boot-template format.

Orlix uses upstream virtio device classes wherever they fit. Orlix-specific code supplies transport and backend mechanics under `drivers/orlix`, shaped as virtio-mmio where practical.

Root storage uses virtio-blk-backed base and writable state images, assembled with upstream OverlayFS by initramfs.

Console support includes serial-style and virtio-console paths selected through normal Linux boot arguments.

Package and execution behavior follows normal Linux package-manager trust, filesystem permissions, mount flags, MM behavior, and upstream security mechanisms.

iOS lifecycle maps to Linux suspend/resume and future hibernation semantics.

## Documentation Outputs

The design is captured in:

- `docs/UPSTREAM_LINUX_IOS_PORT_SPEC.md`
- `docs/adr/0001-upstream-linux-over-local-kernel-rewrite.md`
- `docs/adr/0002-plan-the-port-as-milestones.md`
- `docs/adr/0003-default-to-the-app-store-profile.md`
- `docs/adr/0004-use-a-bootloader-only-product-api.md`
- `docs/adr/0005-use-profile-device-trees-for-boot-data.md`
- `docs/adr/0006-use-a-virtio-first-device-model.md`
- `docs/adr/0007-place-orlix-virtio-transport-under-drivers-orlix.md`
- `docs/adr/0008-use-virtio-block-and-overlayfs-for-root-storage.md`
- `docs/adr/0009-support-serial-and-virtio-console-selection.md`
- `docs/adr/0010-use-normal-linux-package-and-execution-policy.md`
- `docs/adr/0011-map-ios-lifecycle-to-linux-suspend-and-hibernation.md`
- `docs/adr/0012-stage-deletion-of-the-local-kernel-prototype.md`
- `README.md`
- `AGENTS.md`
- `CONTEXT.md`

## Milestone Sequence

Milestone 1 is Kbuild `vmlinux` proof and documentation alignment.

Milestone 2 is boot entrypoint and profile device tree.

Milestone 3 is virtio root disks.

Milestone 4 is initramfs and OverlayFS root assembly.

Milestone 5 is console selection and early interactive boot.

Milestone 6 is remaining virtio-first devices.

Final cleanup deletes the local kernel prototype directories.
