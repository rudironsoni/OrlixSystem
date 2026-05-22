ORLIX_PRODUCT_ADAPTER_ROOT := $(ORLIX_KERNEL_BUILD_DIR)/orlix-product-compile-adapter
ORLIX_PRODUCT_ADAPTER_INCLUDE := $(ORLIX_PRODUCT_ADAPTER_ROOT)/include
ORLIX_PRODUCT_ADAPTER_CFLAGS := -O2 -fshort-wchar -DPER_CPU_BASE_SECTION=\"__DATA,__percpu\" -I"$(ORLIX_PRODUCT_ADAPTER_INCLUDE)"
ORLIX_PRODUCT_PAYLOAD_OBJECT := $(ORLIX_PRODUCT_ADAPTER_ROOT)/orlix-product-payloads.o
ORLIX_PRODUCT_BOUNDARY_OBJECT := $(ORLIX_PRODUCT_ADAPTER_ROOT)/orlix-product-boundaries.o
ORLIX_PRODUCT_BOUNDARY_OBJECTS := $(ORLIX_PRODUCT_PAYLOAD_OBJECT) $(ORLIX_PRODUCT_BOUNDARY_OBJECT)

ORLIX_PRODUCT_ALLOWED_MACHO_SECTIONS := \
	__TEXT,__text \
	__TEXT,__cstring \
	__TEXT,__const \
	__TEXT,__literal4 \
	__TEXT,__literal8 \
	__TEXT,__literal16 \
	__TEXT,__init_text \
	__TEXT,__init_rodata \
	__TEXT,__ref_text \
	__TEXT,__exit_text \
	__TEXT,__sched_text \
	__TEXT,__noinstr_text \
	__TEXT,__cpuidle_text \
	__TEXT,__irqentry_text \
	__TEXT,__softirq_text \
	__TEXT,__dtb_init \
	__DATA,__data \
	__DATA,__const \
	__DATA,__bss \
	__DATA,__common \
	__DATA,__init_data \
	__DATA,__init_tinfo \
	__DATA,__init_ramfs \
	__DATA,__init_setup \
	__DATA,__exit_data \
	__DATA,__exitcall \
	__DATA,__param \
	__DATA,__data_once \
	__DATA,__ro_after_init \
	__DATA,__sched_stop \
	__DATA,__sched_dl \
	__DATA,__sched_rt \
	__DATA,__sched_fair \
	__DATA,__sched_ext \
	__DATA,__sched_idle \
	__DATA,__ref_data \
	__DATA,__cacheline \
	__DATA,__percpu \
	__DATA,__discard \
	__DATA,__discard_addr \
	__DATA,__export_symbol \
	__DATA,__modinfo \
	__DATA,__modver \
	__DATA,__builtin_fw \
	__DATA,__note \
	__DATA,__rmem_tbl \
	__DATA,__rmem_end \
	__DATA,__orlix_bnd \
	__DATA,__initcall_e \
	__DATA,__initcall0 \
	__DATA,__initcall0s \
	__DATA,__initcall1 \
	__DATA,__initcall1s \
	__DATA,__initcall2 \
	__DATA,__initcall2s \
	__DATA,__initcall3 \
	__DATA,__initcall3s \
	__DATA,__initcall4 \
	__DATA,__initcall4s \
	__DATA,__initcall5 \
	__DATA,__initcall5s \
	__DATA,__initcallrf \
	__DATA,__initcallrfs \
	__DATA,__initcall6 \
	__DATA,__initcall6s \
	__DATA,__initcall7 \
	__DATA,__initcall7s \
	__DATA,__con_initcall

define orlix_product_adapter_flags
$(ORLIX_PRODUCT_ADAPTER_CFLAGS)
endef

define orlix_product_adapter_objects
$(ORLIX_PRODUCT_BOUNDARY_OBJECTS)
endef

define orlix_product_adapter_prepare
adapter_root="$(ORLIX_PRODUCT_ADAPTER_ROOT)"; \
adapter_include="$(ORLIX_PRODUCT_ADAPTER_INCLUDE)"; \
for path in "$(ORLIX_KERNEL_BUILD_DIR)" "$$adapter_root" "$$adapter_include"; do \
	if [ -e "$$path" ] && [ -L "$$path" ]; then echo "refusing to use symlinked product adapter path: $$path" >&2; exit 1; fi; \
done; \
rm -rf "$$adapter_root"; \
	mkdir -p "$$adapter_include/linux"; \
	mkdir -p "$$adapter_include/linux/sched"; \
	mkdir -p "$$adapter_include/net"; \
	mkdir -p "$$adapter_root/source/lib"; \
	mkdir -p "$$adapter_root/source/lib/crypto"; \
	mkdir -p "$$adapter_root/source/drivers/of"; \
	mkdir -p "$$adapter_root/source/kernel/sched"; \
	mkdir -p "$$adapter_root/source/mm"; \
$(call orlix_product_adapter_validate_linux_truth); \
$(call orlix_product_adapter_validate_macho_projection); \
$(call orlix_product_adapter_generate_headers); \
$(call orlix_product_adapter_generate_sources)
endef

define orlix_product_adapter_validate_linux_truth
config="$(ORLIX_KERNEL_BUILD_DIR)/.config"; \
lds="$(ORLIX_KERNEL_BUILD_DIR)/arch/$(LINUX_ARCH)/kernel/vmlinux.lds"; \
linux_root="$(ORLIX_KERNEL_PORT_DIR)"; \
[ -s "$$config" ] || { echo "missing generated Orlix kernel config: $$config" >&2; exit 1; }; \
[ -s "$$lds" ] || { echo "missing generated Orlix linker script: $$lds" >&2; exit 1; }; \
for required in \
	"$$linux_root/include/linux/init.h" \
	"$$linux_root/include/linux/linkage.h" \
	"$$linux_root/include/linux/module.h" \
	"$$linux_root/include/linux/moduleparam.h" \
	"$$linux_root/include/linux/compiler.h" \
	"$$linux_root/include/linux/compiler_types.h" \
	"$$linux_root/include/linux/export.h" \
	"$$linux_root/include/linux/cache.h" \
	"$$linux_root/include/linux/elfnote.h" \
	"$$linux_root/include/linux/init_task.h" \
	"$$linux_root/include/linux/interrupt.h" \
	"$$linux_root/include/linux/mmdebug.h" \
	"$$linux_root/include/linux/once.h" \
	"$$linux_root/include/linux/percpu-defs.h" \
	"$$linux_root/include/linux/sched/debug.h" \
	"$$linux_root/include/linux/syscalls.h" \
	"$$linux_root/include/net/net_debug.h" \
	"$$linux_root/include/asm-generic/percpu.h" \
	"$$linux_root/include/asm-generic/vmlinux.lds.h" \
	"$$linux_root/arch/$(LINUX_ARCH)/kernel/vmlinux.lds.S" \
	"$$linux_root/init/Makefile" \
	"$$linux_root/kernel/sched/sched.h" \
	"$$linux_root/mm/internal.h" \
	"$$linux_root/drivers/of/Makefile" \
	"$$linux_root/usr/Makefile" \
	"$$linux_root/lib/Makefile" \
	"$$linux_root/lib/crypto/blake2s-generic.c" \
	"$$linux_root/scripts/mod/modpost.c" \
	"$$linux_root/scripts/link-vmlinux.sh"; do \
	[ -s "$$required" ] || { echo "missing Linux truth input: $$required" >&2; exit 1; }; \
