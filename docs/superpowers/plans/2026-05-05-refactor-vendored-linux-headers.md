# Refactor Vendored Linux Header Generation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

## Product North Star (Non-Negotiable)

- IXLandSystem is a Linux-shaped kernel/runtime substrate hosted inside an iOS app sandbox.
- Public contracts are Linux-shaped; iOS is private host environment only.
- Linux semantics live in `fs/`, `kernel/`, `runtime/`, `include/`.
- Host mechanics live only in `internal/ios/**`, behind narrow subsystem-owned seams.
- Do not treat Darwin behavior as Linux truth; do not invent Linux-looking headers/constants/types.
- Linux header truth comes only from vendored generated Linux headers:
  - tuple root: `third_party/linux/<version>/<arch>/`
  - surfaces: `uapi/include`, `srctree`, `objtree`

**Goal:** Refactor `make vendor-linux-headers` to generate a Linux-shaped tuple under `third_party/linux/<version>/<arch>/` with only upstream Linux material (`uapi/`, `srctree/`, `objtree/`), removing any repo-authored Linux-looking header side-trees, and moving host-build compatibility headers out of `scripts/`.

**Architecture:** Keep the Makefile-driven vendoring flow (curl/tar + Linux `make` in a temp objtree) and copy only three Linux header surfaces into a tuple root. Host-build compatibility headers live under `build_support/` and are used only for building Linux host tools during vendoring.

**Tech Stack:** GNU Make, bash recipes, Xcodegen (`project.yml`), Xcode build settings, repo lints (`scripts/lint_linux_surface.sh`, `scripts/lint_linux_vendor_headers.sh`).

---

## File/Path Map (What Changes Where)

**Modify**
- `Makefile` (rewrite `vendor-linux-headers` staging/layout/metadata/validation/final replacement)
- `project.yml` (new tuple-root variables; remove legacy variables and include flags that reference non-tuple roots)
- `scripts/lint_linux_vendor_headers.sh` (validate new tuple layout; forbid legacy category roots and extra side-trees)
- `scripts/lint_linux_surface.sh` (remove references to old header roots; keep discipline)
- `AGENTS.md` (clarify `uapi/srctree/objtree` as the only concepts)

**Move**
- `scripts/linux_host_compat/{elf.h,endian.h,byteswap.h,linux_arm_elf_compat.h}`
  -> `build_support/linux_host_compat/include/`

**Delete (repo cleanup)**
- Any legacy category-root layouts that existed before the tuple root (keep only `third_party/linux/<version>/<arch>/{uapi,srctree,objtree}`).
- Any extra vendored header side-trees that are not upstream Linux material.

**Generated (by Makefile)**
- `third_party/linux/<version>/<arch>/{README.md,source.json,manifest.sha256}`
- `third_party/linux/<version>/<arch>/{uapi,srctree,objtree}/...`

---

### Task 1: Move Host Tool Compat Headers Out Of `scripts/`

**Files:**
- Move: `scripts/linux_host_compat/*.h` -> `build_support/linux_host_compat/include/*.h`
- Modify: `Makefile` (copy from the new location)
- Modify: any README/comments referencing `scripts/linux_host_compat`

- [ ] **Step 1: Move files**

Run:
```bash
git mv scripts/linux_host_compat build_support/linux_host_compat/include
```

- [ ] **Step 2: Update Makefile copy points**

Replace the `cp "$repo_root/scripts/linux_host_compat/...` lines with:
```bash
cp "$repo_root/build_support/linux_host_compat/include/elf.h" "$host_compat_include/elf.h"
cp "$repo_root/build_support/linux_host_compat/include/endian.h" "$host_compat_include/endian.h"
cp "$repo_root/build_support/linux_host_compat/include/byteswap.h" "$host_compat_include/byteswap.h"
cp "$repo_root/build_support/linux_host_compat/include/linux_arm_elf_compat.h" "$host_compat_include/linux_arm_elf_compat.h"
```

- [ ] **Step 3: Ensure `scripts/linux_host_compat` is gone (or empty)**

