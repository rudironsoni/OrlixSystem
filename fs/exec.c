/* iXland - File Execution
 *
 * Canonical owner for exec syscalls:
 * - execve(), execv(), execvp(), execvpe()
 * - execle(), execl(), execlp()
 * - fexecve()
 *
 * Linux-shaped canonical owner - iOS mediation as implementation detail
 */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Linux UAPI headers for ABI constants and types */
#include <linux/fcntl.h>
#include <linux/elf.h>
#include <linux/auxvec.h>
#include <linux/stat.h>
#include <asm-generic/stat.h>
#ifdef SIG_DFL
#undef SIG_DFL
#endif
#ifdef SIG_IGN
#undef SIG_IGN
#endif
#ifdef SIG_ERR
#undef SIG_ERR
#endif
#include <asm-generic/signal-defs.h>

#include "../kernel/task.h"

/* environ is not available on iOS; use _NSGetEnviron() */
#include <crt_externs.h>
#define environ (*_NSGetEnviron())

#include "../kernel/signal.h"
#include "../kernel/cred_internal.h"
#include "../runtime/native/registry.h"
#include "fdtable.h"
#include "vfs.h"

extern int open_impl(const char *pathname, int flags, mode_t mode);
extern int close_impl(int fd);
extern ssize_t read_impl(int fd, void *buf, size_t count);

/* Forward declarations for exec variants */
int exec_native(struct task_struct *task, const char *path, int argc, char **argv, char **envp);
int exec_elf(struct task_struct *task, const char *path, int argc, char **argv, char **envp);
int exec_wasi(struct task_struct *task, const char *path, int argc, char **argv, char **envp);
int exec_script(struct task_struct *task, const char *path, int argc, char **argv, char **envp);
int exec_build_script_argv_from_line(const char *shebang_line, const char *path, int argc, char **argv,
                                      char *interpreter_path, size_t interpreter_path_len,
                                      char *optional_arg, size_t optional_arg_len,
                                      char **script_argv, int *script_argc);

struct exec_elf_load_plan {
    Elf64_Ehdr ehdr;
    uint32_t segment_count;
    struct {
        uint64_t vaddr;
        uint64_t memsz;
        uint64_t filesz;
        uint64_t offset;
        uint32_t flags;
    } segments[TASK_EXEC_MAX_LOAD_SEGMENTS];
    uint64_t dynamic_vaddr;
    uint64_t dynamic_size;
    char interpreter[MAX_PATH];
};

#define EXEC_INITIAL_STACK_SIZE (8ULL * 1024ULL * 1024ULL)
#define EXEC_INITIAL_STACK_TOP 0x0000fffffff00000ULL
#define EXEC_INITIAL_STACK_ALIGN 16ULL
#define EXEC_PAGE_SIZE 4096ULL

static uint64_t exec_align_down(uint64_t value, uint64_t align) {
    return value & ~(align - 1);
}

static int exec_string_count(char *const strings[]) {
    int count = 0;

    if (!strings) {
        return 0;
    }
    while (strings[count]) {
        count++;
    }
    return count;
}

static int exec_stack_place_string(uint64_t *cursor, const char *value, uint64_t *out_addr) {
    size_t len;

    if (!cursor || !value || !out_addr) {
        errno = EINVAL;
        return -1;
    }

    len = strlen(value) + 1;
    if (*cursor < EXEC_INITIAL_STACK_TOP - EXEC_INITIAL_STACK_SIZE + len) {
        errno = E2BIG;
        return -1;
    }

    *cursor -= len;
    *out_addr = *cursor;
    return 0;
}

static int exec_stack_offset(const struct mm_struct *mm, uint64_t addr, size_t size, size_t *out_offset) {
    if (!mm || !mm->initial_stack_image || !out_offset) {
        errno = EINVAL;
        return -1;
    }
    if (addr < mm->initial_stack_base ||
        addr > mm->initial_stack_base + mm->initial_stack_image_size ||
        size > (mm->initial_stack_base + mm->initial_stack_image_size) - addr) {
        errno = EFAULT;
        return -1;
    }
    *out_offset = (size_t)(addr - mm->initial_stack_base);
    return 0;
}

static int exec_stack_write(struct mm_struct *mm, uint64_t addr, const void *src, size_t size) {
    size_t offset;

    if (!src && size > 0) {
        errno = EINVAL;
        return -1;
    }
    if (exec_stack_offset(mm, addr, size, &offset) != 0) {
        return -1;
    }
    memcpy((unsigned char *)mm->initial_stack_image + offset, src, size);
    return 0;
}

static int exec_stack_write_u64(struct mm_struct *mm, uint64_t *cursor, uint64_t value) {
    if (!cursor) {
        errno = EINVAL;
        return -1;
    }
    if (exec_stack_write(mm, *cursor, &value, sizeof(value)) != 0) {
        return -1;
    }
    *cursor += sizeof(value);
    return 0;
}

static int exec_stack_write_string(struct mm_struct *mm, uint64_t addr, const char *value) {
    if (!value) {
        errno = EINVAL;
        return -1;
    }
    return exec_stack_write(mm, addr, value, strlen(value) + 1);
}

static int exec_auxv_append(struct mm_struct *mm, uint64_t type, uint64_t value) {
    if (!mm || mm->auxv_count >= TASK_EXEC_MAX_AUXV) {
        errno = E2BIG;
        return -1;
    }

    mm->auxv[mm->auxv_count].type = type;
    mm->auxv[mm->auxv_count].value = value;
    mm->auxv_count++;
    return 0;
}

static uint64_t exec_first_load_vaddr(const struct exec_elf_load_plan *plan) {
    if (!plan || plan->segment_count == 0) {
        return 0;
    }
    return plan->segments[0].vaddr;
}

static uint64_t exec_phdr_vaddr(const struct exec_elf_load_plan *plan) {
    if (!plan || plan->ehdr.e_phnum == 0) {
        return 0;
    }

    for (uint32_t i = 0; i < plan->segment_count; i++) {
        uint64_t offset = plan->segments[i].offset;
        uint64_t filesz = plan->segments[i].filesz;

        if (plan->ehdr.e_phoff >= offset &&
            plan->ehdr.e_phoff < offset + filesz) {
            return plan->segments[i].vaddr + (plan->ehdr.e_phoff - offset);
        }
    }

    return plan->ehdr.e_phoff;
}

