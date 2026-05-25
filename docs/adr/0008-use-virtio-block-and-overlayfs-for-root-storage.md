# ADR 0008: Use Virtio-Block And OverlayFS For Root Storage

## Status

Accepted, updated by ADR 0023.

## Context

The release profile needs reviewed bundled content, writable Linux state, cache separation, and Linux-compatible package/database behavior.

A single mutable image or host-path-backed root would blur storage roles and leak host mechanics.

## Decision

The release profile uses virtio-blk-backed Linux filesystem images. Upstream OverlayFS is the writable-root assembly mode, not the only valid root mode.

`/dev/vda` is the immutable bundled base image. `/dev/vdb` is the writable app-private state image. Initramfs can assemble a merged root with upstream OverlayFS; direct immutable-root and initramfs-only proof boots remain explicit Linux-shaped modes.

## Consequences

Writable state mirrors Linux paths such as `/etc`, `/var/lib`, `/home`, and package database locations.

Cache storage is separate from persistent state.

`/tmp` defaults to upstream Linux `tmpfs`.

Raw iOS paths are not Linux-visible truth.