Run:
```bash
test ! -d scripts/linux_host_compat
```

---

### Task 2: Refactor `Makefile:vendor-linux-headers` to Tuple Layout Only

**Files:**
- Modify: `Makefile`

- [ ] **Step 1: Remove all legacy category-root staging**

Replace any category-root staging layouts with tuple-root staging:

With tuple-root staging:
```bash
tuple_stage="$stage_vendor_root/$linux_version/$linux_arch"
uapi_dest="$tuple_stage/uapi/include"
srctree_root="$tuple_stage/srctree"
objtree_root="$tuple_stage/objtree"
```

- [ ] **Step 2: Copy mapping (no flattening)**

Implement:
```bash
copy_tree "$uapi_out/include" "$uapi_dest"
copy_tree "$src/include" "$srctree_root/include"
copy_tree "$src/arch/$linux_arch/include" "$srctree_root/arch/$linux_arch/include"
copy_tree "$obj/include/generated" "$objtree_root/include/generated"
copy_tree "$obj/include/config" "$objtree_root/include/config"
copy_tree "$obj/arch/$linux_arch/include/generated" "$objtree_root/arch/$linux_arch/include/generated"
```

- [ ] **Step 3: Remove forbidden fake header-generation machinery**

Delete from Makefile entirely any code that writes repo-authored Linux-looking headers or generates non-upstream header side-trees.

No replacement mechanism is allowed.

- [ ] **Step 4: Generate tuple metadata**

Ensure `write_source_json` writes:
```json
{
  "linux_version": "...",
  "linux_arch": "...",
  "kernel_series": "...",
  "tarball_url": "...",
  "tarball_sha256": "...",
  "generated_by": "Makefile:vendor-linux-headers",
  "make_targets": ["defconfig","olddefconfig","prepare","modules_prepare","headers_install"]
}
```

Write `README.md` into tuple root and `manifest.sha256` with `LC_ALL=C` sorting and paths relative to tuple root.

- [ ] **Step 5: Validation now validates the tuple root**

Implement required checks against:
- `"$tuple_stage/uapi/include/linux/wait.h"`
- `"$tuple_stage/uapi/include/asm/signal.h"`
- `"$tuple_stage/uapi/include/asm-generic/errno-base.h"`
- `"$tuple_stage/uapi/include/linux/futex.h"`
- `"$tuple_stage/uapi/include/linux/seccomp.h"`
- `"$tuple_stage/srctree/include/linux/fs.h"`
- `"$tuple_stage/srctree/include/linux/sched.h"`
- `"$tuple_stage/srctree/arch/$linux_arch/include"`
- `"$tuple_stage/objtree/include/generated/autoconf.h"`
- `"$tuple_stage/objtree/include/generated/utsrelease.h"`
- `"$tuple_stage/objtree/include/config"`
- `"$tuple_stage/objtree/arch/$linux_arch/include/generated"`

Also fail if tuple has no files at all.

- [ ] **Step 6: Final replacement replaces only the selected tuple root**

Implement:
```bash
final_tuple_root="$final_vendor_root/$linux_version/$linux_arch"
rm -rf "$final_tuple_root"
mkdir -p "$(dirname "$final_tuple_root")"
rsync -a --delete "$tuple_stage/" "$final_tuple_root/"
```

- [ ] **Step 7: Final printed output**

Print exactly:
```text
vendored Linux tuple:
  third_party/linux/<version>/<arch>
surfaces:
  uapi/include
  srctree
  objtree
```

---

### Task 3: Update `project.yml` to Tuple Variables and Remove Legacy Flags

**Files:**
- Modify: `project.yml`

- [ ] **Step 1: Replace old category roots with tuple-root vars**

Add:
```make
LINUX_VERSION: '6.12'
LINUX_ARCH: arm64
LINUX_VENDOR_ROOT: $(SRCROOT)/third_party/linux
LINUX_ROOT: $(LINUX_VENDOR_ROOT)/$(LINUX_VERSION)/$(LINUX_ARCH)
LINUX_UAPI_ROOT: $(LINUX_ROOT)/uapi
LINUX_UAPI_INCLUDE_ROOT: $(LINUX_UAPI_ROOT)/include
LINUX_SRCTREE_ROOT: $(LINUX_ROOT)/srctree
LINUX_OBJTREE_ROOT: $(LINUX_ROOT)/objtree
```

 - [ ] **Step 2: Remove legacy variables**

