#include <uapi/asm/unistd.h>
#include <uapi/asm/siginfo.h>
#include <uapi/linux/resource.h>
#include <uapi/linux/fcntl.h>
#include <uapi/linux/elf.h>
#include <uapi/linux/auxvec.h>
#include <uapi/linux/capability.h>
#include <uapi/linux/mman.h>
#include <uapi/linux/mount.h>
#include <uapi/linux/prctl.h>
#include <uapi/linux/securebits.h>
#include <uapi/linux/signal.h>
#include <uapi/linux/stat.h>
#include <uapi/linux/xattr.h>
#include <uapi/linux/errno.h>
#include <linux/string.h>

#include <stdbool.h>
#include <stddef.h>

#include "fs/fdtable.h"
#include "private/fs/fdtable_state.h"
#include "fs/vfs.h"
#include "private/kernel/cred_state.h"
#include "kernel/cred.h"
#include "kernel/mm.h"
#include "kernel/signal.h"
#include "private/kernel/signal_state.h"
#include "kernel/task.h"
#include "private/kernel/task_state.h"
#include "private/runtime/aarch64/exec_context_api.h"
#include "private/runtime/aarch64/exec_context_state.h"
#include "private/runtime/aarch64/elf_reloc_state.h"
#include "runtime/native/registry.h"
#include "runtime/syscall.h"

extern int errno;

extern int execve(const char *pathname, char *const argv[], char *const envp[]);
extern int fexecve_impl(int fd, char *const argv[], char *const envp[]);
extern int open_impl(const char *pathname, int flags, uint32_t mode);
extern long write_impl(int fd, const void *buf, size_t count);
extern long read_impl(int fd, void *buf, size_t count);
extern ssize_t pread_impl(int fd, void *buf, size_t count, int64_t offset);
extern long readlink_impl(const char *pathname, char *buf, size_t bufsiz);
extern int unlink_impl(const char *pathname);
extern int symlinkat(const char *target, int newdirfd, const char *linkpath);
extern int chmod(const char *pathname, uint32_t mode);
extern int chown(const char *pathname, uint32_t owner, uint32_t group);
extern int mkdir_impl(const char *pathname, uint32_t mode);
extern int rmdir_impl(const char *pathname);
extern int mount(const char *source, const char *target, const char *filesystemtype,
                 unsigned long mountflags, const void *data);
extern int umount_impl(const char *target);
extern int ftruncate_impl(int fd, int64_t length);
extern int capget_impl(cap_user_header_t header, cap_user_data_t data);
extern bool signal_is_pending(const struct task *task, int32_t sig);

static bool task_execed(const struct task *task) {
    return task && atomic_read(&task->execed) != 0;
}

static void task_reset_execed(struct task *task) {
    if (task) {
        atomic_set(&task->execed, 0);
    }
}

static int close_if_open(int fd) {
    if (fd >= 0 && fdtable_is_used_impl(fd)) {
        return close_impl(fd);
    }
    return 0;
}

static int expect_errno(int expected) {
    if (errno != expected) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

static int read_proc_file(const char *path, char *buf, size_t buf_len, ssize_t *out_len) {
    int fd;
    ssize_t nread;

    if (!path || !buf || buf_len == 0 || !out_len) {
        errno = EINVAL;
        return -1;
    }

    fd = open_impl(path, O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }
    nread = read_impl(fd, buf, buf_len);
    close_impl(fd);
    if (nread < 0) {
        return -1;
    }
    *out_len = nread;
    return 0;
}

static int format_maps_range(char *buf, size_t buf_len, uint64_t start, uint64_t end) {
    static const char hex[] = "0123456789abcdef";
    size_t pos = 0;

    if (!buf || buf_len < 27) {
        errno = EINVAL;
        return -1;
    }
    for (int shift = 44; shift >= 0; shift -= 4) {
        buf[pos++] = hex[(start >> shift) & 0xfU];
    }
    buf[pos++] = '-';
    for (int shift = 44; shift >= 0; shift -= 4) {
        buf[pos++] = hex[(end >> shift) & 0xfU];
    }
    buf[pos] = '\0';
    return 0;
}

static int smaps_block_contains(const char *content, const char *range, const char *needle) {
    const char *block;
    const char *next;
    const char *found;

    if (!content || !range || !needle) {
        errno = EINVAL;
        return 0;
    }
    block = strstr(content, range);
    if (!block) {
        return 0;
    }
    next = strstr(block + 1, "\n000");
    if (!next) {
        next = block + strlen(block);
    }
    found = strstr(block, needle);
    return found && found < next;
}

static int write_file_exact(const char *path, const char *content) {
    int fd;
    size_t len;

    if (!path || !content) {
        errno = EINVAL;
        return -1;
    }
    fd = open_impl(path, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (fd < 0) {
        return -1;
    }
    len = strlen(content);
    if (write_impl(fd, content, len) != (long)len) {
        int saved_errno = errno;
        close_impl(fd);
        errno = saved_errno;
        return -1;
    }
    return close_impl(fd);
}

static int expect_nul_vector(const char *buf, ssize_t len, const char *const expected[]) {
    size_t pos = 0;

    if (!buf || len < 0 || !expected) {
        errno = EINVAL;
        return -1;
    }

    for (int i = 0; expected[i]; i++) {
        size_t item_len = strlen(expected[i]);
        if (pos + item_len + 1 > (size_t)len) {
            errno = EPROTO;
            return -1;
        }
        if (memcmp(buf + pos, expected[i], item_len) != 0 || buf[pos + item_len] != '\0') {
            errno = EPROTO;
            return -1;
        }
        pos += item_len + 1;
    }

    if (pos != (size_t)len) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

static int find_auxv_value(const struct task *task, uint64_t type, uint64_t *out_value) {
    if (!task || !task->mm || !out_value) {
        errno = EINVAL;
        return -1;
    }

    for (uint32_t i = 0; i < task->mm->auxv_count; i++) {
        if (task->mm->auxv[i].type == type) {
            *out_value = task->mm->auxv[i].value;
            return 0;
        }
    }

    errno = ENOENT;
    return -1;
}

static int expect_stack_addr(const struct task *task, uint64_t addr) {
    if (!task || !task->mm) {
        errno = EINVAL;
        return -1;
    }
    if (addr < task->mm->initial_stack_base ||
        addr >= task->mm->initial_stack_base + task->mm->initial_stack_size) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

static int stack_image_offset(const struct task *task, uint64_t addr, size_t size, size_t *out_offset) {
    if (!task || !task->mm || !task->mm->initial_stack_image || !out_offset) {
        errno = EINVAL;
        return -1;
    }
    if (addr < task->mm->initial_stack_base ||
        addr > task->mm->initial_stack_base + task->mm->initial_stack_image_size ||
        size > (task->mm->initial_stack_base + task->mm->initial_stack_image_size) - addr) {
        errno = EPROTO;
        return -1;
    }
    *out_offset = (size_t)(addr - task->mm->initial_stack_base);
    return 0;
}

static int stack_image_read_u64(const struct task *task, uint64_t *cursor, uint64_t *out_value) {
    size_t offset;

    if (!cursor || !out_value) {
        errno = EINVAL;
        return -1;
    }
    if (stack_image_offset(task, *cursor, sizeof(*out_value), &offset) != 0) {
        return -1;
    }
    memcpy(out_value, (const unsigned char *)task->mm->initial_stack_image + offset, sizeof(*out_value));
    *cursor += sizeof(*out_value);
    return 0;
}

static int expect_stack_string(const struct task *task, uint64_t addr, const char *expected) {
    size_t offset;
    size_t len;

    if (!expected) {
        errno = EINVAL;
        return -1;
    }
    len = strlen(expected) + 1;
    if (stack_image_offset(task, addr, len, &offset) != 0) {
        return -1;
    }
    if (memcmp((const unsigned char *)task->mm->initial_stack_image + offset, expected, len) != 0) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

static int expect_auxv_value(const struct task *task, uint64_t type, uint64_t expected) {
    uint64_t value = 0;

    if (find_auxv_value(task, type, &value) != 0) {
        return -1;
    }
    if (value != expected) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

static int expect_common_elf_initial_stack(const struct task *task,
                                           const Elf64_Ehdr *ehdr,
                                           int expected_argc,
                                           int expected_envc) {
    if (!task || !task->mm || !ehdr) {
        errno = EINVAL;
        return -1;
    }

    if (task->mm->initial_stack_base == 0 ||
        task->mm->initial_stack_size == 0 ||
        !task->mm->initial_stack_image ||
        task->mm->initial_stack_image_size != task->mm->initial_stack_size ||
        task->mm->initial_stack_pointer < task->mm->initial_stack_base ||
        task->mm->initial_stack_pointer >= task->mm->initial_stack_base + task->mm->initial_stack_size ||
        (task->mm->initial_stack_pointer & 15) != 0 ||
        task->mm->initial_argc != expected_argc ||
        task->mm->initial_envc != expected_envc ||
        task->mm->auxv_count == 0 ||
        task->mm->auxv[task->mm->auxv_count - 1].type != AT_NULL ||
        task->mm->auxv[task->mm->auxv_count - 1].value != 0) {
        errno = EPROTO;
        return -1;
    }

    if (expect_auxv_value(task, AT_PHENT, sizeof(Elf64_Phdr)) != 0 ||
        expect_auxv_value(task, AT_PHNUM, ehdr->e_phnum) != 0 ||
        expect_auxv_value(task, AT_PAGESZ, 4096) != 0 ||
        expect_auxv_value(task, AT_FLAGS, 0) != 0 ||
        expect_auxv_value(task, AT_ENTRY, ehdr->e_entry) != 0 ||
        expect_auxv_value(task, AT_RANDOM, task->mm->auxv_random_addr) != 0 ||
        expect_auxv_value(task, AT_PLATFORM, task->mm->auxv_platform_addr) != 0 ||
        expect_auxv_value(task, AT_EXECFN, task->mm->auxv_execfn_addr) != 0) {
        return -1;
    }

    if (expect_stack_addr(task, task->mm->auxv_random_addr) != 0 ||
        expect_stack_addr(task, task->mm->auxv_platform_addr) != 0 ||
        expect_stack_addr(task, task->mm->auxv_execfn_addr) != 0) {
        return -1;
    }

    for (int i = 0; i < expected_argc; i++) {
        if (expect_stack_addr(task, task->mm->initial_argv[i]) != 0) {
            return -1;
        }
    }
    for (int i = 0; i < expected_envc; i++) {
        if (expect_stack_addr(task, task->mm->initial_envp[i]) != 0) {
            return -1;
        }
    }

    return 0;
}

static int expect_materialized_stack_vector(const struct task *task,
                                            const char *const expected_argv[],
                                            const char *const expected_envp[]) {
    uint64_t cursor;
    uint64_t value;
    int argc = 0;
    int envc = 0;

    if (!task || !task->mm || !expected_argv || !expected_envp) {
        errno = EINVAL;
        return -1;
    }

    while (expected_argv[argc]) {
        argc++;
    }
    while (expected_envp[envc]) {
        envc++;
    }

    cursor = task->mm->initial_stack_pointer;
    if (stack_image_read_u64(task, &cursor, &value) != 0 || value != (uint64_t)argc) {
        errno = EPROTO;
        return -1;
    }

    for (int i = 0; i < argc; i++) {
        if (stack_image_read_u64(task, &cursor, &value) != 0 ||
            value != task->mm->initial_argv[i] ||
            expect_stack_string(task, value, expected_argv[i]) != 0) {
            errno = EPROTO;
            return -1;
        }
    }
    if (stack_image_read_u64(task, &cursor, &value) != 0 || value != 0) {
        errno = EPROTO;
        return -1;
    }

    for (int i = 0; i < envc; i++) {
        if (stack_image_read_u64(task, &cursor, &value) != 0 ||
            value != task->mm->initial_envp[i] ||
            expect_stack_string(task, value, expected_envp[i]) != 0) {
            errno = EPROTO;
            return -1;
        }
    }
    if (stack_image_read_u64(task, &cursor, &value) != 0 || value != 0) {
        errno = EPROTO;
        return -1;
    }

    for (uint32_t i = 0; i < task->mm->auxv_count; i++) {
        uint64_t type;

        if (stack_image_read_u64(task, &cursor, &type) != 0 ||
            stack_image_read_u64(task, &cursor, &value) != 0 ||
            type != task->mm->auxv[i].type ||
            value != task->mm->auxv[i].value) {
            errno = EPROTO;
            return -1;
        }
    }

    return 0;
}

static int expect_materialized_auxv_strings(const struct task *task) {
    static const unsigned char expected_random[16] = {
        0x49, 0x58, 0x4c, 0x41, 0x4e, 0x44, 0x5f, 0x41,
        0x55, 0x58, 0x56, 0x5f, 0x52, 0x4e, 0x44, 0x00,
    };
    size_t offset;

    if (expect_stack_string(task, task->mm->auxv_platform_addr, "aarch64") != 0 ||
        expect_stack_string(task, task->mm->auxv_execfn_addr, task->exe) != 0 ||
        stack_image_offset(task, task->mm->auxv_random_addr, sizeof(expected_random), &offset) != 0) {
        return -1;
    }
    if (memcmp((const unsigned char *)task->mm->initial_stack_image + offset,
               expected_random, sizeof(expected_random)) != 0) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

static int expect_segment_image(const void *segment_image,
                                size_t segment_image_size,
                                const unsigned char *elf_image,
                                const Elf64_Phdr *phdr) {
    const unsigned char *segment_bytes = segment_image;

    if (!segment_image && phdr->p_memsz > 0) {
        errno = EPROTO;
        return -1;
    }
    if (segment_image_size != phdr->p_memsz) {
        errno = EPROTO;
        return -1;
    }
    if (phdr->p_filesz > 0 &&
        memcmp(segment_bytes, elf_image + phdr->p_offset, phdr->p_filesz) != 0) {
        errno = EPROTO;
        return -1;
    }
    for (uint64_t i = phdr->p_filesz; i < phdr->p_memsz; i++) {
        if (segment_bytes[i] != 0) {
            errno = EPROTO;
            return -1;
        }
    }
    return 0;
}

static int expect_task_vm_bytes(struct task *task, uint64_t addr, const void *expected, size_t len) {
    unsigned char buf[128];
    long nread;

    if (!task || !expected || len > sizeof(buf)) {
        errno = EINVAL;
        return -1;
    }

    memset(buf, 0xa5, sizeof(buf));
    nread = task_read_virtual_memory_impl(task, addr, buf, len);
    if (nread != (long)len || memcmp(buf, expected, len) != 0) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

static int expect_task_vm_zeroes(struct task *task, uint64_t addr, size_t len) {
    unsigned char buf[32];
    long nread;

    if (!task || len > sizeof(buf)) {
        errno = EINVAL;
        return -1;
    }

    memset(buf, 0xa5, sizeof(buf));
    nread = task_read_virtual_memory_impl(task, addr, buf, len);
    if (nread != (long)len) {
        errno = EPROTO;
        return -1;
    }
    for (size_t i = 0; i < len; i++) {
        if (buf[i] != 0) {
            errno = EPROTO;
            return -1;
        }
    }
    return 0;
}

static int expect_task_vm_write(struct task *task, uint64_t addr, const void *bytes, size_t len) {
    long nwritten;

    if (!task || !bytes) {
        errno = EINVAL;
        return -1;
    }

    nwritten = task_write_virtual_memory_impl(task, addr, bytes, len);
    if (nwritten != (long)len) {
        errno = EPROTO;
        return -1;
    }
    return expect_task_vm_bytes(task, addr, bytes, len);
}

static uint64_t make_r_info(uint64_t symbol, uint64_t type) {
    return (symbol << 32) | (type & 0xffffffffULL);
}

static int expect_vma(const struct task *task, uint32_t index, uint64_t start, uint64_t size,
                      uint32_t flags, enum task_vma_kind kind, const void *image) {
    if (!task || !task->mm || index >= task->mm->vma_count || size == 0) {
        errno = EINVAL;
        return -1;
    }
    if (task->mm->vmas[index].start != start ||
        task->mm->vmas[index].end != start + size ||
        task->mm->vmas[index].flags != flags ||
        task->mm->vmas[index].kind != kind ||
        task->mm->vmas[index].image != image ||
        task->mm->vmas[index].image_size != size) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

static int native_exec_status(int argc, char **argv, char **envp) {
    (void)argc;
    (void)argv;
    (void)envp;
    return 23;
}

static int captured_argc;
static char captured_argv[8][MAX_PATH];
static char captured_env0[MAX_PATH];

static void clear_captured_exec(void) {
    captured_argc = 0;
    memset(captured_argv, 0, sizeof(captured_argv));
    memset(captured_env0, 0, sizeof(captured_env0));
}

static int native_capture_exec(int argc, char **argv, char **envp) {
    captured_argc = argc;
    for (int i = 0; i < argc && i < 8; i++) {
        if (argv && argv[i]) {
            size_t len = strlen(argv[i]);
            if (len >= sizeof(captured_argv[i])) {
                errno = ENAMETOOLONG;
                return -1;
            }
            memcpy(captured_argv[i], argv[i], len + 1);
        }
    }
    if (envp && envp[0]) {
        size_t len = strlen(envp[0]);
        if (len >= sizeof(captured_env0)) {
            errno = ENAMETOOLONG;
            return -1;
        }
        memcpy(captured_env0, envp[0], len + 1);
    }
    return 37;
}

static int create_exec_file(const char *path, const char *content) {
    int fd = open_impl(path, O_WRONLY | O_CREAT | O_TRUNC, 0700);
    if (fd < 0) {
        return -1;
    }
    if (content) {
        size_t len = strlen(content);
        if (write_impl(fd, content, len) != (long)len) {
            int saved_errno = errno;
            close_impl(fd);
            errno = saved_errno;
            return -1;
        }
    }
    return close_impl(fd);
}

static int create_exec_bytes(const char *path, const void *content, size_t len) {
    int fd = open_impl(path, O_WRONLY | O_CREAT | O_TRUNC, 0700);
    if (fd < 0) {
        return -1;
    }
    if (len > 0 && write_impl(fd, content, len) != (long)len) {
        int saved_errno = errno;
        close_impl(fd);
        errno = saved_errno;
        return -1;
    }
    return close_impl(fd);
}

static void build_minimal_elf64_aarch64(Elf64_Ehdr *ehdr) {
    memset(ehdr, 0, sizeof(*ehdr));
    ehdr->e_ident[EI_MAG0] = ELFMAG0;
    ehdr->e_ident[EI_MAG1] = ELFMAG1;
    ehdr->e_ident[EI_MAG2] = ELFMAG2;
    ehdr->e_ident[EI_MAG3] = ELFMAG3;
    ehdr->e_ident[EI_CLASS] = ELFCLASS64;
    ehdr->e_ident[EI_DATA] = ELFDATA2LSB;
    ehdr->e_ident[EI_VERSION] = EV_CURRENT;
    ehdr->e_type = ET_EXEC;
    ehdr->e_machine = EM_AARCH64;
    ehdr->e_version = EV_CURRENT;
    ehdr->e_entry = 0x400000;
    ehdr->e_ehsize = sizeof(*ehdr);
}

static void build_exec_elf64_with_interp(unsigned char *image, size_t image_len,
                                         const char *interp_path,
                                         uint64_t entry,
                                         uint64_t load_vaddr) {
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)image;
    Elf64_Phdr *interp;
    Elf64_Phdr *load;
    size_t interp_len = strlen(interp_path) + 1;
    size_t interp_offset = sizeof(Elf64_Ehdr) + (2 * sizeof(Elf64_Phdr));
    size_t text_offset = interp_offset + interp_len;

    memset(image, 0, image_len);
    build_minimal_elf64_aarch64(ehdr);
    ehdr->e_entry = entry;
    ehdr->e_phoff = sizeof(Elf64_Ehdr);
    ehdr->e_phentsize = sizeof(Elf64_Phdr);
    ehdr->e_phnum = 2;

    interp = (Elf64_Phdr *)(image + ehdr->e_phoff);
    interp->p_type = PT_INTERP;
    interp->p_offset = interp_offset;
    interp->p_filesz = interp_len;
    interp->p_memsz = interp_len;
    memcpy(image + interp_offset, interp_path, interp_len);

    load = interp + 1;
    load->p_type = PT_LOAD;
    load->p_flags = PF_R | PF_X;
    load->p_offset = text_offset;
    load->p_vaddr = load_vaddr;
    load->p_filesz = image_len - text_offset;
    load->p_memsz = load->p_filesz;
    load->p_align = 0x1000;
    image[text_offset] = 0xcc;
}

static void build_exec_elf64_without_interp(unsigned char *image, size_t image_len,
                                            uint64_t entry,
                                            uint64_t load_vaddr) {
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)image;
    Elf64_Phdr *load;
    size_t text_offset = sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr);

    memset(image, 0, image_len);
    build_minimal_elf64_aarch64(ehdr);
    ehdr->e_entry = entry;
    ehdr->e_phoff = sizeof(Elf64_Ehdr);
    ehdr->e_phentsize = sizeof(Elf64_Phdr);
    ehdr->e_phnum = 1;

    load = (Elf64_Phdr *)(image + ehdr->e_phoff);
    load->p_type = PT_LOAD;
    load->p_flags = PF_R | PF_X;
    load->p_offset = text_offset;
    load->p_vaddr = load_vaddr;
    load->p_filesz = image_len - text_offset;
    load->p_memsz = load->p_filesz;
    load->p_align = 0x1000;
    image[text_offset] = 0xdd;
}

static void build_exec_elf64_without_interp_with_bss(unsigned char *image, size_t image_len,
                                                     uint64_t entry,
                                                     uint64_t load_vaddr) {
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)image;
    Elf64_Phdr *load;
    size_t text_offset = sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr);

    build_exec_elf64_without_interp(image, image_len, entry, load_vaddr);
    load = (Elf64_Phdr *)(image + ehdr->e_phoff);
    load->p_filesz = image_len - text_offset - 16;
    load->p_memsz = load->p_filesz + 16;
    image[text_offset] = 0xee;
    image[text_offset + 1] = 0xff;
}

static void build_exec_elf64_writable_load(unsigned char *image, size_t image_len,
                                           uint64_t entry,
                                           uint64_t load_vaddr) {
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)image;
    Elf64_Phdr *load;

    build_exec_elf64_without_interp_with_bss(image, image_len, entry, load_vaddr);
    load = (Elf64_Phdr *)(image + ehdr->e_phoff);
    load->p_flags = PF_R | PF_W;
}

static void build_exec_elf64_with_dynamic(unsigned char *image, size_t image_len,
                                          uint64_t entry,
                                          uint64_t load_vaddr,
                                          uint64_t dynamic_vaddr) {
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)image;
    Elf64_Phdr *load;
    Elf64_Phdr *dynamic;
    size_t text_offset = sizeof(Elf64_Ehdr) + (2 * sizeof(Elf64_Phdr));

    memset(image, 0, image_len);
    build_minimal_elf64_aarch64(ehdr);
    ehdr->e_entry = entry;
    ehdr->e_phoff = sizeof(Elf64_Ehdr);
    ehdr->e_phentsize = sizeof(Elf64_Phdr);
    ehdr->e_phnum = 2;

    load = (Elf64_Phdr *)(image + ehdr->e_phoff);
    load->p_type = PT_LOAD;
    load->p_flags = PF_R | PF_W;
    load->p_offset = text_offset;
    load->p_vaddr = load_vaddr;
    load->p_filesz = image_len - text_offset;
    load->p_memsz = load->p_filesz;
    load->p_align = 0x1000;

    dynamic = load + 1;
    dynamic->p_type = PT_DYNAMIC;
    dynamic->p_offset = text_offset + (size_t)(dynamic_vaddr - load_vaddr);
    dynamic->p_vaddr = dynamic_vaddr;
    dynamic->p_filesz = 16;
    dynamic->p_memsz = 16;
    dynamic->p_align = 8;

    image[text_offset] = 0xda;
}

static void build_exec_elf64_with_interp_and_dynamic(unsigned char *image, size_t image_len,
                                                     const char *interp_path,
                                                     uint64_t entry,
                                                     uint64_t load_vaddr,
                                                     uint64_t dynamic_vaddr) {
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)image;
    Elf64_Phdr *interp;
    Elf64_Phdr *load;
    Elf64_Phdr *dynamic;
    size_t interp_len = strlen(interp_path) + 1;
    size_t interp_offset = sizeof(Elf64_Ehdr) + (3 * sizeof(Elf64_Phdr));
    size_t text_offset = interp_offset + interp_len;

    memset(image, 0, image_len);
    build_minimal_elf64_aarch64(ehdr);
    ehdr->e_entry = entry;
    ehdr->e_phoff = sizeof(Elf64_Ehdr);
    ehdr->e_phentsize = sizeof(Elf64_Phdr);
    ehdr->e_phnum = 3;

    interp = (Elf64_Phdr *)(image + ehdr->e_phoff);
    interp->p_type = PT_INTERP;
    interp->p_offset = interp_offset;
    interp->p_filesz = interp_len;
    interp->p_memsz = interp_len;
    memcpy(image + interp_offset, interp_path, interp_len);

    load = interp + 1;
    load->p_type = PT_LOAD;
    load->p_flags = PF_R | PF_W;
    load->p_offset = text_offset;
    load->p_vaddr = load_vaddr;
    load->p_filesz = image_len - text_offset;
    load->p_memsz = load->p_filesz;
    load->p_align = 0x1000;

    dynamic = load + 1;
    dynamic->p_type = PT_DYNAMIC;
    dynamic->p_offset = text_offset + (size_t)(dynamic_vaddr - load_vaddr);
    dynamic->p_vaddr = dynamic_vaddr;
    dynamic->p_filesz = 16;
    dynamic->p_memsz = 16;
    dynamic->p_align = 8;

    image[text_offset] = 0xad;
}

static void build_exec_elf64_with_dynamic_entries(unsigned char *image, size_t image_len,
                                                  uint64_t entry,
                                                  uint64_t load_vaddr,
                                                  uint64_t dynamic_vaddr,
                                                  const Elf64_Dyn *entries,
                                                  size_t entry_count) {
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)image;
    Elf64_Phdr *load;
    Elf64_Phdr *dynamic;
    size_t text_offset = sizeof(Elf64_Ehdr) + (2 * sizeof(Elf64_Phdr));
    size_t dynamic_size = entry_count * sizeof(Elf64_Dyn);

    memset(image, 0, image_len);
    build_minimal_elf64_aarch64(ehdr);
    ehdr->e_entry = entry;
    ehdr->e_phoff = sizeof(Elf64_Ehdr);
    ehdr->e_phentsize = sizeof(Elf64_Phdr);
    ehdr->e_phnum = 2;

    load = (Elf64_Phdr *)(image + ehdr->e_phoff);
    load->p_type = PT_LOAD;
    load->p_flags = PF_R | PF_W;
    load->p_offset = text_offset;
    load->p_vaddr = load_vaddr;
    load->p_filesz = image_len - text_offset;
    load->p_memsz = load->p_filesz;
    load->p_align = 0x1000;

    dynamic = load + 1;
    dynamic->p_type = PT_DYNAMIC;
    dynamic->p_offset = text_offset + (size_t)(dynamic_vaddr - load_vaddr);
    dynamic->p_vaddr = dynamic_vaddr;
    dynamic->p_filesz = dynamic_size;
    dynamic->p_memsz = dynamic_size;
    dynamic->p_align = 8;

    image[text_offset] = 0xda;
    if (entries && dynamic->p_offset + dynamic_size <= image_len) {
        memcpy(image + dynamic->p_offset, entries, dynamic_size);
    }
}

