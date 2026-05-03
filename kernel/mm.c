/* IXLandSystem/kernel/mm.c
 * Virtual Linux memory mapping substrate.
 */

#include "mm.h"
#include "task.h"

#include "../fs/fdtable.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <linux/elf.h>
#include <linux/fcntl.h>
#include <linux/mman.h>

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

#define MM_USER_BASE 0x0000001000000000ULL
#define MM_USER_TOP  0x0000007000000000ULL
#define MM_BRK_BASE  0x0000000800000000ULL

static uint64_t mm_next_anon_base = MM_USER_BASE;

struct vm_shared_mapping {
    uint64_t file_identity;
    uint64_t page_index;
    uint8_t image[TASK_VMA_PAGE_SIZE];
    atomic_int refs;
    struct vm_shared_mapping *next;
};

struct vm_private_page {
    uint8_t image[TASK_VMA_PAGE_SIZE];
    atomic_int refs;
};

static kernel_mutex_t mm_shared_mapping_lock = KERNEL_MUTEX_INITIALIZER;
static struct vm_shared_mapping *mm_shared_mappings;

struct mm_file_size_note {
    uint64_t file_identity;
    uint64_t size;
};

static struct mm_file_size_note mm_file_size_notes[128];

extern long long pread_impl(int fd, void *buf, size_t count, long long offset);
extern long long pwrite_impl(int fd, const void *buf, size_t count, long long offset);
extern long long lseek_impl(int fd, long long offset, int whence);
extern long long read_impl(int fd, void *buf, size_t count);
extern long long write_impl(int fd, const void *buf, size_t count);
extern int open_impl(const char *path, int flags, unsigned int mode);
extern int close_impl(int fd);

static long long mm_fd_pread(int fd, void *buf, size_t count, long long offset);

static void mm_shared_mapping_get(struct vm_shared_mapping *mapping) {
    if (mapping) {
        atomic_fetch_add(&mapping->refs, 1);
    }
}

static void mm_shared_mapping_put(struct vm_shared_mapping *mapping) {
    if (!mapping) {
        return;
    }
    if (atomic_fetch_sub(&mapping->refs, 1) != 1) {
        return;
    }

    kernel_mutex_lock(&mm_shared_mapping_lock);
    struct vm_shared_mapping **cursor = &mm_shared_mappings;
    while (*cursor) {
        if (*cursor == mapping) {
            *cursor = mapping->next;
            break;
        }
        cursor = &(*cursor)->next;
    }
    kernel_mutex_unlock(&mm_shared_mapping_lock);

    free(mapping);
}

void mm_shared_mapping_get_impl(struct vm_shared_mapping *mapping) {
    mm_shared_mapping_get(mapping);
}

void mm_shared_mapping_put_impl(struct vm_shared_mapping *mapping) {
    mm_shared_mapping_put(mapping);
}

static struct vm_private_page *mm_private_page_alloc(const void *source, size_t source_len) {
    struct vm_private_page *page = calloc(1, sizeof(*page));

    if (!page) {
        errno = ENOMEM;
        return NULL;
    }
    atomic_init(&page->refs, 1);
    if (source && source_len > 0) {
        if (source_len > TASK_VMA_PAGE_SIZE) {
            source_len = TASK_VMA_PAGE_SIZE;
        }
        memcpy(page->image, source, source_len);
    }
    return page;
}

static void mm_private_page_get(struct vm_private_page *page) {
    if (page) {
        atomic_fetch_add(&page->refs, 1);
    }
}

void mm_private_page_put_impl(struct vm_private_page *page) {
    if (!page) {
        return;
    }
    if (atomic_fetch_sub(&page->refs, 1) == 1) {
        free(page);
    }
}

static void mm_private_pages_put(struct vm_private_page **pages, uint64_t page_count) {
    if (!pages) {
        return;
    }
    for (uint64_t i = 0; i < page_count; i++) {
        mm_private_page_put_impl(pages[i]);
    }
    free(pages);
}

static int mm_private_vma_enable_cow(struct task_vma *vma) {
    struct vm_private_page **pages;

    if (!vma || vma->private_pages || vma->shared_pages || vma->shared_mapping ||
        (vma->kind != TASK_VMA_ANON && vma->kind != TASK_VMA_FILE)) {
        return 0;
    }
    if (!vma->image || vma->page_count == 0) {
        errno = EINVAL;
        return -1;
    }
    pages = calloc((size_t)vma->page_count, sizeof(*pages));
    if (!pages) {
        errno = ENOMEM;
        return -1;
    }
    for (uint64_t i = 0; i < vma->page_count; i++) {
        size_t offset = (size_t)(i * TASK_VMA_PAGE_SIZE);
        size_t remaining = offset < vma->image_size ? vma->image_size - offset : 0;

        pages[i] = mm_private_page_alloc((const unsigned char *)vma->image + offset, remaining);
        if (!pages[i]) {
            mm_private_pages_put(pages, vma->page_count);
            return -1;
        }
    }
    free(vma->image);
    vma->image = pages[0]->image;
    vma->private_pages = pages;
    return 0;
}

long mm_private_vma_read_impl(const struct task_vma *vma, uint64_t addr, void *buf, size_t count) {
    uint64_t offset;
    uint64_t page_index;
    size_t page_offset;
    size_t to_copy;

    if (!vma || !vma->private_pages || addr < vma->start || addr >= vma->end) {
        return 0;
    }
    offset = addr - vma->start;
    page_index = offset / TASK_VMA_PAGE_SIZE;
    if (page_index >= vma->page_count || !vma->private_pages[page_index]) {
        errno = EFAULT;
        return -1;
    }
    page_offset = (size_t)(offset % TASK_VMA_PAGE_SIZE);
    to_copy = count;
    if (to_copy > vma->image_size - (size_t)offset) {
        to_copy = vma->image_size - (size_t)offset;
    }
    if (to_copy > TASK_VMA_PAGE_SIZE - page_offset) {
        to_copy = TASK_VMA_PAGE_SIZE - page_offset;
    }
    memcpy(buf, vma->private_pages[page_index]->image + page_offset, to_copy);
    return (long)to_copy;
}

long mm_private_vma_write_impl(struct task_vma *vma, uint64_t addr, const void *buf, size_t count) {
    struct vm_private_page *page;
    struct vm_private_page *private_copy;
    uint64_t offset;
    uint64_t page_index;
    size_t page_offset;
    size_t to_copy;

    if (!vma || !vma->private_pages || addr < vma->start || addr >= vma->end) {
        return 0;
    }
    offset = addr - vma->start;
    page_index = offset / TASK_VMA_PAGE_SIZE;
    if (page_index >= vma->page_count || !vma->private_pages[page_index]) {
        errno = EFAULT;
        return -1;
    }
    page = vma->private_pages[page_index];
    if (atomic_load(&page->refs) > 1) {
        private_copy = mm_private_page_alloc(page->image, TASK_VMA_PAGE_SIZE);
        if (!private_copy) {
            return -1;
        }
        mm_private_page_put_impl(page);
        vma->private_pages[page_index] = private_copy;
        page = private_copy;
        if (page_index == 0) {
            vma->image = page->image;
        }
    }
    page_offset = (size_t)(offset % TASK_VMA_PAGE_SIZE);
    to_copy = count;
    if (to_copy > vma->image_size - (size_t)offset) {
        to_copy = vma->image_size - (size_t)offset;
    }
    if (to_copy > TASK_VMA_PAGE_SIZE - page_offset) {
        to_copy = TASK_VMA_PAGE_SIZE - page_offset;
    }
    memcpy(page->image + page_offset, buf, to_copy);
    if (vma->resident_pages && page_index < vma->page_count) {
        vma->resident_pages[page_index] = 1;
    }
    if (vma->dirty_pages && page_index < vma->page_count) {
        vma->dirty_pages[page_index] = 1;
    }
    return (long)to_copy;
}

static uint64_t mm_fd_file_identity(int fd) {
    fd_entry_t *entry;
    uint64_t identity;

    entry = get_fd_entry_impl(fd);
    if (!entry) {
        return 0;
    }
    identity = get_fd_file_identity_impl(entry);
    put_fd_entry_impl(entry);
    return identity;
}

static int mm_fd_is_synthetic_zero(int fd) {
    fd_entry_t *entry;
    int is_zero = 0;

    entry = get_fd_entry_impl(fd);
    if (!entry) {
        return 0;
    }
    is_zero = get_fd_is_synthetic_dev_impl(entry) &&
              get_fd_synthetic_dev_node_impl(entry) == SYNTHETIC_DEV_ZERO;
    put_fd_entry_impl(entry);
    return is_zero;
}

