/* OrlixKernel/kernel/task.c
 * Virtual task/process subsystem implementation
 */

#include "../private/kernel/task_state.h"
#include "../private/kernel/signal_state.h"
#include "../private/kernel/cred_state.h"
#include "task.h"
#include "signal.h"
#include "../private/kernel/cgroup_state.h"
#include "cred.h"
#include "mm.h"
#include "ptrace.h"
#include "seccomp.h"
#include "uts.h"
#include "../private/kernel/uts_state.h"
#include "internal/timekeeping.h"
#include "../private/kernel/ptrace_state.h"

#include "../fs/fdtable.h"
#include "../private/fs/fdtable_state.h"
#include "../private/fs/readiness_state.h"
#include "../fs/vfs.h"

#include <linux/errno.h>
#include <linux/gfp_types.h>
#include <linux/limits.h>
#include <linux/string.h>
#include <uapi/linux/fcntl.h>
#include <uapi/linux/capability.h>
#include <uapi/linux/elf.h>
#include <linux/mount.h>
#include <uapi/linux/sched.h>
#include <uapi/linux/signal.h>
#include <uapi/linux/stat.h>
#include <uapi/asm/siginfo.h>
#include <uapi/asm/stat.h>
#include <linux/resource.h>

extern void *__kmalloc_noprof(size_t size, gfp_t flags);
extern void kfree(const void *objp);
extern char *kstrdup(const char *src, gfp_t flags);

static void task_note_memory_signal_fault_impl(struct task *task, int32_t signo, int32_t code,
                                               uint64_t addr);

static struct memory_space *task_ensure_mm_impl(struct task *task) {
    if (!task) {
        return NULL;
    }
    if (!task->mm) {
        task->mm = __kmalloc_noprof(sizeof(*task->mm), GFP_KERNEL | __GFP_ZERO);
        if (!task->mm) {
            return NULL;
        }
        atomic_set(&task->mm->refs, 1);
    }
    return task->mm;
}

static __thread struct task *current_task = NULL;
struct task *task_init_process = NULL;
static atomic64_t task_boot_time_ns = ATOMIC64_INIT(0);

/* Task table - accessible to signal.c for killpg */
kernel_mutex_t task_table_lock = KERNEL_MUTEX_INITIALIZER;
struct task *task_table[TASK_MAX_TASKS] = {NULL};

static uint64_t task_monotonic_time_ns(void) {
    u64 now_ns;

    if (kernel_clock_now_ns(1, &now_ns) != 0) {
        return 0;
    }
    return (uint64_t)now_ns;
}

static uint64_t task_start_time_since_boot_ns(void) {
    uint64_t now = task_monotonic_time_ns();
    uint64_t boot = (uint64_t)atomic64_read(&task_boot_time_ns);

    if (boot == 0) {
        boot = now;
        atomic64_set(&task_boot_time_ns, (s64)boot);
    }
    if (now <= boot) {
        return 1;
    }
    return now - boot;
}

int task_pidfd_getfd_access_impl(struct task *target) {
    struct task *caller = task_current();
    struct task *live_target;

    if (!caller || !target || !caller->cred || !target->cred) {
        return -ESRCH;
    }

    if (caller == target || caller->tgid == target->tgid) {
        if (atomic_read(&target->exited)) {
            return -ESRCH;
        }
        return 0;
    }

    live_target = task_lookup(target->pid);
    if (!live_target) {
        return -ESRCH;
    }
    if (atomic_read(&live_target->exited)) {
        task_put(live_target);
        return -ESRCH;
    }
    task_put(live_target);

    return ptrace_may_access_task_impl(caller, target);
}

int task_hash(int32_t pid) {
    return (int)(pid % TASK_MAX_TASKS);
}

struct task *task_current(void) {
    return current_task;
}

void task_set_current(struct task *task) {
    if (task == current_task) {
        return;
    }

    /* Hold a task reference for as long as it is installed as the current task
     * on a host thread. This prevents other threads from freeing a task while
     * it's still in active use (e.g. readiness wait paths using task->wait_lock).
     *
     * The reference is released when switching away, and on thread exit via the
     * kernel thread trampoline. */
    if (task) {
        atomic_inc(&task->refs);
    }
    if (current_task) {
        task_put(current_task);
    }
    current_task = task;
    set_current_cred(task ? task->cred : NULL);
}

static void task_clear_exec_strings(struct task *task) {
    if (!task) {
        return;
    }

    for (int i = 0; i < TASK_MAX_ARGS; i++) {
        kfree(task->argv[i]);
        task->argv[i] = NULL;
        kfree(task->envp[i]);
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
        kfree(strings[i]);
    }
}

static int task_copy_exec_string_vector(char *const input[], char **output, int *out_count) {
    int count = 0;

    if (!output || !out_count) {
        return -EINVAL;
    }

    *out_count = 0;
    if (!input) {
        return 0;
    }

    while (input[count]) {
        if (count >= TASK_MAX_ARGS - 1) {
            return -E2BIG;
        }
        output[count] = kstrdup(input[count], GFP_KERNEL);
        if (!output[count]) {
            task_free_exec_string_vector(output, count);
            return -ENOMEM;
        }
        count++;
    }

    output[count] = NULL;
    *out_count = count;
    return 0;
}

