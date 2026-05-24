# ADR 0022: Use Hosted Linux ELF Execution

## Status

Accepted

## Context

Orlix has two different products that must not be collapsed into one runtime facade:

- `OrlixKernel` is upstream Linux plus the `arch/orlix` port, compiled into the iOS-hosted kernel product.
- `OrlixMLibC` is the libc for Orlix Linux userspace and tracks upstream mlibc.

The iOS app and XNU/Darwin remain the physical host environment. That does not make Darwin the Orlix userspace ABI. Orlix userspace must see Linux UAPI and Linux syscall behavior owned by `OrlixKernel`.

The host is iOS only, app-sandboxed, and privately mediated through Orlix-owned seams. Host constraints are implementation inputs, not the Linux userspace contract.

## Decision

The primary userspace and package format is Linux ELF for AArch64, linked against OrlixMLibC. OrlixMLibC uses upstream mlibc's Linux sysdeps model and standard Linux UAPI headers installed from `OrlixKernel`; it does not define a custom Orlix application ABI.

Linux ELF text executes as native AArch64 code inside the hosted Orlix process. Orlix-built libc syscall stubs call a private hosted syscall gate instead of emitting a Darwin-routed `svc #0`. The gate enters the `arch/orlix` syscall path, which dispatches through Linux syscall numbers and Linux syscall implementations.

Orlix-built AArch64 Linux user code reserves Apple arm64's host platform register `x18`. This is a hosted-execution code generation constraint, not a new userspace syscall ABI: Linux UAPI layouts, syscall numbers, calling shapes, errno behavior, and mlibc `sysdeps/linux` semantics remain the Linux contract.

The execution stack is:

```text
Linux ELF / Orlix userspace code
  syscall boundary
Orlix hosted-exec layer
  enters OrlixKernel syscall path
OrlixKernel
  owns Linux semantics
OrlixHostAdapter
  supplies private iOS mechanics when needed
XNU/Darwin
  remains the physical host kernel
```

`OrlixHostAdapter` may provide host mechanics such as executable mapping, page protection changes, host thread primitives, timers, exception transport, and app lifecycle notification. It must not decode Linux syscall numbers, own Linux syscall policy, or expose Darwin/POSIX/libc contracts to Linux-owned kernel code.

Processes, signals, process groups, sessions, and similar execution semantics are virtualized inside `OrlixKernel` when iOS cannot provide equivalent private mechanics. Device-like mediation should use upstream virtio classes and Orlix virtio transport/backend code wherever possible.

The hosted-exec substrate is internal to `arch/orlix` and must be designed so future execution engines can be added behind the same kernel-owned boundary. TCTI JIT-less ISA-on-ISA execution and WASM support are deferred. They must not change the primary Linux ELF ABI, OrlixMLibC's Linux sysdeps contract, or the OrlixKernel syscall semantics.

## Consequences

- Packages such as Bash are rebuilt as Orlix Linux ELF AArch64 binaries linked against OrlixMLibC.
- Raw precompiled Linux binaries that issue `svc #0` directly are compatibility work, not the first product runtime.
- `execve()` and `binfmt_elf` remain the long-term Linux loading model.
- Orlix kselftests build against OrlixMLibC and exercise the same syscall gate as packages.
- No public Orlix syscall facade is added to the product API.
- OrlixKernel must not include OrlixMLibC, mlibc, glibc, musl, Darwin, POSIX, Foundation, or Apple SDK headers.

## Rejected Alternatives

- Compiling packages as iOS Mach-O app code as the primary runtime format.
- Routing Linux userspace syscalls to Darwin `svc #0`.
- Adding a custom Orlix Linux-like userspace ABI.
- Embedding libc or shell behavior in OrlixKernel.
- Treating TCTI or WASM as the initial package runtime.
