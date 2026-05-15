# Orlix Upstream-Linux iOS Architecture Port Specification

## 0. Core Decision

Orlix will not continue as a local rewrite of Linux subsystems.

Orlix will compile upstream Linux into an iOS-embeddable kernel product and adapt only the parts Linux already expects to be architecture-specific, platform-specific, device-specific, boot-specific, build-specific, or host-specific.

The target product is `OrlixKernel.xcframework`.

The architecture port is `arch/orlix`.

The host mediation layer remains `OrlixHostAdapter`.

The Linux-specific Orlix drivers live under `drivers/orlix`.

The repository is `OrlixSystem`. The generated Xcode project remains `OrlixKernel.xcodeproj` for this branch, and the product identity remains `OrlixKernel.xcframework`. Do not invent names such as `orlixios`, `LinuxOrlix`, `OrlixRuntime`, or `OrlixHostKit`.

## 1. Product Identity

Correct names:

- Project/product family: `Orlix`
- Repository: `OrlixSystem`
- Xcode project file for this branch: `OrlixKernel.xcodeproj`
- Kernel product: `OrlixKernel.xcframework`
- Linux architecture port: `arch/orlix`
- Linux driver subtree: `drivers/orlix`
- Host mediation: `OrlixHostAdapter`
- Bootloader: `boot/`

Forbidden names:

- `arch/orlixios`
- `LinuxOrlix`
- `OrlixRuntime`
- `OrlixHostKit`
- runtime facade
- kernel API facade
- entry layer

Inside already scoped directories, do not redundantly prefix filenames with `orlix_`.

Correct examples:

```text
drivers/orlix/block/file.c
drivers/orlix/block/image.c
drivers/orlix/fs/external.c
drivers/orlix/tty/console.c
drivers/orlix/net/device.c
drivers/orlix/char/random.c
```

Wrong examples:

```text
drivers/orlix/block/orlix_block.c
drivers/orlix/fs/orlix_externalfs.c
drivers/orlix/tty/orlix_console.c
```

The directory already provides the Orlix scope.

## 2. Architectural Objective

Orlix must behave as full upstream Linux compiled to run inside one iOS app boundary.

Inside Orlix, Linux behavior should be rich, consistent, and observable by Linux userspace.

Outside Orlix, Orlix must not claim to control iOS processes, host filesystems, host privileges, host mounts, host namespaces, host networking, host devices, host security policy, or host scheduler policy.

The iOS app is the host container. It does not become Linux. It does not manage Linux through a custom runtime API. It boots Linux.

## 3. High-Level System Model

```text
iOS app
  -> boot/ Orlix bootloader
  -> Linux boot contract for arch/orlix
  -> arch/orlix upstream Linux architecture port
  -> start_kernel()
  -> upstream Linux kernel subsystems
  -> init
  -> Linux userspace
```

The bootloader prepares the Linux boot environment. After boot, Linux owns Linux.

## 4. Ownership Model

| Area | Owner |
| --- | --- |
| VFS | upstream Linux |
| task/process model | upstream Linux |
| fd tables | upstream Linux |
| signals | upstream Linux |
| wait/zombie/reaping behavior | upstream Linux |
| cgroups | upstream Linux |
| namespaces | upstream Linux |
| procfs | upstream Linux |
| sysfs | upstream Linux |
| devfs/device model | upstream Linux plus `drivers/orlix` |
| sockets/networking | upstream Linux plus `drivers/orlix/net` |
| syscall semantics | upstream Linux |
| architecture glue | `arch/orlix` |
| Orlix-specific Linux drivers | `drivers/orlix` |
| iOS/Darwin mechanics | `OrlixHostAdapter` |
| boot preparation | `boot/` |
| build/XCFramework packaging | `Makefile` and `project.yml` |
| libc startup/sysdeps | userspace libc layer, not kernel |
| package manager policy | Linux userspace plus Orlix profile policy |

## 5. Non-Negotiable Rules

