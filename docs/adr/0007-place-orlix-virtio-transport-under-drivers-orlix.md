# ADR 0007: Place Orlix Virtio Transport Under drivers/orlix

## Status

Accepted

## Context

Upstream Linux virtio transports often live under `drivers/virtio`. Orlix also has an explicit Orlix driver subtree under `drivers/orlix`.

The transport is Orlix-specific, but it should remain close to Linux virtio conventions internally.

## Decision

The Orlix virtio transport lives under `drivers/orlix`, shaped as virtio-mmio where practical.

## Consequences

`drivers/orlix` contains the Orlix-specific transport and backend integration.

Profile device trees should describe normal virtio-mmio-style devices where practical.

Upstream virtio device drivers remain the Linux-visible owners of block, console, entropy, and networking behavior.