Remove legacy variables that reference non-tuple vendor roots or per-surface version/arch splits.

- [ ] **Step 3: Include flags discipline**

Ensure UAPI-only sources use `-I$(LINUX_UAPI_INCLUDE_ROOT)`.

If any source truly needs internal kernel headers, add per-file flags only:
- `-I$(LINUX_OBJTREE_ROOT)/arch/$(LINUX_ARCH)/include/generated`
- `-I$(LINUX_SRCTREE_ROOT)/arch/$(LINUX_ARCH)/include`
- `-I$(LINUX_OBJTREE_ROOT)/include`
- `-I$(LINUX_SRCTREE_ROOT)/include`

Do not add srctree/objtree include paths globally unless the whole target needs them.

---

### Task 4: Update Lints for New Layout (No Weakening)

**Files:**
- Modify: `scripts/lint_linux_vendor_headers.sh`
- Modify: `scripts/lint_linux_surface.sh` (only to match new paths; do not weaken checks)

- [ ] **Step 1: Vendor headers lint**

Update to require:
- `third_party/linux/<version>/<arch>/{uapi,srctree,objtree}`
and fail on any legacy category-root layouts or extra side-trees under `third_party/linux/`.

- [ ] **Step 2: Surface lint**

Replace references to old include roots/variables; keep Darwin/iOS leakage checks intact.

---

### Task 5: Repo Cleanup + Include Fixups

**Files:**
- Delete any legacy category-root vendored header layouts (keep tuple-root only).
- Modify any product/test sources that included headers via legacy non-tuple include roots.

- [ ] **Step 1: Search for old vocabulary**

Run:
```bash
rg -n "third_party/linux/|scripts/linux_host_compat|LINUX_"
```

- [ ] **Step 2: Replace includes**

If any file included repo-authored Linux-looking headers from a now-deleted legacy include root:
- Prefer including the real upstream header reachable via `srctree`/`objtree` include paths, or
- Move IXLandSystem-specific declarations into a project-owned header outside `third_party/linux`.

No hand-copied constants, no fake Linux-looking headers.

---

### Task 6: Generate Tuple + Proof (Required Before Commit)

- [ ] **Step 1: Generate Vendored Tuple**

Run:
```bash
make vendor-linux-headers LINUX_VERSION=6.12 LINUX_ARCH=arm64
```

- [ ] **Step 2: Lint**

Run:
```bash
bash ./scripts/lint_linux_surface.sh
```

- [ ] **Step 3: Project generation**

Run:
```bash
xcodegen generate --project .
```

- [ ] **Step 4: iOS Simulator build-for-testing**

Run:
```bash
xcodebuild build-for-testing -project IXLandSystem.xcodeproj -scheme IXLandSystem-6.12-arm64 -sdk iphonesimulator -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17'
```

If the simulator name is not available, list simulators and use the closest iPhone simulator.

---

### Task 7: Commit + Push

- [ ] **Step 1: Commit**

Run:
```bash
git add -A
git commit -m "Refactor vendored Linux header generation"
```

- [ ] **Step 2: Push**

Run:
```bash
git push
```

---

## Self-Review Checklist (Plan Coverage)

- [ ] Tuple layout only under `third_party/linux/<version>/<arch>/` with `uapi/srctree/objtree`.
- [ ] No legacy category roots or extra vendored header side-trees exist; tuple-root only.
- [ ] Host compat headers relocated to `build_support/...` and used only during vendoring.
- [ ] `source.json`, `README.md`, `manifest.sha256` generated and validated.
- [ ] `project.yml` updated: no legacy variables or include flags referencing non-tuple roots; no global srctree/objtree unless required.
- [ ] Lints updated without weakening; they enforce the new truth.
- [ ] Proof commands run and succeed before commit.
