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

### 2026-06-07 Upstream Test Purity Cleanup And Locale Root Cause

**What happened:**

Removed upstream-test mutations from the durable mlibc conformance patch stack:

- deleted `OrlixMLibC/Sources/patches/0009-posix-test-wait3-rusage.patch`;
- removed the `tests/posix/access.c` hunk from `0011-posix-add-euidaccess-eaccess.patch`;
- removed the `tests/meson.build` and `tests/posix/renameat2.c` hunks from `0012-posix-renameat2-zero-flags-via-renameat.patch`;
- removed the `tests/posix/string.c` hunk from `0013-ansi-string-keep-posix-strerror-r-as-public-name.patch`;
- removed the `tests/ansi/locale.c` hunk from `0021-ansi-mb-cur-max-follows-active-locale.patch`;
- removed `posix/renameat2` and `posix/wait3` from the OrlixMLibC test list because those entries existed only through removed Orlix-added test files, not pinned upstream mlibc tests.

**Root cause found:**

The first full run after test-purity cleanup failed at unmodified upstream `ansi/sscanf`: the test set `de_DE.utf8`, then `%[α-ω]` produced stale/incorrect narrow output and aborted.

The owning bug was in Orlix's durable mlibc locale patch, not in generated tests or a kernel workaround:

- `0016-locale-c-wide-conversion-uses-ascii.patch` added `active_codeset_is_utf8()` and made non-UTF-8 locales use ASCII conversion.
- The mlibc locale archive parser's `parse_string()` returns a string view including the trailing NUL.
- The parser assignment path asserts parsed string entries end with `\0`.
- The LC_CTYPE parser maps `codeset_name` through `parse_string`.
- Therefore a valid locale archive `UTF-8` codeset is represented as `UTF-8\0`; checking `codeset.size() == 5` misclassified UTF-8 locales as non-UTF-8.

**Change made:**

Adjusted `0016-locale-c-wide-conversion-uses-ascii.patch` so `active_codeset_is_utf8()` ignores one trailing NUL when comparing the logical codeset value.

**Evidence:**

- `rtk rg -n "tests/|diff --git .*tests|\\+\\+\\+ [bw]/tests|\\+\\+\\+ b/tests" OrlixMLibC/Sources/patches OrlixOS/Sources/patches`
  - no matches after cleanup.
- `rtk timeout 1200 make -f OrlixMLibC/Makefile __apply-mlibc-patches PROFILE=release`
  - passed; known pre-existing whitespace warnings remain in `0020-frigg-format-long-double-with-long-double-arithmetic.patch`.
- `rtk git -C Build/OrlixMLibC/src/mlibc-43ab07732cdf diff --name-only -- tests`
  - no output; generated upstream mlibc tests are pristine after patch application.
- `rtk timeout 2400 make -f OrlixMLibC/Makefile __test-build PROFILE=release MLIBC_TEST_CASES=ansi/sscanf`
  - passed.
- `rtk timeout 2400 make -f OrlixMLibC/Makefile __test-initramfs PROFILE=release MLIBC_TEST_CASES=ansi/sscanf`
  - passed.
- `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixMLibCUpstreamTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -resultBundlePath .deriveddata/OrlixSystem-sim/MLibCSScanfFocused-20260607-2000.xcresult test`
  - passed.
- `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/MLibCSScanfFocused-20260607-2000.xcresult --compact`
  - `result: Passed`, `totalTestCount: 1`, `passedTests: 1`, `failedTests: 0`, `skippedTests: 0`.
- `rtk timeout 2400 make -f OrlixMLibC/Makefile __test-build PROFILE=release`
  - passed; full 161 upstream-test list restored.
- `rtk timeout 2400 make -f OrlixMLibC/Makefile __test-initramfs PROFILE=release`
  - passed.
- `rtk wc -l Build/OrlixMLibC/tests-install/release/mlibc-test-list.txt`
  - `161`.
- `rtk rg -n "posix/(wait3|renameat2)|ansi/sscanf|ansi/locale|ansi/snprintf|ansi/utf8|posix/access|posix/string" Build/OrlixMLibC/tests-install/release/mlibc-test-list.txt`
  - `ansi/sscanf`, `ansi/snprintf`, `ansi/utf8`, `ansi/locale`, `posix/access`, and `posix/string` are present.
  - `posix/wait3` and `posix/renameat2` are absent because they are not pinned upstream tests.
