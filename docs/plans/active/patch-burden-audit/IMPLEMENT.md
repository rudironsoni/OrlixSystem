# Orlix Patch Burden Audit Implementation Log

## Task Reference

`PLAN.md`

---

## Log

### 2026-06-07

**What happened:**

Started a high-skepticism audit of all durable patches under:

- `OrlixOS/Sources/patches`
- `OrlixMLibC/Sources/patches`

The inventory found 34 patches: 3 OrlixOS package patches and 31 OrlixMLibC upstream mlibc patches.

**Decision:**

Do not add, change, or delete any mlibc/package patch until the lower owning layers are proven. Treat test edits as a separate conformance problem: a patch may be useful as an Orlix-owned regression, but it must not mutate upstream tests in the conformance source tree.

**Evidence:**

- `rtk rg --files OrlixOS/Sources/patches OrlixMLibC/Sources/patches`
- `rtk wc -l OrlixOS/Sources/patches/*.patch OrlixMLibC/Sources/patches/*.patch`
- `rtk rg -n "^(Subject:|diff --git|--- a/|\\+\\+\\+ b/)" OrlixOS/Sources/patches OrlixMLibC/Sources/patches`
- `rtk rg -n "tests/|new file|diff --git .*tests|\\+\\+\\+ [bw]/tests|\\+\\+\\+ b/tests" OrlixMLibC/Sources/patches OrlixOS/Sources/patches`

**Initial OrlixOS patch classification:**

| Patch | Class | Current read | Proof required before keeping |
|---|---|---|---|
| `libselinux-3.10-pthread-once-structured-type.patch` | package-portability | Conditional package-source change for `pthread_once_t` representations when `LIBSELINUX_USE_STRONG_PTHREAD_ONCE` is supplied by the libselinux build. | Prove upstream libselinux 3.10 cannot compile against a Linux-compatible mlibc pthread surface without this source patch; prove the define is package/toolchain policy, not hiding a wrong pthread ABI. Prefer upstreamable package portability fix if valid. |
| `perl-cross-1.6.2-darwin-readelf-size.patch` | workaround-suspect | Adds an awk fallback for `readelf` output parsing during perl-cross type-size probing. | Prove whether the real bug is the OrlixOS package toolchain's `readelf` contract. Prefer a reviewed toolchain wrapper or configure input over patching perl-cross probe parsing. |
| `perl-cross-1.6.2-byteorder-fallback.patch` | workaround-suspect | Infers little-endian byte order from integer width when perl-cross cannot determine byte order. | Replace with target/schema-derived byte-order metadata or a documented perl-cross configure hint. Do not keep a source patch that guesses target endianness from `uvsize`. |

**Initial OrlixMLibC patch classification:**

