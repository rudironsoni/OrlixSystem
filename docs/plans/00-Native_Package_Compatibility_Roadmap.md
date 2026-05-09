# Native Package Compatibility Roadmap

## Status

Authoritative program roadmap for Version 1 native-only package compatibility.

This document is the top-level roadmap.

It depends on `01-OrlixKernel_OrlixHostAdapter_Split_Plan.md` as Milestone 0 and does not replace it.

All later milestones inherit the Milestone 0 constraint:

- the structural split landed and the old branded kernel-facing seam was removed from `OrlixKernel`
- the accepted end-state removed `internal/ios`, removed `OrlixHostAdapter/include`, and moved the active cross-target seam to kernel-owned private declarations
- remaining Milestone 0 debt is now narrower: `OrlixHostAdapter` still sees broader `OrlixKernel` roots than ideal in `project.yml`, and host-backed runtime primitives still need Linux-shaped behavioral proof rather than assumption

No later milestone may regress back to branded adapter-owned kernel-facing vocabulary or treat a host primitive as correct without Linux-visible proof.

## Current Program State

The roadmap is not starting from zero:

- Milestone 0 is structurally delivered enough to unblock later work.
- Milestone 1 is delivered and simulator-proofed.
- Milestone 2 is now the active milestone.
- The current repo already has:
  - `OrlixMLibC` bootstrap ownership for the first package-facing libc header set
  - compile-smoke proof for vendored UAPI, kheaders classification, package-facing libc headers, and configure-style bootstrap probes
  - the `fs/readdir.c` host-directory iteration cut over to a kernel-owned private backing seam

The roadmap therefore needs to stay honest about two different truths at once:

- real M1 closure has landed
- later milestones still have substantial runtime and userspace work ahead

## Product Goal

Build Linux-oriented packages as native iOS arm64 code, with zero target-package source modifications, while preserving Linux-identical observable behavior at the userspace surface.

The package should:

- configure unchanged
- compile unchanged
- install unchanged into the Orlix virtual filesystem
- run as native iOS code
- observe Linux behavior through `OrlixMLibC` plus `OrlixKernel`

Primary proof target:

- `zsh`

Secondary proof target:

- `curl`

## Version Scope

### Version 1

- Native iOS arm64 execution only
- No target-package source modifications
- Shebangs and script execution are mandatory
- No native Linux ELF execution in this version
- No WASM execution in this version

### Future Versions

- Native Linux ELF execution through emulation
- WASM execution

These future backends must be planned for now in the execution architecture, but they are not product requirements for Version 1 and must not distort the native-first kernel roadmap.

## Product Contract

### OrlixMLibC owns

- package-facing Linux libc headers
- libc-owned typedefs and APIs
- sysroot ownership
- Linux-oriented sysdeps for native iOS package builds

### OrlixKernel owns

- Linux syscall-visible semantics
- virtual kernel behavior
- Linux runtime semantics
- VFS, task, signal, wait, PTY, futex, procfs, devfs, and related kernel subsystems

### OrlixHostAdapter owns

- iOS and Darwin host mechanics only
- private mediation under `OrlixHostAdapter/**`
- no adapter-owned include surface for kernel consumption

### Repo Integration

- the main project and Xcode project are now named `OrlixKernel`
- architectural ownership remains split between `OrlixKernel` and `OrlixHostAdapter`
- `OrlixSystem` is a retired name and must not be reintroduced as an active owner or product identity

## Execution Model

Version 1 must use a backend-neutral execution pipeline with only native iOS execution enabled.

Required execution image kinds in architecture:

- native image, implemented now
- script image, implemented now through shebang handling
- ELF image, deferred
- WASM image, deferred

`execve` semantics must be Linux-shaped regardless of backend:

- path resolution
- permission checks
- shebang parsing
- argv and envp rules
- fd inheritance and `FD_CLOEXEC`
- signal reset rules on exec
- thread-group collapse on exec

Version 1 must not depend on ELF loading or emulation to succeed.

## Why zsh Is The Primary Proof Target

`zsh` stresses the hardest high-value Linux behaviors early:

- process creation and exec
- signals
- PTY and job control
- sessions and process groups
- pipes and redirections
- shell startup files and runtime environment
- procfs assumptions
- shebang execution

If `zsh` works correctly, large parts of the kernel surface are already forced into Linux fidelity.

## Why curl Is The Secondary Proof Target

`curl` stresses a different but equally important slice:

- configure and build transparency
- sockets
- DNS and name resolution
- timeout and error behavior
- poll and select readiness
- file I/O integration
- network reliability under native execution

## Milestone Sequence

### Milestone 0

`OrlixHostAdapter` split and boundary lockdown

Authoritative detail lives in:

- `01-OrlixKernel_OrlixHostAdapter_Split_Plan.md`

This milestone is mandatory before all others.

Its acceptance standard is strict:

- separate targets were necessary but insufficient
- the old branded kernel-facing seam is now removed from active `OrlixKernel` code
- the remaining acceptance burden is behavioral: host-backed primitives must still match Linux-visible expectations under simulator proof
- the end-state remains kernel-owned private contracts, with `OrlixHostAdapter` kept private and narrower in dependency scope over time

### Milestone 1

Sysroot and build truth

Goals:

- make package-facing headers and typedef ownership correct
- make configure and compile probes reliable
- enforce UAPI versus kheaders separation
- keep Milestone 1 from normalizing branded `OrlixHostAdapter` seam vocabulary or adapter-owned headers as package-facing truth