- `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixMLibCUpstreamTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -resultBundlePath .deriveddata/OrlixSystem-sim/MLibCFull-20260607-2005.xcresult test`
  - failed as expected on the next unmodified upstream failure, not on the fixed locale path.
- `rtk rg -n "ORLIX-MLIBC-TEST-INIT|TAP version|1\\.\\.162|ok [0-9]+|not ok|assert|failed|killed by signal|upstream failure marker|ORLIX-MLIBC-TEST-DONE" ~/Library/Application\\ Support/rtk/tee/1780855387_xcodebuild_-project_OrlixSystem_xcodepro.log`
  - `1..162`
  - `ok 4 - ansi/sscanf`
  - `ok 6 - ansi/snprintf`
  - `ok 7 - ansi/utf8`
  - `ok 37 - ansi/locale`
  - first remaining failure: `not ok 42 - ansi/ungetwc`, killed by signal 6.
- `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/MLibCFull-20260607-2005.xcresult --compact`
  - `result: Failed`, failure text: `upstream failure marker found: not ok 42 - ansi/ungetwc`.

**Next:**

Investigate `ansi/ungetwc` with the same rule: prove the owning layer before changing any durable patch, and do not alter generated upstream tests.

### 2026-06-08 Clean-Slate Patch Removal And Hosted TLS Root Cause

**What happened:**

Removed the durable OrlixOS and OrlixMLibC patch burden from the active source patch stacks and reran upstream mlibc tests from regenerated upstream sources.

The clean-slate full upstream mlibc run failed first on unmodified upstream tests:

- `ansi/fenv`, killed by signal 11;
- `ansi/longjmp`, killed by signal 11;
- `posix/pthread_attr`, killed by signal 6;
- `posix/pthread_barrier`, later in the same cluster.

**Root cause found for the signal-11 failures:**

Upstream mlibc's Linux AArch64 sysdeps write the Linux user TLS register directly with `msr TPIDR_EL0, Xt`. Upstream Linux owns save/restore of the user-visible `TPIDR_EL0` register on arm64. In Orlix's Darwin-hosted execution mode, a raw userspace write to Darwin's real `TPIDR_EL0` is not a valid Linux surface because it conflicts with host TLS. The owning fix is therefore in the hosted Linux execution boundary, not in mlibc.

**Change made:**

Added hosted Linux emulation for user-visible AArch64 TLS writes:

- executable user pages translated by `OrlixHostAdapter` now rewrite `msr TPIDR_EL0, Xt` into an Orlix private instruction trap while preserving the source register in the instruction encoding;
- the host trap handler recognizes the private TLS-write trap on simulator `SIGTRAP` or `SIGILL`, reads the source register from the Darwin machine context, and passes the Linux-visible TLS value through the existing user trap frame;
- `OrlixKernel` handles `ORLIX_HOST_USER_TRAP_TLS_WRITE` by preserving the captured Linux user TLS in the hosted task state, advancing the user PC, and resuming the current Linux task;
- the kernel-owned syscall gate is now mapped through a trusted executable mapping so its internal TLS restore trampoline is not translated as if it were upstream userspace code.

No mlibc source patch or upstream-test mutation was added.

**Evidence:**

- `rtk rg --files OrlixOS/Sources/patches OrlixMLibC/Sources/patches`
  - no files found; both durable patch stacks are empty.
- `rtk timeout 2400 make -f OrlixMLibC/Makefile test PROFILE=release`
  - failed from regenerated upstream mlibc sources at the original clean-slate cluster, starting with `ansi/fenv` and `ansi/longjmp` signal-11 crashes.
- `rtk timeout 900 make -f OrlixMLibC/Makefile test PROFILE=release MLIBC_TEST_CASES=ansi/fenv MLIBC_TEST_RUN_WAIT_TICKS=300`
  - passed after the kernel/host TLS fix.
  - runtime log contained `ORLIX-MLIBC-TEST-INIT`, `ok 2 - ansi/fenv`, and `ORLIX-MLIBC-TEST-END`.
  - no checked fatal markers: `not ok`, hosted user trap, user fault, panic, Oops, OOM, killed by signal, malformed output, or temporary host diagnostics.
- `rtk timeout 900 make -f OrlixMLibC/Makefile test PROFILE=release MLIBC_TEST_CASES=ansi/longjmp MLIBC_TEST_RUN_WAIT_TICKS=300`
  - passed after the kernel/host TLS fix.
  - runtime log contained `ORLIX-MLIBC-TEST-INIT`, `ok 2 - ansi/longjmp`, and `ORLIX-MLIBC-TEST-END`.
