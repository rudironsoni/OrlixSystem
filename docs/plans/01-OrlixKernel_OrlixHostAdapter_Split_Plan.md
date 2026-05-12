# OrlixKernel and OrlixHostAdapter Split Plan

## Status

Authoritative boundary plan plus retrospective delivery audit.

The target split landed in the physical tree, include graph, and proof graph. The old branded kernel-facing seam is no longer active.

What was delivered:

- `OrlixKernel` is the main project and main output identity.
- `OrlixKernel` and `OrlixHostAdapter` build as separate targets.
- `OrlixKernelTests` and `OrlixHostAdapterTests` are split in `project.yml`.
- `OrlixKernel` target-level header search paths do not ambiently expose raw host implementation files.
- `OrlixKernel` no longer includes headers from `OrlixHostAdapter/**`.
- `OrlixHostAdapter/include` is removed.
- redundant `internal/ios`, `*_host`, and `*_bridge` path/file naming was removed from the active tree.
- the kernel-facing seam now uses kernel-owned private `backing_*` declarations.

What is not yet safe to call finished forever:

- `OrlixHostAdapter` still receives broader `OrlixKernel` roots than the ideal narrow-contract end-state, including `OrlixKernel/internal`
- some host-private helper names still contain descriptive host wording inside `OrlixHostAdapter`, even though they no longer pollute the kernel-facing seam
- the latest simulator proof showed that structural split success does not guarantee correct Linux-visible runtime behavior from host-backed primitives; memfd backing had to be corrected after the seam cutover

Under the product rule, the seam question is now in the correct state: `OrlixKernel` is no longer speaking to the host through branded adapter-owned headers. The remaining work is to narrow adapter dependencies further and to keep proving that host realization choices preserve Linux-visible behavior.

## Purpose

The product goal is a Linux-identical userspace surface inside an iOS app sandbox, with Linux-oriented packages rebuilt as native iOS arm64 code and requiring zero target-package source modifications.

The practical blocker is persistent Darwin and host-mechanics leakage into Linux-owner code. Directory naming and convention have proven insufficient to stop coding agents from importing Darwin headers, host wrappers, and host-shaped abstractions into Linux semantics.

The solution is to make the Linux-owner versus host-mediation boundary real in the build graph, include graph, and proof graph.

This split is not cosmetic refactoring. It is an enforcement mechanism, and the memfd fallout from the cutover proved why runtime proof still matters after the structure is corrected.

## Product Context

Version 1 product scope:

- Native iOS arm64 execution only
- Linux packages rebuilt from source
- Zero target-package source modifications
- Linux-identical observable behavior through the userspace libc layer plus `OrlixKernel`
- Shebangs and script execution are mandatory
- No ELF execution or WASM execution in this version

Future scope that must be planned for now:

- Native Linux ELF execution through emulation
- WASM execution

Those future execution modes must plug into the execution architecture later, but they must not distort Version 1 or justify Darwin leakage now.

## Naming Decisions

Use these names:

1. `OrlixKernel`
- Owns Linux semantics
- Owns Linux-owner source trees

2. `OrlixHostAdapter`
- Owns iOS and Darwin host mediation only
- Owns `OrlixHostAdapter/**`
- Must not export adapter-owned headers as the kernel-facing seam

3. `OrlixSystem`
- Retired name
- Must not remain as an active project or product identity
- Must not be treated as an architectural owner

Rationale:

- `OrlixKernel` states clearly that this layer owns Linux behavior.
- `OrlixHostAdapter` states clearly that this layer adapts to the host and does not own Linux semantics.
- Retiring `OrlixSystem` avoids a third branded owner concept that blurs the binary Linux-versus-host boundary.

## Architectural Decision

Keep `OrlixHostAdapter/**` isolated as the private host target boundary before all other roadmap work continues.

The split must enforce these truths:

- Linux semantics live in `OrlixKernel`.
- Host mechanics live in `OrlixHostAdapter`.
- `OrlixKernel` must not see arbitrary host implementation headers.
- `OrlixHostAdapter` must not decide Linux semantics.

This must be true by build structure, not by developer intention alone.

## Layer Model

### OrlixKernel

Owns:

- `OrlixKernel/fs/**`
- `OrlixKernel/kernel/**`
- `OrlixKernel/runtime/**`
- `OrlixKernel/include/**`
- `OrlixKernel/observability/**`
- `OrlixKernel/internal/**`

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

### OrlixHostAdapter

Owns:

- `OrlixHostAdapter/**`
- no kernel-facing exported header surface

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

Additional simplification rule:

- do not preserve redundant path components such as `internal/ios`
- do not preserve role suffixes such as `_host` or `_bridge` in accepted end-state filenames

## Why The Split Must Be First

Current repo reality already shows structural leakage risk:

- Before the split, Linux-owner files directly included `internal/ios/fs/*.h`
- Before the split, `runtime/native/registry.c` directly included `internal/ios/runtime/sync.h`
- Before the split, `kernel/sync.h` and `fs/sync.h` exposed host-shaped storage-backed thread and synchronization types into Linux-owner code
- Broad header visibility was allowing source files to import host implementation headers too easily

If roadmap work continues before the split, new Linux code will keep being written against the wrong boundary.

That makes every subsequent milestone more expensive and less trustworthy.

## Current Repo Context

Relevant observations from the current tree:

- `OrlixHostAdapter/fs/` is the intended host backing-storage and path realization area.
- `OrlixHostAdapter/kernel/` is the intended host clock, signal, and synchronization realization area.
- Linux-owner code now lives under `OrlixKernel/**`.
- the active seam is now kernel-owned `backing_*` declarations under `OrlixKernel/internal/**`.
- the end-state is still for `OrlixKernel` to receive only `OrlixKernel` roots, vendored Linux include roots, and the narrowest necessary kernel-owned private contract roots.
- Tests are split into `OrlixKernelTests` and `OrlixHostAdapterTests` in `project.yml`.
- host-backed directory iteration for `fs/readdir.c` now routes through a kernel-owned private `backing_dir` contract instead of ambient `dirent.h` usage in Linux-owner code.
- The direct host imports that motivated this plan were removed from the Linux-owner files that previously lived at:
  - `fs/read_write.c`
  - `fs/fdtable.c`
  - `fs/poll.c`
  - `fs/vfs.c`
  - `fs/stat.c`
  - `fs/inode.c`
  - `fs/readdir.c`
  - `fs/ioctl.c`
  - `fs/namei.c`

This means the coarse split and the active kernel-facing seam correction are both present, but dependency narrowing and behavioral proof remain active concerns.

## Delivery Assessment

### Delivered coherently

- the main project identity is now `OrlixKernel`
- target separation exists in `project.yml`
- `OrlixKernel` no longer gets ambient visibility into raw host implementation files
- Linux and host tests are split into distinct targets
- lint enforces several Linux-owner versus host-owner violations
- `OrlixKernel` no longer includes adapter-owned headers
- adapter-owned branded seam headers are gone from the active tree
- full simulator proof is green after the seam cutover and follow-up runtime fixes

### Not yet delivered coherently

- `OrlixHostAdapter` still compiles with broad visibility into `OrlixKernel`, including `OrlixKernel/internal`. That may be temporarily necessary, but it is not the narrow contract end-state.
- Host-private implementation choices can still violate Linux-visible expectations even when the seam is structurally correct. The memfd backing path had to be reworked after simulator proof exposed Darwin-specific behavior that Linux code could not tolerate.
- Some host-private helper names remain descriptive rather than purely neutral, which is acceptable only while they stay private to `OrlixHostAdapter`.

Conclusion:

The split was delivered cleanly enough to say the active kernel-facing surface is no longer host-branded. The remaining obligation is to keep narrowing the adapter dependency boundary and to keep proving host-backed runtime behavior against Linux-facing expectations.

## First-Principles Guardrail

The split is successful only if `OrlixKernel` can no longer accidentally import host implementation detail.

The goal is not merely separate compilation.

The goal is:

- accidental Darwin leakage becomes impossible or immediately build-breaking
- host seam shape becomes explicit and narrow
- Linux semantics stay inside the kernel package by construction

## Non-Negotiable Rules

1. `OrlixKernel` decides Linux semantics.
2. `OrlixHostAdapter` decides only how to realize host mechanics.
3. `OrlixKernel` must not include any header from `OrlixHostAdapter/**`.
4. `OrlixHostAdapter` must not expose Darwin types, Foundation types, pthread types, Objective-C types, or generic platform wrappers through any adapter-owned seam.
5. `OrlixKernelTests` must not depend on host implementation headers.
6. `OrlixHostAdapterTests` may verify host mechanics, but they do not prove Linux semantics.
7. The split must reduce, not broaden, the host seam surface.

## Do And Don't Rules

### Do

- Make the package boundary real in the build graph.
- Export only narrow, curated seam headers from `OrlixHostAdapter`.
- Keep Linux semantic decisions inside `OrlixKernel`.
- Keep host mechanics inside `OrlixHostAdapter`.
- Prefer opaque handles and narrow functions over reusable wrapper frameworks.
- Add build and lint enforcement so boundary violations fail fast.
- Keep the seam subsystem-owned.

### Don't

- Do not create a generic platform abstraction library.
- Do not expose all of `OrlixHostAdapter/internal/ios/**` as exported package headers.
- Do not export Objective-C, Foundation, UIKit, pthread, or host POSIX types into `OrlixKernel`.
- Do not freeze broad host-thread or host-sync wrapper families as a long-term kernel interface.
- Do not solve this by directory renaming alone.
- Do not preserve ambient include visibility if it still allows direct host imports.

