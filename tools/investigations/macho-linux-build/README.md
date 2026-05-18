# Mach-O Linux Build Investigations

These probes support ADR 0020's Mach-O-native OrlixKernel framework lane.

- `make macho-linux-build-probes PROFILE=appstore` builds the section probe, builds the initial Mach-O Linux archive, and records the `start_kernel()` dependency probe result.
- The section probe verifies that freestanding C objects can place data in explicit Mach-O sections used to model Linux section classes.
- The Linux object probe is the `build-linux-mach-o` lane: it compiles selected generated `arch/orlix` Linux source with `-nostdinc` and iOS Mach-O target triples.
- The framework symbol probe runs from the normal framework build and verifies that expected Linux arch symbols are present in `OrlixKernel.framework`.
- `macho_section_probe_compat.h` is used only by the `start_kernel()` dependency probe. It preserves generic `__section()` Linux section names under a dedicated `__ORLIX` Mach-O segment and maps the base percpu section to `__ORLIX,__percpu`, so the probe can separate generic section syntax from remaining direct section attributes and Mach-O's 16-byte section-name limit. It is not the final product section/linker policy.

The probes must not introduce hosted ELF loading, ELF-to-Mach-O conversion, fake `start_kernel()` implementations, or Darwin/libc headers in Linux-owned code.
