# IXLandKernel and IXLandHostAdapter Split Plan

## Status

Authoritative first-step architecture plan.

This plan is the prerequisite for all further roadmap work.

Nothing else in the native-only package compatibility program should begin before this split and boundary lockdown is complete.

## Purpose

The product goal is a Linux-identical userspace surface inside an iOS app sandbox, with Linux-oriented packages rebuilt as native iOS arm64 code and requiring zero target-package source modifications.

The practical blocker is persistent Darwin and host-mechanics leakage into Linux-owner code. Directory naming and convention have proven insufficient to stop coding agents from importing Darwin headers, host wrappers, and host-shaped abstractions into Linux semantics.

The solution is to make the Linux-owner versus host-mediation boundary real in the build graph, include graph, and proof graph.

This split is not cosmetic refactoring. It is an enforcement mechanism.

## Product Context

Version 1 product scope:

- Native iOS arm64 execution only
- Linux packages rebuilt from source
- Zero target-package source modifications
- Linux-identical observable behavior through `IXLandMLibC` plus `IXLandKernel`
- Shebangs and script execution are mandatory
- No ELF execution or WASM execution in this version

Future scope that must be planned for now:

- Native Linux ELF execution through emulation
- WASM execution

Those future execution modes must plug into the execution architecture later, but they must not distort Version 1 or justify Darwin leakage now.

## Naming Decisions

Use these names:

1. `IXLandKernel`
- Owns Linux semantics
- Owns Linux-owner source trees

2. `IXLandHostAdapter`
- Owns iOS and Darwin host mediation only
- Owns the current `internal/ios/**` subtree

3. `IXLandSystem`
- Optional umbrella or integration target
- Links `IXLandKernel` and `IXLandHostAdapter`

Rationale:

- `IXLandKernel` states clearly that this layer owns Linux behavior.
- `IXLandHostAdapter` states clearly that this layer adapts to the host and does not own Linux semantics.
- Avoid generic names such as `HostBridge` or `Platform`, which tend to become abstraction bags.

## Architectural Decision

Split `internal/ios/**` into a separate private package or target before all other roadmap work.

The split must enforce these truths:

- Linux semantics live in `IXLandKernel`.
- Host mechanics live in `IXLandHostAdapter`.
- `IXLandKernel` must not see arbitrary host implementation headers.
- `IXLandHostAdapter` must not decide Linux semantics.

This must be true by build structure, not by developer intention alone.

## Layer Model

### IXLandKernel

Owns:

- `fs/**`
- `kernel/**`
- `runtime/**`
- `include/**`

Responsibilities:

- Linux syscall-visible semantics
- Linux runtime semantics
- virtual kernel objects and state
- VFS, task, signal, wait, PTY, procfs, futex, cgroup, namespace, seccomp, ptrace, netlink, and related kernel behavior
- Linux-facing ABI discipline

Must not own:

- Darwin APIs
- Foundation APIs
- pthread semantics as Linux truth
- iOS container path discovery policy
- host errno policy as Linux truth

### IXLandHostAdapter

Owns:

- `internal/ios/**`

Responsibilities:

- Host file I/O mechanics
- Host path discovery
- Host clock access
- Host sleep and wake primitives
- Host thread and synchronization realization
- Host signal mask save and restore where needed as mechanism only
- Security-scoped file access mechanics when introduced
- Host-side backing storage setup

Must not own:

- Linux path semantics
- Linux mount semantics
- Linux signal semantics
- Linux wait semantics
- Linux VFS policy
- Linux credentials or capabilities policy
- Linux syscall results or Linux errno policy, except narrow translation at the seam

### IXLandSystem

Responsibilities:

- Product integration target
- Link composition of kernel plus host adapter
- Final library or app-facing build product if needed

## Why The Split Must Be First

Current repo reality already shows structural leakage risk:

- Linux-owner files directly include `internal/ios/fs/*.h`
- `runtime/native/registry.c` directly includes `internal/ios/runtime/sync.h`
- `kernel/sync.h` and `fs/sync.h` expose host-shaped storage-backed thread and synchronization types into Linux-owner code
- broad header visibility currently allows source files to import host implementation headers too easily

