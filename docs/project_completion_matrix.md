# IXLand Project Completion Matrix

Generated from `docs/plans/*` and current repo reality.

This document is the project-wide tracking matrix for Version 1 native-only package compatibility and the planned future execution architecture.

It complements:

- `docs/syscall_gap_matrix_6.12_arm64.md`
- `docs/plans/00-Native_Package_Compatibility_Roadmap.md`
- `docs/plans/01-IXLandKernel_IXLandHostAdapter_Split_Plan.md`
- `docs/plans/02-Sysroot_Build_Truth_Plan.md`
- `docs/plans/03-Exec_Shebang_Native_Execution_Plan.md`
- `docs/plans/04-Shell_Process_Signal_PTY_Plan.md`
- `docs/plans/05-VFS_Runtime_Environment_Plan.md`
- `docs/plans/06-Networking_Curl_Plan.md`
- `docs/plans/07-Package_Proof_Program_Plan.md`

## Status Vocabulary

| status | meaning |
| --- | --- |
| `implemented` | artifact exists and has enough proof for its current claimed scope |
| `partial` | artifact exists, but architecture, semantics, or proof are incomplete |
| `planned` | artifact is required and defined in plans, but not yet established enough to claim partial completion |
| `blocked` | artifact is intentionally downstream of another prerequisite milestone |
| `deferred-v2` | artifact is intentionally out of Version 1 scope and reserved for future versions |

## Program Milestones

| milestone | group | artifact | required by | status | observation |
| --- | --- | --- | --- | --- | --- |
| `M0` | roadmap | `IXLandHostAdapter` split and boundary lockdown | all later milestones | `planned` | Explicitly defined as the prerequisite before all other roadmap work. |
| `M1` | roadmap | sysroot and build truth | package configure and build transparency | `blocked` | Must follow the split so ownership and seam discipline are enforced by construction. |
| `M2` | roadmap | native exec plus shebang execution | `zsh`, scripts, Version 1 runtime | `blocked` | Version 1 is native-only, but execution-image plumbing must stay future-extensible. |
| `M3` | roadmap | shell-critical process, signal, and PTY runtime | `zsh` primary proof | `blocked` | High-value Linux semantics cluster for shells and interactive use. |
| `M4` | roadmap | VFS and runtime environment | shell startup, package runtime, `/proc`, `/dev` | `blocked` | Linux environment shape must be stable enough for package workflows. |
| `M5` | roadmap | networking for `curl` | `curl` secondary proof | `blocked` | Socket and DNS semantics must be proven against a real client. |
| `M6` | roadmap | package proof program | Version 1 product validation | `blocked` | Final product proof must be package-driven, not unit-test-only. |

## IXLandKernel: Architecture And Ownership

| subgroup | artifact | required by | status | observation |
| --- | --- | --- | --- | --- |
| ownership | `IXLandKernel` target or package | M0 | `implemented` | Owns the physical `IXLandKernel/fs/**`, `IXLandKernel/kernel/**`, `IXLandKernel/runtime/**`, and `IXLandKernel/include/**` trees. |
| boundary | no Darwin, Foundation, UIKit, pthread, or dispatch headers in Linux-owner code | M0 | `partial` | Plans require this, but current repo still has host-shaped abstractions and direct host seam imports to eliminate. |
| boundary | no arbitrary host implementation header visibility | M0 | `planned` | Must be enforced by target graph and include graph, not just convention. |
| boundary | curated exported seam imports only | M0 | `partial` | Linux-owner code now imports exported adapter seams instead of raw `IXLandHostAdapter/internal/ios/**` headers, but the seam still needs further narrowing. |
| header discipline | Linux UAPI as production ABI truth | M1 | `partial` | Main target already uses vendored UAPI include root, but broader ownership cleanup remains. |
| header discipline | kheaders as classified private reference only | M1 | `partial` | Current project has kheaders smoke, but the project-wide classification discipline is not yet complete. |
| type ownership | no libc-owned typedef reinvention in kernel | M1 | `partial` | Repo still contains Linux-like typedef recreation and host seam leakage that need cleanup. |
| architecture | backend-neutral execution architecture | M2 | `planned` | Needed now so Version 1 native execution does not block future ELF or WASM. |

## IXLandKernel: Process And Execution Runtime