1. Upstream Linux source is read-only input.
2. Orlix must not rewrite Linux core subsystems when upstream Linux can own them.
3. Do not add local replacements for Linux VFS, task, cgroup, fdtable, procfs, sysfs, signals, wait, namespaces, or sockets.
4. All Linux ABI, UAPI, syscall, errno, structure, flag, ioctl, and constant truth comes from upstream Linux.
5. Do not invent local `orlix_*`, `compat_*`, `bridge_*`, `shim_*`, or wrapper names for Linux concepts.
6. iOS-specific mechanics belong only in `OrlixHostAdapter`.
7. Darwin, Foundation, UIKit, POSIX host headers, pthread headers, and Apple SDK types must not leak into upstream Linux or Linux-facing semantic code.
8. The app boots Linux. It does not expose a fake Linux management API.
9. Raw iOS paths must not become Linux-visible truth.
10. App Store constraints are enforced through boot policy, architecture constraints, host adapter behavior, driver behavior, rootfs/package policy, and config. They do not justify rewriting Linux core.

## 6. Repository Layout

Target structure:

```text
OrlixSystem/
  Linux/
    upstream/
      linux-6.12/
        ... pristine generated upstream Linux source ...
    ports/
      orlix/
        overlay/
          arch/orlix/
            Kconfig
            Makefile
            boot/
            kernel/
            mm/
            include/asm/
          drivers/orlix/
            Kconfig
            Makefile
            block/
            fs/
            tty/
            net/
            char/
        configs/
          appstore_defconfig
          development_defconfig
          enterprise_defconfig
        patches/
          exceptions/
            README.md
  boot/
    loader.c
    params.c
    image.c
    initrd.c
    rootfs.c
    dtb.c
  OrlixHostAdapter/
    fs/
    kernel/
    runtime/
  OrlixKernel/
    include/
      OrlixKernel.h
  project.yml
  Makefile
```

`Makefile` owns upstream bootstrap, disposable worktree preparation, Kbuild invocation, and XCFramework packaging. Do not add scattered `Linux/scripts/*.sh` build wrappers in this branch.

`OrlixKernel/include/OrlixKernel.h` is the product header for the XCFramework. It must expose the minimum bootloader-facing product surface, not Linux syscall facades.

## 7. Boot Model

Follow Linux's boot model.

The app does not instantiate Linux through a fake kernel API. The app invokes the Orlix bootloader. The bootloader prepares the Linux boot contract and transfers control to the Linux kernel image.

Boot flow:

```text
iOS app launches
  -> boot/loader.c
  -> boot/rootfs.c resolves app-private root image through OrlixHostAdapter
  -> boot/initrd.c maps bundled initrd if configured
  -> boot/dtb.c builds or loads machine description
  -> boot/params.c prepares boot parameters
  -> arch/orlix/boot/boot.c receives boot contract
  -> arch/orlix/kernel/setup.c
  -> start_kernel()
```

Bootloader owns kernel image selection, command line, initrd selection, root image selection, memory region description, device description, initial console device, initial block device, security/profile flags, and handoff to the `arch/orlix` boot path.

Bootloader does not own syscalls, VFS, exec, fork, wait, signals, mounts, cgroups, namespaces, procfs, sysfs, fd tables, package execution, or Linux userspace ABI.

Boot parameters use a Linux-shaped boot parameter block:

```c
struct boot_params {
    const char *cmdline;
    uintptr_t memory_base;
    size_t memory_size;
    const void *initrd_base;
    size_t initrd_size;
    const void *dtb_base;
    size_t dtb_size;
    const char *root_device;
    const char *console_device;
    unsigned long flags;
};
```

Long-term, converge this toward a device-tree-style description where practical: `/chosen`, `/memory`, `/orlix`, `/block`, and `/console`.

## 8. Product Surface

The product surface must be bootloader-shaped, not runtime-shaped.

Allowed exported symbol:

```c
int OrlixBoot(const struct boot_params *params);
```

Potentially acceptable product helpers, only if strictly bootloader support:

```c
int OrlixPrepareBootParams(struct boot_params *params);
int OrlixLoadInitrd(...);
int OrlixLoadDeviceTree(...);
```

Forbidden product API shape:

```text
OrlixKernelCreate()
OrlixKernelSyscall()
OrlixKernelOpen()
OrlixKernelRead()
OrlixKernelMount()
OrlixKernelExecve()
OrlixKernelSuspendTask()
OrlixKernelFreezeCgroup()
```

