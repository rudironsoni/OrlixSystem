# ADR 0020: Build Linux As Mach-O-Native OrlixKernel Framework

## Status

Accepted

## Context

OrlixKernel is the Linux kernel product that the iOS app hosts. The product artifact is `OrlixKernel.xcframework`, with `ios-arm64` and `ios-arm64-simulator` slices. Prior build wiring proved that upstream Linux Kbuild could produce ELF object output, but that output is not the product architecture for iOS.

iOS signs and links Mach-O binaries. The product cannot boot a standalone ELF kernel image, relocate an ELF kernel payload at runtime, convert `vmlinux.o` into Mach-O as the product architecture, or present `vmlinux`, `vmlinux.o`, or another ELF payload as the OrlixKernel product artifact.

## Decision

`OrlixKernel.xcframework` is the product kernel artifact. It contains upstream Linux plus the Orlix arch port compiled as Mach-O-native iOS code. The app does not boot a standalone ELF image. The framework links real Linux kernel code directly into the signed iOS binary, including `start_kernel()`, once the Mach-O-native Linux build is complete.

The product slices are `ios-arm64` and `ios-arm64-simulator`. The final product object target triples are `arm64-apple-ios` and `arm64-apple-ios-simulator`. Linux-owned source must still be compiled as freestanding kernel code, not Darwin userspace code.

Kbuild remains useful for source tree preparation, defconfig, generated headers, scripts, device trees, `headers_install`, and dependency discovery where feasible. Kbuild's ELF final link is not the product artifact.

The app-hosted entry flow is:

```text
OrlixBoot()
  -> OrlixBootHandoff()
  -> arch_boot_entry()
  -> real Mach-O-linked start_kernel()
```

During bring-up, `OrlixBoot()` may return `ORLIX_BOOT_STATUS_UNAVAILABLE` if a selected profile cannot yet satisfy the upstream Linux dependency chain. That is an app-hosted kernel dependency proof only. Runtime symbol lookup is a temporary bridge and must not become the final product design unless a specific Mach-O or framework constraint requires it. Keep `dlsym("start_kernel")` on the removal list: once the real upstream dependency chain and Mach-O section/linker policy can provide `start_kernel()`, use a direct symbol or an explicitly documented weak-symbol transition instead.

## Consequences

- Linux source remains generated upstream Linux under `Build/OrlixKernel/upstream` and `Build/OrlixKernel/src` plus Orlix overlay, patch, and config inputs under `OrlixKernel/Sources/ports/orlix`.
- The product link format is Mach-O, not ELF.
- Kbuild remains source, config, and generation truth where useful, but the product artifact is not `vmlinux`.
- Linux UAPI headers still come from standard `headers_install`.
- `OrlixKernel.framework` must not include Darwin, Foundation, POSIX host, libc, MLibC, musl, or glibc headers in Linux-owned code.
- Host mechanics stay behind `OrlixHostAdapter/Sources`.
- iOS slices must preserve one Linux userspace ABI.
- `vmlinux`, `vmlinux.o`, and ELF payloads may appear only in explicit non-product experiments.
- Product proof language remains limited to app-hosted kernel dependency proof until real `start_kernel()` executes in the iOS-hosted app environment.

## Section And Linker Work

Mach-O-native Linux is not a C-file recompilation exercise. Linux linker-section semantics must be represented explicitly before code paths can depend on them.

- `__init`: map to a Mach-O init text section with documented lifetime; reclaiming or discarding it is blocked until the app-hosted memory model can prove safe reclamation.
- `__initdata`: map to a Mach-O init data section; lifetime and clearing behavior are blocked until early memory ownership is defined.
- initcall sections: need deterministic Mach-O section names and linker-defined start/stop discovery equivalent to Linux initcall ordering.
- exitcall sections if relevant: do not silently drop them; either preserve ordered sections or document why the selected configuration cannot reach them.
- percpu sections: need a Mach-O representation plus an `arch/orlix` allocation and relocation model before SMP or percpu users can run.
- rodata: must remain read-only in the signed Mach-O product and must not be mixed with host-mutable data.
- exception tables: need Mach-O section preservation and lookup boundaries before fault-table users are enabled.
- alternatives if used: need explicit section preservation and patching policy, or those features must remain disabled for the slice.
- linker-defined start/stop symbols: require a Mach-O linker convention or generated symbol table; ELF `__start_*` and `__stop_*` assumptions cannot be copied blindly.
- alignment requirements: section and symbol alignment must be checked before dependent Linux code is enabled.
- BSS clearing: must be owned by the Mach-O app-hosted entry path or proven already handled for the linked slice.
- early memory layout: must be owned by `arch/orlix` and narrow host seams; it cannot be inferred from ELF image layout.

Unsupported section classes may be stubbed only when the selected first slice cannot reach them, and each stubbed class must be documented as a blocker.

`lib/cmdline.c` cannot enter the product archive yet because upstream Linux section/export metadata requires an accepted `arch/orlix` Mach-O section policy. Generic `__section` remapping and product-lane `__DISABLE_EXPORTS` are not accepted policy.

## Rejected Alternatives

- Hosted ELF image loader.
- Runtime relocation of an ELF kernel payload.
- ELF-to-Mach-O object conversion as product architecture.
- `vmlinux` as product artifact.
- `vmlinux.o` as product object set.
- Fake `start_kernel()`.
- Darwin-hosted kernel facade.

## Initial Build Slice

The first Mach-O-native lane prepares the generated Orlix Linux port tree from `Build/OrlixKernel/upstream`, `Build/OrlixKernel/src`, and `OrlixKernel/Sources/ports/orlix`, runs Kbuild preparation and DTB generation, compiles selected Linux-owned source from the generated tree with iOS Mach-O target triples, archives those objects as `Build/OrlixKernel/<profile>/<platform>/OrlixKernel.a`, links the matching archive into `OrlixKernel.framework`, and verifies exported arch boot symbols.

The current dependency lane includes real upstream `init/main.c` and the Orlix hosted-exec/syscall substrate. Remaining failures in this lane are normal upstream Linux dependency-closure work, not permission to add fake `start_kernel()` providers or boot-only package shortcuts.
