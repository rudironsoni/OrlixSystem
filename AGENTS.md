# AGENTS.md - Linux-Shaped Architecture Rules for OrlixKernel

## Project Invariant

OrlixKernel is a Linux-shaped headers + syscall + runtime target hosted on iOS.

If a change makes OrlixKernel less suitable for real Linux userspace (for example bash, zsh, grep, sed, awk, fzf), the change is wrong.

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
- `OrlixKernel/fs/`
- `OrlixKernel/kernel/`
- `OrlixKernel/runtime/`
- `OrlixKernel/include/`

Host mediation paths (host mechanics live here only):
- `OrlixHostAdapter/**`

OrlixHostAdapter ownership rule:
- `OrlixHostAdapter/**` serves `OrlixKernel` only.
- `OrlixHostAdapter/**` must not become a public libc surface, syscall interposition layer, or package-facing ABI provider.
- `OrlixMLibC` must rely on `OrlixKernel` only. It must not rely on `OrlixHostAdapter` directly.
- If a Linux-facing public function such as `open`, `close`, `read`, `write`, `ioctl`, `sigaction`, `waitpid`, `clock_gettime`, `futex`, `getuid`, `epoll_wait`, or similar must exist for userspace, that ownership belongs to `OrlixMLibC`, not to `OrlixHostAdapter`.
- `OrlixHostAdapter/**` may implement only private host seam entry points for `OrlixKernel`, for example narrow subsystem-owned `*_impl()` functions or equivalent kernel-owned private contracts.
- Public/default-visible symbols in `OrlixHostAdapter/**` are forbidden unless the thing being exported is itself a kernel-private seam declared by `OrlixKernel` rather than a libc/public API surface.
- Bootstrap convenience is not an excuse to keep public libc-shaped wrappers in `OrlixHostAdapter/**`. Delete the wrong layer instead of preserving it.

Wrong-direction changes are forbidden:
- Do not move Linux semantic decisions into `OrlixHostAdapter/**`.
- Do not move host mechanics into Linux-owner paths.
- Do not excuse Darwin/iOS leakage in Linux-owner code as an environmental fact.
- If Darwin/iOS types, headers, macros, constants, process APIs, fd APIs, wait APIs, signal APIs, or filesystem semantics appear in Linux-owner code, treat that as an agent implementation error.
- Fix such leakage by restoring the architecture boundary: Linux header truth (UAPI + kernel-internal generated headers) in Linux-owner code, private host mediation only under `OrlixHostAdapter/**`.
- Never describe Linux-owner files as “not needing Darwin” as if Darwin was a valid option there. Darwin is not a Linux-owner dependency.
- Do not include host toolchain `std*.h` headers in Linux-owner code. That includes, at minimum, `stddef.h`, `stdint.h`, `stdbool.h`, `stdatomic.h`, `stdarg.h`, `stdlib.h`, `stdio.h`, and any similar standard-library convenience headers.
- If a Linux-owner file appears to need a `std*.h` header, that is a boundary bug to fix, not a convenience exception to take. Use the vendored truth from `third_party/linux/6.12/arm64/uapi/include` and `third_party/linux/6.12/arm64/kheaders/**`, or redesign the kernel-private type ownership.

## 3) Narrow Subsystem Seams Only

When Linux-owner code needs host mediation, use narrow, subsystem-owned, private contracts declared by `OrlixKernel` and implemented under `OrlixHostAdapter/**`.

Allowed seam shape:
- specific to one subsystem
- minimal exported surface
- no ambient host vocabulary leakage
- kernel-owned declarations, host-owned implementations only

Forbidden seam shape:
- generic helper bags
- catch-all mediation headers used by unrelated subsystems
- abstractions that rename/deodorize host APIs and make them ambient
- adapter-owned include surfaces consumed by `OrlixKernel`
- filenames or contract names that encode host role redundantly with `_host`, `_bridge`, `internal/ios`, or similar labels
- repo-local compatibility layers that rename Linux-shaped concepts instead of using Linux names
- adapter-owned public wrappers for libc or userspace ABI entry points