Those bypass Linux or confuse kernel, libc, and product boundaries.

## 9. `arch/orlix`

`arch/orlix` is the Linux architecture port. It provides only what Linux expects an architecture to provide.

Target structure:

```text
arch/orlix/
  boot/
    boot.c
    image.c
    params.c
    initrd.c
    dtb.c
  kernel/
    setup.c
    process.c
    signal.c
    syscall.c
    time.c
    memory.c
    uaccess.c
    irq.c
    traps.c
    lifecycle.c
  mm/
    fault.c
    mmap.c
  include/asm/
    boot.h
    setup.h
    processor.h
    thread_info.h
    current.h
    syscall.h
    uaccess.h
    page.h
    pgtable.h
    signal.h
    irq.h
    io.h
    timex.h
```

Responsibilities include boot handoff, machine setup, kernel thread substrate, task context representation, syscall entry, `copy_to_user` and `copy_from_user`, signal frames, virtual fault handling, clocksource/timer glue, virtual IRQ/event delivery, and memory permission modeling.

Non-responsibilities include VFS semantics, cgroup semantics, namespace semantics, fd table semantics, procfs semantics, package policy, host file path policy, host networking policy, and libc startup.

## 10. `drivers/orlix`

`drivers/orlix` contains Linux drivers and filesystems needed to connect upstream Linux to the iOS app boundary. Do not prefix filenames with `orlix_`.

Block drivers:

```text
drivers/orlix/block/file.c
drivers/orlix/block/image.c
```

They expose app-private files and bundled images as Linux block devices, support root filesystem images, support writable overlay/state images, and keep host path details outside Linux-visible truth.

Linux-visible result: `root=/dev/<linux block device>`. Never expose raw iOS paths such as `/private/var/mobile/Containers/...` as Linux root truth.

Filesystem drivers:

```text
drivers/orlix/fs/external.c
drivers/orlix/fs/documents.c
```

They expose user-selected external directories as explicit Linux mounts, expose user-visible document views deliberately, and represent security-scoped access as mount state. External access states include `active`, `inactive`, `stale`, `revoked`, and `unavailable`. Linux-visible failures include `-EACCES`, `-ESTALE`, `-ENODEV`, and `-EIO`.

TTY drivers:

```text
drivers/orlix/tty/console.c
drivers/orlix/tty/pty.c
```

They connect Linux TTY and PTY behavior to the Orlix app UI, including terminal input/output, termios, job-control signals, and window-size changes.

Networking drivers:

```text
drivers/orlix/net/device.c
drivers/orlix/net/loopback.c
drivers/orlix/net/transport.c
```

They expose Orlix virtual networking to Linux, provide loopback, bridge permitted outbound TCP/UDP through approved host networking where policy allows, and avoid claiming host route/interface mutation.

Character drivers:

```text
drivers/orlix/char/random.c
drivers/orlix/char/lifecycle.c
```

They provide entropy source integration and expose app lifecycle as Linux-visible device/event/power-management input.

## 11. `OrlixHostAdapter`

`OrlixHostAdapter` remains the only iOS/Darwin mediation layer.

Responsibilities include Application Support, Caches, and temporary path discovery; security-scoped resource access; host file backing; host directory iteration; host errno translation; host clocks; host entropy; host allocation backing; host synchronization primitives; host thread or execution substrate mechanics; and iOS lifecycle notification source.

Non-responsibilities include Linux VFS semantics, Linux task semantics, Linux cgroup semantics, Linux signal semantics, Linux namespace semantics, Linux permission policy, Linux package execution policy, and Linux syscall ABI.

## 12. Upstream Linux Source Handling

The source tree is `Linux/upstream/linux-6.12`. This tree is immutable generated input.

The disposable build tree is `Build/linux-work`.

Build process:

1. Delete `Build/linux-work`.
2. Copy `Linux/upstream/linux-6.12` to `Build/linux-work`.
3. Overlay `Linux/ports/orlix/overlay` into `Build/linux-work`.
4. Apply mechanical patches from `Linux/ports/orlix/patches`.
5. Build or configure with `ARCH=orlix LLVM=1`, depending on the proof target.
6. Produce iPhoneOS and simulator objects/libraries.
7. Package `OrlixKernel.xcframework`.

