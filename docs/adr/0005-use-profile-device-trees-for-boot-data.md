# ADR 0005: Use Profile Device Trees For Boot Data

## Status

Accepted

## Context

Boot defaults need to vary by product profile. A custom Orlix boot-template file format would be easy to invent but would not match Linux practice.

Linux commonly represents machine description and chosen boot data with device tree plus kernel command line.

## Decision

Profile boot data will be represented as Linux-shaped device tree source under `arch/orlix/boot/dts` in the Orlix overlay. The bootloader supplies dynamic values and passes normal Linux boot data.

## Consequences

No custom `.boot` template format is introduced.

Profile DTS files become durable architecture-port inputs.

The bootloader remains a Linux boot preparer rather than a runtime configuration API.
