# Orlix OCI-Derived Linux Environments And Virtio Device Plane Plan

Planning only. This plan was created from repository inspection for OrlixSystem.

## Current Status Reconciliation

The original repository assessment below is historical. It accurately described
the repo when the plan was first written, but later implementation entries prove
that several formerly missing pieces now exist. Agents must reconcile this plan
against source, tests, and `IMPLEMENT.md` before selecting work.

Current proved state:

- OrlixOS environment descriptors exist.
- Rootfs tar import exists.
- OCI layout import exists.
- OCI whiteout and opaque-directory handling exist.
- Materialized-root iOS Simulator proofs exist.
- Descriptor argv/env/cwd/uid/gid execution defaults exist.
- Immutable base plus writable state proof exists.
- OverlayFS copy-up and unlink proof exists.
- pseudoFS, tmpfs, PTY, and delayed input proofs exist.
- Linux oracle scaffold exists.

Current not-proved state:

- Product-shaped named environment entry API.
- Multiple live environments inside one already-running OrlixKernel.
- Cross-boot persistence of the same mutated environment state image.
- Runtime host-folder mount backend.
- virtio-fs.
- Registry pull.
- Product `orlix run`.
- OCI Runtime Spec lifecycle compliance.
- Truthful OCI feature report.
- virtio-net, `/proc/net`, rtnetlink.
- Virtual cgroup v2.
- Native performance benchmark ladder for imported binaries.

The stale historical gaps below that say no tar importer, OCI importer,
whiteout tests, overlay tests, or oracle tooling exists are superseded by this
reconciliation and by the current `IMPLEMENT.md` evidence.

## 1. Repository State Assessment

### OrlixKernel Layout

Current OrlixKernel is an upstream Linux port, not the retired local kernel prototype.

Relevant paths:

- `OrlixKernel/Sources/ports/orlix/overlay/arch/orlix`
- `OrlixKernel/Sources/ports/orlix/overlay/drivers/orlix`
- `OrlixKernel/Sources/ports/orlix/configs`
- `OrlixKernel/Sources/ports/orlix/kbuild`
- `OrlixKernel/Sources/boot`
- `OrlixKernel/Sources/include`

The active kernel-facing implementation lives under the upstream Linux port overlay. The retired local-kernel paths such as `OrlixKernel/fs`, `OrlixKernel/kernel`, and `OrlixKernel/runtime` are absent from the active implementation and must not be restored.

Important current files:

- `OrlixKernel/Sources/ports/orlix/overlay/arch/orlix/kernel/syscall.c` owns syscall dispatch into the upstream Linux syscall table.
- `OrlixKernel/Sources/ports/orlix/overlay/arch/orlix/kernel/hosted_exec.c` owns hosted userspace trap and syscall entry handling.
- `OrlixKernel/Sources/ports/orlix/overlay/arch/orlix/kernel/process.c` owns Orlix arch process glue such as `copy_thread` and `start_thread`.
- `OrlixKernel/Sources/ports/orlix/overlay/arch/orlix/kernel/signal.c` owns Orlix arch signal frame handling.
- `OrlixKernel/Sources/ports/orlix/overlay/arch/orlix/mm` owns hosted mapping and fault integration.
- `OrlixKernel/Sources/ports/orlix/overlay/drivers/orlix/virtio/mmio.c` implements the current Orlix virtio-mmio backend.
- `OrlixKernel/Sources/ports/orlix/overlay/drivers/orlix/block/file.c` and `block/image.c` are skeleton-style registration files, not a complete block storage model.

Current kernel configuration already enables many Linux primitives needed for the target architecture:

- `CONFIG_BINFMT_ELF=y`
- `CONFIG_BINFMT_SCRIPT=y`
- `CONFIG_PROC_FS=y`
- `CONFIG_SYSFS=y`
- `CONFIG_DEVTMPFS=y`
- `CONFIG_DEVTMPFS_MOUNT=y`
- `CONFIG_TMPFS=y`
- `CONFIG_EXT4_FS=y`
- `CONFIG_OVERLAY_FS=y`
- `CONFIG_NAMESPACES=y`
- `CONFIG_UTS_NS=y`
- `CONFIG_IPC_NS=y`
- `CONFIG_USER_NS=y`
- `CONFIG_PID_NS=y`
- `CONFIG_NET_NS=y`
- `CONFIG_CGROUPS=y`
- `CONFIG_VIRTIO=y`
- `CONFIG_VIRTIO_MMIO=y`
- `CONFIG_VIRTIO_BLK=y`
- `CONFIG_VIRTIO_CONSOLE=y`
- `CONFIG_HW_RANDOM_VIRTIO=y`

Evidence files:

- `OrlixKernel/Sources/ports/orlix/configs/release_defconfig`
- `OrlixKernel/Sources/ports/orlix/configs/development_defconfig`

### OrlixHostAdapter Layout

Current OrlixHostAdapter is private host mediation code.

Relevant paths:

- `OrlixHostAdapter/Sources/OrlixHostAdapter/boot`
- `OrlixHostAdapter/Sources/OrlixHostAdapter/memory`
- `OrlixHostAdapter/Sources/OrlixHostAdapter/runtime`
- `OrlixHostAdapter/Sources/OrlixHostAdapter/terminal`

Important current files:

- `OrlixHostAdapter/Sources/OrlixHostAdapter/boot/resources.c` selects bundled base/state block images and stores writable state under app-private Application Support.
- `OrlixHostAdapter/Sources/OrlixHostAdapter/memory/kernel_mapping.c` owns hosted executable mapping mechanics.
- `OrlixHostAdapter/Sources/OrlixHostAdapter/runtime/trap.c` owns host trap handling.
- `OrlixHostAdapter/Sources/OrlixHostAdapter/runtime/entropy.c`, `time.c`, and `thread.c` own host randomness, time, and thread primitives.
- `OrlixHostAdapter/Sources/OrlixHostAdapter/terminal/console.c` owns host terminal input/output mechanics.

This layer uses Darwin, CoreFoundation, pthread, Mach, and POSIX host APIs. That is acceptable only inside OrlixHostAdapter.

### VFS Implementation

There is no active Orlix-local VFS implementation in durable source. Active VFS behavior is upstream Linux VFS.

Evidence:

- No active `OrlixKernel/fs` local VFS tree exists.
- `OrlixKernel/Sources/ports/orlix/kbuild/kernel-rules.mk` selects upstream Linux filesystem objects, including procfs, sysfs, devpts, ext4, overlayfs, tmpfs, and devtmpfs support.
- `OrlixOS/Sources/init/rootinit.c` mounts ext4 and OverlayFS using normal Linux mount syscalls.
- `OrlixOS/Sources/init/init.c` mounts procfs, sysfs, devtmpfs, devpts, tmpfs, and starts `/bin/sh`.

Current conclusion: Orlix already points in the correct direction by using upstream Linux VFS. The missing part is not a new Orlix VFS. The missing part is environment-level storage, root selection, mount namespace proof, external host-backed mounts, OCI import binding, and virtio-fs/net/vsock expansion.

### Current Root Bootstrap And Storage Policy

Current boot/root policy is already image-backed and mostly Linux-shaped.

Evidence:

- `OrlixOS/Sources/distribution/target-settings.xcconfig`
- `OrlixOS/Sources/make/rootfs.mk`
- `OrlixOS/Sources/init/rootinit.c`
- `OrlixOS/Sources/init/init.c`
- `OrlixHostAdapter/Sources/OrlixHostAdapter/boot/resources.c`

Current behavior:

- Release root uses `/dev/vda` as an ext4 root image.
- Development root uses initramfs and OverlayFS over `/dev/vda` plus `/dev/vdb`.
- Base image is bundled in the OrlixOS payload.
- Writable state image is copied into `~/Library/Application Support/Orlix/...` by HostAdapter.
- `/tmp` is mounted as Linux `tmpfs` by `OrlixOS/Sources/init/init.c`.
- `/run` and `/dev/shm` are mounted as Linux `tmpfs`.
- There is no verified Documents mount.
- There is no verified external security-scoped mount.
- There is no per-environment image registry.

Important correction for future work: Linux-visible `/tmp` should remain Linux `tmpfs`. Host temporary storage may be used for importer scratch, but it must not become Linux truth.

### Current Mount Model

Current mount model is upstream Linux mounts.

Evidence:

- `OrlixOS/Sources/init/rootinit.c` uses `mount("devtmpfs")`, `mount("proc")`, `mount("sysfs")`, `mount("ext4")`, and `mount("overlay")`.
- `OrlixOS/Sources/init/init.c` mounts `devtmpfs`, `proc`, `sysfs`, `devpts`, `tmpfs`, and optional `selinuxfs`.
- Kernel configs enable namespaces and filesystems in `release_defconfig` and `development_defconfig`.

Missing:

- No named environment mount namespace orchestration exists.
- No current proof that named environment root binding and per-environment mount isolation work in product tests.
- No current external folder mount path exists.
- No current virtio-fs mount path exists.

### Current Process And Task Model

Current process/task behavior is upstream Linux plus Orlix arch glue.

