# ADR 0023: Use Release/Development Profiles And Deliver OrlixOS As The Kit

## Status

Accepted

## Context

Orlix is intended for App Store distribution. Naming the normal product profile `appstore` created the wrong distinction: every shippable Orlix build must respect App Store constraints, while the real engineering split is between release behavior and development diagnostics.

The package proof ladder also risked reading as a sequence of arbitrary package experiments. Orlix needs a delivered OS Kit that defines curated userspace content, package trust, storage roles, update channels, payload metadata, and the app-facing Linux session surface.

Root storage has more than one valid Linux-shaped mode. Virtio block plus ext4 and OverlayFS is the writable-root mode, but immutable roots and initramfs-only proof roots are also valid when selected intentionally.

## Decision

Orlix supports exactly two product profiles:

- `release`: the default product profile.
- `development`: a non-release profile for diagnostics and test affordances.

The profiles must remain userspace ABI invariant. Development may add diagnostics, assertions, tracing, and test knobs, but it must not expose a different Linux ABI, package ABI, device shape, or userspace contract.

OrlixOS is the Kit. Release builds bundle curated executable userspace content as signed `OrlixOS` framework resources and update that content through app releases first. Apps consume `OrlixOS` for the delivered Linux session and payload surface; they must not depend on a separate `OrlixKit` module or make `OrlixTerminal` own OS delivery. Downloaded binary package repositories are deferred until a curated, signed, profile-approved channel with App Store-safe disclosure and policy checks is explicitly designed and reviewed.

`OrlixOS` owns curated distribution policy, package/rootfs assembly, product payload packaging, target-derived payload metadata, and the app-facing Linux session API. It may wrap the bootloader-shaped entrypoint as a Linux session. It must not own kernel semantics, libc semantics, syscall ABI, private iOS host mechanics, terminal UI rendering, shell behavior, or Linux test-result interpretation.

`OrlixOS` resolves its payload bundle from target/project metadata and registers the resolved private payload root with `OrlixHostAdapter` before boot. Runtime code must not hardcode product bundle identifiers or resource names that belong in the project schema or target Info.plist metadata.

The package proof ladder remains ordered, but its meaning is distribution compatibility:

1. Bash proves the first interactive POSIX shell environment.
2. `jq` proves a small unpatched third-party package.
3. `curl` proves networking and TLS/package-library depth.
4. `zsh` proves a richer shell/package surface.

Root storage is modeled as Linux-visible storage roles:

- `/dev/vda`: immutable bundled distro/base image.
- `/dev/vdb`: app-private writable Linux state image.
- initramfs-only: test/proof mode.
- direct root: valid immutable-root mode.
- overlay root: writable-root mode using upstream OverlayFS.
- future external mounts: explicit user/document mounts through virtio-fs or 9p.

Profile device trees expose the selected root mode and the vda/vdb storage roles as Linux-visible boot data. Release currently selects direct immutable root. Development currently selects overlay root through a bundled product initramfs so writable-root assembly is continuously exercised without making release updates look like post-install executable code mutation.

`dlsym("start_kernel")` is not allowed in the product boot path. The Mach-O-native kernel product links the real Linux entry directly.

Remaining device-like host services should use upstream virtio classes where they fit. Process model, signals, syscall semantics, lifecycle policy, and memory-management policy are not virtio devices; they remain owned by upstream Linux, `arch/orlix`, and narrow private `OrlixHostAdapter` mechanics.

## Consequences

All durable references to the old `appstore` profile name are removed from active code and documentation. ADR 0003 is superseded. ADRs 0008, 0010, 0013, 0014, 0015, and 0017 remain accepted with the updates recorded here.

Build defaults, profile device trees, defconfigs, public boot enums, terminal profile parsing, tests, fixtures, and generated-output expectations use `release` and `development`.

Release package behavior is conservative until an App Store-safe downloadable package channel is separately designed. Orlix can still develop package build recipes and proof packages, but release does not become an unrestricted executable-code download surface.

`OrlixKit` is not a product component. If code, plans, docs, agents, or tests need the OS delivery/session surface, they target `OrlixOS`.

Virtio-rng follows the existing block and console work through upstream Linux `virtio-rng` and the hwrng core, with Orlix supplying only the private host entropy backend behind the virtio transport. Virtio-net follows after rng. External directory mounts follow after root/storage policy is stable.
