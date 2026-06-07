# Orlix

Orlix is an iOS-hosted upstream Linux port. The product goal is to run Linux userspace inside an iOS app by packaging upstream Linux as `OrlixKernel.xcframework` and pairing it with `OrlixMLibC`, an mlibc-based libc for Orlix Linux userspace.

Think of the app as the host container. It does not become Linux and it does not manage Linux through a custom runtime API. It starts a bootloader, the bootloader prepares Linux-shaped boot inputs, and Linux owns Linux after boot.

OrlixKernel is Linux. It does not provide a shell, libc, package manager, public syscall API, or fake runtime facade. Shells and packages are normal Orlix Linux userspace binaries linked against OrlixMLibC and executed through Linux mechanisms.

## ELI5 Start

Orlix has five important source areas:

- `OrlixKernel/Sources/upstream/linux-6.12` is generated upstream Linux source. Treat it as read-only input.
- `OrlixKernel/Sources/ports/orlix` is where durable Orlix Linux port inputs live.
- `OrlixMLibC/Sources` is the durable component area for OrlixMLibC sysdeps, configs, and patches. The upstream mlibc checkout is generated under `Build/OrlixMLibC/upstream/mlibc`.
- `OrlixOS` is the package and rootfs assembly lane for Orlix Linux userspace inputs. It builds package artifacts against OrlixMLibC and stages generated rootfs content under `Build/OrlixOS`.
- `OrlixHostAdapter/Sources` is where private iOS and Darwin mechanics live.

Project source and test roots are organized consistently:

```text
OrlixKernel/Sources
OrlixKernel/Tests
OrlixHostAdapter/Sources
OrlixHostAdapter/Tests
OrlixMLibC/Sources
OrlixMLibC/Tests
OrlixOS
OrlixTerminal/Sources
OrlixTerminal/Tests
```

Each project has its own `Makefile`; the top-level `Makefile` orchestrates calls into those project Makefiles.

The legacy local-kernel prototype has been retired from the tracked source tree. It is not the target architecture and is not product proof. Do not restore `LegacyOrlix/`, `OrlixKernel/fs`, `OrlixKernel/kernel`, or `OrlixKernel/runtime`. Useful behavior belongs by ownership in upstream Linux-native paths.

## Proof Model

Proof is claim-promoted, not flat. Work may happen in parallel, but product claims must follow ADR 0017:

1. Kernel dependency proof
2. Kselftest kernel-interface proof
3. OrlixMLibC libc proof
4. OrlixMLibC-built syscall/UAPI proof
5. POSIX shell environment proof
6. Third-party package ladder: jq, curl, zsh

Orlix does not require `vmlinux` as a canonical build, proof, or runtime artifact. The canonical OrlixKernel proof artifact is the iOS app-hosted OrlixKernel integration that actually runs inside the Orlix app environment. A `vmlinux`-style artifact may exist only as an optional developer/debug artifact with a named consumer. It is not a milestone, not product proof, not runtime proof, not libc proof, and not required for installed UAPI headers.

KUnit proves kernel-internal behavior. OrlixMLibC-built kselftests prove Linux kernel-interface and libc-to-kernel syscall/UAPI behavior. mlibc tests prove OrlixMLibC. Bash proves the first interactive POSIX shell environment. jq, curl, and zsh prove increasingly realistic third-party package compatibility.

Do not claim product runtime readiness from KUnit, kselftest, boot logs, packaging, or a host-side harness.

## Build And Test Commands

The top-level Makefile keeps a small, Linux-shaped interface and delegates to `OrlixKernel/Makefile`, `OrlixHostAdapter/Makefile`, `OrlixMLibC/Makefile`, `OrlixOS/Makefile`, and `OrlixTerminal/Makefile`:

```bash
make help
make setup-env
make build
make test
make test type=kunit,kselftest
make clean
```

`make setup-env` fetches upstream Linux and generates the disposable Xcode project from `project.yml`. `make build` first runs `make clean`, which removes generated outputs including `OrlixKernel/Sources/upstream/linux-6.12`; the kernel build then reclones upstream Linux through the normal bootstrap path. The same build flow materializes upstream mlibc under `Build/OrlixMLibC/upstream/mlibc`, builds the OrlixMLibC sysroot from upstream mlibc plus durable OrlixMLibC inputs, and stages OrlixOS package/rootfs inputs under `Build/OrlixOS`. It does not prove terminal runtime behavior or build or require `vmlinux` as a normal artifact.

The Linux compile lane emits per-profile, per-platform OrlixKernel static archives under `Build/OrlixKernel/<profile>/<platform>/OrlixKernel.a`. Xcode links the matching archive into `OrlixKernel.framework`, and framework slices are packaged into `OrlixKernel.xcframework`.

`PROFILE=release` is the default profile. Pass another profile only when you intentionally need it.

