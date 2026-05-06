# Package Proof Program Plan

## Purpose

Define the package-driven proof program for Version 1.

This milestone turns the roadmap from subsystem completion into product verification.

## Principles

1. Real package behavior outranks narrow unit-test success.
2. Configure, build, install, and runtime are all required proof stages.
3. No target-package source modifications are allowed.
4. If a package breaks due to Linux-visible behavior differences, IXLand is wrong.

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

### Don't

- Do not count package source modification as success.
- Do not stop at configure-only or build-only success.
- Do not let a package-specific workaround hide a kernel or sysroot defect.

## Acceptance

The package proof program is successful only when:

1. `zsh` passes its proof ladder.
2. `curl` passes its proof ladder.
3. failures in follow-on packages are classified clearly and traced to owned roadmap milestones.
