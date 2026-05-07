# Networking And Curl Plan

## Status

This milestone depends on the split plan and must preserve the Linux-owner versus host-owner boundary.

The network tranche is not allowed to convert host mechanism detail into accepted Linux-facing socket contract surface, and it must assume that host-private transport choices still need Linux-visible proof.

## Purpose

Provide networking behavior sufficient for `curl` as the secondary proof target.

## Goals

1. Make socket behavior reliable for `curl`.
2. Provide DNS and timeout semantics compatible with Linux-oriented client code.
3. Integrate readiness and file I/O behavior with the native package runtime.
4. Keep host transport realization private to `IXLandHostAdapter`.

## Required Surfaces

- socket creation and use
- connect and accept paths as needed
- send and receive behavior
- poll and select readiness interaction
- DNS resolution path
- timeout behavior
- IPv4 and IPv6 compatibility

## Deliverables

1. Socket semantics sufficient for `curl` configure, build, and runtime behavior.
2. DNS behavior integrated into the IXLand runtime model.
3. Timeout and error behavior suitable for real client workflows.

## Guardrails

### Do

- Use `curl` as a real runtime proof target, not just a build target.
- Preserve Linux-visible error and timeout behavior wherever practical.
- Keep host network-stack interaction behind the adapter seam.
- Treat any host-private networking implementation choice as provisional until `curl`-visible behavior proves it.
- Move any required cross-target networking declarations under kernel-owned private contracts.

### Don't

- Do not claim broad networking completion based only on unit tests.
- Do not surface host-network control as Linux semantics.
- Do not postpone DNS and timeout correctness if `curl` still depends on them.
- Do not call this milestone complete if socket-facing behavior still depends on an unproven host-private transport shortcut.

## Proof

1. `curl` configures unchanged.
2. `curl` builds unchanged.
3. HTTP and HTTPS GET work.
4. DNS lookup path works.
5. Timeout behavior is good enough for real use.
6. Host transport details remain private and are not promoted into the accepted Linux-facing networking contract.
