/* IXLandSystem/kernel/task.c
 * Virtual task/process subsystem implementation
 */

#include "task.h"
#include "signal.h"
#include "cgroup.h"
#include "cred_internal.h"
#include "mm.h"
#include "seccomp.h"
#include "uts.h"

#include "../fs/fdtable.h"
#include "../fs/vfs.h"

#include <errno.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <linux/fcntl.h>
#include <linux/elf.h>
#include <linux/mount.h>
#include <linux/sched.h>
#include <linux/stat.h>
#ifdef RLIM_NLIMITS
#undef RLIM_NLIMITS
#endif
#include <asm-generic/resource.h>

#ifdef SIGCHLD
#undef SIGCHLD
#endif
#ifdef SIGIOT
#undef SIGIOT
#endif
#ifdef SIGBUS
#undef SIGBUS
#endif
#ifdef SIGUSR1
#undef SIGUSR1
#endif
#ifdef SIGUSR2
#undef SIGUSR2
#endif
#ifdef SIGCONT
#undef SIGCONT
#endif
#ifdef SIGSTOP
#undef SIGSTOP
#endif
#ifdef SIGTSTP
#undef SIGTSTP
#endif
#ifdef SIGURG
#undef SIGURG
#endif
#ifdef SIGIO
#undef SIGIO
#endif
#ifdef SIGSYS
#undef SIGSYS
#endif
#define __ASSEMBLY__ 1
#include <asm-generic/signal.h>
#undef __ASSEMBLY__

#ifdef SEGV_MAPERR
#undef SEGV_MAPERR
#endif
#ifdef SEGV_ACCERR
#undef SEGV_ACCERR
#endif
#ifdef BUS_ADRERR
#undef BUS_ADRERR
#endif
enum {
    SEGV_MAPERR = 1,
    SEGV_ACCERR = 2,
    BUS_ADRERR = 2,
};

static struct mm_struct *task_ensure_mm_impl(struct task_struct *task) {
    if (!task) {
        errno = ESRCH;
        return NULL;
    }
    if (!task->mm) {
        task->mm = calloc(1, sizeof(*task->mm));
        if (!task->mm) {
            errno = ENOMEM;
            return NULL;
        }
        atomic_init(&task->mm->refs, 1);
    }
    return task->mm;
}

static __thread struct task_struct *current_task = NULL;
struct task_struct *init_task = NULL;
static atomic_ullong task_boot_time_ns = 0;

/* Task table - accessible to signal.c for killpg */
kernel_mutex_t task_table_lock = KERNEL_MUTEX_INITIALIZER;
struct task_struct *task_table[TASK_MAX_TASKS] = {NULL};

static uint64_t task_monotonic_time_ns(void) {
    struct timespec ts;

    if (kernel_clock_gettime(1, &ts) != 0) {
        return 0;
    }
    if (ts.tv_sec < 0 || ts.tv_nsec < 0) {
        return 0;
    }
    return ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
}

static uint64_t task_start_time_since_boot_ns(void) {
    uint64_t now = task_monotonic_time_ns();
    uint64_t boot = atomic_load(&task_boot_time_ns);

    if (boot == 0) {
        boot = now;
        atomic_store(&task_boot_time_ns, boot);
    }
    if (now <= boot) {
        return 1;
    }
    return now - boot;
}

int task_hash(int32_t pid) {
    return (int)(pid % TASK_MAX_TASKS);
}

struct task_struct *get_current(void) {
    return current_task;
}

void set_current(struct task_struct *task) {
    current_task = task;
    if (task && task->cred) {
        set_current_cred(task->cred);
    }
}

static void task_clear_exec_strings(struct task_struct *task) {
    if (!task) {
        return;
    }

    for (int i = 0; i < TASK_MAX_ARGS; i++) {
        free(task->argv[i]);
        task->argv[i] = NULL;
        free(task->envp[i]);
        task->envp[i] = NULL;
    }
    task->argc = 0;
    task->envc = 0;
}

static void task_free_exec_string_vector(char **strings, int count) {
    if (!strings) {
        return;
    }
    for (int i = 0; i < count; i++) {
        free(strings[i]);
    }
}

static int task_copy_exec_string_vector(char *const input[], char **output, int *out_count) {
    int count = 0;

    if (!output || !out_count) {
        errno = EINVAL;
        return -1;
    }

    *out_count = 0;
    if (!input) {
        return 0;
    }

    while (input[count]) {
        if (count >= TASK_MAX_ARGS - 1) {
            errno = E2BIG;
            return -1;
        }
        output[count] = strdup(input[count]);
        if (!output[count]) {
            task_free_exec_string_vector(output, count);
            errno = ENOMEM;
            return -1;
        }
        count++;
    }

    output[count] = NULL;
    *out_count = count;
    return 0;
}

int task_record_exec_strings_impl(char *const argv[], char *const envp[]) {
    struct task_struct *task = get_current();
    char *new_argv[TASK_MAX_ARGS] = {0};
    char *new_envp[TASK_MAX_ARGS] = {0};
    int new_argc = 0;
    int new_envc = 0;

    if (!task) {
        errno = ESRCH;
        return -1;
    }

    if (task_copy_exec_string_vector(argv, new_argv, &new_argc) != 0) {
        return -1;
    }
    if (task_copy_exec_string_vector(envp, new_envp, &new_envc) != 0) {
        task_free_exec_string_vector(new_argv, new_argc);
        return -1;
    }

    task_clear_exec_strings(task);
    for (int i = 0; i < new_argc; i++) {
        task->argv[i] = new_argv[i];
    }
    for (int i = 0; i < new_envc; i++) {
        task->envp[i] = new_envp[i];
    }
    task->argc = new_argc;
    task->envc = new_envc;
    return 0;
}

const struct task_vma *task_find_vma_impl(struct task_struct *task, uint64_t addr) {
    if (!task || !task->mm) {
        return NULL;
    }
    for (uint32_t i = 0; i < task->mm->vma_count; i++) {
        if (addr >= task->mm->vmas[i].start && addr < task->mm->vmas[i].end) {
            return &task->mm->vmas[i];
        }
    }
    return NULL;
}

struct task_vma *task_find_vma_mutable_impl(struct task_struct *task, uint64_t addr) {
    if (!task || !task->mm) {
        return NULL;
    }
    for (uint32_t i = 0; i < task->mm->vma_count; i++) {
        if (addr >= task->mm->vmas[i].start && addr < task->mm->vmas[i].end) {
            return &task->mm->vmas[i];
        }
    }
    return NULL;
}

uint32_t task_vma_page_flags_impl(const struct task_vma *vma, uint64_t addr) {
    uint64_t page_index;

    if (!vma || addr < vma->start || addr >= vma->end) {
        return 0;
    }
    page_index = (addr - vma->start) / TASK_VMA_PAGE_SIZE;
    if (page_index >= vma->page_count) {
        return 0;
    }
    if (!vma->page_flags) {
        return vma->flags;
    }
    return vma->page_flags[page_index];
}