All of these actions are Makefile-owned in this branch.

## 13. Patch Policy

Allowed patches include Kbuild integration not possible through overlay, Apple static library output adaptation, Mach-O section/link adaptation, symbol export for boot handoff, and toolchain compatibility needed for Apple/LLVM build.

Patches touching these paths are forbidden without a written exception:

```text
fs/
kernel/
mm/
ipc/
net/
include/linux/
include/uapi/
```

Any patch touching forbidden paths requires `Linux/ports/orlix/patches/exceptions/<patch-name>.md`.

The exception must explain why overlay could not solve it, why a Linux-native hook was not sufficient, what upstream behavior is preserved, what observable Linux behavior could change, how the patch is tested, and how the patch is expected to rebase.

## 14. Build Profiles

`appstore_defconfig` is the App Store compliant profile. It supports bundled OS-like packages from day one, avoids general-purpose downloaded binary execution, avoids JIT dependency, avoids host privilege claims, and avoids host filesystem or network control claims.

Required App Store profile characteristics include disabled JIT, constrained executable memory, rootfs image support, initrd support, pre-bundled package support, persistent package database support, cache separation, and external mounts only through user-mediated access.

`development_defconfig` is a development profile with broader diagnostics and experimental execution support. It still makes no direct host privilege claims.

`enterprise_defconfig` is an organization-controlled profile. It may permit broader package/repository behavior if distribution policy allows it. It does not define the App Store contract.

## 15. Filesystem and Storage Specification

Map Linux-visible storage to correct iOS storage classes.

Application Support owns persistent Orlix system state, root writable image, package database, private home, `/etc`, `/usr` overlay, and `/var/lib`.

Caches owns package caches, compiler caches, index caches, and `/var/cache`.

Temporary storage owns runtime scratch and `/tmp`.

Documents owns user-visible documents only and explicit mounted views such as `/home/<user>/Documents` or `/mnt`.

Linux-visible mount model:

```text
/                         Linux filesystem image, likely ext4 or overlay stack
/etc                      persistent app-managed state
/usr                      bundled image plus persistent overlay
/var/lib                  persistent app-managed state
/var/cache                cache-backed mount
/tmp                      tmpfs or temp-backed mount
/home/<user>              persistent private home
/home/<user>/Documents    explicit user-visible mount
/proc                     upstream procfs
/dev                      upstream device model plus drivers/orlix
/sys                      upstream sysfs
/sys/fs/cgroup            upstream cgroup2
/mnt/external/<name>      explicit external mount
```

Raw iOS paths are never Linux truth. Documents is never the root filesystem. Temporary storage is never package database or persistent identity. Cache loss must not corrupt persistent Orlix state. External directories enter only through explicit mounts.

## 16. Execution Specification

The target is full Linux execution under upstream Linux semantics.

Do not plan around a fake staged runtime. From design day one, the architecture must support a pre-bundled OS-like package set, root filesystem image, Linux init path, shell-capable userspace, interpreter support, script execution, package database persistence, App Store profile enforcement, JIT-less execution constraints, and future Linux ELF compatibility through the `arch/orlix` execution substrate.

The exec path is upstream Linux. Script interpreter behavior is upstream Linux plus userspace. Dynamic loader behavior belongs to Linux userspace and libc/sysroot. App Store execution allow/deny policy belongs to boot/profile policy plus `arch/orlix` memory/execution constraints. Host executable launch is forbidden as a Linux execution mechanism.

Bundled OS-like tools are first-class. They ship with the app, are part of declared app functionality, are not downloaded after App Review, and may form the base Orlix userspace.

In App Store profile, downloaded content must not silently introduce undisclosed app features. Source-visible user workflows must be represented clearly. Post-install scripts, repositories, package database, and caches must be profile-controlled.

## 17. Memory and JIT Specification

Orlix must not depend on general-purpose JIT or arbitrary writable executable memory for App Store viability.

