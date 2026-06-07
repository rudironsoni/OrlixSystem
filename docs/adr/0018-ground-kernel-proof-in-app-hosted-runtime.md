# ADR 0018: Ground Kernel Proof In The App-Hosted Runtime

## Status

Accepted

## Context

Earlier milestone wording treated a Kbuild `vmlinux` image as the first canonical OrlixKernel proof artifact. That overstates what matters for Orlix. The product does not boot a standalone Linux image in a VM-shaped environment. It runs an app-hosted OrlixKernel integration inside the iOS app boundary with `OrlixOS`, `OrlixHostAdapter/Sources`, and the iOS host app.

Linux-shaped proof means preserving Linux UAPI, syscall behavior, ABI discipline, kernel semantics, and Linux-native test output. It does not require Orlix to make `vmlinux` the default build, proof, or runtime artifact.

## Decision

Orlix does not require `vmlinux` as a canonical build, proof, or runtime artifact.

The canonical OrlixKernel proof artifact is the iOS app-hosted OrlixKernel integration that actually runs inside the Orlix app environment: OrlixKernel static library, framework, or object set plus `OrlixHostAdapter/Sources`, the `OrlixOS` session/payload surface where relevant, the iOS app or test host, and simulator/device execution.

Kernel proof must be based on that hosted runtime path. The proof question is always whether the Orlix app-hosted runtime executed the Linux-shaped behavior on iOS.

A `vmlinux`-style artifact may exist only as an optional developer/debug artifact with a named consumer. It is not a milestone, not product proof, not runtime proof, not libc proof, and not required for installed UAPI headers.

If no concrete workflow consumes `vmlinux`, do not generate it.

## Consequences

Do not claim that `vmlinux` building proves OrlixKernel works, proves Linux runtime behavior, proves userspace ABI behavior, or proves package compatibility.

Milestone proof must promote through the app-hosted runtime path: build OrlixKernel as the iOS-hosted runtime, launch it in the app or test host on simulator/device, reach Linux init handoff, fail Linux-accurately when no userspace exists, run KUnit or kernel-internal tests in the hosted kernel context where possible, run kselftest kernel-interface proof through the hosted runtime, then promote through OrlixMLibC, Bash, jq, curl, and zsh.

Existing docs or build targets that name `vmlinux` as required proof are superseded by this ADR and must be updated or treated as transitional tooling only.