int task_set_vma_page_flags_impl(struct task_struct *task, uint64_t addr, uint64_t size, uint32_t flags) {
    const struct task_vma *found;
    uint64_t cursor;
    uint64_t end;

    if (!task || !task->mm || size == 0 || size > UINT64_MAX - addr) {
        errno = EFAULT;
        return -1;
    }

    end = addr + size;
    cursor = addr;
    while (cursor < end) {
        found = task_find_vma_impl(task, cursor);
        if (!found) {
            errno = ENOMEM;
            return -1;
        }
        cursor = found->end < end ? found->end : end;
    }

    cursor = addr;
    while (cursor < end) {
        struct task_vma *vma;
        uint64_t segment_end;
        uint64_t start_page;
        uint64_t end_page;

        found = task_find_vma_impl(task, cursor);
        vma = &task->mm->vmas[found - task->mm->vmas];
        if (!vma->page_flags || vma->page_count == 0) {
            errno = EFAULT;
            return -1;
        }

        segment_end = vma->end < end ? vma->end : end;
        start_page = (cursor - vma->start) / TASK_VMA_PAGE_SIZE;
        end_page = ((segment_end - 1) - vma->start) / TASK_VMA_PAGE_SIZE;
        if (end_page >= vma->page_count) {
            errno = EFAULT;
            return -1;
        }
        for (uint64_t i = start_page; i <= end_page; i++) {
            vma->page_flags[i] = flags;
        }
        cursor = segment_end;
    }
    return 0;
}

void task_rename_vma_backing_path_impl(const char *old_path, const char *new_path) {
    if (!old_path || !new_path) {
        return;
    }

    kernel_mutex_lock(&task_table_lock);
    for (int i = 0; i < TASK_MAX_TASKS; i++) {
        struct task_struct *task = task_table[i];
        while (task) {
            if (task->mm) {
                for (uint32_t v = 0; v < task->mm->vma_count; v++) {
                    struct task_vma *vma = &task->mm->vmas[v];
                    if (vma->backing_path[0] != '\0' &&
                        strcmp(vma->backing_path, old_path) == 0) {
                        strncpy(vma->backing_path, new_path, sizeof(vma->backing_path) - 1);
                        vma->backing_path[sizeof(vma->backing_path) - 1] = '\0';
                    }
                }
            }
            task = task->hash_next;
        }
    }
    kernel_mutex_unlock(&task_table_lock);
}

void task_exchange_vma_backing_paths_impl(const char *left_path, const char *right_path) {
    if (!left_path || !right_path) {
        return;
    }

    kernel_mutex_lock(&task_table_lock);
    for (int i = 0; i < TASK_MAX_TASKS; i++) {
        struct task_struct *task = task_table[i];
        while (task) {
            if (task->mm) {
                for (uint32_t v = 0; v < task->mm->vma_count; v++) {
                    struct task_vma *vma = &task->mm->vmas[v];
                    if (vma->backing_path[0] == '\0') {
                        continue;
                    }
                    if (strcmp(vma->backing_path, left_path) == 0) {
                        strncpy(vma->backing_path, right_path, sizeof(vma->backing_path) - 1);
                        vma->backing_path[sizeof(vma->backing_path) - 1] = '\0';
                    } else if (strcmp(vma->backing_path, right_path) == 0) {
                        strncpy(vma->backing_path, left_path, sizeof(vma->backing_path) - 1);
                        vma->backing_path[sizeof(vma->backing_path) - 1] = '\0';
                    }
                }
            }
            task = task->hash_next;
        }
    }
    kernel_mutex_unlock(&task_table_lock);
}

void task_note_memory_fault_impl(struct task_struct *task, uint64_t addr, int32_t code) {
    task_note_memory_signal_fault_impl(task, SIGSEGV, code, addr);
}

void task_note_memory_signal_fault_impl(struct task_struct *task, int32_t signo, int32_t code, uint64_t addr) {
    if (!task) {
        return;
    }
    task->last_fault_signal = signo;
    task->last_fault_code = code;
    task->last_fault_addr = addr;
    (void)signal_generate_task_info(task, signo, code, addr);
}

void task_clear_vmas_impl(struct mm_struct *mm) {
    if (!mm) {
        return;
    }
    for (uint32_t i = 0; i < mm->vma_count; i++) {
        if (mm->vmas[i].kind == TASK_VMA_ANON || mm->vmas[i].kind == TASK_VMA_FILE) {
            if (mm->vmas[i].shared_pages) {
                for (uint64_t page = 0; page < mm->vmas[i].page_count; page++) {
                    mm_shared_mapping_put_impl(mm->vmas[i].shared_pages[page]);
                }
                free(mm->vmas[i].shared_pages);
            } else if (mm->vmas[i].private_pages) {
                for (uint64_t page = 0; page < mm->vmas[i].page_count; page++) {
                    mm_private_page_put_impl(mm->vmas[i].private_pages[page]);
                }
                free(mm->vmas[i].private_pages);
            } else if (mm->vmas[i].shared_mapping) {
                mm_shared_mapping_put_impl(mm->vmas[i].shared_mapping);
            } else {
                free(mm->vmas[i].image);
            }
            mm->vmas[i].image = NULL;
            mm->vmas[i].shared_pages = NULL;
            mm->vmas[i].private_pages = NULL;
        }
        free(mm->vmas[i].page_flags);
        mm->vmas[i].page_flags = NULL;
        free(mm->vmas[i].resident_pages);
        mm->vmas[i].resident_pages = NULL;
        free(mm->vmas[i].dirty_pages);
        mm->vmas[i].dirty_pages = NULL;
        mm->vmas[i].page_count = 0;
    }
    memset(mm->vmas, 0, sizeof(mm->vmas));
    mm->vma_count = 0;
}

struct mm_struct *task_mm_get_impl(struct mm_struct *mm) {
    if (!mm) {
        return NULL;
    }
    if (atomic_load(&mm->refs) <= 0) {
        atomic_store(&mm->refs, 1);
    }
    atomic_fetch_add(&mm->refs, 1);
    return mm;
}

void task_mm_put_impl(struct mm_struct *mm) {
    if (!mm) {
        return;
    }
    if (atomic_load(&mm->refs) > 1 && atomic_fetch_sub(&mm->refs, 1) > 1) {
        return;
    }
    for (uint32_t i = 0; i < mm->exec_segment_count; i++) {
        free(mm->exec_segments[i].image);
    }
    for (uint32_t i = 0; i < mm->interp_segment_count; i++) {
        free(mm->interp_segments[i].image);
    }
    task_clear_vmas_impl(mm);
    free(mm->exec_image_base);
    free(mm->interp_image_base);
    free(mm->stack_guard_image);
    free(mm->initial_stack_image);
    free(mm);
}