Linux naming rule:
- If the concept already has a Linux name, use the Linux name.
- If the concept already exists in `third_party/linux/<version>/<arch>/uapi/include` or `third_party/linux/<version>/<arch>/kheaders/**`, do not recreate it in `OrlixKernel/**` under any repo-local spelling.
- If the concept is libc-owned and already belongs in `OrlixMLibC`, do not recreate it in `OrlixKernel/**` under any repo-local spelling.
- Do not invent repo-local spellings for Linux-shaped concepts such as `orlix_*`, `kernel_*`, or `*_compat`.
- Do not invent repo-local `linux_*` stand-ins for common Linux concepts when the real contract name is just `timespec`, `timeval`, `itimerval`, `fd_set`, `sockaddr`, `msghdr`, `termios`, `winsize`, `statfs`, or similar.
- This applies to filenames, header names, structs, typedefs, enums, macros, helper functions, and local private state when the thing being modeled is still a Linux concept.
- Examples of forbidden spellings include patterns such as `kernel_timeval`, `orlix_kernel_fd_zero`, `kernel_select_compat.h`, or similar repo-local renames of `timespec`, `timeval`, `fd_set`, `sockaddr`, `pollfd`, `termios`, `winsize`, and related Linux-shaped concepts.
- Additional forbidden examples include `linux_timespec`, `linux_statfs`, `linux_mmsghdr`, and other repo-local spellings that merely prepend `linux_` to a concept whose real Linux-facing name is already defined elsewhere.
- If the concept is genuinely private state and not a Linux concept, use a plain subsystem noun instead of branding or fake compatibility vocabulary.
- Extrapolation rule: if a repo-local name would still read as “Linux concept plus project/role/private/compat dressing,” it is forbidden even if the exact spelling is new.
- Forbidden dressing includes project branding, ownership branding, and fake adaptation words before or after a Linux concept:
  - prefixes such as `orlix_`, `ix_`, `kernel_`, `linux_`, `private_`, `internal_`, `bridge_`, `adapter_`, `shim_`, `compat_`
  - suffixes such as `_compat`, `_bridge`, `_adapter`, `_shim`, `_private`, `_internal`, `_owner`
- This prohibition applies equally to helper functions, typedefs, structs, macros, local aliases, and header filenames.
- If you are tempted to write a name like `kernel_*`, `linux_*`, `orlix_*`, `*_compat`, `*_bridge`, or similar around a Linux concept, stop and redesign the ownership boundary instead.
- Mandatory stop rule: if the only apparent way forward is to mint a repo-local Linux-concept rename, stop immediately and ask for guidance instead of inventing the name.

## 4) Ambient Host Vocabulary Is Forbidden in Linux-Owner Code

In `OrlixKernel/fs/`, `OrlixKernel/kernel/`, `OrlixKernel/runtime/`, `OrlixKernel/include/`, do not introduce:
- direct host APIs/types/macros
- renamed host APIs wrapped as generic helpers
- generic wrapper families for mutex/thread/cond/signal/io/platform bridging
- broad mediation headers that encode host assumptions globally
- any include from `OrlixHostAdapter/**`
- ambient host vocabulary such as `host_*`, `*_host`, or `*_bridge`

Category rule: banning one prefix and reintroducing the same leakage with a new prefix is still a violation.

## 5) Public ABI Discipline

- Public syscall names remain Linux-shaped.
- No branded/public ABI names that encode platform identity.
- Darwin/BSD header behavior must not define Linux-facing contracts.
- Keep internal implementation behind private `*_impl()` helpers and preserve clean public wrapper boundaries.
- Cross-target contract headers are kernel-private only. `OrlixHostAdapter` does not own or export the seam.
- `OrlixHostAdapter/**` must not export libc entry points or Linux-facing public wrappers with default visibility.
- `OrlixHostAdapter/**` must not define public wrappers for APIs such as file I/O, process credentials, signals, wait, futex, epoll, poll, time, tty, mount, or other userspace-facing libc/syscall surfaces.
- If a wrapper name would be valid to expose from libc or to include from package-facing headers, it does not belong in `OrlixHostAdapter/**`.