done; \
require_text() { file="$$1"; text="$$2"; grep -F -q "$$text" "$$file" || { echo "Linux truth input missing text in $$file: $$text" >&2; exit 1; }; }; \
require_text "$$linux_root/include/linux/init.h" '__section(".init.text")'; \
require_text "$$linux_root/include/linux/init.h" '__section(".init.data")'; \
require_text "$$linux_root/include/linux/init.h" '__section(".init.rodata")'; \
require_text "$$linux_root/include/linux/init.h" '__section(".ref.text")'; \
require_text "$$linux_root/include/linux/init.h" '__section(".exit.text")'; \
require_text "$$linux_root/include/linux/init.h" '__section(".exit.data")'; \
require_text "$$linux_root/include/linux/init.h" '__section(".exitcall.exit")'; \
require_text "$$linux_root/include/linux/init.h" 'extern initcall_entry_t __initcall_start[];'; \
require_text "$$linux_root/include/linux/linkage.h" '#define cond_syscall(x)	asm('; \
require_text "$$linux_root/include/linux/linkage.h" '".weak " __stringify(x) "\n\t"'; \
require_text "$$linux_root/include/linux/moduleparam.h" '__used __section("__param")'; \
require_text "$$linux_root/include/linux/moduleparam.h" 'extern const struct kernel_param __start___param[], __stop___param[];'; \
require_text "$$linux_root/include/linux/compiler.h" '__section(".discard.addressable")'; \
require_text "$$linux_root/include/linux/compiler_types.h" '#define noinstr __noinstr_section(".noinstr.text")'; \
require_text "$$linux_root/include/linux/compiler_types.h" '#define __cpuidle __noinstr_section(".cpuidle.text")'; \
require_text "$$linux_root/include/linux/export.h" '.section ".export_symbol","a"'; \
require_text "$$linux_root/include/linux/cache.h" '__section(".data..ro_after_init")'; \
	require_text "$$linux_root/include/linux/elfnote.h" '__attribute__((section(".note." name),'; \
	require_text "$$linux_root/include/linux/init_task.h" '__section(".data..init_thread_info")'; \
	require_text "$$linux_root/include/linux/interrupt.h" '# define __irq_entry	 __section(".irqentry.text")'; \
	require_text "$$linux_root/include/linux/interrupt.h" '#define __softirq_entry  __section(".softirqentry.text")'; \
	require_text "$$linux_root/include/linux/mmdebug.h" '__section(".data.once")'; \
	require_text "$$linux_root/include/linux/module.h" '__section("__modver")'; \
	require_text "$$linux_root/include/linux/once.h" '__section(".data.once")'; \
	require_text "$$linux_root/include/linux/percpu-defs.h" 'PER_CPU_BASE_SECTION'; \
	require_text "$$linux_root/include/linux/sched/debug.h" '__section(".sched.text")'; \
	require_text "$$linux_root/include/linux/sched/debug.h" 'extern char __sched_text_start[], __sched_text_end[];'; \
	require_text "$$linux_root/include/linux/syscalls.h" '__attribute__((alias(__stringify(__se_sys##name))))'; \
	require_text "$$linux_root/include/net/net_debug.h" '__section(".data.once")'; \
	require_text "$$linux_root/include/asm-generic/percpu.h" '#ifndef PER_CPU_BASE_SECTION'; \