Executable `mmap` is allowed only when backed by a permitted execution source and profile policy. `mprotect` adding executable permission is denied or constrained according to `appstore_defconfig`. Writable plus executable mapping is denied in App Store profile unless a valid entitlement and profile explicitly permit it. JIT runtimes are disabled by default in App Store profile. Linux-visible errors must be Linux-shaped.

Linux memory semantics belong to upstream Linux mm plus `arch/orlix`. iOS host permission reality belongs to `OrlixHostAdapter` and `arch/orlix` permission enforcement.

## 18. Process Model Specification

The process model is owned by upstream Linux. `arch/orlix` supplies the execution substrate needed by Linux.

Required behavior includes virtual Linux PIDs, TGID, PPID, PGID, SID, parent/child relationships, zombies, wait family behavior, fork, vfork, clone, exec identity preservation, process groups, sessions, init process, and reparenting inside Orlix.

iOS host PIDs are not Orlix PIDs. No Orlix PID implies host process control.

## 19. Signals Specification

Signals are owned by upstream Linux. `arch/orlix` supplies signal frame and return-path support.

Required behavior includes per-thread signal masks, process-directed signals, thread-directed signals, standard signal coalescing, realtime signal queueing where enabled, kill, killpg, raise, sigaction, signal, sigprocmask, sigpending, sigsuspend, pause, job-control signals, and interruptible blocking operations.

Signals never target host processes outside Orlix.

## 20. File Descriptor Specification

File descriptors are owned by upstream Linux.

Required behavior includes Linux fd tables, shared open-file descriptions, close-on-exec, dup semantics, fork/clone sharing and copying semantics, pipes, sockets, devices, event objects, timer objects, readiness, and locks.

Host file descriptors are backing mechanics only, inside `OrlixHostAdapter` or drivers. Darwin fd behavior does not define the Orlix contract.

## 21. Mounts and Namespaces Specification

Mounts and namespaces are owned by upstream Linux.

Required behavior includes Linux mount table, bind mounts, mount namespaces, unmount behavior, busy mount checks, open fd references, cwd/root pinning, `/proc/self/mounts`, and `/proc/self/mountinfo`.

Host mounts are not manipulated. Mount operations affect only Orlix's Linux instance.

## 22. Credentials and Capabilities Specification

Credentials and capabilities are owned by upstream Linux.

Required behavior includes uid, gid, euid, egid, supplementary groups, capabilities, setuid, setgid, Linux file permission checks, and user namespaces where enabled.

Root inside Orlix is not iOS root. `CAP_SYS_ADMIN` inside Orlix is not iOS host authority. `setuid` inside Orlix does not change host user identity. No Linux credential operation changes app entitlements.

## 23. Device Filesystem Specification

The device filesystem is owned by upstream Linux device model plus `drivers/orlix`.

Required devices include `/dev/null`, `/dev/zero`, `/dev/random`, `/dev/urandom`, `/dev/tty`, PTY master/slave devices, root block device, console device, and lifecycle device if exposed.

Unsupported devices fail with Linux-shaped errors. No `/dev` entry implies direct host hardware access unless explicitly mediated by public app APIs.

## 24. TTY, PTY, and Job Control Specification

TTY, PTY, and job control are owned by upstream Linux TTY layer plus `drivers/orlix/tty`.

Required behavior includes an interactive shell seeing a PTY-like terminal, controlling terminal, foreground process group, background read/write job-control behavior, SIGINT, SIGQUIT, SIGTSTP, SIGCONT, SIGWINCH, termios, window-size propagation from app UI, pipelines, background jobs, foreground jobs, suspend/resume, and wait integration.

The app UI is a terminal frontend. Linux owns terminal semantics.

## 25. Pipes, Readiness, Poll, Select, and Epoll

Pipes, readiness, poll, select, and epoll are owned by upstream Linux.

Every Orlix device with fd behavior must implement correct `file_operations`, poll callbacks, interruptible blocking paths, and Linux wait queue wakeup behavior.

Required behavior includes pipes, blocking and nonblocking IO, EOF, poll, select, epoll, level-triggered behavior, edge-triggered behavior where claimed, and signals interrupting blocking operations.

## 26. Networking and Sockets Specification

Networking and sockets are owned by upstream Linux networking plus `drivers/orlix/net`.