Evidence:

- `OrlixKernel/Sources/ports/orlix/overlay/arch/orlix/kernel/process.c`
- `OrlixKernel/Sources/ports/orlix/overlay/arch/orlix/kernel/hosted_exec.c`
- `OrlixKernel/Sources/ports/orlix/overlay/arch/orlix/kernel/syscall.c`
- `OrlixOS/Sources/init/init.c`, which uses `fork`, `setsid`, `ioctl(TIOCSCTTY)`, `execve`, `waitpid`, and `poll`.

Partial state:

- Normal Orlix-built Linux userspace execution exists.
- ELF support is enabled through upstream Linux `CONFIG_BINFMT_ELF`.
- Full imported binary compatibility for arbitrary OCI image binaries is not proven by the current tests.

### Current fdtable Model

fdtable behavior is upstream Linux-owned. There is no active Orlix-local fdtable implementation.

Evidence:

- No active local fdtable implementation exists in durable Orlix source.
- `OrlixOS/Sources/init/init.c` exercises normal Linux fd behavior through `open`, `dup2`, `read`, `write`, `poll`, and `waitpid`.
- Terminal and proof tests exercise PTY/fd behavior indirectly.

Missing:

- No dedicated fdtable conformance test matrix exists for imported environments.
- No current oracle diff covers fd inheritance, close-on-exec, pipe readiness, dup semantics, or poll/epoll edge cases.

### Current Syscall Dispatch Model

Syscall dispatch is Orlix arch code calling upstream Linux syscall entries.

Evidence:

- `OrlixKernel/Sources/ports/orlix/overlay/arch/orlix/kernel/syscall.c`
- `OrlixKernel/Sources/ports/orlix/overlay/arch/orlix/kernel/hosted_exec.c`

Current model:

- Syscall number and arguments come from Orlix arch registers.
- Dispatch routes into upstream Linux syscall table macros.
- The hosted raw syscall path exists in `hosted_exec.c`.

This is the correct ownership model for OCI-derived environments. Imported binaries must enter through the same Linux syscall ABI, not through a custom Orlix runtime API.

### Current procfs, devfs, sysfs State

Current procfs/dev/sys state is upstream Linux pseudo filesystems.

Evidence:

- `release_defconfig` and `development_defconfig` enable `CONFIG_PROC_FS`, `CONFIG_SYSFS`, `CONFIG_DEVTMPFS`, `CONFIG_DEVTMPFS_MOUNT`, and `CONFIG_DEVTMPFS_SAFE`.
- `OrlixOS/Sources/init/rootinit.c` mounts proc, sysfs, and devtmpfs.
- `OrlixOS/Sources/init/init.c` mounts proc, sysfs, devtmpfs, devpts, tmpfs, and selinuxfs when available.

Missing:

- No product proof enumerates expected minimal `/proc`, `/dev`, `/sys`, `/dev/pts`, `/proc/self`, `/proc/mounts`, `/proc/net`, or cgroup entries for imported root environments.
- No current environment-local `/proc` proof exists.

### Current Networking Model

Networking is partial.

Evidence:

- `release_defconfig` and `development_defconfig` enable `CONFIG_NET`, `CONFIG_NETDEVICES`, `CONFIG_INET`, `CONFIG_UNIX`, and namespace options.
- `OrlixKernel/Sources/ports/orlix/kbuild/kernel-rules.mk` includes upstream net objects.
- No active Orlix virtio-net backend was found under `OrlixKernel/Sources/ports/orlix/overlay/drivers/orlix`.
- No HostAdapter network backend was found under `OrlixHostAdapter/Sources/OrlixHostAdapter`.

Current conclusion: upstream networking code is present in the build shape, but Orlix has no verified network device backend, no virtio-net path, no synthetic rtnetlink proof, and no product network proof.

### Current PTY/TTY Model

PTY/TTY is partially implemented and tested.

Evidence:

- Kernel configs enable `CONFIG_TTY` and `CONFIG_UNIX98_PTYS`.
- `OrlixKernel/Sources/ports/orlix/overlay/drivers/orlix/tty/console.c` registers the Orlix host console path.
- `OrlixKernel/Sources/ports/orlix/overlay/drivers/orlix/virtio/mmio.c` includes current virtio-console queue handling.
- `OrlixOS/Sources/init/init.c` mounts devpts and starts `/bin/sh` through a PTY.
- `Tests/OrlixPTYRuntimeTests` exists for PTY runtime proof.

Missing:

- virtio-console readiness and PTY readiness are not yet a full Linux conformance matrix.
- OCI-derived environment shells need their own PTY and controlling-terminal proof.

### Current Signal, Wait, Exit Model

Signal/wait/exit is upstream Linux-owned with Orlix arch signal glue.

Evidence:

- `OrlixKernel/Sources/ports/orlix/overlay/arch/orlix/kernel/signal.c`
- `OrlixKernel/Sources/ports/orlix/overlay/arch/orlix/kernel/process.c`
- `OrlixOS/Sources/init/init.c`, which waits on shell processes.
- Terminal proof tests exercise shell lifecycle and signal-adjacent behavior.

Missing:

- No comprehensive imported-environment test matrix for signal delivery, signal masks, wait status, zombies, process groups, and terminal-generated signals.

### Current Futex And Sync Model

Futex behavior is upstream Linux-owned.

Evidence:

- No active local futex implementation was found in durable Orlix source.
- Orlix selftest sources under `OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix` include raw syscall probes that exercise clone/futex behavior.

Missing:

- No OCI-derived environment futex proof exists.
- No oracle diff exists for futex wait/wake errno and timing behavior.

### Current Tests

Current test structure includes host adapter tests, parser/metadata tests, upstream proof wrappers, terminal proof tests, PTY runtime tests, and Orlix selftests.

Relevant paths:

- `Tests/OrlixHostAdapterTests`
- `Tests/OrlixLinuxProofOutputParserTests`
- `Tests/OrlixTerminalProofDriverTests`
- `Tests/OrlixPTYRuntimeTests`
- `Tests/OrlixKernelUpstreamTests`
- `Tests/OrlixMLibCUpstreamTests`
- `Tests/OrlixCoreutilsUpstreamTests`
- `OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix`

Gaps:

- No named environment tests.
- No rootfs tar import tests.
- No OCI layout tests.
- No OCI whiteout tests.
- No overlay snapshot tests for imported layers.
- No virtio-fs tests.
- No virtio-net tests.
- No environment-local namespace tests.
- No Linux oracle diff harness.

### project.yml And Xcode Target Structure

`project.yml` is current build truth.

Important current targets:

- `OrlixKernel`
- `OrlixOS`
- `OrlixTerminal`
- `OrlixTestRunner`
- `OrlixHostAdapterTests`
- `OrlixLinuxProofOutputParserTests`
- `OrlixTerminalProofDriverTests`
- `OrlixPTYRuntimeTests`
- `OrlixKernelUpstreamTests`
- `OrlixMLibCUpstreamTests`
- `OrlixCoreutilsUpstreamTests`

Important current target issue:

- `project.yml` compiles HostAdapter C sources into the `OrlixKernel` framework target and links `CoreFoundation.framework`.
- Treat this as packaging/linkage for the framework target, not permission for Linux-owner code under `OrlixKernel/Sources/ports/orlix/overlay` to include Darwin, Foundation, POSIX host, MLibC, or OrlixHostAdapter headers.

## 2. Architecture Gap Analysis

### Violations Or High-Risk Drift

- `project.yml` allows the `OrlixKernel` framework target to see `OrlixHostAdapter/Sources`. This must remain a build packaging detail only. Linux-owner source must not include HostAdapter headers or Apple SDK headers.
- `OrlixKernel/Sources/ports/orlix/overlay/drivers/orlix/virtio/mmio.c` currently hardcodes Orlix virtio-mmio slots. The slots are internal transport mechanics, but they need spec-backed tests and must not become Linux UAPI.
- New Linux-facing concepts must not be introduced under custom Orlix names. Existing `arch/orlix` and `drivers/orlix` names are port names, not Linux ABI names. Future Linux concepts must use upstream Linux names and shapes.
- The prompt's "temp backend for `/tmp`" must be reconciled with the repo's current Linux-shaped `/tmp` behavior. Current repo correctly uses Linux `tmpfs` for `/tmp`.

### Partial Compliance

- VFS is correctly upstream Linux-owned, but named environment root isolation is missing.
- Mount namespaces are configured, but not proven for Orlix environments.
- procfs/devtmpfs/sysfs are configured and mounted, but minimal imported-root compatibility is not proven.
- ext4 and OverlayFS are configured and used for default development root, but not yet for OCI-derived environment snapshots.
- virtio-mmio, virtio-blk, virtio-console, and virtio-rng are present, but virtio-fs, virtio-net, virtio-vsock, and virtio-balloon are absent.
- Application Support persistence exists for default writable state, but no per-environment storage registry exists.
- OrlixOS owns root payload assembly today, but not named environment descriptors, import metadata, or OCI layout handling.

### Missing Boundaries