require_text "$$linux_root/include/asm-generic/vmlinux.lds.h" '#define COMMON_DISCARDS'; \
require_text "$$linux_root/include/asm-generic/vmlinux.lds.h" '*(.discard.*)'; \
require_text "$$linux_root/include/asm-generic/vmlinux.lds.h" '*(.export_symbol)'; \
	require_text "$$linux_root/include/asm-generic/vmlinux.lds.h" '__start_rodata = .;'; \
	require_text "$$linux_root/include/asm-generic/vmlinux.lds.h" '__end_rodata = .;'; \
	require_text "$$linux_root/include/asm-generic/vmlinux.lds.h" '#define INIT_CALLS'; \
	require_text "$$linux_root/include/asm-generic/vmlinux.lds.h" '__initramfs_start = .;'; \
	require_text "$$linux_root/include/asm-generic/vmlinux.lds.h" 'KEEP(*(.init.ramfs))'; \
	require_text "$$linux_root/include/asm-generic/vmlinux.lds.h" 'BOUNDED_SECTION_BY(.note.*, _notes)'; \
	require_text "$$linux_root/include/asm-generic/vmlinux.lds.h" '__sched_class_highest = .;'; \
	require_text "$$linux_root/include/asm-generic/vmlinux.lds.h" '*(__fair_sched_class)'; \
	require_text "$$linux_root/include/asm-generic/vmlinux.lds.h" '*(.noinstr.text)'; \
	require_text "$$linux_root/include/asm-generic/vmlinux.lds.h" '*(.cpuidle.text)'; \
	require_text "$$linux_root/include/asm-generic/vmlinux.lds.h" '__irqentry_text_start = .;'; \
	require_text "$$linux_root/include/asm-generic/vmlinux.lds.h" '*(.irqentry.text)'; \
	require_text "$$linux_root/include/asm-generic/vmlinux.lds.h" '__softirqentry_text_start = .;'; \
	require_text "$$linux_root/include/asm-generic/vmlinux.lds.h" '*(.softirqentry.text)'; \
	require_text "$$linux_root/include/asm-generic/vmlinux.lds.h" '__sched_text_start = .;'; \
	require_text "$$linux_root/include/asm-generic/vmlinux.lds.h" '__sched_text_end = .;'; \
	require_text "$$linux_root/include/asm-generic/vmlinux.lds.h" 'RESERVEDMEM_OF_TABLES()'; \
	require_text "$$linux_root/include/asm-generic/vmlinux.lds.h" 'KEEP(*(__##name##_of_table_end))'; \
	require_text "$$linux_root/include/asm-generic/vmlinux.lds.h" '* [_sdata, _edata] is the data section'; \
	require_text "$$linux_root/include/asm-generic/vmlinux.lds.h" 'BOUNDED_SECTION_PRE_LABEL(.builtin_fw, _builtin_fw, __start, __end)'; \
	require_text "$$linux_root/arch/$(LINUX_ARCH)/kernel/vmlinux.lds.S" 'jiffies = jiffies_64;'; \
	require_text "$$linux_root/init/Makefile" 'obj-$$(CONFIG_BLK_DEV_INITRD)   += initramfs.o'; \
	require_text "$$linux_root/init/Makefile" 'mounts-$$(CONFIG_BLK_DEV_INITRD)	+= do_mounts_initrd.o'; \
	require_text "$$linux_root/kernel/sched/sched.h" '__section("__" #name "_sched_class")'; \
	require_text "$$linux_root/mm/internal.h" '__section(".data.once")'; \
	require_text "$$linux_root/drivers/of/of_reserved_mem.c" '__used __section("__reservedmem_of_table_end");'; \
	require_text "$$linux_root/drivers/of/Makefile" 'empty_root.dtb.o'; \
	require_text "$$linux_root/usr/Makefile" 'obj-$$(CONFIG_BLK_DEV_INITRD) := initramfs_data.o'; \
	require_text "$$linux_root/lib/Makefile" 'lib-y := ctype.o string.o vsprintf.o cmdline.o \'; \
	require_text "$$linux_root/lib/Makefile" 'is_single_threaded.o plist.o decompress.o kobject_uevent.o \'; \
	require_text "$$linux_root/lib/Makefile" 'lib-$$(CONFIG_DECOMPRESS_GZIP) += decompress_inflate.o'; \
	require_text "$$linux_root/lib/Makefile" 'lib-$$(CONFIG_DECOMPRESS_ZSTD) += decompress_unzstd.o'; \
	require_text "$$linux_root/lib/crypto/blake2s-generic.c" 'void blake2s_compress(struct blake2s_state *state, const u8 *block,'; \
	require_text "$$linux_root/lib/crypto/blake2s-generic.c" '__weak __alias(blake2s_compress_generic);'; \
	require_text "$$linux_root/lib/crc32.c" 'u32 __pure crc32_le_base(u32, unsigned char const *, size_t) __alias(crc32_le);'; \
	require_text "$$linux_root/lib/crc32.c" 'u32 __pure __crc32c_le_base(u32, unsigned char const *, size_t) __alias(__crc32c_le);'; \
	require_text "$$linux_root/lib/crc32.c" 'u32 __pure crc32_be_base(u32, unsigned char const *, size_t) __alias(crc32_be);'; \
	require_text "$$linux_root/scripts/mod/modpost.c" 'EXPORT_SYMBOL'; \
require_text "$$linux_root/scripts/link-vmlinux.sh" 'vmlinux.o'; \
for pattern in \
	'.init.text' '.init.data' '.init.rodata' '.ref.text' '.init.setup' \
	'.exit.text' '.exit.data' '.exitcall.exit' \
	'jiffies = jiffies_64;' \
	'__setup_start = .;' '__setup_end = .;' '__init_begin = .;' '__init_end = .;' \
	'__start_rodata = .;' '__end_rodata = .;' \
	'_sdata = .;' '_edata = .;' '.builtin_fw' '__start_builtin_fw = .;' '__end_builtin_fw = .;' \
	'__start_notes = .;' 'KEEP(*(.note.*))' '__stop_notes = .;' \
	'__sched_class_highest = .;' '*(__fair_sched_class)' '__sched_class_lowest = .;' \
	'*(.noinstr.text)' '*(.cpuidle.text)' \
	'__irqentry_text_start = .;' '*(.irqentry.text)' '__softirqentry_text_start = .;' '*(.softirqentry.text)' \
	'__initcall_start = .;' '__initcall_end = .;' '__param' '.data.once' '.data..ro_after_init' \
	'.data..init_thread_info' '.sched.text' '__sched_text_start = .;' '__sched_text_end = .;' '__reservedmem_of_table = .;' 'KEEP(*(__reservedmem_of_table_end))' \
	'/DISCARD/ : {' '*(.discard)' '*(.discard.*)' '*(.export_symbol)' '*(.modinfo)'; do \
	require_text "$$lds" "$$pattern"; \
done; \
echo "validated Orlix product adapter Linux truth: $$lds"
endef

define orlix_product_adapter_validate_macho_projection
for pair in $(ORLIX_PRODUCT_ALLOWED_MACHO_SECTIONS); do \
	segment="$${pair%,*}"; \
	section="$${pair#*,}"; \
	[ "$$segment" != "$$pair" ] || { echo "invalid Mach-O section spelling: $$pair" >&2; exit 1; }; \
	[ "$${#segment}" -le 16 ] || { echo "Mach-O segment name is too long: $$segment" >&2; exit 1; }; \
	[ "$${#section}" -le 16 ] || { echo "Mach-O section name is too long: $$section" >&2; exit 1; }; \
done; \
echo "validated Orlix product adapter Mach-O section spellings"
endef

define orlix_product_adapter_generate_headers
command -v perl >/dev/null 2>&1 || { echo "perl is required to generate product adapter headers" >&2; exit 1; }; \
adapter_include="$(ORLIX_PRODUCT_ADAPTER_INCLUDE)"; \
linux_root="$(ORLIX_KERNEL_PORT_DIR)"; \
replace_once() { \
	file="$$1"; from="$$2"; to="$$3"; \
	count="$$(TEXT="$$from" perl -0ne 'BEGIN { $$text = $$ENV{"TEXT"}; $$count = 0; } $$count += () = /\Q$$text\E/g; END { print $$count; }' "$$file")"; \
	if [ "$$count" != 1 ]; then echo "expected one adapter replacement in $$file: $$from (found $$count)" >&2; exit 1; fi; \
	tmp="$$file.tmp"; \
	FROM="$$from" TO="$$to" perl -0pe 'BEGIN { $$from = $$ENV{"FROM"}; $$to = $$ENV{"TO"}; } s/\Q$$from\E/$$to/g' "$$file" > "$$tmp"; \
	mv "$$tmp" "$$file"; \
}; \
replace_all() { \
	file="$$1"; from="$$2"; to="$$3"; \
	count="$$(TEXT="$$from" perl -0ne 'BEGIN { $$text = $$ENV{"TEXT"}; $$count = 0; } $$count += () = /\Q$$text\E/g; END { print $$count; }' "$$file")"; \
	if [ "$$count" -lt 1 ]; then echo "expected adapter replacement in $$file: $$from" >&2; exit 1; fi; \
	tmp="$$file.tmp"; \
	FROM="$$from" TO="$$to" perl -0pe 'BEGIN { $$from = $$ENV{"FROM"}; $$to = $$ENV{"TO"}; } s/\Q$$from\E/$$to/g' "$$file" > "$$tmp"; \
	mv "$$tmp" "$$file"; \
}; \
cp "$$linux_root/include/linux/init.h" "$$adapter_include/linux/init.h"; \
cp "$$linux_root/include/linux/linkage.h" "$$adapter_include/linux/linkage.h"; \
cp "$$linux_root/include/linux/module.h" "$$adapter_include/linux/module.h"; \
cp "$$linux_root/include/linux/moduleparam.h" "$$adapter_include/linux/moduleparam.h"; \
cp "$$linux_root/include/linux/compiler.h" "$$adapter_include/linux/compiler.h"; \
cp "$$linux_root/include/linux/compiler_types.h" "$$adapter_include/linux/compiler_types.h"; \
cp "$$linux_root/include/linux/export.h" "$$adapter_include/linux/export.h"; \
cp "$$linux_root/include/linux/cache.h" "$$adapter_include/linux/cache.h"; \
cp "$$linux_root/include/linux/elfnote.h" "$$adapter_include/linux/elfnote.h"; \
cp "$$linux_root/include/linux/init_task.h" "$$adapter_include/linux/init_task.h"; \
cp "$$linux_root/include/linux/interrupt.h" "$$adapter_include/linux/interrupt.h"; \
cp "$$linux_root/include/linux/mmdebug.h" "$$adapter_include/linux/mmdebug.h"; \
cp "$$linux_root/include/linux/once.h" "$$adapter_include/linux/once.h"; \
cp "$$linux_root/include/linux/percpu-defs.h" "$$adapter_include/linux/percpu-defs.h"; \
cp "$$linux_root/include/linux/sched/debug.h" "$$adapter_include/linux/sched/debug.h"; \
cp "$$linux_root/include/linux/syscalls.h" "$$adapter_include/linux/syscalls.h"; \
cp "$$linux_root/include/linux/once_lite.h" "$$adapter_include/linux/once_lite.h"; \
cp "$$linux_root/include/net/net_debug.h" "$$adapter_include/net/net_debug.h"; \
replace_once "$$adapter_include/linux/init.h" '__section(".init.text")' '__section("__TEXT,__init_text")'; \
replace_once "$$adapter_include/linux/init.h" '__section(".init.data")' '__section("__DATA,__init_data")'; \
replace_once "$$adapter_include/linux/init.h" '__section(".init.rodata")' '__section("__TEXT,__init_rodata")'; \
replace_once "$$adapter_include/linux/init.h" '__section(".ref.text")' '__section("__TEXT,__ref_text")'; \
replace_once "$$adapter_include/linux/init.h" '__section(".ref.data")' '__section("__DATA,__ref_data")'; \
replace_once "$$adapter_include/linux/init.h" '__section(".exit.text")' '__section("__TEXT,__exit_text")'; \
replace_once "$$adapter_include/linux/init.h" '__section(".exit.data")' '__section("__DATA,__exit_data")'; \
replace_once "$$adapter_include/linux/init.h" '__section(".exitcall.exit")' '__section("__DATA,__exitcall")'; \
replace_once "$$adapter_include/linux/init.h" '__section(".init.setup")' '__section("__DATA,__init_setup")'; \
perl -0pi -e 'my $$changed = s/#define cond_syscall\(x\)\s+asm\(\s*\\\n\t"\.weak " __stringify\(x\) "\\n\\t"\s*\\\n\t"\.set  " __stringify\(x\) ","\s*\\\n\t\t __stringify\(sys_ni_syscall\)\)/#define cond_syscall(x) asmlinkage long x(void) __attribute__((weak)); asmlinkage long x(void) { return sys_ni_syscall(); }/; die "failed to replace Linux cond_syscall for Mach-O\n" unless $$changed == 1;' "$$adapter_include/linux/linkage.h"; \
perl -0pi -e 'my $$defs = join("\n", q{#define __orlix_product_initcall_section_early "__DATA,__initcall_e"}, q{#define __orlix_product_initcall_section_con "__DATA,__con_initcall"}, q{#define __orlix_product_initcall_section_0 "__DATA,__initcall0"}, q{#define __orlix_product_initcall_section_0s "__DATA,__initcall0s"}, q{#define __orlix_product_initcall_section_1 "__DATA,__initcall1"}, q{#define __orlix_product_initcall_section_1s "__DATA,__initcall1s"}, q{#define __orlix_product_initcall_section_2 "__DATA,__initcall2"}, q{#define __orlix_product_initcall_section_2s "__DATA,__initcall2s"}, q{#define __orlix_product_initcall_section_3 "__DATA,__initcall3"}, q{#define __orlix_product_initcall_section_3s "__DATA,__initcall3s"}, q{#define __orlix_product_initcall_section_4 "__DATA,__initcall4"}, q{#define __orlix_product_initcall_section_4s "__DATA,__initcall4s"}, q{#define __orlix_product_initcall_section_5 "__DATA,__initcall5"}, q{#define __orlix_product_initcall_section_5s "__DATA,__initcall5s"}, q{#define __orlix_product_initcall_section_rootfs "__DATA,__initcallrf"}, q{#define __orlix_product_initcall_section_rootfss "__DATA,__initcallrfs"}, q{#define __orlix_product_initcall_section_6 "__DATA,__initcall6"}, q{#define __orlix_product_initcall_section_6s "__DATA,__initcall6s"}, q{#define __orlix_product_initcall_section_7 "__DATA,__initcall7"}, q{#define __orlix_product_initcall_section_7s "__DATA,__initcall7s"}) . "\n"; my $$inserted = s/\n#ifdef CONFIG_LTO_CLANG\n/\n$$defs\n#ifdef CONFIG_LTO_CLANG\n/; die "failed to insert Orlix initcall section map\n" unless $$inserted == 1; my $$section_defs = s/#define __initcall_section\(__sec, __iid\)\s*\\\n\t(?:#__sec "\.init\.\." #__iid|#__sec "\.init")/#define __initcall_section(__sec, __iid) __sec/g; die "expected two Linux initcall section definitions, found $$section_defs\n" unless $$section_defs == 2; my $$initcalls = s/\.initcall##id/__orlix_product_initcall_section_##id/g; die "expected one initcall token-paste replacement, found $$initcalls\n" unless $$initcalls == 1; my $$con = s/\.con_initcall/__orlix_product_initcall_section_con/g; die "expected one console initcall replacement, found $$con\n" unless $$con == 1;' "$$adapter_include/linux/init.h"; \
replace_once "$$adapter_include/linux/compiler.h" '__section(".discard.addressable")' '__section("__DATA,__discard_addr")'; \
replace_once "$$adapter_include/linux/compiler_types.h" '#define noinstr __noinstr_section(".noinstr.text")' '#define noinstr __noinstr_section("__TEXT,__noinstr_text")'; \
replace_once "$$adapter_include/linux/compiler_types.h" '#define __cpuidle __noinstr_section(".cpuidle.text")' '#define __cpuidle __noinstr_section("__TEXT,__cpuidle_text")'; \
replace_once "$$adapter_include/linux/export.h" '.section ".export_symbol","a"' '.section __DATA,__export_symbol'; \
replace_once "$$adapter_include/linux/cache.h" '__section(".data..ro_after_init")' '__section("__DATA,__ro_after_init")'; \
replace_once "$$adapter_include/linux/cache.h" '__section__(".data..cacheline_aligned")' '__section__("__DATA,__cacheline")'; \
replace_once "$$adapter_include/linux/elfnote.h" '__attribute__((section(".note." name),' '__attribute__((section("__DATA,__note"),'; \
replace_once "$$adapter_include/linux/init_task.h" '__section(".data..init_thread_info")' '__section("__DATA,__init_tinfo")'; \
replace_once "$$adapter_include/linux/interrupt.h" '# define __irq_entry	 __section(".irqentry.text")' '# define __irq_entry	 __section("__TEXT,__irqentry_text")'; \
replace_once "$$adapter_include/linux/interrupt.h" '#define __softirq_entry  __section(".softirqentry.text")' '#define __softirq_entry  __section("__TEXT,__softirq_text")'; \
replace_all "$$adapter_include/linux/mmdebug.h" '__section(".data.once")' '__section("__DATA,__data_once")'; \
replace_once "$$adapter_include/linux/module.h" '__section("__modver")' '__section("__DATA,__modver")'; \
replace_once "$$adapter_include/linux/moduleparam.h" '__section(".modinfo")' '__section("__DATA,__modinfo")'; \
replace_once "$$adapter_include/linux/moduleparam.h" '__section("__param")' '__section("__DATA,__param")'; \
replace_all "$$adapter_include/linux/once.h" '__section(".data.once")' '__section("__DATA,__data_once")'; \
replace_once "$$adapter_include/linux/once_lite.h" '__section(".data.once")' '__section("__DATA,__data_once")'; \
replace_once "$$adapter_include/linux/percpu-defs.h" '__section(".discard")' '__section("__DATA,__discard")'; \
replace_once "$$adapter_include/linux/sched/debug.h" '__section(".sched.text")' '__section("__TEXT,__sched_text")'; \
replace_all "$$adapter_include/net/net_debug.h" '__section(".data.once")' '__section("__DATA,__data_once")'; \
perl -0pi -e 'my $$cast = s/#define __SC_ARGS\(t, a\)\ta\n/#define __SC_ARGS(t, a)\ta\n#define __SC_LONG_CAST(t, a) (__typeof(__builtin_choose_expr(__TYPE_IS_LL(t), 0LL, 0L)))(a)\n/; die "failed to insert Orlix syscall cast helper\n" unless $$cast == 1; my $$alias = s/asmlinkage long sys##name\(__MAP\(x,__SC_DECL,__VA_ARGS__\)\)\s*\\\n\t\t__attribute__\(\(alias\(__stringify\(__se_sys##name\)\)\)\);\s*\\/asmlinkage long __se_sys##name(__MAP(x,__SC_LONG,__VA_ARGS__));\t\\\n\tasmlinkage long sys##name(__MAP(x,__SC_DECL,__VA_ARGS__));\t\\\n\tasmlinkage long sys##name(__MAP(x,__SC_DECL,__VA_ARGS__))\t\\\n\t{\t\t\t\t\t\t\t\t\\\n\t\treturn __se_sys##name(__MAP(x,__SC_LONG_CAST,__VA_ARGS__));\\\n\t}\t\t\t\t\t\t\t\t\\/; die "failed to replace Linux syscall alias for Mach-O\n" unless $$alias == 1;' "$$adapter_include/linux/syscalls.h"; \
perl -0pi -e 's/#define PER_CPU_SHARED_ALIGNED_SECTION "\.\.shared_aligned"/#define PER_CPU_SHARED_ALIGNED_SECTION ""/g; s/#define PER_CPU_ALIGNED_SECTION "\.\.shared_aligned"/#define PER_CPU_ALIGNED_SECTION ""/g;' "$$adapter_include/linux/percpu-defs.h"; \
	echo "generated Orlix product adapter headers: $$adapter_include"
endef

define orlix_product_adapter_generate_sources
adapter_root="$(ORLIX_PRODUCT_ADAPTER_ROOT)"; \
	linux_root="$(ORLIX_KERNEL_PORT_DIR)"; \
	cp "$$linux_root/lib/crc32.c" "$$adapter_root/source/lib/crc32.c"; \
	cp "$$linux_root/lib/crypto/blake2s-generic.c" "$$adapter_root/source/lib/crypto/blake2s-generic.c"; \
	cp "$$linux_root/drivers/of/of_reserved_mem.c" "$$adapter_root/source/drivers/of/of_reserved_mem.c"; \
	cp "$$linux_root/mm/page_alloc.c" "$$adapter_root/source/mm/page_alloc.c"; \
	cp "$$linux_root/mm/internal.h" "$$adapter_root/source/mm/internal.h"; \
	cp "$$linux_root/mm/vma.h" "$$adapter_root/source/mm/vma.h"; \
	cp "$$linux_root/mm/shuffle.h" "$$adapter_root/source/mm/shuffle.h"; \
	cp "$$linux_root/mm/page_reporting.h" "$$adapter_root/source/mm/page_reporting.h"; \
for sched_src in core.c fair.c build_policy.c build_utility.c; do cp "$$linux_root/kernel/sched/$$sched_src" "$$adapter_root/source/kernel/sched/$$sched_src"; done; \
cp "$$linux_root/kernel/sched/sched.h" "$$adapter_root/source/kernel/sched/sched.h"; \
replace_once "$$adapter_root/source/lib/crc32.c" 'u32 __pure crc32_le_base(u32, unsigned char const *, size_t) __alias(crc32_le);' 'u32 __pure crc32_le_base(u32 crc, unsigned char const *p, size_t len) { return crc32_le(crc, p, len); }'; \
replace_once "$$adapter_root/source/lib/crc32.c" 'u32 __pure __crc32c_le_base(u32, unsigned char const *, size_t) __alias(__crc32c_le);' 'u32 __pure __crc32c_le_base(u32 crc, unsigned char const *p, size_t len) { return __crc32c_le(crc, p, len); }'; \
	replace_once "$$adapter_root/source/lib/crc32.c" 'u32 __pure crc32_be_base(u32, unsigned char const *, size_t) __alias(crc32_be);' 'u32 __pure crc32_be_base(u32 crc, unsigned char const *p, size_t len) { return crc32_be(crc, p, len); }'; \
	perl -0pi -e 'my $$changed = s/void blake2s_compress\(struct blake2s_state \*state, const u8 \*block,\n\s*size_t nblocks, const u32 inc\)\n\s*__weak __alias\(blake2s_compress_generic\);/void blake2s_compress(struct blake2s_state *state, const u8 *block,\n\t\t      size_t nblocks, const u32 inc)\n{\n\tblake2s_compress_generic(state, block, nblocks, inc);\n}/; die "failed to replace Linux blake2s weak alias for Mach-O\n" unless $$changed == 1;' "$$adapter_root/source/lib/crypto/blake2s-generic.c"; \
	replace_once "$$adapter_root/source/drivers/of/of_reserved_mem.c" '__used __section("__reservedmem_of_table_end");' '__used __section("__DATA,__rmem_end");'; \
replace_once "$$adapter_root/source/kernel/sched/sched.h" '__section("__" #name "_sched_class")' '__section("__DATA,__sched_" #name)'; \
replace_all "$$adapter_root/source/mm/internal.h" '__section(".data.once")' '__section("__DATA,__data_once")'; \
echo "generated Orlix product adapter sources: $$adapter_root/source"
endef

define orlix_product_adapter_source_resolver
orlix_product_adapter_source_for() { \
	src_rel="$$1"; \
	case "$$src_rel" in \
		drivers/of/of_reserved_mem.c) printf '%s\n' "$(ORLIX_PRODUCT_ADAPTER_ROOT)/source/drivers/of/of_reserved_mem.c" ;; \
		kernel/sched/core.c|kernel/sched/fair.c|kernel/sched/build_policy.c|kernel/sched/build_utility.c) printf '%s\n' "$(ORLIX_PRODUCT_ADAPTER_ROOT)/source/$$src_rel" ;; \
		lib/crypto/blake2s-generic.c) printf '%s\n' "$(ORLIX_PRODUCT_ADAPTER_ROOT)/source/lib/crypto/blake2s-generic.c" ;; \
		lib/crc32.c) printf '%s\n' "$(ORLIX_PRODUCT_ADAPTER_ROOT)/source/lib/crc32.c" ;; \
		mm/page_alloc.c) printf '%s\n' "$(ORLIX_PRODUCT_ADAPTER_ROOT)/source/mm/page_alloc.c" ;; \
		*) printf '%s\n' "$(ORLIX_KERNEL_PORT_DIR)/$$src_rel" ;; \
	esac; \
};
endef

