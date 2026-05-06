# Native Package Compatibility Roadmap

## Status

Authoritative program roadmap for Version 1 native-only package compatibility.

This document is the top-level roadmap.

It depends on `01-IXLandKernel_IXLandHostAdapter_Split_Plan.md` as Milestone 0 and does not replace it.

## Product Goal

Build Linux-oriented packages as native iOS arm64 code, with zero target-package source modifications, while preserving Linux-identical observable behavior at the userspace surface.

The package should:

- configure unchanged
- compile unchanged
- install unchanged into the IXLand virtual filesystem
- run as native iOS code
- observe Linux behavior through `IXLandMLibC` plus `IXLandKernel`

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

### IXLandMLibC owns

- package-facing Linux libc headers
- libc-owned typedefs and APIs
- sysroot ownership
- Linux-oriented sysdeps for native iOS package builds

### IXLandKernel owns

- Linux syscall-visible semantics
- virtual kernel behavior
- Linux runtime semantics
- VFS, task, signal, wait, PTY, futex, procfs, devfs, and related kernel subsystems

### IXLandHostAdapter owns

- iOS and Darwin host mechanics only
- private mediation under `IXLandHostAdapter/internal/ios/**`

### Repo Integration

- the repository and Xcode project may still be named `IXLandSystem`
- architectural ownership remains split between `IXLandKernel` and `IXLandHostAdapter`

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

`IXLandHostAdapter` split and boundary lockdown

Authoritative detail lives in:

- `01-IXLandKernel_IXLandHostAdapter_Split_Plan.md`

This milestone is mandatory before all others.

### Milestone 1

Sysroot and build truth

Goals:

- make package-facing headers and typedef ownership correct
- make configure and compile probes reliable
- enforce UAPI versus kheaders separation

Detailed plan:

- `02-Sysroot_Build_Truth_Plan.md`

### Milestone 2

Native exec, shebang, and execution-image plumbing

Goals:

- implement native-only execution cleanly
- preserve future backend extensibility
- make script execution a first-class feature

Detailed plan:

- `03-Exec_Shebang_Native_Execution_Plan.md`

### Milestone 3

Shell-critical task, signal, and PTY runtime

Goals:

- make `zsh` viable
- make process model, signals, wait, sessions, job control, and PTY semantics Linux-shaped

Detailed plan:

- `04-Shell_Process_Signal_PTY_Plan.md`

### Milestone 4

VFS and runtime environment

Goals:

- make the Linux virtual filesystem environment real enough for shells and package workflows
- stabilize `/etc`, `/usr`, `/var`, `/tmp`, `/run`, `/proc`, and `/dev`

Detailed plan:

- `05-VFS_Runtime_Environment_Plan.md`

### Milestone 5

Networking for `curl`

Goals:

- provide sockets, DNS, timeout behavior, and readiness semantics sufficient for `curl`

Detailed plan:

- `06-Networking_Curl_Plan.md`

### Milestone 6

Package proof program

Goals:

- prove the product with real packages instead of unit tests alone
- use `zsh` as the gating shell proof and `curl` as the gating network proof

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
4. No Darwin leakage into `IXLandKernel`.
5. No libc-owned typedef reinvention inside `IXLandKernel`.
6. No shallow stubs presented as completed kernel support.
7. Real package behavior outranks local convenience.
8. If Linux userspace behavior breaks, fix IXLand, not the package.

## Definition Of Version 1 Success

Version 1 is successful only when all are true:

1. Native Linux-oriented package builds work through `IXLandMLibC` without package source changes.
2. `zsh` behaves like a Linux shell on the surface.
3. `curl` behaves like a Linux client on the surface.
4. Shebang-driven scripts work.
5. The execution architecture remains cleanly extensible to future ELF and WASM backends.
