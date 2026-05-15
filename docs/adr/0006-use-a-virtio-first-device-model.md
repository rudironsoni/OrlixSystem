# ADR 0006: Use A Virtio-First Device Model

## Status

Accepted

## Context

Orlix needs virtual disks, console, entropy, networking, and external directory mounts. The old plan included custom Orlix drivers for several of these areas.

Linux already has upstream virtio device classes for many virtual-device needs.

## Decision

Orlix will use upstream Linux virtio device classes wherever they fit. Orlix-specific code supplies transport and backend mechanics. Upstream Linux drivers own Linux-visible device behavior.

## Consequences

Use `virtio_blk`, `virtio_console`, `virtio-rng`, `virtio_net`, virtio-fs, and 9p over virtio where appropriate.

Do not create custom Orlix block, network, random, console, or filesystem drivers unless an upstream virtio class cannot satisfy a concrete requirement.

Device behavior stays closer to Linux and reduces local subsystem rewrite risk.
