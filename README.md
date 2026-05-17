# Orlix

Orlix is an iOS-hosted upstream Linux port. The product goal is to run Linux userspace inside an iOS app by packaging upstream Linux as `OrlixKernel.xcframework` and pairing it with `OrlixMLibC`, an mlibc-based libc for Orlix Linux userspace.

Think of the app as the host container. It does not become Linux and it does not manage Linux through a custom runtime API. It starts a bootloader, the bootloader prepares Linux-shaped boot inputs, and Linux owns Linux after boot.

OrlixKernel is Linux. It does not provide a shell, libc, package manager, public syscall API, or fake runtime facade. Shells and packages are normal Orlix Linux userspace binaries linked against OrlixMLibC and executed through Linux mechanisms.

## ELI5 Start

Orlix has four important source areas:

- `Linux/upstream/linux-6.12` is generated upstream Linux source. Treat it as read-only input.
- `Linux/ports/orlix` is where durable Orlix Linux port inputs live.
- `OrlixMLibC` is the top-level component for the mlibc-based Orlix userspace C library.
- `OrlixHostAdapter` is where private iOS and Darwin mechanics live.

The old local kernel implementation under `OrlixKernel/fs`, `OrlixKernel/kernel`, and `OrlixKernel/runtime` is not the target architecture. Do not add new Linux subsystem behavior there. Useful behavior should be migrated by ownership into upstream Linux-native paths, then those directories should disappear.

## Proof Model

Proof is claim-promoted, not flat. Work may happen in parallel, but product claims must follow ADR 0017:

1. Kernel dependency proof
2. Kselftest kernel-interface proof
3. OrlixMLibC libc proof
4. OrlixMLibC-built syscall/UAPI proof
5. POSIX shell environment proof
6. Third-party package ladder: jq, curl, zsh

Orlix does not require `vmlinux` as a canonical build, proof, or runtime artifact. The canonical OrlixKernel proof artifact is the iOS app-hosted OrlixKernel integration that actually runs inside the Orlix app environment. A `vmlinux`-style artifact may exist only as an optional developer/debug artifact with a named consumer. It is not a milestone, not product proof, not runtime proof, not libc proof, and not required for installed UAPI headers.

KUnit proves kernel-internal behavior. Temporary, nolibc, or foreign-libc kselftest proves kernel-interface behavior only. mlibc tests prove OrlixMLibC. OrlixMLibC-built kselftests prove the libc-to-kernel syscall/UAPI path. Bash proves the first interactive POSIX shell environment. jq, curl, and zsh prove increasingly realistic third-party package compatibility.

Do not claim product runtime readiness from KUnit, temporary kselftest, boot logs, packaging, or a host-side harness.

## Build And Test Commands

The top-level Makefile keeps a small, Linux-shaped interface:

```bash
make setup-env
make build
make test
make test type=kunit,kselftest
make clean
```

`make setup-env` fetches upstream Linux and generates the disposable Xcode project from `project.yml`. `make build` builds the app-hosted OrlixKernel iOS artifact. It must not build or require `vmlinux` as a normal artifact.

`PROFILE=appstore` is the default profile. Pass another profile only when you intentionally need it.

The Linux-shaped lower-level targets are available when needed:

```bash
make prepare PROFILE=appstore
make headers_install PROFILE=appstore
make kunit PROFILE=appstore
make kselftest PROFILE=appstore libc=linux
make kselftest PROFILE=appstore libc=orlixmlibc ORLIX_MLIBC_SYSROOT=Build/OrlixMLibC/sysroot/appstore
```

`make prepare` materializes the generated upstream-plus-Orlix port tree and Kbuild output without requiring a standalone kernel image. `make headers_install` installs Linux UAPI headers into `Build/OrlixMLibC/kernel-headers/<profile>/include` and does not consume `vmlinux`.

`make kunit` builds Linux KUnit-selected Orlix test objects. KUnit proves kernel-internal behavior only.