Use variables for scope instead of target-name variants: `PROFILE=...`, `type=...`, and `libc=...`. Proof labels are emitted inside artifacts and logs; they are not public Make targets.

The Linux-shaped lower-level targets are available when needed:

```bash
make prepare PROFILE=release
make headers_install PROFILE=release
make kunit PROFILE=release
make kselftest PROFILE=release
make kselftest PROFILE=release libc=orlixmlibc
```

`make prepare` materializes the generated upstream-plus-Orlix port tree and Kbuild output without requiring a standalone kernel image. `make headers_install` installs Linux UAPI headers into `Build/OrlixMLibC/kernel-headers/<profile>/include` and does not consume `vmlinux`.

`make kunit` currently builds Linux KUnit-selected Orlix test objects. That is useful dependency evidence, not iOS-hosted KUnit execution proof. KUnit proves kernel-internal behavior only after the hosted Linux proof path runs it and emits Linux-owned KUnit output.

`make kselftest` uses OrlixMLibC-built kselftests under `Build/OrlixMLibC/kselftest/<profile>/`. That lane requires an OrlixMLibC sysroot plus installed Orlix UAPI headers and is the syscall/UAPI proof lane.

Do not run kselftest or KUnit on Darwin and do not use a VM as product proof. Do not add repo-local shell or standalone C contract tests as milestone proof. Linux kernel-internal behavior belongs in KUnit. Linux kernel-interface behavior belongs in selected kselftests. Orlix userspace ABI and product runtime claims require the later ADR 0017 proof lanes.

Both `iphoneos` and `iphonesimulator` are iOS proof destinations. Milestones must validate the same scope on both.

XCTest suites are organized under project-local test trees. `OrlixKernel/Tests/XCTest/OrlixKernelHostProofTests` launches the bootloader-shaped product API, `OrlixKernel/Tests/XCTest/OrlixLinuxProofOutputParserTests` parses Linux-native KUnit and kselftest output fixtures under `OrlixKernel/Tests/Fixtures`, and `OrlixHostAdapter/Tests/XCTest/OrlixHostAdapterTests` covers narrow host mechanics. Future OrlixMLibC and OrlixTerminal tests belong under `OrlixMLibC/Tests` and `OrlixTerminal/Tests`. They do not own Linux subsystem assertions.

Milestone 5 boot-to-virtio-probe proof keeps the dependency chain honest. Static DTS, defconfig, and kselftest source inputs are preparatory only. The milestone is proved only when iOS-hosted Orlix Linux consumes the profile device tree and reaches the point where upstream virtio-mmio probing can be attempted.

Milestone 5 does not prove `/dev/vda`, `/dev/vdb`, virtio-block request I/O, host-backed disk persistence, initramfs loading, OverlayFS root assembly, or general userspace boot.

## Generated Trees

The pristine upstream source is generated at:

```text
OrlixKernel/Sources/upstream/linux-6.12
```

The disposable upstream-plus-Orlix port tree is generated at:

```text
Build/OrlixKernel/linux-6.12-port
```

The upstream mlibc checkout used by OrlixMLibC builds is generated at:

```text
Build/OrlixMLibC/upstream/mlibc
```

If a kernel change should survive regeneration, put it in `OrlixKernel/Sources/ports/orlix`, not in a generated tree. If an mlibc change should survive regeneration, put it in durable OrlixMLibC inputs such as `OrlixMLibC/Sources/patches`, not in the generated upstream mlibc checkout.

## Port Inputs

Durable Orlix Linux port inputs live under:

```text
OrlixKernel/Sources/ports/orlix/
  overlay/
  patches/
  configs/
```

`overlay` contains files copied into Linux-native paths such as `arch/orlix` and `drivers/orlix`.

`patches` contains minimal upstream-tree deltas that cannot be represented as overlay files.

`configs` contains product profile defconfigs such as `release_defconfig` and `development_defconfig`.

## Product Surface

The public product surface is bootloader-shaped. It should expose a minimal boot entrypoint such as `OrlixBoot` with an app-level boot config. It must not expose syscall, file, mount, exec, task, cgroup, or runtime management APIs.

## Current Proof Boundary

The current blocking proof boundary is iOS-hosted kernel-interface execution. The branch is source-layout and build-hook aligned, not runtime aligned. Real-artifact XCFramework packaging is a prerequisite, not product runtime proof.

Success at this boundary means the iOS host launches packaged OrlixKernel and collects dependency proof from the running kernel path, such as KUnit output, Linux-accurate no-init behavior, or selected OrlixMLibC-built kselftests. It does not mean Bash, jq, curl, zsh, or product runtime compatibility is proved.

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
docs/architecture/ORLIX_UPSTREAM_LINUX_IOS_PORT.md
```

Architecture decisions are recorded under:

```text
docs/adr/
```

Glossary terms resolved during design live in:

```text
docs/reference/ORLIX_GLOSSARY.md
```
