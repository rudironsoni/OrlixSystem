ORLIX_PRODUCT_ADAPTER_ROOT := $(ORLIX_KERNEL_BUILD_DIR)/orlix-product-compile-adapter
ORLIX_PRODUCT_ADAPTER_INCLUDE := $(ORLIX_PRODUCT_ADAPTER_ROOT)/include
ORLIX_PRODUCT_ADAPTER_SOURCE := $(ORLIX_PRODUCT_ADAPTER_ROOT)/source
ORLIX_PRODUCT_ADAPTER_CFLAGS := -O2 -fshort-wchar -DPER_CPU_BASE_SECTION=\"__DATA,__data\" -I"$(ORLIX_PRODUCT_ADAPTER_INCLUDE)"
ORLIX_PRODUCT_BOUNDARY_OBJECT := $(ORLIX_PRODUCT_ADAPTER_ROOT)/orlix-product-boundaries.o

define orlix_product_adapter_prepare
adapter_root="$(ORLIX_PRODUCT_ADAPTER_ROOT)"; \
adapter_include="$(ORLIX_PRODUCT_ADAPTER_INCLUDE)"; \
adapter_source="$(ORLIX_PRODUCT_ADAPTER_SOURCE)"; \
for path in "$(ORLIX_KERNEL_BUILD_DIR)" "$$adapter_root" "$$adapter_include" "$$adapter_source"; do \
	if [ -e "$$path" ] && [ -L "$$path" ]; then echo "refusing to use symlinked product compile adapter path: $$path" >&2; exit 1; fi; \
done; \
	rm -rf "$$adapter_root"; \
	mkdir -p "$$adapter_include/linux/sched" "$$adapter_source/mm" "$$adapter_source/drivers/base" "$$adapter_source/fs" "$$adapter_source/kernel/locking" "$$adapter_source/kernel/rcu" "$$adapter_source/kernel/sched" "$$adapter_source/kernel/time"; \
$(call orlix_product_adapter_validate_linux_truth); \
$(call orlix_product_adapter_generate_headers)
endef

define orlix_product_adapter_validate_linux_truth
config="$(ORLIX_KERNEL_BUILD_DIR)/.config"; \
[ -s "$$config" ] || { echo "missing generated Orlix kernel config for product archive: $$config" >&2; exit 1; }; \
cc_optimize_config="$$(grep -n '^CONFIG_CC_OPTIMIZE_FOR_PERFORMANCE=y' "$$config" || true)"; \
if [ -z "$$cc_optimize_config" ]; then echo "product compile adapter requires Linux performance optimization config for -O2 Mach-O objects" >&2; exit 1; fi; \
	smp_config="$$(grep -n '^CONFIG_SMP=' "$$config" || true)"; \
	if [ -n "$$smp_config" ]; then echo "product non-SMP percpu section adapter requires CONFIG_SMP unset: $$smp_config" >&2; exit 1; fi; \
	preempt_rt_config="$$(grep -n '^CONFIG_PREEMPT_RT=' "$$config" || true)"; \
	if [ -n "$$preempt_rt_config" ]; then echo "product softirq adapter currently requires CONFIG_PREEMPT_RT unset: $$preempt_rt_config" >&2; exit 1; fi; \
	trace_irqflags_config="$$(grep -n '^CONFIG_TRACE_IRQFLAGS=' "$$config" || true)"; \
	if [ -n "$$trace_irqflags_config" ]; then echo "product softirq adapter currently requires CONFIG_TRACE_IRQFLAGS unset: $$trace_irqflags_config" >&2; exit 1; fi; \
	bug_config="$$(grep -n '^CONFIG_BUG=y' "$$config" || true)"; \
	if [ -z "$$bug_config" ]; then echo "product panic/warn adapter requires CONFIG_BUG=y for upstream warn_slowpath_fmt" >&2; exit 1; fi; \
	lds="$(ORLIX_KERNEL_BUILD_DIR)/arch/$(LINUX_ARCH)/kernel/vmlinux.lds"; \
generic_lds="$(ORLIX_KERNEL_PORT_DIR)/include/asm-generic/vmlinux.lds.h"; \
[ -s "$$lds" ] || { echo "missing generated Orlix linker script for product section validation: $$lds" >&2; exit 1; }; \
	for pattern in '.init.text' '.init.data' '.init.rodata' '.ref.text' '.sched.text' '.init.setup' '.con_initcall.init' '__setup_start = .;' '__setup_end = .;' '__init_begin = .;' '__init_end = .;' '__initcall_start = .;' '__initcall_end = .;' '__con_initcall_start = .;' '__con_initcall_end = .;' '__param' '.data.once' '.data..ro_after_init' '/DISCARD/ : {' '*(.discard)' '*(.export_symbol)' '*(.modinfo)'; do \
	grep -F -q "$$pattern" "$$lds" || { echo "generated Orlix Kbuild linker script missing Linux section pattern: $$pattern" >&2; exit 1; }; \
done; \
for pattern in \
	'#define INIT_SETUP(initsetup_align)' \
	'BOUNDED_SECTION_POST_LABEL(.init.setup, __setup, _start, _end)' \
	'#define INIT_CALLS_LEVEL(level)' \
	'__initcall##level##_start = .;' \
	'KEEP(*(.initcall##level##.init))' \
	'KEEP(*(.initcall##level##s.init))' \
	'#define INIT_CALLS' \
	'__initcall_start = .;' \
	'KEEP(*(.initcallearly.init))' \
	'INIT_CALLS_LEVEL(0)' \
	'INIT_CALLS_LEVEL(1)' \
	'INIT_CALLS_LEVEL(2)' \
	'INIT_CALLS_LEVEL(3)' \
	'INIT_CALLS_LEVEL(4)' \
	'INIT_CALLS_LEVEL(5)' \
		'INIT_CALLS_LEVEL(rootfs)' \
		'INIT_CALLS_LEVEL(6)' \
		'INIT_CALLS_LEVEL(7)' \
		'__initcall_end = .;' \
		'#define CON_INITCALL' \
		'BOUNDED_SECTION_POST_LABEL(.con_initcall.init, __con_initcall, _start, _end)' \
		'BOUNDED_SECTION_BY(__param, ___param)' \
		'__start_rodata = .;' \
		'__end_rodata = .;' \
		'#define COMMON_DISCARDS' \
	'*(.discard)' \
	'*(.discard.*)' \
		'*(.export_symbol)' \
		'*(.modinfo)' \
		'#define SCHED_TEXT' \
		'*(.sched.text)' \
		'#define PERCPU_INPUT(cacheline)'; do \
	grep -F -q "$$pattern" "$$generic_lds" || { echo "generic Linux linker script missing product adapter truth: $$pattern" >&2; exit 1; }; \
done; \
for pattern in \
	'__initcall_start = .; KEEP(*(.initcallearly.init))' \
	'__initcall0_start = .; KEEP(*(.initcall0.init)) KEEP(*(.initcall0s.init))' \
	'__initcall1_start = .; KEEP(*(.initcall1.init)) KEEP(*(.initcall1s.init))' \
	'__initcall2_start = .; KEEP(*(.initcall2.init)) KEEP(*(.initcall2s.init))' \
	'__initcall3_start = .; KEEP(*(.initcall3.init)) KEEP(*(.initcall3s.init))' \
	'__initcall4_start = .; KEEP(*(.initcall4.init)) KEEP(*(.initcall4s.init))' \
	'__initcall5_start = .; KEEP(*(.initcall5.init)) KEEP(*(.initcall5s.init))' \
	'__initcallrootfs_start = .; KEEP(*(.initcallrootfs.init)) KEEP(*(.initcallrootfss.init))' \
	'__initcall6_start = .; KEEP(*(.initcall6.init)) KEEP(*(.initcall6s.init))' \
	'__initcall7_start = .; KEEP(*(.initcall7.init)) KEEP(*(.initcall7s.init)) __initcall_end = .;'; do \
	grep -F -q "$$pattern" "$$lds" || { echo "generated Orlix linker script missing initcall ordering truth: $$pattern" >&2; exit 1; }; \
done; \
section_map="$(ORLIX_PRODUCT_ADAPTER_ROOT)/section-map.tsv"; \
{ \
	printf '%s\t%s\t%s\t%s\n' 'linux_section' 'mach_o_projection' 'truth' 'status'; \
	printf '%s\t%s\t%s\t%s\n' '.init.text' '__TEXT,__init_text' 'include/linux/init.h,vmlinux.lds' 'accepted'; \
	printf '%s\t%s\t%s\t%s\n' '.init.data' '__DATA,__init_data' 'include/linux/init.h,vmlinux.lds' 'accepted'; \
	printf '%s\t%s\t%s\t%s\n' '.init.rodata' '__TEXT,__init_rodata' 'include/linux/init.h,vmlinux.lds' 'accepted'; \
	printf '%s\t%s\t%s\t%s\n' '.ref.text' '__TEXT,__ref_text' 'include/linux/init.h,vmlinux.lds' 'accepted'; \
	printf '%s\t%s\t%s\t%s\n' '.sched.text' '__TEXT,__sched_text' 'include/linux/sched/debug.h,vmlinux.lds' 'accepted'; \
	printf '%s\t%s\t%s\t%s\n' '.init.setup' '__DATA,__init_setup' 'include/linux/init.h,vmlinux.lds' 'accepted'; \
	printf '%s\t%s\t%s\t%s\n' '.con_initcall.init' '__DATA,__con_initcall' 'CON_INITCALL' 'accepted'; \
	printf '%s\t%s\t%s\t%s\n' '__param' '__DATA,__param' 'include/linux/moduleparam.h,vmlinux.lds' 'accepted'; \
	printf '%s\t%s\t%s\t%s\n' '.data.once' '__DATA,__data_once' 'include/linux/once_lite.h,vmlinux.lds' 'accepted'; \
	printf '%s\t%s\t%s\t%s\n' '.data..ro_after_init' '__DATA,__ro_after_init' 'include/linux/cache.h,vmlinux.lds' 'accepted'; \
	printf '%s\t%s\t%s\t%s\n' '.discard.addressable' '__DATA,__discard_addr' 'include/linux/compiler.h,COMMON_DISCARDS' 'accepted'; \
	printf '%s\t%s\t%s\t%s\n' '.modinfo' '__DATA,__modinfo' 'include/linux/moduleparam.h,COMMON_DISCARDS' 'accepted'; \
	printf '%s\t%s\t%s\t%s\n' '.ref.data' '__DATA,__ref_data' 'include/linux/init.h,vmlinux.lds,current mm/memblock.c inventory' 'current-need'; \
	printf '%s\t%s\t%s\t%s\n' '.initcallearly.init' '__DATA,__initcall_e' 'INIT_CALLS' 'accepted'; \
	printf '%s\t%s\t%s\t%s\n' '.initcall0.init' '__DATA,__initcall0' 'INIT_CALLS_LEVEL(0)' 'accepted'; \
	printf '%s\t%s\t%s\t%s\n' '.initcall0s.init' '__DATA,__initcall0s' 'INIT_CALLS_LEVEL(0)' 'accepted'; \
	printf '%s\t%s\t%s\t%s\n' '.initcall1.init' '__DATA,__initcall1' 'INIT_CALLS_LEVEL(1)' 'accepted'; \
	printf '%s\t%s\t%s\t%s\n' '.initcall1s.init' '__DATA,__initcall1s' 'INIT_CALLS_LEVEL(1)' 'accepted'; \
	printf '%s\t%s\t%s\t%s\n' '.initcall2.init' '__DATA,__initcall2' 'INIT_CALLS_LEVEL(2)' 'accepted'; \
	printf '%s\t%s\t%s\t%s\n' '.initcall2s.init' '__DATA,__initcall2s' 'INIT_CALLS_LEVEL(2)' 'accepted'; \
	printf '%s\t%s\t%s\t%s\n' '.initcall3.init' '__DATA,__initcall3' 'INIT_CALLS_LEVEL(3)' 'accepted'; \
	printf '%s\t%s\t%s\t%s\n' '.initcall3s.init' '__DATA,__initcall3s' 'INIT_CALLS_LEVEL(3)' 'accepted'; \
	printf '%s\t%s\t%s\t%s\n' '.initcall4.init' '__DATA,__initcall4' 'INIT_CALLS_LEVEL(4)' 'accepted'; \
	printf '%s\t%s\t%s\t%s\n' '.initcall4s.init' '__DATA,__initcall4s' 'INIT_CALLS_LEVEL(4)' 'accepted'; \
	printf '%s\t%s\t%s\t%s\n' '.initcall5.init' '__DATA,__initcall5' 'INIT_CALLS_LEVEL(5)' 'accepted'; \
	printf '%s\t%s\t%s\t%s\n' '.initcall5s.init' '__DATA,__initcall5s' 'INIT_CALLS_LEVEL(5)' 'accepted'; \
	printf '%s\t%s\t%s\t%s\n' '.initcallrootfs.init' '__DATA,__initcallrf' 'INIT_CALLS_LEVEL(rootfs)' 'accepted'; \
	printf '%s\t%s\t%s\t%s\n' '.initcallrootfss.init' '__DATA,__initcallrfs' 'INIT_CALLS_LEVEL(rootfs)' 'accepted'; \
	printf '%s\t%s\t%s\t%s\n' '.initcall6.init' '__DATA,__initcall6' 'INIT_CALLS_LEVEL(6)' 'accepted'; \
	printf '%s\t%s\t%s\t%s\n' '.initcall6s.init' '__DATA,__initcall6s' 'INIT_CALLS_LEVEL(6)' 'accepted'; \
	printf '%s\t%s\t%s\t%s\n' '.initcall7.init' '__DATA,__initcall7' 'INIT_CALLS_LEVEL(7)' 'accepted'; \
	printf '%s\t%s\t%s\t%s\n' '.initcall7s.init' '__DATA,__initcall7s' 'INIT_CALLS_LEVEL(7)' 'accepted'; \
} > "$$section_map"; \
awk -F '\t' 'NR == 1 { next } { split($$2, projection, ","); if (length(projection[1]) > 16 || length(projection[2]) > 16) exit 1; count++ } END { exit count > 0 ? 0 : 1 }' "$$section_map" || { echo "product section map contains an invalid Mach-O segment or section spelling" >&2; exit 1; }; \
printf '%s\t%s\t%s\t%s\n' 'platform' 'object' 'segment' 'section' > "$(ORLIX_PRODUCT_ADAPTER_ROOT)/object-section-inventory.tsv"; \
echo "validated Orlix product adapter Linux truth: $$lds"
endef