static struct vm_shared_mapping *mm_shared_mapping_lookup_locked(uint64_t file_identity,
                                                                 uint64_t page_index) {
    for (struct vm_shared_mapping *mapping = mm_shared_mappings; mapping; mapping = mapping->next) {
        if (mapping->file_identity == file_identity && mapping->page_index == page_index) {
            mm_shared_mapping_get(mapping);
            return mapping;
        }
    }
    return NULL;
}

static struct vm_shared_mapping *mm_shared_mapping_get_or_create(int fd, uint64_t file_identity,
                                                                 uint64_t page_index) {
    struct vm_shared_mapping *mapping;
    long long bytes;

    if (file_identity == 0) {
        errno = EBADF;
        return NULL;
    }

    kernel_mutex_lock(&mm_shared_mapping_lock);
    mapping = mm_shared_mapping_lookup_locked(file_identity, page_index);
    kernel_mutex_unlock(&mm_shared_mapping_lock);
    if (mapping) {
        return mapping;
    }

    mapping = calloc(1, sizeof(*mapping));
    if (!mapping) {
        errno = ENOMEM;
        return NULL;
    }
    bytes = mm_fd_pread(fd, mapping->image, TASK_VMA_PAGE_SIZE,
                        (long long)(page_index * TASK_VMA_PAGE_SIZE));
    if (bytes < 0) {
        int saved_errno = errno;
        free(mapping);
        errno = saved_errno;
        return NULL;
    }
    mapping->file_identity = file_identity;
    mapping->page_index = page_index;
    atomic_init(&mapping->refs, 1);

    kernel_mutex_lock(&mm_shared_mapping_lock);
    struct vm_shared_mapping *existing = mm_shared_mapping_lookup_locked(file_identity, page_index);
    if (existing) {
        kernel_mutex_unlock(&mm_shared_mapping_lock);
        free(mapping);
        return existing;
    }
    mapping->next = mm_shared_mappings;
    mm_shared_mappings = mapping;
    kernel_mutex_unlock(&mm_shared_mapping_lock);
    return mapping;
}

static void mm_shared_pages_put(struct vm_shared_mapping **pages, uint64_t page_count) {
    if (!pages) {
        return;
    }
    for (uint64_t i = 0; i < page_count; i++) {
        mm_shared_mapping_put(pages[i]);
    }
    free(pages);
}

static struct vm_shared_mapping **mm_shared_pages_get_or_create(int fd, uint64_t offset,
                                                                uint64_t page_count) {
    uint64_t file_identity = mm_fd_file_identity(fd);
    uint64_t first_page = offset / TASK_VMA_PAGE_SIZE;
    struct vm_shared_mapping **pages;

    if (file_identity == 0 || page_count == 0 || page_count > SIZE_MAX / sizeof(*pages)) {
        errno = file_identity == 0 ? EBADF : ENOMEM;
        return NULL;
    }
    pages = calloc((size_t)page_count, sizeof(*pages));
    if (!pages) {
        errno = ENOMEM;
        return NULL;
    }
    for (uint64_t i = 0; i < page_count; i++) {
        pages[i] = mm_shared_mapping_get_or_create(fd, file_identity, first_page + i);
        if (!pages[i]) {
            mm_shared_pages_put(pages, i);
            return NULL;
        }
    }
    return pages;
}

void mm_note_file_truncate_impl(int fd, int64_t length) {
    uint64_t identity;

    if (length < 0) {
        return;
    }
    identity = mm_fd_file_identity(fd);
    if (identity == 0) {
        return;
    }
    for (size_t i = 0; i < sizeof(mm_file_size_notes) / sizeof(mm_file_size_notes[0]); i++) {
        if (mm_file_size_notes[i].file_identity == identity) {
            mm_file_size_notes[i].size = (uint64_t)length;
            return;
        }
    }
    for (size_t i = 0; i < sizeof(mm_file_size_notes) / sizeof(mm_file_size_notes[0]); i++) {
        if (mm_file_size_notes[i].file_identity == 0) {
            mm_file_size_notes[i].file_identity = identity;
            mm_file_size_notes[i].size = (uint64_t)length;
            return;
        }
    }
}

static long long mm_shared_file_remaining(const struct task_vma *vma, size_t offset) {
    uint64_t file_offset;
    uint64_t identity;

    if (!vma) {
        return 0;
    }
    identity = vma->backing_file_identity;
    if (identity == 0 && vma->backing_fd >= 0) {
        identity = mm_fd_file_identity(vma->backing_fd);
    }
    if (identity == 0) {
        return (long long)(vma->image_size - offset);
    }
    for (size_t i = 0; i < sizeof(mm_file_size_notes) / sizeof(mm_file_size_notes[0]); i++) {
        if (mm_file_size_notes[i].file_identity != 0 &&
            mm_file_size_notes[i].file_identity == identity) {
            file_offset = vma->backing_offset + (uint64_t)offset;
            if (file_offset >= mm_file_size_notes[i].size) {
                errno = ENXIO;
                return -1;
            }
            return (long long)(mm_file_size_notes[i].size - file_offset);
        }
    }
    return (long long)(vma->image_size - offset);
}

long mm_shared_vma_read_impl(const struct task_vma *vma, uint64_t addr, void *buf, size_t count) {
    size_t offset;
    uint64_t page_index;
    size_t page_offset;
    size_t to_copy;
    long long file_remaining;

    if (!vma || !vma->shared_pages || addr < vma->start || addr >= vma->end) {
        return 0;
    }
    offset = (size_t)(addr - vma->start);
    file_remaining = mm_shared_file_remaining(vma, offset);
    if (file_remaining < 0) {
        return -1;
    }
    page_index = offset / TASK_VMA_PAGE_SIZE;
    if (page_index >= vma->page_count || !vma->shared_pages[page_index]) {
        return 0;
    }
    page_offset = offset % TASK_VMA_PAGE_SIZE;
    to_copy = count;
    if (to_copy > TASK_VMA_PAGE_SIZE - page_offset) {
        to_copy = TASK_VMA_PAGE_SIZE - page_offset;
    }
    if (to_copy > vma->image_size - offset) {
        to_copy = vma->image_size - offset;
    }
    if ((long long)to_copy > file_remaining) {
        to_copy = (size_t)file_remaining;
    }
    memcpy(buf, vma->shared_pages[page_index]->image + page_offset, to_copy);
    return (long)to_copy;
}

long mm_shared_vma_write_impl(struct task_vma *vma, uint64_t addr, const void *buf, size_t count) {
    size_t offset;
    uint64_t page_index;
    size_t page_offset;
    size_t to_copy;
    long long file_remaining;

    if (!vma || !vma->shared_pages || addr < vma->start || addr >= vma->end) {
        return 0;
    }
    offset = (size_t)(addr - vma->start);
    file_remaining = mm_shared_file_remaining(vma, offset);
    if (file_remaining < 0) {
        return -1;
    }
    page_index = offset / TASK_VMA_PAGE_SIZE;
    if (page_index >= vma->page_count || !vma->shared_pages[page_index]) {
        return 0;
    }
    page_offset = offset % TASK_VMA_PAGE_SIZE;
    to_copy = count;
    if (to_copy > TASK_VMA_PAGE_SIZE - page_offset) {
        to_copy = TASK_VMA_PAGE_SIZE - page_offset;
    }
    if (to_copy > vma->image_size - offset) {
        to_copy = vma->image_size - offset;
    }
    if ((long long)to_copy > file_remaining) {
        to_copy = (size_t)file_remaining;
    }
    memcpy(vma->shared_pages[page_index]->image + page_offset, buf, to_copy);
    if (vma->resident_pages && page_index < vma->page_count) {
        vma->resident_pages[page_index] = 1;
    }
    if (vma->dirty_pages && page_index < vma->page_count) {
        vma->dirty_pages[page_index] = 1;
    }
    return (long)to_copy;
}

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