static long task_read_vma(const struct task_vma *vma, uint64_t addr, void *buf, size_t count) {
    size_t offset;
    size_t available;
    uint64_t page_remaining;
    size_t to_copy;
    uint32_t flags;

    if (!vma || (!vma->image && !vma->shared_pages) || vma->image_size == 0 || addr < vma->start) {
        return 0;
    }
    if ((uint64_t)vma->image_size > UINT64_MAX - vma->start) {
        errno = EFAULT;
        return -1;
    }
    if (addr >= vma->end) {
        return 0;
    }
    flags = task_vma_page_flags_impl(vma, addr);
    if ((flags & PF_R) == 0) {
        errno = EACCES;
        return -1;
    }
    if (vma->shared_pages) {
        return mm_shared_vma_read_impl(vma, addr, buf, count);
    }
    if (vma->kind == TASK_VMA_FILE && !vma->shared) {
        uint64_t page_index = (addr - vma->start) / TASK_VMA_PAGE_SIZE;
        size_t file_offset = (size_t)(addr - vma->start);
        long long file_size = mm_vma_file_size_impl(vma);
        if ((!vma->dirty_pages || page_index >= vma->page_count || vma->dirty_pages[page_index] == 0) &&
            file_size >= 0) {
            size_t page_start = file_offset - (file_offset % TASK_VMA_PAGE_SIZE);
            uint64_t backing_page_start = vma->backing_offset + (uint64_t)page_start;
            uint64_t backing_file_offset = vma->backing_offset + (uint64_t)file_offset;

            if (backing_page_start >= (uint64_t)file_size) {
                errno = ENXIO;
                return -1;
            }
            if (backing_file_offset >= (uint64_t)file_size) {
                size_t page_offset = file_offset % TASK_VMA_PAGE_SIZE;
                size_t to_zero = count;
                if (to_zero > TASK_VMA_PAGE_SIZE - page_offset) {
                    to_zero = TASK_VMA_PAGE_SIZE - page_offset;
                }
                memset(buf, 0, to_zero);
                return (long)to_zero;
            }
        }
    }
    if (vma->private_pages) {
        return mm_private_vma_read_impl(vma, addr, buf, count);
    }

    offset = (size_t)(addr - vma->start);
    available = vma->image_size - offset;
    page_remaining = TASK_VMA_PAGE_SIZE - ((addr - vma->start) % TASK_VMA_PAGE_SIZE);
    to_copy = count < available ? count : available;
    if ((uint64_t)to_copy > page_remaining) {
        to_copy = (size_t)page_remaining;
    }
    memcpy(buf, (const unsigned char *)vma->image + offset, to_copy);
    return (long)to_copy;
}

static long task_write_vma(struct task_vma *vma, uint64_t addr, const void *buf, size_t count) {
    size_t offset;
    size_t available;
    uint64_t page_remaining;
    size_t to_copy;
    uint32_t flags;

    if (!vma || (!vma->image && !vma->shared_pages) || vma->image_size == 0 || addr < vma->start) {
        return 0;
    }
    if ((uint64_t)vma->image_size > UINT64_MAX - vma->start) {
        errno = EFAULT;
        return -1;
    }
    if (addr >= vma->end) {
        return 0;
    }
    flags = task_vma_page_flags_impl(vma, addr);
    if ((flags & PF_W) == 0) {
        errno = EACCES;
        return -1;
    }
    if (vma->shared_pages) {
        return mm_shared_vma_write_impl(vma, addr, buf, count);
    }
    if (vma->kind == TASK_VMA_FILE && !vma->shared) {
        uint64_t page_index = (addr - vma->start) / TASK_VMA_PAGE_SIZE;
        size_t file_offset = (size_t)(addr - vma->start);
        long long file_size = mm_vma_file_size_impl(vma);
        size_t page_start = file_offset - (file_offset % TASK_VMA_PAGE_SIZE);
        if ((!vma->dirty_pages || page_index >= vma->page_count || vma->dirty_pages[page_index] == 0) &&
            file_size >= 0 &&
            vma->backing_offset + (uint64_t)page_start >= (uint64_t)file_size) {
            errno = ENXIO;
            return -1;
        }
    }
    if (vma->private_pages) {
        return mm_private_vma_write_impl(vma, addr, buf, count);
    }

    offset = (size_t)(addr - vma->start);
    available = vma->image_size - offset;
    page_remaining = TASK_VMA_PAGE_SIZE - ((addr - vma->start) % TASK_VMA_PAGE_SIZE);
    to_copy = count < available ? count : available;
    if ((uint64_t)to_copy > page_remaining) {
        to_copy = (size_t)page_remaining;
    }
    memcpy((unsigned char *)vma->image + offset, buf, to_copy);
    if (vma->dirty_pages) {
        uint64_t page_index = (addr - vma->start) / TASK_VMA_PAGE_SIZE;
        if (page_index < vma->page_count) {
            if (vma->resident_pages) {
                vma->resident_pages[page_index] = 1;
            }
            vma->dirty_pages[page_index] = 1;
        }
    }
    return (long)to_copy;
}

static int task_grow_stack_down_impl(struct task_struct *task, uint64_t fault_addr) {
    struct mm_struct *mm;
    struct task_vma *stack = NULL;
    struct task_vma *guard = NULL;
    void *new_image = NULL;
    uint32_t *new_page_flags = NULL;
    void *old_image;
    uint64_t old_size;
    uint64_t new_size;
    uint64_t new_base;
    uint64_t new_pages;

    if (!task || !task->mm) {
        errno = EFAULT;
        return -1;
    }
    mm = task->mm;
    for (uint32_t i = 0; i < mm->vma_count; i++) {
        if (mm->vmas[i].kind == TASK_VMA_STACK) {
            stack = &mm->vmas[i];
        } else if (mm->vmas[i].kind == TASK_VMA_GUARD &&
                   fault_addr >= mm->vmas[i].start && fault_addr < mm->vmas[i].end) {
            guard = &mm->vmas[i];
        }
    }
    if (!stack || !guard || guard->end != stack->start ||
        !stack->image || stack->image_size == 0) {
        return 0;
    }

    old_image = stack->image;
    old_size = stack->image_size;
    if (old_size > UINT64_MAX - TASK_VMA_PAGE_SIZE) {
        errno = ENOMEM;
        return -1;
    }
    new_size = old_size + TASK_VMA_PAGE_SIZE;
    if (new_size > task->rlimits[RLIMIT_STACK].cur) {
        errno = EACCES;
        return -1;
    }
    if (mm->initial_stack_base < TASK_VMA_PAGE_SIZE) {
        errno = ENOMEM;
        return -1;
    }
    new_base = mm->initial_stack_base - TASK_VMA_PAGE_SIZE;
    new_pages = new_size / TASK_VMA_PAGE_SIZE;
    if (new_pages == 0 || new_pages > SIZE_MAX / sizeof(*new_page_flags)) {
        errno = ENOMEM;
        return -1;
    }

    new_image = calloc(1, (size_t)new_size);
    new_page_flags = calloc((size_t)new_pages, sizeof(*new_page_flags));
    if (!new_image || !new_page_flags) {
        free(new_image);
        free(new_page_flags);
        errno = ENOMEM;
        return -1;
    }

    memcpy((unsigned char *)new_image + TASK_VMA_PAGE_SIZE, old_image, (size_t)old_size);
    for (uint64_t i = 0; i < new_pages; i++) {
        new_page_flags[i] = PF_R | PF_W;
    }

    free(old_image);
    mm->initial_stack_image = new_image;
    mm->initial_stack_image_size = (size_t)new_size;
    mm->initial_stack_base = new_base;
    mm->initial_stack_size = new_size;

    free(stack->page_flags);
    stack->start = new_base;
    stack->end = new_base + new_size;
    stack->image = mm->initial_stack_image;
    stack->image_size = mm->initial_stack_image_size;
    stack->page_count = new_pages;
    stack->page_flags = new_page_flags;
    stack->resident_pages = NULL;
    stack->dirty_pages = NULL;

    guard->start -= TASK_VMA_PAGE_SIZE;
    guard->end -= TASK_VMA_PAGE_SIZE;
    return 1;
}

