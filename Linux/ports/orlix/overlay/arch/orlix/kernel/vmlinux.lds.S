/* SPDX-License-Identifier: GPL-2.0-only */

#define RO_EXCEPTION_TABLE_ALIGN 4
#define RUNTIME_DISCARD_EXIT

#include <asm-generic/vmlinux.lds.h>
#include <asm/cache.h>
#include <asm/page.h>
#include <asm/thread_info.h>

OUTPUT_ARCH(aarch64)
ENTRY(_text)

jiffies = jiffies_64;

SECTIONS
{
	. = PAGE_OFFSET;

	_text = .;
	HEAD_TEXT_SECTION

	. = ALIGN(PAGE_SIZE);
	_stext = .;
	.text : AT(ADDR(.text) - LOAD_OFFSET) {
		TEXT_TEXT
		SCHED_TEXT
		LOCK_TEXT
		KPROBES_TEXT
		ENTRY_TEXT
		IRQENTRY_TEXT
		SOFTIRQENTRY_TEXT
		*(.fixup)
		*(.gnu.warning)
	}
	_etext = .;

	. = ALIGN(PAGE_SIZE);
	__init_begin = .;
	__inittext_begin = .;
	INIT_TEXT_SECTION(PAGE_SIZE)
	.exit.text : AT(ADDR(.exit.text) - LOAD_OFFSET) {
		EXIT_TEXT
	}
	__inittext_end = .;

	__initdata_begin = .;
	INIT_DATA_SECTION(16)
	.exit.data : AT(ADDR(.exit.data) - LOAD_OFFSET) {
		EXIT_DATA
	}
	PERCPU_SECTION(L1_CACHE_BYTES)
	__initdata_end = .;

	. = ALIGN(PAGE_SIZE);
	__init_end = .;

	_sdata = .;
	RO_DATA(PAGE_SIZE)
	RW_DATA(L1_CACHE_BYTES, PAGE_SIZE, THREAD_SIZE)
	_edata = .;

	BSS_SECTION(PAGE_SIZE, PAGE_SIZE, PAGE_SIZE)
	_end = .;

	STABS_DEBUG
	DWARF_DEBUG
	ELF_DETAILS

	DISCARDS
}