If roadmap work continues before the split, new Linux code will keep being written against the wrong boundary.

That makes every subsequent milestone more expensive and less trustworthy.

## Current Repo Context

Relevant observations from the current tree:

- `internal/ios/fs/` currently contains host backing storage, errno translation, open flag translation, path helpers, epoll bridge helpers, memfd host helpers, and sync helpers.
- `internal/ios/kernel/` currently contains host clock, signal bridge, and sync helpers.
- `internal/ios/runtime/` currently contains `sync.h`, which is imported by `runtime/native/registry.c`.
- HostBridge tests already exist as a separate proof target in `project.yml`.
- Linux-owner code currently imports host headers directly, including:
  - `fs/read_write.c`
  - `fs/fdtable.c`
  - `fs/poll.c`
  - `fs/vfs.c`
  - `fs/stat.c`
  - `fs/inode.c`
  - `fs/readdir.c`
  - `fs/ioctl.c`
  - `fs/namei.c`

This means there is already a natural seam, but it is weakly enforced.

## First-Principles Guardrail

The split is successful only if `IXLandKernel` can no longer accidentally import host implementation detail.

The goal is not merely separate compilation.

The goal is:

- accidental Darwin leakage becomes impossible or immediately build-breaking
- host seam shape becomes explicit and narrow
- Linux semantics stay inside the kernel package by construction

## Non-Negotiable Rules

1. `IXLandKernel` decides Linux semantics.
2. `IXLandHostAdapter` decides only how to realize host mechanics.
3. `IXLandKernel` must not include arbitrary headers from `internal/ios/**`.
4. `IXLandHostAdapter` must not expose Darwin types, Foundation types, pthread types, Objective-C types, or generic platform wrappers through its exported seam.
5. LinuxKernel tests must not depend on host implementation headers.
6. HostBridge tests may verify host mechanics, but they do not prove Linux semantics.
7. The split must reduce, not broaden, the host seam surface.

## Do And Don't Rules

### Do

- Make the package boundary real in the build graph.
- Export only narrow, curated seam headers from `IXLandHostAdapter`.
- Keep Linux semantic decisions inside `IXLandKernel`.
- Keep host mechanics inside `IXLandHostAdapter`.
- Prefer opaque handles and narrow functions over reusable wrapper frameworks.
- Add build and lint enforcement so boundary violations fail fast.
- Keep the seam subsystem-owned.

### Don't

- Do not create a generic platform abstraction library.
- Do not expose all of `internal/ios/**` as exported package headers.
- Do not export Objective-C, Foundation, UIKit, pthread, or host POSIX types into `IXLandKernel`.
- Do not freeze broad host-thread or host-sync wrapper families as a long-term kernel interface.
- Do not solve this by directory renaming alone.
- Do not preserve ambient include visibility if it still allows direct host imports.

## Exported Seam Policy

`IXLandHostAdapter` must export a curated seam surface and keep the rest private.

### Required properties of exported seam headers

1. Narrow
- Only the minimal functions and types needed by `IXLandKernel`

2. Neutral
- No Darwin vocabulary in the exported type system
- No Foundation or Objective-C surface
- No raw pthread types

3. Subsystem-owned
- FS host seam for fs-owned mediation
- Kernel host seam for kernel-owned mediation
- Runtime host seam only where unavoidable

4. Mechanism-only
- Answers how to do host work
- Never answers what Linux semantics should be

### Suggested export shape

Use a distinct exported seam namespace instead of allowing direct inclusion of implementation headers under `internal/ios/**`.

Conceptually:

- `IXLandHostAdapter/fs/...`
- `IXLandHostAdapter/kernel/...`
- `IXLandHostAdapter/runtime/...`

Only those curated exported headers may be visible to `IXLandKernel`.

Everything else in `internal/ios/**` remains private implementation detail.

## Include Graph Policy

