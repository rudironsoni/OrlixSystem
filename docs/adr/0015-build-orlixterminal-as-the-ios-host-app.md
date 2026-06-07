# ADR 0015: Build OrlixTerminal As The iOS Host App

## Status

Accepted

## Context

The iOS host app could be a blank XCTest host, a diagnostic harness, or the actual terminal-shaped app that users will recognize. Orlix needs iOS-hosted proof, but the product direction is Linux inside an iOS app with terminal interaction.

The MobileGhosttyApp example from `Lakr233/libghostty-spm` shows the intended app shape: a UIKit terminal app with a terminal view, navigation-hosted controller, theme handling, and app lifecycle integration.

## Decision

Create the iOS host app as `OrlixTerminal`, with app sources under `OrlixTerminal/Sources` and tests under `OrlixTerminal/Tests` when needed. It should follow the MobileGhosttyApp terminal-first shape rather than a blank proof-only host.

Depend on `libghostty-spm` for the terminal UI surface, theme handling, and app structure inspiration. Depend on `OrlixOS` for the delivered OS session and payload surface. Do not use `ShellCraftKit` as the execution backend. Orlix owns terminal bytes through Linux console/terminal plumbing.

## Consequences

`OrlixTerminal` is the iOS app that consumes `OrlixOS`, embeds the required frameworks, and launches the delivered OrlixOS session for proof and product development.

The initial app may follow the example's UIKit structure, terminal view, theme handling, and lifecycle shape, but the backend session must be Orlix-backed as soon as the Linux console path exists.

Before the Linux console path exists, `OrlixTerminal` may show non-interactive Orlix boot/proof logs. It must not use a fake shell, sandbox shell, or local execution backend to simulate product behavior.

Linux test output collection is separate from the interactive terminal byte stream. The terminal may display logs when available, but XCTest proof privately captures KUnit output from the kernel log path and kselftest output from test-initramfs stdout.

XCTest should target `OrlixTerminal` for iOS-hosted Orlix launch, Linux test-output collection, and terminal/host integration proof. Test code belongs under the owning project `Tests` tree.

The terminal app UI must not become a public Linux management API or OS delivery layer. Linux behavior still belongs inside Orlix Linux, the delivered OS/session surface belongs to `OrlixOS`, and the app acts as host, terminal surface, and proof harness.
