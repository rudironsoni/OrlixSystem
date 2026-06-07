# ADR 0010: Use Normal Linux Package And Execution Policy

## Status

Accepted, updated by ADR 0023.

## Context

App Store constraints create pressure to add custom execution approval metadata or an Orlix-specific execution policy layer.

Linux normally relies on package-manager trust, filesystem permissions, mount flags, MM behavior, and upstream security mechanisms.

## Decision

Orlix uses normal Linux package and execution mechanisms first. Apt/dpkg-style userspace verifies packages and installs files. Execution is then governed by normal Linux permissions, mount flags, MM behavior, and upstream security mechanisms.

## Consequences

No custom Orlix execution policy layer is introduced unless a concrete constraint cannot be represented with normal Linux mechanisms.

The release profile bundles curated OrlixOS distribution content as signed `OrlixOS` Kit/framework resources and updates executable content through app releases first. Downloaded binary repositories are deferred until a curated, signed, profile-approved channel is explicitly designed and reviewed.

Unavoidable iOS memory mechanics are adapted through `arch/orlix` host seams and `OrlixHostAdapter/Sources`, not virtio or package metadata in the kernel.
