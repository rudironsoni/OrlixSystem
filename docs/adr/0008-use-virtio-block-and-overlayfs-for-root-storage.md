# ADR 0008: Use Virtio-Block And OverlayFS For Root Storage

## Status

Accepted

## Context

The App Store profile needs reviewed bundled content, writable Linux state, cache separation, and Linux-compatible package/database behavior.

A single mutable image or host-path-backed root would blur storage roles and leak host mechanics.

## Decision

The App Store profile uses virtio-blk-backed Linux filesystem images and upstream OverlayFS.

`/dev/vda` is the immutable bundled base image. `/dev/vdb` is the writable app-private state image. Initramfs assembles the merged root with upstream OverlayFS.

## Consequences

Writable state mirrors Linux paths such as `/etc`, `/var/lib`, `/home`, and package database locations.

Cache storage is separate from persistent state.

`/tmp` defaults to upstream Linux `tmpfs`.

Raw iOS paths are not Linux-visible truth.
