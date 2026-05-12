# Clang-Tidy Lint Cutover Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace source-policy shell lint with a repo-local custom `clang-tidy` module so source lint is enforced by `clang-tidy` only, while repo/vendor topology stays in a narrow non-lint layout script.

**Architecture:** Add an in-repo `clang-tidy` plugin that scans translation units with path-aware Orlix policy checks. `scripts/lint_linux_surface.sh` becomes a pure `clang-tidy` runner that builds the plugin, generates `compile_commands.json`, and runs only the Orlix checks. `scripts/lint_linux_vendor_headers.sh` is replaced by a repo-layout validator for `third_party/linux` and `project.yml` topology, not source lint.

**Tech Stack:** Homebrew LLVM 22.1.4, `clang-tidy`, CMake, XcodeGen, `compile_commands.json`, shell runner scripts.

---

### Task 1: Create the lint tool structure

**Files:**
- Create: `tools/clang_tidy_orlix/CMakeLists.txt`
- Create: `tools/clang_tidy_orlix/OrlixTidyModule.cpp`
- Create: `tools/clang_tidy_orlix/OrlixSourcePolicyCheck.h`
- Create: `tools/clang_tidy_orlix/OrlixSourcePolicyCheck.cpp`
- Create: `tools/clang_tidy_orlix/OrlixTestPolicyCheck.h`
- Create: `tools/clang_tidy_orlix/OrlixTestPolicyCheck.cpp`
- Create: `.clang-tidy`

- [ ] Define the plugin build shape against Homebrew LLVM/Clang CMake exports.
- [ ] Register an `orlix-source-policy` check and an `orlix-test-policy` check inside one custom module.
- [ ] Set `.clang-tidy` to run only the Orlix checks by default.

### Task 2: Move Linux-owner source policy into clang-tidy

**Files:**
- Modify: `tools/clang_tidy_orlix/OrlixSourcePolicyCheck.h`
- Modify: `tools/clang_tidy_orlix/OrlixSourcePolicyCheck.cpp`
- Reference: `scripts/lint_linux_surface.sh`
- Reference: `AGENTS.md`

- [ ] Implement path classification for Linux-owner files under `OrlixKernel/fs`, `OrlixKernel/kernel`, `OrlixKernel/runtime`, and `OrlixKernel/include`.
- [ ] Implement forbidden-include checks for host frameworks, `OrlixHostAdapter/**`, repo-local libc headers, and banned host headers.
- [ ] Implement source-text or AST-backed checks for forbidden host vocabulary (`host_*`, `*_host`, `*_bridge`), forbidden logging calls, local type packs, forbidden Objective-C files in Linux-owner paths, and related wrong-direction source-policy bans currently enforced in shell.

### Task 3: Move test-policy source lint into clang-tidy

**Files:**
- Modify: `tools/clang_tidy_orlix/OrlixTestPolicyCheck.h`
- Modify: `tools/clang_tidy_orlix/OrlixTestPolicyCheck.cpp`
- Reference: `scripts/lint_linux_surface.sh`

- [ ] Implement path classification for `OrlixKernelTests` and `OrlixHostAdapterTests`.
- [ ] Implement source-policy checks for branded helper vocabulary, Linux UAPI includes from Objective-C test files, forbidden host-mediation includes from LinuxKernel tests, and similar source-level test bans that belong in `clang-tidy`.
- [ ] Keep the checks translation-unit-local. Do not turn repo-layout concerns into fake `clang-tidy` source checks.

### Task 4: Replace shell source lint with a pure clang-tidy runner

**Files:**
- Modify: `scripts/lint_linux_surface.sh`
- Modify: `scripts/generate_compile_commands.sh`
- Create: `scripts/build_orlix_clang_tidy_module.sh`

- [ ] Make `scripts/lint_linux_surface.sh` do only:
  - build the plugin
  - generate `compile_commands.json`
  - run `clang-tidy` with `-load` and the Orlix-only checks
- [ ] Remove all source-policy `rg`, `grep`, `find`, and shell pattern checks from `scripts/lint_linux_surface.sh`.
- [ ] Make the compile database generation stable enough for local lint use with the current XcodeGen build graph.

### Task 5: Replace vendor-header lint with repo-layout-only validation

**Files:**
- Delete: `scripts/lint_linux_vendor_headers.sh`
- Create: `scripts/check_repo_layout.sh`
- Modify: `scripts/lint_linux_surface.sh`

- [ ] Move the `third_party/linux/<version>/<arch>` tuple/root checks, required marker-file checks, and `project.yml` ownership/layout checks into `scripts/check_repo_layout.sh`.
- [ ] Ensure `scripts/lint_linux_surface.sh` does not perform those checks directly.
- [ ] If the repo still wants one top-level lint entrypoint, it may invoke `check_repo_layout.sh` as a topology preflight, but the source-policy enforcement itself must stay `clang-tidy` only.

### Task 6: Update policy/docs and verify

**Files:**
- Modify: `AGENTS.md`
- Modify: `docs/SUBSTRATE_CONTRACT.md`
- Modify: `docs/plans/00-Native_Package_Compatibility_Roadmap.md`
- Modify: `docs/plans/01-OrlixKernel_OrlixHostAdapter_Split_Plan.md`
- Modify: `docs/plans/02-Sysroot_Build_Truth_Plan.md`

- [ ] Rewrite repo policy/docs to describe the new split clearly:
  - source lint is custom `clang-tidy`
  - repo/vendor topology is the repo-layout script
- [ ] Verify with:
  - `bash ./scripts/check_repo_layout.sh`
  - `bash ./scripts/lint_linux_surface.sh`
  - `xcodegen generate --project .`
  - `xcodebuild test -project OrlixKernel.xcodeproj -scheme OrlixKernel-6.12-arm64 -configuration Debug -destination 'platform=iOS Simulator,id=63FEBB50-E358-47C6-A8C2-C77E2A391BB2' -derivedDataPath /private/tmp/OrlixDerivedData -only-testing:OrlixKernelTests`
  - `xcodebuild test -project OrlixKernel.xcodeproj -scheme OrlixKernel-6.12-arm64 -configuration Debug -destination 'platform=iOS Simulator,id=63FEBB50-E358-47C6-A8C2-C77E2A391BB2' -derivedDataPath /private/tmp/OrlixDerivedData -only-testing:OrlixHostAdapterTests`
