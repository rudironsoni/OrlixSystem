# ADR 0009: Support Serial And Virtio Console Selection

## Status

Accepted

## Context

Orlix needs early boot diagnostics, fallback/debug console behavior, and a normal interactive virtual console path.

A single hardwired console would not match Linux boot expectations.

## Decision

Orlix supports both serial-style console behavior and upstream virtio-console. Boot-time selection follows normal Linux `console=` behavior.

## Consequences

The App Store profile enables both console paths.

The serial-style console is available for early, debug, or fallback use.

Virtio-console is the normal interactive direction where upstream Linux behavior fits.

`arch/orlix` code from `OrlixKernel/Sources/ports/orlix/overlay/arch/orlix` may provide a minimal early console before normal console drivers register.