Required behavior includes virtual loopback, Unix-domain sockets inside Orlix, TCP/UDP where permitted, IPv6-only compatibility, socket options where supported, virtual interface view, virtual route view, no host route mutation, no host interface mutation, and no raw privileged networking unless explicitly supported and profile-allowed.

Networking must distinguish Orlix virtual network state from host transport mechanics.

## 27. Netlink Specification

Netlink is owned by upstream Linux netlink, backed by Orlix virtual network/device state.

Route queries return Orlix virtual routes. Interface queries return Orlix virtual interfaces. Address queries return Orlix virtual addresses. Device/mount events describe Orlix devices and mounts. Unsupported families fail consistently.

Netlink never implies host network administration.

## 28. Procfs Specification

Procfs is owned by upstream Linux procfs.

Required behavior includes `/proc/self` referring to the current Orlix Linux task, `/proc/<pid>` exposing Orlix tasks only, `/proc/<pid>/fd` reflecting Linux fd tables, `/proc/<pid>/status` reporting Orlix task state, `/proc/mounts` reflecting Orlix mounts, `/proc/self/mountinfo` reflecting the Orlix mount namespace, and `/proc/uptime` reflecting Orlix kernel instance uptime.

Forbidden procfs content includes host PID lists as Linux truth, host kernel settings as Linux truth, host process memory as Linux truth, and iOS device internals as Linux kernel state.

## 29. Sysfs Specification

Sysfs is owned by upstream Linux sysfs.

`/sys` exposes Linux kobjects for the Orlix kernel instance, including Orlix virtual block devices, Orlix virtual network devices, Orlix virtual tty devices, and minimal cgroup-related views where applicable.

Do not expose fake iOS internals as Linux kernel objects or host hardware claims not mediated by approved APIs.

## 30. Cgroups Specification

Cgroups are owned by upstream Linux cgroup implementation.

Required behavior includes a virtual cgroup hierarchy inside Orlix, task assignment, cgroup2 filesystem, pids controller, freezer controller, membership visibility, `/proc` cgroup views, and cgroup namespace views.

Controller policy:

- `pids`: real inside Orlix
- `freezer`: real inside Orlix
- `cpu`: Orlix scheduling/accounting policy only unless stronger support exists
- `memory`: Orlix memory accounting policy, not host jetsam control
- `io`: Orlix block backend accounting only

iOS memory pressure is host lifecycle pressure, not normal cgroup reclaim.

## 31. Namespaces Specification

Namespaces are owned by upstream Linux.

Required behavior includes PID namespaces, mount namespaces, UTS namespaces, user namespaces, cgroup namespaces, network namespaces, and IPC namespaces where enabled.

Namespaces isolate Orlix state. They do not create host isolation beyond the app sandbox. User namespaces do not create iOS privileges. Network namespaces do not mutate the host network stack.

## 32. Seccomp Specification

Seccomp is owned by upstream Linux seccomp where supported by `arch/orlix`.

Required behavior includes syscall filtering at the Orlix Linux syscall boundary, kill/errno/trap/trace actions where supported, process-local policies, virtual task termination, and Linux-shaped syscall result behavior.

Seccomp does not filter host iOS syscalls directly.

## 33. Ptrace Specification

Ptrace is owned by upstream Linux ptrace where the execution substrate supports it.

Required behavior includes attach to permitted Orlix tasks, ptrace permissions through Linux credentials, syscall-entry stops, syscall-exit stops, signal injection, memory inspection, register inspection, and fork/clone/exec/exit events where supported.

Ptrace never debugs host processes.

## 34. Timers, Clocks, Sleep, and Accounting

Time is owned by upstream Linux time core plus `arch/orlix` and `OrlixHostAdapter/kernel/clock.c`.

Required behavior includes `CLOCK_REALTIME`, `CLOCK_MONOTONIC`, nanosleep, interruptible sleep, interval timers, timerfd, process CPU time where supported, `/proc/uptime`, and task accounting.

App suspension must have defined Linux-visible time behavior: pause virtual time, advance virtual time, or record discontinuity. The selected policy must be explicit.

## 35. Scheduler and Lifecycle

