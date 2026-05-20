ORLIX_PRODUCT_COMPILE_ADAPTER_ROOT := $(ORLIX_KERNEL_BUILD_DIR)/orlix-product-compile-adapter
ORLIX_PRODUCT_COMPILE_ADAPTER_INCLUDE := $(ORLIX_PRODUCT_COMPILE_ADAPTER_ROOT)/include
ORLIX_PRODUCT_COMPILE_PER_CPU_BASE_SECTION := __DATA,__data
ORLIX_PRODUCT_COMPILE_CFLAGS := -fshort-wchar

.PHONY: __orlix-product-compile-adapter

__orlix-product-compile-adapter: __prepare-kbuild
	@set -euo pipefail; \
	adapter_root="$(ORLIX_PRODUCT_COMPILE_ADAPTER_ROOT)"; \
	adapter_include="$(ORLIX_PRODUCT_COMPILE_ADAPTER_INCLUDE)"; \
	for path in "$(ORLIX_KERNEL_BUILD_DIR)" "$$adapter_root" "$$adapter_include"; do \
		if [ -e "$$path" ] && [ -L "$$path" ]; then echo "refusing to use symlinked product compile adapter path: $$path" >&2; exit 1; fi; \
	done; \
	config="$(ORLIX_KERNEL_BUILD_DIR)/.config"; \
	[ -s "$$config" ] || { echo "missing generated Orlix kernel config for product archive: $$config" >&2; exit 1; }; \
	smp_config="$$(grep -n '^CONFIG_SMP=' "$$config" || true)"; \
	if [ -n "$$smp_config" ]; then echo "product non-SMP percpu section adapter requires CONFIG_SMP unset: $$smp_config" >&2; exit 1; fi; \
	lds="$(ORLIX_KERNEL_BUILD_DIR)/arch/$(LINUX_ARCH)/kernel/vmlinux.lds"; \
	[ -s "$$lds" ] || { echo "missing generated Orlix linker script for product section validation: $$lds" >&2; exit 1; }; \
	grep -F -q '.init.text' "$$lds" || { echo "generated Orlix linker script does not describe .init.text" >&2; exit 1; }; \
	grep -F -q '.init.data' "$$lds" || { echo "generated Orlix linker script does not describe .init.data" >&2; exit 1; }; \
	grep -F -q '.init.rodata' "$$lds" || { echo "generated Orlix linker script does not describe .init.rodata" >&2; exit 1; }; \
	grep -F -q '.init.setup' "$$lds" || { echo "generated Orlix linker script does not describe .init.setup" >&2; exit 1; }; \
	grep -F -q '.ref.text' "$$lds" || { echo "generated Orlix linker script does not describe .ref.text" >&2; exit 1; }; \
	grep -F -q '__setup_start' "$$lds" || { echo "generated Orlix linker script does not define __setup_start" >&2; exit 1; }; \
	grep -F -q '__setup_end' "$$lds" || { echo "generated Orlix linker script does not define __setup_end" >&2; exit 1; }; \
	grep -F -q '.data.once' "$$lds" || { echo "generated Orlix linker script does not describe .data.once" >&2; exit 1; }; \
	grep -F -q '__start_once' "$$lds" || { echo "generated Orlix linker script does not define __start_once" >&2; exit 1; }; \
	grep -F -q '__end_once' "$$lds" || { echo "generated Orlix linker script does not define __end_once" >&2; exit 1; }; \
	grep -F -q '.data..ro_after_init' "$$lds" || { echo "generated Orlix linker script does not describe .data..ro_after_init" >&2; exit 1; }; \
	grep -F -q '__param' "$$lds" || { echo "generated Orlix linker script does not describe __param" >&2; exit 1; }; \
	grep -F -q 'BOUNDED_SECTION_BY(__param, ___param)' "$(ORLIX_KERNEL_PORT_DIR)/include/asm-generic/vmlinux.lds.h" || { echo "generic Linux linker script does not bound __param with ___param" >&2; exit 1; }; \
	command -v perl >/dev/null 2>&1 || { echo "perl is required to generate the Orlix product compile adapter" >&2; exit 1; }; \
	rm -rf "$$adapter_root"; \
	mkdir -p "$$adapter_include/linux"; \
	validate_line_once() { \
		file="$$1"; \
		line="$$2"; \
		count="$$(grep -F -x -c "$$line" "$$file")"; \
		if [ "$$count" != 1 ]; then echo "expected one exact product adapter validation line in $$file: $$line (found $$count)" >&2; exit 1; fi; \
		echo "validated product adapter upstream text: $$file: $$line"; \
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
	validate_text_once() { \
		file="$$1"; \
		text="$$2"; \
		count="$$(TEXT="$$text" perl -0ne 'BEGIN { $$text = $$ENV{"TEXT"}; $$count = 0; } $$count += () = /\Q$$text\E/g; END { print $$count; }' "$$file")"; \
		if [ "$$count" != 1 ]; then echo "expected one exact product adapter validation block in $$file (found $$count)" >&2; exit 1; fi; \
		echo "validated product adapter upstream block: $$file"; \
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
	cp "$(ORLIX_KERNEL_PORT_DIR)/include/linux/init.h" "$$adapter_include/linux/init.h"; \
	cp "$(ORLIX_KERNEL_PORT_DIR)/include/linux/compiler.h" "$$adapter_include/linux/compiler.h"; \
	cp "$(ORLIX_KERNEL_PORT_DIR)/include/linux/cache.h" "$$adapter_include/linux/cache.h"; \
	cp "$(ORLIX_KERNEL_PORT_DIR)/include/linux/moduleparam.h" "$$adapter_include/linux/moduleparam.h"; \
	cp "$(ORLIX_KERNEL_PORT_DIR)/include/linux/once_lite.h" "$$adapter_include/linux/once_lite.h"; \
	cp "$(ORLIX_KERNEL_PORT_DIR)/include/linux/percpu-defs.h" "$$adapter_include/linux/percpu-defs.h"; \
	init_text_line=$$'#define __init\t\t__section(".init.text") __cold  __latent_entropy __noinitretpoline'; \
	init_data_line=$$'#define __initdata\t__section(".init.data")'; \
	init_const_line=$$'#define __initconst\t__section(".init.rodata")'; \
	init_setup_line=$$'\t\t__used __section(".init.setup")\t\t\t\t\\'; \
	setup_boundary_line=$$'extern const struct obs_kernel_param __setup_start[], __setup_end[];'; \
	setup_boundary_product_block=$$'extern const struct obs_kernel_param __setup_start[]\n\t__asm("section$$start$$__DATA$$__init_setup");\nextern const struct obs_kernel_param __setup_end[]\n\t__asm("section$$end$$__DATA$$__init_setup");'; \
	ref_text_line=$$'#define __ref            __section(".ref.text") noinline'; \
	compiler_addressable_block=$$'#define __ADDRESSABLE(sym) \\\n\t___ADDRESSABLE(sym, __section(".discard.addressable"))'; \
	ro_after_init_line=$$'#define __ro_after_init __section(".data..ro_after_init")'; \
	module_param_boundary_line=$$'extern const struct kernel_param __start___param[], __stop___param[];'; \
	module_param_boundary_product_block=$$'extern const struct kernel_param __start___param[]\n\t__asm("section$$start$$__DATA$$__param");\nextern const struct kernel_param __stop___param[]\n\t__asm("section$$end$$__DATA$$__param");'; \
	module_param_line=$$'\t__used __section("__param")\t\t\t\t\t\\'; \
	once_data_line=$$'\t\tstatic bool __section(".data.once") __already_done;\t\\'; \
	percpu_nonsmp_aligned_block=$$'#else\n\n#define PER_CPU_SHARED_ALIGNED_SECTION ""\n#define PER_CPU_ALIGNED_SECTION "..shared_aligned"\n#define PER_CPU_FIRST_SECTION ""\n\n#endif'; \
	percpu_nonsmp_product_block=$$'#else\n\n#define PER_CPU_SHARED_ALIGNED_SECTION ""\n#define PER_CPU_ALIGNED_SECTION ""\n#define PER_CPU_FIRST_SECTION ""\n\n#endif'; \
	validate_line_once "$$adapter_include/linux/init.h" "$$init_text_line"; \
	validate_line_once "$$adapter_include/linux/init.h" "$$init_data_line"; \
	validate_line_once "$$adapter_include/linux/init.h" "$$init_const_line"; \
	validate_line_once "$$adapter_include/linux/init.h" "$$init_setup_line"; \
	validate_line_once "$$adapter_include/linux/init.h" "$$setup_boundary_line"; \
	validate_line_once "$$adapter_include/linux/init.h" "$$ref_text_line"; \
	validate_text_once "$$adapter_include/linux/compiler.h" "$$compiler_addressable_block"; \
	validate_line_once "$$adapter_include/linux/cache.h" "$$ro_after_init_line"; \
	validate_line_once "$$adapter_include/linux/moduleparam.h" "$$module_param_boundary_line"; \
	validate_line_once "$$adapter_include/linux/moduleparam.h" "$$module_param_line"; \
	validate_line_once "$$adapter_include/linux/once_lite.h" "$$once_data_line"; \
	validate_text_once "$$adapter_include/linux/percpu-defs.h" "$$percpu_nonsmp_aligned_block"; \
	adapt_in_place "$$adapter_include/linux/init.h" '__section(".init.text")' '__section("__TEXT,__init_text")'; \
	adapt_in_place "$$adapter_include/linux/init.h" '__section(".init.data")' '__section("__DATA,__init_data")'; \
	adapt_in_place "$$adapter_include/linux/init.h" '__section(".init.rodata")' '__section("__TEXT,__init_rodata")'; \
	adapt_in_place "$$adapter_include/linux/init.h" '__section(".init.setup")' '__section("__DATA,__init_setup")'; \
	adapt_text_in_place "$$adapter_include/linux/init.h" "$$setup_boundary_line" "$$setup_boundary_product_block"; \
	adapt_in_place "$$adapter_include/linux/init.h" '__section(".ref.text")' '__section("__TEXT,__ref_text")'; \
	adapt_in_place "$$adapter_include/linux/compiler.h" '__section(".discard.addressable")' '__section("__DATA,__discard_addr")'; \
	adapt_in_place "$$adapter_include/linux/cache.h" '__section(".data..ro_after_init")' '__section("__DATA,__ro_after_init")'; \
	adapt_text_in_place "$$adapter_include/linux/moduleparam.h" "$$module_param_boundary_line" "$$module_param_boundary_product_block"; \
	adapt_in_place "$$adapter_include/linux/moduleparam.h" '__section("__param")' '__section("__DATA,__param")'; \
	adapt_in_place "$$adapter_include/linux/once_lite.h" '__section(".data.once")' '__section("__DATA,__data_once")'; \
	adapt_text_in_place "$$adapter_include/linux/percpu-defs.h" "$$percpu_nonsmp_aligned_block" "$$percpu_nonsmp_product_block"; \
	echo "active CONFIG_SMP for product archive: $${smp_config:-not set}"; \
	echo "PER_CPU_BASE_SECTION for product archive: $(ORLIX_PRODUCT_COMPILE_PER_CPU_BASE_SECTION)"