define orlix_product_adapter_generate_headers
command -v perl >/dev/null 2>&1 || { echo "perl is required to generate the Orlix product compile adapter" >&2; exit 1; }; \
validate_line_once() { \
	file="$$1"; \
	line="$$2"; \
	count="$$(grep -F -x -c "$$line" "$$file")"; \
	if [ "$$count" != 1 ]; then echo "expected one exact product adapter validation line in $$file: $$line (found $$count)" >&2; exit 1; fi; \
	echo "validated product adapter upstream text: $$file: $$line"; \
}; \
validate_text_once() { \
	file="$$1"; \
	text="$$2"; \
	count="$$(TEXT="$$text" perl -0ne 'BEGIN { $$text = $$ENV{"TEXT"}; $$count = 0; } $$count += () = /\Q$$text\E/g; END { print $$count; }' "$$file")"; \
	if [ "$$count" != 1 ]; then echo "expected one exact product adapter validation block in $$file (found $$count)" >&2; exit 1; fi; \
	echo "validated product adapter upstream block: $$file"; \
}; \
adapt_in_place() { \
	file="$$1"; \
	from="$$2"; \
	to="$$3"; \
	count="$$(grep -F -c "$$from" "$$file")"; \
	if [ "$$count" != 1 ]; then echo "expected one product adapter match for $$from in $$file, found $$count" >&2; exit 1; fi; \
	tmp="$$file.tmp"; \
	FROM="$$from" TO="$$to" perl -0pe 'BEGIN { $$from = $$ENV{"FROM"}; $$to = $$ENV{"TO"}; } s/\Q$$from\E/$$to/g' "$$file" > "$$tmp"; \
	if grep -F -q "$$from" "$$tmp"; then echo "product adapter did not replace $$from in $$file" >&2; exit 1; fi; \
	mv "$$tmp" "$$file"; \
	echo "rewrote product adapter section: $$file: $$from -> $$to"; \
}; \
	adapt_text_in_place() { \
		file="$$1"; \
		from="$$2"; \
		to="$$3"; \
	validate_text_once "$$file" "$$from"; \
	tmp="$$file.tmp"; \
	FROM="$$from" TO="$$to" perl -0pe 'BEGIN { $$from = $$ENV{"FROM"}; $$to = $$ENV{"TO"}; } s/\Q$$from\E/$$to/g' "$$file" > "$$tmp"; \
	remaining="$$(TEXT="$$from" perl -0ne 'BEGIN { $$text = $$ENV{"TEXT"}; $$count = 0; } $$count += () = /\Q$$text\E/g; END { print $$count; }' "$$tmp")"; \
	if [ "$$remaining" != 0 ]; then echo "product adapter did not replace expected block in $$file" >&2; exit 1; fi; \
		mv "$$tmp" "$$file"; \
		echo "rewrote product adapter block: $$file"; \
	}; \
	extract_text_range_once() { \
		file="$$1"; \
		start="$$2"; \
		end="$$3"; \
		output="$$4"; \
		count="$$(START="$$start" END="$$end" perl -0ne 'BEGIN { $$start = $$ENV{"START"}; $$end = $$ENV{"END"}; $$count = 0; } while (/\Q$$start\E.*?\Q$$end\E/sg) { $$count++; } END { print $$count; }' "$$file")"; \
		if [ "$$count" != 1 ]; then echo "expected one product adapter source range in $$file (found $$count): $$end" >&2; exit 1; fi; \
		START="$$start" END="$$end" perl -0ne 'BEGIN { $$start = $$ENV{"START"}; $$end = $$ENV{"END"}; } if (/\Q$$start\E.*?\Q$$end\E/s) { print $$&; print "\n\n"; }' "$$file" >> "$$output"; \
		echo "copied product adapter upstream source range: $$file: $$end"; \
	}; \
cp "$(ORLIX_KERNEL_PORT_DIR)/include/linux/init.h" "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/init.h"; \
cp "$(ORLIX_KERNEL_PORT_DIR)/include/linux/compiler.h" "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/compiler.h"; \
cp "$(ORLIX_KERNEL_PORT_DIR)/include/linux/cache.h" "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/cache.h"; \
	cp "$(ORLIX_KERNEL_PORT_DIR)/include/linux/moduleparam.h" "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/moduleparam.h"; \
	cp "$(ORLIX_KERNEL_PORT_DIR)/include/linux/once_lite.h" "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/once_lite.h"; \
	cp "$(ORLIX_KERNEL_PORT_DIR)/include/linux/percpu-defs.h" "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/percpu-defs.h"; \
	cp "$(ORLIX_KERNEL_PORT_DIR)/include/linux/syscalls.h" "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/syscalls.h"; \
	cp "$(ORLIX_KERNEL_PORT_DIR)/include/linux/sched/debug.h" "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/sched/debug.h"; \
init_text_line=$$'#define __init\t\t__section(".init.text") __cold  __latent_entropy __noinitretpoline'; \
init_data_line=$$'#define __initdata\t__section(".init.data")'; \
init_const_line=$$'#define __initconst\t__section(".init.rodata")'; \
ref_text_line=$$'#define __ref            __section(".ref.text") noinline'; \
ref_data_line=$$'#define __refdata        __section(".ref.data")'; \
setup_boundary_line=$$'extern const struct obs_kernel_param __setup_start[], __setup_end[];'; \
setup_boundary_product_block=$$'extern const struct obs_kernel_param __setup_start[]\n\t__asm("section$$start$$__DATA$$__init_setup");\nextern const struct obs_kernel_param __setup_end[]\n\t__asm("section$$end$$__DATA$$__init_setup");'; \
init_setup_line=$$'\t\t__used __section(".init.setup")\t\t\t\t\\'; \
initcall_section_block=$$'#define __initcall_section(__sec, __iid)\t\t\t\\\n\t#__sec ".init"'; \
	initcall_section_product_block=$$'#define __orlix_product_initcall_section_early "__DATA,__initcall_e"\n#define __orlix_product_initcall_section_con "__DATA,__con_initcall"\n#define __orlix_product_initcall_section_0 "__DATA,__initcall0"\n#define __orlix_product_initcall_section_0s "__DATA,__initcall0s"\n#define __orlix_product_initcall_section_1 "__DATA,__initcall1"\n#define __orlix_product_initcall_section_1s "__DATA,__initcall1s"\n#define __orlix_product_initcall_section_2 "__DATA,__initcall2"\n#define __orlix_product_initcall_section_2s "__DATA,__initcall2s"\n#define __orlix_product_initcall_section_3 "__DATA,__initcall3"\n#define __orlix_product_initcall_section_3s "__DATA,__initcall3s"\n#define __orlix_product_initcall_section_4 "__DATA,__initcall4"\n#define __orlix_product_initcall_section_4s "__DATA,__initcall4s"\n#define __orlix_product_initcall_section_5 "__DATA,__initcall5"\n#define __orlix_product_initcall_section_5s "__DATA,__initcall5s"\n#define __orlix_product_initcall_section_rootfs "__DATA,__initcallrf"\n#define __orlix_product_initcall_section_rootfss "__DATA,__initcallrfs"\n#define __orlix_product_initcall_section_6 "__DATA,__initcall6"\n#define __orlix_product_initcall_section_6s "__DATA,__initcall6s"\n#define __orlix_product_initcall_section_7 "__DATA,__initcall7"\n#define __orlix_product_initcall_section_7s "__DATA,__initcall7s"\n#define __initcall_section(__sec, __iid) __sec'; \
	initcall_boundary_block=$$'extern initcall_entry_t __initcall_start[];\nextern initcall_entry_t __initcall0_start[];\nextern initcall_entry_t __initcall1_start[];\nextern initcall_entry_t __initcall2_start[];\nextern initcall_entry_t __initcall3_start[];\nextern initcall_entry_t __initcall4_start[];\nextern initcall_entry_t __initcall5_start[];\nextern initcall_entry_t __initcall6_start[];\nextern initcall_entry_t __initcall7_start[];\nextern initcall_entry_t __initcall_end[];'; \
	initcall_boundary_product_block=$$'extern initcall_entry_t __initcall_start[]\n\t__asm("section$$start$$__DATA$$__initcall_e");\nextern initcall_entry_t __initcall0_start[]\n\t__asm("section$$start$$__DATA$$__initcall0");\nextern initcall_entry_t __initcall1_start[]\n\t__asm("section$$start$$__DATA$$__initcall1");\nextern initcall_entry_t __initcall2_start[]\n\t__asm("section$$start$$__DATA$$__initcall2");\nextern initcall_entry_t __initcall3_start[]\n\t__asm("section$$start$$__DATA$$__initcall3");\nextern initcall_entry_t __initcall4_start[]\n\t__asm("section$$start$$__DATA$$__initcall4");\nextern initcall_entry_t __initcall5_start[]\n\t__asm("section$$start$$__DATA$$__initcall5");\nextern initcall_entry_t __initcallrootfs_start[]\n\t__asm("section$$start$$__DATA$$__initcallrf");\nextern initcall_entry_t __initcall6_start[]\n\t__asm("section$$start$$__DATA$$__initcall6");\nextern initcall_entry_t __initcall7_start[]\n\t__asm("section$$start$$__DATA$$__initcall7");\nextern initcall_entry_t __initcall_end[]\n\t__asm("section$$end$$__DATA$$__initcall7s");'; \
	console_initcall_line=$$'#define console_initcall(fn)\t___define_initcall(fn, con, .con_initcall)'; \
	con_initcall_boundary_line=$$'extern initcall_entry_t __con_initcall_start[], __con_initcall_end[];'; \
	con_initcall_boundary_product_block=$$'extern initcall_entry_t __con_initcall_start[]\n\t__asm("section$$start$$__DATA$$__con_initcall");\nextern initcall_entry_t __con_initcall_end[]\n\t__asm("section$$end$$__DATA$$__con_initcall");'; \