## 6) Test Target Ownership

`OrlixKernelTests` proves Linux-facing OrlixKernel behavior.

Rules for LinuxKernel tests:
- Exercise syscall-facing OrlixKernel functions and Linux-visible runtime behavior.
- Use C contract files for Linux UAPI constants, macros, structs, and ioctl payloads.
- Objective-C test files must not include Linux UAPI headers.
- Do not include `OrlixHostAdapter/**`.
- Do not depend on HostBridge helpers or Darwin host behavior as Linux proof.
- Do not introduce branded helper vocabularies such as `orlix_test_*`, `IX_*`, `TEST_*`, or Linux constant accessor wrappers.

KUnit rules for `OrlixKernelTests`:
- Treat KUnit as the required model for Linux-owner unit tests. New LinuxKernel unit suites must be written as C KUnit-style suites and cases, not as hand-maintained Objective-C test methods.
- The canonical shape is:
  - `void case_name(struct kunit *test)`
  - `struct kunit_case`
  - `struct kunit_suite`
  - `KUNIT_CASE(...)`
  - `KUNIT_EXPECT_*` and `KUNIT_ASSERT_*`
- XCTest is allowed only as outer discovery/reporting plumbing for Xcode. All real Linux test logic, kernel-private setup, and vendored Linux header usage must live in C KUnit-owned files.
- Objective-C files in `OrlixKernelTests` are harness-only:
  - they may select or expose KUnit suites to XCTest
  - they must not own kernel state reset/setup logic
  - they must not manipulate `task_struct`, `signal`, `fdtable`, `vfs`, PTY state, wait state, or similar kernel-private structures directly
  - they must not include kernel-private owner headers such as `kernel/task.h`, `kernel/signal.h`, `kernel/wait.h`, `kernel/futex.h`, `fs/fdtable.h`, `fs/vfs.h`, `fs/path.h`, `fs/pty.h`
- KUnit suite ownership must follow Linux subsystem ownership, not historical wrapper names:
  - prefer paths such as `OrlixKernelTests/kernel/<subsystem>/...` or `OrlixKernelTests/fs/<subsystem>/...`
  - prefer suite names based on the subsystem under test, with underscores instead of dashes
  - do not put `test`, `unittest`, `compat`, `bridge`, or project branding into suite names unless the thing under test is literally the KUnit machinery itself
- Each KUnit case should test one Linux-facing behavior rule. Keep cases short, deterministic, and hermetic. Prefer many small cases over one aggregate case that hides the failing rule.
- Use `KUNIT_EXPECT_*` when the case can continue gathering evidence after a failure, and `KUNIT_ASSERT_*` when continuing would invalidate the rest of the case.
- Shared setup/teardown belongs to the suite-owned C side, not to Objective-C `setUp` / `tearDown` methods and not to ambient global helpers scattered across unrelated files.
- Linux proof must stay Linux-shaped under KUnit too:
  - use vendored Linux UAPI and vendored kheaders where appropriate
  - do not replace Linux contracts with Darwin or POSIX host headers because they are easier to compile
  - do not hide ABI drift behind test-only compatibility wrappers
- If a LinuxKernel test cannot be expressed cleanly as a KUnit-style C suite and apparently requires direct Objective-C/kernel-private coupling, stop and redesign the test seam instead of bypassing KUnit.

Dynamic XCTest discovery rules for `OrlixKernelTests`:
- Preserve per-case XCTest visibility while keeping Linux test ownership in C.
- The accepted end-state is:
  - one shared Objective-C base runner that bridges XCTest discovery/execution to C KUnit-style suite metadata
  - one thin Objective-C subclass per C suite or closely related Linux subsystem group
  - one XCTest case entry per C case discovered dynamically from suite metadata
