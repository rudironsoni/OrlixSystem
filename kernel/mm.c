/* IXLandSystem/kernel/mm.c
 * Virtual Linux memory mapping substrate.
 */

#include "mm.h"
#include "task.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <linux/elf.h>
#include <linux/mman.h>

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

#define MM_USER_BASE 0x0000001000000000ULL
#define MM_USER_TOP  0x0000007000000000ULL
#define MM_BRK_BASE  0x0000000800000000ULL

static uint64_t mm_next_anon_base = MM_USER_BASE;

static uint64_t mm_align_up(uint64_t value, uint64_t align) {
    if (value > UINT64_MAX - (align - 1)) {
        return 0;
    }
    return (value + align - 1) & ~(align - 1);
}

static uint32_t mm_prot_to_pf(int prot) {
    uint32_t flags = 0;

    if ((prot & PROT_READ) != 0) {
        flags |= PF_R;
    }
    if ((prot & PROT_WRITE) != 0) {
        flags |= PF_W;
    }
    if ((prot & PROT_EXEC) != 0) {
        flags |= PF_X;
    }
    return flags;
}

static int mm_validate_mmap_flags(int flags) {
    int allowed = MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED | MAP_FIXED_NOREPLACE;

    if ((flags & ~allowed) != 0) {
        errno = EINVAL;
        return -1;
    }
    if ((flags & MAP_PRIVATE) == 0) {
        errno = EINVAL;
        return -1;
    }
    if ((flags & MAP_ANONYMOUS) == 0) {
        errno = ENOSYS;
        return -1;
    }
    return 0;
}

