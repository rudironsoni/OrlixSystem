# AGENTS.md - Linux-Shaped Architecture Rules for IXLandKernel

## Project Invariant

IXLandKernel is a Linux-shaped headers + syscall + runtime target hosted on iOS.

If a change makes IXLandKernel less suitable for real Linux userspace (for example bash, zsh, grep, sed, awk, fzf), the change is wrong.

Repo-local convenience never outranks Linux userspace compatibility.

## 1) Linux-Shaped Surface First

Linux-owner behavior must prefer Linux expectations for:
- headers, constants, and public names
- struct layouts and ioctl payload contracts
- errno behavior
- file descriptor and open-file-description semantics
- path resolution, dirfd, and pathname rules
- poll/select readiness behavior
- signals, default actions, masking, and delivery
- sessions/process-groups/controlling-tty behavior
- termios/tty behavior
- procfs/devfs names and shape
- exec/shebang/interpreter/argv/env/wait/exit behavior

Do not drift toward Darwin-shaped semantics in Linux-owner code.

## 2) Ownership and Directionality

Linux-owner paths (Linux semantics live here):
- `IXLandKernel/fs/`
- `IXLandKernel/kernel/`
- `IXLandKernel/runtime/`
- `IXLandKernel/include/`

Host mediation paths (host mechanics live here only):
- `IXLandHostAdapter/**`

Wrong-direction changes are forbidden:
- Do not move Linux semantic decisions into `IXLandHostAdapter/**`.
- Do not move host mechanics into Linux-owner paths.
- Do not excuse Darwin/iOS leakage in Linux-owner code as an environmental fact.
- If Darwin/iOS types, headers, macros, constants, process APIs, fd APIs, wait APIs, signal APIs, or filesystem semantics appear in Linux-owner code, treat that as an agent implementation error.
- Fix such leakage by restoring the architecture boundary: Linux header truth (UAPI + kernel-internal generated headers) in Linux-owner code, private host mediation only under `IXLandHostAdapter/**`.
- Never describe Linux-owner files as “not needing Darwin” as if Darwin was a valid option there. Darwin is not a Linux-owner dependency.

## 3) Narrow Subsystem Seams Only

When Linux-owner code needs host mediation, use narrow, subsystem-owned, private contracts declared by `IXLandKernel` and implemented under `IXLandHostAdapter/**`.

Allowed seam shape:
- specific to one subsystem
- minimal exported surface
- no ambient host vocabulary leakage
- kernel-owned declarations, host-owned implementations only

Forbidden seam shape:
- generic helper bags
- catch-all mediation headers used by unrelated subsystems
- abstractions that rename/deodorize host APIs and make them ambient
- adapter-owned include surfaces consumed by `IXLandKernel`
- filenames or contract names that encode host role redundantly with `_host`, `_bridge`, `internal/ios`, or similar labels

## 4) Ambient Host Vocabulary Is Forbidden in Linux-Owner Code

In `IXLandKernel/fs/`, `IXLandKernel/kernel/`, `IXLandKernel/runtime/`, `IXLandKernel/include/`, do not introduce:
- direct host APIs/types/macros
- renamed host APIs wrapped as generic helpers
- generic wrapper families for mutex/thread/cond/signal/io/platform bridging
- broad mediation headers that encode host assumptions globally
- any include from `IXLandHostAdapter/**`
- ambient host vocabulary such as `host_*`, `*_host`, or `*_bridge`

Category rule: banning one prefix and reintroducing the same leakage with a new prefix is still a violation.

## 5) Public ABI Discipline

- Public syscall names remain Linux-shaped.
- No branded/public ABI names that encode platform identity.
- Darwin/BSD header behavior must not define Linux-facing contracts.
- Keep internal implementation behind private `*_impl()` helpers and preserve clean public wrapper boundaries.
- Cross-target contract headers are kernel-private only. `IXLandHostAdapter` does not own or export the seam.

## 6) Test Target Ownership

`IXLandKernelTests` proves Linux-facing IXLandKernel behavior.