compiler_addressable_block=$$'#define __ADDRESSABLE(sym) \\\n\t___ADDRESSABLE(sym, __section(".discard.addressable"))'; \
ro_after_init_line=$$'#define __ro_after_init __section(".data..ro_after_init")'; \
module_param_boundary_line=$$'extern const struct kernel_param __start___param[], __stop___param[];'; \
module_param_boundary_product_block=$$'extern const struct kernel_param __start___param[]\n\t__asm("section$$start$$__DATA$$__param");\nextern const struct kernel_param __stop___param[]\n\t__asm("section$$end$$__DATA$$__param");'; \
	module_param_line=$$'\t__used __section("__param")\t\t\t\t\t\\'; \
	once_data_line=$$'\t\tstatic bool __section(".data.once") __already_done;\t\\'; \
	internal_once_data_line=$$'\tstatic bool __section(".data.once") __warned;\t\t\t\\'; \
	sched_text_line=$$'#define __sched\t\t__section(".sched.text")'; \
	syscall_cast_line=$$'#define __SC_CAST(t, a)\t(__force t) a'; \
	syscall_cast_product_block=$$'#define __SC_CAST(t, a) (__force t) a\n#define __SC_LONG_CAST(t, a) (__force __typeof(__builtin_choose_expr(__TYPE_IS_LL(t), 0LL, 0L))) a'; \
	syscall_define_block=$$'#ifndef __SYSCALL_DEFINEx\n#define __SYSCALL_DEFINEx(x, name, ...)\t\t\t\t\t\\\n\t__diag_push();\t\t\t\t\t\t\t\\\n\t__diag_ignore(GCC, 8, "-Wattribute-alias",\t\t\t\\\n\t\t      "Type aliasing is used to sanitize syscall arguments");\\\n\tasmlinkage long sys##name(__MAP(x,__SC_DECL,__VA_ARGS__))\t\\\n\t\t__attribute__((alias(__stringify(__se_sys##name))));\t\\\n\tALLOW_ERROR_INJECTION(sys##name, ERRNO);\t\t\t\\\n\tstatic inline long __do_sys##name(__MAP(x,__SC_DECL,__VA_ARGS__));\\\n\tasmlinkage long __se_sys##name(__MAP(x,__SC_LONG,__VA_ARGS__));\t\\\n\tasmlinkage long __se_sys##name(__MAP(x,__SC_LONG,__VA_ARGS__))\t\\\n\t{\t\t\t\t\t\t\t\t\\\n\t\tlong ret = __do_sys##name(__MAP(x,__SC_CAST,__VA_ARGS__));\\\n\t\t__MAP(x,__SC_TEST,__VA_ARGS__);\t\t\t\t\\\n\t\t__PROTECT(x, ret,__MAP(x,__SC_ARGS,__VA_ARGS__));\t\\\n\t\treturn ret;\t\t\t\t\t\t\\\n\t}\t\t\t\t\t\t\t\t\\\n\t__diag_pop();\t\t\t\t\t\t\t\\\n\tstatic inline long __do_sys##name(__MAP(x,__SC_DECL,__VA_ARGS__))\n#endif /* __SYSCALL_DEFINEx */'; \
	syscall_define_product_block=$$'#ifndef __SYSCALL_DEFINEx\n#define __SYSCALL_DEFINEx(x, name, ...)\t\t\t\t\t\\\n\t__diag_push();\t\t\t\t\t\t\t\\\n\t__diag_ignore(GCC, 8, "-Wattribute-alias",\t\t\t\\\n\t\t      "Type aliasing is used to sanitize syscall arguments");\\\n\tasmlinkage long sys##name(__MAP(x,__SC_DECL,__VA_ARGS__));\t\\\n\tALLOW_ERROR_INJECTION(sys##name, ERRNO);\t\t\t\\\n\tstatic inline long __do_sys##name(__MAP(x,__SC_DECL,__VA_ARGS__));\\\n\tasmlinkage long __se_sys##name(__MAP(x,__SC_LONG,__VA_ARGS__));\t\\\n\tasmlinkage long sys##name(__MAP(x,__SC_DECL,__VA_ARGS__))\t\\\n\t{\t\t\t\t\t\t\t\t\\\n\t\treturn __se_sys##name(__MAP(x,__SC_LONG_CAST,__VA_ARGS__));\\\n\t}\t\t\t\t\t\t\t\t\\\n\tasmlinkage long __se_sys##name(__MAP(x,__SC_LONG,__VA_ARGS__))\t\\\n\t{\t\t\t\t\t\t\t\t\\\n\t\tlong ret = __do_sys##name(__MAP(x,__SC_CAST,__VA_ARGS__));\\\n\t\t__MAP(x,__SC_TEST,__VA_ARGS__);\t\t\t\t\\\n\t\t__PROTECT(x, ret,__MAP(x,__SC_ARGS,__VA_ARGS__));\t\\\n\t\treturn ret;\t\t\t\t\t\t\\\n\t}\t\t\t\t\t\t\t\t\\\n\t__diag_pop();\t\t\t\t\t\t\t\\\n\tstatic inline long __do_sys##name(__MAP(x,__SC_DECL,__VA_ARGS__))\n#endif /* __SYSCALL_DEFINEx */'; \
	percpu_nonsmp_aligned_block=$$'#else\n\n#define PER_CPU_SHARED_ALIGNED_SECTION ""\n#define PER_CPU_ALIGNED_SECTION "..shared_aligned"\n#define PER_CPU_FIRST_SECTION ""\n\n#endif'; \
percpu_nonsmp_product_block=$$'#else\n\n#define PER_CPU_SHARED_ALIGNED_SECTION ""\n#define PER_CPU_ALIGNED_SECTION ""\n#define PER_CPU_FIRST_SECTION ""\n\n#endif'; \
validate_line_once "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/init.h" "$$init_text_line"; \
validate_line_once "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/init.h" "$$init_data_line"; \
validate_line_once "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/init.h" "$$init_const_line"; \
validate_line_once "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/init.h" "$$ref_text_line"; \
validate_line_once "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/init.h" "$$ref_data_line"; \
validate_line_once "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/init.h" "$$setup_boundary_line"; \
validate_line_once "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/init.h" "$$init_setup_line"; \
validate_text_once "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/init.h" "$$initcall_section_block"; \
	validate_line_once "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/init.h" '#define __define_initcall(fn, id) ___define_initcall(fn, id, .initcall##id)'; \
	validate_text_once "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/init.h" "$$initcall_boundary_block"; \
	validate_line_once "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/init.h" "$$console_initcall_line"; \
	validate_line_once "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/init.h" "$$con_initcall_boundary_line"; \
validate_text_once "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/compiler.h" "$$compiler_addressable_block"; \
validate_line_once "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/cache.h" "$$ro_after_init_line"; \
validate_line_once "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/moduleparam.h" "$$module_param_boundary_line"; \
	validate_line_once "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/moduleparam.h" "$$module_param_line"; \
	validate_line_once "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/once_lite.h" "$$once_data_line"; \
	validate_line_once "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/sched/debug.h" "$$sched_text_line"; \
	validate_line_once "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/syscalls.h" "$$syscall_cast_line"; \
	validate_text_once "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/syscalls.h" "$$syscall_define_block"; \
	validate_text_once "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/percpu-defs.h" "$$percpu_nonsmp_aligned_block"; \
adapt_in_place "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/init.h" '__section(".init.text")' '__section("__TEXT,__init_text")'; \
adapt_in_place "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/init.h" '__section(".init.data")' '__section("__DATA,__init_data")'; \
adapt_in_place "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/init.h" '__section(".init.rodata")' '__section("__TEXT,__init_rodata")'; \
adapt_in_place "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/init.h" '__section(".ref.text")' '__section("__TEXT,__ref_text")'; \
adapt_in_place "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/init.h" '__section(".ref.data")' '__section("__DATA,__ref_data")'; \
adapt_text_in_place "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/init.h" "$$setup_boundary_line" "$$setup_boundary_product_block"; \
adapt_in_place "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/init.h" '__section(".init.setup")' '__section("__DATA,__init_setup")'; \
	adapt_text_in_place "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/init.h" "$$initcall_section_block" "$$initcall_section_product_block"; \
	adapt_in_place "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/init.h" '.initcall##id' '__orlix_product_initcall_section_##id'; \
	adapt_in_place "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/init.h" '.con_initcall' '__orlix_product_initcall_section_con'; \
	adapt_text_in_place "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/init.h" "$$initcall_boundary_block" "$$initcall_boundary_product_block"; \
	adapt_text_in_place "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/init.h" "$$con_initcall_boundary_line" "$$con_initcall_boundary_product_block"; \
adapt_in_place "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/compiler.h" '__section(".discard.addressable")' '__section("__DATA,__discard_addr")'; \
adapt_in_place "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/cache.h" '__section(".data..ro_after_init")' '__section("__DATA,__ro_after_init")'; \
adapt_text_in_place "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/moduleparam.h" "$$module_param_boundary_line" "$$module_param_boundary_product_block"; \
adapt_in_place "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/moduleparam.h" '__section("__param")' '__section("__DATA,__param")'; \
	adapt_in_place "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/moduleparam.h" '__section(".modinfo")' '__section("__DATA,__modinfo")'; \
	adapt_in_place "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/once_lite.h" '__section(".data.once")' '__section("__DATA,__data_once")'; \
	adapt_in_place "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/sched/debug.h" '__section(".sched.text")' '__section("__TEXT,__sched_text")'; \
	adapt_in_place "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/syscalls.h" "$$syscall_cast_line" "$$syscall_cast_product_block"; \
	adapt_text_in_place "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/syscalls.h" "$$syscall_define_block" "$$syscall_define_product_block"; \