static int exec_plan_range_is_loaded(const struct exec_elf_load_plan *plan, uint64_t vaddr, uint64_t size) {
    if (!plan || size == 0 || size > UINT64_MAX - vaddr) {
        return 0;
    }
    for (uint32_t i = 0; i < plan->segment_count; i++) {
        uint64_t start = plan->segments[i].vaddr;
        uint64_t end;

        if (plan->segments[i].memsz > UINT64_MAX - start) {
            continue;
        }
        end = start + plan->segments[i].memsz;
        if (vaddr >= start && vaddr + size <= end) {
            return 1;
        }
    }
    return 0;
}

static void exec_clear_segment_images(struct mm_struct *mm) {
    if (!mm) {
        return;
    }
    for (uint32_t i = 0; i < mm->exec_segment_count; i++) {
        free(mm->exec_segments[i].image);
        mm->exec_segments[i].image = NULL;
        mm->exec_segments[i].image_size = 0;
    }
    for (uint32_t i = 0; i < mm->interp_segment_count; i++) {
        free(mm->interp_segments[i].image);
        mm->interp_segments[i].image = NULL;
        mm->interp_segments[i].image_size = 0;
    }
    task_clear_vmas_impl(mm);
}

static int exec_materialize_segment_image(const unsigned char *image,
                                          size_t image_size,
                                          const struct exec_elf_load_plan *plan,
                                          uint32_t index,
                                          void **out_image,
                                          size_t *out_size) {
    void *segment_image;
    uint64_t offset;
    uint64_t filesz;
    uint64_t memsz;

    if (!image || !plan || !out_image || !out_size || index >= plan->segment_count) {
        errno = EINVAL;
        return -1;
    }

    *out_image = NULL;
    *out_size = 0;
    offset = plan->segments[index].offset;
    filesz = plan->segments[index].filesz;
    memsz = plan->segments[index].memsz;
    if (memsz > SIZE_MAX || filesz > memsz || offset > image_size || filesz > image_size - offset) {
        errno = ENOEXEC;
        return -1;
    }

    segment_image = calloc(1, (size_t)memsz);
    if (!segment_image && memsz > 0) {
        errno = ENOMEM;
        return -1;
    }
    if (filesz > 0) {
        memcpy(segment_image, image + offset, (size_t)filesz);
    }

    *out_image = segment_image;
    *out_size = (size_t)memsz;
    return 0;
}

static int exec_add_vma(struct mm_struct *mm, uint64_t start, uint64_t size,
                        uint32_t flags, enum task_vma_kind kind,
                        void *image, size_t image_size) {
    uint32_t *page_flags;
    uint64_t page_count;

    if (!mm || size == 0) {
        return 0;
    }
    if (!image || image_size == 0 || image_size < size ||
        mm->vma_count >= TASK_EXEC_MAX_VMAS || size > UINT64_MAX - start) {
        errno = ENOEXEC;
        return -1;
    }
    for (uint32_t i = 0; i < mm->vma_count; i++) {
        uint64_t end = start + size;
        if (start < mm->vmas[i].end && end > mm->vmas[i].start) {
            errno = ENOEXEC;
            return -1;
        }
    }
    page_count = size / TASK_VMA_PAGE_SIZE;
    if ((size % TASK_VMA_PAGE_SIZE) != 0) {
        page_count++;
    }
    if (page_count == 0 || page_count > SIZE_MAX / sizeof(*page_flags)) {
        errno = ENOEXEC;
        return -1;
    }
    page_flags = calloc((size_t)page_count, sizeof(*page_flags));
    if (!page_flags) {
        errno = ENOMEM;
        return -1;
    }
    for (uint64_t i = 0; i < page_count; i++) {
        page_flags[i] = flags;
    }
    mm->vmas[mm->vma_count].start = start;
    mm->vmas[mm->vma_count].end = start + size;
    mm->vmas[mm->vma_count].flags = flags;
    mm->vmas[mm->vma_count].kind = kind;
    mm->vmas[mm->vma_count].image = image;
    mm->vmas[mm->vma_count].image_size = image_size;
    mm->vmas[mm->vma_count].page_count = page_count;
    mm->vmas[mm->vma_count].page_flags = page_flags;
    mm->vma_count++;
    return 0;
}

static int exec_dynamic_bytes(struct task_struct *task, uint64_t vaddr, uint64_t size,
                              const Elf64_Dyn **out_entries, uint64_t *out_count) {
    const struct task_vma *vma;
    uint64_t offset;

    if (!task || !task->mm || !out_entries || !out_count || size == 0 ||
        size % sizeof(Elf64_Dyn) != 0 || size > UINT64_MAX - vaddr) {
        errno = ENOEXEC;
        return -1;
    }

    vma = task_find_vma_impl(task, vaddr);
    if (!vma || vaddr + size > vma->end || !vma->image) {
        errno = ENOEXEC;
        return -1;
    }
    offset = vaddr - vma->start;
    if (offset > vma->image_size || size > vma->image_size - offset) {
        errno = ENOEXEC;
        return -1;
    }

    *out_entries = (const Elf64_Dyn *)((const unsigned char *)vma->image + offset);
    *out_count = size / sizeof(Elf64_Dyn);
    return 0;
}

static int exec_parse_dynamic_info(struct task_struct *task,
                                   uint64_t vaddr,
                                   uint64_t size,
                                   struct task_dynamic_info *info) {
    const Elf64_Dyn *entries;
    uint64_t entry_count;

    if (!info) {
        errno = EINVAL;
        return -1;
    }

    memset(info, 0, sizeof(*info));
    info->vaddr = vaddr;
    info->size = size;
    if (size == 0) {
        return 0;
    }

    if (exec_dynamic_bytes(task, vaddr, size, &entries, &entry_count) != 0) {
        return -1;
    }

    for (uint64_t i = 0; i < entry_count; i++) {
        switch (entries[i].d_tag) {
        case DT_NULL:
            return 0;
        case DT_NEEDED:
            if (info->needed_count >= TASK_EXEC_MAX_DYNAMIC_NEEDED) {
                errno = ENOEXEC;
                return -1;
            }
            info->needed_offsets[info->needed_count++] = entries[i].d_un.d_val;
            break;
        case DT_STRTAB:
            info->strtab_vaddr = entries[i].d_un.d_ptr;
            break;
        case DT_STRSZ:
            info->strtab_size = entries[i].d_un.d_val;
            break;
        case DT_SYMTAB:
            info->symtab_vaddr = entries[i].d_un.d_ptr;
            break;
        case DT_RELA:
            info->rela_vaddr = entries[i].d_un.d_ptr;
            break;
        case DT_RELASZ:
            info->rela_size = entries[i].d_un.d_val;
            break;
        case DT_RELAENT:
            info->rela_entry_size = entries[i].d_un.d_val;
            break;
        case DT_JMPREL:
            info->plt_rela_vaddr = entries[i].d_un.d_ptr;
            break;
        case DT_PLTRELSZ:
            info->plt_rela_size = entries[i].d_un.d_val;
            break;
        case DT_PLTREL:
            info->plt_rela_type = entries[i].d_un.d_val;
            break;
        default:
            break;
        }
    }

    errno = ENOEXEC;
    return -1;
}