Scheduling and lifecycle are owned by upstream Linux scheduler plus `arch/orlix/kernel/lifecycle.c` and `drivers/orlix/char/lifecycle.c`.

Required behavior includes Linux runnable/sleeping task states, nice values, priorities where supported, virtual CPU affinity or Linux-shaped unsupported behavior, foreground app execution, background policy, suspend policy, and recovery policy.

Do not pretend Linux daemons keep running indefinitely in App Store builds. Do not claim host scheduler control. On backgrounding, Orlix must suspend, checkpoint, throttle, or terminate according to visible policy. On relaunch, Orlix must report previous virtual task state honestly.

## 36. Resource Limits

Resource limits are owned by upstream Linux rlimits and Orlix backing enforcement.

Required behavior includes file descriptor limits, process limits, address-space limits where supported, memory limits where supported, CPU limits as virtual accounting/policy, and core limits.

Core dumps may be synthetic, disabled, or user-exported diagnostics. Host jetsam/app termination is outside Orlix control.

## 37. Memory Management

Memory management is owned by upstream Linux mm plus `arch/orlix/mm`.

Required behavior includes per-task virtual address spaces, mmap, mprotect, munmap, brk, shared mappings, copy-on-write where fork depends on it, fault handling, Linux-shaped protection errors, and memory accounting.

There must be no general App Store JIT dependency, no arbitrary writable executable memory, and unsupported executable-memory behavior must fail predictably.

## 38. Shared Memory and IPC

Shared memory and IPC are owned by upstream Linux where enabled.

Required behavior includes pipes, Unix sockets, shared memory, semaphores, message queues, futexes, eventfd, signalfd, and timerfd.

IPC is inside Orlix unless explicit App Group support is configured. App Group support is an optional shared-container backend, not a Linux system-wide IPC namespace. No IPC feature implies communication with arbitrary host processes.

## 39. Randomness

Randomness is owned by upstream Linux random interfaces plus `OrlixHostAdapter/kernel/random.c`.

Required behavior includes `getrandom`, `/dev/random`, `/dev/urandom`, cryptographically appropriate bytes, Linux-shaped errors, and conservative documented blocking behavior.

Host API vocabulary is not exposed to Linux userspace.

## 40. Hostname, Uname, and Machine Identity

Hostname, uname, and machine identity are owned by upstream Linux UTS namespace plus `arch/orlix` configuration.

`uname` reports Orlix Linux runtime identity. Hostname and domainname are virtual. UTS namespace support exists where enabled. Machine architecture matches the Orlix compatibility target.

iOS product names and host kernel details are diagnostic metadata only, not default Linux identity.

## 41. Package Management and Repositories

Package management is owned by Linux userspace and constrained by Orlix profile policy.

Required behavior includes a pre-bundled OS-like package set from day one, persistent package database, cache-backed package downloads, profile-controlled repositories, profile-controlled post-install scripts, and source-visible user workflows where App Store profile requires them.

The App Store profile permits no silent downloaded feature expansion, no unrestricted binary execution platform, and no package behavior that contradicts App Review framing.

Development and enterprise profiles may be broader, but they do not define the App Store contract.

## 42. Logging, Tracing, and Diagnostics

Logging, tracing, and diagnostics are owned by upstream Linux logging/tracing plus Orlix explicit export policy.

Kernel-style logs are Orlix logs. Diagnostics are explicit and scoped. Sensitive tracing is opt-in. Debug exports are user-initiated. Instrumentation must be approved.

Forbidden behavior includes silent terminal input capture, silent file content capture, secret/token capture, hidden host logging side channels, and random stderr diagnostics as proof model.

## 43. App Lifecycle, Persistence, and Recovery

Lifecycle, persistence, and recovery are owned by bootloader, `arch/orlix`, `drivers/orlix/char/lifecycle.c`, and `OrlixHostAdapter`.

Required behavior includes Linux boot initializing kernel state and root environment, backgrounding triggering defined lifecycle policy, termination not being treated as normal Linux shutdown unless Orlix performs shutdown, persistent state surviving relaunch, cache loss being recoverable, temporary loss being tolerated, running virtual tasks after relaunch being restored/restarted/reported terminated, and UI not implying unsupported background daemon execution.