adapt_text_in_place "$(ORLIX_PRODUCT_ADAPTER_INCLUDE)/linux/percpu-defs.h" "$$percpu_nonsmp_aligned_block" "$$percpu_nonsmp_product_block"; \
cp "$(ORLIX_KERNEL_PORT_DIR)/mm/page_alloc.c" "$(ORLIX_PRODUCT_ADAPTER_SOURCE)/mm/page_alloc.c"; \
cp "$(ORLIX_KERNEL_PORT_DIR)/mm/internal.h" "$(ORLIX_PRODUCT_ADAPTER_SOURCE)/mm/internal.h"; \
cp "$(ORLIX_KERNEL_PORT_DIR)/mm/shuffle.h" "$(ORLIX_PRODUCT_ADAPTER_SOURCE)/mm/shuffle.h"; \
cp "$(ORLIX_KERNEL_PORT_DIR)/mm/page_reporting.h" "$(ORLIX_PRODUCT_ADAPTER_SOURCE)/mm/page_reporting.h"; \
cp "$(ORLIX_KERNEL_PORT_DIR)/mm/vma.h" "$(ORLIX_PRODUCT_ADAPTER_SOURCE)/mm/vma.h"; \
	cp "$(ORLIX_KERNEL_PORT_DIR)/drivers/base/trace.h" "$(ORLIX_PRODUCT_ADAPTER_SOURCE)/drivers/base/trace.h"; \
	devres_src="$(ORLIX_PRODUCT_ADAPTER_SOURCE)/drivers/base/devres.c"; \
	devres_upstream="$(ORLIX_KERNEL_PORT_DIR)/drivers/base/devres.c"; \
	{ \
		printf '%s\n' '// SPDX-License-Identifier: GPL-2.0'; \
		printf '%s\n' '/* generated Build-only Mach-O product source from validated upstream drivers/base/devres.c helper ranges */'; \
		printf '%s\n' '#include <linux/device.h>'; \
		printf '%s\n' '#include <linux/err.h>'; \
		printf '%s\n' '#include <linux/export.h>'; \
		printf '%s\n' '#include <linux/overflow.h>'; \
		printf '%s\n' '#include <linux/slab.h>'; \
		printf '%s\n' '#include "trace.h"'; \
		printf '\n'; \
	} > "$$devres_src"; \
	seq_file_src="$(ORLIX_PRODUCT_ADAPTER_SOURCE)/fs/seq_file.c"; \
	seq_file_upstream="$(ORLIX_KERNEL_PORT_DIR)/fs/seq_file.c"; \
	{ \
		printf '%s\n' '// SPDX-License-Identifier: GPL-2.0'; \
		printf '%s\n' '/* generated Build-only Mach-O product source from validated upstream fs/seq_file.c helper ranges */'; \
		printf '%s\n' '#include <linux/export.h>'; \
		printf '%s\n' '#include <linux/seq_file.h>'; \
		printf '%s\n' '#include <linux/string.h>'; \
		printf '\n'; \
	} > "$$seq_file_src"; \
	cpu_src="$(ORLIX_PRODUCT_ADAPTER_SOURCE)/kernel/cpu.c"; \
	cpu_upstream="$(ORLIX_KERNEL_PORT_DIR)/kernel/cpu.c"; \
	{ \
		printf '%s\n' '// SPDX-License-Identifier: GPL-2.0-only'; \
		printf '%s\n' '/* generated Build-only Mach-O product source from validated upstream kernel/cpu.c ranges */'; \
		printf '%s\n' '#include <linux/cache.h>'; \
		printf '%s\n' '#include <linux/bug.h>'; \
		printf '%s\n' '#include <linux/cpu.h>'; \
		printf '%s\n' '#include <linux/cpumask.h>'; \
		printf '%s\n' '#include <linux/errno.h>'; \
		printf '%s\n' '#include <linux/export.h>'; \
		printf '%s\n' '#include <linux/list.h>'; \
		printf '%s\n' '#include <linux/lockdep.h>'; \
		printf '%s\n' '#include <linux/mutex.h>'; \
		printf '%s\n' '#include <linux/percpu.h>'; \
		printf '%s\n' '#include <linux/smp.h>'; \
		printf '%s\n' '#include <trace/events/cpuhp.h>'; \
		printf '\n'; \
	} > "$$cpu_src"; \
	cp "$(ORLIX_KERNEL_PORT_DIR)/kernel/locking/mutex.h" "$(ORLIX_PRODUCT_ADAPTER_SOURCE)/kernel/locking/mutex.h"; \
	mutex_src="$(ORLIX_PRODUCT_ADAPTER_SOURCE)/kernel/locking/mutex.c"; \
	mutex_upstream="$(ORLIX_KERNEL_PORT_DIR)/kernel/locking/mutex.c"; \
	{ \
		printf '%s\n' '// SPDX-License-Identifier: GPL-2.0-only'; \
		printf '%s\n' '/* generated Build-only Mach-O product source from validated upstream kernel/locking/mutex.c helper range */'; \
		printf '%s\n' '#include <linux/mutex.h>'; \
		printf '%s\n' '#include <linux/spinlock.h>'; \
		printf '%s\n' '#include <linux/export.h>'; \
		printf '%s\n' '#include "mutex.h"'; \
		printf '\n'; \
	} > "$$mutex_src"; \
	swait_src="$(ORLIX_PRODUCT_ADAPTER_SOURCE)/kernel/sched/swait.c"; \
	swait_upstream="$(ORLIX_KERNEL_PORT_DIR)/kernel/sched/swait.c"; \
	sched_private_header="$(ORLIX_KERNEL_PORT_DIR)/kernel/sched/sched.h"; \
	try_to_wake_up_decl='extern int try_to_wake_up(struct task_struct *tsk, unsigned int state, int wake_flags);'; \
	prepare_to_swait_decl='extern void __prepare_to_swait(struct swait_queue_head *q, struct swait_queue *wait);'; \
	validate_line_once "$$sched_private_header" "$$try_to_wake_up_decl"; \
	validate_line_once "$$sched_private_header" "$$prepare_to_swait_decl"; \
	{ \
		printf '%s\n' '// SPDX-License-Identifier: GPL-2.0'; \
		printf '%s\n' '/* generated Build-only Mach-O product source from validated upstream kernel/sched/swait.c ranges */'; \
		printf '%s\n' '#include <linux/errno.h>'; \
		printf '%s\n' '#include <linux/export.h>'; \
		printf '%s\n' '#include <linux/sched.h>'; \
		printf '%s\n' '#include <linux/sched/signal.h>'; \
		printf '%s\n' '#include <linux/spinlock.h>'; \
		printf '%s\n' '#include <linux/swait.h>'; \
		printf '\n'; \
		printf '%s\n' "$$try_to_wake_up_decl"; \
		printf '\n'; \
	} > "$$swait_src"; \
	completion_src="$(ORLIX_PRODUCT_ADAPTER_SOURCE)/kernel/sched/completion.c"; \
	completion_upstream="$(ORLIX_KERNEL_PORT_DIR)/kernel/sched/completion.c"; \
	{ \
		printf '%s\n' '// SPDX-License-Identifier: GPL-2.0'; \
		printf '%s\n' '/* generated Build-only Mach-O product source from validated upstream kernel/sched/completion.c ranges */'; \
		printf '%s\n' '#include <linux/completion.h>'; \
		printf '%s\n' '#include <linux/errno.h>'; \
		printf '%s\n' '#include <linux/export.h>'; \
		printf '%s\n' '#include <linux/jiffies.h>'; \
		printf '%s\n' '#include <linux/sched.h>'; \
		printf '%s\n' '#include <linux/sched/debug.h>'; \
		printf '%s\n' '#include <linux/sched/signal.h>'; \
		printf '%s\n' '#include <linux/spinlock.h>'; \
		printf '%s\n' '#include <linux/swait.h>'; \
		printf '\n'; \
		printf '%s\n' "$$prepare_to_swait_decl"; \
		printf '\n'; \
	} > "$$completion_src"; \
	rcu_update_src="$(ORLIX_PRODUCT_ADAPTER_SOURCE)/kernel/rcu/update.c"; \
	rcu_update_upstream="$(ORLIX_KERNEL_PORT_DIR)/kernel/rcu/update.c"; \
	{ \
		printf '%s\n' '// SPDX-License-Identifier: GPL-2.0+'; \
		printf '%s\n' '/* generated Build-only Mach-O product source from validated upstream kernel/rcu/update.c range */'; \
		printf '%s\n' '#include <linux/export.h>'; \
		printf '%s\n' '#include <linux/rcupdate_wait.h>'; \
		printf '\n'; \
	} > "$$rcu_update_src"; \
	softirq_src="$(ORLIX_PRODUCT_ADAPTER_SOURCE)/kernel/softirq.c"; \
	softirq_upstream="$(ORLIX_KERNEL_PORT_DIR)/kernel/softirq.c"; \
	{ \
		printf '%s\n' '// SPDX-License-Identifier: GPL-2.0-only'; \
		printf '%s\n' '/* generated Build-only Mach-O product source from validated upstream kernel/softirq.c ranges */'; \
		printf '%s\n' '#include <linux/bottom_half.h>'; \
		printf '%s\n' '#include <linux/bug.h>'; \
		printf '%s\n' '#include <linux/export.h>'; \
		printf '%s\n' '#include <linux/ftrace.h>'; \
		printf '%s\n' '#include <linux/hardirq.h>'; \
		printf '%s\n' '#include <linux/interrupt.h>'; \
		printf '%s\n' '#include <linux/irqflags.h>'; \
		printf '%s\n' '#include <linux/kernel_stat.h>'; \
		printf '%s\n' '#include <linux/lockdep.h>'; \
		printf '%s\n' '#include <linux/percpu.h>'; \
		printf '%s\n' '#include <linux/preempt.h>'; \
		printf '\n'; \
	} > "$$softirq_src"; \
	panic_src="$(ORLIX_PRODUCT_ADAPTER_SOURCE)/kernel/panic.c"; \
	panic_upstream="$(ORLIX_KERNEL_PORT_DIR)/kernel/panic.c"; \
	{ \
		printf '%s\n' '// SPDX-License-Identifier: GPL-2.0-only'; \
		printf '%s\n' '/* generated Build-only Mach-O product source from validated upstream kernel/panic.c ranges */'; \
		printf '%s\n' '#include <linux/bug.h>'; \
		printf '%s\n' '#include <linux/context_tracking.h>'; \
		printf '%s\n' '#include <linux/export.h>'; \
		printf '%s\n' '#include <linux/printk.h>'; \
		printf '%s\n' '#include <linux/stdarg.h>'; \
		printf '\n'; \
	} > "$$panic_src"; \
	time_src="$(ORLIX_PRODUCT_ADAPTER_SOURCE)/kernel/time/time.c"; \
	time_upstream="$(ORLIX_KERNEL_PORT_DIR)/kernel/time/time.c"; \
	{ \
		printf '%s\n' '// SPDX-License-Identifier: GPL-2.0'; \
		printf '%s\n' '/* generated Build-only Mach-O product source from validated upstream kernel/time/time.c helper range */'; \
		printf '%s\n' '#include <linux/jiffies.h>'; \
		printf '%s\n' '#include <linux/export.h>'; \
		printf '\n'; \
	} > "$$time_src"; \
	util_src="$(ORLIX_PRODUCT_ADAPTER_SOURCE)/mm/util.c"; \
	util_upstream="$(ORLIX_KERNEL_PORT_DIR)/mm/util.c"; \
	{ \
		printf '%s\n' '// SPDX-License-Identifier: GPL-2.0-only'; \
		printf '%s\n' '/* generated Build-only Mach-O product source from validated upstream mm/util.c helper ranges */'; \
		printf '%s\n' '#include <linux/types.h>'; \
		printf '%s\n' '#include <linux/mm.h>'; \
		printf '%s\n' '#include <linux/slab.h>'; \
		printf '%s\n' '#include <linux/string.h>'; \
		printf '%s\n' '#include <linux/compiler.h>'; \
		printf '%s\n' '#include <linux/export.h>'; \
		printf '%s\n' '#include <linux/err.h>'; \
		printf '%s\n' '#include <linux/vmalloc.h>'; \
		printf '%s\n' '#include <linux/uaccess.h>'; \
		printf '\n'; \
		printf '%s\n' 'extern char __orlix_product_text_const_start[] __asm("section$$start$$__TEXT$$__const");'; \
		printf '%s\n' 'extern char __orlix_product_text_const_end[] __asm("section$$end$$__TEXT$$__const");'; \
		printf '%s\n' 'extern char __orlix_product_cstring_start[] __asm("section$$start$$__TEXT$$__cstring");'; \
		printf '%s\n' 'extern char __orlix_product_cstring_end[] __asm("section$$end$$__TEXT$$__cstring");'; \
		printf '%s\n' 'extern char __orlix_product_literal16_start[] __asm("section$$start$$__TEXT$$__literal16");'; \
		printf '%s\n' 'extern char __orlix_product_literal16_end[] __asm("section$$end$$__TEXT$$__literal16");'; \
		printf '%s\n' 'extern char __orlix_product_data_const_start[] __asm("section$$start$$__DATA$$__const");'; \
		printf '%s\n' 'extern char __orlix_product_data_const_end[] __asm("section$$end$$__DATA$$__const");'; \
		printf '\n'; \
		printf '%s\n' 'static inline bool __orlix_product_in_rodata_range(unsigned long addr, char *start, char *end)'; \
		printf '%s\n' '{'; \
		printf '%s\n' '    return addr >= (unsigned long)start && addr < (unsigned long)end;'; \
		printf '%s\n' '}'; \
		printf '\n'; \
		printf '%s\n' 'static inline bool is_kernel_rodata(unsigned long addr)'; \
		printf '%s\n' '{'; \
		printf '%s\n' '    return __orlix_product_in_rodata_range(addr, __orlix_product_text_const_start, __orlix_product_text_const_end) ||'; \
		printf '%s\n' '           __orlix_product_in_rodata_range(addr, __orlix_product_cstring_start, __orlix_product_cstring_end) ||'; \
		printf '%s\n' '           __orlix_product_in_rodata_range(addr, __orlix_product_literal16_start, __orlix_product_literal16_end) ||'; \
		printf '%s\n' '           __orlix_product_in_rodata_range(addr, __orlix_product_data_const_start, __orlix_product_data_const_end);'; \
		printf '%s\n' '}'; \
		printf '\n'; \
	} > "$$util_src"; \
	maccess_src="$(ORLIX_PRODUCT_ADAPTER_SOURCE)/mm/maccess.c"; \
	maccess_upstream="$(ORLIX_KERNEL_PORT_DIR)/mm/maccess.c"; \
	{ \
		printf '%s\n' '// SPDX-License-Identifier: GPL-2.0-only'; \
		printf '%s\n' '/* generated Build-only Mach-O product source from validated upstream mm/maccess.c helper range */'; \
		printf '%s\n' '#include <linux/bug.h>'; \
		printf '%s\n' '#include <linux/export.h>'; \
		printf '\n'; \
	} > "$$maccess_src"; \
	mmzone_src="$(ORLIX_PRODUCT_ADAPTER_SOURCE)/mm/mmzone.c"; \
	mmzone_upstream="$(ORLIX_KERNEL_PORT_DIR)/mm/mmzone.c"; \
	{ \
		printf '%s\n' '// SPDX-License-Identifier: GPL-2.0'; \
		printf '%s\n' '/* generated Build-only Mach-O product source from validated upstream mm/mmzone.c helper range */'; \
		printf '%s\n' '#include <linux/stddef.h>'; \
		printf '%s\n' '#include <linux/mm.h>'; \
		printf '%s\n' '#include <linux/mmzone.h>'; \
		printf '\n'; \
	} > "$$mmzone_src"; \
	swap_src="$(ORLIX_PRODUCT_ADAPTER_SOURCE)/mm/swap.c"; \
	swap_upstream="$(ORLIX_KERNEL_PORT_DIR)/mm/swap.c"; \
	{ \
		printf '%s\n' '// SPDX-License-Identifier: GPL-2.0-only'; \
		printf '%s\n' '/* generated Build-only Mach-O product source from validated upstream mm/swap.c helper range */'; \
		printf '%s\n' '#include <linux/mm.h>'; \
		printf '%s\n' '#include <linux/export.h>'; \
		printf '%s\n' '#include <linux/mm_inline.h>'; \
		printf '%s\n' '#include <linux/memremap.h>'; \
		printf '%s\n' '#include <linux/memcontrol.h>'; \
		printf '%s\n' '#include <linux/hugetlb.h>'; \
		printf '%s\n' '#include "internal.h"'; \
		printf '\n'; \
	} > "$$swap_src"; \
	kfree_const_start=$$'/**\n * kfree_const - conditionally free memory'; \
	kstrdup_start=$$'/**\n * kstrdup - allocate space for and copy an existing string'; \
	kstrdup_const_start=$$'/**\n * kstrdup_const - conditionally duplicate an existing const string'; \
	memdup_user_nul_start=$$'/**\n * memdup_user_nul - duplicate memory region from user space and NUL-terminate'; \
	kmalloc_gfp_adjust_start='static gfp_t kmalloc_gfp_adjust(gfp_t flags, size_t size)'; \
	kmalloc_gfp_adjust_end=$$'\treturn flags;\n}'; \
	kvmalloc_node_start=$$'/**\n * __kvmalloc_node - attempt to allocate physically contiguous memory'; \
	devres_node_start='struct devres_node {'; \
	devres_start='struct devres {'; \
	devres_dbg_start='#ifdef CONFIG_DEBUG_DEVRES'; \
	devres_log_start='static void devres_log(struct device *dev, struct devres_node *node,'; \
	check_dr_size_start='static bool check_dr_size(size_t size, size_t *tot_size)'; \
	alloc_dr_start='static __always_inline struct devres *alloc_dr(dr_release_t release,'; \
	add_dr_start='static void add_dr(struct device *dev, struct devres_node *node)'; \
	devres_alloc_start=$$'/**\n * __devres_alloc_node - Allocate device resource data'; \
	devres_free_start=$$'/**\n * devres_free - Free device resource data'; \
	devres_add_start=$$'/**\n * devres_add - Register device resource'; \
	action_devres_start='struct action_devres {'; \
	devm_action_release_start='static void devm_action_release(struct device *dev, void *res)'; \
	devm_add_action_start=$$'/**\n * __devm_add_action() - add a custom action to list of managed resources'; \
	seq_set_overflow_start='static void seq_set_overflow(struct seq_file *m)'; \
	seq_puts_start='void __seq_puts(struct seq_file *m, const char *s)'; \
	seq_write_start=$$'/**\n * seq_write - write arbitrary data to buffer'; \
	cpuhp_cpu_state_start=$$'/**\n * struct cpuhp_cpu_state - Per cpu hotplug state storage'; \
	cpuhp_step_start=$$'/**\n * struct cpuhp_step - Hotplug state machine step'; \
	cpuhp_step_empty_end=$$'static bool cpuhp_step_empty(bool bringup, struct cpuhp_step *step)\n{\n\treturn bringup ? !step->startup.single : !step->teardown.single;\n}'; \
	cpuhp_invoke_callback_start=$$'/**\n * cpuhp_invoke_callback - Invoke the callbacks for a given state'; \
	cpuhp_invoke_callback_end=$$'\treturn ret;\n}'; \
	cpuhp_hp_states_start='/* Boot processor state steps */'; \
	cpuhp_cb_check_start='/* Sanity check for callbacks */'; \
	cpuhp_reserve_state_start=$$'/*\n * Returns a free for dynamic slot assignment of the Online state.'; \
	cpuhp_reserve_state_end=$$'\tWARN(1, "No more dynamic states available for CPU hotplug\\n");\n\treturn -ENOSPC;\n}'; \
	cpuhp_store_callbacks_start='static int cpuhp_store_callbacks(enum cpuhp_state state, const char *name,'; \
	cpuhp_store_callbacks_end=$$'\tINIT_HLIST_HEAD(&sp->list);\n\treturn ret;\n}'; \
	cpuhp_issue_call_start=$$'/*\n * Call the startup/teardown function for a step either on the AP or'; \
	cpuhp_issue_call_end=$$'\treturn ret;\n}'; \
	cpuhp_rollback_install_start=$$'/*\n * Called from __cpuhp_setup_state on a recoverable failure.'; \
	cpuhp_rollback_install_end=$$'\t\tif (cpustate >= state)\n\t\t\tcpuhp_issue_call(cpu, state, false, node);\n\t}\n}'; \
	cpuhp_setup_state_start=$$'/**\n * __cpuhp_setup_state_cpuslocked - Setup the callbacks for an hotplug machine state'; \
	cpu_bit_bitmap_start=$$'/*\n * cpu_bit_bitmap[] is a special, "compressed" data structure'; \
	cpu_masks_start='#ifdef CONFIG_INIT_ALL_POSSIBLE'; \
	set_cpu_online_start='void set_cpu_online(unsigned int cpu, bool online)'; \
	set_cpu_online_end=$$'\t} else {\n\t\tif (cpumask_test_and_clear_cpu(cpu, &__cpu_online_mask))\n\t\t\tatomic_dec(&__num_online_cpus);\n\t}\n}'; \
	boot_cpu_init_start=$$'/*\n * Activate the first processor.'; \
	boot_cpu_hotplug_init_end=$$'\tthis_cpu_write(cpuhp_state.target, CPUHP_ONLINE);\n}'; \
	copy_overflow_start='void __copy_overflow(int size, unsigned long count)'; \
	first_online_pgdat_start='struct pglist_data *first_online_pgdat(void)'; \
	next_online_pgdat_start='struct pglist_data *next_online_pgdat(struct pglist_data *pgdat)'; \
	next_zone_start=$$'/*\n * next_zone - helper magic for for_each_zone()'; \
	next_zone_end=$$'\treturn zone;\n}'; \
	zref_in_nodemask_start='static inline int zref_in_nodemask(struct zoneref *zref, nodemask_t *nodes)'; \
	next_zones_start='struct zoneref *__next_zones_zonelist(struct zoneref *z,'; \
	folio_put_start='static void __page_cache_release(struct folio *folio, struct lruvec **lruvecp,'; \
	mutex_init_start=$$'void\n__mutex_init(struct mutex *lock, const char *name, struct lock_class_key *key)'; \
	swait_init_start='void __init_swait_queue_head(struct swait_queue_head *q, const char *name,'; \
	swake_up_locked_start='void swake_up_locked(struct swait_queue_head *q, int wake_flags)'; \
	swake_up_one_start='void swake_up_one(struct swait_queue_head *q)'; \
	prepare_to_swait_start='void __prepare_to_swait(struct swait_queue_head *q, struct swait_queue *wait)'; \
	prepare_to_swait_event_start='long prepare_to_swait_event(struct swait_queue_head *q, struct swait_queue *wait, int state)'; \
	finish_swait_internal_start='void __finish_swait(struct swait_queue_head *q, struct swait_queue *wait)'; \
	finish_swait_start='void finish_swait(struct swait_queue_head *q, struct swait_queue *wait)'; \
	complete_with_flags_start='static void complete_with_flags(struct completion *x, int wake_flags)'; \
	complete_start=$$'/**\n * complete: - signals a single thread waiting on this completion'; \
	do_wait_for_common_start=$$'static inline long __sched\ndo_wait_for_common(struct completion *x,'; \
	do_wait_for_common_end=$$'\treturn timeout ?: 1;\n}'; \
	wait_for_common_inner_start=$$'static inline long __sched\n__wait_for_common(struct completion *x,'; \
	wait_for_common_inner_end=$$'\treturn timeout;\n}'; \
	wait_for_common_start=$$'static long __sched\nwait_for_common(struct completion *x, long timeout, int state)'; \
	wait_for_completion_start=$$'/**\n * wait_for_completion: - waits for completion of a task'; \
	wakeme_after_rcu_start=$$'/**\n * wakeme_after_rcu() - Callback function to awaken a task after grace period'; \
	warn_args_start='struct warn_args {'; \
	warn_slowpath_fmt_start='void warn_slowpath_fmt(const char *file, int line, unsigned taint,'; \
	softirq_irq_stat_start='#ifndef __ARCH_IRQ_STAT'; \
	local_bh_enable_internal_start='static void __local_bh_enable(unsigned int cnt)'; \
	local_bh_enable_internal_end=$$'\t__preempt_count_sub(cnt);\n}'; \
	local_bh_enable_start='void _local_bh_enable(void)'; \
	local_bh_enable_ip_start=$$'void __local_bh_enable_ip(unsigned long ip, unsigned int cnt)\n{\n\tWARN_ON_ONCE(in_hardirq());\n\tlockdep_assert_irqs_enabled();'; \
	msecs_to_jiffies_start=$$'/**\n * __msecs_to_jiffies: - convert milliseconds to jiffies'; \
	extract_text_range_once "$$devres_upstream" "$$devres_node_start" '};' "$$devres_src"; \
	extract_text_range_once "$$devres_upstream" "$$devres_start" '};' "$$devres_src"; \
	extract_text_range_once "$$devres_upstream" 'static void set_node_dbginfo(struct devres_node *node, const char *name,' '}' "$$devres_src"; \
	extract_text_range_once "$$devres_upstream" "$$devres_dbg_start" '#endif /* CONFIG_DEBUG_DEVRES */' "$$devres_src"; \
	extract_text_range_once "$$devres_upstream" "$$devres_log_start" '}' "$$devres_src"; \
	extract_text_range_once "$$devres_upstream" "$$check_dr_size_start" '}' "$$devres_src"; \
	extract_text_range_once "$$devres_upstream" "$$alloc_dr_start" '}' "$$devres_src"; \
	extract_text_range_once "$$devres_upstream" "$$add_dr_start" '}' "$$devres_src"; \
	extract_text_range_once "$$devres_upstream" "$$devres_alloc_start" 'EXPORT_SYMBOL_GPL(__devres_alloc_node);' "$$devres_src"; \
	extract_text_range_once "$$devres_upstream" "$$devres_free_start" 'EXPORT_SYMBOL_GPL(devres_free);' "$$devres_src"; \
	extract_text_range_once "$$devres_upstream" "$$devres_add_start" 'EXPORT_SYMBOL_GPL(devres_add);' "$$devres_src"; \
	extract_text_range_once "$$devres_upstream" "$$action_devres_start" '};' "$$devres_src"; \
	extract_text_range_once "$$devres_upstream" "$$devm_action_release_start" '}' "$$devres_src"; \
	extract_text_range_once "$$devres_upstream" "$$devm_add_action_start" 'EXPORT_SYMBOL_GPL(__devm_add_action);' "$$devres_src"; \
	extract_text_range_once "$$seq_file_upstream" "$$seq_set_overflow_start" '}' "$$seq_file_src"; \
	extract_text_range_once "$$seq_file_upstream" "$$seq_puts_start" 'EXPORT_SYMBOL(__seq_puts);' "$$seq_file_src"; \
	extract_text_range_once "$$seq_file_upstream" "$$seq_write_start" 'EXPORT_SYMBOL(seq_write);' "$$seq_file_src"; \
	extract_text_range_once "$$cpu_upstream" "$$cpuhp_cpu_state_start" '};' "$$cpu_src"; \
	extract_text_range_once "$$cpu_upstream" 'static DEFINE_PER_CPU(struct cpuhp_cpu_state, cpuhp_state) = {' '};' "$$cpu_src"; \
	extract_text_range_once "$$cpu_upstream" "$$cpuhp_step_start" "$$cpuhp_step_empty_end" "$$cpu_src"; \
	extract_text_range_once "$$cpu_upstream" "$$cpuhp_invoke_callback_start" "$$cpuhp_invoke_callback_end" "$$cpu_src"; \
	extract_text_range_once "$$cpu_upstream" "$$cpuhp_hp_states_start" '};' "$$cpu_src"; \
	extract_text_range_once "$$cpu_upstream" "$$cpuhp_cb_check_start" '}' "$$cpu_src"; \
	extract_text_range_once "$$cpu_upstream" "$$cpuhp_reserve_state_start" "$$cpuhp_reserve_state_end" "$$cpu_src"; \
	extract_text_range_once "$$cpu_upstream" "$$cpuhp_store_callbacks_start" "$$cpuhp_store_callbacks_end" "$$cpu_src"; \
	extract_text_range_once "$$cpu_upstream" "$$cpuhp_issue_call_start" "$$cpuhp_issue_call_end" "$$cpu_src"; \
	extract_text_range_once "$$cpu_upstream" "$$cpuhp_rollback_install_start" "$$cpuhp_rollback_install_end" "$$cpu_src"; \
	extract_text_range_once "$$cpu_upstream" "$$cpuhp_setup_state_start" 'EXPORT_SYMBOL(__cpuhp_setup_state);' "$$cpu_src"; \
	extract_text_range_once "$$cpu_upstream" "$$cpu_bit_bitmap_start" 'EXPORT_SYMBOL_GPL(cpu_bit_bitmap);' "$$cpu_src"; \
	extract_text_range_once "$$cpu_upstream" "$$cpu_masks_start" 'EXPORT_SYMBOL(__num_online_cpus);' "$$cpu_src"; \
	extract_text_range_once "$$cpu_upstream" "$$set_cpu_online_start" "$$set_cpu_online_end" "$$cpu_src"; \
	extract_text_range_once "$$cpu_upstream" "$$boot_cpu_init_start" "$$boot_cpu_hotplug_init_end" "$$cpu_src"; \
	extract_text_range_once "$$mutex_upstream" "$$mutex_init_start" 'EXPORT_SYMBOL(__mutex_init);' "$$mutex_src"; \
	extract_text_range_once "$$swait_upstream" "$$swait_init_start" 'EXPORT_SYMBOL(__init_swait_queue_head);' "$$swait_src"; \
	extract_text_range_once "$$swait_upstream" "$$swake_up_locked_start" 'EXPORT_SYMBOL(swake_up_locked);' "$$swait_src"; \
	extract_text_range_once "$$swait_upstream" "$$swake_up_one_start" 'EXPORT_SYMBOL(swake_up_one);' "$$swait_src"; \
	extract_text_range_once "$$swait_upstream" "$$prepare_to_swait_start" '}' "$$swait_src"; \
	extract_text_range_once "$$swait_upstream" "$$prepare_to_swait_event_start" 'EXPORT_SYMBOL(prepare_to_swait_event);' "$$swait_src"; \
	extract_text_range_once "$$swait_upstream" "$$finish_swait_internal_start" '}' "$$swait_src"; \
	extract_text_range_once "$$swait_upstream" "$$finish_swait_start" 'EXPORT_SYMBOL(finish_swait);' "$$swait_src"; \
	extract_text_range_once "$$completion_upstream" "$$complete_with_flags_start" '}' "$$completion_src"; \
	extract_text_range_once "$$completion_upstream" "$$complete_start" 'EXPORT_SYMBOL(complete);' "$$completion_src"; \
	extract_text_range_once "$$completion_upstream" "$$do_wait_for_common_start" "$$do_wait_for_common_end" "$$completion_src"; \
	extract_text_range_once "$$completion_upstream" "$$wait_for_common_inner_start" "$$wait_for_common_inner_end" "$$completion_src"; \
	extract_text_range_once "$$completion_upstream" "$$wait_for_common_start" '}' "$$completion_src"; \
	extract_text_range_once "$$completion_upstream" "$$wait_for_completion_start" 'EXPORT_SYMBOL(wait_for_completion);' "$$completion_src"; \
	extract_text_range_once "$$rcu_update_upstream" "$$wakeme_after_rcu_start" 'EXPORT_SYMBOL_GPL(wakeme_after_rcu);' "$$rcu_update_src"; \
	extract_text_range_once "$$softirq_upstream" "$$softirq_irq_stat_start" '#endif' "$$softirq_src"; \
	extract_text_range_once "$$softirq_upstream" "$$local_bh_enable_internal_start" "$$local_bh_enable_internal_end" "$$softirq_src"; \
	extract_text_range_once "$$softirq_upstream" "$$local_bh_enable_start" 'EXPORT_SYMBOL(_local_bh_enable);' "$$softirq_src"; \
	extract_text_range_once "$$softirq_upstream" "$$local_bh_enable_ip_start" 'EXPORT_SYMBOL(__local_bh_enable_ip);' "$$softirq_src"; \
	extract_text_range_once "$$panic_upstream" "$$warn_args_start" '};' "$$panic_src"; \
	extract_text_range_once "$$panic_upstream" "$$warn_slowpath_fmt_start" 'EXPORT_SYMBOL(warn_slowpath_fmt);' "$$panic_src"; \
	extract_text_range_once "$$time_upstream" "$$msecs_to_jiffies_start" 'EXPORT_SYMBOL(__msecs_to_jiffies);' "$$time_src"; \
	extract_text_range_once "$$maccess_upstream" "$$copy_overflow_start" 'EXPORT_SYMBOL(__copy_overflow);' "$$maccess_src"; \
	extract_text_range_once "$$mmzone_upstream" "$$first_online_pgdat_start" '}' "$$mmzone_src"; \
	extract_text_range_once "$$mmzone_upstream" "$$next_online_pgdat_start" '}' "$$mmzone_src"; \
	extract_text_range_once "$$mmzone_upstream" "$$next_zone_start" "$$next_zone_end" "$$mmzone_src"; \
	extract_text_range_once "$$mmzone_upstream" "$$zref_in_nodemask_start" '}' "$$mmzone_src"; \
	extract_text_range_once "$$mmzone_upstream" "$$next_zones_start" '}' "$$mmzone_src"; \
	extract_text_range_once "$$swap_upstream" "$$folio_put_start" 'EXPORT_SYMBOL(__folio_put);' "$$swap_src"; \
	extract_text_range_once "$$util_upstream" "$$kfree_const_start" 'EXPORT_SYMBOL(kfree_const);' "$$util_src"; \
	extract_text_range_once "$$util_upstream" "$$kstrdup_start" 'EXPORT_SYMBOL(kstrdup);' "$$util_src"; \
	extract_text_range_once "$$util_upstream" "$$kstrdup_const_start" 'EXPORT_SYMBOL(kstrdup_const);' "$$util_src"; \
	extract_text_range_once "$$util_upstream" "$$memdup_user_nul_start" 'EXPORT_SYMBOL(memdup_user_nul);' "$$util_src"; \
	extract_text_range_once "$$util_upstream" "$$kmalloc_gfp_adjust_start" "$$kmalloc_gfp_adjust_end" "$$util_src"; \
	extract_text_range_once "$$util_upstream" "$$kvmalloc_node_start" 'EXPORT_SYMBOL(__kvmalloc_node_noprof);' "$$util_src"; \
