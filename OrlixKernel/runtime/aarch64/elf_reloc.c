/* OrlixKernel/runtime/aarch64/elf_reloc.c
 * AArch64 ELF relocation application for Orlix virtual task memory.
 */

#include "elf_reloc.h"

#include <linux/errno.h>
#include <stddef.h>
#include <stdint.h>

#include <uapi/linux/elf.h>

#include "../../kernel/task.h"

static int read_exact(struct task_struct *task, uint64_t addr, void *buf, size_t size) {
    long nread = task_read_virtual_memory_impl(task, addr, buf, size);
    if (nread != (long)size) {
        return nread >= 0 ? -EFAULT : (int)nread;
    }
    return 0;
}

static int write_u64(struct task_struct *task, uint64_t addr, uint64_t value) {
    long nwritten = task_write_virtual_memory_impl(task, addr, &value, sizeof(value));
    if (nwritten != (long)sizeof(value)) {
        return nwritten >= 0 ? -EFAULT : (int)nwritten;
    }
    return 0;
}

static int relocation_symbol_value(struct task_struct *task,
                                   const struct task_dynamic_info *dynamic,
                                   uint64_t symbol_index,
                                   uint64_t *out_value) {
    Elf64_Sym sym;

    if (!out_value) {
        return -EINVAL;
    }
    if (symbol_index == 0) {
        *out_value = 0;
        return 0;
    }
    if (!dynamic || dynamic->symtab_vaddr == 0 ||
        symbol_index > (UINT64_MAX - dynamic->symtab_vaddr) / sizeof(sym)) {
        return -ENOEXEC;
    }
    if (read_exact(task, dynamic->symtab_vaddr + (symbol_index * sizeof(sym)), &sym, sizeof(sym)) != 0) {
        return -1;
    }
    *out_value = sym.st_value;
    return 0;
}

static int apply_rela(struct task_struct *task,
                      const struct task_dynamic_info *dynamic,
                      uint64_t load_base,
                      const Elf64_Rela *rela) {
    uint64_t type;
    uint64_t symbol_index;
    uint64_t value;

    if (!task || !dynamic || !rela) {
        return -EINVAL;
    }

    type = ELF64_R_TYPE(rela->r_info);
    symbol_index = ELF64_R_SYM(rela->r_info);
    switch (type) {
    case R_AARCH64_NONE:
        return 0;
    case R_AARCH64_RELATIVE:
        value = load_base + (uint64_t)rela->r_addend;
        return write_u64(task, rela->r_offset, value);
    case R_AARCH64_ABS64:
    case R_AARCH64_GLOB_DAT:
    case R_AARCH64_JUMP_SLOT:
        {
            int ret = relocation_symbol_value(task, dynamic, symbol_index, &value);
            if (ret != 0) {
                return ret;
            }
        }
        value += (uint64_t)rela->r_addend;
        return write_u64(task, rela->r_offset, value);
    default:
        return -ENOEXEC;
    }
}

static int apply_rela_table(struct task_struct *task,
                            const struct task_dynamic_info *dynamic,
                            uint64_t load_base,
                            uint64_t rela_vaddr,
                            uint64_t rela_size,
                            uint64_t rela_entry_size,
                            uint32_t *applied) {
    uint64_t count;

    if (rela_vaddr == 0 || rela_size == 0) {
        return 0;
    }
    if (rela_entry_size != sizeof(Elf64_Rela) || (rela_size % sizeof(Elf64_Rela)) != 0) {
        return -ENOEXEC;
    }

    count = rela_size / sizeof(Elf64_Rela);
    for (uint64_t i = 0; i < count; i++) {
        Elf64_Rela rela;
        if (i > (UINT64_MAX - rela_vaddr) / sizeof(rela)) {
            return -ENOEXEC;
        }
        {
            int ret = read_exact(task, rela_vaddr + (i * sizeof(rela)), &rela, sizeof(rela));
            if (ret != 0) {
                return ret;
            }
        }
        {
            int ret = apply_rela(task, dynamic, load_base, &rela);
            if (ret != 0) {
                return ret;
            }
        }
        if (applied) {
            (*applied)++;
        }
    }
    return 0;
}

int aarch64_apply_dynamic_relocations(struct task_struct *task,
                                      const struct task_dynamic_info *dynamic,
                                      uint64_t load_base,
                                      uint32_t *out_applied) {
    uint32_t applied = 0;

    if (!task || !dynamic) {
        return -EINVAL;
    }

    if (apply_rela_table(task, dynamic, load_base,
                         dynamic->rela_vaddr, dynamic->rela_size,
                         dynamic->rela_entry_size, &applied) != 0) {
        return -1;
    }
    if (dynamic->plt_rela_type != 0 &&
        dynamic->plt_rela_type != DT_RELA) {
        return -ENOEXEC;
    }
    if (apply_rela_table(task, dynamic, load_base,
                         dynamic->plt_rela_vaddr, dynamic->plt_rela_size,
                         dynamic->rela_entry_size, &applied) != 0) {
        return -1;
    }

    if (out_applied) {
        *out_applied = applied;
    }
    return 0;
}