static void build_exec_elf64_with_interp_and_dynamic_entries(unsigned char *image, size_t image_len,
                                                             const char *interp_path,
                                                             uint64_t entry,
                                                             uint64_t load_vaddr,
                                                             uint64_t dynamic_vaddr,
                                                             const Elf64_Dyn *entries,
                                                             size_t entry_count) {
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)image;
    Elf64_Phdr *interp;
    Elf64_Phdr *load;
    Elf64_Phdr *dynamic;
    size_t interp_len = strlen(interp_path) + 1;
    size_t interp_offset = sizeof(Elf64_Ehdr) + (3 * sizeof(Elf64_Phdr));
    size_t text_offset = interp_offset + interp_len;
    size_t dynamic_size = entry_count * sizeof(Elf64_Dyn);

    memset(image, 0, image_len);
    build_minimal_elf64_aarch64(ehdr);
    ehdr->e_entry = entry;
    ehdr->e_phoff = sizeof(Elf64_Ehdr);
    ehdr->e_phentsize = sizeof(Elf64_Phdr);
    ehdr->e_phnum = 3;

    interp = (Elf64_Phdr *)(image + ehdr->e_phoff);
    interp->p_type = PT_INTERP;
    interp->p_offset = interp_offset;
    interp->p_filesz = interp_len;
    interp->p_memsz = interp_len;
    memcpy(image + interp_offset, interp_path, interp_len);

    load = interp + 1;
    load->p_type = PT_LOAD;
    load->p_flags = PF_R | PF_W;
    load->p_offset = text_offset;
    load->p_vaddr = load_vaddr;
    load->p_filesz = image_len - text_offset;
    load->p_memsz = load->p_filesz;
    load->p_align = 0x1000;

    dynamic = load + 1;
    dynamic->p_type = PT_DYNAMIC;
    dynamic->p_offset = text_offset + (size_t)(dynamic_vaddr - load_vaddr);
    dynamic->p_vaddr = dynamic_vaddr;
    dynamic->p_filesz = dynamic_size;
    dynamic->p_memsz = dynamic_size;
    dynamic->p_align = 8;

    image[text_offset] = 0xad;
    if (entries && dynamic->p_offset + dynamic_size <= image_len) {
        memcpy(image + dynamic->p_offset, entries, dynamic_size);
    }
}