- No environment descriptor model exists in `OrlixOS`.
- No per-environment image/state registration exists in HostAdapter boot resources.
- No rootfs tar importer exists.
- No OCI layout importer exists.
- No OCI registry pull implementation exists.
- No content-addressed blob store exists.
- No external folder mount path exists.
- No security-scoped host folder mount implementation exists.
- No virtio-fs device/backend exists.
- No virtio-net backend exists.
- No Linux oracle tool exists.
- No test guard prevents Apple container/containerization from becoming an iOS runtime dependency.

### Host Leakage

Current verified host leakage is mostly contained to OrlixHostAdapter.

Allowed current host mechanics:

- Darwin and POSIX host file I/O in `OrlixHostAdapter/Sources/OrlixHostAdapter/boot/resources.c`.
- Mach and host mapping in `OrlixHostAdapter/Sources/OrlixHostAdapter/memory/kernel_mapping.c`.
- CoreFoundation and signal/trap mechanics in HostAdapter runtime code.

Potential issue:

- The `OrlixKernel` Xcode framework target includes HostAdapter paths. Future invariant tests must verify that Linux-owner source under `OrlixKernel/Sources/ports/orlix/overlay` does not include HostAdapter headers or Apple SDK headers.

### Hand-Written Linux ABI Constants

Verified current good pattern:

- `OrlixKernel/Sources/ports/orlix/overlay/drivers/orlix/virtio/mmio.c` includes upstream Linux virtio UAPI headers such as `uapi/linux/virtio_mmio.h`, `uapi/linux/virtio_ring.h`, and `uapi/linux/virtio_blk.h`.

Required audit:

- Any constants that duplicate Linux UAPI, errno values, ioctl payloads, syscall numbers, mount flags, namespace flags, netlink layouts, or virtio protocol values must either come from upstream Linux headers or be removed.
- Host trap instruction constants in HostAdapter are host execution mechanics, not Linux UAPI, but they must stay private and must not leak into Linux userspace contracts.

### VFS Path Remapping

No active thin host-path VFS remapper was found in durable OrlixKernel source.

Current risk is different:

- Orlix does not yet have environment-level root selection, external mounts, OCI layer binding, or per-environment mount namespace proof.
- The implementation must continue using upstream Linux VFS and Linux mount namespaces, not reintroduce local path-prefix translation.

### HostAdapter Deciding Linux Semantics

Current HostAdapter decides:

- Where writable block image files live on the host.
- How bundled resources are resolved.
- How host file bytes are read/written for block devices.
- How host time/random/thread/trap primitives are performed.

Current HostAdapter must not decide:

- Linux mount behavior.
- Linux path resolution.
- Linux permissions.
- Linux uid/gid/capability behavior.
- Linux errno semantics beyond translating host errors at the private host boundary.
- Linux root selection inside an already-running environment.

### Untested Linux Semantics

Major untested areas for OCI-derived environments:

- Mount namespace creation and isolation.
- Environment root binding for process launch.
- `/proc`, `/dev`, `/sys`, `/dev/pts`, `/proc/self`, `/proc/mounts` inside environment roots.
- OverlayFS copy-up, whiteout, opaque directory behavior.
- uid/gid/chown/chmod behavior across imported root filesystems.
- PTY controlling terminal behavior inside imported roots.
- signal/wait/exit behavior inside environment namespaces.
- fd inheritance and close-on-exec across environment launch.
- futex wait/wake under imported userspace.
- network interface enumeration and rtnetlink behavior.
- cgroup v2 shape and unsupported-operation behavior.

### Tests That Prove Host Mediation But Not Linux Behavior

Examples:

- HostAdapter storage and boot resource tests can prove host file placement, but not Linux mount semantics.
- Parser or manifest tests can prove metadata handling, but not Linux exec or filesystem semantics.
- Simulator launch or app-hosted boot can prove bootstrap, but not environment isolation.
- A successful shell launch proves default root execution, but not OCI-derived image compatibility.

## 3. Proposed Target Architecture

### Principle

Do not build a parallel Orlix VFS, process model, container runtime, or Docker-like runtime.

The target is:

```text
Linux userspace
  -> Linux syscall ABI
  -> upstream Linux subsystems in OrlixKernel
  -> Linux device/filesystem/network primitives
  -> Orlix virtio-mmio device plane where host-backed devices are needed
  -> OrlixHostAdapter private host mechanics
  -> iOS/Darwin sandbox APIs
```

OCI images configure Orlix environments. They do not define Linux semantics.

### Module Structure

Keep current ownership:

- `OrlixKernel/Sources/ports/orlix/overlay/arch/orlix`
  - Orlix upstream Linux arch port.
  - syscall entry, process, signal, fault, hosted execution, memory.
- `OrlixKernel/Sources/ports/orlix/overlay/drivers/orlix/virtio`
  - Orlix virtio-mmio backend and future backend split if the current file becomes too large.
  - Must use upstream Linux virtio headers or spec-derived generated material.
- `OrlixKernel/Sources/ports/orlix/overlay/drivers/orlix/tty`
  - Host console/TTY device integration only.
- `OrlixHostAdapter/Sources/OrlixHostAdapter`
  - Private host mechanics for app storage, block image files, security-scoped access, host time, entropy, host threading, traps, terminal bytes.
  - No Linux policy.
- `OrlixOS/Sources/Session`
  - App-facing Linux session API.
- Proposed `OrlixOS/Sources/Environments`
  - Environment descriptors, environment registry, environment lifecycle policy.
- Proposed `OrlixOS/Sources/Storage`
  - App-private location selection and metadata for base/state/cache/import artifacts.
  - Calls HostAdapter for private host mechanics where needed.
- Proposed `OrlixOS/Sources/RootfsImport`
  - Rootfs tar import into an environment image.
- Proposed `OrlixOS/Sources/OCI`
  - OCI layout import first.
  - Registry pull later.
  - No OrlixKernel dependency.
- Proposed `tools/orlix-linux-oracle`
  - Mac-only real-Linux comparison harness.
- Proposed `tools/oci-fixtures`
  - Deterministic fixture generation for tar and OCI layout tests.
  - Mac-only where needed.

### VFS Mount Table

Use upstream Linux mount namespaces and VFS.

No new Orlix public VFS table should be introduced outside Linux.

Required proof target:

- Environment entry creates or selects a Linux mount namespace.
- The environment root is a Linux mount root.
- `/proc`, `/dev`, `/sys`, `/tmp`, `/run`, and external mounts are visible through Linux mounts.
- No raw host path is ever Linux truth.

### Backend Model

Model "backend classes" as Linux-native filesystem and device choices:

- Bundled image backend
  - Payload ext4 image exposed as virtio-blk and mounted by Linux.
- Application Support backend
  - Writable ext4 or state image file stored under app-private Application Support and exposed through virtio-blk.
- Caches backend
  - Download/cache blob storage under app-private Caches.
  - Not Linux truth unless deliberately mounted through a Linux filesystem path.
- temp backend
  - Host scratch for import/build staging only.
  - Linux `/tmp` remains tmpfs.
- Security-scoped external backend
  - Future virtio-fs mount backed by user-selected security-scoped folder.
- procfs/devfs/sysfs
  - Upstream Linux procfs, devtmpfs/devpts, and sysfs.
  - Orlix drivers populate Linux-visible devices.
- Future ext4 image backend
  - Already aligned with current `/dev/vda` and `/dev/vdb` pattern.
- Future overlay backend
  - Prefer upstream Linux OverlayFS for initial OCI snapshots.
  - Only build Orlix-specific overlay behavior if upstream OverlayFS cannot satisfy iOS-hosted constraints.

### Virtio Device Plane

Current `virtio/mmio.c` already implements a useful first device plane. Evolve it without moving Linux semantics below the device layer.

Priority order:

1. Stabilize current virtio-mmio transport tests.
2. Stabilize virtio-blk persistence and flush behavior.
3. Stabilize virtio-console readiness and PTY interaction.
4. Stabilize virtio-rng backing for getrandom and `/dev/urandom`.
5. Add virtio-fs or the closest upstream Linux-compatible host folder device path.
6. Add virtio-net.
7. Add virtio-vsock only if a Linux-visible AF_VSOCK or internal host control use case is justified.
8. Add virtio-balloon-inspired pressure/accounting only below Linux resource accounting.
9. Defer virtio-input.
10. Defer virtio-gpu.

### Environment Manager

Environment management belongs in OrlixOS and Linux userspace orchestration. Linux semantics remain in OrlixKernel.

Environment descriptor minimum fields:

- environment id
- display name
- source type: default, copy, tar, OCI layout, future registry
- platform, initially `linux/arm64`
- base image path or content digest
- writable state image path
- root mode: direct ext4 or overlay
- default argv
- default env
- default cwd
- default uid/gid
- hostname
- mount policy
- created/updated metadata

Environment entry must be implemented through Linux mechanisms:

- `clone` or `unshare` for mount namespace when available.
- Linux mount syscalls.
- environment root binding through Linux-owned process launch state.
- `execve` for process launch.
- normal fdtable, PTY, signal, wait, and exit behavior.