- `rtk timeout 900 make -f OrlixMLibC/Makefile test PROFILE=release MLIBC_TEST_CASES=posix/pthread_attr MLIBC_TEST_RUN_WAIT_TICKS=300`
  - still fails as an unmodified upstream abort: `# posix/pthread_attr killed by signal 6`, `not ok 2 - posix/pthread_attr`.
  - this is not part of the fixed TLS signal-11 root cause and remains the next Linux-surface investigation target.

**Next:**

Investigate `posix/pthread_attr` from first principles against upstream Linux pthread/futex/clone/TLS expectations. Do not patch mlibc or upstream tests.

### 2026-06-07 Wide Pushback Root Cause And Fix

**What happened:**

Root-caused the next full-run failure at unmodified upstream `ansi/ungetwc`.

The failing assertion was in `tests/ansi/ungetwc.c` at the Greek alpha wide-character pushback check. The test does not call `setlocale()`, so the stream is in the default C locale. After the corrected `0016` locale patch, C-locale `wcrtomb()` correctly rejects non-ASCII wide characters. mlibc's existing `ungetwc_unlocked()` encoded the pushed wide character through `wcrtomb()` and then pushed bytes into the byte `ungetc` buffer; therefore that `ungetwc()` call returned `WEOF`.

That was an mlibc stdio wide-pushback bug, not a kernel/filesystem issue and not an upstream-test issue. Linux/glibc keeps wide pushback as wide characters in a wide backup area instead of requiring the pushed character to be representable in the stream's current external byte encoding.

**Change made:**

Added `OrlixMLibC/Sources/patches/0032-ansi-stdio-use-wide-unget-buffer.patch`.

The patch:

- moves mlibc's existing internal `ungetBufferSize` constant next to the internal `abstract_file` declaration so byte and wide pushback share the same implementation limit;
- adds an internal wide pushback buffer to `abstract_file`;
- makes `ungetwc_unlocked()` push `wchar_t` values directly instead of encoding them with `wcrtomb()`;
- makes `fgetwc_unlocked()` consume wide pushback before checking EOF or reading/decoding bytes;
- clears wide pushback through `purge()` and `_reset()` so seek/rewind/freopen-style reset paths discard it.

**Evidence:**

- `rtk timeout 1200 make -f OrlixMLibC/Makefile __apply-mlibc-patches PROFILE=release`
  - passed; known pre-existing whitespace warnings remain in `0020-frigg-format-long-double-with-long-double-arithmetic.patch`.
- `rtk git -C Build/OrlixMLibC/src/mlibc-43ab07732cdf diff --name-only -- tests`
  - no output; generated upstream mlibc tests remained pristine.
- `rtk timeout 2400 make -f OrlixMLibC/Makefile __test-build PROFILE=release MLIBC_TEST_CASES=ansi/ungetwc`
  - passed.
- `rtk timeout 2400 make -f OrlixMLibC/Makefile __test-initramfs PROFILE=release MLIBC_TEST_CASES=ansi/ungetwc`
  - passed.
- `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixMLibCUpstreamTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -resultBundlePath .deriveddata/OrlixSystem-sim/MLibCUngetwcFocused-20260607-2115.xcresult test`
  - passed.
- `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/MLibCUngetwcFocused-20260607-2115.xcresult --compact`
  - `result: Passed`, `totalTestCount: 1`, `passedTests: 1`, `failedTests: 0`, `skippedTests: 0`.
- `rtk timeout 2400 make -f OrlixMLibC/Makefile __test-build PROFILE=release`
  - passed; full upstream list restored.
- `rtk timeout 2400 make -f OrlixMLibC/Makefile __test-initramfs PROFILE=release`
  - passed.
- `rtk wc -l Build/OrlixMLibC/tests-install/release/mlibc-test-list.txt`
  - `161`.
- `rtk rg -n "ansi/(ungetwc|sscanf|locale|snprintf|utf8)|posix/(access|string|wait3|renameat2)" Build/OrlixMLibC/tests-install/release/mlibc-test-list.txt`
  - `ansi/ungetwc`, `ansi/sscanf`, `ansi/locale`, `ansi/snprintf`, `ansi/utf8`, `posix/access`, and `posix/string` are present.
  - `posix/wait3` and `posix/renameat2` are absent because they are not pinned upstream tests.