static int verify_state_unchanged(struct task *task,
                                  const char *expected_exe,
                                  const char *expected_comm,
                                  bool expected_execed,
                                  int cloexec_fd,
                                  int keep_fd) {
    if (strcmp(task->exe, expected_exe) != 0) {
        errno = EPROTO;
        return -1;
    }
    if (strcmp(task->comm, expected_comm) != 0) {
        errno = EPROTO;
        return -1;
    }
    if (task_execed(task) != expected_execed) {
        errno = EPROTO;
        return -1;
    }
    if ((cloexec_fd >= 0 && !fdtable_is_used_impl(cloexec_fd)) ||
        (keep_fd >= 0 && !fdtable_is_used_impl(keep_fd))) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int exec_syscall_contract_rejects_null_path_without_transition(void) {
    struct task *task = task_current();

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    memcpy(task->exe, "/before", 8);
    memset(task->comm, 0, sizeof(task->comm));
    memcpy(task->comm, "before", 7);
    task_reset_execed(task);

    errno = 0;
    if (execve(NULL, NULL, NULL) != -1) {
        errno = EPROTO;
        return -1;
    }
    if (expect_errno(EFAULT) != 0) {
        return -1;
    }
    return verify_state_unchanged(task, "/before", "before", false, -1, -1);
}

int exec_syscall_contract_rejects_empty_path_without_transition(void) {
    struct task *task = task_current();

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    memcpy(task->exe, "/before", 8);
    memset(task->comm, 0, sizeof(task->comm));
    memcpy(task->comm, "before", 7);
    task_reset_execed(task);

    errno = 0;
    if (execve("", NULL, NULL) != -1) {
        errno = EPROTO;
        return -1;
    }
    if (expect_errno(ENOENT) != 0) {
        return -1;
    }
    return verify_state_unchanged(task, "/before", "before", false, -1, -1);
}

int exec_syscall_contract_missing_path_preserves_state_and_cloexec_fds(void) {
    struct task *task = task_current();
    int cloexec_fd = -1;
    int keep_fd = -1;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    memcpy(task->exe, "/before", 8);
    memset(task->comm, 0, sizeof(task->comm));
    memcpy(task->comm, "before", 7);
    task_reset_execed(task);

    cloexec_fd = open_impl("/dev/null", O_RDONLY | O_CLOEXEC, 0);
    if (cloexec_fd < 0) {
        return -1;
    }

    keep_fd = open_impl("/dev/zero", O_RDONLY, 0);
    if (keep_fd < 0) {
        goto out;
    }

    errno = 0;
    if (execve("/missing", NULL, NULL) != -1) {
        errno = EPROTO;
        goto out;
    }
    if (expect_errno(ENOENT) != 0) {
        goto out;
    }
    if (verify_state_unchanged(task, "/before", "before", false, cloexec_fd, keep_fd) != 0) {
        goto out;
    }

    result = 0;

out:
    close_if_open(keep_fd);
    close_if_open(cloexec_fd);
    return result;
}

int exec_syscall_contract_native_success_applies_transition_and_returns_entry_status(void) {
    struct task *task = task_current();
    char *argv[] = {"custom-shell", "arg1", NULL};
    char *envp[] = {"A=B", NULL};
    int cloexec_fd = -1;
    int keep_fd = -1;
    char link_target[MAX_PATH];
    long link_len;
    int status;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    native_registry_clear();
    if (native_register("//usr//bin///env/", native_exec_status) != 0) {
        return -1;
    }

    memcpy(task->exe, "/before", 8);
    memset(task->comm, 0, sizeof(task->comm));
    memcpy(task->comm, "before", 7);
    task_reset_execed(task);

    cloexec_fd = open_impl("/dev/null", O_RDONLY | O_CLOEXEC, 0);
    if (cloexec_fd < 0) {
        goto out;
    }

    keep_fd = open_impl("/dev/zero", O_RDONLY, 0);
    if (keep_fd < 0) {
        goto out;
    }

    status = execve("//usr//bin///env/", argv, envp);
    if (status != 23) {
        errno = EPROTO;
        goto out;
    }
    if (!task_execed(task)) {
        errno = EPROTO;
        goto out;
    }
    if (strcmp(task->exe, "/usr/bin/env") != 0) {
        errno = EPROTO;
        goto out;
    }
    if (strcmp(task->comm, "custom-shell") != 0) {
        errno = EPROTO;
        goto out;
    }
    if (fdtable_is_used_impl(cloexec_fd) || !fdtable_is_used_impl(keep_fd)) {
        errno = EPROTO;
        goto out;
    }
    if (!task->exec_image) {
        errno = EPROTO;
        goto out;
    }
    if (strcmp(task->exec_image->path, "/usr/bin/env") != 0) {
        errno = EPROTO;
        goto out;
    }

    link_len = readlink_impl("/proc/self/exe", link_target, sizeof(link_target));
    if (link_len < 0) {
        goto out;
    }
    if ((size_t)link_len >= sizeof(link_target)) {
        errno = EPROTO;
        goto out;
    }
    link_target[link_len] = '\0';
    if (strcmp(link_target, "/usr/bin/env") != 0) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    native_registry_clear();
    close_if_open(keep_fd);
    close_if_open(cloexec_fd);
    return result;
}

int exec_syscall_contract_native_exec_records_proc_cmdline_and_environ(void) {
    char *argv[] = {"custom-shell", "arg1", "arg two", NULL};
    char *envp[] = {"A=B", "C=D", NULL};
    const char *const expected_cmdline[] = {"custom-shell", "arg1", "arg two", NULL};
    const char *const expected_environ[] = {"A=B", "C=D", NULL};
    char buf[512];
    ssize_t nread = 0;
    int status;
    int result = -1;

    native_registry_clear();
    if (native_register("/usr/bin/proc-env", native_exec_status) != 0) {
        return -1;
    }

    status = execve("/usr/bin/proc-env", argv, envp);
    if (status != 23) {
        errno = EPROTO;
        goto out;
    }

    if (read_proc_file("/proc/self/cmdline", buf, sizeof(buf), &nread) != 0) {
        goto out;
    }
    if (expect_nul_vector(buf, nread, expected_cmdline) != 0) {
        errno = ENOMSG;
        goto out;
    }

    if (read_proc_file("/proc/self/environ", buf, sizeof(buf), &nread) != 0) {
        goto out;
    }
    if (expect_nul_vector(buf, nread, expected_environ) != 0) {
        errno = ENODATA;
        goto out;
    }

    result = 0;

out:
    native_registry_clear();
    return result;
}

static int expect_current_ids(uint32_t uid, uint32_t euid, uint32_t suid,
                              uint32_t gid, uint32_t egid, uint32_t sgid) {
    struct cred *cred = cred_current();

    if (!cred) {
        errno = ESRCH;
        return -1;
    }
    if (cred->uid != uid || cred->euid != euid || cred->suid != suid ||
        cred->fsuid != euid ||
        cred->gid != gid || cred->egid != egid || cred->sgid != sgid ||
        cred->fsgid != egid) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int exec_syscall_contract_native_execve_applies_setuid_setgid_saved_ids(void) {
    char *argv[] = {"cred-native", NULL};
    char *envp[] = {NULL};
    const char *path = "/tmp/exec-native-cred-file";
    int status;
    int result = -1;

    cred_reset_to_defaults();
    native_registry_clear();
    unlink_impl(path);
    if (native_register(path, native_exec_status) != 0 ||
        write_file_exact(path, "native") != 0 ||
        chown(path, 2000, 3000) != 0 ||
        chmod(path, S_ISUID | S_ISGID | 0755) != 0 ||
        setgid_impl(1000) != 0 ||
        setuid_impl(1000) != 0) {
        goto out;
    }

    status = execve(path, argv, envp);
    if (status != 23) {
        errno = EPROTO;
        goto out;
    }
    if (expect_current_ids(1000, 2000, 2000, 1000, 3000, 3000) != 0) {
        goto out;
    }

    result = 0;

out:
    native_registry_clear();
    unlink_impl(path);
    cred_reset_to_defaults();
    return result;
}

int exec_syscall_contract_native_execve_setid_marks_secure_and_not_dumpable(void) {
    char *argv[] = {"secure-setid-native", NULL};
    char *envp[] = {NULL};
    const char *path = "/tmp/exec-native-secure-setid";
    int status;
    int result = -1;
    struct task *task = task_current();

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    cred_reset_to_defaults();
    native_registry_clear();
    unlink_impl(path);
    if (native_register(path, native_exec_status) != 0 ||
        write_file_exact(path, "native") != 0 ||
        chown(path, 2000, 3000) != 0 ||
        chmod(path, S_ISUID | S_ISGID | 0755) != 0 ||
        setgid_impl(1000) != 0 ||
        setuid_impl(1000) != 0) {
        goto out;
    }

    status = execve(path, argv, envp);
    if (status != 23) {
        errno = EPROTO;
        goto out;
    }
    if (task->exec_secure != 1 || task->exec_dumpable != 0 ||
        expect_current_ids(1000, 2000, 2000, 1000, 3000, 3000) != 0) {
        errno = ENODATA;
        goto out;
    }

    result = 0;

out:
    native_registry_clear();
    unlink_impl(path);
    cred_reset_to_defaults();
    task->exec_secure = 0;
    task->exec_dumpable = 1;
    return result;
}

int exec_syscall_contract_native_execve_no_new_privs_blocks_setid_saved_ids(void) {
    char *argv[] = {"cred-nnp-native", NULL};
    char *envp[] = {NULL};
    const char *path = "/tmp/exec-native-nnp-cred-file";
    int status;
    int result = -1;

    cred_reset_to_defaults();
    native_registry_clear();
    unlink_impl(path);
    if (native_register(path, native_exec_status) != 0 ||
        write_file_exact(path, "native") != 0 ||
        chown(path, 2000, 3000) != 0 ||
        chmod(path, S_ISUID | S_ISGID | 0755) != 0 ||
        prctl_impl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0 ||
        setgid_impl(1000) != 0 ||
        setuid_impl(1000) != 0) {
        goto out;
    }

    status = execve(path, argv, envp);
    if (status != 23) {
        errno = EPROTO;
        goto out;
    }
    if (expect_current_ids(1000, 1000, 1000, 1000, 1000, 1000) != 0 ||
        prctl_impl(PR_GET_NO_NEW_PRIVS, 0, 0, 0, 0) != 1) {
        goto out;
    }

    result = 0;

out:
    native_registry_clear();
    unlink_impl(path);
    cred_reset_to_defaults();
    return result;
}

int exec_syscall_contract_native_execve_applies_file_capability_metadata(void) {
    char *argv[] = {"cap-native", NULL};
    char *envp[] = {NULL};
    const char *path = "/tmp/exec-native-file-cap";
    struct __user_cap_header_struct header = {
        .version = _LINUX_CAPABILITY_VERSION_3,
        .pid = 0,
    };
    struct __user_cap_data_struct data[_LINUX_CAPABILITY_U32S_3];
    uint64_t cap_mask = 1ULL << CAP_NET_BIND_SERVICE;
    int status;
    int result = -1;

    cred_reset_to_defaults();
    native_registry_clear();
    unlink_impl(path);
    if (native_register(path, native_exec_status) != 0 ||
        write_file_exact(path, "native") != 0 ||
        chmod(path, 0755) != 0 ||
        vfs_set_file_capabilities(path, cap_mask, 0, true) != 0 ||
        setgid_impl(1000) != 0 ||
        setuid_impl(1000) != 0) {
        goto out;
    }

    status = execve(path, argv, envp);
    if (status != 23) {
        errno = EPROTO;
        goto out;
    }
    memset(data, 0, sizeof(data));
    if (capget_impl(&header, data) != 0) {
        goto out;
    }
    if ((data[0].permitted & (1U << CAP_NET_BIND_SERVICE)) == 0 ||
        (data[0].effective & (1U << CAP_NET_BIND_SERVICE)) == 0 ||
        cred_current()->euid != 1000 ||
        cred_current()->suid != 1000) {
        errno = ENODATA;
        goto out;
    }

    result = 0;

out:
    native_registry_clear();
    unlink_impl(path);
    cred_reset_to_defaults();
    return result;
}

int exec_syscall_contract_native_execve_file_caps_mark_secure_and_clear_ambient(void) {
    char *argv[] = {"secure-cap-native", NULL};
    char *envp[] = {NULL};
    const char *path = "/tmp/exec-native-secure-file-cap";
    uint64_t cap_mask = 1ULL << CAP_NET_BIND_SERVICE;
    int status;
    int result = -1;
    struct task *task = task_current();

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    cred_reset_to_defaults();
    native_registry_clear();
    unlink_impl(path);
    if (native_register(path, native_exec_status) != 0 ||
        write_file_exact(path, "native") != 0 ||
        chmod(path, 0755) != 0 ||
        vfs_set_file_capabilities(path, cap_mask, 0, true) != 0 ||
        setgid_impl(1000) != 0 ||
        setuid_impl(1000) != 0) {
        goto out;
    }
    cred_current()->cap_ambient = cap_mask;

    status = execve(path, argv, envp);
    if (status != 23) {
        errno = EPROTO;
        goto out;
    }
    if (task->exec_secure != 1 ||
        task->exec_dumpable != 0 ||
        cred_current()->cap_ambient != 0 ||
        (cred_current()->cap_permitted & cap_mask) == 0 ||
        (cred_current()->cap_effective & cap_mask) == 0) {
        errno = ENODATA;
        goto out;
    }

    result = 0;

out:
    native_registry_clear();
    unlink_impl(path);
    cred_reset_to_defaults();
    task->exec_secure = 0;
    task->exec_dumpable = 1;
    return result;
}

int exec_syscall_contract_native_execve_honors_capability_bounding_drop(void) {
    char *argv[] = {"cap-bset-native", NULL};
    char *envp[] = {NULL};
    const char *path = "/tmp/exec-native-cap-bset";
    struct __user_cap_header_struct header = {
        .version = _LINUX_CAPABILITY_VERSION_3,
        .pid = 0,
    };
    struct __user_cap_data_struct data[_LINUX_CAPABILITY_U32S_3];
    uint64_t cap_mask = 1ULL << CAP_NET_BIND_SERVICE;
    int status;
    int result = -1;

    cred_reset_to_defaults();
    native_registry_clear();
    unlink_impl(path);
    if (native_register(path, native_exec_status) != 0 ||
        write_file_exact(path, "native") != 0 ||
        chmod(path, 0755) != 0 ||
        vfs_set_file_capabilities(path, cap_mask, 0, true) != 0) {
        goto out;
    }
    if (prctl_impl(PR_CAPBSET_READ, CAP_NET_BIND_SERVICE, 0, 0, 0) != 1) {
        errno = EPROTO;
        goto out;
    }
    if (prctl_impl(PR_CAPBSET_DROP, CAP_NET_BIND_SERVICE, 0, 0, 0) != 0 ||
        prctl_impl(PR_CAPBSET_READ, CAP_NET_BIND_SERVICE, 0, 0, 0) != 0 ||
        setgid_impl(1000) != 0 ||
        setuid_impl(1000) != 0) {
        goto out;
    }

    status = execve(path, argv, envp);
    if (status != 23) {
        errno = EPROTO;
        goto out;
    }
    memset(data, 0, sizeof(data));
    if (capget_impl(&header, data) != 0) {
        goto out;
    }
    if ((data[0].permitted & (1U << CAP_NET_BIND_SERVICE)) != 0 ||
        (data[0].effective & (1U << CAP_NET_BIND_SERVICE)) != 0) {
        errno = ENODATA;
        goto out;
    }

    result = 0;

out:
    native_registry_clear();
    unlink_impl(path);
    cred_reset_to_defaults();
    return result;
}

int exec_syscall_contract_native_execve_no_new_privs_blocks_file_capabilities(void) {
    char *argv[] = {"cap-nnp-native", NULL};
    char *envp[] = {NULL};
    const char *path = "/tmp/exec-native-nnp-file-cap";
    struct __user_cap_header_struct header = {
        .version = _LINUX_CAPABILITY_VERSION_3,
        .pid = 0,
    };
    struct __user_cap_data_struct data[_LINUX_CAPABILITY_U32S_3];
    uint64_t cap_mask = 1ULL << CAP_NET_BIND_SERVICE;
    int status;
    int result = -1;

    cred_reset_to_defaults();
    native_registry_clear();
    unlink_impl(path);
    if (native_register(path, native_exec_status) != 0 ||
        write_file_exact(path, "native") != 0 ||
        chmod(path, 0755) != 0 ||
        vfs_set_file_capabilities(path, cap_mask, 0, true) != 0 ||
        prctl_impl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0 ||
        setgid_impl(1000) != 0 ||
        setuid_impl(1000) != 0) {
        goto out;
    }

    status = execve(path, argv, envp);
    if (status != 23) {
        errno = EPROTO;
        goto out;
    }
    memset(data, 0, sizeof(data));
    if (capget_impl(&header, data) != 0) {
        goto out;
    }
    if ((data[0].permitted & (1U << CAP_NET_BIND_SERVICE)) != 0 ||
        (data[0].effective & (1U << CAP_NET_BIND_SERVICE)) != 0 ||
        prctl_impl(PR_GET_NO_NEW_PRIVS, 0, 0, 0, 0) != 1) {
        errno = ENODATA;
        goto out;
    }

    result = 0;

out:
    native_registry_clear();
    unlink_impl(path);
    cred_reset_to_defaults();
    return result;
}

int exec_syscall_contract_native_execve_no_new_privs_clears_ambient_on_file_caps(void) {
    char *argv[] = {"cap-nnp-ambient-native", NULL};
    char *envp[] = {NULL};
    const char *path = "/tmp/exec-native-nnp-ambient-file-cap";
    uint64_t cap_mask = 1ULL << CAP_NET_BIND_SERVICE;
    int status;
    int result = -1;

    cred_reset_to_defaults();
    native_registry_clear();
    unlink_impl(path);
    if (native_register(path, native_exec_status) != 0 ||
        write_file_exact(path, "native") != 0 ||
        chmod(path, 0755) != 0 ||
        vfs_set_file_capabilities(path, cap_mask, 0, true) != 0 ||
        prctl_impl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        goto out;
    }
    cred_current()->cap_ambient = cap_mask;

    status = execve(path, argv, envp);
    if (status != 23) {
        errno = EPROTO;
        goto out;
    }
    if (cred_current()->cap_ambient != 0 ||
        prctl_impl(PR_GET_NO_NEW_PRIVS, 0, 0, 0, 0) != 1) {
        errno = ENODATA;
        goto out;
    }

    result = 0;

out:
    native_registry_clear();
    unlink_impl(path);
    cred_reset_to_defaults();
    return result;
}

int exec_syscall_contract_native_execve_secure_noroot_blocks_root_cap_gain(void) {
    char *argv[] = {"secure-noroot-native", NULL};
    char *envp[] = {NULL};
    const char *path = "/tmp/exec-native-secure-noroot";
    struct __user_cap_header_struct header = {
        .version = _LINUX_CAPABILITY_VERSION_3,
        .pid = 0,
    };
    struct __user_cap_data_struct data[_LINUX_CAPABILITY_U32S_3];
    int status;
    int result = -1;

    cred_reset_to_defaults();
    native_registry_clear();
    unlink_impl(path);
    if (native_register(path, native_exec_status) != 0 ||
        write_file_exact(path, "native") != 0 ||
        chown(path, 0, 0) != 0 ||
        chmod(path, S_ISUID | 0755) != 0 ||
        prctl_impl(PR_SET_SECUREBITS, SECBIT_NOROOT, 0, 0, 0) != 0 ||
        setgid_impl(1000) != 0 ||
        setuid_impl(1000) != 0) {
        goto out;
    }

    status = execve(path, argv, envp);
    if (status != 23) {
        errno = EPROTO;
        goto out;
    }
    memset(data, 0, sizeof(data));
    if (capget_impl(&header, data) != 0) {
        goto out;
    }
    if (cred_current()->euid != 0 ||
        data[0].permitted != 0 || data[0].effective != 0 ||
        data[1].permitted != 0 || data[1].effective != 0) {
        errno = ENODATA;
        goto out;
    }

    result = 0;

out:
    native_registry_clear();
    unlink_impl(path);
    cred_reset_to_defaults();
    return result;
}

int exec_syscall_contract_native_execve_applies_security_capability_xattr(void) {
    char *argv[] = {"cap-xattr-native", NULL};
    char *envp[] = {NULL};
    const char *path = "/tmp/exec-native-file-cap-xattr";
    const char name[] = "security.capability";
    struct vfs_ns_cap_data cap = {0};
    struct __user_cap_header_struct header = {
        .version = _LINUX_CAPABILITY_VERSION_3,
        .pid = 0,
    };
    struct __user_cap_data_struct data[_LINUX_CAPABILITY_U32S_3];
    int status;
    int result = -1;

    cred_reset_to_defaults();
    native_registry_clear();
    unlink_impl(path);
    cap.magic_etc = VFS_CAP_REVISION_3 | VFS_CAP_FLAGS_EFFECTIVE;
    cap.data[0].permitted = 1U << CAP_NET_BIND_SERVICE;
    cap.rootid = 0;
    if (native_register(path, native_exec_status) != 0 ||
        write_file_exact(path, "native") != 0 ||
        chmod(path, 0755) != 0 ||
        syscall_dispatch_impl(__NR_setxattr, (long)(uintptr_t)path, (long)(uintptr_t)name,
                              (long)(uintptr_t)&cap, sizeof(cap), XATTR_CREATE, 0) != 0 ||
        setgid_impl(1000) != 0 ||
        setuid_impl(1000) != 0) {
        goto out;
    }

    status = execve(path, argv, envp);
    if (status != 23) {
        errno = EPROTO;
        goto out;
    }
    memset(data, 0, sizeof(data));
    if (capget_impl(&header, data) != 0) {
        goto out;
    }
    if ((data[0].permitted & (1U << CAP_NET_BIND_SERVICE)) == 0 ||
        (data[0].effective & (1U << CAP_NET_BIND_SERVICE)) == 0) {
        errno = ENODATA;
        goto out;
    }

    result = 0;

out:
    native_registry_clear();
    unlink_impl(path);
    cred_reset_to_defaults();
    return result;
}

int exec_syscall_contract_native_execve_nosuid_mount_blocks_setid_and_file_caps(void) {
    char *argv[] = {"nosuid-native", NULL};
    char *envp[] = {NULL};
    const char *source_dir = "/tmp/exec-nosuid-source";
    const char *target_dir = "/tmp/exec-nosuid-target";
    const char *source_path = "/tmp/exec-nosuid-source/bin";
    const char *target_path = "/tmp/exec-nosuid-target/bin";
    struct __user_cap_header_struct header = {
        .version = _LINUX_CAPABILITY_VERSION_3,
        .pid = 0,
    };
    struct __user_cap_data_struct data[_LINUX_CAPABILITY_U32S_3];
    uint64_t cap_mask = 1ULL << CAP_NET_BIND_SERVICE;
    int mounted = 0;
    int status;
    int result = -1;

    cred_reset_to_defaults();
    native_registry_clear();
    umount_impl(target_dir);
    unlink_impl(source_path);
    rmdir_impl(source_dir);
    rmdir_impl(target_dir);

    if ((mkdir_impl(source_dir, 0700) != 0 && errno != EEXIST) ||
        (mkdir_impl(target_dir, 0700) != 0 && errno != EEXIST) ||
        native_register(target_path, native_exec_status) != 0 ||
        write_file_exact(source_path, "native") != 0 ||
        chown(source_path, 2000, 3000) != 0 ||
        chmod(source_path, S_ISUID | S_ISGID | 0755) != 0 ||
        vfs_set_file_capabilities(source_path, cap_mask, 0, true) != 0 ||
        mount(source_dir, target_dir, NULL, MS_BIND | MS_NOSUID, NULL) != 0) {
        goto out;
    }
    mounted = 1;
    if (setgid_impl(1000) != 0 || setuid_impl(1000) != 0) {
        goto out;
    }

    status = execve(target_path, argv, envp);
    if (status != 23) {
        errno = EPROTO;
        goto out;
    }
    memset(data, 0, sizeof(data));
    if (capget_impl(&header, data) != 0) {
        goto out;
    }
    if (expect_current_ids(1000, 1000, 1000, 1000, 1000, 1000) != 0 ||
        (data[0].permitted & (1U << CAP_NET_BIND_SERVICE)) != 0 ||
        (data[0].effective & (1U << CAP_NET_BIND_SERVICE)) != 0) {
        errno = ENODATA;
        goto out;
    }

    result = 0;

out:
    {
        int saved_errno = errno;
        native_registry_clear();
        cred_reset_to_defaults();
        if (mounted) {
            umount_impl(target_dir);
        }
        unlink_impl(source_path);
        rmdir_impl(source_dir);
        rmdir_impl(target_dir);
        errno = saved_errno;
    }
    return result;
}

int exec_syscall_contract_oversized_argv_returns_e2big_without_transition(void) {
    struct task *task = task_current();
    char *too_many[TASK_MAX_ARGS + 1];
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    native_registry_clear();
    if (native_register("/usr/bin/too-many-args", native_exec_status) != 0) {
        return -1;
    }
    for (int i = 0; i < TASK_MAX_ARGS; i++) {
        too_many[i] = "x";
    }
    too_many[TASK_MAX_ARGS] = NULL;

    memcpy(task->exe, "/before", 8);
    memset(task->comm, 0, sizeof(task->comm));
    memcpy(task->comm, "before", 7);
    task_reset_execed(task);

    errno = 0;
    if (execve("/usr/bin/too-many-args", too_many, NULL) != -1 ||
        expect_errno(E2BIG) != 0 ||
        verify_state_unchanged(task, "/before", "before", false, -1, -1) != 0) {
        goto out;
    }

    result = 0;

out:
    native_registry_clear();
    return result;
}

int exec_syscall_contract_script_uses_virtual_path_and_native_interpreter(void) {
    struct task *task = task_current();
    char *argv[] = {"script-name", "arg1", NULL};
    char *envp[] = {"KEY=VALUE", NULL};
    int status;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    memcpy(task->exe, "/before", 8);
    memset(task->comm, 0, sizeof(task->comm));
    memcpy(task->comm, "before", 7);
    task_reset_execed(task);

    native_registry_clear();
    clear_captured_exec();
    unlink_impl("/tmp/exec-script-launch");

    if (create_exec_file("/tmp/exec-script-launch", "#!//usr//bin///interp/\n") != 0) {
        goto out;
    }
    if (native_register("/usr/bin/interp", native_capture_exec) != 0) {
        goto out;
    }

    status = execve("//tmp///exec-script-launch", argv, envp);
    if (status != 37) {
        errno = EPROTO;
        goto out;
    }
    if (!task_execed(task) ||
        strcmp(task->exe, "/tmp/exec-script-launch") != 0 ||
        strcmp(task->comm, "script-name") != 0) {
        errno = EPROTO;
        goto out;
    }
    if (!task->exec_image ||
        strcmp(task->exec_image->path, "/tmp/exec-script-launch") != 0 ||
        strcmp(task->exec_image->interpreter, "/usr/bin/interp") != 0 ||
        task->exec_image->type != EXEC_IMAGE_SCRIPT) {
        errno = EPROTO;
        goto out;
    }
    if (captured_argc != 3 ||
        strcmp(captured_argv[0], "//usr//bin///interp/") != 0 ||
        strcmp(captured_argv[1], "/tmp/exec-script-launch") != 0 ||
        strcmp(captured_argv[2], "arg1") != 0 ||
        strcmp(captured_env0, "KEY=VALUE") != 0) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    native_registry_clear();
    unlink_impl("/tmp/exec-script-launch");
    return result;
}

int exec_syscall_contract_script_exec_records_interpreter_proc_cmdline(void) {
    char *argv[] = {"script-name", "arg1", NULL};
    char *envp[] = {"KEY=VALUE", NULL};
    const char *const expected_cmdline[] = {"/usr/bin/interp", "-x", "/tmp/exec-script-proc", "arg1", NULL};
    const char *const expected_environ[] = {"KEY=VALUE", NULL};
    char buf[512];
    ssize_t nread = 0;
    int status;
    int result = -1;

    native_registry_clear();
    unlink_impl("/tmp/exec-script-proc");

    if (create_exec_file("/tmp/exec-script-proc", "#!/usr/bin/interp -x\n") != 0) {
        goto out;
    }
    if (native_register("/usr/bin/interp", native_capture_exec) != 0) {
        goto out;
    }

    status = execve("/tmp/exec-script-proc", argv, envp);
    if (status != 37) {
        errno = EPROTO;
        goto out;
    }

    if (read_proc_file("/proc/self/cmdline", buf, sizeof(buf), &nread) != 0) {
        goto out;
    }
    if (expect_nul_vector(buf, nread, expected_cmdline) != 0) {
        errno = ENOMSG;
        goto out;
    }

    if (read_proc_file("/proc/self/environ", buf, sizeof(buf), &nread) != 0) {
        goto out;
    }
    if (expect_nul_vector(buf, nread, expected_environ) != 0) {
        errno = ENODATA;
        goto out;
    }

    result = 0;

out:
    native_registry_clear();
    unlink_impl("/tmp/exec-script-proc");
    return result;
}

int exec_syscall_contract_script_symlink_records_resolved_target(void) {
    struct task *task = task_current();
    char *argv[] = {"script-link", "arg1", NULL};
    char *envp[] = {"LINK=1", NULL};
    int status;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    native_registry_clear();
    clear_captured_exec();
    unlink_impl("/tmp/exec-script-link");
    unlink_impl("/tmp/exec-script-real");

    if (create_exec_file("/tmp/exec-script-real", "#!/usr/bin/interp\n") != 0) {
        goto out;
    }
    if (symlinkat("/tmp/exec-script-real", AT_FDCWD, "/tmp/exec-script-link") != 0) {
        goto out;
    }
    if (native_register("/usr/bin/interp", native_capture_exec) != 0) {
        goto out;
    }

    status = execve("/tmp/exec-script-link", argv, envp);
    if (status != 37) {
        errno = EPROTO;
        goto out;
    }
    if (!task_execed(task) ||
        strcmp(task->exe, "/tmp/exec-script-real") != 0 ||
        strcmp(task->comm, "script-link") != 0) {
        errno = EPROTO;
        goto out;
    }
    if (!task->exec_image ||
        strcmp(task->exec_image->path, "/tmp/exec-script-real") != 0 ||
        strcmp(task->exec_image->interpreter, "/usr/bin/interp") != 0 ||
        task->exec_image->type != EXEC_IMAGE_SCRIPT) {
        errno = EPROTO;
        goto out;
    }
    if (captured_argc != 3 ||
        strcmp(captured_argv[0], "/usr/bin/interp") != 0 ||
        strcmp(captured_argv[1], "/tmp/exec-script-real") != 0 ||
        strcmp(captured_argv[2], "arg1") != 0 ||
        strcmp(captured_env0, "LINK=1") != 0) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    native_registry_clear();
    unlink_impl("/tmp/exec-script-link");
    unlink_impl("/tmp/exec-script-real");
    return result;
}

int exec_syscall_contract_nested_script_interpreter_chains_to_native(void) {
    struct task *task = task_current();
    char *argv[] = {"outer-name", "arg1", NULL};
    char *envp[] = {"NESTED=1", NULL};
    const char *const expected_cmdline[] = {
        "/usr/bin/interp",
        "/tmp/exec-inner-script",
        "/tmp/exec-outer-script",
        "arg1",
        NULL,
    };
    const char *const expected_environ[] = {"NESTED=1", NULL};
    char buf[256];
    ssize_t nread;
    int status;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    native_registry_clear();
    clear_captured_exec();
    unlink_impl("/tmp/exec-outer-script");
    unlink_impl("/tmp/exec-inner-script");

    if (create_exec_file("/tmp/exec-outer-script", "#!/tmp/exec-inner-script\n") != 0) {
        goto out;
    }
    if (create_exec_file("/tmp/exec-inner-script", "#!/usr/bin/interp\n") != 0) {
        goto out;
    }
    if (native_register("/usr/bin/interp", native_capture_exec) != 0) {
        goto out;
    }

    status = execve("/tmp/exec-outer-script", argv, envp);
    if (status != 37) {
        errno = EPROTO;
        goto out;
    }
    if (!task_execed(task) ||
        strcmp(task->exe, "/tmp/exec-outer-script") != 0 ||
        strcmp(task->comm, "outer-name") != 0) {
        errno = EPROTO;
        goto out;
    }
    if (!task->exec_image ||
        strcmp(task->exec_image->path, "/tmp/exec-outer-script") != 0 ||
        strcmp(task->exec_image->interpreter, "/usr/bin/interp") != 0 ||
        task->exec_image->type != EXEC_IMAGE_SCRIPT) {
        errno = EPROTO;
        goto out;
    }
    if (captured_argc != 4 ||
        strcmp(captured_argv[0], "/usr/bin/interp") != 0 ||
        strcmp(captured_argv[1], "/tmp/exec-inner-script") != 0 ||
        strcmp(captured_argv[2], "/tmp/exec-outer-script") != 0 ||
        strcmp(captured_argv[3], "arg1") != 0 ||
        strcmp(captured_env0, "NESTED=1") != 0) {
        errno = EPROTO;
        goto out;
    }

    if (read_proc_file("/proc/self/cmdline", buf, sizeof(buf), &nread) != 0) {
        goto out;
    }
    if (expect_nul_vector(buf, nread, expected_cmdline) != 0) {
        errno = ENOMSG;
        goto out;
    }

    if (read_proc_file("/proc/self/environ", buf, sizeof(buf), &nread) != 0) {
        goto out;
    }
    if (expect_nul_vector(buf, nread, expected_environ) != 0) {
        errno = ENODATA;
        goto out;
    }

    result = 0;

out:
    native_registry_clear();
    unlink_impl("/tmp/exec-outer-script");
    unlink_impl("/tmp/exec-inner-script");
    return result;
}

int exec_syscall_contract_recursive_script_loop_returns_eloop_without_transition(void) {
    struct task *task = task_current();
    char *argv[] = {"loop-name", NULL};
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    native_registry_clear();
    clear_captured_exec();
    unlink_impl("/tmp/exec-loop-a");
    unlink_impl("/tmp/exec-loop-b");

    if (create_exec_file("/tmp/exec-loop-a", "#!/tmp/exec-loop-b\n") != 0) {
        goto out;
    }
    if (create_exec_file("/tmp/exec-loop-b", "#!/tmp/exec-loop-a\n") != 0) {
        goto out;
    }

    memcpy(task->exe, "/before", 8);
    memset(task->comm, 0, sizeof(task->comm));
    memcpy(task->comm, "before", 7);
    task_reset_execed(task);

    errno = 0;
    if (execve("/tmp/exec-loop-a", argv, NULL) != -1) {
        errno = EPROTO;
        goto out;
    }
    if (expect_errno(ELOOP) != 0) {
        goto out;
    }
    if (verify_state_unchanged(task, "/before", "before", false, -1, -1) != 0) {
        goto out;
    }
    if (captured_argc != 0 || captured_argv[0][0] != '\0' || captured_env0[0] != '\0') {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    unlink_impl("/tmp/exec-loop-a");
    unlink_impl("/tmp/exec-loop-b");
    return result;
}

int exec_syscall_contract_missing_script_interpreter_preserves_state(void) {
    struct task *task = task_current();
    char *argv[] = {"script-name", NULL};
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    native_registry_clear();
    unlink_impl("/tmp/exec-script-missing-interpreter");
    if (create_exec_file("/tmp/exec-script-missing-interpreter", "#!/usr/bin/not-there\n") != 0) {
        goto out;
    }

    memcpy(task->exe, "/before", 8);
    memset(task->comm, 0, sizeof(task->comm));
    memcpy(task->comm, "before", 7);
    task_reset_execed(task);

    errno = 0;
    if (execve("/tmp/exec-script-missing-interpreter", argv, NULL) != -1) {
        errno = EPROTO;
        goto out;
    }
    if (expect_errno(ENOENT) != 0) {
        goto out;
    }
    if (verify_state_unchanged(task, "/before", "before", false, -1, -1) != 0) {
        goto out;
    }

    result = 0;

out:
    unlink_impl("/tmp/exec-script-missing-interpreter");
    return result;
}

int exec_syscall_contract_fexecve_uses_fd_path(void) {
    struct task *task = task_current();
    char *argv[] = {"fd-native", NULL};
    char *envp[] = {"FD=1", NULL};
    int fd = -1;
    int status;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    native_registry_clear();
    clear_captured_exec();
    unlink_impl("/tmp/exec-native-fd");
    if (create_exec_file("/tmp/exec-native-fd", "") != 0) {
        goto out;
    }
    fd = open_impl("//tmp///exec-native-fd", O_RDONLY, 0);
    if (fd < 0) {
        goto out;
    }
    if (native_register("/tmp/exec-native-fd", native_capture_exec) != 0) {
        goto out;
    }

    status = fexecve_impl(fd, argv, envp);
    if (status != 37) {
        errno = EPROTO;
        goto out;
    }
    if (!task_execed(task) ||
        strcmp(task->exe, "/tmp/exec-native-fd") != 0 ||
        strcmp(task->comm, "fd-native") != 0 ||
        captured_argc != 1 ||
        strcmp(captured_argv[0], "fd-native") != 0 ||
        strcmp(captured_env0, "FD=1") != 0) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    native_registry_clear();
    close_if_open(fd);
    unlink_impl("/tmp/exec-native-fd");
    return result;
}

int exec_syscall_contract_fexecve_rejects_invalid_fd(void) {
    errno = 0;
    if (fexecve_impl(240, NULL, NULL) != -1) {
        errno = EPROTO;
        return -1;
    }
    return expect_errno(EBADF);
}

int exec_syscall_contract_execveat_uses_dirfd_relative_path(void) {
    struct task *task = task_current();
    char *argv[] = {"execveat-rel", NULL};
    char *envp[] = {"EXECVEAT=REL", NULL};
    const char dir[] = "/tmp/execveat-dir";
    const char path[] = "/tmp/execveat-dir/native-rel";
    int dirfd = -1;
    long status;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    native_registry_clear();
    clear_captured_exec();
    unlink_impl(path);
    rmdir_impl(dir);
    if (mkdir_impl(dir, 0700) != 0) {
        goto out;
    }
    if (create_exec_file(path, "") != 0) {
        goto out;
    }
    dirfd = open_impl(dir, O_RDONLY | O_DIRECTORY, 0);
    if (dirfd < 0) {
        goto out;
    }
    if (native_register(path, native_capture_exec) != 0) {
        goto out;
    }

    status = syscall_dispatch_impl(__NR_execveat, dirfd, (long)(uintptr_t)"native-rel",
                                   (long)(uintptr_t)argv, (long)(uintptr_t)envp, 0, 0);
    if (status != 37) {
        errno = status < 0 ? (int)-status : EPROTO;
        goto out;
    }
    if (!task_execed(task) ||
        strcmp(task->exe, path) != 0 ||
        strcmp(task->comm, "execveat-rel") != 0 ||
        captured_argc != 1 ||
        strcmp(captured_argv[0], "execveat-rel") != 0 ||
        strcmp(captured_env0, "EXECVEAT=REL") != 0) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    native_registry_clear();
    close_if_open(dirfd);
    unlink_impl(path);
    rmdir_impl(dir);
    return result;
}

int exec_syscall_contract_execveat_empty_path_uses_fd(void) {
    struct task *task = task_current();
    char *argv[] = {"execveat-fd", NULL};
    char *envp[] = {"EXECVEAT=FD", NULL};
    const char path[] = "/tmp/execveat-fd";
    int fd = -1;
    long status;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    native_registry_clear();
    clear_captured_exec();
    unlink_impl(path);
    if (create_exec_file(path, "") != 0) {
        goto out;
    }
    fd = open_impl(path, O_RDONLY, 0);
    if (fd < 0) {
        goto out;
    }
    if (native_register(path, native_capture_exec) != 0) {
        goto out;
    }

    status = syscall_dispatch_impl(__NR_execveat, fd, (long)(uintptr_t)"",
                                   (long)(uintptr_t)argv, (long)(uintptr_t)envp,
                                   AT_EMPTY_PATH, 0);
    if (status != 37) {
        errno = status < 0 ? (int)-status : EPROTO;
        goto out;
    }
    if (!task_execed(task) ||
        strcmp(task->exe, path) != 0 ||
        strcmp(task->comm, "execveat-fd") != 0 ||
        captured_argc != 1 ||
        strcmp(captured_argv[0], "execveat-fd") != 0 ||
        strcmp(captured_env0, "EXECVEAT=FD") != 0) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    native_registry_clear();
    close_if_open(fd);
    unlink_impl(path);
    return result;
}

int exec_syscall_contract_execveat_nofollow_rejects_symlink(void) {
    struct task *task = task_current();
    char *argv[] = {"execveat-link", NULL};
    const char target[] = "/tmp/execveat-target";
    const char linkpath[] = "/tmp/execveat-link";
    long status;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    native_registry_clear();
    clear_captured_exec();
    unlink_impl(linkpath);
    unlink_impl(target);
    if (create_exec_file(target, "") != 0) {
        goto out;
    }
    if (symlinkat(target, AT_FDCWD, linkpath) != 0) {
        goto out;
    }

    status = syscall_dispatch_impl(__NR_execveat, AT_FDCWD, (long)(uintptr_t)linkpath,
                                   (long)(uintptr_t)argv, 0, AT_SYMLINK_NOFOLLOW, 0);
    if (status != -ELOOP) {
        errno = status < 0 ? (int)-status : EPROTO;
        goto out;
    }

    result = 0;

out:
    native_registry_clear();
    unlink_impl(linkpath);
    unlink_impl(target);
    return result;
}

int exec_syscall_contract_elf64_aarch64_exec_loads_virtual_image(void) {
    struct task *task = task_current();
    char *argv[] = {"elf-prog", "arg1", NULL};
    char *envp[] = {"ELF=1", NULL};
    const char *const expected_cmdline[] = {"elf-prog", "arg1", NULL};
    const char *const expected_environ[] = {"ELF=1", NULL};
    unsigned char image[sizeof(Elf64_Ehdr) + 8];
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)image;
    char buf[256];
    ssize_t nread = 0;
    int status;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    native_registry_clear();
    unlink_impl("/tmp/exec-elf64");
    memset(image, 0, sizeof(image));
    build_minimal_elf64_aarch64(ehdr);
    image[sizeof(Elf64_Ehdr)] = 0xaa;
    image[sizeof(Elf64_Ehdr) + 1] = 0xbb;
    if (create_exec_bytes("/tmp/exec-elf64", image, sizeof(image)) != 0) {
        goto out;
    }

    memcpy(task->exe, "/before", 8);
    memset(task->comm, 0, sizeof(task->comm));
    memcpy(task->comm, "before", 7);
    task_reset_execed(task);

    status = execve("/tmp/exec-elf64", argv, envp);
    if (status != 0) {
        errno = EPROTO;
        goto out;
    }
    if (!task_execed(task) ||
        strcmp(task->exe, "/tmp/exec-elf64") != 0 ||
        strcmp(task->comm, "elf-prog") != 0) {
        errno = EPROTO;
        goto out;
    }
    if (!task->exec_image ||
        task->exec_image->type != EXEC_IMAGE_ELF ||
        strcmp(task->exec_image->path, "/tmp/exec-elf64") != 0 ||
        task->exec_image->u.elf.entry != ehdr->e_entry ||
        task->exec_image->u.elf.machine != EM_AARCH64) {
        errno = EPROTO;
        goto out;
    }
    if (!task->mm ||
        !task->mm->exec_image_base ||
        task->mm->exec_image_size != sizeof(image) ||
        memcmp(task->mm->exec_image_base, image, sizeof(image)) != 0) {
        errno = EPROTO;
        goto out;
    }
    if (read_proc_file("/proc/self/cmdline", buf, sizeof(buf), &nread) != 0 ||
        expect_nul_vector(buf, nread, expected_cmdline) != 0) {
        goto out;
    }
    if (read_proc_file("/proc/self/environ", buf, sizeof(buf), &nread) != 0 ||
        expect_nul_vector(buf, nread, expected_environ) != 0) {
        goto out;
    }

    result = 0;

out:
    unlink_impl("/tmp/exec-elf64");
    return result;
}

int exec_syscall_contract_elf_program_headers_create_virtual_segments(void) {
    struct task *task = task_current();
    unsigned char image[sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr) + 64];
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)image;
    Elf64_Phdr *load;
    int status;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    native_registry_clear();
    unlink_impl("/tmp/exec-elf-loadable");
    build_exec_elf64_without_interp_with_bss(image, sizeof(image), 0x400000, 0x400000);
    load = (Elf64_Phdr *)(image + ehdr->e_phoff);
    if (create_exec_bytes("/tmp/exec-elf-loadable", image, sizeof(image)) != 0) {
        goto out;
    }

    status = execve("/tmp/exec-elf-loadable", NULL, NULL);
    if (status != 0) {
        errno = EPROTO;
        goto out;
    }
    if (!task->mm ||
        task->mm->exec_entry != ehdr->e_entry ||
        task->mm->exec_segment_count != 1 ||
        task->mm->exec_segments[0].vaddr != load->p_vaddr ||
        task->mm->exec_segments[0].filesz != load->p_filesz ||
        task->mm->exec_segments[0].memsz != load->p_memsz ||
        task->mm->exec_segments[0].offset != load->p_offset ||
        task->mm->exec_segments[0].flags != load->p_flags ||
        expect_segment_image(task->mm->exec_segments[0].image,
                             task->mm->exec_segments[0].image_size,
                             image, load) != 0 ||
        expect_task_vm_bytes(task, load->p_vaddr,
                             image + load->p_offset, 2) != 0 ||
        expect_task_vm_zeroes(task, load->p_vaddr + load->p_filesz, 16) != 0) {
        errno = EPROTO;
        goto out;
    }
    if (!task->exec_image ||
        task->exec_image->interpreter[0] != '\0' ||
        task->exec_image->u.elf.type != ET_EXEC) {
        errno = EPROTO;
        goto out;
    }
    if (task->mm->interp_image_base ||
        task->mm->interp_image_size != 0 ||
        task->mm->interp_entry != 0 ||
        task->mm->interp_segment_count != 0 ||
        task->mm->entry_point != ehdr->e_entry) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    unlink_impl("/tmp/exec-elf-loadable");
    return result;
}