static int mm_validate_mmap_flags(int flags, int fd) {
    int allowed = MAP_PRIVATE | MAP_SHARED | MAP_ANONYMOUS | MAP_FIXED | MAP_FIXED_NOREPLACE;

    if ((flags & ~allowed) != 0) {
        errno = EINVAL;
        return -1;
    }
    if (((flags & MAP_PRIVATE) != 0) == ((flags & MAP_SHARED) != 0)) {
        errno = EINVAL;
        return -1;
    }
    if ((flags & MAP_ANONYMOUS) != 0 && fd != -1) {
        errno = EINVAL;
        return -1;
    }
    if ((flags & MAP_ANONYMOUS) == 0 && fd < 0) {
        errno = EBADF;
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

static int mm_validate_madvise(int advice) {
    switch (advice) {
    case MADV_NORMAL:
    case MADV_RANDOM:
    case MADV_SEQUENTIAL:
    case MADV_WILLNEED:
    case MADV_DONTNEED:
        return 0;
    default:
        errno = EINVAL;
        return -1;
    }
}

static long long mm_fd_pread(int fd, void *buf, size_t count, long long offset) {
    long long bytes = pread_impl(fd, buf, count, offset);

    if (bytes >= 0 || errno != ENOTSUP) {
        return bytes;
    }

    long long original = lseek_impl(fd, 0, SEEK_CUR);
    if (original < 0) {
        return -1;
    }
    if (lseek_impl(fd, offset, SEEK_SET) < 0) {
        return -1;
    }
    bytes = read_impl(fd, buf, count);
    int saved_errno = errno;
    if (lseek_impl(fd, original, SEEK_SET) < 0 && bytes >= 0) {
        return -1;
    }
    if (bytes < 0) {
        errno = saved_errno;
    }
    return bytes;
}

static long long mm_fd_pwrite(int fd, const void *buf, size_t count, long long offset) {
    long long bytes = pwrite_impl(fd, buf, count, offset);

    if (bytes >= 0 || errno != ENOTSUP) {
        return bytes;
    }

    long long original = lseek_impl(fd, 0, SEEK_CUR);
    if (original < 0) {
        return -1;
    }
    if (lseek_impl(fd, offset, SEEK_SET) < 0) {
        return -1;
    }
    bytes = write_impl(fd, buf, count);
    int saved_errno = errno;
    if (lseek_impl(fd, original, SEEK_SET) < 0 && bytes >= 0) {
        return -1;
    }
    if (bytes < 0) {
        errno = saved_errno;
    }
    return bytes;
}

static long long mm_vma_pread(const struct task_vma *vma, void *buf, size_t count,
                              long long offset) {
    long long bytes;
    int reopened;
    int saved_errno;

    if (!vma || !buf) {
        errno = EINVAL;
        return -1;
    }

    if (vma->backing_fd >= 0) {
        bytes = mm_fd_pread(vma->backing_fd, buf, count, offset);
        if (bytes >= 0 || errno != EBADF) {
            return bytes;
        }
    }
    if (vma->backing_path[0] == '\0') {
        errno = EBADF;
        return -1;
    }

    reopened = open_impl(vma->backing_path, O_RDONLY, 0);
    if (reopened < 0) {
        return -1;
    }
    bytes = mm_fd_pread(reopened, buf, count, offset);
    saved_errno = errno;
    if (close_impl(reopened) != 0 && bytes >= 0) {
        return -1;
    }
    if (bytes < 0) {
        errno = saved_errno;
    }
    return bytes;
}

static long long mm_vma_pwrite(struct task_vma *vma, const void *buf, size_t count,
                               long long offset) {
    long long bytes;
    int reopened;
    int saved_errno;

    if (!vma) {
        errno = EINVAL;
        return -1;
    }

    if (vma->backing_fd >= 0) {
        bytes = mm_fd_pwrite(vma->backing_fd, buf, count, offset);
        if (bytes >= 0 || errno != EBADF) {
            return bytes;
        }
    }
    if (vma->backing_path[0] == '\0') {
        if (vma->shared_pages) {
            return (long long)count;
        }
        errno = EBADF;
        return -1;
    }

    reopened = open_impl(vma->backing_path, O_RDWR, 0);
    if (reopened < 0) {
        if (errno == ENOENT && vma->shared_pages) {
            return (long long)count;
        }
        return -1;
    }
    bytes = mm_fd_pwrite(reopened, buf, count, offset);
    saved_errno = errno;
    if (close_impl(reopened) != 0 && bytes >= 0) {
        return -1;
    }
    if (bytes < 0) {
        errno = saved_errno;
    }
    return bytes;
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
    vma->backing_fd = -1;
    vma->backing_file_identity = 0;
    vma->page_count = page_count;
    vma->page_flags = calloc((size_t)page_count, sizeof(*vma->page_flags));
    if (!vma->page_flags) {
        memset(vma, 0, sizeof(*vma));
        errno = ENOMEM;
        return -1;
    }
    vma->resident_pages = calloc((size_t)page_count, sizeof(*vma->resident_pages));
    if (!vma->resident_pages) {
        free(vma->page_flags);
        memset(vma, 0, sizeof(*vma));
        errno = ENOMEM;
        return -1;
    }
    vma->dirty_pages = calloc((size_t)page_count, sizeof(*vma->dirty_pages));
    if (!vma->dirty_pages) {
        free(vma->resident_pages);
        free(vma->page_flags);
        memset(vma, 0, sizeof(*vma));
        errno = ENOMEM;
        return -1;
    }
    for (uint64_t i = 0; i < page_count; i++) {
        vma->page_flags[i] = pf_flags;
        vma->resident_pages[i] = 1;
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
    if (vma->kind == TASK_VMA_ANON || vma->kind == TASK_VMA_FILE) {
        if (vma->shared_pages) {
            mm_shared_pages_put(vma->shared_pages, vma->page_count);
        } else if (vma->private_pages) {
            mm_private_pages_put(vma->private_pages, vma->page_count);
        } else if (vma->shared_mapping) {
            mm_shared_mapping_put(vma->shared_mapping);
        } else {
            free(vma->image);
        }
    }
    free(vma->page_flags);
    free(vma->resident_pages);
    free(vma->dirty_pages);
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

static int mm_copy_vma_slice(struct task_vma *source, uint64_t start, uint64_t end,
                             struct task_vma *dest) {
    uint64_t size;
    uint64_t page_count;
    size_t source_offset;

    if (!source || !dest || start < source->start ||
        end > source->end || start >= end) {
        errno = EINVAL;
        return -1;
    }
    size = end - start;
    if (size > SIZE_MAX) {
        errno = ENOMEM;
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

    memset(dest, 0, sizeof(*dest));
    dest->start = start;
    dest->end = end;
    dest->flags = source->flags;
    dest->kind = source->kind;
    dest->backing_fd = source->backing_fd;
    dest->backing_file_identity = source->backing_file_identity;
    memcpy(dest->backing_path, source->backing_path, sizeof(dest->backing_path));
    dest->backing_offset = source->backing_offset + (start - source->start);
    dest->shared = source->shared;
    dest->shared_mapping = source->shared_mapping;
    dest->shared_mapping_offset = source->shared_mapping_offset + (start - source->start);
    dest->image_size = (size_t)size;
    dest->page_count = page_count;
    if (source->shared_pages) {
        uint64_t first_page = (start - source->start) / TASK_VMA_PAGE_SIZE;
        dest->shared_pages = calloc((size_t)page_count, sizeof(*dest->shared_pages));
        if (!dest->shared_pages) {
            errno = ENOMEM;
            return -1;
        }
        for (uint64_t i = 0; i < page_count; i++) {
            dest->shared_pages[i] = source->shared_pages[first_page + i];
            mm_shared_mapping_get(dest->shared_pages[i]);
        }
        dest->image = dest->shared_pages[0] ? dest->shared_pages[0]->image : NULL;
    } else if (source->private_pages) {
        uint64_t first_page = (start - source->start) / TASK_VMA_PAGE_SIZE;
        dest->private_pages = calloc((size_t)page_count, sizeof(*dest->private_pages));
        if (!dest->private_pages) {
            errno = ENOMEM;
            return -1;
        }
        for (uint64_t i = 0; i < page_count; i++) {
            dest->private_pages[i] = source->private_pages[first_page + i];
            mm_private_page_get(dest->private_pages[i]);
        }
        dest->image = dest->private_pages[0] ? dest->private_pages[0]->image : NULL;
    } else if (dest->shared_mapping) {
        mm_shared_mapping_get(dest->shared_mapping);
        dest->image = dest->shared_mapping->image + dest->shared_mapping_offset;
    } else {
        dest->image = calloc(1, dest->image_size);
        if (!dest->image) {
            errno = ENOMEM;
            return -1;
        }
    }
    dest->page_flags = calloc((size_t)page_count, sizeof(*dest->page_flags));
    if (!dest->page_flags) {
        if (dest->shared_pages) {
            mm_shared_pages_put(dest->shared_pages, page_count);
        } else if (dest->private_pages) {
            mm_private_pages_put(dest->private_pages, page_count);
        } else if (dest->shared_mapping) {
            mm_shared_mapping_put(dest->shared_mapping);
        } else {
            free(dest->image);
        }
        memset(dest, 0, sizeof(*dest));
        errno = ENOMEM;
        return -1;
    }
    dest->resident_pages = calloc((size_t)page_count, sizeof(*dest->resident_pages));
    if (!dest->resident_pages) {
        free(dest->page_flags);
        if (dest->shared_pages) {
            mm_shared_pages_put(dest->shared_pages, page_count);
        } else if (dest->private_pages) {
            mm_private_pages_put(dest->private_pages, page_count);
        } else if (dest->shared_mapping) {
            mm_shared_mapping_put(dest->shared_mapping);
        } else {
            free(dest->image);
        }
        memset(dest, 0, sizeof(*dest));
        errno = ENOMEM;
        return -1;
    }
    dest->dirty_pages = calloc((size_t)page_count, sizeof(*dest->dirty_pages));
    if (!dest->dirty_pages) {
        free(dest->resident_pages);
        free(dest->page_flags);
        if (dest->shared_pages) {
            mm_shared_pages_put(dest->shared_pages, page_count);
        } else if (dest->private_pages) {
            mm_private_pages_put(dest->private_pages, page_count);
        } else if (dest->shared_mapping) {
            mm_shared_mapping_put(dest->shared_mapping);
        } else {
            free(dest->image);
        }
        memset(dest, 0, sizeof(*dest));
        errno = ENOMEM;
        return -1;
    }

    source_offset = (size_t)(start - source->start);
    if (!dest->shared_mapping && !dest->shared_pages && !dest->private_pages) {
        memcpy(dest->image, (const unsigned char *)source->image + source_offset, dest->image_size);
    }
    for (uint64_t i = 0; i < page_count; i++) {
        uint64_t source_page = ((start - source->start) / TASK_VMA_PAGE_SIZE) + i;
        dest->page_flags[i] = task_vma_page_flags_impl(source, start + (i * TASK_VMA_PAGE_SIZE));
        if (source->resident_pages && source_page < source->page_count) {
            dest->resident_pages[i] = source->resident_pages[source_page];
        }
        if (source->dirty_pages && source_page < source->page_count) {
            dest->dirty_pages[i] = source->dirty_pages[source_page];
        }
    }
    return 0;
}

static int mm_extend_copied_vma(struct task_vma *vma, uint64_t new_len) {
    uint64_t old_len;
    uint64_t old_pages;
    uint64_t new_pages;
    uint32_t *page_flags;
    uint8_t *resident_pages;
    uint8_t *dirty_pages;
    void *new_image = NULL;
    struct vm_private_page **private_pages = NULL;
    struct vm_shared_mapping **shared_pages = NULL;

    if (!vma || new_len == 0 || new_len > SIZE_MAX) {
        errno = ENOMEM;
        return -1;
    }
    old_len = vma->end - vma->start;
    if (new_len <= old_len) {
        return 0;
    }
    old_pages = vma->page_count;
    new_pages = new_len / TASK_VMA_PAGE_SIZE;
    if ((new_len % TASK_VMA_PAGE_SIZE) != 0) {
        new_pages++;
    }
    if (new_pages <= old_pages || new_pages > SIZE_MAX / sizeof(uint32_t)) {
        errno = ENOMEM;
        return -1;
    }

    page_flags = calloc((size_t)new_pages, sizeof(*page_flags));
    resident_pages = calloc((size_t)new_pages, sizeof(*resident_pages));
    dirty_pages = calloc((size_t)new_pages, sizeof(*dirty_pages));
    if (!page_flags || !resident_pages || !dirty_pages) {
        free(page_flags);
        free(resident_pages);
        free(dirty_pages);
        errno = ENOMEM;
        return -1;
    }
    memcpy(page_flags, vma->page_flags, (size_t)old_pages * sizeof(*page_flags));
    memcpy(resident_pages, vma->resident_pages, (size_t)old_pages * sizeof(*resident_pages));
    memcpy(dirty_pages, vma->dirty_pages, (size_t)old_pages * sizeof(*dirty_pages));
    for (uint64_t i = old_pages; i < new_pages; i++) {
        page_flags[i] = vma->flags;
        resident_pages[i] = 1;
    }

    if (vma->private_pages) {
        private_pages = calloc((size_t)new_pages, sizeof(*private_pages));
        if (!private_pages) {
            free(page_flags);
            free(resident_pages);
            free(dirty_pages);
            errno = ENOMEM;
            return -1;
        }
        memcpy(private_pages, vma->private_pages, (size_t)old_pages * sizeof(*private_pages));
        for (uint64_t i = old_pages; i < new_pages; i++) {
            private_pages[i] = mm_private_page_alloc(NULL, 0);
            if (!private_pages[i]) {
                for (uint64_t j = old_pages; j < i; j++) {
                    mm_private_page_put_impl(private_pages[j]);
                }
                free(private_pages);
                free(page_flags);
                free(resident_pages);
                free(dirty_pages);
                return -1;
            }
        }
    } else if (vma->shared_pages && vma->backing_fd >= 0) {
        shared_pages = calloc((size_t)new_pages, sizeof(*shared_pages));
        if (!shared_pages) {
            free(page_flags);
            free(resident_pages);
            free(dirty_pages);
            errno = ENOMEM;
            return -1;
        }
        memcpy(shared_pages, vma->shared_pages, (size_t)old_pages * sizeof(*shared_pages));
        for (uint64_t i = old_pages; i < new_pages; i++) {
            uint64_t page_offset = vma->backing_offset + i * TASK_VMA_PAGE_SIZE;
            struct vm_shared_mapping **page =
                mm_shared_pages_get_or_create(vma->backing_fd, page_offset, 1);
            if (!page) {
                for (uint64_t j = old_pages; j < i; j++) {
                    mm_shared_mapping_put(shared_pages[j]);
                }
                free(shared_pages);
                free(page_flags);
                free(resident_pages);
                free(dirty_pages);
                return -1;
            }
            shared_pages[i] = page[0];
            free(page);
        }
    } else if (vma->shared_mapping) {
        free(page_flags);
        free(resident_pages);
        free(dirty_pages);
        errno = ENOSYS;
        return -1;
    } else {
        new_image = calloc(1, (size_t)new_len);
        if (!new_image) {
            free(page_flags);
            free(resident_pages);
            free(dirty_pages);
            errno = ENOMEM;
            return -1;
        }
        memcpy(new_image, vma->image, (size_t)old_len);
    }

    free(vma->page_flags);
    free(vma->resident_pages);
    free(vma->dirty_pages);
    vma->page_flags = page_flags;
    vma->resident_pages = resident_pages;
    vma->dirty_pages = dirty_pages;
    if (private_pages) {
        free(vma->private_pages);
        vma->private_pages = private_pages;
    } else if (shared_pages) {
        free(vma->shared_pages);
        vma->shared_pages = shared_pages;
        vma->image = shared_pages[0] ? shared_pages[0]->image : NULL;
    } else if (new_image) {
        free(vma->image);
        vma->image = new_image;
    }
    vma->end = vma->start + new_len;
    vma->image_size = (size_t)new_len;
    vma->page_count = new_pages;
    return 0;
}

static void *mm_dup_bytes(const void *source, size_t size) {
    void *copy;

    if (!source || size == 0) {
        return NULL;
    }
    copy = malloc(size);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, source, size);
    return copy;
}

struct mm_struct *task_mm_dup_impl(const struct mm_struct *source) {
    struct mm_struct *copy;

    if (!source) {
        return NULL;
    }
    copy = calloc(1, sizeof(*copy));
    if (!copy) {
        errno = ENOMEM;
        return NULL;
    }
    atomic_init(&copy->refs, 1);
    copy->exec_entry = source->exec_entry;
    copy->exec_dynamic_vaddr = source->exec_dynamic_vaddr;
    copy->exec_dynamic_size = source->exec_dynamic_size;
    copy->exec_dynamic = source->exec_dynamic;
    copy->interp_entry = source->interp_entry;
    copy->interp_dynamic_vaddr = source->interp_dynamic_vaddr;
    copy->interp_dynamic_size = source->interp_dynamic_size;
    copy->interp_dynamic = source->interp_dynamic;
    memcpy(copy->interp_path, source->interp_path, sizeof(copy->interp_path));
    copy->entry_point = source->entry_point;
    copy->initial_stack_base = source->initial_stack_base;
    copy->initial_stack_size = source->initial_stack_size;
    copy->initial_stack_pointer = source->initial_stack_pointer;
    copy->initial_argc = source->initial_argc;
    copy->initial_envc = source->initial_envc;
    memcpy(copy->initial_argv, source->initial_argv, sizeof(copy->initial_argv));
    memcpy(copy->initial_envp, source->initial_envp, sizeof(copy->initial_envp));
    copy->auxv_random_addr = source->auxv_random_addr;
    copy->auxv_platform_addr = source->auxv_platform_addr;
    copy->auxv_execfn_addr = source->auxv_execfn_addr;
    memcpy(copy->auxv, source->auxv, sizeof(copy->auxv));
    copy->auxv_count = source->auxv_count;
    copy->handoff = source->handoff;
    copy->vma_addr_space = source->vma_addr_space;
    copy->brk_start = source->brk_start;
    copy->brk_current = source->brk_current;
    copy->signal_frame_sp = source->signal_frame_sp;
    copy->signal_frame_signo = source->signal_frame_signo;
    copy->signal_frame_return_pc = source->signal_frame_return_pc;
    copy->signal_handler_pc = source->signal_handler_pc;
    copy->signal_frame_flags = source->signal_frame_flags;
    copy->signal_frame_restorer_pc = source->signal_frame_restorer_pc;
    copy->signal_frame_mask = source->signal_frame_mask;
    copy->signal_frame_altstack_sp = source->signal_frame_altstack_sp;
    copy->signal_frame_altstack_size = source->signal_frame_altstack_size;
    copy->signal_frame_altstack_flags = source->signal_frame_altstack_flags;
    copy->signal_frame_current_sp = source->signal_frame_current_sp;
    copy->signal_frame_size = source->signal_frame_size;
    copy->signal_frame_ucontext_flags = source->signal_frame_ucontext_flags;

    copy->exec_image_size = source->exec_image_size;
    copy->exec_image_base = mm_dup_bytes(source->exec_image_base, source->exec_image_size);
    if (source->exec_image_base && !copy->exec_image_base) {
        goto oom;
    }
    copy->interp_image_size = source->interp_image_size;
    copy->interp_image_base = mm_dup_bytes(source->interp_image_base, source->interp_image_size);
    if (source->interp_image_base && !copy->interp_image_base) {
        goto oom;
    }
    copy->stack_guard_image = mm_dup_bytes(source->stack_guard_image, TASK_VMA_PAGE_SIZE);
    if (source->stack_guard_image && !copy->stack_guard_image) {
        goto oom;
    }
    copy->initial_stack_image_size = source->initial_stack_image_size;
    copy->initial_stack_image = mm_dup_bytes(source->initial_stack_image,
                                            source->initial_stack_image_size);
    if (source->initial_stack_image && !copy->initial_stack_image) {
        goto oom;
    }

    copy->exec_segment_count = source->exec_segment_count;
    for (uint32_t i = 0; i < source->exec_segment_count; i++) {
        copy->exec_segments[i] = source->exec_segments[i];
        copy->exec_segments[i].image = mm_dup_bytes(source->exec_segments[i].image,
                                                    source->exec_segments[i].image_size);
        if (source->exec_segments[i].image && !copy->exec_segments[i].image) {
            goto oom;
        }
    }
    copy->interp_segment_count = source->interp_segment_count;
    for (uint32_t i = 0; i < source->interp_segment_count; i++) {
        copy->interp_segments[i] = source->interp_segments[i];
        copy->interp_segments[i].image = mm_dup_bytes(source->interp_segments[i].image,
                                                      source->interp_segments[i].image_size);
        if (source->interp_segments[i].image && !copy->interp_segments[i].image) {
            goto oom;
        }
    }

    for (uint32_t i = 0; i < source->vma_count; i++) {
        struct task_vma *source_vma = (struct task_vma *)&source->vmas[i];

        if (mm_private_vma_enable_cow(source_vma) != 0) {
            goto oom;
        }
        if (mm_copy_vma_slice(source_vma, source_vma->start, source_vma->end,
                              &copy->vmas[copy->vma_count]) != 0) {
            goto oom;
        }
        copy->vma_count++;
    }
    return copy;

oom:
    task_mm_put_impl(copy);
    errno = ENOMEM;
    return NULL;
}

static void mm_free_vma_contents(struct task_vma *vma) {
    if (!vma) {
        return;
    }
    if (vma->kind == TASK_VMA_ANON || vma->kind == TASK_VMA_FILE) {
        if (vma->shared_pages) {
            mm_shared_pages_put(vma->shared_pages, vma->page_count);
        } else if (vma->private_pages) {
            mm_private_pages_put(vma->private_pages, vma->page_count);
        } else if (vma->shared_mapping) {
            mm_shared_mapping_put(vma->shared_mapping);
        } else {
            free(vma->image);
        }
    }
    free(vma->page_flags);
    free(vma->resident_pages);
    free(vma->dirty_pages);
    memset(vma, 0, sizeof(*vma));
}

static int mm_replace_vma_with_slices(struct mm_struct *mm, uint32_t index,
                                      const struct task_vma *left,
                                      const struct task_vma *right) {
    int has_left = left && left->start < left->end;
    int has_right = right && right->start < right->end;
    uint32_t replacement_count = (has_left ? 1U : 0U) + (has_right ? 1U : 0U);

    if (!mm || index >= mm->vma_count) {
        errno = EINVAL;
        return -1;
    }
    if (mm->vma_count - 1 + replacement_count > TASK_EXEC_MAX_VMAS) {
        errno = ENOMEM;
        return -1;
    }

    mm_free_vma_contents(&mm->vmas[index]);
    if (replacement_count == 0) {
        for (uint32_t i = index + 1; i < mm->vma_count; i++) {
            mm->vmas[i - 1] = mm->vmas[i];
        }
        mm->vma_count--;
        memset(&mm->vmas[mm->vma_count], 0, sizeof(mm->vmas[0]));
        return 0;
    }

    if (replacement_count == 2) {
        for (uint32_t i = mm->vma_count; i > index + 1; i--) {
            mm->vmas[i] = mm->vmas[i - 1];
        }
        mm->vma_count++;
        mm->vmas[index] = *left;
        mm->vmas[index + 1] = *right;
        return 0;
    }

    mm->vmas[index] = has_left ? *left : *right;
    return 0;
}

static int mm_insert_vma_sorted(struct mm_struct *mm, const struct task_vma *vma) {
    uint32_t index = 0;

    if (!mm || !vma || vma->start >= vma->end) {
        errno = EINVAL;
        return -1;
    }
    if (mm->vma_count >= TASK_EXEC_MAX_VMAS) {
        errno = ENOMEM;
        return -1;
    }
    while (index < mm->vma_count && mm->vmas[index].start < vma->start) {
        index++;
    }
    for (uint32_t i = mm->vma_count; i > index; i--) {
        mm->vmas[i] = mm->vmas[i - 1];
    }
    mm->vmas[index] = *vma;
    mm->vma_count++;
    return 0;
}

static int mm_remove_exact_vma(struct mm_struct *mm, uint64_t start, uint64_t end) {
    if (!mm) {
        errno = EINVAL;
        return -1;
    }
    for (uint32_t i = 0; i < mm->vma_count; i++) {
        if (mm->vmas[i].start == start && mm->vmas[i].end == end) {
            mm_remove_vma_at(mm, i);
            return 0;
        }
    }
    errno = ENOENT;
    return -1;
}

static int mm_unmap_vma_range(struct mm_struct *mm, uint32_t index,
                              uint64_t start, uint64_t end) {
    struct task_vma source;
    struct task_vma left;
    struct task_vma right;
    int has_left;
    int has_right;

    if (!mm || index >= mm->vma_count || start >= end) {
        errno = EINVAL;
        return -1;
    }
    source = mm->vmas[index];
    if (start <= source.start && end >= source.end) {
        mm_remove_vma_at(mm, index);
        return 0;
    }

    memset(&left, 0, sizeof(left));
    memset(&right, 0, sizeof(right));
    has_left = start > source.start;
    has_right = end < source.end;
    if (has_left && mm_copy_vma_slice(&source, source.start, start, &left) != 0) {
        return -1;
    }
    if (has_right && mm_copy_vma_slice(&source, end, source.end, &right) != 0) {
        if (has_left) {
            mm_free_vma_contents(&left);
        }
        return -1;
    }
    if (mm_replace_vma_with_slices(mm, index, has_left ? &left : NULL, has_right ? &right : NULL) != 0) {
        if (has_left) {
            mm_free_vma_contents(&left);
        }
        if (has_right) {
            mm_free_vma_contents(&right);
        }
        return -1;
    }
    return 0;
}

void *mmap_impl(void *addr, size_t length, int prot, int flags, int fd, int64_t offset) {
    struct task_struct *task = get_current();
    uint64_t map_len;
    uint64_t map_addr;
    enum task_vma_kind kind;
    void *image;
    struct vm_shared_mapping *shared_mapping = NULL;
    struct vm_shared_mapping **shared_pages = NULL;
    long long bytes;

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
        atomic_init(&task->mm->refs, 1);
    }
    if (length == 0 || mm_validate_prot(prot) != 0) {
        if (length == 0) {
            errno = EINVAL;
        }
        return (void *)-1;
    }
    if (mm_validate_mmap_flags(flags, fd) != 0) {
        return (void *)-1;
    }
    if ((flags & MAP_ANONYMOUS) == 0 && ((uint64_t)offset % TASK_VMA_PAGE_SIZE) != 0) {
        errno = EINVAL;
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
            map_len > UINT64_MAX - map_addr) {
            errno = EINVAL;
            return (void *)-1;
        }
        if ((flags & MAP_FIXED_NOREPLACE) != 0 &&
            mm_range_overlaps(task->mm, map_addr, map_addr + map_len)) {
            errno = EEXIST;
            return (void *)-1;
        }
    } else {
        map_addr = mm_alloc_addr(task->mm, map_len);
        if (map_addr == 0) {
            errno = ENOMEM;
            return (void *)-1;
        }
    }

    if ((flags & MAP_FIXED) != 0 &&
        munmap_impl((void *)(uintptr_t)map_addr, (size_t)map_len) != 0) {
        return (void *)-1;
    }

    kind = (flags & MAP_ANONYMOUS) != 0 ? TASK_VMA_ANON : TASK_VMA_FILE;
    int synthetic_zero = kind == TASK_VMA_FILE && mm_fd_is_synthetic_zero(fd);
    if (kind == TASK_VMA_FILE && (flags & MAP_SHARED) != 0 && !synthetic_zero) {
        shared_pages = mm_shared_pages_get_or_create(fd, (uint64_t)offset, map_len / TASK_VMA_PAGE_SIZE);
        if (!shared_pages) {
            return (void *)-1;
        }
        image = shared_pages[0] ? shared_pages[0]->image : NULL;
    } else {
        image = calloc(1, (size_t)map_len);
        if (!image) {
            errno = ENOMEM;
            return (void *)-1;
        }
    }
    if (kind == TASK_VMA_FILE && !shared_pages && !synthetic_zero) {
        bytes = mm_fd_pread(fd, image, (size_t)map_len, (long long)offset);
        if (bytes < 0) {
            int saved_errno = errno;
            free(image);
            errno = saved_errno;
            return (void *)-1;
        }
    }
    if (mm_add_vma(task->mm, map_addr, map_len, mm_prot_to_pf(prot), kind, image) != 0) {
        int saved_errno = errno;
        if (shared_pages) {
            mm_shared_pages_put(shared_pages, map_len / TASK_VMA_PAGE_SIZE);
        } else {
            free(image);
        }
        errno = saved_errno;
        return (void *)-1;
    }
    if (kind == TASK_VMA_FILE) {
        struct task_vma *vma = task_find_vma_mutable_impl(task, map_addr);
        if (vma) {
            vma->backing_fd = fd;
            vma->backing_file_identity = mm_fd_file_identity(fd);
            {
                void *entry = get_fd_entry_impl(fd);
                if (entry) {
                    if (get_fd_path_impl(entry, vma->backing_path, sizeof(vma->backing_path)) != 0) {
                        vma->backing_path[0] = '\0';
                    }
                    put_fd_entry_impl(entry);
                }
            }
            vma->backing_offset = (uint64_t)offset;
            vma->shared = (flags & MAP_SHARED) != 0;
            vma->shared_mapping = shared_mapping;
            vma->shared_pages = shared_pages;
            vma->shared_mapping_offset = 0;
        }
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
    int unmapped = 0;

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
    for (uint32_t i = 0; i < task->mm->vma_count;) {
        struct task_vma *vma = &task->mm->vmas[i];
        uint64_t overlap_start;
        uint64_t overlap_end;

        if (end <= vma->start || start >= vma->end) {
            i++;
            continue;
        }
        overlap_start = start > vma->start ? start : vma->start;
        overlap_end = end < vma->end ? end : vma->end;
        if (overlap_start < overlap_end) {
            if (mm_unmap_vma_range(task->mm, i, overlap_start, overlap_end) != 0) {
                return -1;
            }
            unmapped = 1;
            continue;
        }
        i++;
    }
    (void)unmapped;
    return 0;
}

static int mm_load_private_file_page(const struct task_vma *vma, uint64_t page_index,
                                     uint8_t page_image[TASK_VMA_PAGE_SIZE]) {
    uint64_t image_offset;
    uint64_t file_offset;
    size_t image_remaining;
    size_t source_len = TASK_VMA_PAGE_SIZE;
    long long bytes = -1;

    if (!vma || !page_image || vma->kind != TASK_VMA_FILE || vma->shared ||
        page_index >= vma->page_count) {
        errno = EINVAL;
        return -1;
    }

    memset(page_image, 0, TASK_VMA_PAGE_SIZE);
    image_offset = page_index * TASK_VMA_PAGE_SIZE;
    file_offset = vma->backing_offset + image_offset;
    if (vma->backing_fd >= 0 || vma->backing_path[0] != '\0') {
        bytes = mm_vma_pread(vma, page_image, TASK_VMA_PAGE_SIZE, (long long)file_offset);
        if (bytes < 0 && errno != ENXIO) {
            return -1;
        }
        if (bytes >= 0) {
            source_len = (size_t)bytes;
        }
    } else if (vma->image && image_offset < vma->image_size) {
        image_remaining = vma->image_size - (size_t)image_offset;
        if (image_remaining < source_len) {
            source_len = image_remaining;
        }
        memcpy(page_image, (const unsigned char *)vma->image + image_offset, source_len);
    } else {
        source_len = 0;
    }
    (void)source_len;
    return 0;
}

static int mm_restore_private_file_page(struct task_vma *vma, uint64_t page_index) {
    uint8_t page_image[TASK_VMA_PAGE_SIZE];
    struct vm_private_page *page;

    if (mm_load_private_file_page(vma, page_index, page_image) != 0) {
        return -1;
    }
    page = mm_private_page_alloc(page_image, TASK_VMA_PAGE_SIZE);
    if (!page) {
        return -1;
    }
    mm_private_page_put_impl(vma->private_pages[page_index]);
    vma->private_pages[page_index] = page;
    if (page_index == 0) {
        vma->image = page->image;
    }
    return 0;
}

static int mm_restore_private_file_image_page(struct task_vma *vma, uint64_t page_index) {
    uint8_t page_image[TASK_VMA_PAGE_SIZE];
    size_t image_offset;
    size_t to_copy;

    if (mm_load_private_file_page(vma, page_index, page_image) != 0) {
        return -1;
    }
    if (!vma->image) {
        errno = EINVAL;
        return -1;
    }
    image_offset = (size_t)(page_index * TASK_VMA_PAGE_SIZE);
    if (image_offset >= vma->image_size) {
        errno = EINVAL;
        return -1;
    }
    to_copy = vma->image_size - image_offset;
    if (to_copy > TASK_VMA_PAGE_SIZE) {
        to_copy = TASK_VMA_PAGE_SIZE;
    }
    memcpy((unsigned char *)vma->image + image_offset, page_image, to_copy);
    return 0;
}

static int mm_zero_vma_range(struct task_vma *vma, uint64_t start, uint64_t end) {
    uint64_t cursor = start;

    while (cursor < end) {
        uint64_t offset = cursor - vma->start;
        uint64_t page_index = offset / TASK_VMA_PAGE_SIZE;
        size_t page_offset = (size_t)(offset % TASK_VMA_PAGE_SIZE);
        size_t to_zero = (size_t)(end - cursor);

        if (to_zero > TASK_VMA_PAGE_SIZE - page_offset) {
            to_zero = TASK_VMA_PAGE_SIZE - page_offset;
        }
        if (vma->shared_pages && page_index < vma->page_count && vma->shared_pages[page_index]) {
            memset(vma->shared_pages[page_index]->image + page_offset, 0, to_zero);
        } else if (vma->private_pages && page_index < vma->page_count && vma->private_pages[page_index]) {
            uint8_t zero_page[TASK_VMA_PAGE_SIZE] = {0};

            if (vma->kind == TASK_VMA_FILE && !vma->shared &&
                page_offset == 0 && to_zero == TASK_VMA_PAGE_SIZE) {
                if (mm_restore_private_file_page(vma, page_index) != 0) {
                    return -1;
                }
            } else if (mm_private_vma_write_impl(vma, cursor, zero_page, to_zero) != (long)to_zero) {
                return -1;
            }
        } else if (vma->image) {
            if (vma->kind == TASK_VMA_FILE && !vma->shared &&
                page_offset == 0 && to_zero == TASK_VMA_PAGE_SIZE) {
                if (mm_restore_private_file_image_page(vma, page_index) != 0) {
                    return -1;
                }
            } else {
                memset((unsigned char *)vma->image + offset, 0, to_zero);
            }
        }
        if (vma->dirty_pages && page_index < vma->page_count) {
            vma->dirty_pages[page_index] = 0;
        }
        if (vma->resident_pages && page_index < vma->page_count) {
            vma->resident_pages[page_index] = 0;
        }
        cursor += to_zero;
    }
    return 0;
}

int madvise_impl(void *addr, size_t length, int advice) {
    struct task_struct *task = get_current();
    uint64_t start = (uint64_t)(uintptr_t)addr;
    uint64_t size;
    uint64_t end;

    if (!task || !task->mm) {
        errno = ENOMEM;
        return -1;
    }
    if (length == 0) {
        return 0;
    }
    if ((start % TASK_VMA_PAGE_SIZE) != 0 || mm_validate_madvise(advice) != 0) {
        if ((start % TASK_VMA_PAGE_SIZE) != 0) {
            errno = EINVAL;
        }
        return -1;
    }
    size = mm_align_up((uint64_t)length, TASK_VMA_PAGE_SIZE);
    if (size == 0 || size > UINT64_MAX - start) {
        errno = ENOMEM;
        return -1;
    }
    if (advice != MADV_DONTNEED) {
        return 0;
    }
    end = start + size;
    for (uint32_t i = 0; i < task->mm->vma_count; i++) {
        struct task_vma *vma = &task->mm->vmas[i];
        uint64_t overlap_start;
        uint64_t overlap_end;

        if (end <= vma->start || start >= vma->end) {
            continue;
        }
        overlap_start = start > vma->start ? start : vma->start;
        overlap_end = end < vma->end ? end : vma->end;
        if (mm_zero_vma_range(vma, overlap_start, overlap_end) != 0) {
            return -1;
        }
    }
    return 0;
}

int mincore_impl(void *addr, size_t length, unsigned char *vec) {
    struct task_struct *task = get_current();
    uint64_t start = (uint64_t)(uintptr_t)addr;
    uint64_t size;
    uint64_t page_count;

    if (!task || !task->mm) {
        errno = ENOMEM;
        return -1;
    }
    if (!vec) {
        errno = EFAULT;
        return -1;
    }
    if (length == 0) {
        return 0;
    }
    if ((start % TASK_VMA_PAGE_SIZE) != 0) {
        errno = EINVAL;
        return -1;
    }
    size = mm_align_up((uint64_t)length, TASK_VMA_PAGE_SIZE);
    if (size == 0 || size > UINT64_MAX - start) {
        errno = ENOMEM;
        return -1;
    }
    page_count = size / TASK_VMA_PAGE_SIZE;

    for (uint64_t page = 0; page < page_count; page++) {
        uint64_t addr_for_page = start + (page * TASK_VMA_PAGE_SIZE);
        const struct task_vma *vma = task_find_vma_impl(task, addr_for_page);
        uint64_t page_index;

        if (!vma) {
            errno = ENOMEM;
            return -1;
        }
        page_index = (addr_for_page - vma->start) / TASK_VMA_PAGE_SIZE;
        if (page_index >= vma->page_count) {
            errno = ENOMEM;
            return -1;
        }
        if (vma->kind == TASK_VMA_FILE &&
            mm_shared_file_remaining(vma, (size_t)(page_index * TASK_VMA_PAGE_SIZE)) < 0 &&
            errno == ENXIO) {
            vec[page] = 0U;
            errno = 0;
            continue;
        }
        vec[page] = (!vma->resident_pages || vma->resident_pages[page_index]) ? 1U : 0U;
    }

    return 0;
}

void *mremap_impl(void *old_address, size_t old_size, size_t new_size, int flags, void *new_address) {
    struct task_struct *task = get_current();
    uint64_t old_start = (uint64_t)(uintptr_t)old_address;
    uint64_t new_start = (uint64_t)(uintptr_t)new_address;
    uint64_t old_len;
    uint64_t new_len;
    const struct task_vma *found;
    uint32_t vma_flags;
    void *mapped;
    size_t copy_len;

    errno = 0;
    if (!task || !task->mm) {
        errno = EFAULT;
        return (void *)-1;
    }
    if ((flags & ~(MREMAP_MAYMOVE | MREMAP_FIXED)) != 0 ||
        ((flags & MREMAP_FIXED) != 0 && (flags & MREMAP_MAYMOVE) == 0)) {
        errno = EINVAL;
        return (void *)-1;
    }
    if (old_size == 0 || new_size == 0 || (old_start % TASK_VMA_PAGE_SIZE) != 0) {
        errno = EINVAL;
        return (void *)-1;
    }
    old_len = mm_align_up((uint64_t)old_size, TASK_VMA_PAGE_SIZE);
    new_len = mm_align_up((uint64_t)new_size, TASK_VMA_PAGE_SIZE);
    if (old_len == 0 || new_len == 0 || old_len > UINT64_MAX - old_start) {
        errno = ENOMEM;
        return (void *)-1;
    }
    found = task_find_vma_impl(task, old_start);
    if (!found || old_start + old_len > found->end) {
        errno = EFAULT;
        return (void *)-1;
    }
    if ((flags & MREMAP_FIXED) != 0 &&
        (new_start == 0 || (new_start % TASK_VMA_PAGE_SIZE) != 0 ||
         new_len > UINT64_MAX - new_start)) {
        errno = EINVAL;
        return (void *)-1;
    }
    if ((flags & MREMAP_FIXED) != 0 &&
        new_start < old_start + old_len && new_start + new_len > old_start) {
        errno = EINVAL;
        return (void *)-1;
    }
    if ((flags & MREMAP_FIXED) != 0 && old_len == new_len) {
        struct task_vma moved_vma;

        if (mm_copy_vma_slice((struct task_vma *)found, old_start, old_start + old_len, &moved_vma) != 0) {
            return (void *)-1;
        }
        moved_vma.start = new_start;
        moved_vma.end = new_start + new_len;
        if (munmap_impl(new_address, (size_t)new_len) != 0) {
            int saved_errno = errno;
            mm_free_vma_contents(&moved_vma);
            errno = saved_errno;
            return (void *)-1;
        }
        if (mm_insert_vma_sorted(task->mm, &moved_vma) != 0) {
            int saved_errno = errno;
            mm_free_vma_contents(&moved_vma);
            errno = saved_errno;
            return (void *)-1;
        }
        if (munmap_impl(old_address, (size_t)old_len) != 0) {
            int saved_errno = errno;
            mm_remove_exact_vma(task->mm, new_start, new_start + new_len);
            errno = saved_errno;
            return (void *)-1;
        }
        return new_address;
    }
    if ((flags & MREMAP_FIXED) != 0 && new_len > old_len) {
        struct task_vma moved_vma;

        if (mm_copy_vma_slice((struct task_vma *)found, old_start, old_start + old_len, &moved_vma) != 0) {
            return (void *)-1;
        }
        moved_vma.start = new_start;
        moved_vma.end = new_start + old_len;
        if (mm_extend_copied_vma(&moved_vma, new_len) != 0) {
            int saved_errno = errno;
            mm_free_vma_contents(&moved_vma);
            errno = saved_errno;
            return (void *)-1;
        }
        if (munmap_impl(new_address, (size_t)new_len) != 0) {
            int saved_errno = errno;
            mm_free_vma_contents(&moved_vma);
            errno = saved_errno;
            return (void *)-1;
        }
        if (mm_insert_vma_sorted(task->mm, &moved_vma) != 0) {
            int saved_errno = errno;
            mm_free_vma_contents(&moved_vma);
            errno = saved_errno;
            return (void *)-1;
        }
        if (munmap_impl(old_address, (size_t)old_len) != 0) {
            int saved_errno = errno;
            mm_remove_exact_vma(task->mm, new_start, new_start + new_len);
            errno = saved_errno;
            return (void *)-1;
        }
        return new_address;
    }
    if (new_len <= old_len) {
        if (new_len < old_len &&
            munmap_impl((void *)(uintptr_t)(old_start + new_len), (size_t)(old_len - new_len)) != 0) {
            return (void *)-1;
        }
        return old_address;
    }
    if ((flags & MREMAP_FIXED) == 0 &&
        found->end == old_start + old_len &&
        !mm_range_overlaps_except(task->mm, old_start, old_start + new_len,
                                  (uint32_t)(found - task->mm->vmas))) {
        struct task_vma *vma = task_find_vma_mutable_impl(task, old_start);
        uint64_t added_pages = (new_len - old_len) / TASK_VMA_PAGE_SIZE;
        uint64_t old_pages = vma->page_count;

        uint64_t new_pages = old_pages + added_pages;
        uint32_t *page_flags = calloc((size_t)new_pages, sizeof(*page_flags));
        uint8_t *resident_pages = calloc((size_t)new_pages, sizeof(*resident_pages));
        uint8_t *dirty_pages = calloc((size_t)new_pages, sizeof(*dirty_pages));
        void *new_image = NULL;
        struct vm_private_page **private_pages = NULL;
        struct vm_shared_mapping **shared_pages = NULL;

        if (!page_flags || !resident_pages || !dirty_pages) {
            free(page_flags);
            free(resident_pages);
            free(dirty_pages);
            errno = ENOMEM;
            return (void *)-1;
        }
        memcpy(page_flags, vma->page_flags, (size_t)old_pages * sizeof(*page_flags));
        memcpy(resident_pages, vma->resident_pages, (size_t)old_pages * sizeof(*resident_pages));
        memcpy(dirty_pages, vma->dirty_pages, (size_t)old_pages * sizeof(*dirty_pages));
        for (uint64_t i = old_pages; i < new_pages; i++) {
            page_flags[i] = vma->flags;
            resident_pages[i] = 1;
        }
        if (!vma->private_pages && !vma->shared_pages && !vma->shared_mapping) {
            new_image = calloc(1, (size_t)new_len);
            if (!new_image) {
                free(page_flags);
                free(resident_pages);
                free(dirty_pages);
                errno = ENOMEM;
                return (void *)-1;
            }
            memcpy(new_image, vma->image, (size_t)old_len);
        } else if (vma->private_pages) {
            private_pages = calloc((size_t)new_pages, sizeof(*private_pages));
            if (!private_pages) {
                free(page_flags);
                free(resident_pages);
                free(dirty_pages);
                errno = ENOMEM;
                return (void *)-1;
            }
            memcpy(private_pages, vma->private_pages, (size_t)old_pages * sizeof(*private_pages));
            for (uint64_t i = old_pages; i < new_pages; i++) {
                private_pages[i] = mm_private_page_alloc(NULL, 0);
                if (!private_pages[i]) {
                    for (uint64_t j = old_pages; j < i; j++) {
                        mm_private_page_put_impl(private_pages[j]);
                    }
                    free(private_pages);
                    free(page_flags);
                    free(resident_pages);
                    free(dirty_pages);
                    return (void *)-1;
                }
            }
        } else if (vma->shared_pages && vma->backing_fd >= 0) {
            shared_pages = calloc((size_t)new_pages, sizeof(*shared_pages));
            if (!shared_pages) {
                free(page_flags);
                free(resident_pages);
                free(dirty_pages);
                errno = ENOMEM;
                return (void *)-1;
            }
            memcpy(shared_pages, vma->shared_pages, (size_t)old_pages * sizeof(*shared_pages));
            for (uint64_t i = old_pages; i < new_pages; i++) {
                uint64_t page_offset = vma->backing_offset + i * TASK_VMA_PAGE_SIZE;
                struct vm_shared_mapping **page =
                    mm_shared_pages_get_or_create(vma->backing_fd, page_offset, 1);
                if (!page) {
                    for (uint64_t j = old_pages; j < i; j++) {
                        mm_shared_mapping_put(shared_pages[j]);
                    }
                    free(shared_pages);
                   free(page_flags);
                    free(resident_pages);
                    free(dirty_pages);
                    return (void *)-1;
                }
                shared_pages[i] = page[0];
                free(page);
            }
        } else {
           free(page_flags);
            free(resident_pages);
            free(dirty_pages);
            errno = ENOSYS;
            return (void *)-1;
        }
        free(vma->page_flags);
        free(vma->resident_pages);
        free(vma->dirty_pages);
        vma->page_flags = page_flags;
        vma->resident_pages = resident_pages;
        vma->dirty_pages = dirty_pages;
        if (private_pages) {
            free(vma->private_pages);
            vma->private_pages = private_pages;
        } else if (shared_pages) {
            free(vma->shared_pages);
            vma->shared_pages = shared_pages;
            vma->image = shared_pages[0] ? shared_pages[0]->image : NULL;
        } else {
            free(vma->image);
            vma->image = new_image;
        }
        for (uint64_t i = old_pages; i < old_pages + added_pages; i++) {
            vma->dirty_pages[i] = 0;
        }
        vma->end = old_start + new_len;
        vma->image_size = (size_t)new_len;
        vma->page_count = old_pages + added_pages;
        return old_address;
    }
    if ((flags & MREMAP_MAYMOVE) == 0) {
        errno = ENOMEM;
        return (void *)-1;
    }
    vma_flags = found->flags;
    mapped = mmap_impl((flags & MREMAP_FIXED) ? new_address : NULL, (size_t)new_len,
                       ((vma_flags & PF_R) ? PROT_READ : 0) |
                       ((vma_flags & PF_W) ? PROT_WRITE : 0) |
                       ((vma_flags & PF_X) ? PROT_EXEC : 0),
                       MAP_PRIVATE | MAP_ANONYMOUS | ((flags & MREMAP_FIXED) ? MAP_FIXED : 0),
                       -1, 0);
    if ((long)(uintptr_t)mapped < 0) {
        return (void *)-1;
    }
    copy_len = old_size < new_size ? old_size : new_size;
    if (copy_len > 0) {
        void *tmp = malloc(copy_len);
        if (!tmp) {
            munmap_impl(mapped, new_len);
            errno = ENOMEM;
            return (void *)-1;
        }
        if (task_read_virtual_memory_impl(task, old_start, tmp, copy_len) != (long)copy_len ||
            task_write_virtual_memory_impl(task, (uint64_t)(uintptr_t)mapped, tmp, copy_len) != (long)copy_len) {
            int saved_errno = errno;
            free(tmp);
            munmap_impl(mapped, new_len);
            errno = saved_errno;
            return (void *)-1;
        }
        free(tmp);
    }
    if (munmap_impl(old_address, old_len) != 0) {
        int saved_errno = errno;
        munmap_impl(mapped, new_len);
        errno = saved_errno;
        return (void *)-1;
    }
    return mapped;
}

int msync_impl(void *addr, size_t len, int flags) {
    struct task_struct *task = get_current();
    uint64_t start = (uint64_t)(uintptr_t)addr;
    uint64_t size;
    uint64_t end;
    int synced = 0;

    if ((flags & ~(MS_ASYNC | MS_SYNC | MS_INVALIDATE)) != 0 ||
        ((flags & MS_ASYNC) != 0 && (flags & MS_SYNC) != 0)) {
        errno = EINVAL;
        return -1;
    }
    if (!task || !task->mm) {
        errno = ENOMEM;
        return -1;
    }
    if (len == 0) {
        return 0;
    }
    if ((start % TASK_VMA_PAGE_SIZE) != 0) {
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
        uint64_t overlap_start;
        uint64_t overlap_end;
        uint64_t start_page;
        uint64_t end_page;

        if (end <= vma->start || start >= vma->end) {
            continue;
        }
        overlap_start = start > vma->start ? start : vma->start;
        overlap_end = end < vma->end ? end : vma->end;
        if (vma->kind != TASK_VMA_FILE || !vma->shared) {
            synced = 1;
            continue;
        }
        start_page = (overlap_start - vma->start) / TASK_VMA_PAGE_SIZE;
        end_page = ((overlap_end - 1) - vma->start) / TASK_VMA_PAGE_SIZE;
        for (uint64_t page = start_page; page <= end_page && page < vma->page_count; page++) {
            uint64_t page_start = vma->start + (page * TASK_VMA_PAGE_SIZE);
            uint64_t page_end = page_start + TASK_VMA_PAGE_SIZE;
            uint64_t write_start = overlap_start > page_start ? overlap_start : page_start;
            uint64_t write_end = overlap_end < page_end ? overlap_end : page_end;
            size_t image_offset;
            size_t to_write;
            long long written;

            if (vma->dirty_pages && vma->dirty_pages[page] == 0) {
                continue;
            }
            image_offset = (size_t)(write_start - vma->start);
            to_write = (size_t)(write_end - write_start);
            const unsigned char *source = vma->shared_pages && vma->shared_pages[page]
                ? vma->shared_pages[page]->image + (image_offset % TASK_VMA_PAGE_SIZE)
                : (const unsigned char *)vma->image + image_offset;
            written = mm_vma_pwrite(vma, source, to_write,
                                    (long long)(vma->backing_offset + image_offset));
            if (written < 0) {
                return -1;
            }
            if ((size_t)written != to_write) {
                errno = EIO;
                return -1;
            }
            if (vma->dirty_pages) {
                vma->dirty_pages[page] = 0;
            }
        }
        synced = 1;
    }
    if (!synced) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
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
        atomic_init(&task->mm->refs, 1);
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