- `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixMLibCUpstreamTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -resultBundlePath .deriveddata/OrlixSystem-sim/MLibCFull-20260607-2125.xcresult test`
  - failed on the next unmodified upstream failure, not on `ansi/ungetwc`.
- `rtk rg -n "ORLIX-MLIBC-TEST-INIT|TAP version|1\\.\\.162|ok [0-9]+|not ok|assert|failed|killed by signal|upstream failure marker|ORLIX-MLIBC-TEST-DONE|panic|OOM|Out of memory" ~/Library/Application\\ Support/rtk/tee/1780856574_xcodebuild_-project_OrlixSystem_xcodepro.log`
  - `1..162`
  - `ok 42 - ansi/ungetwc`
  - first remaining failure marker: `not ok 89 - posix/getservbyname`
  - paired failure marker: `not ok 90 - posix/getservbyport`
- `rtk xcrun xcresulttool get test-results summary --path .deriveddata/OrlixSystem-sim/MLibCFull-20260607-2125.xcresult --compact`
  - `result: Failed`, failure text: `upstream failure marker found: not ok 89 - posix/getservbyname`.

**Next:**

Investigate `posix/getservbyname` and `posix/getservbyport` from first principles. The likely ownership area is still OrlixMLibC or OrlixOS rootfs data (`/etc/services`), but do not patch mlibc or package data until proving whether the Linux rootfs service database and libc parser behavior match upstream Linux/glibc expectations.

### 2026-06-07 Zero-Patch Clean Slate And Linux SVC Runtime Fix

**What happened:**

Removed all durable patch files under:

- `OrlixMLibC/Sources/patches`
- `OrlixOS/Sources/patches`

Removed OrlixOS package-source patch application hooks and stale mlibc patch-only build inputs. The build now starts from pristine upstream mlibc and pristine upstream package sources for this checkpoint.

**Root causes found:**

1. Pristine upstream mlibc's AArch64 Linux syscall path uses normal `svc #0`.
2. The iOS-hosted execution path cannot let guest userspace execute `svc #0` directly because that traps as a Darwin host syscall, not an Orlix Linux syscall.
3. The old mlibc hosted-syscall patch hid that missing kernel/hosted execution surface by rewriting libc's syscall path.
4. After removing `-femulated-tls`, pristine mlibc no longer needed the old patched `__emutls_get_address` support for the focused build path.
5. The kernel test-payload path was stale when `ORLIX_KERNEL_TEST_INITRAMFS_INPUT` changed: `run` did not force `__kernel-payload`, and the OrlixOS embed script treated intentionally empty target-provided base/state inputs as unset.
6. Boot profile selection for test initramfs payloads must follow the same Linux-shaped boot input path as product payloads. Scanning generated DTBs for a profile whose command line happened to contain `rdinit=/init` was a harness shortcut, not target metadata.

**Changes made:**

- Added a private hosted-trap classification for Linux userspace syscall traps.
- Made executable guest-user host mappings private copies and translated `svc #0` instructions to a private host breakpoint encoding in the host mirror only. The Linux guest page and upstream binary contents remain unchanged.
- Disabled the host mapping cache fast path for executable user mappings, because executable mappings are now private translated copies and must be recopied when the kernel asks to sync them.
- Routed that private host trap back into `OrlixKernel` as a Linux syscall using AArch64 Linux register conventions.
- Fixed the existing hosted syscall-gate path to preserve syscall argument 0 in `orig_x0` before dispatch.
- Routed product and test-initramfs kernel command lines through OrlixOS target/profile metadata:
  - `OrlixOS/Sources/distribution/target-settings.xcconfig` owns release/development command lines and their test-initramfs variants;
  - `OrlixOS/Makefile` passes the selected profile's command line metadata into kernel payload packaging;
  - payload `Info.plist` records `OrlixKernelCommandLine`;
  - `OrlixOS` session boot reads the bundled payload metadata and passes it into the bootloader config;
  - test initramfs payload selection still derives the root mode from `ORLIX_KERNEL_ROOT_MODES`, but no longer scans built DTBs for `rdinit=/init`.
- Preserved target-provided empty OrlixOS expected base/state root inputs instead of falling back to product payload inputs.

**Evidence:**

- `rtk rg --files OrlixOS/Sources/patches OrlixMLibC/Sources/patches`
  - no files found.