validate_line_once "$(ORLIX_PRODUCT_ADAPTER_SOURCE)/mm/page_alloc.c" '#include "internal.h"'; \
validate_line_once "$(ORLIX_PRODUCT_ADAPTER_SOURCE)/mm/page_alloc.c" '#include "shuffle.h"'; \
validate_line_once "$(ORLIX_PRODUCT_ADAPTER_SOURCE)/mm/page_alloc.c" '#include "page_reporting.h"'; \
validate_line_once "$(ORLIX_PRODUCT_ADAPTER_SOURCE)/mm/internal.h" '#include "vma.h"'; \
validate_line_once "$(ORLIX_PRODUCT_ADAPTER_SOURCE)/mm/internal.h" "$$internal_once_data_line"; \
adapt_in_place "$(ORLIX_PRODUCT_ADAPTER_SOURCE)/mm/internal.h" '__section(".data.once")' '__section("__DATA,__data_once")'; \
echo "generated Orlix product adapter inputs: $(ORLIX_PRODUCT_ADAPTER_INCLUDE), $(ORLIX_PRODUCT_ADAPTER_SOURCE)"
endef

define orlix_product_adapter_verify_object_contract
orlix_product_adapter_verify_object_contract() { \
	obj="$$1"; \
	contract_dir="$$2"; \
	obj_base="$$(basename "$$obj")"; \
	platform="$$(basename "$$(dirname "$$contract_dir")")"; \
	sections="$$contract_dir/$$obj_base.sections.txt"; \
	symbols="$$contract_dir/$$obj_base.symbols.txt"; \
	undefined="$$contract_dir/$$obj_base.undefined.txt"; \
	unsupported_sections='sectname (\.init\.text|\.init\.data|\.init\.setup|\.initcall[^[:space:]]*\.init|\.exit[^[:space:]]*|\.ref[^[:space:]]*|\.data\.\.percpu|\.data\.\.ro_after_init|\.export_symbol|\.discard[^[:space:]]*|\.ex_table|\.exception[^[:space:]]*|__head_text|__init_text|__init_data|__data_once|__init_setup|__initcall[^[:space:]]*|__exit[^[:space:]]*|__ref[^[:space:]]*|__percpu|__ro_after_init|__ksymtab[^[:space:]]*|__kcrctab[^[:space:]]*|__export_symbol|__discard[^[:space:]]*|__ex_table|__jump_table)'; \
	unsupported_symbols='(^|[[:space:]])_+(_sinittext|_einittext|__init_begin|__init_end|__setup_start|__setup_end|__initcall[^[:space:]]*|__con_initcall_start|__con_initcall_end|__per_cpu_load|__per_cpu_start|__per_cpu_end|__start_ro_after_init|__end_ro_after_init|__start___jump_table|__stop___jump_table|__ksymtab|__kcrctab|__start___ksymtab|__stop___ksymtab|__start___kcrctab|__stop___kcrctab|__ex_table)([[:space:]]|$$)'; \
	if [ "$$obj_base" = "arch_orlix_kernel_setup.c.o" ] || [ "$$obj_base" = "arch_orlix_kernel_irq.c.o" ]; then \
		unsupported_sections='sectname (\.init\.text|\.init\.data|\.init\.setup|\.initcall[^[:space:]]*\.init|\.exit[^[:space:]]*|\.ref[^[:space:]]*|\.data\.once|\.data\.\.percpu|\.data\.\.ro_after_init|\.export_symbol|\.discard[^[:space:]]*|\.ex_table|\.exception[^[:space:]]*|__head_text|__init_rodata|__init_setup|__initcall[^[:space:]]*|__exit[^[:space:]]*|__ref[^[:space:]]*|__ro_after_init|__ksymtab[^[:space:]]*|__kcrctab[^[:space:]]*|__export_symbol|__discard[^[:space:]]*|__ex_table|__jump_table)'; \
	elif [ "$$obj_base" = "init_main.c.o" ]; then \
		unsupported_sections='sectname (\.init\.text|\.init\.data|\.init\.setup|\.initcall[^[:space:]]*\.init|\.exit[^[:space:]]*|\.ref[^[:space:]]*|\.data\.once|\.data\.\.percpu|\.data\.\.ro_after_init|\.export_symbol|\.discard[^[:space:]]*|\.ex_table|\.exception[^[:space:]]*|__head_text|__initcall[^[:space:]]*|__exit[^[:space:]]*|__percpu|__ksymtab[^[:space:]]*|__kcrctab[^[:space:]]*|__ex_table|__jump_table)'; \
		unsupported_symbols='(^|[[:space:]])_+(_sinittext|_einittext|__setup_start|__setup_end|__con_initcall_start|__con_initcall_end|__per_cpu_load|__per_cpu_start|__per_cpu_end|__start_ro_after_init|__end_ro_after_init|__start___jump_table|__stop___jump_table|__ksymtab|__kcrctab|__start___ksymtab|__stop___ksymtab|__start___kcrctab|__stop___kcrctab|__ex_table)([[:space:]]|$$)'; \
	elif [ "$$obj_base" = "kernel_cpu.c.o" ] || [ "$$obj_base" = "kernel_rcu_srcutiny.c.o" ]; then \
		unsupported_sections='sectname (\.init\.text|\.init\.data|\.init\.setup|\.initcall[^[:space:]]*\.init|\.exit[^[:space:]]*|\.ref[^[:space:]]*|\.data\.once|\.data\.\.percpu|\.export_symbol|\.discard[^[:space:]]*|\.ex_table|\.exception[^[:space:]]*|__head_text|__init_data|__init_setup|__initcall[^[:space:]]*|__exit[^[:space:]]*|__ref[^[:space:]]*|__percpu|__ksymtab[^[:space:]]*|__kcrctab[^[:space:]]*|__export_symbol|__ex_table|__jump_table)'; \
	elif [ "$$obj_base" = "arch_orlix_mm_delay.c.o" ] || [ "$$obj_base" = "kernel_panic.c.o" ] || [ "$$obj_base" = "kernel_rcu_update.c.o" ] || [ "$$obj_base" = "kernel_sched_swait.c.o" ] || [ "$$obj_base" = "kernel_softirq.c.o" ] || [ "$$obj_base" = "lib_string.c.o" ] || [ "$$obj_base" = "lib_string_helpers.c.o" ] || [ "$$obj_base" = "lib_ctype.c.o" ] || [ "$$obj_base" = "lib_cmdline.c.o" ] || [ "$$obj_base" = "lib_errname.c.o" ] || [ "$$obj_base" = "lib_hexdump.c.o" ] || [ "$$obj_base" = "lib_uuid.c.o" ] || [ "$$obj_base" = "lib_siphash.c.o" ] || [ "$$obj_base" = "lib_kasprintf.c.o" ] || [ "$$obj_base" = "lib_ratelimit.c.o" ] || [ "$$obj_base" = "lib_find_bit.c.o" ] || [ "$$obj_base" = "lib_hweight.c.o" ] || [ "$$obj_base" = "lib_bitmap.c.o" ] || [ "$$obj_base" = "lib_math_int_sqrt.c.o" ] || [ "$$obj_base" = "mm_maccess.c.o" ] || [ "$$obj_base" = "mm_mmzone.c.o" ] || [ "$$obj_base" = "mm_swap.c.o" ] || [ "$$obj_base" = "mm_util.c.o" ] || [ "$$obj_base" = "mm_show_mem.c.o" ] || [ "$$obj_base" = "kernel_printk_printk_safe.c.o" ] || [ "$$obj_base" = "kernel_printk_printk_ringbuffer.c.o" ]; then \
		unsupported_sections='sectname (\.init\.text|\.init\.data|\.init\.setup|\.initcall[^[:space:]]*\.init|\.exit[^[:space:]]*|\.ref[^[:space:]]*|\.data\.once|\.data\.\.percpu|\.data\.\.ro_after_init|\.export_symbol|\.discard[^[:space:]]*|\.ex_table|\.exception[^[:space:]]*|__head_text|__init_text|__init_data|__init_setup|__initcall[^[:space:]]*|__exit[^[:space:]]*|__ref[^[:space:]]*|__percpu|__ro_after_init|__ksymtab[^[:space:]]*|__kcrctab[^[:space:]]*|__export_symbol|__ex_table|__jump_table)'; \
	elif [ "$$obj_base" = "kernel_sched_completion.c.o" ]; then \
		unsupported_sections='sectname (\.init\.text|\.init\.data|\.init\.setup|\.initcall[^[:space:]]*\.init|\.exit[^[:space:]]*|\.ref[^[:space:]]*|\.data\.once|\.data\.\.percpu|\.data\.\.ro_after_init|\.export_symbol|\.discard[^[:space:]]*|\.ex_table|\.exception[^[:space:]]*|__head_text|__init_text|__init_data|__init_setup|__initcall[^[:space:]]*|__exit[^[:space:]]*|__ref[^[:space:]]*|__percpu|__ro_after_init|__ksymtab[^[:space:]]*|__kcrctab[^[:space:]]*|__export_symbol|__ex_table|__jump_table)'; \
	elif [ "$$obj_base" = "lib_vsprintf.c.o" ]; then \
		unsupported_sections='sectname (\.init\.text|\.init\.data|\.init\.rodata|\.init\.setup|\.initcall[^[:space:]]*\.init|\.exit[^[:space:]]*|\.ref[^[:space:]]*|\.data\.\.percpu|\.data\.\.ro_after_init|\.export_symbol|\.discard[^[:space:]]*|\.ex_table|\.exception[^[:space:]]*|__head_text|__init_data|__exit[^[:space:]]*|__ref[^[:space:]]*|__percpu|__ksymtab[^[:space:]]*|__kcrctab[^[:space:]]*|__export_symbol|__ex_table|__jump_table)'; \
		unsupported_symbols='(^|[[:space:]])_+(_sinittext|_einittext|__init_begin|__init_end|__setup_start|__setup_end|__con_initcall_start|__con_initcall_end|__per_cpu_load|__per_cpu_start|__per_cpu_end|__start_ro_after_init|__end_ro_after_init|__start___jump_table|__stop___jump_table|__ksymtab|__kcrctab|__start___ksymtab|__stop___ksymtab|__start___kcrctab|__stop___kcrctab|__ex_table)([[:space:]]|$$)'; \
	elif [ "$$obj_base" = "drivers_base_devres.c.o" ] || [ "$$obj_base" = "fs_seq_file.c.o" ] || [ "$$obj_base" = "kernel_locking_mutex.c.o" ] || [ "$$obj_base" = "kernel_time_time.c.o" ] || [ "$$obj_base" = "kernel_time_timeconv.c.o" ] || [ "$$obj_base" = "lib_kstrtox.c.o" ]; then \
		unsupported_sections='sectname (\.init\.text|\.init\.data|\.init\.setup|\.initcall[^[:space:]]*\.init|\.exit[^[:space:]]*|\.ref[^[:space:]]*|\.data\.\.percpu|\.data\.\.ro_after_init|\.export_symbol|\.discard[^[:space:]]*|\.ex_table|\.exception[^[:space:]]*|__head_text|__init_text|__init_data|__init_setup|__initcall[^[:space:]]*|__exit[^[:space:]]*|__ref[^[:space:]]*|__percpu|__ro_after_init|__ksymtab[^[:space:]]*|__kcrctab[^[:space:]]*|__export_symbol|__ex_table|__jump_table)'; \
	elif [ "$$obj_base" = "mm_memblock.c.o" ]; then \
		unsupported_sections='sectname (\.init\.text|\.init\.data|\.init\.setup|\.initcall[^[:space:]]*\.init|\.exit[^[:space:]]*|\.ref[^[:space:]]*|\.data\.once|\.data\.\.percpu|\.data\.\.ro_after_init|\.export_symbol|\.discard[^[:space:]]*|\.ex_table|\.exception[^[:space:]]*|__head_text|__initcall[^[:space:]]*|__exit[^[:space:]]*|__percpu|__ro_after_init|__ksymtab[^[:space:]]*|__kcrctab[^[:space:]]*|__export_symbol|__ex_table|__jump_table)'; \
	elif [ "$$obj_base" = "mm_slab_common.c.o" ] || [ "$$obj_base" = "mm_slub.c.o" ]; then \
		unsupported_sections='sectname (\.init\.text|\.init\.data|\.init\.rodata|\.init\.setup|\.initcall[^[:space:]]*\.init|\.exit[^[:space:]]*|\.ref[^[:space:]]*|\.data\.once|\.data\.\.percpu|\.data\.\.ro_after_init|\.export_symbol|\.discard[^[:space:]]*|\.ex_table|\.exception[^[:space:]]*|__head_text|__exit[^[:space:]]*|__ref[^[:space:]]*|__percpu|__ksymtab[^[:space:]]*|__kcrctab[^[:space:]]*|__export_symbol|__ex_table|__jump_table)'; \
		unsupported_symbols='(^|[[:space:]])_+(_sinittext|_einittext|__init_begin|__init_end|__setup_start|__setup_end|__con_initcall_start|__con_initcall_end|__per_cpu_load|__per_cpu_start|__per_cpu_end|__start_ro_after_init|__end_ro_after_init|__start___jump_table|__stop___jump_table|__ksymtab|__kcrctab|__start___ksymtab|__stop___ksymtab|__start___kcrctab|__stop___kcrctab|__ex_table)([[:space:]]|$$)'; \
	elif [ "$$obj_base" = "mm_page_alloc.c.o" ]; then \
		unsupported_sections='sectname (\.init\.text|\.init\.data|\.init\.rodata|\.init\.setup|\.initcall[^[:space:]]*\.init|\.exit[^[:space:]]*|\.ref[^[:space:]]*|\.data\.once|\.data\.\.percpu|\.data\.\.ro_after_init|\.export_symbol|\.discard[^[:space:]]*|\.ex_table|\.exception[^[:space:]]*|__head_text|__exit[^[:space:]]*|__percpu|__ro_after_init|__ksymtab[^[:space:]]*|__kcrctab[^[:space:]]*|__export_symbol|__ex_table|__jump_table)'; \
		unsupported_symbols='(^|[[:space:]])_+(_sinittext|_einittext|__init_begin|__init_end|__setup_start|__setup_end|__con_initcall_start|__con_initcall_end|__per_cpu_load|__per_cpu_start|__per_cpu_end|__start_ro_after_init|__end_ro_after_init|__start___jump_table|__stop___jump_table|__ksymtab|__kcrctab|__start___ksymtab|__stop___ksymtab|__start___kcrctab|__stop___kcrctab|__ex_table)([[:space:]]|$$)'; \
	elif [ "$$obj_base" = "kernel_printk_printk.c.o" ]; then \
		unsupported_sections='sectname (\.init\.text|\.init\.data|\.init\.rodata|\.init\.setup|\.initcall[^[:space:]]*\.init|\.con_initcall\.init|\.sched\.text|\.exit[^[:space:]]*|\.ref[^[:space:]]*|\.data\.once|\.data\.\.percpu|\.data\.\.ro_after_init|\.export_symbol|\.discard[^[:space:]]*|\.ex_table|\.exception[^[:space:]]*|__head_text|__exit[^[:space:]]*|__ref[^[:space:]]*|__percpu|__ksymtab[^[:space:]]*|__kcrctab[^[:space:]]*|__export_symbol|__ex_table|__jump_table)'; \
		unsupported_symbols='(^|[[:space:]])_+(_sinittext|_einittext|__setup_start|__setup_end|__con_initcall_start|__con_initcall_end|__per_cpu_load|__per_cpu_start|__per_cpu_end|__start_ro_after_init|__end_ro_after_init|__start___jump_table|__stop___jump_table|__ksymtab|__kcrctab|__start___ksymtab|__stop___ksymtab|__start___kcrctab|__stop___kcrctab|__ex_table)([[:space:]]|$$)'; \
	fi; \
	unsupported_undefined='(^|[[:space:]])_+__(start|stop)_'; \
	mkdir -p "$$contract_dir"; \
	"$$otool_cmd" -l "$$obj" > "$$sections"; \
	"$$nm_cmd" -m "$$obj" > "$$symbols"; \
	"$$nm_cmd" -u "$$obj" > "$$undefined" 2>/dev/null || true; \
	awk -v platform="$$platform" -v object="$$obj_base" '/sectname / { section=$$2; next } /segname / { if (section != "") { print platform "\t" object "\t" $$2 "\t" section; section="" } }' "$$sections" >> "$(ORLIX_PRODUCT_ADAPTER_ROOT)/object-section-inventory.tsv"; \
	if grep -E "$$unsupported_sections" "$$sections"; then \
		echo "OrlixKernel product object requires unsupported Linux section semantics: $$obj" >&2; \
		exit 1; \
	fi; \
	if [ "$$obj_base" = "init_main.c.o" ]; then \
		if grep -E 'sectname __ref[^[:space:]]*' "$$sections" | grep -v 'sectname __ref_text'; then echo "OrlixKernel init/main object contains unsupported ref section: $$obj" >&2; exit 1; fi; \
		if grep -E 'sectname __discard[^[:space:]]*' "$$sections" | grep -v 'sectname __discard_addr'; then echo "OrlixKernel init/main object contains unsupported discard metadata section: $$obj" >&2; exit 1; fi; \
		if grep -E 'sectname __export[^[:space:]]*' "$$sections" | grep -v 'sectname __export_symbol'; then echo "OrlixKernel init/main object contains unsupported export metadata section: $$obj" >&2; exit 1; fi; \
	elif [ "$$obj_base" = "arch_orlix_mm_delay.c.o" ] || [ "$$obj_base" = "kernel_cpu.c.o" ] || [ "$$obj_base" = "kernel_panic.c.o" ] || [ "$$obj_base" = "kernel_rcu_srcutiny.c.o" ] || [ "$$obj_base" = "kernel_rcu_update.c.o" ] || [ "$$obj_base" = "kernel_sched_swait.c.o" ] || [ "$$obj_base" = "kernel_softirq.c.o" ] || [ "$$obj_base" = "lib_string.c.o" ] || [ "$$obj_base" = "lib_string_helpers.c.o" ] || [ "$$obj_base" = "lib_ctype.c.o" ] || [ "$$obj_base" = "lib_cmdline.c.o" ] || [ "$$obj_base" = "lib_errname.c.o" ] || [ "$$obj_base" = "lib_hexdump.c.o" ] || [ "$$obj_base" = "lib_uuid.c.o" ] || [ "$$obj_base" = "lib_siphash.c.o" ] || [ "$$obj_base" = "lib_kasprintf.c.o" ] || [ "$$obj_base" = "lib_ratelimit.c.o" ] || [ "$$obj_base" = "lib_find_bit.c.o" ] || [ "$$obj_base" = "lib_hweight.c.o" ] || [ "$$obj_base" = "lib_bitmap.c.o" ] || [ "$$obj_base" = "lib_math_int_sqrt.c.o" ] || [ "$$obj_base" = "mm_maccess.c.o" ] || [ "$$obj_base" = "mm_mmzone.c.o" ] || [ "$$obj_base" = "mm_swap.c.o" ] || [ "$$obj_base" = "mm_util.c.o" ] || [ "$$obj_base" = "mm_show_mem.c.o" ] || [ "$$obj_base" = "kernel_printk_printk_safe.c.o" ] || [ "$$obj_base" = "kernel_printk_printk_ringbuffer.c.o" ]; then \
		if grep -E 'sectname __discard[^[:space:]]*' "$$sections" | grep -v 'sectname __discard_addr'; then echo "OrlixKernel library object contains unsupported discard metadata section: $$obj" >&2; exit 1; fi; \
		if grep -E 'sectname __export[^[:space:]]*' "$$sections" | grep -v 'sectname __export_symbol'; then echo "OrlixKernel library object contains unsupported export metadata section: $$obj" >&2; exit 1; fi; \
	elif [ "$$obj_base" = "kernel_sched_completion.c.o" ]; then \
		if grep -E 'sectname __sched[^[:space:]]*' "$$sections" | grep -v 'sectname __sched_text'; then echo "OrlixKernel completion object contains unsupported sched section: $$obj" >&2; exit 1; fi; \
		if grep -E 'sectname __discard[^[:space:]]*' "$$sections" | grep -v 'sectname __discard_addr'; then echo "OrlixKernel completion object contains unsupported discard metadata section: $$obj" >&2; exit 1; fi; \
		if grep -E 'sectname __export[^[:space:]]*' "$$sections" | grep -v 'sectname __export_symbol'; then echo "OrlixKernel completion object contains unsupported export metadata section: $$obj" >&2; exit 1; fi; \
	elif [ "$$obj_base" = "lib_vsprintf.c.o" ]; then \
		if grep -E 'sectname __init[^[:space:]]*' "$$sections" | grep -v -E 'sectname __(init_text|init_rodata|init_setup|initcall4)$$'; then echo "OrlixKernel vsprintf object contains unsupported init section: $$obj" >&2; exit 1; fi; \
		if grep -E 'sectname __discard[^[:space:]]*' "$$sections" | grep -v 'sectname __discard_addr'; then echo "OrlixKernel vsprintf object contains unsupported discard metadata section: $$obj" >&2; exit 1; fi; \
		if grep -E 'sectname __export[^[:space:]]*' "$$sections" | grep -v 'sectname __export_symbol'; then echo "OrlixKernel vsprintf object contains unsupported export metadata section: $$obj" >&2; exit 1; fi; \
	elif [ "$$obj_base" = "kernel_time_timeconv.c.o" ] || [ "$$obj_base" = "lib_kstrtox.c.o" ]; then \
		if grep -E 'sectname __discard[^[:space:]]*' "$$sections" | grep -v 'sectname __discard_addr'; then echo "OrlixKernel parse/time object contains unsupported discard metadata section: $$obj" >&2; exit 1; fi; \
		if grep -E 'sectname __export[^[:space:]]*' "$$sections" | grep -v 'sectname __export_symbol'; then echo "OrlixKernel parse/time object contains unsupported export metadata section: $$obj" >&2; exit 1; fi; \
	elif [ "$$obj_base" = "mm_memblock.c.o" ]; then \
		if grep -E 'sectname __ref[^[:space:]]*' "$$sections" | grep -v 'sectname __ref_data'; then echo "OrlixKernel memblock object contains unsupported ref section: $$obj" >&2; exit 1; fi; \
		if grep -E 'sectname __discard[^[:space:]]*' "$$sections" | grep -v 'sectname __discard_addr'; then echo "OrlixKernel memblock object contains unsupported discard metadata section: $$obj" >&2; exit 1; fi; \
		if grep -E 'sectname __export[^[:space:]]*' "$$sections" | grep -v 'sectname __export_symbol'; then echo "OrlixKernel memblock object contains unsupported export metadata section: $$obj" >&2; exit 1; fi; \
	elif [ "$$obj_base" = "mm_slab_common.c.o" ] || [ "$$obj_base" = "mm_slub.c.o" ]; then \
		if grep -E 'sectname __initcall[^[:space:]]*' "$$sections" | grep -v -E 'sectname __initcall(6|7|7s)$$'; then echo "OrlixKernel slab object contains unsupported initcall section: $$obj" >&2; exit 1; fi; \
		if grep -E 'sectname __discard[^[:space:]]*' "$$sections" | grep -v 'sectname __discard_addr'; then echo "OrlixKernel slab object contains unsupported discard metadata section: $$obj" >&2; exit 1; fi; \
		if grep -E 'sectname __export[^[:space:]]*' "$$sections" | grep -v 'sectname __export_symbol'; then echo "OrlixKernel slab object contains unsupported export metadata section: $$obj" >&2; exit 1; fi; \
	elif [ "$$obj_base" = "mm_page_alloc.c.o" ]; then \
		if grep -E 'sectname __initcall[^[:space:]]*' "$$sections" | grep -v -E 'sectname __initcall2$$'; then echo "OrlixKernel page allocator object contains unsupported initcall section: $$obj" >&2; exit 1; fi; \
		if grep -E 'sectname __ref[^[:space:]]*' "$$sections" | grep -v 'sectname __ref_text'; then echo "OrlixKernel page allocator object contains unsupported ref section: $$obj" >&2; exit 1; fi; \
		if grep -E 'sectname __discard[^[:space:]]*' "$$sections" | grep -v 'sectname __discard_addr'; then echo "OrlixKernel page allocator object contains unsupported discard metadata section: $$obj" >&2; exit 1; fi; \
		if grep -E 'sectname __export[^[:space:]]*' "$$sections" | grep -v 'sectname __export_symbol'; then echo "OrlixKernel page allocator object contains unsupported export metadata section: $$obj" >&2; exit 1; fi; \
	elif [ "$$obj_base" = "kernel_printk_printk.c.o" ]; then \
		if grep -E 'sectname __initcall[^[:space:]]*' "$$sections" | grep -v -E 'sectname __initcall(_e|7)$$'; then echo "OrlixKernel printk object contains unsupported initcall section: $$obj" >&2; exit 1; fi; \
		if grep -E 'sectname __sched[^[:space:]]*' "$$sections" | grep -v 'sectname __sched_text'; then echo "OrlixKernel printk object contains unsupported sched section: $$obj" >&2; exit 1; fi; \
		if grep -E 'sectname __discard[^[:space:]]*' "$$sections" | grep -v 'sectname __discard_addr'; then echo "OrlixKernel printk object contains unsupported discard metadata section: $$obj" >&2; exit 1; fi; \
		if grep -E 'sectname __export[^[:space:]]*' "$$sections" | grep -v 'sectname __export_symbol'; then echo "OrlixKernel printk object contains unsupported export metadata section: $$obj" >&2; exit 1; fi; \
	fi; \
	if grep -E "$$unsupported_symbols" "$$symbols"; then \
		echo "OrlixKernel product object requires unsupported Linux section/export symbols: $$obj" >&2; \
		exit 1; \
	fi; \
	if [ "$$obj_base" = "arch_orlix_kernel_setup.c.o" ] && grep -E '(^|[[:space:]])_+(__start_once|__end_once)([[:space:]]|$$)' "$$undefined"; then \
		echo "OrlixKernel setup object references unsupported .data.once boundary symbols: $$obj" >&2; \
		exit 1; \
	fi; \
	if grep -E "$$unsupported_undefined" "$$undefined"; then \
		echo "OrlixKernel product object has unresolved Linux section boundary dependency: $$obj" >&2; \
		exit 1; \
	fi; \
};
endef