- Do not hand-maintain one Objective-C method per Linux test case.
- Objective-C subclasses should do only suite selection, for example “return the suite for `kernel/wait`” or “return the suite for `fs/pty`”.
- Dynamic selector generation must be deterministic and derived from the C case name. Preserve the original C case name for failure reporting even if the Objective-C selector must be sanitized.
- Failure reporting must point back to the C test file and line that raised the KUnit failure. Do not collapse failures into generic Objective-C wrapper locations when the underlying C case can be reported directly.
- XCTest filtering and rerun must continue to work at per-case granularity after the bridge is introduced. If a proposed harness would only expose one aggregate suite-level XCTest case, it is not acceptable.
- Compile-smoke and header-surface tests may remain plain C files without dynamic discovery when they are not modeling a KUnit suite. Do not force discovery plumbing onto simple compile probes unnecessarily.

Current course and migration direction for LinuxKernel tests:
- The repo is moving away from mixed Objective-C kernel-state tests plus ad hoc `*Contract.c` wrappers toward subsystem-owned C suites with a thin XCTest bridge.
- Preferred path layout is Linux-shaped by subsystem, for example:
  - `OrlixKernelTests/kernel/cred/cred_test.c`
  - `OrlixKernelTests/kernel/wait/wait_test.c`
  - `OrlixKernelTests/kernel/futex/futex_test.c`
  - `OrlixKernelTests/fs/pty/pty_job_control_test.c`
  - `OrlixKernelTests/fs/pipe/pipe_test.c`
  - `OrlixKernelTests/fs/poll/poll_test.c`
  - `OrlixKernelTests/fs/epoll/epoll_test.c`
  - `OrlixKernelTests/runtime/syscall/syscall_test.c`
- Historical names like `*Contract.c` are transitional at best. When touching a migrated subsystem, prefer finishing the move to subsystem-owned KUnit-style files rather than adding more logic to the old wrapper shape.
- The first-wave migration model is:
  - process-group/session
  - PTY job control
  - wait/job control
  - futex
  - pipe
- After that, continue the same pattern for signal, task-group, VFS-path/internal VFS, and related kernel-internal suites instead of creating a second competing test architecture.
- Keep suite-local reset/setup helpers in C next to the owning subsystem tests. Do not split reset logic back into Objective-C `setUp`/`tearDown` or broad shared helper bags.
- The policy/lint direction must match the architecture:
  - C files in `OrlixKernelTests` may include vendored Linux headers and kernel-private headers as needed for Linux proof
  - Objective-C files in `OrlixKernelTests` are harness-only and must not include kernel-private owner headers
  - `OrlixKernelTests` must not include `OrlixHostAdapter/**` or host-fixture helpers as Linux proof
- If extending the harness, prefer strengthening the single shared bridge instead of creating parallel custom runners for individual subsystems.

`OrlixHostAdapterTests` proves private iOS host mediation seams only.

Rules for HostBridge tests:
- May include `OrlixHostAdapter/**` and may use Darwin/Foundation/POSIX host APIs when testing host mechanics.
- Must verify bridge contracts such as host path discovery, host errno translation, backing storage setup, security-scoped access, and host fd mediation.
- Must not be cited as proof that Linux semantics are correct.
- Must not define Linux-facing ABI, Linux UAPI aliases, or Linux-looking compatibility helpers.
- HostBridge helpers should be host-test fixtures with plain, non-branded names, or direct host calls where clearer.

The test target split is intentional:

```text
OrlixKernelTests -> Linux surface proof
OrlixHostAdapterTests  -> OrlixHostAdapter seam proof
```

HostBridge failures can block a full repo-green milestone, but they do not replace LinuxKernel proof.

## 7) mlibc Reference Boundary