`make kselftest libc=linux` uses Linux's kselftest install shape with a temporary foreign-libc sysroot under `Build/OrlixKernel/kselftest/temporary/<profile>/`. It is kernel-interface coverage only, not OrlixMLibC proof, Orlix userspace ABI proof, shell proof, package proof, or product runtime proof.

`make kselftest libc=orlixmlibc` uses a separate install path under `Build/OrlixMLibC/kselftest/<profile>/`. That lane requires an OrlixMLibC sysroot plus installed Orlix UAPI headers and is the later syscall/UAPI proof lane.

Do not run kselftest or KUnit on Darwin and do not use a VM as product proof. Do not add repo-local shell or standalone C contract tests as milestone proof. Linux kernel-internal behavior belongs in KUnit. Linux kernel-interface behavior belongs in selected kselftests. Orlix userspace ABI and product runtime claims require the later ADR 0017 proof lanes.

Both `iphoneos` and `iphonesimulator` are iOS proof destinations. Milestones must validate the same scope on both.

XCTest proof targets are organized under `Tests/XCTest/`. `OrlixKernelHostProofTests` launches the bootloader-shaped product API, `OrlixLinuxProofOutputParserTests` parses Linux-native KUnit and kselftest output fixtures, and `OrlixHostAdapterTests` covers narrow host mechanics. They do not own Linux subsystem assertions.

Milestone 5 boot-to-virtio-probe proof keeps the dependency chain honest. It verifies that generated profile DTS files describe virtio-mmio probe-shape devices, that the selected generated profile defconfig enables upstream OF, virtio-mmio, and virtio-block probe paths, and that Orlix architecture boot handoff state is covered by kernel dependency or kernel-interface proof.

Milestone 5 does not prove `/dev/vda`, `/dev/vdb`, virtio-block request I/O, host-backed disk persistence, initramfs loading, OverlayFS root assembly, or general userspace boot.

## Generated Trees

The pristine upstream source is generated at:

```text
Linux/upstream/linux-6.12
```

The disposable upstream-plus-Orlix port tree is generated at:

```text
Build/OrlixKernel/linux-6.12-port
```

If a change should survive regeneration, put it in `Linux/ports/orlix`, not in a generated tree.

## Port Inputs

Durable Orlix Linux port inputs live under:

```text
Linux/ports/orlix/
  overlay/
  patches/
  configs/
```

`overlay` contains files copied into Linux-native paths such as `arch/orlix` and `drivers/orlix`.

`patches` contains minimal upstream-tree deltas that cannot be represented as overlay files.

`configs` contains product profile defconfigs such as `appstore_defconfig` and `development_defconfig`.

## Product Surface

The public product surface is bootloader-shaped. It should expose a minimal boot entrypoint such as `OrlixBoot` with an app-level boot config. It must not expose syscall, file, mount, exec, task, cgroup, or runtime management APIs.

## Current Proof Boundary

The current blocking proof boundary is iOS-hosted kernel-interface execution. Real-artifact XCFramework packaging is a prerequisite, not product runtime proof.

Success at this boundary means the iOS host launches packaged OrlixKernel and collects dependency proof from the running kernel path, such as KUnit output, Linux-accurate no-init behavior, or selected kselftests through a temporary harness. It does not mean OrlixMLibC, Bash, jq, curl, zsh, or product runtime compatibility is proved.

## Device Direction

Orlix is virtio-first where Linux already has upstream device classes.

Use upstream Linux behavior for Linux-visible devices:

- `virtio_blk` for root disks
- `virtio_console` for the main console path
- `virtio-rng` for entropy
- `virtio_net` for networking
- virtio-fs first, or 9p over virtio if needed, for external directory mounts

Orlix-specific code supplies transport and backend mechanics under `drivers/orlix`, shaped as close to Linux virtio conventions as possible.

## Read Deeper

The canonical architecture specification is:

```text
docs/UPSTREAM_LINUX_IOS_PORT_SPEC.md
```

Architecture decisions are recorded under:

```text
docs/adr/
```

Glossary terms resolved during design live in:

```text
CONTEXT.md
```