int exec_syscall_contract_elf_interp_loads_virtual_loader_image(void) {
    struct task *task = task_current();
    const char interp_path[] = "/tmp/exec-elf-loader";
    unsigned char image[sizeof(Elf64_Ehdr) + (2 * sizeof(Elf64_Phdr)) + 96];
    unsigned char loader[sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr) + 64];
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)image;
    Elf64_Ehdr *loader_ehdr = (Elf64_Ehdr *)loader;
    Elf64_Phdr *loader_load;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    unlink_impl("/tmp/exec-elf-dynamic");
    unlink_impl(interp_path);
    build_exec_elf64_with_interp(image, sizeof(image), interp_path, 0x401000, 0x400000);
    build_exec_elf64_without_interp_with_bss(loader, sizeof(loader), 0x701000, 0x700000);
    loader_load = (Elf64_Phdr *)(loader + loader_ehdr->e_phoff);
    if (create_exec_bytes("/tmp/exec-elf-dynamic", image, sizeof(image)) != 0 ||
        create_exec_bytes(interp_path, loader, sizeof(loader)) != 0) {
        goto out;
    }

    if (execve("/tmp/exec-elf-dynamic", NULL, NULL) != 0) {
        errno = EPROTO;
        goto out;
    }

    if (!task->mm ||
        !task->mm->exec_image_base ||
        !task->mm->interp_image_base ||
        task->mm->exec_entry != ehdr->e_entry ||
        task->mm->interp_entry != loader_ehdr->e_entry ||
        task->mm->entry_point != loader_ehdr->e_entry ||
        strcmp(task->mm->interp_path, interp_path) != 0 ||
        task->mm->interp_image_size != sizeof(loader) ||
        memcmp(task->mm->interp_image_base, loader, sizeof(loader)) != 0 ||
        task->mm->interp_segment_count != 1 ||
        task->mm->interp_segments[0].vaddr != loader_load->p_vaddr ||
        task->mm->interp_segments[0].filesz != loader_load->p_filesz ||
        task->mm->interp_segments[0].memsz != loader_load->p_memsz ||
        task->mm->interp_segments[0].offset != loader_load->p_offset ||
        task->mm->interp_segments[0].flags != loader_load->p_flags ||
        expect_segment_image(task->mm->interp_segments[0].image,
                             task->mm->interp_segments[0].image_size,
                             loader, loader_load) != 0 ||
        expect_task_vm_bytes(task, loader_load->p_vaddr,
                             loader + loader_load->p_offset, 2) != 0 ||
        expect_task_vm_zeroes(task, loader_load->p_vaddr + loader_load->p_filesz, 16) != 0) {
        errno = EPROTO;
        goto out;
    }
    if (!task->exec_image ||
        strcmp(task->exec_image->interpreter, interp_path) != 0 ||
        task->exec_image->u.elf.entry != ehdr->e_entry) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    unlink_impl("/tmp/exec-elf-dynamic");
    unlink_impl(interp_path);
    return result;
}