static bool task_addr_is_below_stack_guard(const struct task_struct *task, uint64_t addr) {
    const struct mm_struct *mm;

    if (!task || !task->mm) {
        return false;
    }
    mm = task->mm;
    for (uint32_t i = 0; i < mm->vma_count; i++) {
        const struct task_vma *guard = &mm->vmas[i];

        if (guard->kind != TASK_VMA_GUARD || guard->start < TASK_VMA_PAGE_SIZE) {
            continue;
        }
        for (uint32_t j = 0; j < mm->vma_count; j++) {
            const struct task_vma *stack = &mm->vmas[j];

            if (stack->kind != TASK_VMA_STACK || guard->end != stack->start ||
                stack->start < (2ULL * TASK_VMA_PAGE_SIZE)) {
                continue;
            }
            if (addr >= stack->start - (2ULL * TASK_VMA_PAGE_SIZE) &&
                addr < guard->start) {
                return true;
            }
        }
    }
    return false;
}

static void task_propagate_shared_file_write(struct mm_struct *mm, struct task_vma *source,
                                             uint64_t addr, const void *buf, size_t count) {
    if (!mm || !source || source->shared_pages || source->kind != TASK_VMA_FILE || !source->shared ||
        source->backing_fd < 0 || count == 0) {
        return;
    }
    for (uint32_t i = 0; i < mm->vma_count; i++) {
        struct task_vma *vma = &mm->vmas[i];
        uint64_t source_file_start;
        uint64_t source_file_end;
        uint64_t target_file_start;
        uint64_t target_file_end;
        uint64_t overlap_start;
        uint64_t overlap_end;

        if (vma == source || vma->kind != TASK_VMA_FILE || !vma->shared ||
            vma->backing_fd != source->backing_fd || !vma->image) {
            continue;
        }
        source_file_start = source->backing_offset + (addr - source->start);
        source_file_end = source_file_start + count;
        target_file_start = vma->backing_offset;
        target_file_end = target_file_start + vma->image_size;
        if (source_file_end <= target_file_start || source_file_start >= target_file_end) {
            continue;
        }
        overlap_start = source_file_start > target_file_start ? source_file_start : target_file_start;
        overlap_end = source_file_end < target_file_end ? source_file_end : target_file_end;
        memcpy((unsigned char *)vma->image + (overlap_start - target_file_start),
               (const unsigned char *)buf + (overlap_start - source_file_start),
               (size_t)(overlap_end - overlap_start));
    }
}

long task_read_virtual_memory_impl(struct task_struct *task, uint64_t addr, void *buf, size_t count) {
    struct mm_struct *mm;
    long copied;

    if (!buf && count > 0) {
        errno = EFAULT;
        return -1;
    }
    if (count == 0) {
        return 0;
    }
    if (!task || !task->mm) {
        errno = EFAULT;
        return -1;
    }

    mm = task->mm;
    for (size_t total = 0; total < count;) {
        if ((uint64_t)total > UINT64_MAX - addr) {
            if (total > 0) {
                return (long)total;
            }
            errno = EFAULT;
            return -1;
        }
        if (task_addr_is_below_stack_guard(task, addr + total)) {
            task_note_memory_fault_impl(task, addr + total, SEGV_MAPERR);
            if (total > 0) {
                return (long)total;
            }
            errno = EFAULT;
            return -1;
        }
        copied = 0;
        for (uint32_t i = 0; i < mm->vma_count; i++) {
            copied = task_read_vma(&mm->vmas[i], addr + total, (unsigned char *)buf + total, count - total);
            if (copied != 0) {
                break;
            }
        }
        if (copied < 0) {
            if (errno == ENXIO) {
                errno = EFAULT;
                task_note_memory_signal_fault_impl(task, SIGBUS, BUS_ADRERR, addr + total);
            } else {
                task_note_memory_fault_impl(task, addr + total,
                                            errno == EACCES ? SEGV_ACCERR : SEGV_MAPERR);
            }
            return total > 0 ? (long)total : -1;
        }
        if (copied == 0) {
            task_note_memory_fault_impl(task, addr + total, SEGV_MAPERR);
            if (total > 0) {
                return (long)total;
            }
            errno = EFAULT;
            return -1;
        }
        total += (size_t)copied;
    }
    return (long)count;
}

long task_write_virtual_memory_impl(struct task_struct *task, uint64_t addr, const void *buf, size_t count) {
    struct mm_struct *mm;
    long copied;

    if (!buf && count > 0) {
        errno = EFAULT;
        return -1;
    }
    if (count == 0) {
        return 0;
    }
    if (!task || !task->mm) {
        errno = EFAULT;
        return -1;
    }

    mm = task->mm;
    for (size_t total = 0; total < count;) {
        if ((uint64_t)total > UINT64_MAX - addr) {
            if (total > 0) {
                return (long)total;
            }
            errno = EFAULT;
            return -1;
        }
        {
            int grow_ret = task_grow_stack_down_impl(task, addr + total);
            if (grow_ret > 0) {
                continue;
            }
        }
        if (task_addr_is_below_stack_guard(task, addr + total)) {
            task_note_memory_fault_impl(task, addr + total, SEGV_MAPERR);
            if (total > 0) {
                return (long)total;
            }
            errno = EFAULT;
            return -1;
        }
        copied = 0;
        for (uint32_t i = 0; i < mm->vma_count; i++) {
            struct task_vma *vma = &mm->vmas[i];
            copied = task_write_vma(vma, addr + total, (const unsigned char *)buf + total, count - total);
            if (copied > 0) {
                task_propagate_shared_file_write(mm, vma, addr + total,
                                                 (const unsigned char *)buf + total, (size_t)copied);
            }
            if (copied != 0) {
                break;
            }
        }
        if (copied < 0) {
            if (errno == ENXIO) {
                errno = EFAULT;
                task_note_memory_signal_fault_impl(task, SIGBUS, BUS_ADRERR, addr + total);
            } else {
                task_note_memory_fault_impl(task, addr + total,
                                            errno == EACCES ? SEGV_ACCERR : SEGV_MAPERR);
            }
            return total > 0 ? (long)total : -1;
        }
        if (copied == 0) {
            task_note_memory_fault_impl(task, addr + total, SEGV_MAPERR);
            if (total > 0) {
                return (long)total;
            }
            errno = EFAULT;
            return -1;
        }
        total += (size_t)copied;
    }
    return (long)count;
}

