/* IXLandKernel/runtime/aarch64/elf_reloc.h
 * Private aarch64 ELF relocation application over virtual task memory.
 */

#ifndef RUNTIME_AARCH64_ELF_RELOC_H
#define RUNTIME_AARCH64_ELF_RELOC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct task_dynamic_info;
struct task_struct;

enum aarch64_elf_relocation_type {
    R_AARCH64_NONE = 0,
    R_AARCH64_ABS64 = 257,
    R_AARCH64_GLOB_DAT = 1025,
    R_AARCH64_JUMP_SLOT = 1026,
    R_AARCH64_RELATIVE = 1027,
};

int aarch64_apply_dynamic_relocations(struct task_struct *task,
                                      const struct task_dynamic_info *dynamic,
                                      uint64_t load_base,
                                      uint32_t *out_applied);

#ifdef __cplusplus
}
#endif

#endif /* RUNTIME_AARCH64_ELF_RELOC_H */