int exec_syscall_contract_elf_static_builds_initial_stack_and_auxv(void) {
    struct task *task = task_current();
    char *argv[] = {"elf-static", "one", NULL};
    char *envp[] = {"A=B", NULL};
    const char *const expected_argv[] = {"elf-static", "one", NULL};
    const char *const expected_envp[] = {"A=B", NULL};
    unsigned char image[sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr) + 64];
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)image;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    unlink_impl("/tmp/exec-elf-stack-static");
    build_exec_elf64_without_interp(image, sizeof(image), 0x402000, 0x400000);
    if (create_exec_bytes("/tmp/exec-elf-stack-static", image, sizeof(image)) != 0) {
        goto out;
    }

    if (execve("/tmp/exec-elf-stack-static", argv, envp) != 0) {
        errno = EPROTO;
        goto out;
    }

    if (expect_common_elf_initial_stack(task, ehdr, 2, 1) != 0 ||
        expect_auxv_value(task, AT_BASE, 0) != 0 ||
        expect_auxv_value(task, AT_PHDR, ehdr->e_phoff) != 0 ||
        expect_materialized_stack_vector(task, expected_argv, expected_envp) != 0 ||
        expect_materialized_auxv_strings(task) != 0 ||
        expect_task_vm_bytes(task, task->mm->initial_argv[0], "elf-static", sizeof("elf-static")) != 0 ||
        expect_task_vm_bytes(task, task->mm->initial_envp[0], "A=B", sizeof("A=B")) != 0) {
        goto out;
    }

    errno = 0;
    if (task_read_virtual_memory_impl(task, 0x1000, image, 1) != -1 ||
        expect_errno(EFAULT) != 0) {
        goto out;
    }

    result = 0;

out:
    unlink_impl("/tmp/exec-elf-stack-static");
    return result;
}

int exec_syscall_contract_elf_dynamic_auxv_points_to_loader_base(void) {
    struct task *task = task_current();
    const char interp_path[] = "/tmp/exec-elf-aux-loader";
    char *argv[] = {"elf-dynamic", NULL};
    const char *const expected_argv[] = {"elf-dynamic", NULL};
    const char *const expected_envp[] = {NULL};
    unsigned char image[sizeof(Elf64_Ehdr) + (2 * sizeof(Elf64_Phdr)) + 96];
    unsigned char loader[sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr) + 64];
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)image;
    Elf64_Ehdr *loader_ehdr = (Elf64_Ehdr *)loader;
    Elf64_Phdr *loader_load;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    unlink_impl("/tmp/exec-elf-aux-dynamic");
    unlink_impl(interp_path);
    build_exec_elf64_with_interp(image, sizeof(image), interp_path, 0x403000, 0x400000);
    build_exec_elf64_without_interp(loader, sizeof(loader), 0x703000, 0x700000);
    loader_load = (Elf64_Phdr *)(loader + loader_ehdr->e_phoff);
    if (create_exec_bytes("/tmp/exec-elf-aux-dynamic", image, sizeof(image)) != 0 ||
        create_exec_bytes(interp_path, loader, sizeof(loader)) != 0) {
        goto out;
    }

    if (execve("/tmp/exec-elf-aux-dynamic", argv, NULL) != 0) {
        errno = EPROTO;
        goto out;
    }

    if (expect_common_elf_initial_stack(task, ehdr, 1, 0) != 0 ||
        expect_auxv_value(task, AT_BASE, loader_load->p_vaddr) != 0 ||
        expect_materialized_stack_vector(task, expected_argv, expected_envp) != 0 ||
        expect_materialized_auxv_strings(task) != 0 ||
        task->mm->entry_point != loader_ehdr->e_entry ||
        task->mm->exec_entry != ehdr->e_entry ||
        task->mm->interp_entry != loader_ehdr->e_entry) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    unlink_impl("/tmp/exec-elf-aux-dynamic");
    unlink_impl(interp_path);
    return result;
}

int exec_syscall_contract_elf_auxv_records_virtual_credentials(void) {
    struct task *task = task_current();
    const struct cred *cred;
    unsigned char image[sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr) + 64];
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    unlink_impl("/tmp/exec-elf-aux-cred");
    build_exec_elf64_without_interp(image, sizeof(image), 0x404000, 0x400000);
    if (create_exec_bytes("/tmp/exec-elf-aux-cred", image, sizeof(image)) != 0) {
        goto out;
    }

    if (execve("/tmp/exec-elf-aux-cred", NULL, NULL) != 0) {
        errno = EPROTO;
        goto out;
    }

    cred = cred_current();
    if (!cred ||
        expect_auxv_value(task, AT_UID, cred->uid) != 0 ||
        expect_auxv_value(task, AT_EUID, cred->euid) != 0 ||
        expect_auxv_value(task, AT_GID, cred->gid) != 0 ||
        expect_auxv_value(task, AT_EGID, cred->egid) != 0 ||
        expect_auxv_value(task, AT_SECURE, 0) != 0) {
        goto out;
    }

    result = 0;

out:
    unlink_impl("/tmp/exec-elf-aux-cred");
    return result;
}

int exec_syscall_contract_elf_setid_auxv_sets_at_secure_and_dumpable(void) {
    struct task *task = task_current();
    unsigned char image[sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr) + 64];
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    cred_reset_to_defaults();
    unlink_impl("/tmp/exec-elf-secure-setid");
    build_exec_elf64_without_interp(image, sizeof(image), 0x404000, 0x400000);
    if (create_exec_bytes("/tmp/exec-elf-secure-setid", image, sizeof(image)) != 0 ||
        chown("/tmp/exec-elf-secure-setid", 2000, 3000) != 0 ||
        chmod("/tmp/exec-elf-secure-setid", S_ISUID | S_ISGID | 0755) != 0 ||
        setgid_impl(1000) != 0 ||
        setuid_impl(1000) != 0) {
        goto out;
    }

    if (execve("/tmp/exec-elf-secure-setid", NULL, NULL) != 0) {
        errno = EPROTO;
        goto out;
    }

    if (expect_auxv_value(task, AT_UID, 1000) != 0 ||
        expect_auxv_value(task, AT_EUID, 2000) != 0 ||
        expect_auxv_value(task, AT_GID, 1000) != 0 ||
        expect_auxv_value(task, AT_EGID, 3000) != 0 ||
        expect_auxv_value(task, AT_SECURE, 1) != 0 ||
        task->exec_secure != 1 ||
        task->exec_dumpable != 0) {
        goto out;
    }

    result = 0;

out:
    unlink_impl("/tmp/exec-elf-secure-setid");
    cred_reset_to_defaults();
    task->exec_secure = 0;
    task->exec_dumpable = 1;
    return result;
}