define orlix_product_adapter_generate_payloads
orlix_product_adapter_generate_payloads() { \
	platform="$$1"; \
	target="$$2"; \
	payload_src="$(ORLIX_PRODUCT_ADAPTER_ROOT)/orlix-product-payloads.S"; \
	payload_obj="$(ORLIX_PRODUCT_PAYLOAD_OBJECT)"; \
	empty_root_dtb="$(ORLIX_KERNEL_BUILD_DIR)/drivers/of/empty_root.dtb"; \
	initramfs_data="$(ORLIX_KERNEL_BUILD_DIR)/usr/initramfs_inc_data"; \
	[ -s "$$empty_root_dtb" ] || { echo "missing generated Linux empty-root DTB: $$empty_root_dtb" >&2; exit 1; }; \
	[ -s "$$initramfs_data" ] || { echo "missing generated Linux initramfs input: $$initramfs_data" >&2; exit 1; }; \
	initramfs_size="$$(wc -c < "$$initramfs_data" | tr -d '[:space:]')"; \
	{ \
		printf '%s\n' '/* generated Build-only Mach-O wrappers for Linux-generated payload inputs */'; \
		printf '%s\n' '.section __TEXT,__dtb_init'; \
		printf '%s\n' '.p2align 3'; \
		printf '%s\n' '.globl ___dtb_empty_root_begin'; \
		printf '%s\n' '___dtb_empty_root_begin:'; \
		printf '.incbin "%s"\n' "$$empty_root_dtb"; \
		printf '%s\n' '.globl ___dtb_empty_root_end'; \
		printf '%s\n' '___dtb_empty_root_end:'; \
		printf '%s\n' '.p2align 3'; \
		printf '%s\n' '.section __DATA,__init_ramfs'; \
		printf '%s\n' '.p2align 2'; \
		printf '%s\n' '.globl ___initramfs_start'; \
		printf '%s\n' '___initramfs_start:'; \
		printf '.incbin "%s"\n' "$$initramfs_data"; \
		printf '%s\n' '.p2align 3'; \
		printf '%s\n' '.globl ___initramfs_size'; \
		printf '%s\n' '___initramfs_size:'; \
		printf '.quad %s\n' "$$initramfs_size"; \
	} > "$$payload_src"; \
	/usr/bin/env -u SDKROOT "$$cc" -target "$$target" -isysroot / -x assembler -c "$$payload_src" -o "$$payload_obj"; \
	orlix_product_adapter_verify_object_contract "$$payload_obj"; \
	objs+=("$$payload_obj"); \
	echo "generated Orlix product payload object: $$payload_obj ($$platform)"; \
};
endef