- `rtk timeout 1200 make -f OrlixMLibC/Makefile __apply-mlibc-patches PROFILE=release`
  - passed and reported no OrlixMLibC upstream mlibc patches present.
- `rtk timeout 2400 make -f OrlixMLibC/Makefile __test-build PROFILE=release MLIBC_TEST_CASES=ansi/abs`
  - passed.
- `rtk timeout 2400 make -f OrlixMLibC/Makefile __test-initramfs PROFILE=release MLIBC_TEST_CASES=ansi/abs`
  - passed.
- `rtk timeout 2400 make -f OrlixMLibC/Makefile __test-run PROFILE=release MLIBC_TEST_CASES=ansi/abs MLIBC_TEST_RUN_WAIT_TICKS=200`
  - passed and verified `ORLIX-MLIBC-TEST-INIT` and `ORLIX-MLIBC-TEST-END` in the simulator runtime log.
- `rtk timeout 1200 make -f OrlixOS/Makefile kernel-payload PROFILE=release`
  - passed and packaged `Build/OrlixKernel/payload/OrlixKernelPayload.bundle`.
- `rtk plutil -p Build/OrlixKernel/payload/OrlixKernelPayload.bundle/Info.plist | rtk rg "OrlixKernelCommandLine|OrlixSelectedProfile|OrlixSelectedRootMode"`
  - after product payload packaging: `OrlixKernelCommandLine` was `console=ttyS0 console=hvc0 root=/dev/vda rootfstype=ext4 ro orlix.profile=release`, profile was `release`, and root mode was `direct`.
- `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixOSTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim test`
  - passed after adding OrlixOS payload command-line metadata coverage.
- `rtk timeout 2400 make -f OrlixMLibC/Makefile __test-run PROFILE=release MLIBC_TEST_CASES=ansi/abs MLIBC_TEST_RUN_WAIT_TICKS=200`
  - passed with no OrlixMLibC upstream mlibc patches present, verified `ORLIX-MLIBC-TEST-INIT` and `ORLIX-MLIBC-TEST-END`, and produced a test-initramfs payload with command line `console=ttyS0 console=hvc0 rdinit=/init orlix.root=initramfs-only orlix.profile=release`, profile `release`, and root mode `initramfs-only`.
- `rtk timeout 2400 make -f OrlixMLibC/Makefile __test-run PROFILE=release MLIBC_TEST_CASES='ansi/abs ansi/fopen posix/access posix/getcwd posix/vfork posix/waitid' MLIBC_TEST_RUN_WAIT_TICKS=600`
  - passed and verified the same completion markers in `Build/OrlixKernel/run/release/OrlixTerminal-runtime.log`.
- `rtk rg -n "ORLIX-MLIBC|FAIL|panic|Oops|hosted user trap|signal|timeout|Malformed|ERROR" Build/OrlixKernel/run/release/OrlixTerminal-runtime.log`
  - showed `ORLIX-MLIBC-TEST-INIT` and `ORLIX-MLIBC-TEST-END`;
  - no failure, panic, Oops, malformed-output, or hosted-user-trap marker was present;
  - the later signal 15 line was the harness terminating the app after successful marker collection.

**Current status:**

This checkpoint proves the clean-slate mlibc/package patch removal plus the Linux `svc #0` hosted runtime path for a focused upstream mlibc smoke set. It does not prove full upstream mlibc/Coreutils/Linux conformance yet.

**Next:**

Continue from zero patches and expand upstream conformance coverage. Any new failure must be attributed to its owning layer before implementation; do not reintroduce mlibc or package source patches as a shortcut.

### 2026-06-08 OrlixOS Payload Metadata And Hosted PTY mmap Fix

**What happened:**

The OrlixOS product payload could become stale after focused upstream-test
packaging because the Xcode embed phase validated only bundle existence and did
not rebuild when the stamp pointed at a different rootfs input. Separately, the
PTY runtime proof booted the OrlixOS product rootfs and reached `/sbin/init`,
but Bash crashed after the PTY shell started:

- `Orlix: user fault pc=0x6000000cf790 ... addr=0x1018`
- `orlix-init: PTY shell session ended`

**Root causes found:**

1. The embed phase needed to compare the payload stamp against target-provided
   OrlixOS rootfs/base/state metadata before copying it into the framework.