## 44. Libc Boundary

Libc is outside the kernel.

Userspace libc/sysroot owns `_start`, crt objects, libc startup, syscall stubs, errno exposure, open/read/write wrappers, execve wrapper, dynamic loader integration, MLibC sysdeps, and package-facing headers.

Kernel does not own libc startup. `arch/orlix/kernel/syscall.c` owns kernel-side syscall entry mechanics. That is not libc.

## 45. App Store Constraint Boundary

App Store constraints are enforced by `appstore_defconfig`, boot parameters, rootfs/package profile, `arch/orlix` memory/execution constraints, `drivers/orlix` external access behavior, `OrlixHostAdapter` sandbox/security-scope mechanics, lifecycle device/policy, and diagnostic export policy.

They are not enforced by rewriting VFS, task, cgroups, procfs, fd tables, inventing a runtime facade, or translating Linux syscalls into Darwin syscalls.

## 46. Proof Model

Proof truth must remain Xcode-native and Linux-behavior-oriented.

Required proof categories include Kbuild accepting `ARCH=orlix`, upstream Linux worktree generation from immutable source plus overlay, minimal and auditable patch queue, `OrlixKernel.xcframework` builds for iPhoneOS arm64 and iPhoneSimulator arm64, bootloader transfer into `arch/orlix` boot path, Linux reaching `start_kernel`, root image mounting through upstream VFS, procfs/sysfs/devfs/cgroupfs mounting correctly, bundled userspace booting, PTY shell path working, package database persistence, cache purge recovery, external mount revocation handling, and App Store profile rejection of unsupported executable-memory behavior.

Current skeleton proof is narrower: the branch materializes upstream Linux, prepares `Build/linux-work`, runs the Orlix Kbuild configuration step, builds bootloader-facing static library slices, packages `OrlixKernel.xcframework`, runs lint over the current source surface, and builds the Xcode target. It does not yet prove `start_kernel`, runtime filesystems, userspace boot, or App Store execution policy.

Tests should prove Linux behavior, not host adapter behavior alone. `OrlixHostAdapter` tests prove host mechanics only.

## 47. Migration Away From Current Repo Shape

Current local subsystem code under `OrlixKernel/fs`, `OrlixKernel/kernel`, and `OrlixKernel/runtime` should stop being the primary implementation path.

Treat it as prototype reference, behavior notes, test inspiration, or temporary compatibility surface.

Do not treat it as future VFS, task model, cgroup implementation, procfs, devfs, or signal implementation.

Future implementation belongs in upstream Linux, `arch/orlix`, `drivers/orlix`, `boot/`, and `OrlixHostAdapter`.

## 48. Final Rule

Orlix must not imitate Linux by rewriting it. Orlix must compile Linux.

Orlix must adapt Linux only through Linux-native extension points:

- boot
- arch
- drivers
- filesystems
- Kconfig
- device description
- host adapter
- build packaging

After boot, Linux owns Linux.

## Sources

1. Linux Kbuild Makefiles: https://docs.kernel.org/kbuild/makefiles.html
2. Building Linux with Clang/LLVM: https://docs.kernel.org/kbuild/llvm.html
3. Linux VFS documentation: https://docs.kernel.org/filesystems/vfs.html
4. Linux cgroup v2 documentation: https://docs.kernel.org/admin-guide/cgroup-v2.html
5. Linux procfs documentation: https://docs.kernel.org/filesystems/proc.html
6. Linux sysfs documentation: https://docs.kernel.org/filesystems/sysfs.html
7. Linux seccomp filter documentation: https://docs.kernel.org/userspace-api/seccomp_filter.html
8. Apple Platform Security, runtime process security: https://support.apple.com/en-gb/guide/security/sec15bfe098e/web
9. Apple App Review Guidelines: https://developer.apple.com/app-store/review/guidelines/
10. Apple File System Programming Guide, iOS standard directories: https://developer.apple.com/library/archive/documentation/FileManagement/Conceptual/FileSystemProgrammingGuide/FileSystemOverview/FileSystemOverview.html
11. Apple security-scoped resource access: https://developer.apple.com/documentation/foundation/url/startaccessingsecurityscopedresource()