const struct task_exec_handoff *task_get_exec_handoff_impl(struct task_struct *task) {
    if (!task || !task->mm) {
        errno = EFAULT;
        return NULL;
    }
    return &task->mm->handoff;
}

void task_restart_clear_impl(struct task_struct *task) {
    if (!task || !task->mm) {
        return;
    }
    task->mm->signal_frame_restart_kind = TASK_RESTART_NONE;
    task->mm->signal_frame_restart_arg0 = 0;
    task->mm->signal_frame_restart_arg1 = 0;
    task->mm->signal_frame_restart_arg2 = 0;
    task->mm->signal_frame_restart_arg3 = 0;
    task->mm->signal_frame_restart_arg4 = 0;
    task->mm->signal_frame_restart_arg5 = 0;
}

int task_restart_record_impl(struct task_struct *task, enum task_restart_kind kind,
                             uint64_t arg0, uint64_t arg1, uint64_t arg2,
                             uint64_t arg3, uint64_t arg4, uint64_t arg5) {
    if (!task || kind == TASK_RESTART_NONE) {
        return -EINVAL;
    }
    if (!task_ensure_mm_impl(task)) {
        return -errno;
    }
    task->mm->signal_frame_restart_kind = (uint64_t)kind;
    task->mm->signal_frame_restart_arg0 = arg0;
    task->mm->signal_frame_restart_arg1 = arg1;
    task->mm->signal_frame_restart_arg2 = arg2;
    task->mm->signal_frame_restart_arg3 = arg3;
    task->mm->signal_frame_restart_arg4 = arg4;
    task->mm->signal_frame_restart_arg5 = arg5;
    return 0;
}

struct task_struct *alloc_task(void) {
    struct task_struct *task = calloc(1, sizeof(struct task_struct));
    if (!task)
        return NULL;

    task->pid = alloc_pid();
    task->tgid = task->pid;
    /* A new task starts without pgid/sid; fork_impl will inherit from parent */
    task->pgid = 0;
    task->sid = 0;
    task->ns_pid = task->pid;
    task->pid_ns_level = 0;
    task->cgroup_ns_id = 1;
    task->vfork_parent = NULL;
    for (int i = 0; i < 16; i++) {
        task->rlimits[i].cur = UINT64_MAX;
        task->rlimits[i].max = UINT64_MAX;
    }
    task->rlimits[RLIMIT_STACK].cur = 8ULL * 1024ULL * 1024ULL;
    task->rlimits[RLIMIT_STACK].max = 64ULL * 1024ULL * 1024ULL;
    task->rlimits[RLIMIT_NPROC].cur = TASK_MAX_TASKS;
    task->rlimits[RLIMIT_NPROC].max = TASK_MAX_TASKS;
    task->rlimits[RLIMIT_NOFILE].cur = NR_OPEN_DEFAULT;
    task->rlimits[RLIMIT_NOFILE].max = NR_OPEN_DEFAULT;

    atomic_init(&task->state, TASK_RUNNING);
    atomic_init(&task->refs, 1);
    atomic_init(&task->exited, false);
    atomic_init(&task->signaled, false);
    atomic_init(&task->stopped, false);
    atomic_init(&task->stopsig, 0);
    atomic_init(&task->continued, false);
    atomic_init(&task->stop_report_pending, false);
    atomic_init(&task->continue_report_pending, false);
    atomic_init(&task->execed, false);
    task->thread_pending_signals = 0;
    atomic_init(&task->new_pid_namespace_pending, false);
    task->clone_flags = 0;
    task->exec_secure = 0;
    task->exec_dumpable = 1;

    kernel_mutex_init(&task->lock);
    kernel_cond_init(&task->wait_cond);
    kernel_mutex_init(&task->wait_lock);
    task->uts_ns = uts_get_initial_namespace();
    {
        struct cgroup *root = cgroup_root();
        if (!root || task_attach_cgroup(task, root) != 0) {
            if (root) {
                cgroup_put(root);
            }
            kernel_cond_destroy(&task->wait_cond);
            kernel_mutex_destroy(&task->wait_lock);
            kernel_mutex_destroy(&task->lock);
            free_pid(task->pid);
            free(task);
            return NULL;
        }
        cgroup_put(root);
    }
    if (!task->cgroup) {
        kernel_cond_destroy(&task->wait_cond);
        kernel_mutex_destroy(&task->wait_lock);
        kernel_mutex_destroy(&task->lock);
        free_pid(task->pid);
        free(task);
        return NULL;
    }
    task->cgroup_ns_root = cgroup_get(task->cgroup);
    task->cred = alloc_cred();
    if (!task->cred) {
        cgroup_put(task->cgroup_ns_root);
        task->cgroup_ns_root = NULL;
        task_detach_cgroup(task);
        uts_put(task->uts_ns);
        kernel_cond_destroy(&task->wait_cond);
        kernel_mutex_destroy(&task->wait_lock);
        kernel_mutex_destroy(&task->lock);
        free_pid(task->pid);
        free(task);
        return NULL;
    }

    task->start_time_ns = task_start_time_since_boot_ns();

    int idx = task_hash(task->pid);
    kernel_mutex_lock(&task_table_lock);
    task->hash_next = task_table[idx];
    task_table[idx] = task;
    kernel_mutex_unlock(&task_table_lock);

    return task;
}

struct task_struct *task_create_child_impl(struct task_struct *parent) {
    return task_create_child_with_flags_impl(parent, 0);
}

struct task_struct *task_create_child_with_flags_impl(struct task_struct *parent, uint64_t flags) {
    struct task_struct *child;

    if (!parent) {
        errno = ESRCH;
        return NULL;
    }

    child = alloc_task();
    if (!child) {
        errno = ENOMEM;
        return NULL;
    }

    child->ppid = parent->pid;
    child->pgid = parent->pgid;
    child->sid = parent->sid;
    child->clone_flags = flags;
    child->exec_secure = parent->exec_secure;
    child->exec_dumpable = parent->exec_dumpable;
    if ((flags & CLONE_THREAD) != 0) {
        child->tgid = parent->tgid;
        child->ppid = parent->ppid;
    }
    if ((flags & CLONE_NEWPID) != 0 || atomic_load(&parent->new_pid_namespace_pending)) {
        child->pid_ns_level = parent->pid_ns_level + 1;
        child->ns_pid = 1;
    } else {
        child->pid_ns_level = parent->pid_ns_level;
        child->ns_pid = child->pid;
    }
    if (parent->uts_ns) {
        uts_put(child->uts_ns);
        child->uts_ns = uts_get(parent->uts_ns);
    }
    if (parent->cred) {
        put_cred(child->cred);
        child->cred = dup_cred(parent->cred);
        if (!child->cred) {
            free_task(child);
            errno = ENOMEM;
            return NULL;
        }
    }
    if (parent->cgroup) {
        if (task_attach_cgroup(child, parent->cgroup) != 0) {
            free_task(child);
            errno = ENOMEM;
            return NULL;
        }
    }
    if (parent->cgroup_ns_root) {
        cgroup_put(child->cgroup_ns_root);
        child->cgroup_ns_root = cgroup_get(parent->cgroup_ns_root);
        child->cgroup_ns_id = task_cgroup_namespace_id(parent);
    }
    if (parent->seccomp) {
        child->seccomp = seccomp_get(parent->seccomp);
    }