## Kernel-Owned Contract Policy

`OrlixKernel` must own the private cross-target contract surface and `OrlixHostAdapter` must implement it.

### Required properties of private contract headers

1. Kernel-owned
- Declared under `OrlixKernel` private contract roots
- Never declared under `OrlixHostAdapter`

2. Narrow
- Only the minimal functions and types needed by `OrlixKernel`

3. Neutral
- No Darwin vocabulary in the contract type system
- No Foundation or Objective-C surface
- No raw pthread types

4. Subsystem-owned
- FS host seam for fs-owned mediation
- Kernel host seam for kernel-owned mediation
- Runtime host seam only where unavoidable

5. Mechanism-only
- Answers how to do host work
- Never answers what Linux semantics should be

### Required shape

Do not treat an adapter-owned include namespace as success by itself.

Do not regress to `OrlixKernel` including `OrlixHostAdapter/...` headers or ambient `host_*` vocabulary.

The contract must trend toward narrow mechanism declarations that do not make branded host vocabulary ambient in Linux-owner code.

End-state structure:

- `OrlixKernel/internal/contracts/fs/...`
- `OrlixKernel/internal/contracts/kernel/...`
- `OrlixKernel/internal/contracts/runtime/...`
- `OrlixHostAdapter/fs/...`
- `OrlixHostAdapter/kernel/...`
- `OrlixHostAdapter/runtime/...`

Current repo truth:

- `OrlixHostAdapter/**` is the simplified host implementation tree
- `OrlixHostAdapter/include` is removed
- redundant `internal/ios` is removed from the active tree
- kernel-owned contract roots are active and must stay kernel-owned

Only kernel-owned private contract headers may be visible to `OrlixKernel`.

Everything in `OrlixHostAdapter/**` remains private implementation detail.

## Include Graph Policy

### OrlixKernel allowed includes

- Linux UAPI headers
- Classified private `OrlixKernel` headers
- Kernel-owned private contract headers

### OrlixKernel forbidden includes

- any header from `OrlixHostAdapter/**`
- Darwin headers
- Foundation headers
- UIKit headers
- pthread headers
- dispatch headers

### OrlixHostAdapter allowed includes

- Darwin and iOS SDK headers
- narrow shared contract headers from `OrlixKernel` only where needed

### OrlixHostAdapter discouraged includes

- broad Linux subsystem internals when a narrower contract can be introduced instead

## Build Graph Decision

Required dependency direction:

- `OrlixKernel` depends on `OrlixHostAdapter` implementation at link time
- `OrlixHostAdapter` may depend only on narrow `OrlixKernel` contract headers where unavoidable
- no active project or product identity should remain named `OrlixSystem`

Semantically, Linux model first and host realization second remains the product truth.

Current repo truth:

- the first bullet is not yet fully implemented because the seam is still adapter-owned in the source tree
- the second bullet is only partially true today because `OrlixHostAdapter` still receives broad `OrlixKernel` header visibility, including `OrlixKernel/internal`

## Header Search Path Guardrails

The split fails if broad header search paths make host implementation detail or branded host vocabulary ambient in Linux-owner code.

### Required target-level policy

`OrlixKernel` should receive only:

- its own Linux-owner roots
- vendored Linux UAPI include root
- the kernel-owned private contract include root

It should not receive whole-repo host implementation visibility as ambient include search state.

Current repo truth:

- this target-level rule is substantially implemented for `OrlixKernel`
- the corresponding inverse rule is not yet fully implemented for `OrlixHostAdapter`, which still sees broad `OrlixKernel` roots

### Transitional rule if full lockdown cannot happen immediately

If the repo still requires broad root visibility temporarily, add a hard lint blocker:

- any `OrlixKernel` source including anything from `OrlixHostAdapter/**` directly fails lint

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

- directly included `internal/ios/runtime/sync.h` before the split

Milestone 0 direction:

- move through kernel-owned private contracts or pull synchronization ownership back into a neutral kernel-owned contract

### 4. direct host header imports from `OrlixKernel/fs/**`

Current problem:

- multiple Linux-owner fs files included `internal/ios/fs/*.h` directly before the split

Milestone 0 direction:

- replace those with kernel-owned private contract headers only

### 5. host package dependency on broad kernel internals

Examples:

- `internal/ios/kernel/signal_bridge.c` includes `kernel/signal.h`
- several host fs files include `fs/stat_types.h`
- `internal/ios/kernel/sync.c` includes `kernel/task.h`

Those dependencies should now be read as historical examples that justify narrow private contracts, not as permission to reintroduce broad host-to-kernel imports.

Milestone 0 direction:

- where possible, replace with narrow contract headers
- only keep direct internal dependencies when the contract is truly kernel-private and intentionally shared