static int exec_build_initial_elf_stack(struct task_struct *task,
                                        const struct exec_elf_load_plan *plan,
                                        const struct exec_elf_load_plan *interp_plan) {
    struct mm_struct *mm;
    const struct cred *cred;
    uint64_t cursor = EXEC_INITIAL_STACK_TOP;
    uint64_t random_addr;
    uint64_t pointer_slots;
    const unsigned char random_bytes[16] = {
        0x49, 0x58, 0x4c, 0x41, 0x4e, 0x44, 0x5f, 0x41,
        0x55, 0x58, 0x56, 0x5f, 0x52, 0x4e, 0x44, 0x00,
    };

    if (!task || !task->mm || !plan) {
        errno = EINVAL;
        return -1;
    }

    mm = task->mm;
    memset(mm->initial_argv, 0, sizeof(mm->initial_argv));
    memset(mm->initial_envp, 0, sizeof(mm->initial_envp));
    memset(mm->auxv, 0, sizeof(mm->auxv));

    mm->initial_stack_base = EXEC_INITIAL_STACK_TOP - EXEC_INITIAL_STACK_SIZE;
    mm->initial_stack_size = EXEC_INITIAL_STACK_SIZE;
    free(mm->initial_stack_image);
    mm->initial_stack_image = calloc(1, EXEC_INITIAL_STACK_SIZE);
    if (!mm->initial_stack_image) {
        mm->initial_stack_image_size = 0;
        errno = ENOMEM;
        return -1;
    }
    mm->initial_stack_image_size = EXEC_INITIAL_STACK_SIZE;
    mm->initial_argc = exec_string_count(task->argv);
    mm->initial_envc = exec_string_count(task->envp);
    mm->auxv_count = 0;

    if (exec_stack_place_string(&cursor, "aarch64", &mm->auxv_platform_addr) != 0 ||
        exec_stack_place_string(&cursor, task->exe, &mm->auxv_execfn_addr) != 0) {
        return -1;
    }

    if (cursor < mm->initial_stack_base + 16) {
        errno = E2BIG;
        return -1;
    }
    cursor -= 16;
    random_addr = cursor;
    mm->auxv_random_addr = random_addr;

    for (int i = mm->initial_envc - 1; i >= 0; i--) {
        if (exec_stack_place_string(&cursor, task->envp[i], &mm->initial_envp[i]) != 0) {
            return -1;
        }
    }
    for (int i = mm->initial_argc - 1; i >= 0; i--) {
        if (exec_stack_place_string(&cursor, task->argv[i], &mm->initial_argv[i]) != 0) {
            return -1;
        }
    }

    cursor = exec_align_down(cursor, EXEC_INITIAL_STACK_ALIGN);

    cred = get_current_cred();
    if (exec_auxv_append(mm, AT_PHDR, exec_phdr_vaddr(plan)) != 0 ||
        exec_auxv_append(mm, AT_PHENT, sizeof(Elf64_Phdr)) != 0 ||
        exec_auxv_append(mm, AT_PHNUM, plan->ehdr.e_phnum) != 0 ||
        exec_auxv_append(mm, AT_PAGESZ, EXEC_PAGE_SIZE) != 0 ||
        exec_auxv_append(mm, AT_BASE, interp_plan ? exec_first_load_vaddr(interp_plan) : 0) != 0 ||
        exec_auxv_append(mm, AT_FLAGS, 0) != 0 ||
        exec_auxv_append(mm, AT_ENTRY, plan->ehdr.e_entry) != 0 ||
        exec_auxv_append(mm, AT_UID, cred ? cred->uid : 0) != 0 ||
        exec_auxv_append(mm, AT_EUID, cred ? cred->euid : 0) != 0 ||
        exec_auxv_append(mm, AT_GID, cred ? cred->gid : 0) != 0 ||
        exec_auxv_append(mm, AT_EGID, cred ? cred->egid : 0) != 0 ||
        exec_auxv_append(mm, AT_SECURE, 0) != 0 ||
        exec_auxv_append(mm, AT_RANDOM, mm->auxv_random_addr) != 0 ||
        exec_auxv_append(mm, AT_PLATFORM, mm->auxv_platform_addr) != 0 ||
        exec_auxv_append(mm, AT_EXECFN, mm->auxv_execfn_addr) != 0 ||
        exec_auxv_append(mm, AT_NULL, 0) != 0) {
        return -1;
    }

    pointer_slots = 1ULL +
                    (uint64_t)mm->initial_argc + 1ULL +
                    (uint64_t)mm->initial_envc + 1ULL +
                    ((uint64_t)mm->auxv_count * 2ULL);
    if (cursor < mm->initial_stack_base + (pointer_slots * sizeof(uint64_t))) {
        errno = E2BIG;
        return -1;
    }
    cursor -= pointer_slots * sizeof(uint64_t);
    mm->initial_stack_pointer = exec_align_down(cursor, EXEC_INITIAL_STACK_ALIGN);
    if (mm->initial_stack_pointer < mm->initial_stack_base ||
        mm->initial_stack_pointer >= EXEC_INITIAL_STACK_TOP) {
        errno = E2BIG;
        return -1;
    }

    if (exec_stack_write_string(mm, mm->auxv_platform_addr, "aarch64") != 0 ||
        exec_stack_write_string(mm, mm->auxv_execfn_addr, task->exe) != 0 ||
        exec_stack_write(mm, mm->auxv_random_addr, random_bytes, sizeof(random_bytes)) != 0) {
        return -1;
    }

    for (int i = 0; i < mm->initial_argc; i++) {
        if (exec_stack_write_string(mm, mm->initial_argv[i], task->argv[i]) != 0) {
            return -1;
        }
    }
    for (int i = 0; i < mm->initial_envc; i++) {
        if (exec_stack_write_string(mm, mm->initial_envp[i], task->envp[i]) != 0) {
            return -1;
        }
    }

    cursor = mm->initial_stack_pointer;
    if (exec_stack_write_u64(mm, &cursor, (uint64_t)mm->initial_argc) != 0) {
        return -1;
    }
    for (int i = 0; i < mm->initial_argc; i++) {
        if (exec_stack_write_u64(mm, &cursor, mm->initial_argv[i]) != 0) {
            return -1;
        }
    }
    if (exec_stack_write_u64(mm, &cursor, 0) != 0) {
        return -1;
    }
    for (int i = 0; i < mm->initial_envc; i++) {
        if (exec_stack_write_u64(mm, &cursor, mm->initial_envp[i]) != 0) {
            return -1;
        }
    }
    if (exec_stack_write_u64(mm, &cursor, 0) != 0) {
        return -1;
    }
    for (uint32_t i = 0; i < mm->auxv_count; i++) {
        if (exec_stack_write_u64(mm, &cursor, mm->auxv[i].type) != 0 ||
            exec_stack_write_u64(mm, &cursor, mm->auxv[i].value) != 0) {
            return -1;
        }
    }

    return 0;
}