2. `OrlixTestRunner` and the PTY runtime proof were still selecting boot
   profiles with hardcoded enum values instead of consuming the bundled
   OrlixOS payload metadata.
3. Bash's fault PC disassembled to a vectorized allocator `memset` path. The
   destination address `0x1018` showed that anonymous allocation had received a
   low userspace mapping.
4. The owning Linux-surface bug was in `arch/orlix/mm/mmap.c`: the Orlix
   top-down unmapped-area path for hosted `PROT_NONE` anonymous reservations
   kept upstream Linux's `PAGE_SIZE` low limit. That is valid for normal Linux
   low user ranges, but Orlix app-hosted userspace is valid only inside the
   hosted window beginning at `TASK_UNMAPPED_BASE`.

**Changes made:**

- OrlixOS payload embedding now rebuilds the kernel payload when the existing
  stamp is missing or does not match the target metadata for rootfs/base/state
  inputs.
- Bash package build inputs now derive the bootstrap tool path from the local
  Homebrew prefix and set `CC_FOR_BUILD` only for Bash's build-host generator
  binaries; target binaries remain `aarch64-linux-gnu` Orlix Linux ELF files.
- `OrlixTestRunner` and `OrlixPTYRuntimeTests` now derive the boot profile from
  bundled OrlixOS payload metadata.
- `arch/orlix/mm/mmap.c` now uses `TASK_UNMAPPED_BASE` as the low limit for the
  hosted top-down `PROT_NONE` reservation search.
- The Orlix-owned kselftest `mprotect_stack_probe` now receives
  `ORLIX_HOSTED_USER_BASE_ADDRESS` from target metadata and verifies that
  `PROT_NONE` anonymous mmap stays inside the hosted user window.

No mlibc, Coreutils, Bash, generated upstream tree, or upstream test source was
patched.

**Evidence:**

- `rtk git diff --check`
  - passed.
- `rtk rg --files OrlixOS/Sources/patches OrlixMLibC/Sources/patches`
  - no files found; both durable patch stacks are empty.
- `rtk timeout 300 make xcodeproj`
  - passed.
- `rtk timeout 1800 make -f OrlixOS/Makefile kernel-payload PROFILE=release`
  - passed; rebuilt Bash, Coreutils, Findutils, init, the OrlixOS rootfs, and
    `Build/OrlixKernel/payload/OrlixKernelPayload.bundle`.
- `rtk file Build/OrlixOS/packages/release/usr/bin/bash Build/OrlixOS/packages/release/usr/bin/ls Build/OrlixOS/packages/release/usr/bin/find Build/OrlixOS/packages/release/sbin/init`
  - all checked product binaries were `ELF 64-bit LSB executable, ARM aarch64`.
- `rtk timeout 1800 make -f OrlixKernel/Makefile kselftest PROFILE=release`
  - passed; rebuilt and installed the OrlixMLibC-built Orlix kselftests,
    including the new hosted-window mmap regression.
- `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixKernelUpstreamTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixKernelUpstreamTests/OrlixKernelUpstreamTests/testKselftestRootfsCompletesThroughOrlixOSTerminalSession test`
  - passed through the OrlixOS terminal-session path.
- `rtk xcodebuild -project OrlixSystem.xcodeproj -scheme OrlixPTYRuntimeTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath .deriveddata/OrlixSystem-sim -only-testing:OrlixPTYRuntimeTests/OrlixPTYRuntimeTests/testLinuxPTYCarriesInteractiveShellInputAndOutput test`
  - passed; the prior Bash user fault and `orlix-init: PTY shell session ended`
    marker did not recur.

**Next:**

Continue upstream conformance expansion from the zero-patch state. The next
known clean-slate mlibc target remains the unmodified upstream
`posix/pthread_attr` failure, unless a broader suite exposes an earlier
kernel-surface failure.

---

## Deviations Summary

| Deviation | Reason | Plan updated? |
|---|---|---|
| User redirected from audit-first cleanup to zero-patch clean slate. | The required goal changed to removing all OrlixOS and OrlixMLibC patches, then fixing root causes in the owning Orlix layer. | Yes |

## Open Questions

- [ ] Which patches already have upstream PRs or local reference proof outside this repo?

## Resolved Questions

| Question | Answer | Date |
|---|---|---|
| Are all durable patches acceptable because they make tests pass? | No. Every patch needs a root-cause chain, and upstream test mutations cannot count as exact upstream conformance proof. | 2026-06-07 |