static int mm_validate_prot(int prot) {
    if ((prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC | PROT_NONE)) != 0) {
        errno = EINVAL;
        return -1;
    }
    if ((prot & PROT_NONE) != 0 && (prot & (PROT_READ | PROT_WRITE | PROT_EXEC)) != 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

static int mm_range_overlaps(struct mm_struct *mm, uint64_t start, uint64_t end) {
    if (!mm) {
        return 0;
    }
    for (uint32_t i = 0; i < mm->vma_count; i++) {
        if (start < mm->vmas[i].end && end > mm->vmas[i].start) {
            return 1;
        }
    }
    return 0;
}

static uint64_t mm_alloc_addr(struct mm_struct *mm, uint64_t length) {
    uint64_t candidate = mm_align_up(mm_next_anon_base, TASK_VMA_PAGE_SIZE);

    while (candidate != 0 && candidate <= MM_USER_TOP && length <= MM_USER_TOP - candidate) {
        if (!mm_range_overlaps(mm, candidate, candidate + length)) {
            mm_next_anon_base = candidate + length;
            return candidate;
        }
        candidate += length;
        candidate = mm_align_up(candidate, TASK_VMA_PAGE_SIZE);
    }
    return 0;
}

static int mm_add_vma(struct mm_struct *mm, uint64_t start, uint64_t size, uint32_t pf_flags,
                      enum task_vma_kind kind, void *image) {
    struct task_vma *vma;
    uint64_t page_count;

    if (!mm || !image || size == 0 || size > UINT64_MAX - start || mm->vma_count >= TASK_EXEC_MAX_VMAS) {
        errno = ENOMEM;
        return -1;
    }
    if (mm_range_overlaps(mm, start, start + size)) {
        errno = EEXIST;
        return -1;
    }

    page_count = size / TASK_VMA_PAGE_SIZE;
    if ((size % TASK_VMA_PAGE_SIZE) != 0) {
        page_count++;
    }
    if (page_count == 0 || page_count > SIZE_MAX / sizeof(uint32_t)) {
        errno = ENOMEM;
        return -1;
    }

    vma = &mm->vmas[mm->vma_count];
    memset(vma, 0, sizeof(*vma));
    vma->start = start;
    vma->end = start + size;
    vma->flags = pf_flags;
    vma->kind = kind;
    vma->image = image;
    vma->image_size = (size_t)size;
    vma->page_count = page_count;
    vma->page_flags = calloc((size_t)page_count, sizeof(*vma->page_flags));
    if (!vma->page_flags) {
        memset(vma, 0, sizeof(*vma));
        errno = ENOMEM;
        return -1;
    }
    for (uint64_t i = 0; i < page_count; i++) {
        vma->page_flags[i] = pf_flags;
    }
    mm->vma_count++;
    return 0;
}

static void mm_remove_vma_at(struct mm_struct *mm, uint32_t index) {
    struct task_vma *vma;

    if (!mm || index >= mm->vma_count) {
        return;
    }
    vma = &mm->vmas[index];
    if (vma->kind == TASK_VMA_ANON) {
        free(vma->image);
    }
    free(vma->page_flags);
    for (uint32_t i = index + 1; i < mm->vma_count; i++) {
        mm->vmas[i - 1] = mm->vmas[i];
    }
    mm->vma_count--;
    memset(&mm->vmas[mm->vma_count], 0, sizeof(mm->vmas[0]));
}

static int mm_find_brk_vma(struct mm_struct *mm, uint32_t *index_out) {
    if (!mm || mm->brk_start == 0) {
        return 0;
    }
    for (uint32_t i = 0; i < mm->vma_count; i++) {
        if (mm->vmas[i].kind == TASK_VMA_ANON && mm->vmas[i].start == mm->brk_start) {
            if (index_out) {
                *index_out = i;
            }
            return 1;
        }
    }
    return 0;
}

static int mm_range_overlaps_except(struct mm_struct *mm, uint64_t start, uint64_t end,
                                    uint32_t except_index) {
    if (!mm) {
        return 0;
    }
    for (uint32_t i = 0; i < mm->vma_count; i++) {
        if (i == except_index) {
            continue;
        }
        if (start < mm->vmas[i].end && end > mm->vmas[i].start) {
            return 1;
        }
    }
    return 0;
}

void *mmap_impl(void *addr, size_t length, int prot, int flags, int fd, int64_t offset) {
    struct task_struct *task = get_current();
    uint64_t map_len;
    uint64_t map_addr;
    void *image;

    if (!task) {
        errno = ESRCH;
        return (void *)-1;
    }
    if (!task->mm) {
        task->mm = calloc(1, sizeof(*task->mm));
        if (!task->mm) {
            errno = ENOMEM;
            return (void *)-1;
        }
    }
    if (length == 0 || mm_validate_prot(prot) != 0) {
        if (length == 0) {
            errno = EINVAL;
        }
        return (void *)-1;
    }
    if (mm_validate_mmap_flags(flags) != 0) {
        return (void *)-1;
    }
    if (fd != -1 || offset != 0) {
        errno = ENOSYS;
        return (void *)-1;
    }

    map_len = mm_align_up((uint64_t)length, TASK_VMA_PAGE_SIZE);
    if (map_len == 0 || map_len > SIZE_MAX) {
        errno = ENOMEM;
        return (void *)-1;
    }

    if ((flags & (MAP_FIXED | MAP_FIXED_NOREPLACE)) != 0) {
        map_addr = (uint64_t)(uintptr_t)addr;
        if ((map_addr % TASK_VMA_PAGE_SIZE) != 0 || map_addr == 0 ||
            map_len > UINT64_MAX - map_addr || mm_range_overlaps(task->mm, map_addr, map_addr + map_len)) {
            errno = EINVAL;
            return (void *)-1;
        }
    } else {
        map_addr = mm_alloc_addr(task->mm, map_len);
        if (map_addr == 0) {
            errno = ENOMEM;
            return (void *)-1;
        }
    }

    image = calloc(1, (size_t)map_len);
    if (!image) {
        errno = ENOMEM;
        return (void *)-1;
    }
    if (mm_add_vma(task->mm, map_addr, map_len, mm_prot_to_pf(prot), TASK_VMA_ANON, image) != 0) {
        int saved_errno = errno;
        free(image);
        errno = saved_errno;
        return (void *)-1;
    }
    return (void *)(uintptr_t)map_addr;
}

int mprotect_impl(void *addr, size_t len, int prot) {
    struct task_struct *task = get_current();
    uint64_t start = (uint64_t)(uintptr_t)addr;
    uint64_t size;

    if (!task || !task->mm) {
        errno = EFAULT;
        return -1;
    }
    if (len == 0 || (start % TASK_VMA_PAGE_SIZE) != 0 || mm_validate_prot(prot) != 0) {
        if (len == 0 || (start % TASK_VMA_PAGE_SIZE) != 0) {
            errno = EINVAL;
        }
        return -1;
    }
    size = mm_align_up((uint64_t)len, TASK_VMA_PAGE_SIZE);
    if (size == 0 || size > UINT64_MAX - start) {
        errno = ENOMEM;
        return -1;
    }
    return task_set_vma_page_flags_impl(task, start, size, mm_prot_to_pf(prot));
}

int munmap_impl(void *addr, size_t len) {
    struct task_struct *task = get_current();
    uint64_t start = (uint64_t)(uintptr_t)addr;
    uint64_t size;
    uint64_t end;

    if (!task || !task->mm) {
        errno = EFAULT;
        return -1;
    }
    if (len == 0 || (start % TASK_VMA_PAGE_SIZE) != 0) {
        errno = EINVAL;
        return -1;
    }
    size = mm_align_up((uint64_t)len, TASK_VMA_PAGE_SIZE);
    if (size == 0 || size > UINT64_MAX - start) {
        errno = ENOMEM;
        return -1;
    }
    end = start + size;
    for (uint32_t i = 0; i < task->mm->vma_count; i++) {
        struct task_vma *vma = &task->mm->vmas[i];
        if (vma->start == start && vma->end == end) {
            mm_remove_vma_at(task->mm, i);
            return 0;
        }
    }
    errno = EINVAL;
    return -1;
}

void *brk_impl(void *addr) {
    struct task_struct *task = get_current();
    uint64_t requested = (uint64_t)(uintptr_t)addr;
    uint64_t aligned;
    uint32_t brk_index = 0;
    int has_brk_vma;

    if (!task) {
        errno = ESRCH;
        return (void *)-1;
    }
    if (!task->mm) {
        task->mm = calloc(1, sizeof(*task->mm));
        if (!task->mm) {
            errno = ENOMEM;
            return (void *)-1;
        }
    }
    if (task->mm->brk_start == 0) {
        task->mm->brk_start = MM_BRK_BASE;
        task->mm->brk_current = MM_BRK_BASE;
    }
    if (requested == 0) {
        return (void *)(uintptr_t)task->mm->brk_current;
    }
    if (requested < task->mm->brk_start || requested >= MM_USER_BASE) {
        return (void *)(uintptr_t)task->mm->brk_current;
    }

    aligned = mm_align_up(requested - task->mm->brk_start, TASK_VMA_PAGE_SIZE);
    if (aligned == 0 && requested > task->mm->brk_start) {
        return (void *)(uintptr_t)task->mm->brk_current;
    }

    has_brk_vma = mm_find_brk_vma(task->mm, &brk_index);
    if (aligned == 0) {
        if (has_brk_vma) {
            mm_remove_vma_at(task->mm, brk_index);
        }
        task->mm->brk_current = task->mm->brk_start;
        return (void *)(uintptr_t)task->mm->brk_current;
    }

    if (!has_brk_vma) {
        void *image = calloc(1, (size_t)aligned);
        if (!image) {
            return (void *)(uintptr_t)task->mm->brk_current;
        }
        if (mm_add_vma(task->mm, task->mm->brk_start, aligned, PF_R | PF_W, TASK_VMA_ANON, image) != 0) {
            free(image);
            return (void *)(uintptr_t)task->mm->brk_current;
        }
    } else {
        struct task_vma *vma = &task->mm->vmas[brk_index];
        uint64_t page_count = aligned / TASK_VMA_PAGE_SIZE;
        void *resized_image;
        uint32_t *resized_flags;
        size_t copy_size;

        if (aligned > SIZE_MAX || page_count == 0 || page_count > SIZE_MAX / sizeof(uint32_t) ||
            mm_range_overlaps_except(task->mm, task->mm->brk_start, task->mm->brk_start + aligned,
                                     brk_index)) {
            return (void *)(uintptr_t)task->mm->brk_current;
        }

        resized_image = calloc(1, (size_t)aligned);
        if (!resized_image) {
            return (void *)(uintptr_t)task->mm->brk_current;
        }
        resized_flags = calloc((size_t)page_count, sizeof(*resized_flags));
        if (!resized_flags) {
            free(resized_image);
            return (void *)(uintptr_t)task->mm->brk_current;
        }
        copy_size = vma->image_size < (size_t)aligned ? vma->image_size : (size_t)aligned;
        memcpy(resized_image, vma->image, copy_size);
        for (uint64_t i = 0; i < page_count; i++) {
            resized_flags[i] = PF_R | PF_W;
        }
        free(vma->image);
        free(vma->page_flags);
        vma->image = resized_image;
        vma->image_size = (size_t)aligned;
        vma->page_flags = resized_flags;
        vma->page_count = page_count;
        vma->end = task->mm->brk_start + aligned;
    }

    task->mm->brk_current = requested;
    return (void *)(uintptr_t)task->mm->brk_current;
}