/* Deep copy argv array */
static char **exec_copy_argv(char *const argv[]) {
    if (!argv) {
        return NULL;
    }

    int argc = 0;
    while (argv[argc]) {
        argc++;
    }

    char **copy = calloc(argc + 1, sizeof(char *));
    if (!copy) {
        return NULL;
    }

    for (int i = 0; i < argc; i++) {
        copy[i] = strdup(argv[i]);
        if (!copy[i]) {
            for (int j = 0; j < i; j++) {
                free(copy[j]);
            }
            free(copy);
            return NULL;
        }
    }

    return copy;
}

/* Deep copy envp array */
static char **exec_copy_envp(char *const envp[]) {
    if (!envp) {
        return NULL;
    }

    int envc = 0;
    while (envp[envc]) {
        envc++;
    }

    char **copy = calloc(envc + 1, sizeof(char *));
    if (!copy) {
        return NULL;
    }

    for (int i = 0; i < envc; i++) {
        copy[i] = strdup(envp[i]);
        if (!copy[i]) {
            for (int j = 0; j < i; j++) {
                free(copy[j]);
            }
            free(copy);
            return NULL;
        }
    }

    return copy;
}

/* Free copied argv */
static void exec_free_argv(char **argv) {
    if (!argv) {
        return;
    }

    for (int i = 0; argv[i]; i++) {
        free(argv[i]);
    }
    free(argv);
}

/* Internal: Ensure task has an exec_image allocated */
static int exec_image_ensure(struct task_struct *task) {
    if (!task) {
        errno = EINVAL;
        return -1;
    }

    if (task->exec_image) {
        return 0;
    }

    task->exec_image = calloc(1, sizeof(struct exec_image));
    if (!task->exec_image) {
        errno = ENOMEM;
        return -1;
    }

    return 0;
}

static void exec_record_script_image(struct task_struct *task, const char *path, const char *interpreter_path) {
    if (!task || !task->exec_image || !path || !interpreter_path) {
        return;
    }

    strncpy(task->exec_image->path, path, sizeof(task->exec_image->path) - 1);
    task->exec_image->path[sizeof(task->exec_image->path) - 1] = '\0';
    if (vfs_normalize_linux_path(interpreter_path, task->exec_image->interpreter,
                                 sizeof(task->exec_image->interpreter)) != 0) {
        task->exec_image->interpreter[0] = '\0';
    }
    task->exec_image->type = EXEC_IMAGE_SCRIPT;
}

static int exec_elf_header_is_magic(const Elf64_Ehdr *ehdr) {
    return ehdr &&
           ehdr->e_ident[EI_MAG0] == ELFMAG0 &&
           ehdr->e_ident[EI_MAG1] == ELFMAG1 &&
           ehdr->e_ident[EI_MAG2] == ELFMAG2 &&
           ehdr->e_ident[EI_MAG3] == ELFMAG3;
}

static int exec_validate_elf64_aarch64(const Elf64_Ehdr *ehdr) {
    if (!exec_elf_header_is_magic(ehdr)) {
        errno = ENOEXEC;
        return -1;
    }
    if (ehdr->e_ident[EI_CLASS] != ELFCLASS64 ||
        ehdr->e_ident[EI_DATA] != ELFDATA2LSB ||
        ehdr->e_ident[EI_VERSION] != EV_CURRENT ||
        ehdr->e_version != EV_CURRENT ||
        ehdr->e_machine != EM_AARCH64 ||
        (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN) ||
        ehdr->e_ehsize != sizeof(Elf64_Ehdr)) {
        errno = ENOEXEC;
        return -1;
    }
    return 0;
}

static int exec_elf_range_in_image(uint64_t offset, uint64_t size, size_t image_size) {
    if (offset > (uint64_t)image_size) {
        return 0;
    }
    if (size > (uint64_t)image_size - offset) {
        return 0;
    }
    return 1;
}

static int exec_build_elf_load_plan(const void *image, size_t image_size, struct exec_elf_load_plan *plan) {
    const unsigned char *bytes = image;

    if (!image || !plan || image_size < sizeof(Elf64_Ehdr)) {
        errno = ENOEXEC;
        return -1;
    }

    memset(plan, 0, sizeof(*plan));
    memcpy(&plan->ehdr, image, sizeof(plan->ehdr));
    if (exec_validate_elf64_aarch64(&plan->ehdr) != 0) {
        return -1;
    }

    if (plan->ehdr.e_phnum > 0) {
        uint64_t ph_size;

        if (plan->ehdr.e_phentsize != sizeof(Elf64_Phdr)) {
            errno = ENOEXEC;
            return -1;
        }
        ph_size = (uint64_t)plan->ehdr.e_phentsize * (uint64_t)plan->ehdr.e_phnum;
        if (!exec_elf_range_in_image(plan->ehdr.e_phoff, ph_size, image_size)) {
            errno = ENOEXEC;
            return -1;
        }
    }

    for (uint16_t i = 0; i < plan->ehdr.e_phnum; i++) {
        Elf64_Phdr phdr;
        uint64_t phoff = plan->ehdr.e_phoff + ((uint64_t)i * (uint64_t)plan->ehdr.e_phentsize);

        memcpy(&phdr, bytes + phoff, sizeof(phdr));
        if (phdr.p_type == PT_LOAD) {
            if (plan->segment_count >= TASK_EXEC_MAX_LOAD_SEGMENTS ||
                phdr.p_filesz > phdr.p_memsz ||
                !exec_elf_range_in_image(phdr.p_offset, phdr.p_filesz, image_size)) {
                errno = ENOEXEC;
                return -1;
            }

            plan->segments[plan->segment_count].vaddr = phdr.p_vaddr;
            plan->segments[plan->segment_count].memsz = phdr.p_memsz;
            plan->segments[plan->segment_count].filesz = phdr.p_filesz;
            plan->segments[plan->segment_count].offset = phdr.p_offset;
            plan->segments[plan->segment_count].flags = phdr.p_flags;
            plan->segment_count++;
        } else if (phdr.p_type == PT_DYNAMIC) {
            if (phdr.p_memsz == 0 ||
                phdr.p_filesz > phdr.p_memsz ||
                !exec_elf_range_in_image(phdr.p_offset, phdr.p_filesz, image_size)) {
                errno = ENOEXEC;
                return -1;
            }
            plan->dynamic_vaddr = phdr.p_vaddr;
            plan->dynamic_size = phdr.p_memsz;
        } else if (phdr.p_type == PT_INTERP) {
            size_t interp_len;

            if (phdr.p_filesz == 0 ||
                phdr.p_filesz >= sizeof(plan->interpreter) ||
                !exec_elf_range_in_image(phdr.p_offset, phdr.p_filesz, image_size)) {
                errno = ENOEXEC;
                return -1;
            }
            interp_len = (size_t)phdr.p_filesz;
            memcpy(plan->interpreter, bytes + phdr.p_offset, interp_len);
            plan->interpreter[sizeof(plan->interpreter) - 1] = '\0';
            if (plan->interpreter[interp_len - 1] != '\0') {
                errno = ENOEXEC;
                return -1;
            }
        }
    }

    if (plan->dynamic_size != 0 &&
        !exec_plan_range_is_loaded(plan, plan->dynamic_vaddr, plan->dynamic_size)) {
        errno = ENOEXEC;
        return -1;
    }

    return 0;
}