| subgroup | artifact | required by | status | observation |
| --- | --- | --- | --- | --- |
| exec | Linux-shaped `execve` semantics for native binaries | M2, `zsh` | `partial` | Repo already has process and exec support, but full native-exec milestone proof is not yet established. |
| exec | shebang parsing and interpreter chaining | M2, scripts | `planned` | Mandatory for Version 1 and explicitly required by roadmap. |
| exec | `execveat` semantics | M2 | `partial` | Syscall surface exists, but full milestone proof remains part of execution plan. |
| exec | `FD_CLOEXEC` semantics across exec | M2, shell correctness | `partial` | Critical for shell and process correctness; must be covered in milestone proof. |
| exec | thread-group collapse on exec | M2, shell correctness | `partial` | Called out in plans as mandatory Linux-visible behavior. |
| process | task and process model | M3, `zsh` | `partial` | Large subsystem exists, but shell-critical completeness is not yet claimed. |
| process | fork semantics | M3, `zsh` | `partial` | Present in repo, but package-proof-level shell behavior is still pending. |
| process | vfork semantics | M3, `zsh` | `partial` | Repo has a simulation path; plans explicitly warn against calling it complete too early. |
| process | clone and clone3 semantics | M3, future process fidelity | `partial` | Existing code is present, but full Linux dependency and conflict behavior still needs milestone proof. |
| process | waitpid and waitid semantics | M3, `zsh` | `partial` | Existing code and tests exist, but shell-proof maturity remains downstream. |
| process | sessions and process groups | M3, `zsh` | `partial` | Present in code and tests, but still part of shell-critical milestone. |

## IXLandKernel: Signal, PTY, And Job Control

| subgroup | artifact | required by | status | observation |
| --- | --- | --- | --- | --- |
| signal | shared dispositions plus per-task mask and pending design | M3, `zsh` | `planned` | Plans call for a stronger Linux-shaped ownership model than the current shallow state. |
| signal | pending-signal preservation across exec | M3 | `planned` | Explicitly called out as a Linux-visible rule that must be corrected. |
| signal | standard and realtime signal queue semantics | M3 | `partial` | Signal support exists, but plans highlight correctness gaps and proof needs. |
| signal | default action matrix | M3 | `planned` | Must cover terminate, core, ignore, stop, and continue behaviors. |
| signal | sigaction, sigprocmask, sigpending, sigsuspend, raise, kill, killpg | M3 | `partial` | Surface exists, but milestone proof remains shell-driven. |
| PTY | PTY master and slave runtime | M3, `zsh` | `partial` | Tests exist, but primary proof target is still pending. |
| PTY | controlling tty model | M3, `zsh` | `partial` | Required for job control and shell interactivity. |
| job control | foreground and background process-group enforcement | M3, `zsh` | `partial` | Existing tests indicate progress, but milestone completion is still ahead. |
| job control | `SIGTTIN`, `SIGTTOU`, `SIGTSTP`, `SIGCONT`, `SIGCHLD` shell-visible behavior | M3, `zsh` | `partial` | Must be validated through interactive shell scenarios, not only unit tests. |

## IXLandKernel: VFS, Procfs, Devfs, And Runtime Environment

| subgroup | artifact | required by | status | observation |
| --- | --- | --- | --- | --- |
| VFS | Linux-shaped virtual root | M4, shells, packages | `partial` | VFS implementation already exists, but environment completion is still a roadmap milestone. |
| VFS | explicit backend object model | M4 | `planned` | Plans call for stronger backend typing and less implicit host-path routing. |
| path semantics | `openat` and `*at` path resolution fidelity | M4, packages | `partial` | Strong syscall coverage exists, but full package-proof environment is still pending. |
| runtime env | `/etc` | M4, shell startup | `partial` | Required for shell rc files and package runtime behavior. |
| runtime env | `/usr` | M4, packages | `partial` | Required as part of Linux-shaped filesystem environment. |
| runtime env | `/var` | M4, packages | `partial` | Required for package runtime and caching behavior. |
| runtime env | `/tmp` and `/run` | M4, shells, packages | `partial` | Existing routing exists; milestone proof still required. |
| procfs | `/proc/self/status` | M4, proc-aware tools | `partial` | Existing procfs support exists, but proof target is package consumers. |
| procfs | `/proc/self/mountinfo` | M4, proc-aware tools | `partial` | Tests exist; completion still belongs to runtime-environment milestone. |
| devfs | synthetic `/dev` essentials | M4, PTY, shell tools | `partial` | Existing devfs support exists, but package-facing completeness is still pending. |
| mount | mount namespace visibility | M4, tooling | `partial` | Existing work is present, but milestone proof is still required. |
| host path hygiene | no raw host paths as Linux truth | M4 | `planned` | Explicit guardrail in the plans; adapter-backed storage must stay private. |