define orlix_product_adapter_verify_object_contract
orlix_product_adapter_verify_object_contract() { \
	obj="$$1"; \
	allowed_sections="$(ORLIX_PRODUCT_ALLOWED_MACHO_SECTIONS)"; \
	"$$otool_cmd" -l "$$obj" | awk -v allowed_sections="$$allowed_sections" 'BEGIN { split(allowed_sections, pairs, /[[:space:]]+/); for (i in pairs) if (pairs[i] != "") allowed[pairs[i]] = 1 } /sectname / { section=$$2; next } /segname / { segment=$$2; key=segment "," section; if (section != "" && !(key in allowed)) { print "unclassified Mach-O section " key " in " FILENAME > "/dev/stderr"; bad=1 } section="" } END { exit bad ? 1 : 0 }' || { echo "Orlix product object contains unclassified Mach-O section: $$obj" >&2; exit 1; }; \
	if "$$otool_cmd" -l "$$obj" | grep -E 'segname \.(init|exit|ref|discard|export_symbol|modinfo)'; then echo "Orlix product object leaked GNU/Linux section spelling into Mach-O segment: $$obj" >&2; exit 1; fi; \
	if "$$otool_cmd" -l "$$obj" | grep -E 'sectname \.(init|exit|ref|discard|export_symbol|modinfo)'; then echo "Orlix product object leaked GNU/Linux section spelling into Mach-O section: $$obj" >&2; exit 1; fi; \
	if "$$nm_cmd" -m "$$obj" | grep -E '(^|[[:space:]])_+(__DISABLE_EXPORTS|HAVE_ARCH_COMPILER_H)([[:space:]]|$$)'; then echo "Orlix product object uses forbidden adapter escape symbol: $$obj" >&2; exit 1; fi; \
};
endef