    if (parent->fs) {
        child->fs = dup_fs_struct(parent->fs);
        if (!child->fs) {
            free_task(child);
            errno = ENOMEM;
            return NULL;
        }
    }

    if (parent->files && (flags & CLONE_FILES) != 0) {
        child->files = get_files(parent->files);
    } else if (parent->files) {
        child->files = dup_files(parent->files);
        if (!child->files) {
            free_task(child);
            errno = ENOMEM;
            return NULL;
        }
    }

    if ((flags & CLONE_VM) != 0 && !task_ensure_mm_impl(parent)) {
        free_task(child);
        errno = ENOMEM;
        return NULL;
    }

    if ((flags & CLONE_VM) != 0 && parent->mm) {
        child->mm = task_mm_get_impl(parent->mm);
    } else if (parent->mm) {
        task_mm_put_impl(child->mm);
        child->mm = task_mm_dup_impl(parent->mm);
        if (!child->mm) {
            free_task(child);
            errno = ENOMEM;
            return NULL;
        }
    }

    if ((flags & CLONE_SIGHAND) != 0 && parent->signal) {
        child->signal = parent->signal;
        atomic_fetch_add(&child->signal->refs, 1);
    } else if (parent->signal) {
        child->signal = dup_signal_struct(parent->signal);
        if (!child->signal) {
            free_task(child);
            errno = ENOMEM;
            return NULL;
        }
    } else if (signal_init_task(child) != 0) {
        free_task(child);
        errno = ENOMEM;
        return NULL;
    }

    if (parent->tty) {
        child->tty = parent->tty;
        atomic_fetch_add(&child->tty->refs, 1);
    }

    kernel_mutex_lock(&parent->lock);
    child->parent = parent;
    child->next_sibling = parent->children;
    parent->children = child;
    kernel_mutex_unlock(&parent->lock);

    return child;
}

void task_unlink_child_impl(struct task_struct *parent, struct task_struct *child) {
    struct task_struct **link;

    if (!parent || !child) {
        return;
    }

    kernel_mutex_lock(&parent->lock);
    link = &parent->children;
    while (*link && *link != child) {
        link = &(*link)->next_sibling;
    }
    if (*link == child) {
        *link = child->next_sibling;
    }
    child->next_sibling = NULL;
    if (child->parent == parent) {
        child->parent = NULL;
        child->ppid = 0;
    }
    kernel_mutex_unlock(&parent->lock);
}

void task_mark_stopped_by_signal(struct task_struct *task, int32_t sig) {
    if (!task) {
        return;
    }

    kernel_mutex_lock(&task_table_lock);
    for (int i = 0; i < TASK_MAX_TASKS; i++) {
        struct task_struct *peer = task_table[i];
        while (peer) {
            if (peer->tgid == task->tgid) {
                atomic_store(&peer->termsig, 0);
                atomic_store(&peer->state, TASK_STOPPED);
                atomic_store(&peer->stopped, true);
                atomic_store(&peer->stopsig, sig);
                atomic_store(&peer->continued, false);
                atomic_store(&peer->stop_report_pending, peer->pid == peer->tgid);
                atomic_store(&peer->continue_report_pending, false);
            }
            peer = peer->hash_next;
        }
    }
    kernel_mutex_unlock(&task_table_lock);
}

void task_mark_continued_by_signal(struct task_struct *task) {
    if (!task) {
        return;
    }

    kernel_mutex_lock(&task_table_lock);
    for (int i = 0; i < TASK_MAX_TASKS; i++) {
        struct task_struct *peer = task_table[i];
        while (peer) {
            if (peer->tgid == task->tgid) {
                atomic_store(&peer->state, TASK_RUNNING);
                atomic_store(&peer->stopped, false);
                atomic_store(&peer->stopsig, 0);
                atomic_store(&peer->continued, true);
                atomic_store(&peer->stop_report_pending, false);
                atomic_store(&peer->continue_report_pending, peer->pid == peer->tgid);
            }
            peer = peer->hash_next;
        }
    }
    kernel_mutex_unlock(&task_table_lock);
}

void task_mark_signaled_exit(struct task_struct *task, int32_t sig) {
    if (!task) {
        return;
    }

    atomic_store(&task->signaled, true);
    atomic_store(&task->termsig, sig);
    atomic_store(&task->exited, true);
    atomic_store(&task->state, TASK_ZOMBIE);
    atomic_store(&task->stopped, false);
    atomic_store(&task->stopsig, 0);
    atomic_store(&task->continued, false);
    atomic_store(&task->stop_report_pending, false);
    atomic_store(&task->continue_report_pending, false);
}

void task_mark_exited(struct task_struct *task, int status) {
    if (!task) {
        return;
    }

    task->exit_status = status;
    atomic_store(&task->signaled, false);
    atomic_store(&task->termsig, 0);
    atomic_store(&task->exited, true);
    atomic_store(&task->state, TASK_ZOMBIE);
    atomic_store(&task->stopped, false);
    atomic_store(&task->stopsig, 0);
    atomic_store(&task->continued, false);
    atomic_store(&task->stop_report_pending, false);
    atomic_store(&task->continue_report_pending, false);
}

void task_notify_parent_state_change(struct task_struct *task) {
    struct task_struct *parent;

    if (!task || !task->parent) {
        return;
    }
    if ((task->clone_flags & CLONE_THREAD) != 0) {
        return;
    }

    parent = task->parent;
    atomic_fetch_add(&parent->refs, 1);

    (void)signal_generate_task(parent, SIGCHLD);

    kernel_mutex_lock(&parent->lock);
    if (parent->waiters > 0) {
        kernel_cond_broadcast(&parent->wait_cond);
    }
    kernel_mutex_unlock(&parent->lock);

    free_task(parent);
}

