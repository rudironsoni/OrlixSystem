# ADR 0019: Keep Make Targets Linux-Shaped

## Status

Accepted

## Context

Orlix needs a repeatable repository command surface for preparing the Linux port tree, building the app-hosted OrlixKernel artifact, installing UAPI headers, and staging KUnit or kselftest artifacts.

Earlier wording and transitional scripts risked turning proof lane names, artifact paths, and milestone labels into public Make target names. That makes the command surface harder to learn, treats proof metadata as API, and drifts away from Linux and Kbuild conventions.

## Decision

Keep the top-level Makefile's public targets small and Linux-shaped. The top-level Makefile delegates to `OrlixKernel/Makefile`, `OrlixHostAdapter/Makefile`, `OrlixMLibC/Makefile`, and `OrlixTerminal/Makefile`. The normal public targets are `all`, `setup-env`, `build`, `test`, `prepare`, `scripts`, `dtbs`, `headers_install`, `kunit`, `kselftest`, `kselftest-install`, `xcodeproj`, `run`, `clean`, and `mrproper`.

Use variables for Orlix-specific scope. `PROFILE=appstore` selects the normal profile, `type=kunit,kselftest` selects test classes for `make test`, and `libc=linux` or `libc=orlixmlibc` selects the kselftest libc lane.

Proof labels are artifact metadata and log markers, not public Make targets. Labels such as `temporary-kselftest-kernel-interface` and `orlixmlibc-kselftest-syscall-uapi` describe what the generated output proves; they do not become command names.

Project Makefiles may use private implementation targets, but normal documentation and user workflows should point to the Linux-shaped public targets on the top-level Makefile.

Non-product investigations belong in ADRs or explicitly approved scratch space, not in normal Make targets or repository-owned probe directories.

## Consequences

Do not add public targets named after milestones, proof lanes, artifact paths, or generated staging directories when an existing Linux-shaped target plus variables can express the same scope.

Do not preserve old transitional target names as compatibility aliases unless a concrete external consumer requires them.

`make test` remains a selector over supported test classes, not a product-runtime proof claim. Product runtime claims still follow ADR 0017's promotion order and ADR 0018's app-hosted runtime proof requirement.

Documentation should describe proof labels as metadata/log markers and generated artifacts, not as commands users run directly.