int exec_syscall_contract_elf_virtual_memory_writes_writable_segment(void) {
    struct task *task = task_current();
    unsigned char image[sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr) + 64];
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)image;
    Elf64_Phdr *load;
    const unsigned char patch[] = {0x12, 0x34, 0x56, 0x78};
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    unlink_impl("/tmp/exec-elf-vm-write");
    build_exec_elf64_writable_load(image, sizeof(image), 0x405000, 0x500000);
    load = (Elf64_Phdr *)(image + ehdr->e_phoff);
    if (create_exec_bytes("/tmp/exec-elf-vm-write", image, sizeof(image)) != 0) {
        goto out;
    }

    if (execve("/tmp/exec-elf-vm-write", NULL, NULL) != 0) {
        errno = EPROTO;
        goto out;
    }

    if (task->mm->exec_segments[0].flags != (PF_R | PF_W) ||
        expect_task_vm_write(task, load->p_vaddr + 1, patch, sizeof(patch)) != 0) {
        goto out;
    }

    result = 0;

out:
    unlink_impl("/tmp/exec-elf-vm-write");
    return result;
}

int exec_syscall_contract_elf_virtual_memory_writes_initial_stack(void) {
    struct task *task = task_current();
    char *argv[] = {"elf-stack-write", NULL};
    unsigned char image[sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr) + 64];
    const char replacement[] = "stack-mutated";
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    unlink_impl("/tmp/exec-elf-vm-stack");
    build_exec_elf64_without_interp(image, sizeof(image), 0x406000, 0x400000);
    if (create_exec_bytes("/tmp/exec-elf-vm-stack", image, sizeof(image)) != 0) {
        goto out;
    }

    if (execve("/tmp/exec-elf-vm-stack", argv, NULL) != 0) {
        errno = EPROTO;
        goto out;
    }

    if (expect_task_vm_write(task, task->mm->initial_argv[0], replacement, sizeof(replacement)) != 0 ||
        expect_stack_string(task, task->mm->initial_argv[0], replacement) != 0) {
        goto out;
    }

    result = 0;

out:
    unlink_impl("/tmp/exec-elf-vm-stack");
    return result;
}

int exec_syscall_contract_elf_virtual_memory_fault_policy(void) {
    struct task *task = task_current();
    unsigned char image[sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr) + 64];
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)image;
    Elf64_Phdr *load;
    const unsigned char patch[] = {0x90, 0x91};
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    unlink_impl("/tmp/exec-elf-vm-faults");
    build_exec_elf64_without_interp_with_bss(image, sizeof(image), 0x407000, 0x600000);
    load = (Elf64_Phdr *)(image + ehdr->e_phoff);
    load->p_flags = PF_X;
    if (create_exec_bytes("/tmp/exec-elf-vm-faults", image, sizeof(image)) != 0) {
        goto out;
    }

    if (execve("/tmp/exec-elf-vm-faults", NULL, NULL) != 0) {
        errno = EPROTO;
        goto out;
    }

    errno = 0;
    if (task_write_virtual_memory_impl(task, load->p_vaddr, patch, sizeof(patch)) != -1 ||
        expect_errno(EACCES) != 0) {
        goto out;
    }

    errno = 0;
    if (task_read_virtual_memory_impl(task, load->p_vaddr, image, 1) != -1 ||
        expect_errno(EACCES) != 0) {
        goto out;
    }

    errno = 0;
    if (task_write_virtual_memory_impl(task, 0x1000, patch, sizeof(patch)) != -1 ||
        expect_errno(EFAULT) != 0) {
        goto out;
    }

    errno = 0;
    if (task_write_virtual_memory_impl(task, task->mm->initial_stack_pointer, NULL, 1) != -1 ||
        expect_errno(EFAULT) != 0) {
        goto out;
    }

    if (task_write_virtual_memory_impl(task, task->mm->initial_stack_pointer, NULL, 0) != 0) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    signal_clear_pending_task(task, SIGSEGV);
    unlink_impl("/tmp/exec-elf-vm-faults");
    return result;
}

int exec_syscall_contract_elf_vma_metadata_covers_exec_loader_and_stack(void) {
    struct task *task = task_current();
    const char interp_path[] = "/tmp/exec-elf-vma-loader";
    unsigned char image[sizeof(Elf64_Ehdr) + (2 * sizeof(Elf64_Phdr)) + 96];
    unsigned char loader[sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr) + 64];
    Elf64_Ehdr *loader_ehdr = (Elf64_Ehdr *)loader;
    Elf64_Phdr *exec_load;
    Elf64_Phdr *loader_load;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    unlink_impl("/tmp/exec-elf-vma");
    unlink_impl(interp_path);
    build_exec_elf64_with_interp(image, sizeof(image), interp_path, 0x408000, 0x400000);
    build_exec_elf64_without_interp_with_bss(loader, sizeof(loader), 0x708000, 0x700000);
    exec_load = ((Elf64_Phdr *)(image + ((Elf64_Ehdr *)image)->e_phoff)) + 1;
    loader_load = (Elf64_Phdr *)(loader + loader_ehdr->e_phoff);
    if (create_exec_bytes("/tmp/exec-elf-vma", image, sizeof(image)) != 0 ||
        create_exec_bytes(interp_path, loader, sizeof(loader)) != 0) {
        goto out;
    }

    if (execve("/tmp/exec-elf-vma", NULL, NULL) != 0) {
        errno = EPROTO;
        goto out;
    }

    if (task->mm->vma_count != 4 ||
        expect_vma(task, 0, exec_load->p_vaddr, exec_load->p_memsz, exec_load->p_flags,
                   TASK_VMA_EXEC, task->mm->exec_segments[0].image) != 0 ||
        expect_vma(task, 1, loader_load->p_vaddr, loader_load->p_memsz, loader_load->p_flags,
                   TASK_VMA_INTERP, task->mm->interp_segments[0].image) != 0 ||
        expect_vma(task, 2, task->mm->initial_stack_base - TASK_VMA_PAGE_SIZE, TASK_VMA_PAGE_SIZE,
                   0, TASK_VMA_GUARD, task->mm->stack_guard_image) != 0 ||
        expect_vma(task, 3, task->mm->initial_stack_base, task->mm->initial_stack_image_size,
                   PF_R | PF_W, TASK_VMA_STACK, task->mm->initial_stack_image) != 0 ||
        task_find_vma_impl(task, exec_load->p_vaddr)->kind != TASK_VMA_EXEC ||
        task_find_vma_impl(task, loader_load->p_vaddr)->kind != TASK_VMA_INTERP ||
        task_find_vma_impl(task, task->mm->initial_stack_base - 1)->kind != TASK_VMA_GUARD ||
        task_find_vma_impl(task, task->mm->initial_stack_pointer)->kind != TASK_VMA_STACK ||
        task_find_vma_impl(task, 0x2000) != NULL) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    unlink_impl("/tmp/exec-elf-vma");
    unlink_impl(interp_path);
    return result;
}

int exec_syscall_contract_elf_below_stack_guard_faults_with_sigsegv_maperr(void) {
    struct task *task = task_current();
    unsigned char image[4096];
    char byte = 'g';
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    unlink_impl("/tmp/exec-elf-stack-guard");
    build_exec_elf64_without_interp(image, sizeof(image), 0x401000, 0x400000);
    if (create_exec_bytes("/tmp/exec-elf-stack-guard", image, sizeof(image)) != 0) {
        goto out;
    }
    if (execve("/tmp/exec-elf-stack-guard", NULL, NULL) != 0) {
        goto out;
    }
    if (task_write_virtual_memory_impl(task, task->mm->initial_stack_base - TASK_VMA_PAGE_SIZE - 1,
                                       &byte, 1) != -1 ||
        errno != EFAULT ||
        task->last_fault_signal != SIGSEGV ||
        task->last_fault_code != SEGV_MAPERR ||
        task->last_fault_addr != task->mm->initial_stack_base - TASK_VMA_PAGE_SIZE - 1) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    signal_clear_pending_task(task, SIGSEGV);
    unlink_impl("/tmp/exec-elf-stack-guard");
    return result;
}

int exec_syscall_contract_elf_stack_grows_down_within_rlimit(void) {
    struct task *task = task_current();
    unsigned char image[4096];
    char byte = 's';
    uint64_t old_base;
    uint64_t old_size;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    unlink_impl("/tmp/exec-elf-stack-grow");
    build_exec_elf64_without_interp(image, sizeof(image), 0x401000, 0x400000);
    if (create_exec_bytes("/tmp/exec-elf-stack-grow", image, sizeof(image)) != 0) {
        goto out;
    }
    if (execve("/tmp/exec-elf-stack-grow", NULL, NULL) != 0) {
        goto out;
    }
    task->rlimits[RLIMIT_STACK].cur = 16ULL * 1024ULL * 1024ULL;

    old_base = task->mm->initial_stack_base;
    old_size = task->mm->initial_stack_image_size;
    if (task->rlimits[RLIMIT_STACK].cur != 16ULL * 1024ULL * 1024ULL ||
        old_size + TASK_VMA_PAGE_SIZE > task->rlimits[RLIMIT_STACK].cur) {
        errno = ENOSPC;
        goto out;
    }
    {
        const struct task_vma *guard = task_find_vma_impl(task, old_base - 1);
        const struct task_vma *stack = task_find_vma_impl(task, old_base);
        if (!guard || guard->kind != TASK_VMA_GUARD) {
            errno = ENXIO;
            goto out;
        }
        if (!stack || stack->kind != TASK_VMA_STACK) {
            errno = ENODEV;
            goto out;
        }
        if (guard->end != stack->start || !stack->image || stack->image_size == 0) {
            errno = ENOTCONN;
            goto out;
        }
    }
    if (task_write_virtual_memory_impl(task, old_base - 1, &byte, 1) != 1) {
        if (errno == EACCES) {
            errno = ENOLCK;
        }
        goto out;
    }
    if (task->mm->initial_stack_base != old_base - TASK_VMA_PAGE_SIZE ||
        task->mm->initial_stack_image_size != old_size + TASK_VMA_PAGE_SIZE ||
        task_find_vma_impl(task, old_base - 1)->kind != TASK_VMA_STACK ||
        task_find_vma_impl(task, task->mm->initial_stack_base - 1)->kind != TASK_VMA_GUARD) {
        errno = EPROTO;
        goto out;
    }
    byte = 0;
    if (task_read_virtual_memory_impl(task, old_base - 1, &byte, 1) != 1 || byte != 's') {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    unlink_impl("/tmp/exec-elf-stack-grow");
    return result;
}

int exec_syscall_contract_elf_stack_growth_respects_rlimit(void) {
    struct task *task = task_current();
    unsigned char image[4096];
    char byte = 'l';
    uint64_t old_base;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    unlink_impl("/tmp/exec-elf-stack-rlimit");
    build_exec_elf64_without_interp(image, sizeof(image), 0x401000, 0x400000);
    if (create_exec_bytes("/tmp/exec-elf-stack-rlimit", image, sizeof(image)) != 0) {
        goto out;
    }
    task->rlimits[RLIMIT_STACK].cur = 8ULL * 1024ULL * 1024ULL;
    if (execve("/tmp/exec-elf-stack-rlimit", NULL, NULL) != 0) {
        goto out;
    }

    old_base = task->mm->initial_stack_base;
    errno = 0;
    if (task_write_virtual_memory_impl(task, old_base - 1, &byte, 1) != -1 ||
        errno != EACCES ||
        task->mm->initial_stack_base != old_base ||
        task->last_fault_signal != SIGSEGV ||
        task->last_fault_code != SEGV_ACCERR ||
        task->last_fault_addr != old_base - 1) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    signal_clear_pending_task(task, SIGSEGV);
    unlink_impl("/tmp/exec-elf-stack-rlimit");
    return result;
}

int exec_syscall_contract_elf_stack_growth_keeps_lower_guard_faulting(void) {
    struct task *task = task_current();
    unsigned char image[4096];
    char byte = 'g';
    uint64_t old_base;
    uint64_t lower_guard_addr;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    unlink_impl("/tmp/exec-elf-stack-lower-guard");
    build_exec_elf64_without_interp(image, sizeof(image), 0x401000, 0x400000);
    if (create_exec_bytes("/tmp/exec-elf-stack-lower-guard", image, sizeof(image)) != 0) {
        goto out;
    }
    if (execve("/tmp/exec-elf-stack-lower-guard", NULL, NULL) != 0) {
        goto out;
    }
    task->rlimits[RLIMIT_STACK].cur = 16ULL * 1024ULL * 1024ULL;

    old_base = task->mm->initial_stack_base;
    if (task_write_virtual_memory_impl(task, old_base - 1, &byte, 1) != 1 ||
        task->mm->initial_stack_base != old_base - TASK_VMA_PAGE_SIZE ||
        task_find_vma_impl(task, old_base - 1)->kind != TASK_VMA_STACK ||
        task_find_vma_impl(task, task->mm->initial_stack_base - 1)->kind != TASK_VMA_GUARD) {
        errno = EPROTO;
        goto out;
    }

    lower_guard_addr = task->mm->initial_stack_base - TASK_VMA_PAGE_SIZE - 1;
    if (task_write_virtual_memory_impl(task, lower_guard_addr, &byte, 1) != -1 ||
        errno != EFAULT ||
        task->last_fault_signal != SIGSEGV ||
        task->last_fault_code != SEGV_MAPERR ||
        task->last_fault_addr != lower_guard_addr) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    signal_clear_pending_task(task, SIGSEGV);
    unlink_impl("/tmp/exec-elf-stack-lower-guard");
    return result;
}

int exec_syscall_contract_elf_stack_growth_tracks_smaps_dirty_page(void) {
    struct task *task = task_current();
    unsigned char image[4096];
    char byte = 'd';
    char smaps[16384];
    char stack_range[32];
    ssize_t smaps_len = 0;
    uint64_t old_base;
    uint64_t old_size;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    unlink_impl("/tmp/exec-elf-stack-smaps");
    build_exec_elf64_without_interp(image, sizeof(image), 0x401000, 0x400000);
    if (create_exec_bytes("/tmp/exec-elf-stack-smaps", image, sizeof(image)) != 0) {
        goto out;
    }
    if (execve("/tmp/exec-elf-stack-smaps", NULL, NULL) != 0) {
        goto out;
    }
    task->rlimits[RLIMIT_STACK].cur = 16ULL * 1024ULL * 1024ULL;

    old_base = task->mm->initial_stack_base;
    old_size = task->mm->initial_stack_image_size;
    if (task_write_virtual_memory_impl(task, old_base - 1, &byte, 1) != 1 ||
        task->mm->initial_stack_base != old_base - TASK_VMA_PAGE_SIZE ||
        task->mm->initial_stack_image_size != old_size + TASK_VMA_PAGE_SIZE) {
        errno = EPROTO;
        goto out;
    }

    if (format_maps_range(stack_range, sizeof(stack_range), task->mm->initial_stack_base,
                          task->mm->initial_stack_base + task->mm->initial_stack_image_size) != 0 ||
        read_proc_file("/proc/self/smaps", smaps, sizeof(smaps) - 1, &smaps_len) != 0) {
        goto out;
    }
    smaps[smaps_len] = '\0';
    if (!smaps_block_contains(smaps, stack_range, "[stack]")) {
        errno = ENOENT;
        goto out;
    }
    if (!smaps_block_contains(smaps, stack_range, "Private_Dirty:         4 kB")) {
        errno = ENODATA;
        goto out;
    }
    if (!smaps_block_contains(smaps, stack_range, "Anonymous:")) {
        errno = ENOMSG;
        goto out;
    }

    result = 0;

out:
    unlink_impl("/tmp/exec-elf-stack-smaps");
    return result;
}

int exec_syscall_contract_elf_dynamic_metadata_records_exec_and_loader(void) {
    struct task *task = task_current();
    const char interp_path[] = "/tmp/exec-elf-dynamic-loader";
    unsigned char image[sizeof(Elf64_Ehdr) + (3 * sizeof(Elf64_Phdr)) + 128];
    unsigned char loader[sizeof(Elf64_Ehdr) + (2 * sizeof(Elf64_Phdr)) + 96];
    uint64_t exec_dynamic_vaddr = 0x400040;
    uint64_t loader_dynamic_vaddr = 0x700030;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    unlink_impl("/tmp/exec-elf-dynamic-meta");
    unlink_impl(interp_path);
    build_exec_elf64_with_interp_and_dynamic(image, sizeof(image), interp_path,
                                             0x409000, 0x400000, exec_dynamic_vaddr);
    build_exec_elf64_with_dynamic(loader, sizeof(loader), 0x709000, 0x700000, loader_dynamic_vaddr);
    if (create_exec_bytes("/tmp/exec-elf-dynamic-meta", image, sizeof(image)) != 0 ||
        create_exec_bytes(interp_path, loader, sizeof(loader)) != 0) {
        goto out;
    }

    if (execve("/tmp/exec-elf-dynamic-meta", NULL, NULL) != 0) {
        errno = EPROTO;
        goto out;
    }

    if (task->mm->exec_dynamic_vaddr != exec_dynamic_vaddr ||
        task->mm->exec_dynamic_size != 16 ||
        task->mm->interp_dynamic_vaddr != loader_dynamic_vaddr ||
        task->mm->interp_dynamic_size != 16) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    unlink_impl("/tmp/exec-elf-dynamic-meta");
    unlink_impl(interp_path);
    return result;
}

int exec_syscall_contract_elf_exec_handoff_exposes_entry_stack_and_memory_access(void) {
    struct task *task = task_current();
    const char interp_path[] = "/tmp/exec-elf-handoff-loader";
    unsigned char image[sizeof(Elf64_Ehdr) + (2 * sizeof(Elf64_Phdr)) + 96];
    unsigned char loader[sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr) + 64];
    Elf64_Ehdr *loader_ehdr = (Elf64_Ehdr *)loader;
    Elf64_Phdr *loader_load;
    const struct task_exec_handoff *handoff;
    unsigned char bytes[2];
    const unsigned char patch[] = {0x44, 0x55};
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    unlink_impl("/tmp/exec-elf-handoff");
    unlink_impl(interp_path);
    build_exec_elf64_with_interp(image, sizeof(image), interp_path, 0x40a000, 0x400000);
    build_exec_elf64_writable_load(loader, sizeof(loader), 0x70a000, 0x700000);
    loader_load = (Elf64_Phdr *)(loader + loader_ehdr->e_phoff);
    if (create_exec_bytes("/tmp/exec-elf-handoff", image, sizeof(image)) != 0 ||
        create_exec_bytes(interp_path, loader, sizeof(loader)) != 0) {
        goto out;
    }

    if (execve("/tmp/exec-elf-handoff", NULL, NULL) != 0) {
        errno = EPROTO;
        goto out;
    }

    handoff = task_get_exec_handoff_impl(task);
    if (!handoff ||
        handoff->entry_point != loader_ehdr->e_entry ||
        handoff->initial_stack_pointer != task->mm->initial_stack_pointer ||
        !handoff->read_memory ||
        !handoff->write_memory ||
        handoff->read_memory(task, loader_load->p_vaddr, bytes, sizeof(bytes)) != (long)sizeof(bytes) ||
        memcmp(bytes, loader + loader_load->p_offset, sizeof(bytes)) != 0 ||
        handoff->write_memory(task, loader_load->p_vaddr, patch, sizeof(patch)) != (long)sizeof(patch) ||
        handoff->read_memory(task, loader_load->p_vaddr, bytes, sizeof(bytes)) != (long)sizeof(bytes) ||
        memcmp(bytes, patch, sizeof(patch)) != 0) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    unlink_impl("/tmp/exec-elf-handoff");
    unlink_impl(interp_path);
    return result;
}