int task_record_exec_strings_impl(char *const argv[], char *const envp[]) {
    struct task *task = task_current();
    char *new_argv[TASK_MAX_ARGS] = {0};
    char *new_envp[TASK_MAX_ARGS] = {0};
    int new_argc = 0;
    int new_envc = 0;

    if (!task) {
        return -ESRCH;
    }

    {
        int ret = task_copy_exec_string_vector(argv, new_argv, &new_argc);
        if (ret < 0) {
            return ret;
        }
    }
    {
        int ret = task_copy_exec_string_vector(envp, new_envp, &new_envc);
        if (ret < 0) {
            task_free_exec_string_vector(new_argv, new_argc);
            return ret;
        }
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

const struct task_vma *task_find_vma_impl(struct task *task, uint64_t addr) {
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

struct task_vma *task_find_vma_mutable_impl(struct task *task, uint64_t addr) {
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

int task_set_vma_page_flags_impl(struct task *task, uint64_t addr, uint64_t size, uint32_t flags) {
    const struct task_vma *found;
    uint64_t cursor;
    uint64_t end;

    if (!task || !task->mm || size == 0 || size > U64_MAX - addr) {
        return -EFAULT;
    }

    end = addr + size;
    cursor = addr;
    while (cursor < end) {
        found = task_find_vma_impl(task, cursor);
        if (!found) {
            return -ENOMEM;
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
            return -EFAULT;
        }

        segment_end = vma->end < end ? vma->end : end;
        start_page = (cursor - vma->start) / TASK_VMA_PAGE_SIZE;
        end_page = ((segment_end - 1) - vma->start) / TASK_VMA_PAGE_SIZE;
        if (end_page >= vma->page_count) {
            return -EFAULT;
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
        struct task *task = task_table[i];
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
        struct task *task = task_table[i];
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

static void task_note_memory_fault_impl(struct task *task, uint64_t addr, int32_t code) {
    task_note_memory_signal_fault_impl(task, SIGSEGV, code, addr);
}

static void task_note_memory_signal_fault_impl(struct task *task, int32_t signo, int32_t code, uint64_t addr) {
    if (!task) {
        return;
    }
    task->last_fault_signal = signo;
    task->last_fault_code = code;
    task->last_fault_addr = addr;
    (void)signal_generate_task_info(task, signo, code, addr);
}

void task_clear_vmas_impl(struct memory_space *mm) {
    if (!mm) {
        return;
    }
    for (uint32_t i = 0; i < mm->vma_count; i++) {
        if (mm->vmas[i].kind == TASK_VMA_ANON || mm->vmas[i].kind == TASK_VMA_FILE) {
            if (mm->vmas[i].shared_pages) {
                for (uint64_t page = 0; page < mm->vmas[i].page_count; page++) {
                    mm_shared_mapping_put_impl(mm->vmas[i].shared_pages[page]);
                }
                kfree(mm->vmas[i].shared_pages);
            } else if (mm->vmas[i].private_pages) {
                for (uint64_t page = 0; page < mm->vmas[i].page_count; page++) {
                    mm_private_page_put_impl(mm->vmas[i].private_pages[page]);
                }
                kfree(mm->vmas[i].private_pages);
            } else if (mm->vmas[i].shared_mapping) {
                mm_shared_mapping_put_impl(mm->vmas[i].shared_mapping);
            } else {
                kfree(mm->vmas[i].image);
            }
            mm->vmas[i].image = NULL;
            mm->vmas[i].shared_pages = NULL;
            mm->vmas[i].private_pages = NULL;
        }
        kfree(mm->vmas[i].page_flags);
        mm->vmas[i].page_flags = NULL;
        kfree(mm->vmas[i].resident_pages);
        mm->vmas[i].resident_pages = NULL;
        kfree(mm->vmas[i].dirty_pages);
        mm->vmas[i].dirty_pages = NULL;
        mm->vmas[i].page_count = 0;
    }
    memset(mm->vmas, 0, sizeof(mm->vmas));
    mm->vma_count = 0;
}

struct memory_space *task_mm_get_impl(struct memory_space *mm) {
    if (!mm) {
        return NULL;
    }
    if (atomic_read(&mm->refs) <= 0) {
        atomic_set(&mm->refs, 1);
    }
    atomic_inc(&mm->refs);
    return mm;
}

void task_mm_put_impl(struct memory_space *mm) {
    if (!mm) {
        return;
    }
    if (atomic_read(&mm->refs) > 1 && atomic_dec_return(&mm->refs) > 0) {
        return;
    }
    for (uint32_t i = 0; i < mm->exec_segment_count; i++) {
        kfree(mm->exec_segments[i].image);
    }
    for (uint32_t i = 0; i < mm->interp_segment_count; i++) {
        kfree(mm->interp_segments[i].image);
    }
    task_clear_vmas_impl(mm);
    kfree(mm->exec_image_base);
    kfree(mm->interp_image_base);
    kfree(mm->stack_guard_image);
    kfree(mm->initial_stack_image);
    kfree(mm);
}

void task_mm_update_high_water_impl(struct memory_space *mm) {
    uint64_t size_pages = 0;
    uint64_t resident_pages = 0;

    if (!mm) {
        return;
    }
    for (uint32_t i = 0; i < mm->vma_count; i++) {
        struct task_vma *vma = &mm->vmas[i];

        size_pages += vma->page_count;
        if (!vma->resident_pages) {
            resident_pages += vma->page_count;
            continue;
        }
        for (uint64_t page = 0; page < vma->page_count; page++) {
            if (vma->resident_pages[page]) {
                resident_pages++;
            }
        }
    }
    if (size_pages > mm->vm_peak_pages) {
        mm->vm_peak_pages = size_pages;
    }
    if (resident_pages > mm->vm_high_water_rss_pages) {
        mm->vm_high_water_rss_pages = resident_pages;
    }
}

static long task_read_vma(struct task_vma *vma, uint64_t addr, void *buf, size_t count) {
    size_t offset;
    size_t available;
    uint64_t page_remaining;
    size_t to_copy;
    uint32_t flags;

    if (!vma || (!vma->image && !vma->shared_pages) || vma->image_size == 0 || addr < vma->start) {
        return 0;
    }
    if ((uint64_t)vma->image_size > U64_MAX - vma->start) {
        return -EFAULT;
    }
    if (addr >= vma->end) {
        return 0;
    }
    flags = task_vma_page_flags_impl(vma, addr);
    if ((flags & PF_R) == 0) {
        return -EACCES;
    }
    if (vma->kind == TASK_VMA_FILE) {
        uint64_t page_index = (addr - vma->start) / TASK_VMA_PAGE_SIZE;
        size_t file_offset = (size_t)(addr - vma->start);
        long long file_size = mm_vma_file_size_impl(vma);
        if ((vma->shared || !vma->dirty_pages || page_index >= vma->page_count ||
             vma->dirty_pages[page_index] == 0) &&
            file_size >= 0) {
            size_t page_start = file_offset - (file_offset % TASK_VMA_PAGE_SIZE);
            uint64_t backing_page_start = vma->backing_offset + (uint64_t)page_start;
            uint64_t backing_file_offset = vma->backing_offset + (uint64_t)file_offset;

            if (backing_page_start >= (uint64_t)file_size) {
                return -ENXIO;
            }
            if (backing_file_offset >= (uint64_t)file_size) {
                size_t page_offset = file_offset % TASK_VMA_PAGE_SIZE;
                size_t to_zero = count;
                if (to_zero > TASK_VMA_PAGE_SIZE - page_offset) {
                    to_zero = TASK_VMA_PAGE_SIZE - page_offset;
                }
                if (vma->resident_pages && page_index < vma->page_count) {
                    vma->resident_pages[page_index] = 1;
                }
                memset(buf, 0, to_zero);
                return (long)to_zero;
            }
        }
    }
    if (vma->shared_pages) {
        return mm_shared_vma_read_impl(vma, addr, buf, count);
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
    if (vma->resident_pages) {
        uint64_t page_index = (addr - vma->start) / TASK_VMA_PAGE_SIZE;
        if (page_index < vma->page_count) {
            vma->resident_pages[page_index] = 1;
        }
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
    if ((uint64_t)vma->image_size > U64_MAX - vma->start) {
        return -EFAULT;
    }
    if (addr >= vma->end) {
        return 0;
    }
    flags = task_vma_page_flags_impl(vma, addr);
    if ((flags & PF_W) == 0) {
        return -EACCES;
    }
    if (vma->kind == TASK_VMA_FILE) {
        uint64_t page_index = (addr - vma->start) / TASK_VMA_PAGE_SIZE;
        size_t file_offset = (size_t)(addr - vma->start);
        long long file_size = mm_vma_file_size_impl(vma);
        size_t page_start = file_offset - (file_offset % TASK_VMA_PAGE_SIZE);
        if ((vma->shared || !vma->dirty_pages || page_index >= vma->page_count ||
             vma->dirty_pages[page_index] == 0) &&
            file_size >= 0 &&
            vma->backing_offset + (uint64_t)page_start >= (uint64_t)file_size) {
            return -ENXIO;
        }
    }
    if (vma->shared_pages) {
        return mm_shared_vma_write_impl(vma, addr, buf, count);
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

static int task_grow_stack_down_impl(struct task *task, uint64_t fault_addr) {
    struct memory_space *mm;
    struct task_vma *stack = NULL;
    struct task_vma *guard = NULL;
    void *new_image = NULL;
    uint32_t *new_page_flags = NULL;
    uint8_t *new_resident_pages = NULL;
    uint8_t *new_dirty_pages = NULL;
    void *old_image;
    uint64_t old_size;
    uint64_t new_size;
    uint64_t new_base;
    uint64_t new_pages;
    size_t page_flags_bytes;
    size_t resident_bytes;
    size_t dirty_bytes;

    if (!task || !task->mm) {
        return -EFAULT;
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
    if (old_size > U64_MAX - TASK_VMA_PAGE_SIZE) {
        return -ENOMEM;
    }
    new_size = old_size + TASK_VMA_PAGE_SIZE;
    if (new_size > task->rlimits[RLIMIT_STACK].cur) {
        return -EACCES;
    }
    if (mm->initial_stack_base < TASK_VMA_PAGE_SIZE) {
        return -ENOMEM;
    }
    new_base = mm->initial_stack_base - TASK_VMA_PAGE_SIZE;
    new_pages = new_size / TASK_VMA_PAGE_SIZE;
    if (new_pages == 0 ||
        new_pages > SIZE_MAX / sizeof(*new_page_flags) ||
        new_pages > SIZE_MAX / sizeof(*new_resident_pages) ||
        new_pages > SIZE_MAX / sizeof(*new_dirty_pages)) {
        return -ENOMEM;
    }
    page_flags_bytes = (size_t)new_pages * sizeof(*new_page_flags);
    resident_bytes = (size_t)new_pages * sizeof(*new_resident_pages);
    dirty_bytes = (size_t)new_pages * sizeof(*new_dirty_pages);

    new_image = __kmalloc_noprof((size_t)new_size, GFP_KERNEL | __GFP_ZERO);
    new_page_flags = __kmalloc_noprof(page_flags_bytes, GFP_KERNEL | __GFP_ZERO);
    new_resident_pages = __kmalloc_noprof(resident_bytes, GFP_KERNEL | __GFP_ZERO);
    new_dirty_pages = __kmalloc_noprof(dirty_bytes, GFP_KERNEL | __GFP_ZERO);
    if (!new_image || !new_page_flags || !new_resident_pages || !new_dirty_pages) {
        kfree(new_image);
        kfree(new_page_flags);
        kfree(new_resident_pages);
        kfree(new_dirty_pages);
        return -ENOMEM;
    }

    memcpy((unsigned char *)new_image + TASK_VMA_PAGE_SIZE, old_image, (size_t)old_size);
    for (uint64_t i = 0; i < new_pages; i++) {
        new_page_flags[i] = PF_R | PF_W;
    }
    for (uint64_t i = 1; i < new_pages; i++) {
        uint64_t old_page = i - 1;
        new_resident_pages[i] = stack->resident_pages && old_page < stack->page_count
                                    ? stack->resident_pages[old_page]
                                    : 1;
        new_dirty_pages[i] = stack->dirty_pages && old_page < stack->page_count
                                 ? stack->dirty_pages[old_page]
                                 : 0;
    }

    kfree(old_image);
    mm->initial_stack_image = new_image;
    mm->initial_stack_image_size = (size_t)new_size;
    mm->initial_stack_base = new_base;
    mm->initial_stack_size = new_size;

    kfree(stack->page_flags);
    kfree(stack->resident_pages);
    kfree(stack->dirty_pages);
    stack->start = new_base;
    stack->end = new_base + new_size;
    stack->image = mm->initial_stack_image;
    stack->image_size = mm->initial_stack_image_size;
    stack->page_count = new_pages;
    stack->page_flags = new_page_flags;
    stack->resident_pages = new_resident_pages;
    stack->dirty_pages = new_dirty_pages;

    guard->start -= TASK_VMA_PAGE_SIZE;
    guard->end -= TASK_VMA_PAGE_SIZE;
    task_mm_update_high_water_impl(mm);
    return 1;
}

static bool task_addr_is_below_stack_guard(const struct task *task, uint64_t addr) {
    const struct memory_space *mm;

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

static void task_propagate_shared_file_write(struct memory_space *mm, struct task_vma *source,
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

long task_read_virtual_memory_impl(struct task *task, uint64_t addr, void *buf, size_t count) {
    struct memory_space *mm;
    long copied;

    if (!buf && count > 0) {
        return -EFAULT;
    }
    if (count == 0) {
        return 0;
    }
    if (!task || !task->mm) {
        return -EFAULT;
    }

    mm = task->mm;
    for (size_t total = 0; total < count;) {
        if ((uint64_t)total > U64_MAX - addr) {
            if (total > 0) {
                return (long)total;
            }
            return -EFAULT;
        }
        if (task_addr_is_below_stack_guard(task, addr + total)) {
            task_note_memory_fault_impl(task, addr + total, SEGV_MAPERR);
            if (total > 0) {
                return (long)total;
            }
            return -EFAULT;
        }
        copied = 0;
        for (uint32_t i = 0; i < mm->vma_count; i++) {
            copied = task_read_vma(&mm->vmas[i], addr + total, (unsigned char *)buf + total, count - total);
            if (copied != 0) {
                break;
            }
        }
        if (copied < 0) {
            if (copied == -ENXIO) {
                task_note_memory_signal_fault_impl(task, SIGBUS, BUS_ADRERR, addr + total);
            } else {
                task_note_memory_fault_impl(task, addr + total,
                                            copied == -EACCES ? SEGV_ACCERR : SEGV_MAPERR);
            }
            return total > 0 ? (long)total : copied;
        }
        if (copied == 0) {
            task_note_memory_fault_impl(task, addr + total, SEGV_MAPERR);
            if (total > 0) {
                return (long)total;
            }
            return -EFAULT;
        }
        total += (size_t)copied;
    }
    return (long)count;
}

long task_write_virtual_memory_impl(struct task *task, uint64_t addr, const void *buf, size_t count) {
    struct memory_space *mm;
    long copied;

    if (!buf && count > 0) {
        return -EFAULT;
    }
    if (count == 0) {
        return 0;
    }
    if (!task || !task->mm) {
        return -EFAULT;
    }

    mm = task->mm;
    for (size_t total = 0; total < count;) {
        if ((uint64_t)total > U64_MAX - addr) {
            if (total > 0) {
                return (long)total;
            }
            return -EFAULT;
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
            return -EFAULT;
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
            if (copied == -ENXIO) {
                task_note_memory_signal_fault_impl(task, SIGBUS, BUS_ADRERR, addr + total);
            } else {
                task_note_memory_fault_impl(task, addr + total,
                                            copied == -EACCES ? SEGV_ACCERR : SEGV_MAPERR);
            }
            return total > 0 ? (long)total : copied;
        }
        if (copied == 0) {
            task_note_memory_fault_impl(task, addr + total, SEGV_MAPERR);
            if (total > 0) {
                return (long)total;
            }
            return -EFAULT;
        }
        total += (size_t)copied;
    }
    return (long)count;
}

const struct task_exec_handoff *task_get_exec_handoff_impl(struct task *task) {
    if (!task || !task->mm) {
        return NULL;
    }
    return &task->mm->handoff;
}

int task_restart_record_impl(struct task *task, enum task_restart_kind kind,
                             uint64_t arg0, uint64_t arg1, uint64_t arg2,
                             uint64_t arg3, uint64_t arg4, uint64_t arg5) {
    if (!task || kind == TASK_RESTART_NONE) {
        return -EINVAL;
    }
    if (!task_ensure_mm_impl(task)) {
        return -ENOMEM;
    }
    return signal_frame_restart_record_task(task, (uint64_t)kind,
                                            arg0, arg1, arg2, arg3, arg4, arg5);
}

struct task *alloc_task(void) {
    struct task *task = __kmalloc_noprof(sizeof(struct task), GFP_KERNEL | __GFP_ZERO);
    if (!task)
        return NULL;

    task->pid = task_alloc_pid();
    task->tgid = task->pid;
    /* A new task starts without pgid/sid; fork_impl will inherit from parent */
    task->pgid = 0;
    task->sid = 0;
    task->ns_pid = task->pid;
    task->pid_ns_level = 0;
    task->cgroup_ns_id = 1;
    task->cgroup_ns_owner_user_ns_id = 1;
    task->vfork_parent = NULL;
    for (int i = 0; i < 16; i++) {
        task->rlimits[i].cur = U64_MAX;
        task->rlimits[i].max = U64_MAX;
    }
    task->rlimits[RLIMIT_STACK].cur = 8ULL * 1024ULL * 1024ULL;
    task->rlimits[RLIMIT_STACK].max = 64ULL * 1024ULL * 1024ULL;
    task->rlimits[RLIMIT_NPROC].cur = TASK_MAX_TASKS;
    task->rlimits[RLIMIT_NPROC].max = TASK_MAX_TASKS;
    task->rlimits[RLIMIT_NOFILE].cur = NR_OPEN_DEFAULT;
    task->rlimits[RLIMIT_NOFILE].max = NR_OPEN_DEFAULT;

    atomic_set(&task->state, RUN_STATE_RUNNING);
    atomic_set(&task->refs, 1);
    atomic_set(&task->exited, 0);
    atomic_set(&task->signaled, 0);
    atomic_set(&task->stopped, 0);
    atomic_set(&task->stopsig, 0);
    atomic_set(&task->continued, 0);
    atomic_set(&task->stop_report_pending, 0);
    atomic_set(&task->continue_report_pending, 0);
    atomic_set(&task->execed, 0);
    task->thread_pending_signals = 0;
    atomic_set(&task->new_pid_namespace_pending, 0);
    task->clone_flags = 0;
    task->exec_secure = 0;
    task->exec_dumpable = 1;
    task->ptracer_pid = 0;
    task->ptrace_attached = false;
    task->ptrace_syscall_trace = false;
    task->ptrace_syscall_exit_next = false;
    task->ptrace_signal_bypass = false;
    task->ptrace_signal = 0;
    task->ptrace_signal_stop = 0;
    task->ptrace_options = 0;
    task->ptrace_event = 0;
    task->ptrace_event_message = 0;
    task->ptrace_syscall_op = 0;
    task->ptrace_syscall_nr = 0;
    memset(task->ptrace_syscall_args, 0, sizeof(task->ptrace_syscall_args));
    task->ptrace_syscall_retval = 0;
    task->ptrace_syscall_is_error = false;
    memset(task->ptrace_regs, 0, sizeof(task->ptrace_regs));
    task->ptrace_sp = 0;
    task->ptrace_pc = 0;
    task->ptrace_pstate = 0;

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
            task_free_pid(task->pid);
            kfree(task);
            return NULL;
        }
        cgroup_put(root);
    }
    if (!task->cgroup) {
        kernel_cond_destroy(&task->wait_cond);
        kernel_mutex_destroy(&task->wait_lock);
        kernel_mutex_destroy(&task->lock);
        task_free_pid(task->pid);
        kfree(task);
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
        task_free_pid(task->pid);
        kfree(task);
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

struct task *task_create_child_impl(struct task *parent) {
    return task_create_child_with_flags_impl(parent, 0);
}

struct task *task_create_child_with_flags_impl(struct task *parent, uint64_t flags) {
    struct task *child;

    if (!parent) {
        return NULL;
    }

    child = alloc_task();
    if (!child) {
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
    if ((flags & CLONE_NEWPID) != 0 || atomic_read(&parent->new_pid_namespace_pending)) {
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
        cred_release(child->cred);
        child->cred = dup_cred(parent->cred);
        if (!child->cred) {
            task_put(child);
            return NULL;
        }
    }
    if (parent->cgroup) {
        if (task_attach_cgroup(child, parent->cgroup) != 0) {
            task_put(child);
            return NULL;
        }
    }
    if (parent->cgroup_ns_root) {
        cgroup_put(child->cgroup_ns_root);
        child->cgroup_ns_root = cgroup_get(parent->cgroup_ns_root);
        child->cgroup_ns_id = task_cgroup_namespace_id(parent);
        child->cgroup_ns_owner_user_ns_id = parent->cgroup_ns_owner_user_ns_id ?
                                            parent->cgroup_ns_owner_user_ns_id : 1;
    }
    if (parent->seccomp) {
        child->seccomp = seccomp_get(parent->seccomp);
    }

    if (parent->fs) {
        child->fs = dup_fs_struct(parent->fs);
        if (!child->fs) {
            task_put(child);
            return NULL;
        }
    }

    if (parent->files && (flags & CLONE_FILES) != 0) {
        child->files = get_files(parent->files);
    } else if (parent->files) {
        child->files = dup_files(parent->files);
        if (!child->files) {
            task_put(child);
            return NULL;
        }
    }

    if ((flags & CLONE_VM) != 0 && !task_ensure_mm_impl(parent)) {
        task_put(child);
        return NULL;
    }

    if ((flags & CLONE_VM) != 0 && parent->mm) {
        child->mm = task_mm_get_impl(parent->mm);
    } else if (parent->mm) {
        task_mm_put_impl(child->mm);
        child->mm = task_mm_dup_impl(parent->mm);
        if (!child->mm) {
            task_put(child);
            return NULL;
        }
    }

    if ((flags & CLONE_SIGHAND) != 0 && parent->signal) {
        child->signal = parent->signal;
        atomic_inc(&child->signal->refs);
    } else if (parent->signal) {
        child->signal = dup_signal_struct(parent->signal);
        if (!child->signal) {
            task_put(child);
            return NULL;
        }
    } else if (signal_init_task(child) != 0) {
        task_put(child);
        return NULL;
    }

    if (parent->tty) {
        child->tty = parent->tty;
        atomic_inc(&child->tty->refs);
    }

    kernel_mutex_lock(&parent->lock);
    child->parent = parent;
    child->next_sibling = parent->children;
    parent->children = child;
    kernel_mutex_unlock(&parent->lock);

    ptrace_note_fork_event(parent, child->pid, (flags & CLONE_THREAD) != 0);

    return child;
}

void task_unlink_child_impl(struct task *parent, struct task *child) {
    struct task **link;

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

void task_mark_stopped_by_signal(struct task *task, int32_t sig) {
    if (!task) {
        return;
    }

    kernel_mutex_lock(&task_table_lock);
    for (int i = 0; i < TASK_MAX_TASKS; i++) {
        struct task *peer = task_table[i];
        while (peer) {
            if (peer->tgid == task->tgid) {
                atomic_set(&peer->termsig, 0);
                atomic_set(&peer->state, RUN_STATE_STOPPED);
                atomic_set(&peer->stopped, 1);
                atomic_set(&peer->stopsig, sig);
                atomic_set(&peer->continued, 0);
                atomic_set(&peer->stop_report_pending, peer->pid == peer->tgid ? 1 : 0);
                atomic_set(&peer->continue_report_pending, 0);
            }
            peer = peer->hash_next;
        }
    }
    kernel_mutex_unlock(&task_table_lock);
}

void task_mark_continued_by_signal(struct task *task) {
    if (!task) {
        return;
    }

    kernel_mutex_lock(&task_table_lock);
    for (int i = 0; i < TASK_MAX_TASKS; i++) {
        struct task *peer = task_table[i];
        while (peer) {
            if (peer->tgid == task->tgid) {
                atomic_set(&peer->state, RUN_STATE_RUNNING);
                atomic_set(&peer->stopped, 0);
                atomic_set(&peer->stopsig, 0);
                atomic_set(&peer->continued, 1);
                atomic_set(&peer->stop_report_pending, 0);
                atomic_set(&peer->continue_report_pending, peer->pid == peer->tgid ? 1 : 0);
            }
            peer = peer->hash_next;
        }
    }
    kernel_mutex_unlock(&task_table_lock);
}

void task_mark_signaled_exit(struct task *task, int32_t sig) {
    if (!task) {
        return;
    }

    atomic_set(&task->signaled, 1);
    atomic_set(&task->termsig, sig);
    atomic_set(&task->exited, 1);
    atomic_set(&task->state, RUN_STATE_ZOMBIE);
    atomic_set(&task->stopped, 0);
    atomic_set(&task->stopsig, 0);
    atomic_set(&task->continued, 0);
    atomic_set(&task->stop_report_pending, 0);
    atomic_set(&task->continue_report_pending, 0);
    poll_notify_readiness_impl();
}

void task_mark_exited(struct task *task, int status) {
    if (!task) {
        return;
    }

    task->exit_status = status;
    atomic_set(&task->signaled, 0);
    atomic_set(&task->termsig, 0);
    atomic_set(&task->exited, 1);
    atomic_set(&task->state, RUN_STATE_ZOMBIE);
    atomic_set(&task->stopped, 0);
    atomic_set(&task->stopsig, 0);
    atomic_set(&task->continued, 0);
    atomic_set(&task->stop_report_pending, 0);
    atomic_set(&task->continue_report_pending, 0);
    poll_notify_readiness_impl();
}

void task_notify_parent_state_change(struct task *task) {
    struct task *parent;

    if (!task || !task->parent) {
        return;
    }
    if ((task->clone_flags & CLONE_THREAD) != 0) {
        return;
    }

    parent = task->parent;
    atomic_inc(&parent->refs);

    (void)signal_generate_task(parent, SIGCHLD);

    kernel_mutex_lock(&parent->lock);
    if (parent->waiters > 0) {
        kernel_cond_broadcast(&parent->wait_cond);
    }
    kernel_mutex_unlock(&parent->lock);

    task_put(parent);
}

void task_put(struct task *task) {
    if (!task)
        return;

    if (atomic_dec_return(&task->refs) > 0)
        return;

    int idx = task_hash(task->pid);
    kernel_mutex_lock(&task_table_lock);
    struct task **pp = &task_table[idx];
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
        cred_release(task->cred);
    if (task->cgroup)
        task_detach_cgroup(task);
    if (task->cgroup_ns_root)
        cgroup_put(task->cgroup_ns_root);
    seccomp_clear_task_policy(task);
    if (task->tty)
        atomic_dec(&task->tty->refs);
    if (task->mm)
        task_mm_put_impl(task->mm);
    if (task->exec_image)
        kfree(task->exec_image);
    if (task->uts_ns)
        uts_put(task->uts_ns);
    task_clear_exec_strings(task);

    kernel_cond_destroy(&task->wait_cond);
    kernel_mutex_destroy(&task->wait_lock);
    kernel_mutex_destroy(&task->lock);

    task_free_pid(task->pid);
    kfree(task);
}

struct task *task_lookup(int32_t pid) {
    if (pid <= 0)
        return NULL;

    int idx = task_hash(pid);
    kernel_mutex_lock(&task_table_lock);
    struct task *task = task_table[idx];
    while (task && task->pid != pid) {
        task = task->hash_next;
    }
    if (task) {
        atomic_inc(&task->refs);
    }
    kernel_mutex_unlock(&task_table_lock);
    return task;
}

int task_reassign_pid_impl(struct task *task, int32_t pid) {
    struct task **link;
    int old_pid;
    int old_idx;
    int new_idx;

    if (!task) {
        return -ESRCH;
    }
    if (pid <= 0) {
        return -EINVAL;
    }
    if (task->pid == pid) {
        return 0;
    }
    if (pid_reserve(pid) != 0) {
        return -1;
    }

    old_pid = task->pid;
    old_idx = task_hash(old_pid);
    new_idx = task_hash(pid);

    kernel_mutex_lock(&task_table_lock);
    link = &task_table[old_idx];
    while (*link && *link != task) {
        link = &(*link)->hash_next;
    }
    if (*link != task) {
        kernel_mutex_unlock(&task_table_lock);
        task_free_pid(pid);
        return -ESRCH;
    }

    *link = task->hash_next;
    task->pid = pid;
    if (task->tgid == old_pid) {
        task->tgid = pid;
    }
    if (task->ns_pid == old_pid) {
        task->ns_pid = pid;
    }
    task->hash_next = task_table[new_idx];
    task_table[new_idx] = task;
    kernel_mutex_unlock(&task_table_lock);

    task_free_pid(old_pid);
    return 0;
}

static atomic_t task_initialized = ATOMIC_INIT(0);

int task_init(void) {
    /* Fast path: already initialized */
    if (atomic_read(&task_initialized) && task_init_process) {
        if (!current_task) {
            /* Hold a reference for the thread-local current task binding. */
            atomic_inc(&task_init_process->refs);
            current_task = task_init_process;
            fdtable_sync_current_task_from_static_impl();
        }
        return 0;
    }

    /* Re-initialization path after deinit */
    if (!task_init_process) {
        /* Re-initialize from scratch */
        pid_init();
        atomic64_set(&task_boot_time_ns, (s64)task_monotonic_time_ns());
        if (cgroup_init() != 0) {
            return -1;
        }

        task_init_process = alloc_task();
        if (!task_init_process)
            return -1;

        task_init_process->ppid = 0;
        task_init_process->pgid = task_init_process->pid;
        task_init_process->sid = task_init_process->pid;
        task_init_process->ns_pid = 1;
        task_init_process->pid_ns_level = 0;
        strncpy(task_init_process->comm, "init", sizeof(task_init_process->comm));
        task_init_process->comm[sizeof(task_init_process->comm) - 1] = '\0';

        task_init_process->files = alloc_files(NR_OPEN_DEFAULT);
        if (!task_init_process->files) {
            task_put(task_init_process);
            task_init_process = NULL;
            return -1;
        }

        task_init_process->fs = alloc_fs_struct();
        if (!task_init_process->fs) {
            task_put(task_init_process);
            task_init_process = NULL;
            return -1;
        }
        fs_init_root(task_init_process->fs, "/");
        fs_init_pwd(task_init_process->fs, "/");

        task_init_process->signal = alloc_signal_struct();
        if (!task_init_process->signal) {
            task_put(task_init_process);
            task_init_process = NULL;
            return -1;
        }

        /* Hold a reference for the thread-local current task binding. */
        atomic_inc(&task_init_process->refs);
        current_task = task_init_process;
        fdtable_sync_current_task_from_static_impl();
        atomic_set(&task_initialized, 1);
        return 0;
    }

    return 0;
}

void task_deinit(void) {
    /* Drop the thread-local current-task reference (if any) so teardown can
     * actually free tasks like task_init_process. current_task is TLS but shutdown runs
     * on a specific host thread; we must release that thread's reference. */
    if (current_task) {
        struct task *task = current_task;
        current_task = NULL;
        set_current_cred(NULL);
        task_put(task);
    }
    if (task_init_process) {
        struct task *task = task_init_process;
        task_init_process = NULL;
        task_put(task);
    }
    cgroup_deinit();
    atomic64_set(&task_boot_time_ns, 0);
    atomic_set(&task_initialized, 0);
}

/* ============================================================================
 * PID/IDENTITY FUNCTIONS
 * ============================================================================ */

int32_t getpid_impl(void) {
    struct task *task = task_current();
    if (!task) {
        /* Try to initialize if not already done */
        if (task_init() == 0) {
            task = task_current();
        }
    }
    return task ? task->pid : 0;
}

int32_t getppid_impl(void) {
    struct task *task = task_current();
    if (!task) {
        /* Try to initialize if not already done */
        if (task_init() == 0) {
            task = task_current();
        }
    }
    return task ? task->ppid : 0;
}

/* ============================================================================
 * SESSION AND PROCESS GROUP FUNCTIONS
 * ============================================================================ */

int32_t getpgrp_impl(void) {
    struct task *task = task_current();
    if (!task) {
        return -ESRCH;
    }
    return task->pgid;
}

int32_t getpgid_impl(int32_t pid) {
    if (pid == 0) {
        return getpgrp_impl();
    }

    struct task *task = task_lookup(pid);
    if (!task) {
        return -ESRCH;
    }

    int32_t pgid = task->pgid;
    task_put(task);
    return pgid;
}

int setpgid_impl(int32_t pid, int32_t pgid) {
    struct task *current = task_current();
    if (!current) {
        return -ESRCH;
    }

    if (pid == 0) {
        pid = current->pid;
    }

    /* Linux: reject negative pgid */
    if (pgid < 0 && pgid != 0) {
        return -EINVAL;
    }

    if (pgid == 0) {
        pgid = pid;
    }

    struct task *target = task_lookup(pid);
    if (!target) {
        return -ESRCH;
    }

    kernel_mutex_lock(&target->lock);

    /* Linux: check permissions: caller must be target or target's parent */
    if (target->ppid != current->pid && target->pid != current->pid) {
        kernel_mutex_unlock(&target->lock);
        task_put(target);
        return -EPERM;
    }

    /* Linux: session match - can't move to different session */
    if (target->sid != current->sid) {
        kernel_mutex_unlock(&target->lock);
        task_put(target);
        return -EPERM;
    }

    /* Linux: cannot change PGID of a session leader */
    if (target->pid == target->sid) {
        kernel_mutex_unlock(&target->lock);
        task_put(target);
        return -EPERM;
    }

    /* Linux: if joining existing group, group must exist in same session */
    if (pgid != pid) {
        /* Check if target group exists */
        int found_group = 0;
        for (int i = 0; i < TASK_MAX_TASKS; i++) {
            struct task *t = task_table[i];
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
            task_put(target);
            return -EPERM;
        }
    }

    /* Linux: if child already execve'd, reject with EACCES */
    if (target->pid != current->pid && atomic_read(&target->execed)) {
        kernel_mutex_unlock(&target->lock);
        task_put(target);
        return -EACCES;
    }

    target->pgid = pgid;
    kernel_mutex_unlock(&target->lock);
    task_put(target);

    return 0;
}

int32_t getsid_impl(int32_t pid) {
    if (pid == 0) {
        struct task *task = task_current();
        if (!task) {
            return -ESRCH;
        }
        return task->sid;
    }

    struct task *task = task_lookup(pid);
    if (!task) {
        return -ESRCH;
    }

    int32_t sid = task->sid;
    task_put(task);
    return sid;
}

int32_t setsid_impl(void) {
    struct task *task = task_current();
    if (!task) {
        return -ESRCH;
    }

    kernel_mutex_lock(&task->lock);

    /* Check if already process group leader */
    if (task->pgid == task->pid) {
        kernel_mutex_unlock(&task->lock);
        return -EPERM;
    }

    if (task->tty) {
        atomic_dec(&task->tty->refs);
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
        struct task *task = task_table[i];
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
    struct task *task;
    char normalized_path[MAX_PATH];
    struct stat st;
    const char *comm_source;
    size_t comm_len;
    int closed;
    uint32_t old_euid = 0;
    uint32_t old_egid = 0;
    uint64_t old_cap_permitted = 0;
    uint64_t old_cap_effective = 0;
    bool secure_exec = false;

    task = task_current();
    if (!task) {
        return -ESRCH;
    }

    if (!path) {
        return -EFAULT;
    }

    closed = vfs_normalize_linux_path(path, normalized_path, sizeof(normalized_path));
    if (closed < 0) {
        return closed;
    }
    if ((vfs_mount_flags_for_path(normalized_path) & MNT_NOEXEC) != 0) {
        return -EACCES;
    }

    if (task->cred) {
        old_euid = task->cred->euid;
        old_egid = task->cred->egid;
        old_cap_permitted = task->cred->cap_permitted;
        old_cap_effective = task->cred->cap_effective;
    }

    if (vfs_path_fstatat(AT_FDCWD, normalized_path, &st, 0) == 0) {
        uint32_t exec_mode = st.st_mode;
        uint64_t file_cap_permitted = 0;
        uint64_t file_cap_inheritable = 0;
        bool file_cap_effective = false;
        if ((vfs_mount_flags_for_path(normalized_path) & MNT_NOSUID) != 0) {
            exec_mode &= ~(uint32_t)(S_ISUID | S_ISGID);
        }
        cred_apply_exec_metadata(cred_current(), st.st_uid, st.st_gid, exec_mode);
        if ((vfs_mount_flags_for_path(normalized_path) & MNT_NOSUID) == 0 &&
            vfs_get_file_capabilities(normalized_path, &file_cap_permitted,
                                      &file_cap_inheritable, &file_cap_effective) == 0) {
            cred_apply_exec_file_capabilities(cred_current(), file_cap_permitted,
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
    ptrace_note_exec_event(task);

    closed = close_on_exec_impl();
    if (closed < 0) {
        return -1;
    }

    signal_frame_clear_task(task);

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

    atomic_set(&task->execed, 1);

    if (task->vfork_parent) {
        vfork_exec_notify();
    }

    return 0;
}

/* ============================================================================
 * PUBLIC CANONICAL WRAPPERS
 * ============================================================================
 * These wrappers convert between POSIX/Linux public types and
 * OrlixKernel's internal representation.
 */

__attribute__((visibility("default"))) __kernel_pid_t getpid(void) {
    return (__kernel_pid_t)getpid_impl();
}

__attribute__((visibility("default"))) __kernel_pid_t getppid(void) {
    return (__kernel_pid_t)getppid_impl();
}

__attribute__((visibility("default"))) __kernel_pid_t getpgrp(void) {
    return (__kernel_pid_t)getpgrp_impl();
}

__attribute__((visibility("default"))) __kernel_pid_t getpgid(__kernel_pid_t pid) {
    return (__kernel_pid_t)getpgid_impl((int32_t)pid);
}

__attribute__((visibility("default"))) int setpgid(__kernel_pid_t pid, __kernel_pid_t pgid) {
    return setpgid_impl((int32_t)pid, (int32_t)pgid);
}

__attribute__((visibility("default"))) __kernel_pid_t setsid(void) {
    return (__kernel_pid_t)setsid_impl();
}

__attribute__((visibility("default"))) __kernel_pid_t getsid(__kernel_pid_t pid) {
    return (__kernel_pid_t)getsid_impl((int32_t)pid);
}
