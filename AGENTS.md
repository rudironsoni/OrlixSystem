# AGENTS.md - Linux-Shaped Architecture Rules for IXLandSystem

## Project Invariant

IXLandSystem is a Linux-shaped headers + syscall + runtime target hosted on iOS.

If a change makes IXLandSystem less suitable for real Linux userspace (for example bash, zsh, grep, sed, awk, fzf), the change is wrong.

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
- `fs/`
- `kernel/`
- `runtime/`
- `include/`

Host mediation paths (host mechanics live here only):
- `internal/ios/**`

Wrong-direction changes are forbidden:
- Do not move Linux semantic decisions into `internal/ios/**`.
- Do not move host mechanics into Linux-owner paths.
- Do not excuse Darwin/iOS leakage in Linux-owner code as an environmental fact.
- If Darwin/iOS types, headers, macros, constants, process APIs, fd APIs, wait APIs, signal APIs, or filesystem semantics appear in Linux-owner code, treat that as an agent implementation error.
- Fix such leakage by restoring the architecture boundary: Linux header truth (UAPI + kernel-internal generated headers) in Linux-owner code, private host mediation only under `internal/ios/**`.
- Never describe Linux-owner files as “not needing Darwin” as if Darwin was a valid option there. Darwin is not a Linux-owner dependency.

## 3) Narrow Subsystem Seams Only

When Linux-owner code needs host mediation, use narrow, subsystem-owned, private seams under `internal/ios/**`.

Allowed seam shape:
- specific to one subsystem
- minimal exported surface
- no ambient host vocabulary leakage

Forbidden seam shape:
- generic helper bags
- catch-all mediation headers used by unrelated subsystems
- abstractions that rename/deodorize host APIs and make them ambient

## 4) Ambient Host Vocabulary Is Forbidden in Linux-Owner Code

In `fs/`, `kernel/`, `runtime/`, `include/`, do not introduce:
- direct host APIs/types/macros
- renamed host APIs wrapped as generic helpers
- generic wrapper families for mutex/thread/cond/signal/io/platform bridging
- broad mediation headers that encode host assumptions globally

Category rule: banning one prefix and reintroducing the same leakage with a new prefix is still a violation.

## 5) Public ABI Discipline

- Public syscall names remain Linux-shaped.
- No branded/public ABI names that encode platform identity.
- Darwin/BSD header behavior must not define Linux-facing contracts.
- Keep internal implementation behind private `*_impl()` helpers and preserve clean public wrapper boundaries.

## 6) Test Target Ownership

`IXLandSystemLinuxKernelTests` proves Linux-facing IXLandSystem behavior.

Rules for LinuxKernel tests:
- Exercise syscall-facing IXLandSystem functions and Linux-visible runtime behavior.
- Use C contract files for Linux UAPI constants, macros, structs, and ioctl payloads.
- Objective-C test files must not include Linux UAPI headers.
- Do not include `internal/ios/**`.
- Do not depend on HostBridge helpers or Darwin host behavior as Linux proof.
- Do not introduce branded helper vocabularies such as `ixland_test_*`, `IX_*`, `TEST_*`, or Linux constant accessor wrappers.

`IXLandSystemHostBridgeTests` proves private iOS host mediation seams only.

Rules for HostBridge tests:
- May include `internal/ios/**` and may use Darwin/Foundation/POSIX host APIs when testing host mechanics.
- Must verify bridge contracts such as host path discovery, host errno translation, backing storage setup, security-scoped access, and host fd mediation.
- Must not be cited as proof that Linux semantics are correct.
- Must not define Linux-facing ABI, Linux UAPI aliases, or Linux-looking compatibility helpers.
- HostBridge helpers should be host-test fixtures with plain, non-branded names, or direct host calls where clearer.

The test target split is intentional:

```text
IXLandSystemLinuxKernelTests -> Linux surface proof
IXLandSystemHostBridgeTests  -> internal/ios seam proof
```

HostBridge failures can block a full repo-green milestone, but they do not replace LinuxKernel proof.

## 7) mlibc Reference Boundary

mlibc is a design reference for Linux source compatibility, not code to paste into IXLandSystem.

Use these mlibc surfaces deliberately:
- `mlibc/abis/linux` is a Linux libc ABI surface checklist.
- `mlibc/sysdeps/linux` is a libc syscall backend design reference.
- `mlibc/options` is a libc feature/API organization reference.

IXLandSystem ownership:
- virtual kernel behavior that libc sysdeps call into
- syscall/runtime ABI entry points
- tasks, signals, wait, fdtable, VFS, mounts, pipes, PTY, poll/select/epoll, procfs/devfs, credentials, namespaces, cgroups, seccomp, ptrace, netlink
- vendored Linux UAPI consumption
  - Vendored generated Linux headers are the only source of truth:
    - tuple root: `third_party/linux/<version>/<arch>/`
    - UAPI: `third_party/linux/<version>/<arch>/uapi/include`
    - srctree: `third_party/linux/<version>/<arch>/srctree/**`
    - objtree: `third_party/linux/<version>/<arch>/objtree/**`

IXLandMLibC ownership:
- libc ABI headers and typedef surfaces
- libc `options` APIs
- native iOS arm64/aarch64 sysdeps for recompiled Linux-oriented packages
- errno exposure at the libc boundary
- `_start`, libc startup, libc syscall stubs/wrappers, package-facing sysroot headers

Correct native userspace path:

```text
native iOS arm64/aarch64 package code
        ↓
IXLandMLibC Linux ABI headers + IXLand sysdeps
        ↓
IXLandSystem syscall/runtime ABI
        ↓
IXLandSystem virtual kernel subsystems
        ↓
internal/ios host mediation
```

Do not vendor mlibc `abis`, `sysdeps`, or `options` into IXLandSystem.
Do not implement libc sysdeps inside IXLandSystem.
Do not create kernel-owned replacements for libc typedef headers such as `pid_t`, `uid_t`, `gid_t`, `mode_t`, `dev_t`, `ino_t`, `sigevent`, `sigval`, `socklen_t`, `statvfs`, or `suseconds_t`.

The Linux header vendoring pipeline must treat mlibc `abis/linux` as a coverage reference:
- map headers already present in vendored generated Linux headers to the correct surface (UAPI vs kernel-internal)
- classify libc-owned surfaces as IXLandMLibC-owned
- fail on unclassified surfaces instead of silently inventing kernel headers

## 8) Proof Discipline (Required)

Lint green is necessary but insufficient.
Build green is necessary but insufficient.

Authoritative proof target is iOS Simulator:
1. `bash ./scripts/lint_linux_surface.sh`
2. `xcodegen generate --project .`
3. `xcodebuild build-for-testing -project IXLandSystem.xcodeproj -scheme IXLandSystem-6.12-arm64 -sdk iphonesimulator -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17'`
4. required targeted tests for the current tranche

Catalyst may be secondary smoke only.
No commit/push before required proof is green.

## 9) Tranche Discipline

Changes must be bounded by subsystem tranche with explicit ownership and proof.

Do not mix unrelated architecture migrations into one tranche.
Do not “fix lint” by weakening checks or broadening allowlists.

## 10) Quality Bar: No Shortcuts

IXLandSystem is a kernel/runtime substrate, not a test-passing exercise.

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
- keep libc-owned typedef/API surfaces out of IXLandSystem and in IXLandMLibC
- model the real virtual-kernel behavior in the owning subsystem
- make host mediation explicit under `internal/ios/**` only
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

Required response to lint conflicts:
- refine seam boundaries
- relocate host mechanics behind `internal/ios/**`
- preserve Linux-owner semantics and contracts

@RTK.md