Rules for LinuxKernel tests:
- Exercise syscall-facing IXLandKernel functions and Linux-visible runtime behavior.
- Use C contract files for Linux UAPI constants, macros, structs, and ioctl payloads.
- Objective-C test files must not include Linux UAPI headers.
- Do not include `IXLandHostAdapter/**`.
- Do not depend on HostBridge helpers or Darwin host behavior as Linux proof.
- Do not introduce branded helper vocabularies such as `ixland_test_*`, `IX_*`, `TEST_*`, or Linux constant accessor wrappers.

`IXLandHostAdapterTests` proves private iOS host mediation seams only.

Rules for HostBridge tests:
- May include `IXLandHostAdapter/**` and may use Darwin/Foundation/POSIX host APIs when testing host mechanics.
- Must verify bridge contracts such as host path discovery, host errno translation, backing storage setup, security-scoped access, and host fd mediation.
- Must not be cited as proof that Linux semantics are correct.
- Must not define Linux-facing ABI, Linux UAPI aliases, or Linux-looking compatibility helpers.
- HostBridge helpers should be host-test fixtures with plain, non-branded names, or direct host calls where clearer.

The test target split is intentional:

```text
IXLandKernelTests -> Linux surface proof
IXLandHostAdapterTests  -> IXLandHostAdapter seam proof
```

HostBridge failures can block a full repo-green milestone, but they do not replace LinuxKernel proof.

## 7) mlibc Reference Boundary

mlibc is a design reference for Linux source compatibility, not code to paste into IXLandKernel.

Use these mlibc surfaces deliberately:
- `mlibc/abis/linux` is a Linux libc ABI surface checklist.
- `mlibc/sysdeps/linux` is a libc syscall backend design reference.
- `mlibc/options` is a libc feature/API organization reference.

IXLandKernel ownership:
- virtual kernel behavior that libc sysdeps call into
- syscall/runtime ABI entry points
- tasks, signals, wait, fdtable, VFS, mounts, pipes, PTY, poll/select/epoll, procfs/devfs, credentials, namespaces, cgroups, seccomp, ptrace, netlink
- vendored Linux UAPI consumption
  - Vendored generated Linux headers are the only source of truth:
    - tuple root: `third_party/linux/<version>/<arch>/`
    - UAPI: `third_party/linux/<version>/<arch>/uapi/include`
    - kheaders/source: `third_party/linux/<version>/<arch>/kheaders/source/**`
    - kheaders/generated: `third_party/linux/<version>/<arch>/kheaders/generated/**`

IXLandMLibC ownership:
- libc ABI headers and typedef surfaces
- libc `options` APIs
- native iOS arm64/aarch64 sysdeps for recompiled Linux-oriented packages
- errno exposure at the libc boundary
- `_start`, libc startup, libc syscall stubs/wrappers, package-facing sysroot headers

Hard direction rule:
- `IXLandKernel/**` must not include `IXLandMLibC/**`.
- The only allowed consumers of `IXLandMLibC/**` inside this repo are package-facing compile probes, sysroot/bootstrap plumbing, and non-kernel targets that explicitly model native userspace.
- If Linux-owner code needs a type or struct that is not appropriate to take from Darwin and not appropriate to take from `IXLandMLibC`, define or use a kernel-private/Linux-owned surface instead of crossing into libc ownership.
- Do not "fix" Darwin leakage in `IXLandKernel` by replacing it with `IXLandMLibC` includes. That is still a wrong-direction dependency.

Correct native userspace path:

```text
native iOS arm64/aarch64 package code
        ↓
IXLandMLibC Linux ABI headers + IXLand sysdeps
        ↓
IXLandKernel syscall/runtime ABI
        ↓
IXLandKernel virtual kernel subsystems
        ↓
IXLandHostAdapter host mediation
```

Do not vendor mlibc `abis`, `sysdeps`, or `options` into IXLandKernel.
Do not implement libc sysdeps inside IXLandKernel.
Do not create kernel-owned replacements for libc typedef headers such as `pid_t`, `uid_t`, `gid_t`, `mode_t`, `dev_t`, `ino_t`, `sigevent`, `sigval`, `socklen_t`, `statvfs`, or `suseconds_t`.