static int exec_read_image_file(const char *path, void **out_image, size_t *out_size) {
    int fd;
    void *image;
    size_t image_capacity = 4096;
    size_t offset = 0;

    if (!path || !out_image || !out_size) {
        errno = EINVAL;
        return -1;
    }

    fd = open_impl(path, O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }

    image = malloc(image_capacity);
    if (!image) {
        close_impl(fd);
        errno = ENOMEM;
        return -1;
    }

    for (;;) {
        ssize_t nread;

        if (offset == image_capacity) {
            size_t new_capacity = image_capacity * 2;
            void *new_image = realloc(image, new_capacity);
            if (!new_image) {
                free(image);
                close_impl(fd);
                errno = ENOMEM;
                return -1;
            }
            image = new_image;
            image_capacity = new_capacity;
        }

        nread = read_impl(fd, (char *)image + offset, image_capacity - offset);
        if (nread < 0) {
            int saved_errno = errno;
            free(image);
            close_impl(fd);
            errno = saved_errno;
            return -1;
        }
        if (nread == 0) {
            break;
        }
        offset += (size_t)nread;
    }

    close_impl(fd);
    *out_image = image;
    *out_size = offset;
    return 0;
}

static int exec_read_elf_load_plan(const char *path, void **out_image, size_t *out_size,
                                   struct exec_elf_load_plan *out_plan) {
    void *image = NULL;
    size_t image_size = 0;

    if (!path || !out_image || !out_size || !out_plan) {
        errno = EINVAL;
        return -1;
    }

    if (exec_read_image_file(path, &image, &image_size) != 0) {
        return -1;
    }
    if (exec_build_elf_load_plan(image, image_size, out_plan) != 0) {
        free(image);
        return -1;
    }

    *out_image = image;
    *out_size = image_size;
    return 0;
}

static int exec_resolve_elf_interpreter(const char *interpreter, char *resolved, size_t resolved_len) {
    int ret;

    if (!interpreter || interpreter[0] == '\0' || !resolved || resolved_len == 0) {
        errno = EINVAL;
        return -1;
    }

    ret = vfs_resolve_virtual_path_task_follow(interpreter, resolved, resolved_len,
                                               get_current() ? get_current()->fs : NULL, true);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }
    return 0;
}

static int exec_validate_elf_interpreter_for_path(const char *path) {
    void *image = NULL;
    size_t image_size = 0;
    struct exec_elf_load_plan plan;

    if (exec_read_elf_load_plan(path, &image, &image_size, &plan) != 0) {
        return -1;
    }
    free(image);

    if (plan.interpreter[0] != '\0') {
        char resolved_interp[MAX_PATH];
        void *interp_image = NULL;
        size_t interp_image_size = 0;
        struct exec_elf_load_plan interp_plan;

        if (exec_resolve_elf_interpreter(plan.interpreter, resolved_interp, sizeof(resolved_interp)) != 0) {
            return -1;
        }
        if (exec_read_elf_load_plan(resolved_interp, &interp_image, &interp_image_size, &interp_plan) != 0) {
            return -1;
        }
        free(interp_image);
    }

    (void)image_size;
    return 0;
}

static int exec_read_shebang_line(const char *path, char *buffer, size_t buffer_len) {
    char resolved_path[MAX_PATH];
    if (!path || !buffer || buffer_len < 3) {
        errno = EINVAL;
        return -1;
    }

    int ret = vfs_resolve_virtual_path_task_follow(path, resolved_path, sizeof(resolved_path), NULL, true);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }

    int fd = open_impl(resolved_path, O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }

    ssize_t nread = read_impl(fd, buffer, buffer_len - 1);
    int saved_errno = errno;
    close_impl(fd);
    errno = saved_errno;
    if (nread < 0) {
        return -1;
    }

    buffer[nread] = '\0';
    char *newline = strchr(buffer, '\n');
    if (newline) {
        *newline = '\0';
    }

    return 0;
}

int exec_build_script_argv(const char *path, int argc, char **argv,
                           char *interpreter_path, size_t interpreter_path_len,
                           char *optional_arg, size_t optional_arg_len,
                           char **script_argv, int *script_argc) {
    if (!path || !interpreter_path || interpreter_path_len == 0 ||
        !optional_arg || optional_arg_len == 0 || !script_argv || !script_argc) {
        errno = EINVAL;
        return -1;
    }

    char shebang[MAX_PATH];
    if (exec_read_shebang_line(path, shebang, sizeof(shebang)) < 0) {
        return -1;
    }

    return exec_build_script_argv_from_line(shebang, path, argc, argv,
                                             interpreter_path, interpreter_path_len,
                                             optional_arg, optional_arg_len,
                                             script_argv, script_argc);
}

