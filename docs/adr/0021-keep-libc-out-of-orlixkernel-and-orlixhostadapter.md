# ADR 0021: Keep Libc Out Of OrlixKernel And OrlixHostAdapter

## Status

Accepted

## Context

Orlix has three separate ownership domains: the Linux kernel product, the private iOS host adapter, and the userspace libc. Collapsing those domains would make the kernel less Linux-shaped and would hide userspace ABI behavior behind host or product-framework shortcuts.

`OrlixKernel` is upstream Linux plus the Orlix Linux port, built as the app-hosted kernel artifact. It must remain freestanding kernel code. `OrlixHostAdapter` is a narrow bridge for private iOS and Darwin mechanics needed by `arch/orlix` and Linux-native Orlix drivers. `OrlixMLibC` is the only libc component and is responsible for Orlix Linux userspace libc behavior.

## Decision

Do not put a userspace libc in `OrlixKernel` or `OrlixHostAdapter`.

`OrlixKernel` must not include, link, implement, or depend on OrlixMLibC, mlibc, musl, glibc, Darwin libc compatibility layers, libc sysdeps, libc startup code, dynamic-loader behavior, pthreads, stdio, malloc-family userspace allocators, resolver behavior, locale, errno wrappers, signal wrappers, package runtime behavior, or shell/runtime facade behavior.

`OrlixHostAdapter` must not become a libc backend, syscall translator, userspace runtime, package facade, dynamic loader, or ABI compatibility layer. It may expose only narrow host mechanics needed by Linux-owned `arch/orlix` and `drivers/orlix` code, such as clocks, timers, execution substrate, low-level memory mapping, lifecycle notification, very-early entropy, virtio transport, and backend mechanics.

`OrlixMLibC` remains a separate top-level component. It consumes Linux UAPI only through `headers_install` output and calls Linux-shaped syscalls. It is the only place for libc sysdeps and libc compatibility work.

Linux kernel internal helper sources are not libc ownership. They may be added to Mach-O-native kernel builds only as real upstream Linux kernel dependencies, never as a libc substitute, and only after auditing header dependencies, exported symbols, and collisions with host or framework symbols.

## Consequences

- Linux-owned kernel code must compile freestanding and must not include Apple SDK, Darwin, Foundation, POSIX host, libc, OrlixMLibC, mlibc, musl, or glibc headers.
- Host adapter code must not provide Linux userspace ABI behavior or libc services.
- Kernel bring-up must resolve missing dependencies through upstream Linux and `arch/orlix` ownership, not through libc-shaped stubs.
- OrlixMLibC proof remains separate from OrlixKernel proof.
- App-hosted kernel dependency proof does not prove libc, syscall/UAPI, shell, or package behavior.
- Any kernel object that defines common C runtime symbol names, such as `memcpy`, `memset`, `memmove`, `strlen`, `strcmp`, `strncmp`, `strlcpy`, or `strscpy`, requires explicit symbol/export policy review before product-link claims.

## Rejected Alternatives

- Linking OrlixMLibC, mlibc, musl, glibc, or another libc into `OrlixKernel`.
- Implementing libc sysdeps or dynamic-loader behavior in `OrlixHostAdapter`.
- Translating Linux syscalls into host libc calls as the kernel or host boundary.
- Adding libc-shaped stubs to make Mach-O kernel objects compile.
- Treating app-hosted XCTest or simulator launch as libc proof.