The Linux header vendoring pipeline must treat mlibc `abis/linux` as a coverage reference:
- map headers already present in vendored generated Linux headers to the correct surface (UAPI vs kernel-internal)
- classify libc-owned surfaces as IXLandMLibC-owned
- fail on unclassified surfaces instead of silently inventing kernel headers

## 8) Proof Discipline (Required)

Lint green is necessary but insufficient.
Build green is necessary but insufficient.

Lint model:
- Source lint is `clang-tidy` only.
- `make lint` is the only lint entrypoint.
- Do not reintroduce shell/Python lint wrappers, repo-layout lint targets, or sidecar text-policy checks.
- Do not reintroduce `grep`, `rg`, `sed`, Python regex scans, or shell text-policy checks as a parallel lint system outside the `Makefile` + `clang-tidy` path.
- Do not revive `scripts/lint_linux_vendor_headers.sh` or any shell wrapper that pretends to be source lint.

Authoritative proof target is iOS Simulator:
1. `make lint`
3. `xcodegen generate --project .`
4. `xcodebuild build-for-testing -project IXLandKernel.xcodeproj -scheme IXLandKernel-6.12-arm64 -sdk iphonesimulator -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17'`
5. required targeted tests for the current tranche

Catalyst may be secondary smoke only.
No commit/push before required proof is green.

## 9) Tranche Discipline

Changes must be bounded by subsystem tranche with explicit ownership and proof.

Do not mix unrelated architecture migrations into one tranche.
Do not “fix lint” by weakening checks or broadening allowlists.

## 10) Quality Bar: No Shortcuts

IXLandKernel is a kernel/runtime substrate, not a test-passing exercise.

Forbidden implementation behavior:
- shallow compatibility stubs presented as completed kernel capability
- compile-only hacks that weaken Linux source compatibility
- local typedefs, aliases, wrappers, or renames that dodge ABI ownership
- replacing Linux UAPI/libc-owned concepts with repo-local convenience types
- using Darwin/POSIX host types because they are easier to compile
- moving semantic problems into tests instead of fixing subsystem ownership
- stopping at the smallest green test when the architecture is still wrong
- narrowing behavior to one current test case instead of modeling the Linux-facing rule

Required implementation behavior:
- use vendored generated Linux headers for kernel/userspace contract truth
- keep libc-owned typedef/API surfaces out of IXLandKernel and in IXLandMLibC
- model the real virtual-kernel behavior in the owning subsystem
- keep host mediation inside `IXLandHostAdapter/**` only
- declare any cross-target contract from `IXLandKernel`, not from `IXLandHostAdapter`
- keep `IXLandKernel` independent from `IXLandMLibC` includes and package-facing header ownership
- prefer deleting a bad shortcut over preserving compatibility with it
- verify the subsystem through syscall-facing LinuxKernel tests, not internal struct peeking
- raise the implementation to the product contract before claiming completion

If a quick fix conflicts with these rules, the quick fix is wrong.

Additional hard rule:
- Do not rationalize Darwin/iOS leakage in Linux-owner files by saying a file
  “does not need Darwin.” Linux-owner files are forbidden from depending on
  Darwin/iOS in the first place. If such a dependency appears, fix the boundary
  instead of explaining it away.
- Do not downgrade Linux ABI types to local fixed-width convenience types when
  vendored generated Linux headers provide the contract type.
- Laziness is not an implementation strategy: do not choose shallow stubs,
  renamed adapters, local typedefs, or narrow test-shaped behavior when the
  kernel capability requires real subsystem semantics.

## 11) No Policy Theater

Forbidden:
- incident-specific blacklist hacks (single test/helper name grudges)
- fake completion claims without repo truth and proof logs
- cosmetic renames that preserve the same architectural violation
- adapter-owned seam headers consumed by `IXLandKernel`
- redundant path or filename role labels such as `internal/ios`, `*_host`, and `*_bridge` in the accepted end-state

Required response to lint conflicts:
- refine seam boundaries
- relocate host mechanics behind `IXLandHostAdapter/**`
- move cross-target declarations under kernel-owned private contract headers
- preserve Linux-owner semantics and contracts

@RTK.md