| Patch | Class | Current read | Proof required before keeping |
|---|---|---|---|
| `0001-sysdeps-linux-aarch64-use-orlix-hosted-syscall-gate.patch` | target-integration | Replaces AArch64 `svc` syscall path with the hosted syscall gate and adds hosted TLS/panic plumbing. | Prove the gate uses Linux syscall numbers, argument order, negative errno, cancellation, clone/thread, signal-return, and aux/TLS semantics. Gate address must come from target metadata, not magic defaults. |
| `0002-mlibc-apply-frigg-positional-printf-arg-cache-diff.patch` | package-portability | Patches frigg printf positional argument caching. | Prove this is a frigg/mlibc semantic bug with reference behavior; upstream or vendor it as a dependency patch with tests, not as an unexplained Orlix workaround. |
| `0003-sysdeps-linux-aarch64-use-orlix-page-size.patch` | target-integration | Uses `ORLIX_HOSTED_PAGE_SIZE` for mlibc page-size and guard-size alignment. | Prove the value is derived from the OrlixKernel target/config and all mmap/thread guard behavior matches Linux page semantics. Remove hardcoded defaults. |
| `0004-sysdeps-linux-implement-chroot.patch` | libc-semantics | Adds Linux `chroot` sysdep by calling `SYS_chroot`. | Prove OrlixKernel implements Linux `chroot` semantics; then this is normal mlibc Linux sysdeps completion. |
| `0005-posix-stat-declare-optional-type-test-macros.patch` | libc-semantics | Adds Linux-visible `S_TYPEIS*` macros. | Compare against Linux/glibc headers and prove no false runtime object-type claims. |
| `0006-ansi-limits-declare-c23-width-macros.patch` | libc-semantics | Adds C23 width macros from compiler builtins. | Prove values match target compiler ABI and Linux-visible headers. |
| `0007-ansi-math-declare-xopen-and-gnu-libm-functions.patch` | libc-semantics | Adds math declarations for X/Open/GNU functions. | Prove every declared symbol exists and has Linux-compatible semantics. |
| `0008-posix-pathconf-report-path-max.patch` | libc-semantics | Returns `PATH_MAX` for `_PC_PATH_MAX`. | Prove this matches Linux behavior for Orlix filesystems or route dynamic pathconf behavior to the kernel/filesystem layer. |
| `0009-posix-test-wait3-rusage.patch` | test-impurity | Adds an upstream mlibc test file. | Remove from the upstream conformance patch stack or move to Orlix-owned regression tests. It cannot count as exact upstream mlibc proof. |
| `0010-posix-wait3-use-wait4-sysdep.patch` | libc-semantics | Implements `wait3` through `Waitpid`/`wait4` sysdep with rusage. | Prove OrlixKernel wait/reaping/rusage semantics with upstream tests; do not use `0009` as conformance proof. |
| `0011-posix-add-euidaccess-eaccess.patch` | libc-semantics + test-impurity | Adds GNU `euidaccess`/`eaccess` and modifies an upstream access test. | Keep runtime code only after effective-ID behavior is proven against Linux. Move test additions out of upstream conformance source. |
| `0012-posix-renameat2-zero-flags-via-renameat.patch` | libc-semantics + test-impurity | Adds `renameat2`, flags, sysdep, fallback, and a new upstream test. | Prove OrlixKernel `renameat2` flag semantics. Zero-flag fallback is acceptable only for `ENOSYS`; test addition must move to Orlix-owned regression proof. |
| `0013-ansi-string-keep-posix-strerror-r-as-public-name.patch` | libc-semantics + test-impurity | Changes GNU/POSIX `strerror_r` exposure and modifies upstream string test. | Prove glibc/musl source compatibility for feature macros and symbols. Remove test mutation from upstream conformance patch stack. |
| `0014-internal-allocator-use-hosted-page-size.patch` | target-integration | Makes allocator page size depend on `ORLIX_HOSTED_PAGE_SIZE`. | Same page-size proof as `0003`; source must consume target-derived metadata. |
| `0015-sysdeps-linux-report-linux-clk-tck.patch` | target-integration | Hardcodes `_SC_CLK_TCK` to 100. | Prove this comes from Linux `USER_HZ`/target config or UAPI, not an arbitrary libc constant. |
| `0016-locale-c-wide-conversion-uses-ascii.patch` | workaround-suspect | Treats non-UTF-8 active locales as ASCII for wide/multibyte conversion. | First prove locale files, env, `setlocale`, `nl_langinfo(CODESET)`, `MB_CUR_MAX`, and file-window syscalls work through OrlixOS/rootfs/kernel. Then compare mlibc behavior against Linux/glibc before keeping or changing this patch. |
| `0017-options-getopt-use-gnu-diagnostics.patch` | libc-semantics | Adjusts getopt diagnostics to GNU-style messages. | Prove exact GNU getopt diagnostics and parser state behavior with unmodified upstream package tests. |
| `0018-posix-readdir-reports-getdents-errors.patch` | libc-semantics | Returns `NULL` and sets `errno` on directory read errors instead of aborting. | Prove sysdep returns Linux errno and that EOF vs error semantics match Linux. |
| `0019-sysdeps-linux-implement-vmsplice.patch` | libc-semantics | Adds Linux `vmsplice` wrapper/sysdep. | Prove OrlixKernel implements `vmsplice` semantics or returns the correct Linux errno; then this is sysdeps completion. |
| `0020-frigg-format-long-double-with-long-double-arithmetic.patch` | package-portability | Patches frigg formatting to preserve long-double arithmetic. | Prove against glibc/reference formatting and upstream frigg behavior; upstream or isolate as dependency patch with tests. |
| `0021-ansi-mb-cur-max-follows-active-locale.patch` | libc-semantics + test-impurity | Makes `MB_CUR_MAX` call active locale metadata and modifies upstream locale test. | Runtime fix is plausible, but only after locale loading proof. Test mutation must not count as upstream conformance proof. |
| `0022-posix-nl-langinfo-returns-gnu-time-strings.patch` | libc-semantics | Extends `nl_langinfo_l` routing for GNU time items. | Prove locale archive data and item mapping match Linux/glibc. |
| `0023-elf-cache-auxv-from-entry-stack.patch` | target-integration | Caches auxv from the entry stack for `peekauxval`. | Prove Orlix exec stack layout and auxv entries are Linux-shaped and survive startup. |
| `0024-ansi-stdio-reclassify-redirected-pipes.patch` | workaround-suspect | Reclassifies stdio streams as pipe-like after `ESPIPE` on a zero seek adjustment. | High-risk lower-layer workaround. Prove Linux open-file-description, fd redirection, shell builtin, and stdio buffering behavior before keeping. |
| `0025-posix-pwd-grp-use-linux-nss-no-match-semantics.patch` | libc-semantics | Changes passwd/group no-match errno behavior. | Compare glibc/musl behavior for missing users/groups vs real I/O errors. |
| `0026-internal-allocator-propagate-anon-allocate-failure.patch` | libc-semantics | Returns 0 on allocator mmap failure instead of aborting. | Prove callers handle allocation failure correctly and Linux errno is preserved where relevant. |
| `0027-ansi-strerror-use-linux-enoent-text.patch` | libc-semantics | Changes `strerror(ENOENT)` to glibc-style text for Coreutils diagnostics. | Prove Linux/glibc string compatibility and unmodified Coreutils expected output. |
| `0028-ansi-stdio-preserve-fclose-error-surface.patch` | libc-semantics | Preserves flush/close errors through `fclose` return and errno. | Prove with Linux/glibc reference and upstream stdio/package tests. |
| `0029-ansi-time-normalize-broken-down-time.patch` | libc-semantics | Large time/mktime/timegm/strftime normalization and GNU formatter patch driven by Coreutils behavior. | Split into independently proven libc semantics, compare against glibc for negative time, DST, `mktime`, `timegm`, `%s`, flags, widths, signed years, timezone formats, and run unmodified Coreutils tests. |
| `0030-options-getopt-accept-leading-dash-required-args.patch` | libc-semantics | Lets required short-option arguments begin with `-`. | Prove GNU getopt behavior directly; ensure package failures are not due to wrong argv/env/test harness input. |
| `0031-ansi-strtofp-parse-infinity-prefix.patch` | libc-semantics | Parses `inf`/`infinity` prefixes case-insensitively. | Prove C/POSIX/glibc behavior for `strtod`/wide variants and package expectations. |

