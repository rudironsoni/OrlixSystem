# Orlix Upstream Linux Port

Orlix is an iOS-hosted port of upstream Linux. This repository keeps the local Orlix port overlay and build packaging around an upstream Linux source tree generated on demand.

The product goal for this skeleton is narrow: materialize and configure an Orlix-flavored upstream Linux worktree, then package the bootloader-facing `OrlixKernel.xcframework` skeleton library for iOS simulator and device targets. This repository does not claim real boot-to-`start_kernel`, syscall entry, drivers, rootfs mount, bundled userspace, procfs/sysfs/devfs/cgroupfs runtime proof, or App Store execution policy.

The generated Xcode project remains `OrlixKernel.xcodeproj` in this branch. See `docs/UPSTREAM_LINUX_IOS_PORT_SPEC.md` for the canonical architecture and naming rules.

## Source Model

Upstream Linux source is local generated input, not committed project source.

Run:

```bash
make bootstrap-linux-upstream
```

That materializes upstream Linux into:

```text
Linux/upstream/linux-6.12
```

The generated upstream tree is ignored. Treat it as read-only input. Do not make durable Orlix changes there.

Durable Orlix Linux port changes belong in:

```text
Linux/ports/orlix/
├── overlay/
├── patches/
└── configs/
```

The overlay contains committed files that are copied into Linux-native paths in the disposable worktree. The patches contain intentional upstream-tree deltas that cannot be represented as plain overlay files.

## Disposable Worktree

Run:

```bash
make prepare-linux-worktree
```

That creates:

```text
Build/linux-work
```

`Build/linux-work` is disposable. It is produced from `Linux/upstream/linux-6.12` plus `Linux/ports/orlix/overlay` and `Linux/ports/orlix/patches`.

Use it for local build products and generated Linux state only. If a change should survive regeneration, move it back into the committed port overlay or patch set.

## Port Ownership

Orlix materializes and configures upstream Linux through Kbuild. It does not locally rewrite Linux core subsystems.

Committed Orlix Linux code is owned by Linux-native extension points:

- `Linux/ports/orlix/overlay/arch/orlix/` owns Orlix architecture glue.
- `Linux/ports/orlix/overlay/drivers/orlix/` owns Orlix Linux drivers.
- `Linux/ports/orlix/overlay/arch/orlix/boot/` owns boot preparation code for the Orlix architecture port.
- `OrlixHostAdapter/` owns private iOS/Darwin mechanics used behind Orlix-owned seams.

Host mechanics must not become Linux policy. Linux core behavior remains upstream Linux behavior unless an explicit Orlix port patch changes it for the port.

## Bootloader-Facing Product Surface

The public product API is intentionally small and bootloader-facing.

It is declared in:

```text
OrlixKernel/include/OrlixKernel.h
```

The public surface is limited to:

- `struct boot_params`
- `OrlixPrepareBootParams`
- `OrlixBoot`

Do not add syscall, libc, driver, filesystem, or runtime facade APIs to this public header as part of the skeleton. Those capabilities require real upstream Linux execution proof before they can be represented as product surface.

## Build And Package Targets

Use the Makefile targets as the proof entrypoints for this skeleton:

```bash
make build-linux-simulator
make build-linux-iphoneos
make package-orlixkernel-xcframework
```

`make build-linux-simulator` prepares the disposable Linux worktree, runs the Orlix Linux Kbuild configuration step, and builds the simulator OrlixKernel skeleton library slice.

`make build-linux-iphoneos` prepares the disposable Linux worktree, runs the Orlix Linux Kbuild configuration step, and builds the device OrlixKernel skeleton library slice.

`make package-orlixkernel-xcframework` builds both slices and packages the public OrlixKernel XCFramework artifact.

These targets prove the skeleton build/package flow. They are not runtime proof of boot, userspace, filesystems, drivers, or syscall behavior.

## Contributor Rules

- Keep upstream Linux source generated and read-only.
- Commit Orlix port code only through the overlay, patch set, configs, or bootloader-facing product surface.
- Keep `Build/linux-work` disposable.
- Keep Linux policy in upstream Linux and Linux-native Orlix port paths.
- Keep iOS/Darwin mechanics private to `OrlixHostAdapter/`.
- Keep the public product API limited to the bootloader-facing surface until runtime proof exists.

See `docs/UPSTREAM_LINUX_IOS_PORT_SPEC.md` for the full architecture rules, long-term ownership model, App Store profile boundaries, and proof expectations.