int exec_build_script_argv_from_line(const char *shebang_line, const char *path, int argc, char **argv,
                                      char *interpreter_path, size_t interpreter_path_len,
                                      char *optional_arg, size_t optional_arg_len,
                                      char **script_argv, int *script_argc) {
    if (!shebang_line || !path || !interpreter_path || interpreter_path_len == 0 ||
        !optional_arg || optional_arg_len == 0 || !script_argv || !script_argc) {
        errno = EINVAL;
        return -1;
    }

    optional_arg[0] = '\0';

    if (shebang_line[0] != '#' || shebang_line[1] != '!') {
        errno = ENOEXEC;
        return -1;
    }

    char linebuf[MAX_PATH];
    strncpy(linebuf, shebang_line, sizeof(linebuf) - 1);
    linebuf[sizeof(linebuf) - 1] = '\0';

    char *cursor = linebuf + 2;
    while (*cursor && isspace((unsigned char)*cursor)) {
        cursor++;
    }

    if (*cursor == '\0') {
        errno = ENOEXEC;
        return -1;
    }

    char *interpreter = cursor;
    while (*cursor && !isspace((unsigned char)*cursor)) {
        cursor++;
    }
    if (*cursor) {
        *cursor = '\0';
        cursor++;
    }

    while (*cursor && isspace((unsigned char)*cursor)) {
        cursor++;
    }
    char *optional = cursor;
    char *end = optional + strlen(optional);
    while (end > optional && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = '\0';

    if (interpreter[0] == '\0') {
        errno = ENOEXEC;
        return -1;
    }

    if (strlen(interpreter) >= interpreter_path_len) {
        errno = ENAMETOOLONG;
        return -1;
    }

    strcpy(interpreter_path, interpreter);
    if (optional[0] != '\0') {
        if (strlen(optional) >= optional_arg_len) {
            errno = ENAMETOOLONG;
            return -1;
        }
        strcpy(optional_arg, optional);
    }

    int outc = 0;
    script_argv[outc++] = interpreter_path;
    if (optional_arg[0] != '\0' && outc < TASK_MAX_ARGS - 1) {
        script_argv[outc++] = optional_arg;
    }
    script_argv[outc++] = (char *)path;
    for (int i = 1; i < argc && outc < TASK_MAX_ARGS - 1; i++) {
        script_argv[outc++] = argv[i];
    }
    script_argv[outc] = NULL;
    *script_argc = outc;

    return 0;
}

enum exec_image_type exec_classify(const char *path) {
    char resolved_path[MAX_PATH];
    int ret;

    if (!path) {
        return EXEC_IMAGE_NONE;
    }

    ret = vfs_resolve_virtual_path_task_follow(path, resolved_path, sizeof(resolved_path), NULL, true);
    if (ret != 0) {
        return EXEC_IMAGE_NONE;
    }

    if (native_lookup(resolved_path)) {
        return EXEC_IMAGE_NATIVE;
    }

    int fd = open_impl(resolved_path, O_RDONLY, 0);
    if (fd < 0) {
        return EXEC_IMAGE_NONE;
    }

    unsigned char magic[4];
    ssize_t n = read_impl(fd, magic, 4);
    close_impl(fd);

    if (n < 2) {
        return EXEC_IMAGE_NONE;
    }

    if (n >= 4 && magic[0] == ELFMAG0 && magic[1] == ELFMAG1 &&
        magic[2] == ELFMAG2 && magic[3] == ELFMAG3) {
        void *image = NULL;
        size_t image_size = 0;
        struct exec_elf_load_plan plan;
        if (exec_read_image_file(resolved_path, &image, &image_size) != 0 ||
            exec_build_elf_load_plan(image, image_size, &plan) != 0) {
            free(image);
            return EXEC_IMAGE_INVALID;
        }
        free(image);
        return EXEC_IMAGE_ELF;
    }

    if (n >= 4 && magic[0] == 0x00 && magic[1] == 0x61 && magic[2] == 0x73 && magic[3] == 0x6d) {
        return EXEC_IMAGE_WASI;
    }

    if (magic[0] == '#' && magic[1] == '!') {
        return EXEC_IMAGE_SCRIPT;
    }

    return EXEC_IMAGE_NONE;
}

void exec_reset_signals(struct signal_struct *sighand) {
    if (!sighand) {
        return;
    }

    for (int i = 0; i < KERNEL_SIG_NUM; i++) {
        if (sighand->actions[i].handler != SIG_IGN) {
            sighand->actions[i].handler = SIG_DFL;
        }
    }

    memset(&sighand->blocked, 0, sizeof(sighand->blocked));
    memset(&sighand->pending, 0, sizeof(sighand->pending));
}

int execve(const char *pathname, char *const argv[], char *const envp[]) {
    char resolved_path[MAX_PATH];
    if (!pathname) {
        errno = EFAULT;
        return -1;
    }

    if (pathname[0] == '\0') {
        errno = ENOENT;
        return -1;
    }

    struct task_struct *task = get_current();
    if (!task) {
        errno = ESRCH;
        return -1;
    }

    int ret = vfs_resolve_virtual_path_task_follow(pathname, resolved_path, sizeof(resolved_path), task->fs, true);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }

    int type = exec_classify(resolved_path);
    if (type == EXEC_IMAGE_NONE) {
        errno = ENOENT;
        return -1;
    }
    if (type == EXEC_IMAGE_INVALID) {
        errno = ENOEXEC;
        return -1;
    }
    if (type == EXEC_IMAGE_WASI) {
        errno = ENOEXEC;
        return -1;
    }

    char **argv_copy = exec_copy_argv(argv);
    char **envp_copy = exec_copy_envp(envp);

    if (argv && !argv_copy) {
        errno = ENOMEM;
        return -1;
    }
    if (envp && !envp_copy) {
        exec_free_argv(argv_copy);
        errno = ENOMEM;
        return -1;
    }

    if (exec_image_ensure(task) < 0) {
        exec_free_argv(argv_copy);
        exec_free_argv(envp_copy);
        return -1;
    }

    int argc = 0;
    if (argv_copy) {
        while (argv_copy[argc]) {
            argc++;
        }
    }

    int launch_status;
    if (type == EXEC_IMAGE_SCRIPT) {
        char interpreter_path[MAX_PATH];
        char optional_arg[MAX_PATH];
        char *script_argv[TASK_MAX_ARGS + 4];
        int script_argc = 0;
        native_entry_fn entry;

        if (exec_build_script_argv(resolved_path, argc, argv_copy,
                                   interpreter_path, sizeof(interpreter_path),
                                   optional_arg, sizeof(optional_arg),
                                   script_argv, &script_argc) < 0) {
            exec_free_argv(argv_copy);
            exec_free_argv(envp_copy);
            return -1;
        }

        entry = native_lookup(interpreter_path);
        if (!entry) {
            exec_free_argv(argv_copy);
            exec_free_argv(envp_copy);
            errno = ENOENT;
            return -1;
        }

        if (task_record_exec_strings_impl(script_argv, envp_copy) < 0) {
            exec_free_argv(argv_copy);
            exec_free_argv(envp_copy);
            return -1;
        }
        if (task_exec_transition_impl(resolved_path, argv_copy ? argv_copy[0] : NULL) < 0) {
            exec_free_argv(argv_copy);
            exec_free_argv(envp_copy);
            return -1;
        }
        if (task->signal) {
            exec_reset_signals(task->signal);
        }
        exec_record_script_image(task, resolved_path, interpreter_path);
        launch_status = entry(script_argc, script_argv, envp_copy);
    } else if (type == EXEC_IMAGE_ELF) {
        if (exec_validate_elf_interpreter_for_path(resolved_path) != 0) {
            exec_free_argv(argv_copy);
            exec_free_argv(envp_copy);
            return -1;
        }
        if (task_record_exec_strings_impl(argv_copy, envp_copy) < 0) {
            exec_free_argv(argv_copy);
            exec_free_argv(envp_copy);
            return -1;
        }
        if (task_exec_transition_impl(resolved_path, argv_copy ? argv_copy[0] : NULL) < 0) {
            exec_free_argv(argv_copy);
            exec_free_argv(envp_copy);
            return -1;
        }
        if (task->signal) {
            exec_reset_signals(task->signal);
        }
        launch_status = exec_elf(task, resolved_path, argc, argv_copy, envp_copy);
    } else {
        if (task_record_exec_strings_impl(argv_copy, envp_copy) < 0) {
            exec_free_argv(argv_copy);
            exec_free_argv(envp_copy);
            return -1;
        }
        if (task_exec_transition_impl(resolved_path, argv_copy ? argv_copy[0] : NULL) < 0) {
            exec_free_argv(argv_copy);
            exec_free_argv(envp_copy);
            return -1;
        }
        if (task->signal) {
            exec_reset_signals(task->signal);
        }
        launch_status = exec_native(task, resolved_path, argc, argv_copy, envp_copy);
    }

    exec_free_argv(argv_copy);
    exec_free_argv(envp_copy);

    return launch_status;
}