**Immediate high-risk backlog:**

1. Remove test mutations from the upstream mlibc conformance patch stack or split them into Orlix-owned regression tests: `0009`, test hunks in `0011`, `0012`, `0013`, and `0021`.
2. Re-prove locale before touching `0016` or `0021`: the focused OrlixTestRunner run showed `posix/locale` failing before the narrower wide-conversion failures.
3. Re-prove stdio/fd redirection before keeping `0024`.
4. Replace Perl configure-probe patches with target/schema/toolchain-derived inputs where possible.
5. Review hardcoded hosted gate/page/user-base constants and route them through target/project metadata.

**Current status:**

No runtime patch files were changed in this audit pass.

**Next:**

Create focused cleanup tasks for:

- exact-upstream mlibc conformance source purity;
- OrlixOS package toolchain metadata replacing Perl probe patches;
- locale root-cause proof using the OrlixTestRunner path;
- stdio redirection proof against Linux reference behavior.

### 2026-06-07 Verification

**What happened:**

Verified the audit and harness changes without modifying runtime patch files.

**Evidence:**

- `rtk git diff --check -- docs/plans/active/patch-burden-audit docs/harness .agents/skills`
- `rtk git diff --name-only -- OrlixOS/Sources/patches OrlixMLibC/Sources/patches`
- `rtk rg -n "[[:blank:]]$" docs/plans/active/patch-burden-audit docs/harness .agents/skills/orlix-implementation-boundaries/SKILL.md .agents/skills/orlix-upstream-conformance/SKILL.md`
- `rtk rg -n "OrlixKit|modify upstream tests|durable patch stacks|Patch Burden|test-impurity|workaround-suspect" docs/plans/active/patch-burden-audit docs/harness .agents/skills`
- `rtk rg --files OrlixOS/Sources/patches OrlixMLibC/Sources/patches`
- `rtk git diff --check -- .agents/skills/orlix-implementation-boundaries/SKILL.md .agents/skills/orlix-upstream-conformance/SKILL.md docs/harness/MEMORY.md docs/harness/README.md`

**Result:**

- Whitespace checks passed.
- No files under `OrlixOS/Sources/patches` or `OrlixMLibC/Sources/patches` were modified.
- Patch inventory still shows 34 patch files.
- Stale-reference scan found only explicitly forbidden/retired `OrlixKit` mentions in harness guidance.
- Reviewer delegation did not return findings within the gate window; keep this active plan open until a later reviewer pass or patch cleanup work completes.

---

## Deviations Summary

| Deviation | Reason | Plan updated? |
|---|---|---|
| None yet. | N/A | N/A |

## Open Questions

- [ ] Which patches already have upstream PRs or local reference proof outside this repo?

## Resolved Questions

| Question | Answer | Date |
|---|---|---|
| Are all durable patches acceptable because they make tests pass? | No. Every patch needs a root-cause chain, and upstream test mutations cannot count as exact upstream conformance proof. | 2026-06-07 |