## IXLandKernel: Readiness, FD, Memory, And Futex Support

| subgroup | artifact | required by | status | observation |
| --- | --- | --- | --- | --- |
| fd | open file description semantics | `zsh`, `curl`, packages | `partial` | Strong syscall coverage exists in the gap matrix; package-level proof still required. |
| fd | `dup`, `dup2`, `dup3`, `fcntl` correctness | shells, packages | `partial` | Existing implementation exists, but completion is tied to package workflows. |
| readiness | `poll`, `select`, `epoll` semantics | `curl`, shells | `partial` | Gap matrix shows broad readiness coverage, but package proof is still pending. |
| pipes | pipeline and redirection behavior | `zsh` | `partial` | Existing tests exist; shell-based proof still required. |
| VM | native runtime memory management for packages | Version 1 runtime | `partial` | VM syscalls exist in current repo, but are not yet part of an explicit milestone doc. |
| futex | Linux-shaped futex wait and wake behavior | threading, future package breadth | `partial` | Gap matrix shows futex syscall support, but plans still call out guest-memory and semantic hardening gaps. |
| futex | guest-memory-safe robust-list and clear-child-tid handling | correctness, future backends | `planned` | Existing code path still needs hardening away from direct host pointer assumptions. |

## IXLandKernel: Networking And Client Support

| subgroup | artifact | required by | status | observation |
| --- | --- | --- | --- | --- |
| sockets | socket surface sufficient for `curl` | M5, `curl` | `partial` | Syscall gap matrix shows broad virtual-network implementation, but `curl` is the real proof target. |
| DNS | DNS resolution path | M5, `curl` | `planned` | Explicitly called out in networking milestone and not yet claimed complete. |
| timeout semantics | timeout and error behavior | M5, `curl` | `planned` | Must be proven against real client workflows. |
| readiness | network readiness interaction with poll and select | M5, `curl` | `partial` | Existing readiness and socket coverage exists; milestone proof still pending. |
| protocol scope | HTTP and HTTPS proof path | M5, `curl` | `planned` | Runtime proof is still future work. |
| compatibility | IPv4 and IPv6 compatibility | M5, `curl` | `planned` | Explicitly listed in plan and not yet claimed complete. |

## IXLandHostAdapter

| subgroup | artifact | required by | status | observation |
| --- | --- | --- | --- | --- |
| packaging | `IXLandHostAdapter` target or package | M0 | `implemented` | Owns the physical `IXLandHostAdapter/internal/ios/**` tree. |
| seam | curated exported seam namespace | M0 | `planned` | Must replace casual direct imports of host headers from kernel code. |
| fs host mechanics | backing storage and path discovery | M0, M4 | `partial` | Current host fs helpers exist but need packaging and seam discipline. |
| fs host mechanics | errno translation | M0, M4, M5 | `partial` | Present in current tree; still needs stronger boundary enforcement. |
| fs host mechanics | open flags and host file I/O realization | M0, M4 | `partial` | Existing host helpers exist; must remain mechanism-only. |
| kernel host mechanics | host clock access | M0, M3, M5 | `partial` | Existing host clock code lives in `IXLandHostAdapter/internal/ios/kernel`. |
| kernel host mechanics | host sleep and wake primitives | M0, M3 | `partial` | Existing sync and wait realization exists, but seam redesign is still ahead. |
| kernel host mechanics | host signal mask save and restore where required as mechanism | M0, M3 | `partial` | Existing bridge code exists, but semantics must remain kernel-owned. |
| runtime host mechanics | runtime synchronization helper ownership | M0 | `implemented` | `IXLandKernel/runtime/native/registry.c` no longer imports host runtime sync directly. |
| future host mechanics | security-scoped file access lifecycle | future external mounts | `planned` | Explicitly planned but not yet completed. |