mlibc is a design reference for Linux source compatibility, not code to paste into OrlixKernel.

Use these mlibc surfaces deliberately:
- `mlibc/abis/linux` is a Linux libc ABI surface checklist.
- `mlibc/sysdeps/linux` is a libc syscall backend design reference.
- `mlibc/options` is a libc feature/API organization reference.

OrlixKernel ownership:
- virtual kernel behavior that libc sysdeps call into
- syscall/runtime ABI entry points
- tasks, signals, wait, fdtable, VFS, mounts, pipes, PTY, poll/select/epoll, procfs/devfs, credentials, namespaces, cgroups, seccomp, ptrace, netlink
- vendored Linux UAPI consumption
  - Vendored generated Linux headers are the only source of truth:
    - tuple root: `third_party/linux/<version>/<arch>/`
    - UAPI: `third_party/linux/<version>/<arch>/uapi/include`
    - kheaders/source: `third_party/linux/<version>/<arch>/kheaders/source/**`
    - kheaders/generated: `third_party/linux/<version>/<arch>/kheaders/generated/**`

OrlixMLibC ownership:
- libc ABI headers and typedef surfaces
- libc `options` APIs
- native iOS arm64/aarch64 sysdeps for recompiled Linux-oriented packages
- errno exposure at the libc boundary
- `_start`, libc startup, libc syscall stubs/wrappers, package-facing sysroot headers
- userspace socket ABI surfaces built from vendored Linux UAPI, including:
  - `sys/socket.h`
  - `socklen_t`
  - `sa_family_t`
  - `struct sockaddr`
  - `struct sockaddr_storage`
  - `struct msghdr`
  - `struct cmsghdr`
  - `struct mmsghdr` when needed by Linux userspace ABI

Hard direction rule:
- `OrlixKernel/**` must not include `OrlixMLibC/**`.
- The only allowed consumers of `OrlixMLibC/**` inside this repo are package-facing compile probes, sysroot/bootstrap plumbing, and non-kernel targets that explicitly model native userspace.
- If Linux-owner code needs a type or struct that is not appropriate to take from Darwin and not appropriate to take from `OrlixMLibC`, define or use a kernel-private/Linux-owned surface instead of crossing into libc ownership.
- Do not "fix" Darwin leakage in `OrlixKernel` by replacing it with `OrlixMLibC` includes. That is still a wrong-direction dependency.
- `OrlixKernel` must not own libc specifics. Libc-facing typedefs, structs, and APIs belong either to vendored Linux UAPI/kernel headers or to `OrlixMLibC`, never to repo-local kernel headers.
- Treat libc-owned spellings such as `pid_t`, `uid_t`, `gid_t`, `mode_t`, `dev_t`, `ino_t`, `nlink_t`, `socklen_t`, `sa_family_t`, `suseconds_t`, `sigval`, `sigevent`, `statvfs`, `termios`, `winsize`, `iovec`, `msghdr`, `cmsghdr`, and `mmsghdr` as non-kernel ownership unless they come from the correct vendored Linux source of truth.
- `OrlixKernel/**` must not include libc-facing headers such as `sys/socket.h`, `sys/types.h`, `sys/uio.h`, `sys/ioctl.h`, `sys/select.h`, `sys/resource.h`, `sys/statvfs.h`, `sys/wait.h`, `poll.h`, `termios.h`, or `signal.h`. Use vendored Linux UAPI/kheaders or a kernel-owned private state model instead.
- Do not recreate public socket ABI structs or typedefs inside `OrlixKernel`. Socket runtime behavior is kernel-owned; userspace socket ABI types are `OrlixMLibC`-owned.

Correct native userspace path:

```text
native iOS arm64/aarch64 package code
        ↓
OrlixMLibC Linux ABI headers + Orlix sysdeps
        ↓
OrlixKernel syscall/runtime ABI
        ↓
OrlixKernel virtual kernel subsystems
        ↓
OrlixHostAdapter host mediation
```