## Milestone 0 Deliverables

Delivered:

1. `OrlixKernel` target exists.
2. `OrlixHostAdapter` target exists.
3. `OrlixHostAdapter/**` is the host implementation tree.
4. `OrlixKernel` no longer gets direct ambient visibility into raw host implementation files.
5. Linux and host tests are intentionally separated.
6. lint checks exist and are enforcing several boundary rules.

Still open inside the same boundary problem:

1. adapter-owned seams still expose branded host vocabulary to `OrlixKernel`
2. `OrlixHostAdapter` still has broader-than-desired visibility into `OrlixKernel`
3. the split cannot yet be described as fully Linux-shaped by construction

## Milestone 0 Acceptance Gates

Milestone 0 is not complete until all are true:

1. `OrlixKernel` builds without direct imports of non-exported host headers.
2. Darwin, Foundation, UIKit, pthread, and dispatch headers do not appear in Linux-owner paths.
3. `OrlixHostAdapter` builds as its own target or package.
4. `OrlixKernelTests` remain host-implementation-free.
5. `OrlixHostAdapterTests` remain isolated to host mediation proof.
6. `OrlixKernel` does not include any header from `OrlixHostAdapter/**`.
7. The kernel-owned private contract surface is documented and reviewed as part of the split.
8. The kernel-facing seam does not require ambient branded host vocabulary as the normal way Linux-owner code reaches host mechanics.
9. `OrlixHostAdapter` no longer relies on broad `OrlixKernel` header visibility where a narrower contract would suffice.
10. redundant `internal/ios`, `*_host`, and `*_bridge` naming is removed from the accepted end-state.

## Test And Proof Policy

### OrlixKernel tests

Must prove:

- Linux-facing behavior through integrated product behavior
- current tranche behavior through syscall-facing LinuxKernel tests and contract files

Must not:

- include host implementation headers
- use Darwin or Foundation host behavior as Linux proof

### OrlixHostAdapter tests

May prove:

- backing storage discovery
- errno translation
- host file and path mechanics
- host clock and synchronization mechanics
- security-scoped lifecycle mechanics later

Must not claim:

- Linux semantics are correct merely because adapter behavior is correct

Required proof discipline for any future completion claim:

1. `bash ./scripts/lint_linux_surface.sh`
2. `xcodegen generate --project .`
3. `xcodebuild build-for-testing -project OrlixKernel.xcodeproj -scheme OrlixKernel-6.12-arm64 -sdk iphonesimulator -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17'`
4. required targeted tests for the current tranche

Host-adapter tests can block repo-green proof, but they do not replace LinuxKernel proof.

## Lint Guardrails

Add hard checks for Milestone 0:

1. Fail on Darwin or iOS headers in Linux-owner paths.
2. Fail on any `OrlixHostAdapter/**` header imports from `OrlixKernel`.
3. Fail on remaining `internal/ios`, `*_host`, and `*_bridge` naming in the accepted end-state.
4. Fail on Foundation, UIKit, pthread, dispatch vocabulary in Linux-owner headers and source.
5. Fail on new generic host wrapper families appearing in Linux-owner ownership zones.
6. Fail on adapter-owned include roots in the accepted end-state.

## Decisions About Future Work

This split does not postpone the future architecture for ELF or WASM.

It improves it.

By forcing a narrow host mediation layer now, future execution backends can plug into:

- a cleaner execution-image abstraction
- a cleaner task execution model
- a cleaner virtual memory access model
- a cleaner signal and ptrace integration boundary

Version 1 remains native-only.

Future ELF and WASM execution must not cause new Darwin leakage into `OrlixKernel`.

## Dependency On Broader Roadmap

The native-only package compatibility roadmap must be reordered so this split comes first.

Revised roadmap order:

1. `OrlixHostAdapter` split and boundary lockdown
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
- introduce `OrlixKernel`
- introduce `OrlixHostAdapter`
- keep repo-level integration naming separate from architectural ownership

2. Define kernel-owned private contract surface
- classify which current host-facing declarations move under `OrlixKernel`
- classify which host files remain private implementation only
- remove adapter include surfaces and redundant `internal/ios` naming

3. Lock down `OrlixKernel` include visibility
- reduce ambient visibility
- make direct host implementation imports impossible or lint-fatal

4. Keep `OrlixHostAdapter/**` sources under `OrlixHostAdapter` ownership in build configuration

5. Update Linux-owner includes to use kernel-owned private contract headers only

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
- continue sysroot discipline in the userspace libc layer

Those belong to later milestones and should happen on top of the enforced boundary created here.

## Final Rule

If a coding agent can still casually import Darwin mechanics into Linux-owner code after this split, the split is not done.

The goal is not a new directory structure.

The goal is a system where Linux semantics and iOS mediation are structurally forced to stay in their owning layers.