## IXLandMLibC

| subgroup | artifact | required by | status | observation |
| --- | --- | --- | --- | --- |
| ownership | package-facing Linux libc headers | M1, all packages | `planned` | Top-level roadmap assigns ownership clearly, but this matrix tracks project requirement, not proof of delivery yet. |
| ownership | libc-owned typedefs and APIs | M1, all packages | `planned` | Must remain outside `IXLandKernel`. |
| sysroot | sysroot ownership discipline | M1, configure and build | `planned` | Core requirement for zero package source changes. |
| sysdeps | Linux-oriented sysdeps for native iOS builds | M1, Version 1 runtime | `planned` | Required for package transparency and native execution path. |
| integration | no kernel reinvention of libc-owned surfaces | M1 | `planned` | Shared ownership boundary must be enforced through both kernel and sysroot cleanup. |

## IXLandSystem Integration And Proof Harness

| subgroup | artifact | required by | status | observation |
| --- | --- | --- | --- | --- |
| integration | `IXLandSystem` integration target | all milestones | `partial` | Existing project target exists, but future composition with `IXLandKernel` and `IXLandHostAdapter` must be updated. |
| proof split | LinuxKernel tests as Linux behavior proof | M0 onward | `partial` | Test target exists today and must remain host-implementation-free. |
| proof split | HostBridge tests as host-adapter proof | M0 onward | `partial` | Test target exists today and already matches the intended split conceptually. |
| proof policy | package-driven proof over unit-test-only proof | M6 | `planned` | Explicitly defined in the package proof milestone. |
| proof artifacts | compile-smoke and contract files | M1 onward | `partial` | Current repo already uses UAPI compile smoke and contract tests. |

## Package Proof Targets

| group | artifact | required by | status | observation |
| --- | --- | --- | --- | --- |
| primary gate | `zsh` configure unchanged | M6 | `planned` | Primary product proof target for shell compatibility. |
| primary gate | `zsh` build unchanged | M6 | `planned` | Must compile as native iOS code without package source changes. |
| primary gate | `zsh` install unchanged | M6 | `planned` | Must install into Linux-shaped virtual filesystem. |
| primary gate | `zsh` shell runtime ladder | M6 | `planned` | Includes pipelines, redirections, background jobs, PTY behavior, and shebang execution. |
| secondary gate | `curl` configure unchanged | M6 | `planned` | Secondary proof target for network and client behavior. |
| secondary gate | `curl` build unchanged | M6 | `planned` | Must compile natively without package source changes. |
| secondary gate | `curl` install unchanged | M6 | `planned` | Required before client runtime proof. |
| secondary gate | `curl` client runtime ladder | M6 | `planned` | Includes HTTP, HTTPS, DNS, timeout, and redirected output behavior. |
| follow-on set | `busybox`, `grep`, `sed`, `gawk`, `make` | future M6 expansion | `planned` | Defined as proof set after primary and secondary gates. |

## Deferred Future Execution Backends

| subgroup | artifact | required by | status | observation |
| --- | --- | --- | --- | --- |
| execution images | native image backend | Version 1 | `planned` | Required now through Milestone 2. |
| execution images | script image backend | Version 1 | `planned` | Mandatory now because shebang execution is a must-have. |
| execution images | ELF image backend | future version | `deferred-v2` | Must be planned for now but not implemented as a Version 1 dependency. |
| execution images | WASM image backend | future version | `deferred-v2` | Same rule as ELF: architecture-ready, implementation deferred. |
| execution plumbing | backend-neutral execution-image abstraction | Version 1 and future versions | `planned` | Needed now so native execution does not hard-code future backend limitations. |

## Current Completion Summary

| area | summary |
| --- | --- |
| strongest current reality | syscall surface has broad existing implementation coverage across fd, process, time, mount, VM, readiness, and virtual networking, but package-driven proof is still missing |
| most important prerequisite | `IXLandHostAdapter` split and boundary lockdown |
| primary Version 1 proof target | `zsh` |
| secondary Version 1 proof target | `curl` |
| biggest architectural risk | Darwin and host-mechanics leakage into `IXLandKernel` ownership zones |
| biggest proof risk | passing narrow tests while real packages still fail unchanged configure, build, install, or runtime |