void free_task(struct task_struct *task) {
    if (!task)
        return;

    if (atomic_fetch_sub(&task->refs, 1) > 1)
        return;

    int idx = task_hash(task->pid);
    kernel_mutex_lock(&task_table_lock);
    struct task_struct **pp = &task_table[idx];
    while (*pp && *pp != task) {
        pp = &(*pp)->hash_next;
    }
    if (*pp) {
        *pp = task->hash_next;
    }
    kernel_mutex_unlock(&task_table_lock);

    if (task->files)
        free_files(task->files);
    if (task->fs)
        free_fs_struct(task->fs);
    if (task->signal)
        free_signal_struct(task->signal);
    if (task->cred)
        put_cred(task->cred);
    if (task->cgroup)
        task_detach_cgroup(task);
    if (task->cgroup_ns_root)
        cgroup_put(task->cgroup_ns_root);
    seccomp_clear_task_policy(task);
    if (task->tty)
        atomic_fetch_sub(&task->tty->refs, 1);
    if (task->mm)
        task_mm_put_impl(task->mm);
    if (task->exec_image)
        free(task->exec_image);
    if (task->uts_ns)
        uts_put(task->uts_ns);
    task_clear_exec_strings(task);

    kernel_cond_destroy(&task->wait_cond);
    kernel_mutex_destroy(&task->wait_lock);
    kernel_mutex_destroy(&task->lock);

    free_pid(task->pid);
    free(task);
}

struct task_struct *task_lookup(int32_t pid) {
    if (pid <= 0)
        return NULL;

    int idx = task_hash(pid);
    kernel_mutex_lock(&task_table_lock);
    struct task_struct *task = task_table[idx];
    while (task && task->pid != pid) {
        task = task->hash_next;
    }
    if (task) {
        atomic_fetch_add(&task->refs, 1);
    }
    kernel_mutex_unlock(&task_table_lock);
    return task;
}

static atomic_bool task_initialized = false;

int task_init(void) {
    /* Fast path: already initialized */
    if (atomic_load(&task_initialized) && init_task) {
        if (!current_task) {
            current_task = init_task;
            fdtable_sync_current_task_from_static_impl();
        }
        return 0;
    }

    /* Re-initialization path after deinit */
    if (!init_task) {
        /* Re-initialize from scratch */
        pid_init();
        atomic_store(&task_boot_time_ns, task_monotonic_time_ns());
        if (cgroup_init() != 0) {
            return -1;
        }

        init_task = alloc_task();
        if (!init_task)
            return -1;

        init_task->ppid = 0;
        init_task->pgid = init_task->pid;
        init_task->sid = init_task->pid;
        init_task->ns_pid = 1;
        init_task->pid_ns_level = 0;
        strncpy(init_task->comm, "init", sizeof(init_task->comm));
        init_task->comm[sizeof(init_task->comm) - 1] = '\0';

        init_task->files = alloc_files(NR_OPEN_DEFAULT);
        if (!init_task->files) {
            free_task(init_task);
            init_task = NULL;
            return -1;
        }

        init_task->fs = alloc_fs_struct();
        if (!init_task->fs) {
            free_task(init_task);
            init_task = NULL;
            return -1;
        }
        fs_init_root(init_task->fs, "/");
        fs_init_pwd(init_task->fs, "/");

        init_task->signal = alloc_signal_struct();
        if (!init_task->signal) {
            free_task(init_task);
            init_task = NULL;
            return -1;
        }

        current_task = init_task;
        fdtable_sync_current_task_from_static_impl();
        atomic_store(&task_initialized, true);
        return 0;
    }

    return 0;
}

void task_deinit(void) {
    if (init_task) {
        free_task(init_task);
        init_task = NULL;
    }
    cgroup_deinit();
    current_task = NULL;
    atomic_store(&task_boot_time_ns, 0);
    atomic_store(&task_initialized, false);
}

/* ============================================================================
 * PID/IDENTITY FUNCTIONS
 * ============================================================================ */

int32_t getpid_impl(void) {
    struct task_struct *task = get_current();
    if (!task) {
        /* Try to initialize if not already done */
        if (task_init() == 0) {
            task = get_current();
        }
    }
    return task ? task->pid : 0;
}

int32_t getppid_impl(void) {
    struct task_struct *task = get_current();
    if (!task) {
        /* Try to initialize if not already done */
        if (task_init() == 0) {
            task = get_current();
        }
    }
    return task ? task->ppid : 0;
}

/* ============================================================================
 * SESSION AND PROCESS GROUP FUNCTIONS
 * ============================================================================ */

int32_t getpgrp_impl(void) {
    struct task_struct *task = get_current();
    if (!task) {
        errno = ESRCH;
        return -1;
    }
    return task->pgid;
}

int32_t getpgid_impl(int32_t pid) {
    if (pid == 0) {
        return getpgrp_impl();
    }

    struct task_struct *task = task_lookup(pid);
    if (!task) {
        errno = ESRCH;
        return -1;
    }

    int32_t pgid = task->pgid;
    free_task(task);
    return pgid;
}

int setpgid_impl(int32_t pid, int32_t pgid) {
    struct task_struct *current = get_current();
    if (!current) {
        errno = ESRCH;
        return -1;
    }

    if (pid == 0) {
        pid = current->pid;
    }

    /* Linux: reject negative pgid */
    if (pgid < 0 && pgid != 0) {
        errno = EINVAL;
        return -1;
    }

    if (pgid == 0) {
        pgid = pid;
    }

    struct task_struct *target = task_lookup(pid);
    if (!target) {
        errno = ESRCH;
        return -1;
    }

    kernel_mutex_lock(&target->lock);

    /* Linux: check permissions: caller must be target or target's parent */
    if (target->ppid != current->pid && target->pid != current->pid) {
        kernel_mutex_unlock(&target->lock);
        free_task(target);
        errno = EPERM;
        return -1;
    }

    /* Linux: session match - can't move to different session */
    if (target->sid != current->sid) {
        kernel_mutex_unlock(&target->lock);
        free_task(target);
        errno = EPERM;
        return -1;
    }

    /* Linux: cannot change PGID of a session leader */
    if (target->pid == target->sid) {
        kernel_mutex_unlock(&target->lock);
        free_task(target);
        errno = EPERM;
        return -1;
    }

    /* Linux: if joining existing group, group must exist in same session */
    if (pgid != pid) {
        /* Check if target group exists */
        int found_group = 0;
        for (int i = 0; i < TASK_MAX_TASKS; i++) {
            struct task_struct *t = task_table[i];
            while (t) {
                if (t->pgid == pgid && t->sid == target->sid) {
                    found_group = 1;
                    break;
                }
                t = t->hash_next;
            }
            if (found_group) break;
        }
        if (!found_group) {
            kernel_mutex_unlock(&target->lock);
            free_task(target);
            errno = EPERM;
            return -1;
        }
    }

    /* Linux: if child already execve'd, reject with EACCES */
    if (target->pid != current->pid && atomic_load(&target->execed)) {
        kernel_mutex_unlock(&target->lock);
        free_task(target);
        errno = EACCES;
        return -1;
    }

    target->pgid = pgid;
    kernel_mutex_unlock(&target->lock);
    free_task(target);

    return 0;
}

int32_t getsid_impl(int32_t pid) {
    if (pid == 0) {
        struct task_struct *task = get_current();
        if (!task) {
            errno = ESRCH;
            return -1;
        }
        return task->sid;
    }

    struct task_struct *task = task_lookup(pid);
    if (!task) {
        errno = ESRCH;
        return -1;
    }

    int32_t sid = task->sid;
    free_task(task);
    return sid;
}