define orlix_product_adapter_generate_boundaries
orlix_product_adapter_generate_boundaries() { \
	contract_dir="$$1"; \
	platform="$$2"; \
	target="$$3"; \
	boundary_src="$(ORLIX_PRODUCT_ADAPTER_ROOT)/orlix-product-boundaries.S"; \
	boundary_obj="$(ORLIX_PRODUCT_BOUNDARY_OBJECT)"; \
	boundary_symbols="$$contract_dir/orlix-product-boundaries.o.symbols.txt"; \
	boundary_sections="$$contract_dir/orlix-product-boundaries.o.sections.txt"; \
	inventory="$(ORLIX_PRODUCT_ADAPTER_ROOT)/object-section-inventory.tsv"; \
	section_absent() { \
		segment="$$1"; \
		section="$$2"; \
		! awk -F '\t' -v platform="$$platform" -v segment="$$segment" -v section="$$section" 'NR > 1 && $$1 == platform && $$3 == segment && $$4 == section { found = 1 } END { exit found ? 0 : 1 }' "$$inventory"; \
	}; \
	emit_label() { \
		symbol="$$1"; \
		printf '.globl %s\n' "$$symbol"; \
		printf '%s:\n' "$$symbol"; \
	}; \
	mkdir -p "$$(dirname "$$boundary_src")"; \
	{ \
		printf '%s\n' '/* generated Build-only product-link boundary glue derived from generated arch/orlix/kernel/vmlinux.lds */'; \
		printf '%s\n' '/* Empty labels represent Linux linker-script ranges whose Mach-O section is absent from the current product object inventory. */'; \
		printf '%s\n' '.section __DATA,__orlix_bnd'; \
		printf '%s\n' '.p2align 3'; \
		emit_label ___init_begin; \
		emit_label ___init_end; \
		if section_absent __DATA __initcall_e; then emit_label ___initcall_start; emit_label 'section$$start$$__DATA$$__initcall_e'; fi; \
		if section_absent __DATA __initcall0; then emit_label ___initcall0_start; emit_label 'section$$start$$__DATA$$__initcall0'; fi; \
		if section_absent __DATA __initcall1; then emit_label ___initcall1_start; emit_label 'section$$start$$__DATA$$__initcall1'; fi; \
		if section_absent __DATA __initcall2; then emit_label ___initcall2_start; emit_label 'section$$start$$__DATA$$__initcall2'; fi; \
		if section_absent __DATA __initcall3; then emit_label ___initcall3_start; emit_label 'section$$start$$__DATA$$__initcall3'; fi; \
		if section_absent __DATA __initcall4; then emit_label ___initcall4_start; emit_label 'section$$start$$__DATA$$__initcall4'; fi; \
		if section_absent __DATA __initcall5; then emit_label ___initcall5_start; emit_label 'section$$start$$__DATA$$__initcall5'; fi; \
		if section_absent __DATA __initcallrf; then emit_label ___initcallrootfs_start; emit_label 'section$$start$$__DATA$$__initcallrf'; fi; \
		if section_absent __DATA __initcall6; then emit_label ___initcall6_start; emit_label 'section$$start$$__DATA$$__initcall6'; fi; \
		if section_absent __DATA __initcall7; then emit_label ___initcall7_start; emit_label 'section$$start$$__DATA$$__initcall7'; fi; \
		if section_absent __DATA __initcall7s; then emit_label ___initcall_end; emit_label 'section$$end$$__DATA$$__initcall7s'; fi; \
		if section_absent __DATA __con_initcall; then emit_label ___con_initcall_start; emit_label 'section$$start$$__DATA$$__con_initcall'; emit_label ___con_initcall_end; emit_label 'section$$end$$__DATA$$__con_initcall'; fi; \
	} > "$$boundary_src"; \
	/usr/bin/env -u SDKROOT "$$cc" -target "$$target" -isysroot / -x assembler -c "$$boundary_src" -o "$$boundary_obj"; \
	"$$nm_cmd" -m "$$boundary_obj" > "$$boundary_symbols"; \
	"$$otool_cmd" -l "$$boundary_obj" > "$$boundary_sections"; \
	for symbol in ___init_begin ___init_end; do \
		grep -F -q "$$symbol" "$$boundary_symbols" || { echo "product boundary object missing symbol: $$symbol" >&2; exit 1; }; \
	done; \
	validate_empty_initcall_boundary() { \
		symbol="$$1"; \
		segment="$$2"; \
		section="$$3"; \
		if section_absent "$$segment" "$$section" && ! grep -F -q "$$symbol" "$$boundary_symbols"; then echo "product boundary inventory missing empty Linux linker-script symbol: $$symbol" >&2; exit 1; fi; \
	}; \
	validate_empty_initcall_boundary ___initcall_start __DATA __initcall_e; \
	validate_empty_initcall_boundary ___initcall0_start __DATA __initcall0; \
	validate_empty_initcall_boundary ___initcall1_start __DATA __initcall1; \
	validate_empty_initcall_boundary ___initcall2_start __DATA __initcall2; \
	validate_empty_initcall_boundary ___initcall3_start __DATA __initcall3; \
	validate_empty_initcall_boundary ___initcall4_start __DATA __initcall4; \
	validate_empty_initcall_boundary ___initcall5_start __DATA __initcall5; \
	validate_empty_initcall_boundary ___initcallrootfs_start __DATA __initcallrf; \
	validate_empty_initcall_boundary ___initcall6_start __DATA __initcall6; \
	validate_empty_initcall_boundary ___initcall7_start __DATA __initcall7; \
	validate_empty_initcall_boundary ___initcall_end __DATA __initcall7s; \
	validate_empty_initcall_boundary ___con_initcall_start __DATA __con_initcall; \
	validate_empty_initcall_boundary ___con_initcall_end __DATA __con_initcall; \
	objs+=("$$boundary_obj"); \
	echo "generated Orlix product boundary object: $$boundary_obj"; \
};
endef