int exec_syscall_contract_elf_vma_page_permissions_are_page_granular(void) {
    struct task *task = task_current();
    unsigned char image[sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr) + 128];
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)image;
    Elf64_Phdr *load;
    const unsigned char first_page_patch[] = {0x01, 0x02};
    const unsigned char crossing_patch[] = {0x03, 0x04};
    unsigned char check[2] = {0};
    uint64_t second_page;
    uint64_t crossing_addr;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    unlink_impl("/tmp/exec-elf-vma-page-policy");
    build_exec_elf64_writable_load(image, sizeof(image), 0x40b000, 0x800000);
    load = (Elf64_Phdr *)(image + ehdr->e_phoff);
    load->p_memsz = TASK_VMA_PAGE_SIZE * 2;
    if (create_exec_bytes("/tmp/exec-elf-vma-page-policy", image, sizeof(image)) != 0) {
        goto out;
    }

    if (execve("/tmp/exec-elf-vma-page-policy", NULL, NULL) != 0) {
        errno = EPROTO;
        goto out;
    }

    second_page = load->p_vaddr + TASK_VMA_PAGE_SIZE;
    crossing_addr = second_page - 1;
    if (task->mm->vma_count < 1 ||
        task->mm->vmas[0].page_count != 2 ||
        task_vma_page_flags_impl(&task->mm->vmas[0], load->p_vaddr) != (PF_R | PF_W) ||
        task_vma_page_flags_impl(&task->mm->vmas[0], second_page) != (PF_R | PF_W)) {
        errno = EPROTO;
        goto out;
    }

    if (task_set_vma_page_flags_impl(task, second_page, TASK_VMA_PAGE_SIZE, PF_R) != 0 ||
        task_vma_page_flags_impl(&task->mm->vmas[0], second_page) != PF_R) {
        errno = EPROTO;
        goto out;
    }

    if (task_write_virtual_memory_impl(task, load->p_vaddr, first_page_patch, sizeof(first_page_patch)) !=
        (long)sizeof(first_page_patch)) {
        errno = EPROTO;
        goto out;
    }

    errno = 0;
    if (task_write_virtual_memory_impl(task, second_page, first_page_patch, sizeof(first_page_patch)) != -1 ||
        expect_errno(EACCES) != 0) {
        goto out;
    }

    errno = 0;
    if (task_write_virtual_memory_impl(task, crossing_addr, crossing_patch, sizeof(crossing_patch)) != 1) {
        errno = EPROTO;
        goto out;
    }
    if (task_read_virtual_memory_impl(task, crossing_addr, check, sizeof(check)) != (long)sizeof(check) ||
        check[0] != crossing_patch[0] ||
        check[1] != 0) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    signal_clear_pending_task(task, SIGSEGV);
    unlink_impl("/tmp/exec-elf-vma-page-policy");
    return result;
}

int exec_syscall_contract_elf_dynamic_relocation_metadata_is_discovered(void) {
    struct task *task = task_current();
    const char interp_path[] = "/tmp/exec-elf-reloc-loader";
    unsigned char image[sizeof(Elf64_Ehdr) + (3 * sizeof(Elf64_Phdr)) + 384];
    unsigned char loader[sizeof(Elf64_Ehdr) + (2 * sizeof(Elf64_Phdr)) + 384];
    Elf64_Dyn exec_dyn[8];
    Elf64_Dyn loader_dyn[7];
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    memset(exec_dyn, 0, sizeof(exec_dyn));
    exec_dyn[0].d_tag = DT_NEEDED;
    exec_dyn[0].d_un.d_val = 7;
    exec_dyn[1].d_tag = DT_STRTAB;
    exec_dyn[1].d_un.d_ptr = 0x400120;
    exec_dyn[2].d_tag = DT_STRSZ;
    exec_dyn[2].d_un.d_val = 64;
    exec_dyn[3].d_tag = DT_SYMTAB;
    exec_dyn[3].d_un.d_ptr = 0x400160;
    exec_dyn[4].d_tag = DT_RELA;
    exec_dyn[4].d_un.d_ptr = 0x4001a0;
    exec_dyn[5].d_tag = DT_RELASZ;
    exec_dyn[5].d_un.d_val = 48;
    exec_dyn[6].d_tag = DT_RELAENT;
    exec_dyn[6].d_un.d_val = sizeof(Elf64_Rela);
    exec_dyn[7].d_tag = DT_NULL;

    memset(loader_dyn, 0, sizeof(loader_dyn));
    loader_dyn[0].d_tag = DT_STRTAB;
    loader_dyn[0].d_un.d_ptr = 0x700120;
    loader_dyn[1].d_tag = DT_SYMTAB;
    loader_dyn[1].d_un.d_ptr = 0x700160;
    loader_dyn[2].d_tag = DT_JMPREL;
    loader_dyn[2].d_un.d_ptr = 0x7001a0;
    loader_dyn[3].d_tag = DT_PLTRELSZ;
    loader_dyn[3].d_un.d_val = 24;
    loader_dyn[4].d_tag = DT_PLTREL;
    loader_dyn[4].d_un.d_val = DT_RELA;
    loader_dyn[5].d_tag = DT_RELAENT;
    loader_dyn[5].d_un.d_val = sizeof(Elf64_Rela);
    loader_dyn[6].d_tag = DT_NULL;

    unlink_impl("/tmp/exec-elf-reloc-main");
    unlink_impl(interp_path);
    build_exec_elf64_with_interp_and_dynamic_entries(image, sizeof(image), interp_path,
                                                     0x40c000, 0x400000, 0x400040,
                                                     exec_dyn, sizeof(exec_dyn) / sizeof(exec_dyn[0]));
    build_exec_elf64_with_dynamic_entries(loader, sizeof(loader), 0x70c000, 0x700000, 0x700040,
                                          loader_dyn, sizeof(loader_dyn) / sizeof(loader_dyn[0]));
    if (create_exec_bytes("/tmp/exec-elf-reloc-main", image, sizeof(image)) != 0 ||
        create_exec_bytes(interp_path, loader, sizeof(loader)) != 0) {
        goto out;
    }

    if (execve("/tmp/exec-elf-reloc-main", NULL, NULL) != 0) {
        errno = EPROTO;
        goto out;
    }

    if (task->mm->exec_dynamic.vaddr != 0x400040 ||
        task->mm->exec_dynamic.size != sizeof(exec_dyn) ||
        task->mm->exec_dynamic.needed_count != 1 ||
        task->mm->exec_dynamic.needed_offsets[0] != 7 ||
        task->mm->exec_dynamic.strtab_vaddr != 0x400120 ||
        task->mm->exec_dynamic.strtab_size != 64 ||
        task->mm->exec_dynamic.symtab_vaddr != 0x400160 ||
        task->mm->exec_dynamic.rela_vaddr != 0x4001a0 ||
        task->mm->exec_dynamic.rela_size != 48 ||
        task->mm->exec_dynamic.rela_entry_size != sizeof(Elf64_Rela) ||
        task->mm->interp_dynamic.vaddr != 0x700040 ||
        task->mm->interp_dynamic.size != sizeof(loader_dyn) ||
        task->mm->interp_dynamic.strtab_vaddr != 0x700120 ||
        task->mm->interp_dynamic.symtab_vaddr != 0x700160 ||
        task->mm->interp_dynamic.plt_rela_vaddr != 0x7001a0 ||
        task->mm->interp_dynamic.plt_rela_size != 24 ||
        task->mm->interp_dynamic.plt_rela_type != DT_RELA ||
        task->mm->interp_dynamic.rela_entry_size != sizeof(Elf64_Rela)) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    unlink_impl("/tmp/exec-elf-reloc-main");
    unlink_impl(interp_path);
    return result;
}

int exec_syscall_contract_aarch64_exec_context_uses_exec_handoff(void) {
    struct task *task = task_current();
    const char interp_path[] = "/tmp/exec-elf-aarch64-context-loader";
    unsigned char image[sizeof(Elf64_Ehdr) + (2 * sizeof(Elf64_Phdr)) + 96];
    unsigned char loader[sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr) + 64];
    Elf64_Ehdr *loader_ehdr = (Elf64_Ehdr *)loader;
    struct aarch64_exec_context context;
    uint64_t argc = 0;
    const unsigned char patch[] = {0x71, 0x72};
    unsigned char bytes[2] = {0};
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    unlink_impl("/tmp/exec-elf-aarch64-context");
    unlink_impl(interp_path);
    build_exec_elf64_with_interp(image, sizeof(image), interp_path, 0x40d000, 0x400000);
    build_exec_elf64_writable_load(loader, sizeof(loader), 0x70d000, 0x700000);
    if (create_exec_bytes("/tmp/exec-elf-aarch64-context", image, sizeof(image)) != 0 ||
        create_exec_bytes(interp_path, loader, sizeof(loader)) != 0) {
        goto out;
    }

    if (execve("/tmp/exec-elf-aarch64-context", NULL, NULL) != 0) {
        errno = EPROTO;
        goto out;
    }

    memset(&context, 0, sizeof(context));
    if (aarch64_exec_context_from_task(task, &context) != 0 ||
        context.task != task ||
        context.pc != loader_ehdr->e_entry ||
        context.sp != task->mm->initial_stack_pointer ||
        !context.read_memory ||
        !context.write_memory ||
        context.read_memory(task, context.sp, &argc, sizeof(argc)) != (long)sizeof(argc) ||
        argc != 0 ||
        context.write_memory(task, task->mm->interp_segments[0].vaddr, patch, sizeof(patch)) !=
            (long)sizeof(patch) ||
        context.read_memory(task, task->mm->interp_segments[0].vaddr, bytes, sizeof(bytes)) !=
            (long)sizeof(bytes) ||
        memcmp(bytes, patch, sizeof(patch)) != 0) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    unlink_impl("/tmp/exec-elf-aarch64-context");
    unlink_impl(interp_path);
    return result;
}

int exec_syscall_contract_aarch64_relocations_apply_relative_globdat_and_jumpslot(void) {
    struct task *task = task_current();
    unsigned char image[sizeof(Elf64_Ehdr) + (2 * sizeof(Elf64_Phdr)) + 768];
    Elf64_Dyn dyn[8];
    Elf64_Rela rela[3];
    Elf64_Sym sym[2];
    uint64_t relative_target = 0x400260;
    uint64_t globdat_target = 0x400268;
    uint64_t jumpslot_target = 0x400270;
    uint64_t rela_vaddr = 0x400120;
    uint64_t symtab_vaddr = 0x4001c0;
    uint64_t value = 0;
    uint32_t applied = 0;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    memset(dyn, 0, sizeof(dyn));
    dyn[0].d_tag = DT_RELA;
    dyn[0].d_un.d_ptr = rela_vaddr;
    dyn[1].d_tag = DT_RELASZ;
    dyn[1].d_un.d_val = 2 * sizeof(Elf64_Rela);
    dyn[2].d_tag = DT_RELAENT;
    dyn[2].d_un.d_val = sizeof(Elf64_Rela);
    dyn[3].d_tag = DT_SYMTAB;
    dyn[3].d_un.d_ptr = symtab_vaddr;
    dyn[4].d_tag = DT_JMPREL;
    dyn[4].d_un.d_ptr = rela_vaddr + (2 * sizeof(Elf64_Rela));
    dyn[5].d_tag = DT_PLTRELSZ;
    dyn[5].d_un.d_val = sizeof(Elf64_Rela);
    dyn[6].d_tag = DT_PLTREL;
    dyn[6].d_un.d_val = DT_RELA;
    dyn[7].d_tag = DT_NULL;

    memset(rela, 0, sizeof(rela));
    rela[0].r_offset = relative_target;
    rela[0].r_info = make_r_info(0, R_AARCH64_RELATIVE);
    rela[0].r_addend = 0x88;
    rela[1].r_offset = globdat_target;
    rela[1].r_info = make_r_info(1, R_AARCH64_GLOB_DAT);
    rela[1].r_addend = 0x10;
    rela[2].r_offset = jumpslot_target;
    rela[2].r_info = make_r_info(1, R_AARCH64_JUMP_SLOT);
    rela[2].r_addend = 0x20;

    memset(sym, 0, sizeof(sym));
    sym[1].st_value = 0x400300;

    unlink_impl("/tmp/exec-elf-reloc-apply");
    build_exec_elf64_with_dynamic_entries(image, sizeof(image), 0x40e000, 0x400000, 0x400040,
                                          dyn, sizeof(dyn) / sizeof(dyn[0]));
    memcpy(image + (size_t)(rela_vaddr - 0x400000) + sizeof(Elf64_Ehdr) + (2 * sizeof(Elf64_Phdr)),
           rela, sizeof(rela));
    memcpy(image + (size_t)(symtab_vaddr - 0x400000) + sizeof(Elf64_Ehdr) + (2 * sizeof(Elf64_Phdr)),
           sym, sizeof(sym));
    if (create_exec_bytes("/tmp/exec-elf-reloc-apply", image, sizeof(image)) != 0) {
        goto out;
    }

    if (execve("/tmp/exec-elf-reloc-apply", NULL, NULL) != 0) {
        errno = EPROTO;
        goto out;
    }

    if (aarch64_apply_dynamic_relocations(task, &task->mm->exec_dynamic, 0x400000, &applied) != 0 ||
        applied != 3) {
        errno = EPROTO;
        goto out;
    }
    if (task_read_virtual_memory_impl(task, relative_target, &value, sizeof(value)) != (long)sizeof(value) ||
        value != 0x400088) {
        errno = EPROTO;
        goto out;
    }
    if (task_read_virtual_memory_impl(task, globdat_target, &value, sizeof(value)) != (long)sizeof(value) ||
        value != 0x400310) {
        errno = EPROTO;
        goto out;
    }
    if (task_read_virtual_memory_impl(task, jumpslot_target, &value, sizeof(value)) != (long)sizeof(value) ||
        value != 0x400320) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    unlink_impl("/tmp/exec-elf-reloc-apply");
    return result;
}