### OCI And Rootfs Import

Order:

1. Tar rootfs import.
2. OCI image layout import.
3. Registry pull.

Tar import requirements:

- Preserve mode, uid, gid, symlinks, hardlinks, device node metadata where policy allows.
- Reject unsafe path traversal.
- Import into an environment base image or tree that is later mounted as Linux root.
- Do not execute host-side imported code during import.

OCI layout import requirements:

- Parse `oci-layout`, `index.json`, manifests, config, and blobs.
- Select `linux/arm64`.
- Verify every digest before use.
- Apply layers in order.
- Apply OCI whiteouts and opaque directories.
- Bind result to an Orlix environment descriptor.
- Do not call Apple container/containerization from OrlixKernel or iOS runtime.

### OCI Runtime Spec Alignment

Pin a specific `opencontainers/runtime-spec` tag or commit before implementing
schema-backed runtime behavior. Do not track floating `main` in tests.

Use the OCI Image Spec and OCI Runtime Spec as separate standards tracks. OCI
image import produces rootfs content and image-derived defaults. OCI Runtime
Spec config and lifecycle input describes how to create, start, inspect, signal,
and delete a runtime bundle. OrlixOS translates the runtime bundle/config into
Orlix execution state. Orlix does not become `runc`.

Standards corpus:

- `config.md`: `ociVersion`, `root`, `mounts`, `process`, hooks, annotations.
- `config-linux.md`: default Linux filesystems, namespaces, user mappings,
  devices, net devices, cgroups/resources, seccomp, masked paths, readonly
  paths, rootfs propagation, personality, time offsets.
- `runtime.md`: `state`, `create`, `start`, `kill`, `delete`.
- `runtime-linux.md`: Linux fd policy and `/dev/fd`, `/dev/stdin`,
  `/dev/stdout`, `/dev/stderr`.
- `features.md` and `features-linux.md`: runtime feature report.
- `schema/config-schema.json`, `schema/config-linux.json`, `schema/defs.json`,
  `schema/defs-linux.json`, `schema/features-schema.json`,
  `schema/features-linux.json`.

Runtime config mapping:

| OCI Runtime field | Orlix target | v1 behavior |
|---|---|---|
| `ociVersion` | OrlixOS parser | validate pinned supported range |
| `root.path` | environment root input | resolve into imported/materialized root |
| `root.readonly` | root policy | read-only lower or reject if impossible |
| `mounts[]` | Linux mount namespace | accept supported mounts, reject unsupported |
| `process.args` | Linux `execve` argv | required on start |
| `process.env` | task environment | preserve order and values where possible |
| `process.cwd` | Linux cwd | require absolute path |
| `process.terminal` | PTY/TTY | allocate PTY when true |
| `process.consoleSize` | TTY sizing | apply only when terminal is true |
| `process.rlimits` | Linux rlimits | apply or deterministic unsupported error |
| `process.user` | credentials | map uid/gid, staged supplementary groups |
| `linux.namespaces` | namespaces | staged mount, UTS, PID, network, IPC, user, cgroup, time |
| `linux.uidMappings/gidMappings` | user namespace | reject until real support exists |
| `linux.devices` | devfs/device policy | default devices first, explicit devices deferred |
| `linux.netDevices` | networking | reject until virtio-net namespace path exists |
| `linux.resources` | cgroups | virtual cgroup v2 track |
| `linux.cgroupsPath` | cgroup path | deterministic virtual path policy |
| `linux.seccomp` | seccomp | parse and reject until implemented |
| `linux.maskedPaths` | mount policy | deferred until mount namespace path supports it |
| `linux.readonlyPaths` | mount policy | deferred until mount namespace path supports it |
| `linux.rootfsPropagation` | mount propagation | implement or reject deterministically |
| `annotations` | metadata | no Linux semantics |

Feature reporting milestone:

- Implement only when truthful.
- Validate against `features-schema.json` and `features-linux.json`.
- Report `ociVersionMin` and `ociVersionMax`.
- Report recognized mount options only.
- Report Linux features in three internal states: recognized, implemented, and
  rejected with deterministic error.
- Do not report support for cgroups, seccomp, AppArmor, SELinux, netDevices,
  idmapped mounts, or user namespace mapping until proved.

### Linux Oracle Tooling

Add Mac-only oracle tooling outside the iOS runtime.

Allowed references:

- Apple `container` CLI as an external runner.
- Apple `containerization` as a Mac-only reference or tool dependency.
- Other local real-Linux runners if available.

Forbidden:

- No Apple container/containerization in OrlixKernel.
- No Virtualization.framework runtime dependency in iOS app.
- No vminitd/vmnet/Rosetta/guest-kernel path in Orlix runtime.

## 4. Dependency Rules

| Component | Allowed | Forbidden |
|---|---|---|
| OrlixKernel upstream overlay | upstream Linux source, upstream Linux UAPI, Orlix arch/driver code, internal kernel-declared host mechanics headers, upstream virtio headers, OASIS virtio spec-derived generated material | Darwin headers, Foundation, Apple SDK types, POSIX host headers, libc, MLibC, OrlixHostAdapter headers, Apple container, Apple containerization, Virtualization.framework |
| OrlixHostAdapter | Darwin, Foundation/CoreFoundation, POSIX host APIs, Mach, pthread, private host storage, security-scoped access, host time/random/thread/trap mechanics, kernel-declared private host boundary headers | Linux policy, Linux path resolution, Linux permissions, Linux mount semantics, Linux syscall ABI decisions, public Linux UAPI clones |
| OrlixMLibC and userspace sysdeps | upstream Linux UAPI from headers_install, MLibC sysdeps, Linux syscall ABI use | OrlixKernel internals, OrlixHostAdapter headers, Darwin shortcuts for Linux behavior |
| OrlixOS | curated OS distribution policy, root payload assembly, environment descriptors, import orchestration, app-facing session API | kernel semantics, libc semantics, syscall ABI definitions, private host mechanics, Darwin-shaped Linux public APIs |
| iOS app targets | OrlixOS public session API, UI, app sandbox integration through OrlixOS | Apple container runtime, Virtualization.framework runtime, raw HostAdapter access for Linux policy, OrlixKernel internals |
| Mac-only tools | Apple container, Apple containerization, Virtualization.framework, registry clients, fixture generators, Linux oracle runners | shipping iOS runtime dependency, OrlixKernel dependency |
| Tests | upstream Linux tests, kselftest, MLibC tests, app-hosted tests, oracle comparisons, Apple container as external Mac-only comparison target | editing generated upstream sources, treating host mediation proof as Linux proof |
| Apple container/containerization | Mac-only reference, Mac-only tooling, registry/image/archive/ext4/oracle comparison | OrlixKernel dependency, iOS app runtime dependency, VM runtime substrate |
| Virtualization.framework | Mac-only oracle or reference if needed | iOS runtime architecture, OrlixKernel dependency, required product path |
| Darwin/Foundation/POSIX host headers | OrlixHostAdapter, app code, Mac-only tools | OrlixKernel Linux-owner code, Linux ABI definitions |
| upstream Linux UAPI headers | mandatory ABI contract for OrlixKernel, OrlixMLibC, userspace tests | local replacements, aliases, cloned constants |
| OASIS virtio material | transport and device protocol tests, generated constants when upstream Linux headers are insufficient | ad hoc hand-defined protocol constants in subsystem files |

Forbidden dependency paths:

- `OrlixKernel -> OrlixHostAdapter headers`
- `OrlixKernel -> Darwin/Foundation/POSIX host headers`
- `OrlixKernel -> MLibC`
- `OrlixKernel -> Apple container/containerization`
- `OrlixKernel -> Virtualization.framework`
- `OrlixOS -> kernel semantics`
- `OrlixHostAdapter -> Linux policy`
- `iOS runtime -> Apple container VM path`
- `iOS runtime -> vminitd`
- `iOS runtime -> vmnet`
- `iOS runtime -> Rosetta`
- `Linux userspace ABI -> Orlix-specific UAPI clones`

## 5. Phased Implementation Plan

### Phase 0: Repository Audit And Invariant Enforcement Plan

Goal:

- Lock the current architecture facts and add guardrails before feature work.

Required repo changes:

- Add automated checks that scan active runtime source for forbidden IXLand names, OrlixKit names, Darwin/Foundation/POSIX host includes in Linux-owner code, HostAdapter includes in OrlixKernel overlay, local Linux UAPI clones, and Apple container/containerization runtime dependencies.
- Add a short ADR or active plan update documenting that OCI-derived environments use upstream Linux mechanisms plus OrlixOS orchestration.

Likely touched areas:

- `docs/adr`
- `docs/plans/active`
- `Tests`
- `tools`
- `.codex` or harness checks only if allowed by harness rules

Tests to add:

- Static dependency tests.
- Naming invariant tests.
- Forbidden include tests.
- Project dependency tests over `project.yml`.

Build commands to run:

```bash
rtk xcodebuild -list -project OrlixSystem.xcodeproj
rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixHostAdapterTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim test
rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim test
```