int32_t setsid_impl(void) {
    struct task_struct *task = get_current();
    if (!task) {
        errno = ESRCH;
        return -1;
    }

    kernel_mutex_lock(&task->lock);

    /* Check if already process group leader */
    if (task->pgid == task->pid) {
        kernel_mutex_unlock(&task->lock);
        errno = EPERM;
        return -1;
    }

    if (task->tty) {
        atomic_fetch_sub(&task->tty->refs, 1);
        task->tty = NULL;
    }

    /* Create new session and process group */
    task->sid = task->pid;
    task->pgid = task->pid;

    kernel_mutex_unlock(&task->lock);

    return task->pid;
}

int task_session_has_pgrp_impl(int32_t sid, int32_t pgid) {
    if (sid <= 0 || pgid <= 0) {
        return 0;
    }

    int found = 0;

    kernel_mutex_lock(&task_table_lock);
    for (int i = 0; i < TASK_MAX_TASKS; i++) {
        struct task_struct *task = task_table[i];
        while (task) {
            if (task->sid == sid && task->pgid == pgid) {
                found = 1;
                break;
            }
            task = task->hash_next;
        }
        if (found) {
            break;
        }
    }
    kernel_mutex_unlock(&task_table_lock);

    return found;
}

static const char *task_exec_basename(const char *name) {
    const char *base;

    if (!name || name[0] == '\0') {
        return NULL;
    }

    base = strrchr(name, '/');
    if (!base) {
        return name;
    }

    if (base[1] == '\0') {
        return name;
    }

    return base + 1;
}

int task_exec_transition_impl(const char *path, const char *argv0) {
    struct task_struct *task;
    char normalized_path[MAX_PATH];
    struct linux_stat st;
    const char *comm_source;
    size_t comm_len;
    int closed;
    uint32_t old_euid = 0;
    uint32_t old_egid = 0;
    uint64_t old_cap_permitted = 0;
    uint64_t old_cap_effective = 0;
    bool secure_exec = false;

    task = get_current();
    if (!task) {
        errno = ESRCH;
        return -1;
    }

    if (!path) {
        errno = EFAULT;
        return -1;
    }

    closed = vfs_normalize_linux_path(path, normalized_path, sizeof(normalized_path));
    if (closed < 0) {
        errno = -closed;
        return -1;
    }

    if (task->cred) {
        old_euid = task->cred->euid;
        old_egid = task->cred->egid;
        old_cap_permitted = task->cred->cap_permitted;
        old_cap_effective = task->cred->cap_effective;
    }

    if (vfs_fstatat(AT_FDCWD, normalized_path, &st, 0) == 0) {
        uint32_t exec_mode = st.st_mode;
        uint64_t file_cap_permitted = 0;
        uint64_t file_cap_inheritable = 0;
        bool file_cap_effective = false;
        if ((vfs_mount_flags_for_path(normalized_path) & MS_NOSUID) != 0) {
            exec_mode &= ~(uint32_t)(S_ISUID | S_ISGID);
        }
        cred_apply_exec_metadata(get_current_cred(), st.st_uid, st.st_gid, exec_mode);
        if ((vfs_mount_flags_for_path(normalized_path) & MS_NOSUID) == 0 &&
            vfs_get_file_capabilities(normalized_path, &file_cap_permitted,
                                      &file_cap_inheritable, &file_cap_effective) == 0) {
            cred_apply_exec_file_capabilities(get_current_cred(), file_cap_permitted,
                                              file_cap_inheritable, file_cap_effective);
        }
    }
    if (task->cred) {
        secure_exec = task->cred->euid != old_euid || task->cred->egid != old_egid;
        if ((task->cred->cap_permitted != old_cap_permitted ||
             task->cred->cap_effective != old_cap_effective) &&
            (task->cred->cap_permitted != 0 || task->cred->cap_effective != 0)) {
            secure_exec = true;
        }
    }
    task->exec_secure = secure_exec ? 1 : 0;
    task->exec_dumpable = secure_exec ? 0 : 1;

    closed = close_on_exec_impl();
    if (closed < 0) {
        return -1;
    }

    if (task->mm) {
        task->mm->signal_frame_sp = 0;
        task->mm->signal_frame_signo = 0;
        task->mm->signal_frame_return_pc = 0;
        task->mm->signal_handler_pc = 0;
        task->mm->signal_frame_flags = 0;
        task->mm->signal_frame_restorer_pc = 0;
        task->mm->signal_frame_mask = 0;
        task->mm->signal_frame_altstack_sp = 0;
        task->mm->signal_frame_altstack_size = 0;
        task->mm->signal_frame_altstack_flags = 0;
        task->mm->signal_frame_current_sp = 0;
        task->mm->signal_frame_size = 0;
        task->mm->signal_frame_ucontext_flags = 0;
        task->mm->signal_frame_restartable = 0;
        task->mm->signal_frame_restart_return_pc = 0;
        task->mm->signal_frame_restart_sp = 0;
        task->mm->signal_frame_restart_signo = 0;
        task_restart_clear_impl(task);
    }

    memcpy(task->exe, normalized_path, strlen(normalized_path) + 1);

    comm_source = task_exec_basename(argv0);
    if (!comm_source) {
        comm_source = task_exec_basename(normalized_path);
    }
    if (!comm_source) {
        comm_source = normalized_path;
    }

    memset(task->comm, 0, sizeof(task->comm));
    comm_len = strlen(comm_source);
    if (comm_len >= sizeof(task->comm)) {
        comm_len = sizeof(task->comm) - 1;
    }
    memcpy(task->comm, comm_source, comm_len);

    atomic_store(&task->execed, true);

    if (task->vfork_parent) {
        vfork_exec_notify();
    }

    return 0;
}

/* ============================================================================
 * PUBLIC CANONICAL WRAPPERS
 * ============================================================================
 * These wrappers convert between POSIX/Linux public types and
 * IXLandSystem's internal representation.
 */

__attribute__((visibility("default"))) pid_t getpid(void) {
    return (pid_t)getpid_impl();
}

__attribute__((visibility("default"))) pid_t getppid(void) {
    return (pid_t)getppid_impl();
}

__attribute__((visibility("default"))) pid_t getpgrp(void) {
    return (pid_t)getpgrp_impl();
}

__attribute__((visibility("default"))) pid_t getpgid(pid_t pid) {
    return (pid_t)getpgid_impl((int32_t)pid);
}

__attribute__((visibility("default"))) int setpgid(pid_t pid, pid_t pgid) {
    return setpgid_impl((int32_t)pid, (int32_t)pgid);
}

__attribute__((visibility("default"))) pid_t setsid(void) {
    return (pid_t)setsid_impl();
}

__attribute__((visibility("default"))) pid_t getsid(pid_t pid) {
    return (pid_t)getsid_impl((int32_t)pid);
}