int execv(const char *pathname, char *const argv[]) {
    return execve(pathname, argv, environ);
}

int execvp(const char *file, char *const argv[]) {
    if (strchr(file, '/') != NULL) {
        return execv(file, argv);
    }

    const char *path_env = getenv("PATH");
    if (path_env == NULL) {
        path_env = "/usr/bin:/bin";
    }

    char *path_copy = strdup(path_env);
    if (path_copy == NULL) {
        return -1;
    }

    char *saveptr = NULL;
    char *dir = strtok_r(path_copy, ":", &saveptr);

    while (dir != NULL) {
        char fullpath[MAX_PATH];
        int len = snprintf(fullpath, sizeof(fullpath), "%s/%s", dir, file);

        if (len > 0 && (size_t)len < sizeof(fullpath)) {
            int result = execv(fullpath, argv);
            if (result != -1) {
                free(path_copy);
                return result;
            }
            if (errno != ENOENT && errno != ENOTDIR) {
                free(path_copy);
                return -1;
            }
        }

        dir = strtok_r(NULL, ":", &saveptr);
    }

    free(path_copy);
    errno = ENOENT;
    return -1;
}

int fexecve(int fd, char *const argv[], char *const envp[]) {
    char path[MAX_PATH];
    fd_entry_t *entry = get_fd_entry_impl(fd);
    if (!entry) {
        errno = EBADF;
        return -1;
    }

    int ret = get_fd_path_impl(entry, path, sizeof(path));
    int saved_errno = errno;
    put_fd_entry_impl(entry);
    errno = saved_errno;
    if (ret != 0) {
        return -1;
    }

    return execve(path, argv, envp);
}

int exec_native(struct task_struct *task, const char *path, int argc, char **argv, char **envp) {
    native_program_t program;
    if (native_lookup_program(path, &program) != 0) {
        errno = ENOENT;
        return -1;
    }

    strncpy(task->exec_image->path, path, sizeof(task->exec_image->path) - 1);
    task->exec_image->path[sizeof(task->exec_image->path) - 1] = '\0';
    task->exec_image->type = EXEC_IMAGE_NATIVE;

    return program.entry(argc, argv, envp);
}

