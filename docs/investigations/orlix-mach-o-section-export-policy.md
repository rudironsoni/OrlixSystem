# Orlix Mach-O Section And Export Policy Investigation

## Status

Investigation only. This is not accepted product section policy.

The current product archive remains limited to `arch/orlix/boot/boot.c`. No upstream Linux source expansion is accepted until the section and export metadata policy is specified and guarded by lint.

## Why `lib/cmdline.c` Is Blocked

`lib/cmdline.c` is the upstream Linux provider for the `_get_option` blocker reported by the `start_kernel` Mach-O link probe. It is source-owned by Linux and is not a libc, host, or OrlixMLibC provider.

The source itself is not the problem. The blocker is that compiling it as a product Mach-O object reaches Linux section and export metadata forms that are not yet represented for `arch/orlix`.

The observed compile failures came from these classes:

- `__init`: declarations such as `init_rootfs()` and `parse_early_param()` expand through Linux `__section(".init.text")`, which is not valid Mach-O section spelling.
- `__initdata`: declarations such as `boot_command_line[]` expand through Linux `__section(".init.data")`, which is not valid Mach-O section spelling.
- `EXPORT_SYMBOL()` metadata: `EXPORT_SYMBOL(get_option)` and related exports use `__ADDRESSABLE()` and Linux discard/export metadata sections such as `.discard.addressable` and `.export_symbol`, which are ELF-shaped and unresolved for product Mach-O objects.

Adding `lib/cmdline.c` without resolving those policies would smuggle section policy into a blocker-driven source change.

## Rejected Shortcuts

### Generic `__section()` Mapping

Mapping every Linux `__section(section)` to a Mach-O section such as `__ORLIX,<section>` is not accepted.

That mapping is too broad because Linux section names carry more than object-format spelling. They can imply ordering, lifetime, start/stop boundaries, discard behavior, per-CPU relocation, read-only-after-init behavior, exception table lookup, and module or symbol metadata ownership.

The fact that the current product object set does not emit `__ORLIX` sections does not prove the mapping. It only proves the current object set is too small to exercise the policy.

### `__DISABLE_EXPORTS`

Defining `__DISABLE_EXPORTS` in the product compile lane is not accepted as normal policy.

It may be useful in a temporary probe for a `CONFIG_MODULES=n` slice, but it suppresses real Linux export metadata handling. The export and discard sections must remain tracked as section/linker blockers, not treated as solved.

## Section Classes To Support

The section policy must explicitly cover at least these classes before upstream source expansion depends on them:

- `__init` / `.init.text`: needs Mach-O spelling, ordering, linker boundaries, and lifetime policy.
- `__initdata` / `.init.data`: needs Mach-O spelling, linker boundaries, and lifetime policy.
- initcall sections: need deterministic ordering and start/stop discovery equivalent to Linux initcall traversal.
- exitcall sections: must either be represented or proven unreachable for the selected profile.
- per-CPU sections: need Mach-O representation plus `arch/orlix` allocation and relocation policy.
- `__ro_after_init`: needs read-only transition policy or a documented blocker.
- exception tables: need preserved sections and lookup boundaries before fault-table users are enabled.
- discard sections: need explicit policy for what can be dropped and when.
- export metadata: needs a Mach-O representation or a documented no-modules omission with replacement plan.
- linker-defined start/stop symbols: need a Mach-O convention or generated symbol table.

## Reachability Today

Current product lane:

- Product Linux source manifest: `arch/orlix/boot/boot.c` only.
- Current product objects do not require accepted `__ORLIX` sections.
- Product framework does not contain `_start_kernel`.

Current probes:

- `init/main.c` compiles only in the `start_kernel` dependency probe with probe-only compatibility glue.
- `lib/cmdline.c` exposes the next real upstream dependency, `_simple_strtoull`, whose normal upstream provider is `lib/vsprintf.c`.
- `lib/cmdline.c` is blocked for product use until `__init`, `__initdata`, and export metadata policy exist.

## Future Blockers

These remain blockers for product-lane source expansion:

- a narrow `arch/orlix` section spelling policy with an allowlist, not a global silent rewrite
- export metadata policy for `CONFIG_MODULES=n` and any future module-capable profile
- lint that rejects unallowlisted `__ORLIX` product sections
- lint that rejects Mach-O policy leakage into installed UAPI headers
- lint that rejects host, Apple SDK, libc, OrlixMLibC, musl, glibc, and Homebrew headers in Linux object dependencies
- lint that rejects product source additions without a named blocker or reason
- symbol lint that rejects fake `start_kernel` providers and keeps `_start_kernel` absent until the real upstream product link exists

## Required Lint Gates

Before section policy can move into the product lane, these gates must exist and pass:

- Header boundary lint over Linux object dependency files.
- UAPI boundary lint over installed OrlixMLibC kernel headers.
- XcodeGen boundary lint to keep Xcode from compiling Linux-owned sources.
- Mach-O section lint over product objects with an explicit section allowlist.
- Symbol lint for `_start_kernel` and fake provider detection.
- Linux source manifest lint requiring a named blocker or reason for every product Linux source.

Until those gates protect the product lane, section mapping and export suppression belong only in probes and documentation.