Proof criteria:

- Static checks fail on forbidden active IXLand, OrlixKit, HostAdapter include, Apple container runtime dependency, or Darwin include in OrlixKernel overlay.
- Existing OrlixOS and HostAdapter tests still pass.

Risks:

- False positives from historical docs or build-only host compatibility helpers.

Blockers:

- Need exact allowlist for historical docs and build-only host compatibility files.

Must not be done:

- Do not rename historical documentation unless explicitly part of cleanup.
- Do not edit generated upstream trees.
- Do not introduce runtime compatibility aliases.

### Phase 1: VFS Storage Policy Correction

Goal:

- Make storage policy explicit and enforceable for default and future environments.

Required repo changes:

- Extend OrlixOS storage metadata to distinguish persistent Linux state, cache/download blobs, import scratch, bundled payloads, and user-selected mounts.
- Keep writable Linux system state under app-private Application Support.
- Keep cache/download blobs under app-private Caches.
- Keep host scratch under temp.
- Keep Linux `/tmp` as Linux tmpfs.
- Ensure Documents is never the Linux root and only appears later through an explicit mount.

Likely touched areas:

- `OrlixOS/Sources/Session`
- proposed `OrlixOS/Sources/Storage`
- `OrlixHostAdapter/Sources/OrlixHostAdapter/boot/resources.c`
- `OrlixOS/Sources/distribution/target-settings.xcconfig`
- `OrlixOS/Sources/make/rootfs.mk`
- `Tests/OrlixHostAdapterTests`
- `Tests/OrlixOSTests`

Tests to add:

- Application Support path selection for writable state.
- Caches path selection for cache blobs.
- temp path selection for importer scratch.
- `/tmp` inside Linux is tmpfs.
- Documents is absent from Linux truth unless mounted.

Build commands to run:

```bash
rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixHostAdapterTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim test
rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim test
```

Proof criteria:

- Tests prove persistent state path is Application Support.
- Tests prove cache path is Caches.
- Product boot proof shows `/tmp` is tmpfs.
- No test or source treats Documents as root.

Risks:

- Confusing host temp with Linux `/tmp`.

Blockers:

- Need exact OrlixOS API surface for storage descriptors.

Must not be done:

- Do not change Linux `/tmp` from tmpfs to host temp.
- Do not expose raw host paths as Linux paths.
- Do not put storage policy into OrlixKernel unless it is a Linux mount/device decision.

### Phase 2: Real Mount Table And Backend-Classed VFS

Goal:

- Prove Orlix uses upstream Linux mount dispatch and mount namespaces for environment roots.

Required repo changes:

- Add product tests for Linux mount namespace behavior, mount visibility, environment root binding, ext4 root, OverlayFS root, and mount propagation.
- Add minimal fixes only in Orlix arch/driver code if upstream Linux mount behavior fails due Orlix port gaps.

Likely touched areas:

- `OrlixKernel/Sources/ports/orlix/overlay/arch/orlix`
- `OrlixOS/Sources/init`
- `OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix`
- `Tests/OrlixKernelUpstreamTests`
- `Tests/OrlixTerminalProofDriverTests`

Tests to add:

- `mount` and `umount` smoke tests.
- `unshare(CLONE_NEWNS)` if available.
- `/proc/self/mountinfo` validation.
- environment root binding behavior.
- ext4 mount read/write.
- OverlayFS mount read/write/copy-up smoke.

Build commands to run:

```bash
rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixKernelUpstreamTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim test
rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixTerminalProofDriverTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim test
```

Proof criteria:

- A process can enter a separate mount namespace.
- Environment root can be selected by Linux mount mechanics.
- `/proc/self/mountinfo` shows expected Linux mounts.
- No direct host path prefix translation is involved.

Risks:

- Mount namespace support may be configured but broken in the Orlix port.
- OverlayFS may need additional upstream object inclusion or config.

Blockers:

- Need product proof for namespace syscalls.

Must not be done:

- Do not create a parallel Orlix VFS.
- Do not implement mount dispatch in HostAdapter.
- Do not make OrlixOS decide path lookup semantics.

### Phase 3: Synthetic procfs/devfs/sysfs Baseline

Goal:

- Prove the Linux pseudo-filesystem baseline needed by minimal imported root environments.

Repo-grounded interpretation:

- Use upstream Linux procfs, devtmpfs/devpts, and sysfs already enabled in defconfigs.
- Add Orlix device data through Linux drivers and upstream device model.
- Do not build independent pseudo-filesystem implementations outside upstream Linux.

Required repo changes:

- Add tests for expected `/proc`, `/dev`, `/sys`, `/dev/pts`, `/proc/self`, `/proc/mounts`, `/proc/self/fd`, and `/proc/self/status`.
- Add missing Orlix device registration only where tests prove a Linux userspace-visible gap.

Likely touched areas:

- `OrlixOS/Sources/init`
- `OrlixKernel/Sources/ports/orlix/overlay/drivers/orlix`
- `OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix`
- `Tests/OrlixPTYRuntimeTests`
- `Tests/OrlixTerminalProofDriverTests`

Tests to add:

- procfs shape.
- devtmpfs node presence.
- devpts allocation.
- sysfs virtio/device visibility.
- `/dev/urandom`, `/dev/console`, `/dev/tty`, `/dev/pts/*`.

Build commands to run:

```bash
rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim test
rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixTerminalProofDriverTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim test
```

Proof criteria:

- Minimal shell can inspect Linux-shaped proc/dev/sys.
- PTY allocation works through devpts.
- No HostAdapter-created fake `/proc`, `/dev`, or `/sys` tree exists.

Risks:

- Imported images may expect more proc/sys entries than Orlix currently exposes.

Blockers:

- Need real target image probes for BusyBox and Alpine.

Must not be done:

- Do not create host-generated pseudo files as Linux truth.
- Do not bypass upstream procfs/devtmpfs/sysfs.

### Phase 4: Named Orlix Environments

Goal:

- Add persistent named environments without OCI.

Required repo changes:

- Add OrlixOS environment descriptor storage.
- Add environment registry.
- Add create-copy or create-empty environment operation.
- Add environment entry operation using Linux mount namespace and normal exec.
- Add per-environment root/state image selection.

Likely touched areas:

- proposed `OrlixOS/Sources/Environments`
- proposed `OrlixOS/Sources/Storage`
- `OrlixHostAdapter/Sources/OrlixHostAdapter/boot/resources.c`
- `OrlixOS/Sources/init`
- `Tests/OrlixOSTests`
- `Tests/OrlixTerminalProofDriverTests`

Tests to add:

- Create environment from default root.
- Enter environment.
- Modify `/etc` or `/var/lib` in one environment and prove default root unchanged.
- Distinct hostname if UTS namespace is enabled.
- Distinct mount namespace proof.

Build commands to run:

```bash
rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim test
rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixTerminalProofDriverTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim test
```

Proof criteria:

- Default environment remains default.
- Named environment has isolated root state.
- `cat /etc/os-release` runs from the selected environment root.
- Modifying one environment does not mutate another.

Risks:

- Environment entry may expose missing namespace, mount, or exec behavior.
- OrlixOS could accidentally become a Linux runtime facade.

Blockers:

- Need Phase 2 mount namespace proof.

Must not be done:

- No OCI.
- No registry pull.
- No Docker/runc semantics.
- No HostAdapter path remapping.

### Phase 5: Rootfs Tar Import

Goal:

- Import a Linux rootfs tarball into a named Orlix environment and run from it if current exec support allows.

Required repo changes:

- Add rootfs tar importer in OrlixOS.
- Preserve Linux metadata needed for rootfs correctness.
- Reject unsafe paths.
- Materialize import into ext4-backed environment root or a tree that is packed into an ext4 image.
- Add fixture tarballs for BusyBox or Alpine test roots.

Likely touched areas:

- proposed `OrlixOS/Sources/RootfsImport`
- proposed `OrlixOS/Sources/Environments`
- proposed `tools/oci-fixtures`
- `Tests/OrlixOSTests`
- `Tests/OrlixTerminalProofDriverTests`

Tests to add:

- Tar path traversal rejection.
- Symlink and hardlink preservation.
- Mode, uid, gid preservation where supported.
- Device node metadata policy.
- `cat /etc/os-release` from imported root.
- `/bin/sh` execution from imported root if supported by existing ELF path.

Build commands to run:

```bash
rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim test
rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixTerminalProofDriverTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim test
```

Proof criteria:

- Imported root appears as the environment root through Linux mounts.
- Imported `/etc/os-release` is visible.
- BusyBox or Alpine shell runs when binary compatibility is sufficient.
- Import does not mutate the source tarball.

Risks:

- Prebuilt binaries may hit ELF, auxv, dynamic linker, TLS, mmap, syscall, or page mapping gaps.
- Metadata preservation may be incomplete on host-side unpack.

Blockers:

- Need Phase 4 named environment entry.
- Need deterministic linux/arm64 fixture tarball.

Must not be done:

- Do not rewrite imported binaries.
- Do not require Orlix-specific instructions on the Linux surface.
- Do not import through Apple container in the iOS runtime.
- Do not call host `tar` as product behavior without sandbox and metadata proof.

### Phase 6: Virtio Core

Goal:

- Turn current virtio-mmio behavior into a tested internal device plane.

Required repo changes:

- Add tests for split virtqueue descriptor chains, available ring, used ring, interrupt/completion, reset, status transitions, config space, and feature negotiation.
- Keep packed virtqueues deferred.
- Refactor `virtio/mmio.c` only if tests require clearer isolation.

Likely touched areas:

- `OrlixKernel/Sources/ports/orlix/overlay/drivers/orlix/virtio/mmio.c`
- `OrlixKernel/Sources/ports/orlix/overlay/tools/testing/selftests/orlix`
- `Tests/OrlixKernelUpstreamTests`

Tests to add:

- Descriptor chain validation.
- Indirect descriptor validation.
- Feature negotiation.
- Device reset.
- Completion ordering.
- Interrupt status behavior.

Build commands to run:

```bash
rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixKernelUpstreamTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim test
```

Proof criteria:

- Current block, console, and rng devices still boot.
- Ring mechanics are covered by deterministic tests.
- Constants are sourced from upstream Linux virtio headers or spec-derived generated material.

Risks:

- Over-refactor could destabilize current boot path.

Blockers:

- Need a test shape that can exercise virtio backend behavior deterministically.

Must not be done:

- Do not move Linux VFS/TTY/socket semantics into virtio code.
- Do not hand-define Linux UAPI constants.
- Do not implement PCI unless justified by user-visible Linux compatibility.

### Phase 7: Initial Virtio Devices

Goal:

- Stabilize current virtio-blk, virtio-console, and virtio-rng, then add host-folder support through a Linux-compatible device path.

Required repo changes:

- Add persistence and flush tests for virtio-blk.
- Add readiness and PTY integration tests for virtio-console.
- Add getrandom and `/dev/urandom` tests for virtio-rng.
- Add virtio-fs design spike and implementation only after VFS and environment roots are proven.

Likely touched areas:

- `OrlixKernel/Sources/ports/orlix/overlay/drivers/orlix/virtio`
- `OrlixHostAdapter/Sources/OrlixHostAdapter/boot`
- `OrlixHostAdapter/Sources/OrlixHostAdapter/terminal`
- `OrlixHostAdapter/Sources/OrlixHostAdapter/runtime/entropy.c`
- `Tests/OrlixPTYRuntimeTests`
- `Tests/OrlixTerminalProofDriverTests`

Tests to add:

- virtio-blk read/write/flush persistence.
- root image survives restart.
- virtio-console input/output readiness.
- PTY still owns terminal semantics.
- getrandom and `/dev/urandom` behavior.
- future virtio-fs host-backed mount test.

Build commands to run:

```bash
rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim test
rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixTerminalProofDriverTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim test
```

Proof criteria:

- Block state persists.
- Console does not bypass Linux TTY/PTY behavior.
- Randomness reaches Linux through the expected devices/syscalls.
- Host folders, when added, appear only as Linux mounts.

Risks:

- virtio-fs may require more upstream Linux objects than currently selected.
- Host folder metadata may not map cleanly to Linux metadata.

Blockers:

- Need Phase 2 mount proof and Phase 1 storage policy.

Must not be done:

- Do not expose host folders as raw paths.
- Do not make virtio-console replace PTY/session semantics.
- Do not make virtio-rng define getrandom semantics.

### Phase 8: OCI Layout Import

Goal:

- Import OCI image layouts into Orlix environments without registry pull.

Required repo changes:

- Add OCI layout parser under OrlixOS.
- Parse `oci-layout`, `index.json`, manifest, config, and blob paths.
- Select `linux/arm64`.
- Verify sha256 digests before using blobs.
- Apply layers in order.
- Apply whiteouts and opaque directories.
- Bind the result to an environment descriptor.

Likely touched areas:

- proposed `OrlixOS/Sources/OCI`
- proposed `OrlixOS/Sources/RootfsImport`
- proposed `OrlixOS/Sources/Environments`
- proposed `tools/oci-fixtures`
- `Tests/OrlixOSTests`

Tests to add:

- OCI layout validation.
- Missing blob rejection.
- Digest mismatch rejection.
- linux/arm64 selection.
- Unsupported platform rejection.
- Layer order application.
- Whiteout file removal.
- Opaque directory behavior.
- Environment creation from imported layout.

Build commands to run:

```bash
rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim test
rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixTerminalProofDriverTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim test
```

Proof criteria:

- Imported OCI layout creates a named environment.
- No digest mismatch is accepted.
- `/etc/os-release` comes from the OCI-derived environment.
- No Apple container/runtime dependency is linked into OrlixKernel or iOS runtime.

Risks:

- Whiteout semantics are easy to get subtly wrong.
- OCI config user/env/entrypoint may be mistaken for runtime semantics instead of environment defaults.

Blockers:

- Need Phase 5 import materialization and Phase 4 environment binding.

Must not be done:

- No registry pull.
- No Apple container runtime dependency.
- No runc config execution.
- No Docker daemon assumptions.

### Phase 9: Overlay And Snapshot Model

Goal:

- Add read-only imported image roots with writable per-environment upper layers.

Required repo changes:

- Use upstream OverlayFS first.
- Store lower image roots read-only.
- Store upper/work state per environment under Application Support.
- Persist snapshot metadata in OrlixOS environment descriptors.
- Add copy-up and unlink tests.

Likely touched areas:

- `OrlixOS/Sources/Environments`
- `OrlixOS/Sources/Storage`
- `OrlixOS/Sources/init`
- `OrlixKernel/Sources/ports/orlix/configs`
- `Tests/OrlixOSTests`
- `Tests/OrlixTerminalProofDriverTests`

Tests to add:

- Directory merge.
- Copy-up on write.
- Lower file remains unchanged.
- Unlink hides lower file.
- Rename behavior smoke.
- Opaque directory behavior.
- Snapshot persists after restart.

Build commands to run:

```bash
rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim test
rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixTerminalProofDriverTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim test
```

Proof criteria:

- OCI-derived lower image is immutable.
- Environment writes persist in upper state.
- Removing a lower-layer file in an environment does not mutate the lower image.
- Overlay behavior matches real Linux oracle for covered cases.

Risks:

- OverlayFS may expose missing Orlix kernel features.
- Snapshot metadata can drift from actual mounted state.

Blockers:

- Need Phase 8 OCI layout import.

Must not be done:

- Do not implement custom overlay semantics before proving upstream OverlayFS cannot work.
- Do not mutate imported base image layers.

### Phase 10: Registry Pull Tooling

Goal:

- Add registry pull only after layout import works.

Required repo changes:

- Add Mac-only registry fixture tool first, or an explicitly sandbox-safe OrlixOS registry client after product/legal approval.
- Reuse OCI layout import path after pull.
- Store downloaded blobs in Caches or a content-addressed store under app-private storage according to product policy.

Likely touched areas:

- proposed `tools/oci-fixtures`
- proposed `OrlixOS/Sources/OCI`
- proposed `OrlixOS/Sources/Storage`
- `Tests/OrlixOSTests`

Tests to add:

- Registry manifest resolution with local fixture server.
- Auth/token handling only if implemented.
- Retry behavior only if implemented.
- Digest verification remains mandatory.
- Pulled image imports through the same layout path.

Build commands to run:

```bash
rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim test
```

Proof criteria:

- Pull path produces the same verified OCI layout inputs as Phase 8.
- OrlixKernel has no new dependencies.
- iOS runtime does not link Apple container/containerization.
- Registry support can be disabled or excluded if product/legal requires.

Risks:

- App Store review risk for downloading executable payloads.
- Registry auth and caching can grow scope quickly.

Blockers:

- Product/legal review for in-app registry pull.
- Phase 8 must be complete.

Must not be done:

- Do not start here.
- Do not add Apple container/containerization to iOS runtime.
- Do not let registry metadata decide Linux behavior.
- Do not market this as arbitrary Docker support.

### Phase 11: `orlix run`

Goal:

- Add one-shot process launch from an existing reliable environment.

Required repo changes:

- Add a userspace command or OrlixOS session operation that resolves an environment, sets argv/env/cwd/user defaults, attaches stdio, and launches through Linux `execve`.
- Implement `--rm` only after persistent named environments are stable.

Likely touched areas:

- `OrlixOS/Sources/Session`
- `OrlixOS/Sources/Environments`
- `OrlixOS/Sources/init`
- `Tests/OrlixTerminalProofDriverTests`

Tests to add:

- argv mapping.
- env mapping.
- cwd mapping.
- uid/gid mapping where supported.
- stdin/stdout/stderr behavior.
- exit status.
- signal termination.
- cleanup for `--rm` when added.

Build commands to run:

```bash
rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixTerminalProofDriverTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim test
```

Proof criteria:

- `orlix run alpine /bin/echo hello` equivalent works through Linux exec.
- Exit status is preserved.
- stdio is Linux fd behavior, not HostAdapter direct command execution.
- Persistent environments remain the primary mode until `--rm` is proven.

Risks:

- Command UX may be confused with Docker/runc compatibility.
- Process cleanup is easy to under-test.

Blockers:

- Need Phase 4 and Phase 8 or Phase 5.

Must not be done:

- No runc dependency.
- No Docker daemon.
- No custom process API bypassing Linux exec/fd/signal/wait behavior.

### Phase 12: Networking And Namespace Expansion

Goal:

- Add Linux-shaped networking for environments.

Required repo changes:

- Add virtio-net backend or another upstream Linux-compatible net device path.
- Add private HostAdapter network backend for iOS/Darwin mechanics.
- Add shared outbound networking first.
- Add `/proc/net` and rtnetlink proof.
- Add virtual loopback and per-environment network namespace later.

Likely touched areas:

- `OrlixKernel/Sources/ports/orlix/overlay/drivers/orlix/virtio`
- `OrlixHostAdapter/Sources/OrlixHostAdapter`
- `OrlixKernel/Sources/ports/orlix/configs`
- `Tests/OrlixKernelUpstreamTests`
- `Tests/OrlixTerminalProofDriverTests`

Tests to add:

- `ip link` equivalent netlink probe.
- `/proc/net` entries.
- loopback behavior.
- TCP/UDP smoke if implemented.
- shared outbound request if product policy allows.
- namespace-local interface view when added.

Build commands to run:

```bash
rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixKernelUpstreamTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim test
rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixTerminalProofDriverTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim test
```

Proof criteria:

- Linux userspace sees Linux-shaped interface state.
- rtnetlink responses match Linux expectations for covered cases.
- Host networking remains private below OrlixKernel.
- No Darwin interface names leak as Linux truth unless intentionally translated by Linux netdev state.

Risks:

- Network.framework/BSD sockets may not map cleanly to Linux socket semantics.
- Per-environment network isolation is much harder than shared outbound.

Blockers:

- Need virtio-net design and host networking policy.
- Product/legal review for network behavior may be needed.

Must not be done:

- Do not translate Linux socket syscalls directly to Darwin sockets in userspace.
- Do not expose Darwin network interfaces as Linux interfaces without Linux netdev modeling.
- Do not start with full Kubernetes/Docker networking expectations.

### Phase 13: Virtual Cgroups And Resource Model

Goal:

- Provide Linux-shaped cgroup v2 behavior and resource accounting where feasible.

Required repo changes:

- Mount and test cgroup v2 shape.
- Add Orlix resource accounting hooks only where upstream Linux needs arch/driver support.
- Use virtio-balloon-inspired host pressure only below Linux resource accounting.
- Return Linux-shaped unsupported behavior where real enforcement is unavailable.

Likely touched areas:

- `OrlixKernel/Sources/ports/orlix/configs`
- `OrlixKernel/Sources/ports/orlix/overlay/arch/orlix`
- `OrlixKernel/Sources/ports/orlix/overlay/drivers/orlix/virtio`
- `OrlixHostAdapter/Sources/OrlixHostAdapter`
- `Tests/OrlixKernelUpstreamTests`

Tests to add:

- cgroup v2 mount shape.
- basic `cgroup.procs`.
- memory/cpu file presence according to enabled controllers.
- unsupported write behavior.
- pressure/accounting smoke tests if implemented.

Build commands to run:

```bash
rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixKernelUpstreamTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim test
```

Proof criteria:

- Minimal cgroup v2 userspace expectations are satisfied or fail with Linux-shaped errno.
- Resource accounting does not leak Darwin policy into Linux-visible files.
- virtio-balloon-inspired pressure handling is private and does not pretend to be hypervisor ballooning.

Risks:

- Imported images may expect real cgroup delegation.
- systemd images will likely fail without much deeper cgroup and namespace support.

Blockers:

- Need Phase 12 if cgroup behavior is tied to per-environment resource isolation.

Must not be done:

- Do not claim systemd support.
- Do not fake successful cgroup writes that are not enforced.
- Do not expose iOS memory pressure APIs directly to Linux userspace.

## 6. Test Strategy

Deterministic test layers:

- Static invariant tests
  - Active source has no IXLand runtime names.
  - Active source has no OrlixKit compatibility names.
  - OrlixKernel overlay has no Darwin/Foundation/POSIX host includes.
  - OrlixKernel overlay has no OrlixHostAdapter includes.
  - No Apple container/containerization runtime dependency exists in iOS targets.

- VFS and mount tests
  - Linux path resolution cases: `.`, `..`, symlinks, hardlinks, trailing slash, `ENOENT`, `ENOTDIR`, `ELOOP`.
  - mount dispatch: ext4, tmpfs, procfs, sysfs, devtmpfs, devpts, overlay.
  - mount namespace isolation.
  - `/proc/self/mountinfo` expected entries.
  - environment root binding behavior.

- Storage policy tests
  - Application Support for persistent Linux state.
  - Caches for downloaded/cache blobs.
  - host temp only for import scratch.
  - Linux `/tmp` is tmpfs.
  - `/etc`, `/usr`, `/var/lib`, and home persist according to environment policy.
  - Documents is not root.
  - Documents appears only as an explicit mount when that feature exists.
  - External security-scoped folder mount behavior where feasible.

- procfs/devfs/sysfs tests
  - `/proc/self`, `/proc/self/fd`, `/proc/mounts`, `/proc/self/status`.
  - `/dev/console`, `/dev/null`, `/dev/urandom`, `/dev/pts`.
  - `/sys/bus/virtio/devices` or equivalent expected sysfs device shape.

- Environment tests
  - default environment boot.
  - empty or copied named environment creation.
  - isolated root writes.
  - isolated mount namespace.
  - imported tar environment.
  - OCI layout environment.
  - environment-local `/etc/os-release`.

- Import tests
  - tar path traversal rejection.
  - tar symlink and hardlink preservation.
  - tar uid/gid/mode preservation.
  - OCI digest verification.
  - OCI missing blob rejection.
  - OCI platform selection.
  - OCI whiteout semantics.
  - OCI opaque directory semantics.

- Overlay tests
  - directory merge.
  - copy-up.
  - unlink hides lower file.
  - lower image remains unchanged.
  - snapshot persists after restart.

- Virtio tests
  - virtqueue descriptor/ring behavior.
  - feature negotiation.
  - reset/status/completion.
  - virtio-rng getrandom and `/dev/urandom`.
  - virtio-console readiness and PTY integration.
  - virtio-blk persistence and flush.
  - future virtio-fs host-backed mount behavior.

- Linux errno and syscall tests
  - openat/fstatat/renameat/unlinkat/linkat.
  - fork/exec/wait.
  - pipe and PTY readiness.
  - poll/select/epoll.
  - futex wait/wake.
  - signals.
  - uid/gid/chown/chmod.
  - namespace syscalls.

- Oracle comparison tests
  - Same C test program.
  - Same fixture filesystem.
  - Real Linux result captured.
  - Orlix result captured.
  - Compare stdout, stderr, exit status, errno, stat metadata, directory mutation, signal behavior, and fd behavior.

## 7. Linux Oracle Plan

Goal:

- Build a Mac-only comparison harness that treats real Linux behavior as the reference for Orlix conformance.

Location:

- proposed `tools/orlix-linux-oracle`

Representation:

- Test cases as JSON plus C or shell fixtures.
- Each case declares command, argv, env, cwd, uid/gid expectation if needed, filesystem fixture, and comparison rules.
- C fixtures compile for `linux/arm64`.

Execution model:

- Real Linux runner:
  - Apple `container` CLI may be used externally on macOS.
  - Alternative real-Linux runners may be supported if available.
- Orlix runner:
  - OrlixTestRunner or app-hosted OrlixOS proof driver runs the same fixture and command.

Comparison outputs:

- stdout
- stderr
- exit status
- errno values
- signal termination
- stat metadata
- filesystem mutations
- fd readiness behavior
- `/proc` and mount observations where relevant

CI split:

- CI should always run parser tests, invariant tests, deterministic Orlix tests, and fixture validation.
- Real-Linux oracle execution enters CI only when the runner is available and stable.
- Apple container remains local developer-only or Mac-CI-only until explicitly provisioned.

Dependency boundary:

- Apple container can be an external oracle runner.
- Apple container/containerization must not be linked into OrlixKernel.
- Apple container/containerization must not become an iOS runtime dependency.
- Virtualization.framework must not become an iOS runtime dependency.

## 8. App Store And Host Compliance Plan

Sensitive areas:

- downloading executable payloads
- running imported binary rootfs images
- user-visible product claims
- sandbox storage
- external folder access
- background execution
- network access
- security-scoped resources

[UNVERIFIED] I cannot verify App Store acceptance for imported or downloaded executable Linux root filesystems from repository code alone. This requires product and legal review.

Architectural requirements:

