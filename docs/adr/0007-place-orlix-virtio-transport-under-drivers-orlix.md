# ADR 0007: Place Orlix Virtio Transport Under drivers/orlix

## Status

Accepted

## Context

Upstream Linux virtio transports often live under `drivers/virtio`. Orlix also has an explicit Orlix driver subtree under `drivers/orlix`, with durable inputs stored in `OrlixKernel/Sources/ports/orlix/overlay/drivers/orlix`.

The transport is Orlix-specific, but it should remain close to Linux virtio conventions internally.

## Decision

The Orlix virtio transport lives under `drivers/orlix` in the generated Linux tree and under `OrlixKernel/Sources/ports/orlix/overlay/drivers/orlix` in durable source form, shaped as virtio-mmio where practical.

Boot-to-virtio-probe work stages the first virtio step as normal `virtio,mmio` profile device-tree shape and upstream OF/platform probing readiness. That stage is not allowed to claim virtio-block binding, `/dev/vda`, `/dev/vdb`, request I/O, or root assembly.

## Consequences

`OrlixKernel/Sources/ports/orlix/overlay/drivers/orlix` contains the durable Orlix-specific transport and backend integration.

Profile device trees should describe normal virtio-mmio-style devices where practical.

Upstream virtio device drivers remain the Linux-visible owners of block, console, entropy, and networking behavior.

Virtio root disks remain a later proof after the probe-shape milestone can run through the iOS-hosted Linux test path.
