# Package Proof Program Plan

## Purpose

Define the package-driven proof program for Version 1.

This milestone turns the roadmap from subsystem completion into product verification.

It also verifies the split outcome indirectly:

- package success must come from Linux-shaped behavior
- package success does not excuse hidden host-private behavioral divergence
- package success does not excuse overly broad adapter dependency exposure or accidental reintroduction of adapter-owned kernel-facing contracts

## Principles

1. Real package behavior outranks narrow unit-test success.
2. Configure, build, install, and runtime are all required proof stages.
3. No target-package source modifications are allowed.
4. If a package breaks due to Linux-visible behavior differences, IXLand is wrong.
5. Passing package proof does not retroactively bless bad host-private implementation choices that only happened to satisfy one narrow test shape.

## Package Order

### Gate 1

- `zsh`

### Gate 2

- `curl`

### Follow-on proof set

- `busybox`
- `grep`
- `sed`
- `gawk`
- `make`

## Required Proof Stages Per Package

1. Configure unchanged
2. Build unchanged
3. Install unchanged
4. Smoke runtime
5. Failure classification back to owning subsystem when something breaks

## zsh Proof Ladder

1. configure
2. build
3. install
4. `zsh -c 'echo hello'`
5. startup file handling
6. redirections and pipelines
7. `sleep 1 & wait`
8. PTY and job control
9. shebang-driven script execution

## curl Proof Ladder

1. configure
2. build
3. install
4. HTTP GET
5. HTTPS GET
6. DNS lookup path
7. timeout behavior
8. redirected output and file handling

## Guardrails

### Do

- Treat package proof as authoritative product proof.
- Use blockers to drive subsystem ownership decisions.
- Keep package patches out of scope unless the issue is unrelated to IXLand semantics.
- Use package failures to push ownership back toward Linux-shaped contracts rather than preserving branded seam convenience.
- Use package failures to push declaration ownership back into kernel-owned private contracts rather than adapter-owned seams.

### Don't

- Do not count package source modification as success.
- Do not stop at configure-only or build-only success.
- Do not let a package-specific workaround hide a kernel or sysroot defect.
- Do not let package success be used to declare the split complete while branded host-adapter vocabulary remains part of the normal kernel-facing seam.

## Acceptance

The package proof program is successful only when:

1. `zsh` passes its proof ladder.
2. `curl` passes its proof ladder.
3. failures in follow-on packages are classified clearly and traced to owned roadmap milestones.
4. proof narratives do not describe branded host-adapter vocabulary as an acceptable Linux-facing end-state.