### IXLandKernel allowed includes

- Linux UAPI headers
- Classified private `IXLandKernel` headers
- Curated exported seam headers from `IXLandHostAdapter`

### IXLandKernel forbidden includes

- non-exported `internal/ios/**` headers
- Darwin headers
- Foundation headers
- UIKit headers
- pthread headers
- dispatch headers

### IXLandHostAdapter allowed includes

- Darwin and iOS SDK headers
- narrow shared contract headers from `IXLandKernel` only where needed

### IXLandHostAdapter discouraged includes

- broad Linux subsystem internals when a narrower contract can be introduced instead

## Build Graph Decision

Recommended dependency direction:

- `IXLandKernel` depends on `IXLandHostAdapter` exported seam
- `IXLandHostAdapter` may depend only on narrow `IXLandKernel` contract headers where unavoidable
- `IXLandSystem` links both

Semantically, Linux model first and host realization second remains the product truth.

## Header Search Path Guardrails

The split will fail if broad header search paths continue to make all of `internal/ios/**` ambiently visible.

### Required target-level policy

`IXLandKernel` should receive only:

- its own Linux-owner roots
- vendored Linux UAPI include root
- the curated exported seam include root from `IXLandHostAdapter`

It should not receive whole-repo host implementation visibility as ambient include search state.

### Transitional rule if full lockdown cannot happen immediately

If the repo still requires broad root visibility temporarily, add a hard lint blocker:

- any `IXLandKernel` source including `internal/ios/...` directly fails lint unless the path is part of the curated exported seam namespace

This transitional state is not the goal. It is only acceptable if clearly temporary.

## Existing Risky Surfaces To Address Inside Milestone 0

These are not later cleanup. They are part of the split tranche because they define the seam shape.

### 1. `kernel/sync.h`

Current problem:

- exposes storage-backed thread, mutex, cond, once, and signal-set wrappers that are explicitly designed to be cast to pthread types in the host layer

Risk:

- freezes host-thread and host-sync design into Linux-owner code
- invites Linux semantics to be written around host primitives

Milestone 0 direction:

- do not let this remain the long-term kernel seam model
- move toward opaque execution, wait, and lock state where Linux-owner code should not know the host primitive

### 2. `fs/sync.h`

Current problem:

- same pattern for fs mutexes and condvars

Milestone 0 direction:

- minimize or replace with narrower internal locking ownership that does not encode host storage assumptions into Linux-owner design

### 3. `runtime/native/registry.c`

Current problem:

- directly includes `internal/ios/runtime/sync.h`

Milestone 0 direction:

- move through exported adapter seam or pull synchronization ownership back into a neutral kernel-owned contract

### 4. direct host header imports from `fs/**`

Current problem:

- multiple `fs/**` files include `internal/ios/fs/*.h` directly

Milestone 0 direction:

- replace those with curated exported seam headers only

### 5. host package dependency on broad kernel internals

Examples:

- `internal/ios/kernel/signal_bridge.c` includes `kernel/signal.h`
- several host fs files include `fs/stat_types.h`
- `internal/ios/kernel/sync.c` includes `kernel/task.h`

Milestone 0 direction:

- where possible, replace with narrow contract headers
- only keep direct internal dependencies when the contract is truly kernel-private and intentionally shared

## Milestone 0 Deliverables

1. Create `IXLandKernel` target or package.
2. Create `IXLandHostAdapter` target or package.
3. Move current `internal/ios/**` ownership fully under `IXLandHostAdapter`.
4. Define and expose only curated seam headers from `IXLandHostAdapter` to `IXLandKernel`.
5. Lock down include visibility so `IXLandKernel` cannot import host implementation headers casually.
6. Update tests so Linux proof and host proof remain intentionally separated.
7. Add lint and no-garbage checks that make regression impossible by policy.

## Milestone 0 Acceptance Gates

Milestone 0 is not complete until all are true:

1. `IXLandKernel` builds without direct imports of non-exported host headers.
2. Darwin, Foundation, UIKit, pthread, and dispatch headers do not appear in Linux-owner paths.
3. `IXLandHostAdapter` builds as its own target or package.
4. LinuxKernel tests remain host-implementation-free.
5. HostBridge tests remain isolated to host mediation proof.
6. Direct `internal/ios/...` imports from Linux-owner code are either gone or limited strictly to the curated exported seam namespace if transitional naming remains.
7. The exported seam surface is documented and reviewed as part of the split.

## Test And Proof Policy

### LinuxKernel tests

Must prove:

- Linux-facing behavior through integrated product behavior

Must not:

- include host implementation headers
- use Darwin or Foundation host behavior as Linux proof

### HostBridge tests

May prove:

- backing storage discovery
- errno translation
- host file and path mechanics
- host clock and synchronization mechanics
- security-scoped lifecycle mechanics later

Must not claim:

- Linux semantics are correct merely because adapter behavior is correct

## Lint Guardrails

Add hard checks for Milestone 0:

1. Fail on Darwin or iOS headers in Linux-owner paths.
2. Fail on non-exported `IXLandHostAdapter` header imports from `IXLandKernel`.
3. Fail on direct `internal/ios/**` imports from Linux-owner code once exported seam namespace is in place.
4. Fail on Foundation, UIKit, pthread, dispatch vocabulary in Linux-owner headers and source.
5. Fail on new generic host wrapper families appearing in Linux-owner ownership zones.

## Decisions About Future Work

This split does not postpone the future architecture for ELF or WASM.

It improves it.

By forcing a narrow host mediation layer now, future execution backends can plug into:

- a cleaner execution-image abstraction
- a cleaner task execution model
- a cleaner virtual memory access model
- a cleaner signal and ptrace integration boundary

Version 1 remains native-only.

Future ELF and WASM execution must not cause new Darwin leakage into `IXLandKernel`.

## Dependency On Broader Roadmap

The native-only package compatibility roadmap must be reordered so this split comes first.

Revised roadmap order:

1. `IXLandHostAdapter` split and boundary lockdown
2. sysroot and build truth
3. native exec and shebang semantics
4. shell-critical process model
5. signal and PTY fidelity
6. VFS and runtime environment
7. fd, readiness, and pipe semantics
8. networking for `curl`
9. package compatibility program with:
   - `zsh` primary proof target
   - `curl` secondary proof target

## Sequencing Within Milestone 0

1. Define target graph
- introduce `IXLandKernel`
- introduce `IXLandHostAdapter`
- define optional `IXLandSystem` integration target behavior

2. Define curated exported seam surface
- classify which current host headers are exported
- classify which remain private
- introduce new exported seam namespace if needed

3. Lock down `IXLandKernel` include visibility
- reduce ambient visibility
- make direct host implementation imports impossible or lint-fatal

4. Move `internal/ios/**` sources under `IXLandHostAdapter` ownership in build configuration

5. Update Linux-owner includes to use curated exported seam headers only

6. Update tests and target dependencies

7. Add lint and proof gates

8. Only after all gates are green, resume the rest of the roadmap

## What Not To Optimize For

Do not optimize for:

- preserving current include convenience
- minimizing target churn at the expense of real isolation
- keeping current sync wrapper design because it compiles today
- package naming symmetry over enforcement value
- broad “platform abstraction” reuse

The split is successful only if it makes wrong code hard to write.

## Open Technical Follow-Ups After Milestone 0

These are expected next, but not part of the split completion claim:

- redesign Linux-owner task execution state to remove host-thread primitive leakage
- redesign signal ownership around per-task and shared state split
- tighten futex guest-memory semantics
- formalize VFS backend object model
- continue sysroot discipline in `IXLandMLibC`

Those belong to later milestones and should happen on top of the enforced boundary created here.

## Final Rule

If a coding agent can still casually import Darwin mechanics into Linux-owner code after this split, the split is not done.

The goal is not a new directory structure.

The goal is a system where Linux semantics and iOS mediation are structurally forced to stay in their owning layers.