Do not vendor mlibc `abis`, `sysdeps`, or `options` into OrlixKernel.
Do not implement libc sysdeps inside OrlixKernel.
Do not create kernel-owned replacements for libc typedef headers such as `pid_t`, `uid_t`, `gid_t`, `mode_t`, `dev_t`, `ino_t`, `sigevent`, `sigval`, `socklen_t`, `statvfs`, or `suseconds_t`.

The Linux header vendoring pipeline must treat mlibc `abis/linux` as a coverage reference:
- map headers already present in vendored generated Linux headers to the correct surface (UAPI vs kernel-internal)
- classify libc-owned surfaces as OrlixMLibC-owned
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
4. `xcodebuild build-for-testing -project OrlixKernel.xcodeproj -scheme OrlixKernel-6.12-arm64 -sdk iphonesimulator -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17'`
5. required targeted tests for the current tranche

Catalyst may be secondary smoke only.
No commit/push before required proof is green.

## 9) Tranche Discipline

Changes must be bounded by subsystem tranche with explicit ownership and proof.

Do not mix unrelated architecture migrations into one tranche.
Do not “fix lint” by weakening checks or broadening allowlists.

## 10) Quality Bar: No Shortcuts

OrlixKernel is a kernel/runtime substrate, not a test-passing exercise.

Forbidden implementation behavior:
- shallow compatibility stubs presented as completed kernel capability
- compile-only hacks that weaken Linux source compatibility
- local typedefs, aliases, wrappers, or renames that dodge ABI ownership
- replacing Linux UAPI/libc-owned concepts with repo-local convenience types
- renaming Linux concepts into repo-local “private” or “compat” spellings instead of using Linux names
- using Darwin/POSIX host types because they are easier to compile
- moving semantic problems into tests instead of fixing subsystem ownership
- stopping at the smallest green test when the architecture is still wrong
- narrowing behavior to one current test case instead of modeling the Linux-facing rule

Required implementation behavior:
- use vendored generated Linux headers for kernel/userspace contract truth
- keep libc-owned typedef/API surfaces out of OrlixKernel and in OrlixMLibC
- model the real virtual-kernel behavior in the owning subsystem
- keep host mediation inside `OrlixHostAdapter/**` only
- declare any cross-target contract from `OrlixKernel`, not from `OrlixHostAdapter`
- keep `OrlixKernel` independent from `OrlixMLibC` includes and package-facing header ownership
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
- Do not create repo-local header files whose purpose is to restate Linux UAPI or kheaders concepts that already exist under `third_party/linux/<version>/<arch>/**`.
- Do not create repo-local wrapper macros or helper functions whose sole job is to rename Linux constants, Linux structs, or Linux fd-set/socket/time/termios behavior into project-specific spellings.
- Laziness is not an implementation strategy: do not choose shallow stubs,
  renamed adapters, local typedefs, or narrow test-shaped behavior when the
  kernel capability requires real subsystem semantics.

## 11) No Policy Theater

Forbidden:
- incident-specific blacklist hacks (single test/helper name grudges)
- fake completion claims without repo truth and proof logs
- cosmetic renames that preserve the same architectural violation
- adapter-owned seam headers consumed by `OrlixKernel`
- redundant path or filename role labels such as `internal/ios`, `*_host`, and `*_bridge` in the accepted end-state
- repo-local Linux-concept renames such as `orlix_*`, `kernel_*`, or `*_compat` when Linux naming already exists
- near-equivalent permutations of the same rename pattern using `linux_*`, `ix_*`, `private_*`, `internal_*`, `bridge_*`, `adapter_*`, `shim_*`, or matching suffix variants

Required response to lint conflicts:
- refine seam boundaries
- relocate host mechanics behind `OrlixHostAdapter/**`
- move cross-target declarations under kernel-owned private contract headers
- preserve Linux-owner semantics and contracts

@RTK.md