int exec_elf(struct task_struct *task, const char *path, int argc, char **argv, char **envp) {
    void *image = NULL;
    size_t image_size = 0;
    struct exec_elf_load_plan plan;
    void *interp_image = NULL;
    size_t interp_image_size = 0;
    struct exec_elf_load_plan interp_plan;
    char resolved_interp[MAX_PATH] = {0};

    (void)argc;
    (void)argv;
    (void)envp;

    if (!task || !path || !task->exec_image) {
        errno = EINVAL;
        return -1;
    }

    if (exec_read_elf_load_plan(path, &image, &image_size, &plan) != 0) {
        return -1;
    }

    if (plan.interpreter[0] != '\0') {
        if (exec_resolve_elf_interpreter(plan.interpreter, resolved_interp, sizeof(resolved_interp)) != 0) {
            free(image);
            return -1;
        }
        if (exec_read_elf_load_plan(resolved_interp, &interp_image, &interp_image_size, &interp_plan) != 0) {
            free(image);
            return -1;
        }
    }

    if (!task->mm) {
        task->mm = calloc(1, sizeof(*task->mm));
        if (!task->mm) {
            free(image);
            free(interp_image);
            errno = ENOMEM;
            return -1;
        }
    }

    exec_clear_segment_images(task->mm);
    free(task->mm->exec_image_base);
    free(task->mm->interp_image_base);
    task->mm->exec_image_base = image;
    task->mm->exec_image_size = image_size;
    task->mm->exec_entry = plan.ehdr.e_entry;
    task->mm->exec_dynamic_vaddr = plan.dynamic_vaddr;
    task->mm->exec_dynamic_size = plan.dynamic_size;
    memset(&task->mm->exec_dynamic, 0, sizeof(task->mm->exec_dynamic));
    task->mm->exec_segment_count = plan.segment_count;
    memset(task->mm->exec_segments, 0, sizeof(task->mm->exec_segments));
    for (uint32_t i = 0; i < plan.segment_count; i++) {
        task->mm->exec_segments[i].vaddr = plan.segments[i].vaddr;
        task->mm->exec_segments[i].memsz = plan.segments[i].memsz;
        task->mm->exec_segments[i].filesz = plan.segments[i].filesz;
        task->mm->exec_segments[i].offset = plan.segments[i].offset;
        task->mm->exec_segments[i].flags = plan.segments[i].flags;
        if (exec_materialize_segment_image(task->mm->exec_image_base, task->mm->exec_image_size,
                                           &plan, i,
                                           &task->mm->exec_segments[i].image,
                                           &task->mm->exec_segments[i].image_size) != 0) {
            return -1;
        }
        if (exec_add_vma(task->mm,
                         task->mm->exec_segments[i].vaddr,
                         task->mm->exec_segments[i].image_size,
                         task->mm->exec_segments[i].flags,
                         TASK_VMA_EXEC,
                         task->mm->exec_segments[i].image,
                         task->mm->exec_segments[i].image_size) != 0) {
            return -1;
        }
    }

    task->mm->interp_image_base = interp_image;
    task->mm->interp_image_size = interp_image_size;
    task->mm->interp_entry = 0;
    task->mm->interp_dynamic_vaddr = 0;
    task->mm->interp_dynamic_size = 0;
    memset(&task->mm->interp_dynamic, 0, sizeof(task->mm->interp_dynamic));
    task->mm->interp_segment_count = 0;
    task->mm->interp_path[0] = '\0';
    memset(task->mm->interp_segments, 0, sizeof(task->mm->interp_segments));
    task->mm->entry_point = plan.ehdr.e_entry;
    if (interp_image) {
        task->mm->interp_entry = interp_plan.ehdr.e_entry;
        task->mm->interp_dynamic_vaddr = interp_plan.dynamic_vaddr;
        task->mm->interp_dynamic_size = interp_plan.dynamic_size;
        task->mm->interp_segment_count = interp_plan.segment_count;
        for (uint32_t i = 0; i < interp_plan.segment_count; i++) {
            task->mm->interp_segments[i].vaddr = interp_plan.segments[i].vaddr;
            task->mm->interp_segments[i].memsz = interp_plan.segments[i].memsz;
            task->mm->interp_segments[i].filesz = interp_plan.segments[i].filesz;
            task->mm->interp_segments[i].offset = interp_plan.segments[i].offset;
            task->mm->interp_segments[i].flags = interp_plan.segments[i].flags;
            if (exec_materialize_segment_image(task->mm->interp_image_base, task->mm->interp_image_size,
                                               &interp_plan, i,
                                               &task->mm->interp_segments[i].image,
                                               &task->mm->interp_segments[i].image_size) != 0) {
                return -1;
            }
            if (exec_add_vma(task->mm,
                             task->mm->interp_segments[i].vaddr,
                             task->mm->interp_segments[i].image_size,
                             task->mm->interp_segments[i].flags,
                             TASK_VMA_INTERP,
                             task->mm->interp_segments[i].image,
                             task->mm->interp_segments[i].image_size) != 0) {
                return -1;
            }
        }
        strncpy(task->mm->interp_path, resolved_interp, sizeof(task->mm->interp_path) - 1);
        task->mm->interp_path[sizeof(task->mm->interp_path) - 1] = '\0';
        task->mm->entry_point = interp_plan.ehdr.e_entry;
    }

    if (exec_parse_dynamic_info(task,
                                task->mm->exec_dynamic_vaddr,
                                task->mm->exec_dynamic_size,
                                &task->mm->exec_dynamic) != 0) {
        return -1;
    }
    if (interp_image &&
        exec_parse_dynamic_info(task,
                                task->mm->interp_dynamic_vaddr,
                                task->mm->interp_dynamic_size,
                                &task->mm->interp_dynamic) != 0) {
        return -1;
    }

    if (exec_build_initial_elf_stack(task, &plan, interp_image ? &interp_plan : NULL) != 0) {
        return -1;
    }
    if (exec_add_vma(task->mm,
                     task->mm->initial_stack_base,
                     task->mm->initial_stack_image_size,
                     PF_R | PF_W,
                     TASK_VMA_STACK,
                     task->mm->initial_stack_image,
                     task->mm->initial_stack_image_size) != 0) {
        return -1;
    }
    task->mm->handoff.entry_point = task->mm->entry_point;
    task->mm->handoff.initial_stack_pointer = task->mm->initial_stack_pointer;
    task->mm->handoff.aarch64_pc = task->mm->entry_point;
    task->mm->handoff.aarch64_sp = task->mm->initial_stack_pointer;
    task->mm->handoff.read_memory = task_read_virtual_memory_impl;
    task->mm->handoff.write_memory = task_write_virtual_memory_impl;

    strncpy(task->exec_image->path, path, sizeof(task->exec_image->path) - 1);
    task->exec_image->path[sizeof(task->exec_image->path) - 1] = '\0';
    strncpy(task->exec_image->interpreter, plan.interpreter, sizeof(task->exec_image->interpreter) - 1);
    task->exec_image->interpreter[sizeof(task->exec_image->interpreter) - 1] = '\0';
    task->exec_image->type = EXEC_IMAGE_ELF;
    task->exec_image->u.elf.entry = plan.ehdr.e_entry;
    task->exec_image->u.elf.type = plan.ehdr.e_type;
    task->exec_image->u.elf.machine = plan.ehdr.e_machine;

    return 0;
}

int exec_wasi(struct task_struct *task, const char *path, int argc, char **argv, char **envp) {
    (void)task;
    (void)path;
    (void)argc;
    (void)argv;
    (void)envp;
    errno = ENOEXEC;
    return -1;
}

int exec_script(struct task_struct *task, const char *path, int argc, char **argv, char **envp) {
    if (!task || !path || !argv) {
        errno = EINVAL;
        return -1;
    }

    char interpreter_path[MAX_PATH];
    char optional_arg[MAX_PATH];
    char *script_argv[TASK_MAX_ARGS + 4];
    int script_argc = 0;

    if (exec_build_script_argv(path, argc, argv,
                               interpreter_path, sizeof(interpreter_path),
                               optional_arg, sizeof(optional_arg),
                               script_argv, &script_argc) < 0) {
        return -1;
    }

    if (task->exec_image) {
        strncpy(task->exec_image->interpreter, interpreter_path, sizeof(task->exec_image->interpreter) - 1);
        task->exec_image->interpreter[sizeof(task->exec_image->interpreter) - 1] = '\0';
    }

    native_entry_fn entry = native_lookup(interpreter_path);
    if (!entry) {
        errno = ENOENT;
        return -1;
    }

    return entry(script_argc, script_argv, envp);
}
