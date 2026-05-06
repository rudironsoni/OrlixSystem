# Networking And Curl Plan

## Purpose

Provide networking behavior sufficient for `curl` as the secondary proof target.

## Goals

1. Make socket behavior reliable for `curl`.
2. Provide DNS and timeout semantics compatible with Linux-oriented client code.
3. Integrate readiness and file I/O behavior with the native package runtime.

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

### Don't

- Do not claim broad networking completion based only on unit tests.
- Do not surface host-network control as Linux semantics.
- Do not postpone DNS and timeout correctness if `curl` still depends on them.

## Proof

1. `curl` configures unchanged.
2. `curl` builds unchanged.
3. HTTP and HTTPS GET work.
4. DNS lookup path works.
5. Timeout behavior is good enough for real use.