- All persistent state stays inside the app container or user-selected locations.
- External folders require user selection and security-scoped access.
- No private APIs.
- No JIT requirement.
- No VM runtime dependency.
- No host privilege escalation.
- No raw host path exposure as Linux truth.
- No background daemon behavior without explicit product review.
- Registry pull must be separately review-gated before iOS runtime shipment.
- User-visible claims must avoid implying Docker daemon, privileged containers, systemd, kernel modules, or arbitrary server hosting.

## 9. Naming And Compatibility Cleanup

Current inspected state:

- No active IXLand references were found in source scan outside historical context.
- `LegacyOrlix` appears in docs as retired prototype context.
- `OrlixKit` appears in docs as a forbidden or old direction.
- Build-only host compatibility helpers exist under `tools` and kbuild support paths. These are not public runtime compatibility surfaces.

Plan rules:

- Do not add IXLand aliases, compatibility names, compatibility wrappers, or old branding.
- Do not recreate `OrlixKit`.
- Do not introduce hidden compatibility aliases.
- Do not name Linux concepts with custom Orlix-specific names.
- Do not clone Linux ABI constants or structs.
- Do not expose Darwin-shaped public surfaces for Linux behavior.
- Use upstream Linux names for upstream Linux concepts.
- Use Orlix-specific names only for Orlix product packaging, Orlix arch port code, private host mechanics, and app-facing OrlixOS APIs.

## 10. Risk Register

| Risk | Severity | Likelihood | Mitigation | Proof Required |
|---|---:|---:|---|---|
| VFS semantics drift | High | Medium | Use upstream Linux VFS and mount namespaces. Add oracle tests. | openat, rename, link, unlink, mountinfo, namespace tests match Linux. |
| Linux ABI drift | High | Medium | Use headers_install and upstream Linux UAPI only. Add static scans. | No local UAPI clones. MLibC uses generated Linux headers. |
| Host leakage | High | Medium | Enforce include and dependency checks. Keep HostAdapter private. | OrlixKernel overlay has no HostAdapter/Darwin/POSIX includes. |
| Overbuilding virtio before VFS is correct | Medium | High | Complete storage and mount namespace proof before virtio-fs/net expansion. | Phase 2 and 4 proofs green before Phase 7 expansion. |
| OCI support mistaken for Docker support | Medium | High | Product naming uses OCI-derived environments. No Docker daemon/runc claims. | Docs, CLI help, tests show named environments first. |
| App Store review risk | High | Medium | Gate registry pull and executable payload downloads behind review. | Product/legal decision recorded before shipment. |
| Imported binary compatibility risk | High | High | Start with BusyBox/Alpine arm64. Add ELF/syscall/oracle tests. | Imported `/bin/sh` and basic commands run with expected behavior. |
| Missing namespace behavior | High | Medium | Add namespace tests before environment feature claims. | Mount namespace, UTS, PID view tests pass or are explicitly scoped. |
| Cgroup expectations | Medium | High | Start with Linux-shaped cgroup v2 shape and explicit unsupported behavior. | cgroup tests match Linux for supported paths and errno for unsupported writes. |
| Networking expectations | High | High | Shared outbound first, virtio-net later, oracle rtnetlink tests. | `/proc/net`, rtnetlink, loopback, TCP/UDP tests pass for supported scope. |
| systemd images failing | Medium | High | Declare first implementation excludes systemd. | systemd images remain non-goal until cgroup, PID, tmpfs, dbus, and service tests exist. |
| Tests proving host behavior instead of Linux behavior | High | Medium | Separate HostAdapter tests from Linux conformance tests. | Every runtime claim has app-hosted Linux or oracle evidence. |
| Apple container code becoming runtime dependency | High | Low | Static dependency tests and package manifest checks. | iOS targets do not link Apple container/containerization or Virtualization.framework. |
| Whiteout semantics wrong | High | Medium | Add OCI whiteout and opaque directory fixtures. | Oracle and deterministic fixture tests match expected merged tree. |
| Security-scoped mount leakage | High | Medium | Mount only through Linux filesystem path, never raw host path. | External folder tests prove no raw host path appears in Linux. |

## 11. Explicit Non-Goals

- No Docker daemon.
- No runc dependency.
- No Linux VM on iOS.
- No Virtualization.framework dependency in iOS runtime.
- No Apple container runtime dependency in iOS runtime.
- No vminitd.
- No vmnet.
- No Rosetta.
- No systemd target in first implementation.
- No privileged containers.
- No kernel modules.
- No raw host path root.
- No local Linux UAPI clones.
- No Darwin/libc/MLibC leakage into OrlixKernel.
- No hidden compatibility aliases for IXLand.
- No new OrlixKit.
- No custom Orlix syscall ABI for container execution.
- No HostAdapter-owned Linux semantics.
- No custom VFS outside upstream Linux.

## 12. Open Questions

1. Can current Orlix device builds run arbitrary linux/arm64 musl dynamic binaries without additional ELF, auxv, loader, TLS, mmap, or syscall fixes?
   - Recommended answer path: use BusyBox static first, then Alpine `/bin/sh`, then run oracle ELF/syscall probes.

2. Should the first environment entry surface be an OrlixOS Swift test API, a Linux userspace `orlix-env` command, or both?
   - Recommended default: add OrlixOS SPI for tests first, then expose a normal Linux userspace command after mount namespace proof.

3. Can virtio-fs be enabled cleanly with the current upstream Linux object selection?
   - Recommended answer path: inspect required upstream virtio-fs/FUSE objects and add a minimal mount proof before external folder product work.

4. Is registry pull allowed in the iOS runtime product?
   - Recommended answer path: product/legal review before Phase 10. Until then, keep registry work Mac-only or fixture-only.

5. Which CI runner can execute the real-Linux oracle?
   - Recommended answer path: inspect CI configuration and provision Apple container or another real Linux runner explicitly.

6. Should per-environment roots be separate ext4 images first, or directory trees packed into ext4 only at launch?
   - Recommended default: separate ext4 image per base/state because current Orlix boot and virtio-blk path already use ext4 images.

7. Should Phase 9 use upstream OverlayFS only, or build an Orlix-specific layer merger?
   - Recommended default: upstream OverlayFS only. Build custom layer behavior only if oracle tests prove OverlayFS cannot satisfy the target.

8. What exact product wording is allowed for OCI-derived environments?
   - Recommended answer path: avoid Docker claims, say "OCI-derived Linux environments" until product/legal approves stronger language.

## 13. Final Recommended Execution Order

1. Reconcile active plan status.
   - Proof: `PLAN.md`, `IMPLEMENT.md`, and `GOAL.md` no longer send agents to already-proved work.

2. Refresh baseline iOS Simulator proofs.
   - Proof: focused OrlixOS and OrlixPTYRuntime tests for current materialized tar and OCI roots pass.

3. Add mandatory plan-context harness gate.
   - Proof: mutation before plan read is blocked, mutation after plan read is allowed.

4. Add active-plan update gate.
   - Proof: commit/push after mutation is blocked until active `IMPLEMENT.md` is updated.

5. Product-shaped named environment session API.
   - Proof: OrlixOS can select a named environment root and descriptor through a public app-facing session path.

6. Cross-boot writable state persistence.
   - Proof: mutate environment state, shut down, restart same environment, observe mutation persisted while base image stayed unchanged.

7. Product import-to-enter flow.
   - Proof: tar import and OCI layout import both create environments that can be entered without test-only wiring.

8. OCI Runtime config parser and schema validation.
   - Proof: minimal Linux `config.json` validates against pinned schemas and converts to Orlix descriptors.

9. OCI Runtime lifecycle model.
   - Proof: `create` prepares resources without executing, `start` executes, `state` reports correct status, `kill` sends Linux signal, `delete` removes created resources only.

10. OCI Linux runtime defaults.
    - Proof: fd policy and `/dev/fd`, `/dev/stdin`, `/dev/stdout`, `/dev/stderr` match `runtime-linux.md`.

11. OCI feature report.
    - Proof: generated feature JSON validates and does not overclaim.

12. Runtime host-folder mount backend.
    - Proof: Documents and security-scoped folders enter only as Linux mounts, not raw host paths.

13. virtio-fs.
    - Proof: host-backed folder appears through Linux-owned mount behavior and passes path/stat/open/rename/unlink tests.

14. Linux oracle expansion.
    - Proof: same fixture produces real-Linux and Orlix JSON results, comparator catches drift.

15. Native performance benchmark suite.
    - Proof: ELF launch, syscall round trip, file IO, pipe, PTY, futex, and process lifecycle benchmarks have repeatable iOS Simulator baselines.

16. Product `orlix run`.
    - Proof: argv/env/cwd/user/stdio/lifecycle/exit status work through Linux exec, not HostAdapter command execution.

17. Registry pull tooling.
    - Proof: registry pull produces the same verified OCI layout input as local layout import, with no OrlixKernel or iOS runtime dependency on Apple container.

18. Networking and namespace expansion.
    - Proof: virtio-net, `/proc/net`, rtnetlink, and staged network namespace behavior have focused tests.

19. Virtual cgroup v2 and resource accounting.
    - Proof: synthetic cgroup v2 tree and resource behavior match declared feature support.