int exec_syscall_contract_mmap_mprotect_and_munmap_update_vmas(void) {
    struct task *task = task_current();
    void *addr;
    uint64_t vaddr;
    const unsigned char patch[] = {0xa1, 0xa2};
    unsigned char bytes[2] = {0};
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    addr = mmap_impl(NULL, TASK_VMA_PAGE_SIZE * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (addr == (void *)-1) {
        return -1;
    }
    vaddr = (uint64_t)(uintptr_t)addr;
    if ((vaddr % TASK_VMA_PAGE_SIZE) != 0 ||
        !task_find_vma_impl(task, vaddr) ||
        task_find_vma_impl(task, vaddr)->kind != TASK_VMA_ANON ||
        task_write_virtual_memory_impl(task, vaddr, patch, sizeof(patch)) != (long)sizeof(patch) ||
        task_read_virtual_memory_impl(task, vaddr, bytes, sizeof(bytes)) != (long)sizeof(bytes) ||
        memcmp(bytes, patch, sizeof(patch)) != 0) {
        errno = EPROTO;
        goto out;
    }

    if (mprotect_impl(addr, TASK_VMA_PAGE_SIZE, PROT_READ) != 0) {
        goto out;
    }
    errno = 0;
    if (task_write_virtual_memory_impl(task, vaddr, patch, sizeof(patch)) != -1 ||
        expect_errno(EACCES) != 0) {
        goto out;
    }
    if (mprotect_impl(addr, TASK_VMA_PAGE_SIZE, PROT_READ | PROT_WRITE) != 0 ||
        task_write_virtual_memory_impl(task, vaddr, patch, sizeof(patch)) != (long)sizeof(patch)) {
        errno = EPROTO;
        goto out;
    }
    if (munmap_impl(addr, TASK_VMA_PAGE_SIZE * 2) != 0 ||
        task_find_vma_impl(task, vaddr) != NULL) {
        errno = EPROTO;
        goto out;
    }
    errno = 0;
    if (task_read_virtual_memory_impl(task, vaddr, bytes, sizeof(bytes)) != -1 ||
        expect_errno(EFAULT) != 0) {
        goto out;
    }

    result = 0;
    signal_clear_pending_task(task, SIGSEGV);
    return result;

out:
    signal_clear_pending_task(task, SIGSEGV);
    munmap_impl(addr, TASK_VMA_PAGE_SIZE * 2);
    return result;
}

int exec_syscall_contract_mmap_private_file_write_marks_private_dirty_and_preserves_file(void) {
    struct task *task = task_current();
    const char *path = "/tmp/exec-mm-private-cow-file";
    unsigned char page[TASK_VMA_PAGE_SIZE];
    unsigned char file_bytes[4] = {0};
    unsigned char patch[] = {'W', 'X', 'Y', 'Z'};
    char smaps[8192];
    ssize_t smaps_len = 0;
    void *addr = (void *)-1;
    int fd = -1;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    memset(page, 'a', sizeof(page));
    page[0] = 'a';
    page[1] = 'b';
    page[2] = 'c';
    page[3] = 'd';

    unlink_impl(path);
    fd = open_impl(path, O_CREAT | O_RDWR | O_TRUNC, 0644);
    if (fd < 0 || write_impl(fd, page, sizeof(page)) != (long)sizeof(page)) {
        goto out;
    }

    addr = mmap_impl(NULL, sizeof(page), PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    if (addr == (void *)-1) {
        goto out;
    }
    if (task_write_virtual_memory_impl(task, (uint64_t)(uintptr_t)addr, patch, sizeof(patch)) !=
        (long)sizeof(patch)) {
        goto out;
    }
    if (pread_impl(fd, file_bytes, sizeof(file_bytes), 0) != (ssize_t)sizeof(file_bytes) ||
        memcmp(file_bytes, "abcd", sizeof(file_bytes)) != 0) {
        errno = EPROTO;
        goto out;
    }
    if (read_proc_file("/proc/self/smaps", smaps, sizeof(smaps) - 1, &smaps_len) != 0) {
        goto out;
    }
    smaps[smaps_len] = '\0';
    if (!strstr(smaps, path)) {
        errno = ENOENT;
        goto out;
    }
    if (!strstr(smaps, "Private_Dirty:         4 kB")) {
        errno = ENODATA;
        goto out;
    }
    if (!strstr(smaps, "VmFlags:") ||
        !strstr(smaps, "wr")) {
        errno = ENOMSG;
        goto out;
    }

    result = 0;

out:
    if (addr != (void *)-1) {
        munmap_impl(addr, sizeof(page));
    }
    close_if_open(fd);
    unlink_impl(path);
    return result;
}

int exec_syscall_contract_shared_file_truncate_faults_with_sigbus_bus_adrerr(void) {
    struct task *task = task_current();
    const char *path = "/tmp/exec-mm-shared-truncate-file";
    unsigned char page[TASK_VMA_PAGE_SIZE];
    unsigned char byte = 0;
    void *addr = (void *)-1;
    int fd = -1;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }
    memset(page, 's', sizeof(page));

    unlink_impl(path);
    fd = open_impl(path, O_CREAT | O_RDWR | O_TRUNC, 0644);
    if (fd < 0 ||
        write_impl(fd, page, sizeof(page)) != (long)sizeof(page) ||
        write_impl(fd, page, sizeof(page)) != (long)sizeof(page)) {
        goto out;
    }
    addr = mmap_impl(NULL, sizeof(page) * 2, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (addr == (void *)-1) {
        goto out;
    }
    if (ftruncate_impl(fd, 0) != 0) {
        goto out;
    }
    errno = 0;
    if (task_read_virtual_memory_impl(task, (uint64_t)(uintptr_t)addr, &byte, 1) != -1 ||
        errno != EFAULT ||
        !signal_is_pending(task, SIGBUS) ||
        task->last_fault_signal != SIGBUS ||
        task->last_fault_code != BUS_ADRERR ||
        task->last_fault_addr != (uint64_t)(uintptr_t)addr) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    signal_clear_pending_task(task, SIGBUS);
    if (addr != (void *)-1) {
        munmap_impl(addr, sizeof(page) * 2);
    }
    close_if_open(fd);
    unlink_impl(path);
    return result;
}

int exec_syscall_contract_aarch64_exec_context_runs_nop_until_brk(void) {
    struct task *task = task_current();
    unsigned char image[sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr) + 64];
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)image;
    Elf64_Phdr *load;
    struct aarch64_exec_context context;
    uint32_t program[] = {0xd503201fU, 0xd503201fU, 0xd4200000U};
    uint64_t steps = 0;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    unlink_impl("/tmp/exec-elf-aarch64-run");
    build_exec_elf64_writable_load(image, sizeof(image), 0x400000, 0x400000);
    load = (Elf64_Phdr *)(image + ehdr->e_phoff);
    memcpy(image + load->p_offset, program, sizeof(program));
    if (create_exec_bytes("/tmp/exec-elf-aarch64-run", image, sizeof(image)) != 0) {
        goto out;
    }

    if (execve("/tmp/exec-elf-aarch64-run", NULL, NULL) != 0) {
        errno = EPROTO;
        goto out;
    }
    if (aarch64_exec_context_from_task(task, &context) != 0 ||
        aarch64_exec_context_run(&context, 8, &steps) != 0 ||
        steps != 3 ||
        context.steps != 3 ||
        context.stopped != 1 ||
        context.pc != 0x400008) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    unlink_impl("/tmp/exec-elf-aarch64-run");
    return result;
}

int exec_syscall_contract_elf_missing_interp_returns_enoent_without_transition(void) {
    struct task *task = task_current();
    const char interp_path[] = "/tmp/exec-missing-loader";
    unsigned char image[sizeof(Elf64_Ehdr) + (2 * sizeof(Elf64_Phdr)) + 96];
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    unlink_impl("/tmp/exec-elf-missing-loader");
    unlink_impl(interp_path);
    build_exec_elf64_with_interp(image, sizeof(image), interp_path, 0x401000, 0x400000);
    if (create_exec_bytes("/tmp/exec-elf-missing-loader", image, sizeof(image)) != 0) {
        goto out;
    }

    memcpy(task->exe, "/before", 8);
    memset(task->comm, 0, sizeof(task->comm));
    memcpy(task->comm, "before", 7);
    task_reset_execed(task);

    errno = 0;
    if (execve("/tmp/exec-elf-missing-loader", NULL, NULL) != -1 ||
        expect_errno(ENOENT) != 0 ||
        verify_state_unchanged(task, "/before", "before", false, -1, -1) != 0) {
        goto out;
    }

    result = 0;

out:
    unlink_impl("/tmp/exec-elf-missing-loader");
    unlink_impl(interp_path);
    return result;
}

int exec_syscall_contract_elf_invalid_interp_returns_enoexec_without_transition(void) {
    struct task *task = task_current();
    const char interp_path[] = "/tmp/exec-invalid-loader";
    unsigned char image[sizeof(Elf64_Ehdr) + (2 * sizeof(Elf64_Phdr)) + 96];
    const char invalid_loader[] = "not an elf";
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    unlink_impl("/tmp/exec-elf-invalid-loader");
    unlink_impl(interp_path);
    build_exec_elf64_with_interp(image, sizeof(image), interp_path, 0x401000, 0x400000);
    if (create_exec_bytes("/tmp/exec-elf-invalid-loader", image, sizeof(image)) != 0 ||
        create_exec_bytes(interp_path, invalid_loader, sizeof(invalid_loader)) != 0) {
        goto out;
    }

    memcpy(task->exe, "/before", 8);
    memset(task->comm, 0, sizeof(task->comm));
    memcpy(task->comm, "before", 7);
    task_reset_execed(task);

    errno = 0;
    if (execve("/tmp/exec-elf-invalid-loader", NULL, NULL) != -1 ||
        expect_errno(ENOEXEC) != 0 ||
        verify_state_unchanged(task, "/before", "before", false, -1, -1) != 0) {
        goto out;
    }

    result = 0;

out:
    unlink_impl("/tmp/exec-elf-invalid-loader");
    unlink_impl(interp_path);
    return result;
}

int exec_syscall_contract_elf_dyn_image_is_accepted(void) {
    struct task *task = task_current();
    unsigned char image[sizeof(Elf64_Ehdr) + 8];
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)image;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    unlink_impl("/tmp/exec-elf-dyn");
    memset(image, 0, sizeof(image));
    build_minimal_elf64_aarch64(ehdr);
    ehdr->e_type = ET_DYN;
    if (create_exec_bytes("/tmp/exec-elf-dyn", image, sizeof(image)) != 0) {
        goto out;
    }

    if (execve("/tmp/exec-elf-dyn", NULL, NULL) != 0) {
        errno = EPROTO;
        goto out;
    }
    if (!task->exec_image ||
        task->exec_image->type != EXEC_IMAGE_ELF ||
        task->exec_image->u.elf.type != ET_DYN ||
        strcmp(task->exec_image->path, "/tmp/exec-elf-dyn") != 0) {
        errno = EPROTO;
        goto out;
    }

    result = 0;

out:
    unlink_impl("/tmp/exec-elf-dyn");
    return result;
}

int exec_syscall_contract_elf_interp_without_nul_returns_enoexec_without_transition(void) {
    struct task *task = task_current();
    unsigned char image[sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr) + 8];
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)image;
    Elf64_Phdr *interp;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    unlink_impl("/tmp/exec-elf-bad-interp");
    memset(image, 0, sizeof(image));
    build_minimal_elf64_aarch64(ehdr);
    ehdr->e_phoff = sizeof(Elf64_Ehdr);
    ehdr->e_phentsize = sizeof(Elf64_Phdr);
    ehdr->e_phnum = 1;
    interp = (Elf64_Phdr *)(image + ehdr->e_phoff);
    interp->p_type = PT_INTERP;
    interp->p_offset = sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr);
    interp->p_filesz = 4;
    interp->p_memsz = 4;
    memcpy(image + interp->p_offset, "/bad", 4);
    if (create_exec_bytes("/tmp/exec-elf-bad-interp", image, sizeof(image)) != 0) {
        goto out;
    }

    memcpy(task->exe, "/before", 8);
    memset(task->comm, 0, sizeof(task->comm));
    memcpy(task->comm, "before", 7);
    task_reset_execed(task);

    errno = 0;
    if (execve("/tmp/exec-elf-bad-interp", NULL, NULL) != -1 ||
        expect_errno(ENOEXEC) != 0 ||
        verify_state_unchanged(task, "/before", "before", false, -1, -1) != 0) {
        goto out;
    }

    result = 0;

out:
    unlink_impl("/tmp/exec-elf-bad-interp");
    return result;
}

int exec_syscall_contract_elf_too_many_load_segments_returns_enoexec_without_transition(void) {
    struct task *task = task_current();
    unsigned char image[sizeof(Elf64_Ehdr) + ((TASK_EXEC_MAX_LOAD_SEGMENTS + 1) * sizeof(Elf64_Phdr)) + 8];
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)image;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    unlink_impl("/tmp/exec-elf-too-many-loads");
    memset(image, 0, sizeof(image));
    build_minimal_elf64_aarch64(ehdr);
    ehdr->e_phoff = sizeof(Elf64_Ehdr);
    ehdr->e_phentsize = sizeof(Elf64_Phdr);
    ehdr->e_phnum = TASK_EXEC_MAX_LOAD_SEGMENTS + 1;
    for (int i = 0; i < TASK_EXEC_MAX_LOAD_SEGMENTS + 1; i++) {
        Elf64_Phdr *load = ((Elf64_Phdr *)(image + ehdr->e_phoff)) + i;
        load->p_type = PT_LOAD;
        load->p_offset = sizeof(image) - 8;
        load->p_filesz = 1;
        load->p_memsz = 1;
        load->p_vaddr = 0x400000 + ((uint64_t)i * 0x1000);
        load->p_flags = PF_R;
    }
    if (create_exec_bytes("/tmp/exec-elf-too-many-loads", image, sizeof(image)) != 0) {
        goto out;
    }

    memcpy(task->exe, "/before", 8);
    memset(task->comm, 0, sizeof(task->comm));
    memcpy(task->comm, "before", 7);
    task_reset_execed(task);

    errno = 0;
    if (execve("/tmp/exec-elf-too-many-loads", NULL, NULL) != -1 ||
        expect_errno(ENOEXEC) != 0 ||
        verify_state_unchanged(task, "/before", "before", false, -1, -1) != 0) {
        goto out;
    }

    result = 0;

out:
    unlink_impl("/tmp/exec-elf-too-many-loads");
    return result;
}

int exec_syscall_contract_elf_bad_load_segment_returns_enoexec_without_transition(void) {
    struct task *task = task_current();
    unsigned char image[sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr) + 8];
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)image;
    Elf64_Phdr *load;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    native_registry_clear();
    unlink_impl("/tmp/exec-elf-bad-segment");
    memset(image, 0, sizeof(image));
    build_minimal_elf64_aarch64(ehdr);
    ehdr->e_phoff = sizeof(Elf64_Ehdr);
    ehdr->e_phentsize = sizeof(Elf64_Phdr);
    ehdr->e_phnum = 1;
    load = (Elf64_Phdr *)(image + ehdr->e_phoff);
    load->p_type = PT_LOAD;
    load->p_offset = sizeof(image) - 4;
    load->p_filesz = 8;
    load->p_memsz = 8;
    load->p_flags = PF_R;
    if (create_exec_bytes("/tmp/exec-elf-bad-segment", image, sizeof(image)) != 0) {
        goto out;
    }

    memcpy(task->exe, "/before", 8);
    memset(task->comm, 0, sizeof(task->comm));
    memcpy(task->comm, "before", 7);
    task_reset_execed(task);

    errno = 0;
    if (execve("/tmp/exec-elf-bad-segment", NULL, NULL) != -1 ||
        expect_errno(ENOEXEC) != 0 ||
        verify_state_unchanged(task, "/before", "before", false, -1, -1) != 0) {
        goto out;
    }

    result = 0;

out:
    unlink_impl("/tmp/exec-elf-bad-segment");
    return result;
}

int exec_syscall_contract_elf_wrong_machine_returns_enoexec_without_transition(void) {
    struct task *task = task_current();
    Elf64_Ehdr ehdr;
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    native_registry_clear();
    unlink_impl("/tmp/exec-elf-wrong-machine");
    build_minimal_elf64_aarch64(&ehdr);
    ehdr.e_machine = EM_386;
    if (create_exec_bytes("/tmp/exec-elf-wrong-machine", &ehdr, sizeof(ehdr)) != 0) {
        goto out;
    }

    memcpy(task->exe, "/before", 8);
    memset(task->comm, 0, sizeof(task->comm));
    memcpy(task->comm, "before", 7);
    task_reset_execed(task);

    errno = 0;
    if (execve("/tmp/exec-elf-wrong-machine", NULL, NULL) != -1 ||
        expect_errno(ENOEXEC) != 0 ||
        verify_state_unchanged(task, "/before", "before", false, -1, -1) != 0) {
        goto out;
    }

    result = 0;

out:
    unlink_impl("/tmp/exec-elf-wrong-machine");
    return result;
}

int exec_syscall_contract_truncated_elf_returns_enoexec_without_transition(void) {
    struct task *task = task_current();
    unsigned char magic[4] = {ELFMAG0, ELFMAG1, ELFMAG2, ELFMAG3};
    int result = -1;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    native_registry_clear();
    unlink_impl("/tmp/exec-elf-truncated");
    if (create_exec_bytes("/tmp/exec-elf-truncated", magic, sizeof(magic)) != 0) {
        goto out;
    }

    memcpy(task->exe, "/before", 8);
    memset(task->comm, 0, sizeof(task->comm));
    memcpy(task->comm, "before", 7);
    task_reset_execed(task);

    errno = 0;
    if (execve("/tmp/exec-elf-truncated", NULL, NULL) != -1 ||
        expect_errno(ENOEXEC) != 0 ||
        verify_state_unchanged(task, "/before", "before", false, -1, -1) != 0) {
        goto out;
    }

    result = 0;

out:
    unlink_impl("/tmp/exec-elf-truncated");
    return result;
}