define orlix_product_adapter_generate_boundaries
orlix_product_adapter_generate_boundaries() { \
	platform="$$1"; \
	target="$$2"; \
	shift 2; \
	product_objects=("$${@}"); \
	boundary_src="$(ORLIX_PRODUCT_ADAPTER_ROOT)/orlix-product-boundaries.S"; \
	boundary_obj="$(ORLIX_PRODUCT_BOUNDARY_OBJECT)"; \
	[ "$${#product_objects[@]}" -gt 0 ] || { echo "cannot generate product boundaries without product objects" >&2; exit 1; }; \
	present_sections="$$(for candidate in "$${product_objects[@]}"; do "$$otool_cmd" -l "$$candidate" | awk '/sectname / { section=$$2; next } /segname / { if (section != "") print $$2 "," section; section="" }'; done | LC_ALL=C sort -u)"; \
	present_section_names="$$(printf '%s\n' "$$present_sections" | awk -F, 'NF == 2 { print $$2 }' | LC_ALL=C sort -u)"; \
	undefined_symbols="$$(for candidate in "$${product_objects[@]}"; do "$$nm_cmd" -u "$$candidate" | awk 'NF { print $$NF }'; done | LC_ALL=C sort -u)"; \
	list_has_line() { list="$$1"; needle="$$2"; printf '%s\n' "$$list" | awk -v needle="$$needle" '$$0 == needle { found = 1 } END { exit found ? 0 : 1 }'; }; \
	section_present() { \
		segment="$$1"; section="$$2"; \
		list_has_line "$$present_sections" "$$segment,$$section"; \
	}; \
	section_name_matches() { \
		expr="$$1"; \
		printf '%s\n' "$$present_section_names" | awk -v expr="$$expr" '$$0 ~ expr { found = 1 } END { exit found ? 0 : 1 }'; \
	}; \
	undefined_symbol_present() { \
		symbol="$$1"; \
		list_has_line "$$undefined_symbols" "$$symbol"; \
	}; \
	section_label() { kind="$$1"; segment="$$2"; section="$$3"; printf 'section$$%s$$%s$$%s' "$$kind" "$$segment" "$$section"; }; \
	emit_label() { symbol="$$1"; printf '.globl %s\n%s:\n' "$$symbol" "$$symbol"; }; \
	emit_alias() { symbol="$$1"; target_symbol="$$2"; printf '.globl %s\n.set %s, %s\n' "$$symbol" "$$symbol" "$$target_symbol"; }; \
	emit_empty_pair() { emit_label "$$1"; emit_label "$$2"; }; \
	emit_section_pair() { \
		start_symbol="$$1"; end_symbol="$$2"; segment="$$3"; section="$$4"; \
		if section_present "$$segment" "$$section"; then \
			emit_alias "$$start_symbol" "$$(section_label start "$$segment" "$$section")"; \
			emit_alias "$$end_symbol" "$$(section_label end "$$segment" "$$section")"; \
		else \
			emit_empty_pair "$$start_symbol" "$$end_symbol"; \
		fi; \
	}; \
	emit_empty_pair_if_needed() { \
		start_symbol="$$1"; end_symbol="$$2"; \
		if undefined_symbol_present "$$start_symbol" || undefined_symbol_present "$$end_symbol"; then emit_empty_pair "$$start_symbol" "$$end_symbol"; fi; \
	}; \
	emit_section_pair_if_needed() { \
		start_symbol="$$1"; end_symbol="$$2"; segment="$$3"; section="$$4"; \
		if undefined_symbol_present "$$start_symbol" || undefined_symbol_present "$$end_symbol"; then emit_section_pair "$$start_symbol" "$$end_symbol" "$$segment" "$$section"; fi; \
	}; \
	emit_required_section_pair_if_needed() { \
		start_symbol="$$1"; end_symbol="$$2"; segment="$$3"; section="$$4"; \
		if undefined_symbol_present "$$start_symbol" || undefined_symbol_present "$$end_symbol"; then \
			section_present "$$segment" "$$section" || { echo "requested Linux boundary $$start_symbol/$$end_symbol has no Mach-O projection section $$segment,$$section" >&2; exit 1; }; \
			emit_section_pair "$$start_symbol" "$$end_symbol" "$$segment" "$$section"; \
		fi; \
	}; \
	emit_symbol_if_needed() { \
		symbol="$$1"; \
		if undefined_symbol_present "$$symbol"; then emit_label "$$symbol"; fi; \
	}; \
	emit_reservedmem_table_if_needed() { \
		if undefined_symbol_present ___reservedmem_of_table; then \
			if section_present __DATA __rmem_tbl; then emit_alias ___reservedmem_of_table "$$(section_label start __DATA __rmem_tbl)"; \
			elif section_present __DATA __rmem_end; then emit_alias ___reservedmem_of_table "$$(section_label start __DATA __rmem_end)"; \
			else emit_label ___reservedmem_of_table; fi; \
		fi; \
	}; \
	emit_sched_class_range_if_needed() { \
		if undefined_symbol_present ___sched_class_highest || undefined_symbol_present ___sched_class_lowest; then \
			first=""; last=""; \
			for section in __sched_stop __sched_dl __sched_rt __sched_fair __sched_ext __sched_idle; do \
				if section_present __DATA "$$section"; then [ -n "$$first" ] || first="$$section"; last="$$section"; fi; \
			done; \
			if [ -n "$$first" ]; then \
				emit_alias ___sched_class_highest "$$(section_label start __DATA "$$first")"; \
				emit_alias ___sched_class_lowest "$$(section_label end __DATA "$$last")"; \
			else \
				emit_empty_pair ___sched_class_highest ___sched_class_lowest; \
			fi; \
		fi; \
	}; \
	emit_data_range_if_needed() { \
		if undefined_symbol_present __sdata || undefined_symbol_present __edata; then \
			if section_present __TEXT __const && section_present __DATA __data; then \
				emit_alias __sdata "$$(section_label start __TEXT __const)"; \
				emit_alias __edata "$$(section_label end __DATA __data)"; \
			elif section_present __DATA __data; then \
				emit_section_pair __sdata __edata __DATA __data; \
			else \
				emit_empty_pair __sdata __edata; \
			fi; \
		fi; \
	}; \
	initcall_cursor=""; \
	first_present_initcall_section() { \
		for section in "$${@}"; do \
			if section_present __DATA "$$section"; then printf '%s' "$$section"; return 0; fi; \
		done; \
		return 1; \
	}; \
	last_present_initcall_section() { \
		last=""; \
		for section in "$${@}"; do \
			if section_present __DATA "$$section"; then last="$$section"; fi; \
		done; \
		[ -n "$$last" ] || return 1; \
		printf '%s' "$$last"; \
	}; \
	emit_initcall_group_boundary() { \
		symbol="$$1"; shift; \
		first="$$(first_present_initcall_section "$${@}" || true)"; \
		if [ -n "$$first" ]; then \
			emit_alias "$$symbol" "$$(section_label start __DATA "$$first")"; \
			last="$$(last_present_initcall_section "$${@}")"; \
			initcall_cursor="$$(section_label end __DATA "$$last")"; \
		elif [ -n "$$initcall_cursor" ]; then \
			emit_alias "$$symbol" "$$initcall_cursor"; \
		else \
			emit_label "$$symbol"; \
			initcall_cursor="$$symbol"; \
		fi; \
	}; \
	emit_initcall_end_boundary() { \
		symbol="$$1"; \
		if [ -n "$$initcall_cursor" ]; then emit_alias "$$symbol" "$$initcall_cursor"; else emit_label "$$symbol"; initcall_cursor="$$symbol"; fi; \
	}; \
	mkdir -p "$$(dirname "$$boundary_src")"; \
	{ \
		printf '%s\n' '/* generated Build-only product-link boundary glue for the Mach-O OrlixKernel product */'; \
		printf '%s\n' '/* Only undefined Linux linker-script boundary symbols requested by the current object set are emitted. */'; \
		printf '%s\n' '.section __DATA,__orlix_bnd'; \
		printf '%s\n' '.p2align 3'; \
		printf '%s\n' '/* __init_begin/__init_end are conservative until init-memory reclaim semantics exist. */'; \
		emit_section_pair_if_needed __stext __etext __TEXT __text; \
		if undefined_symbol_present _jiffies; then emit_alias _jiffies _jiffies_64; fi; \
		emit_data_range_if_needed; \
		emit_section_pair_if_needed ___cpuidle_text_start ___cpuidle_text_end __TEXT __cpuidle_text; \
		emit_section_pair_if_needed ___irqentry_text_start ___irqentry_text_end __TEXT __irqentry_text; \
		emit_section_pair_if_needed ___noinstr_text_start ___noinstr_text_end __TEXT __noinstr_text; \
		emit_section_pair_if_needed ___sched_text_start ___sched_text_end __TEXT __sched_text; \
		emit_section_pair_if_needed ___softirqentry_text_start ___softirqentry_text_end __TEXT __softirq_text; \
		emit_required_section_pair_if_needed ___start_rodata ___end_rodata __TEXT __const; \
		emit_sched_class_range_if_needed; \
		emit_section_pair_if_needed __sinittext __einittext __TEXT __init_text; \
		emit_empty_pair_if_needed ___init_begin ___init_end; \
		emit_section_pair_if_needed ___setup_start ___setup_end __DATA __init_setup; \
		if section_name_matches '^__initcall' || undefined_symbol_present ___initcall_start || undefined_symbol_present ___initcall_end; then \
			emit_initcall_group_boundary ___initcall_start __initcall_e; \
			emit_initcall_group_boundary ___initcall0_start __initcall0 __initcall0s; \
			emit_initcall_group_boundary ___initcall1_start __initcall1 __initcall1s; \
			emit_initcall_group_boundary ___initcall2_start __initcall2 __initcall2s; \
			emit_initcall_group_boundary ___initcall3_start __initcall3 __initcall3s; \
			emit_initcall_group_boundary ___initcall4_start __initcall4 __initcall4s; \
			emit_initcall_group_boundary ___initcall5_start __initcall5 __initcall5s __initcallrf __initcallrfs; \
			emit_initcall_group_boundary ___initcall6_start __initcall6 __initcall6s; \
			emit_initcall_group_boundary ___initcall7_start __initcall7 __initcall7s; \
			emit_initcall_end_boundary ___initcall_end; \
		else \
			for symbol in ___initcall_start ___initcall0_start ___initcall1_start ___initcall2_start ___initcall3_start ___initcall4_start ___initcall5_start ___initcall6_start ___initcall7_start ___initcall_end; do emit_symbol_if_needed "$$symbol"; done; \
		fi; \
		emit_section_pair_if_needed ___con_initcall_start ___con_initcall_end __DATA __con_initcall; \
		emit_section_pair_if_needed ___start_once ___end_once __DATA __data_once; \
		emit_section_pair_if_needed ___start_ro_after_init ___end_ro_after_init __DATA __ro_after_init; \
		emit_section_pair_if_needed ___start_builtin_fw ___end_builtin_fw __DATA __builtin_fw; \
		emit_section_pair_if_needed ___per_cpu_start ___per_cpu_end __DATA __percpu; \
		if undefined_symbol_present ___bss_start || undefined_symbol_present ___bss_stop; then if section_present __DATA __bss && ! section_present __DATA __common; then emit_section_pair ___bss_start ___bss_stop __DATA __bss; else emit_empty_pair ___bss_start ___bss_stop; fi; fi; \
		emit_section_pair_if_needed ___start___param ___stop___param __DATA __param; \
		emit_section_pair_if_needed ___start___modver ___stop___modver __DATA __modver; \
		emit_section_pair_if_needed ___start_notes ___stop_notes __DATA __note; \
		emit_reservedmem_table_if_needed; \
		emit_section_pair_if_needed ___start___ksymtab ___stop___ksymtab __DATA __ksymtab; \
		emit_section_pair_if_needed ___start___kcrctab ___stop___kcrctab __DATA __kcrctab; \
		emit_section_pair_if_needed ___start___ex_table ___stop___ex_table __DATA __ex_table; \
		emit_section_pair_if_needed ___start___jump_table ___stop___jump_table __DATA __jump_table; \
		emit_section_pair_if_needed ___start___bug_table ___stop___bug_table __DATA __bug_table; \
	} > "$$boundary_src"; \
	/usr/bin/env -u SDKROOT "$$cc" -target "$$target" -isysroot / -x assembler -c "$$boundary_src" -o "$$boundary_obj"; \
	for symbol in _jiffies __sdata __edata ___init_begin ___init_end ___cpuidle_text_start ___cpuidle_text_end ___irqentry_text_start ___irqentry_text_end ___noinstr_text_start ___noinstr_text_end ___sched_text_start ___sched_text_end ___softirqentry_text_start ___softirqentry_text_end ___start_rodata ___end_rodata ___sched_class_highest ___sched_class_lowest ___setup_start ___setup_end ___initcall_start ___initcall0_start ___initcall1_start ___initcall2_start ___initcall3_start ___initcall4_start ___initcall5_start ___initcall6_start ___initcall7_start ___initcall_end ___con_initcall_start ___con_initcall_end ___start_once ___end_once ___start_ro_after_init ___end_ro_after_init ___start_builtin_fw ___end_builtin_fw ___per_cpu_start ___per_cpu_end ___bss_start ___bss_stop ___start___param ___stop___param ___start___modver ___stop___modver ___start_notes ___stop_notes ___start___ksymtab ___stop___ksymtab ___start___kcrctab ___stop___kcrctab ___start___ex_table ___stop___ex_table ___start___jump_table ___stop___jump_table ___start___bug_table ___stop___bug_table; do if undefined_symbol_present "$$symbol"; then "$$nm_cmd" -m "$$boundary_obj" | grep -F -q "$$symbol" || { echo "product boundary object missing requested symbol: $$symbol" >&2; exit 1; }; fi; done; \
	objs+=("$$boundary_obj"); \
	echo "generated Orlix product boundary object: $$boundary_obj"; \
};
endef