Current delivered learning:

- `OrlixMLibC` bootstrap ownership is now real enough to host package-facing `fcntl`, `poll`, `signal`, `sys/stat`, `sys/types`, `sys/uio`, `unistd`, `time`, `sys/time`, `sys/socket`, and `sys/select` surfaces
- package-facing compile smoke now includes both direct libc header proof and a configure-style probe file
- host-backed directory iteration for `fs/readdir.c` is now behind a kernel-owned private backing contract instead of ambient `dirent.h` usage in Linux-owner code

Current delivered learning:

- Milestone 1 was not a license to force every host-libc-shaped kernel include out in one tranche
- the attempted `time` / `sys/time` / `sys/socket` / `sys/select` kernel cutover only became valid once it was split into explicit, simulator-proven ownership tranches
- the repo now has green bounded proof for package-facing bootstrap ownership, internal `poll` / `select` cleanup, internal and public socket-surface cleanup, and `time` / `sys/time` cleanup without host-facing drift
- the durable rule is to keep package-facing bootstrap proof and deeper kernel-owner cleanup separate unless a tranche proves they can move together safely

Current next executable step:

- keep Milestone 2 active
- move to native exec, shebang, and execution-image plumbing on top of the now-green Milestone 1 ownership baseline
- preserve the Milestone 1 tranche rule: bounded Linux-owner progress with simulator proof, not broad concept sweeps

Detailed plan:

- `02-Sysroot_Build_Truth_Plan.md`

### Milestone 2

Native exec, shebang, and execution-image plumbing

Goals:

- implement native-only execution cleanly
- preserve future backend extensibility
- make script execution a first-class feature
- keep exec-path ownership Linux-first, with host mechanics remaining private and non-authoritative

Detailed plan:

- `03-Exec_Shebang_Native_Execution_Plan.md`

Activation rule:

- do not treat Milestone 2 as the active coding tranche while Milestone 1 still has known ownership drift that materially affects package configure and build truth
- Milestone 2 becomes the active tranche only after the remaining M1 build-truth work is narrow enough that exec-path work is no longer being built on ambiguous ownership

### Milestone 3

Shell-critical task, signal, and PTY runtime

Goals:

- make `zsh` viable
- make process model, signals, wait, sessions, job control, and PTY semantics Linux-shaped
- do not resolve shell-critical semantics by preserving branded host-facing contracts as the kernel model

Detailed plan:

- `04-Shell_Process_Signal_PTY_Plan.md`

### Milestone 4

VFS and runtime environment

Goals:

- make the Linux virtual filesystem environment real enough for shells and package workflows
- stabilize `/etc`, `/usr`, `/var`, `/tmp`, `/run`, `/proc`, and `/dev`
- keep host root discovery and backing storage strictly private to `OrlixHostAdapter`

Detailed plan:

- `05-VFS_Runtime_Environment_Plan.md`

### Milestone 5

Networking for `curl`

Goals:

- provide sockets, DNS, timeout behavior, and readiness semantics sufficient for `curl`
- keep host network plumbing private and avoid promoting branded host seam vocabulary or adapter-owned headers into Linux-facing contracts

Detailed plan:

- `06-Networking_Curl_Plan.md`

### Milestone 6

Package proof program

Goals:

- prove the product with real packages instead of unit tests alone
- use `zsh` as the gating shell proof and `curl` as the gating network proof
- fail any milestone claim that still depends on branded host vocabulary as part of the kernel-facing story

Detailed plan:

- `07-Package_Proof_Program_Plan.md`

## Package Proof Targets

### Primary gate

- `zsh`

Minimum proof ladder:

1. configure unchanged
2. build unchanged
3. install unchanged
4. `zsh -c 'echo hello'`
5. startup file handling
6. pipelines and redirections
7. `sleep 1 & wait`
8. PTY foreground and background job control
9. shebang script execution through the common exec path

### Secondary gate

- `curl`

Minimum proof ladder:

1. configure unchanged
2. build unchanged
3. install unchanged
4. HTTP GET
5. HTTPS GET
6. DNS lookup path
7. timeout behavior
8. redirected output and file handling

### Additional follow-on packages

- `busybox`
- `grep`
- `sed`
- `gawk`
- `make`
- later: `git`, `tar`, `xz`, `less`

## Guardrails

1. No target-package source modifications in Version 1.
2. No ELF execution dependency in Version 1.
3. No WASM execution dependency in Version 1.
4. No Darwin leakage into `OrlixKernel`.
5. No libc-owned typedef reinvention inside `OrlixKernel`.
6. No milestone is complete if the kernel-facing surface still depends on branded host-adapter vocabulary as normal contract surface.
7. No adapter-owned header surface is accepted as the kernel-facing seam.
8. No shallow stubs presented as completed kernel support.
9. Real package behavior outranks local convenience.
10. If Linux userspace behavior breaks, fix Orlix, not the package.
11. Do not compress package-facing bootstrap work and deeper kernel-owner ownership cleanup into one tranche unless simulator proof shows they are ABI-safe together.

## Definition Of Version 1 Success

Version 1 is successful only when all are true:

1. Native Linux-oriented package builds work through `OrlixMLibC` without package source changes.
2. `zsh` behaves like a Linux shell on the surface.
3. `curl` behaves like a Linux client on the surface.
4. Shebang-driven scripts work.
5. The execution architecture remains cleanly extensible to future ELF and WASM backends.
