/* OrlixKernel/kernel/mm.c
 * Virtual Linux memory mapping substrate.
 */

#include "mm.h"
#include "../private/kernel/mm_state.h"
#include "task.h"
#include "../private/kernel/task_state.h"

#include "../fs/fdtable.h"
#include "../private/fs/fdtable_state.h"

#include <linux/atomic.h>
#include <linux/errno.h>
#include <linux/gfp_types.h>
#include <linux/limits.h>
#include <linux/string.h>

#include <uapi/linux/elf.h>
#include <uapi/linux/fcntl.h>
#include <uapi/linux/fs.h>
#define BUILD_VDSO 1
#include <uapi/linux/mman.h>
#undef BUILD_VDSO

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
    atomic_t refs;
    struct vm_shared_mapping *next;
};

struct vm_private_page {
    uint8_t image[TASK_VMA_PAGE_SIZE];
    atomic_t refs;
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
extern void *__kmalloc_noprof(size_t size, gfp_t flags);
extern void kfree(const void *objp);

static long long mm_fd_pread(int fd, void *buf, size_t count, long long offset);

static void *err_ptr_impl(long error) {
    return (void *)error;
}

static long ptr_err_impl(const void *ptr) {
    return (long)ptr;
}

static bool is_err_impl(const void *ptr) {
    long error = ptr_err_impl(ptr);
    return error < 0 && error >= -4095;
}

#define ERR_PTR(err) err_ptr_impl(err)
#define PTR_ERR(ptr) ptr_err_impl(ptr)
#define IS_ERR(ptr) is_err_impl(ptr)

static void *mm_alloc_array(uint64_t count, size_t elem_size) {
    size_t bytes;

    if (count == 0 || elem_size == 0 || count > SIZE_MAX / elem_size) {
        return NULL;
    }
    bytes = (size_t)count * elem_size;
    return __kmalloc_noprof(bytes, GFP_KERNEL | __GFP_ZERO);
}

static void mm_shared_mapping_get(struct vm_shared_mapping *mapping) {
    if (mapping) {
        atomic_inc(&mapping->refs);
    }
}

static void mm_shared_mapping_put(struct vm_shared_mapping *mapping) {
    if (!mapping) {
        return;
    }
    if (atomic_dec_return(&mapping->refs) != 0) {
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

    kfree(mapping);
}

void mm_shared_mapping_get_impl(struct vm_shared_mapping *mapping) {
    mm_shared_mapping_get(mapping);
}

void mm_shared_mapping_put_impl(struct vm_shared_mapping *mapping) {
    mm_shared_mapping_put(mapping);
}

static struct vm_private_page *mm_private_page_alloc(const void *source, size_t source_len) {
    struct vm_private_page *page = __kmalloc_noprof(sizeof(*page), GFP_KERNEL | __GFP_ZERO);

    if (!page) {
        return NULL;
    }
    atomic_set(&page->refs, 1);
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
        atomic_inc(&page->refs);
    }
}

static void mm_vma_mark_resident(struct task_vma *vma, uint64_t page_index) {
    if (vma && vma->resident_pages && page_index < vma->page_count) {
        vma->resident_pages[page_index] = 1;
    }
}

void mm_private_page_put_impl(struct vm_private_page *page) {
    if (!page) {
        return;
    }
    if (atomic_dec_return(&page->refs) == 0) {
        kfree(page);
    }
}

static void mm_private_pages_put(struct vm_private_page **pages, uint64_t page_count) {
    if (!pages) {
        return;
    }
    for (uint64_t i = 0; i < page_count; i++) {
        mm_private_page_put_impl(pages[i]);
    }
    kfree(pages);
}

static int mm_private_vma_enable_cow(struct task_vma *vma) {
    struct vm_private_page **pages;

    if (!vma || vma->private_pages || vma->shared_pages || vma->shared_mapping ||
        (vma->kind != TASK_VMA_ANON && vma->kind != TASK_VMA_FILE)) {
        return 0;
    }
    if (!vma->image || vma->page_count == 0) {
        return -EINVAL;
    }
    pages = __kmalloc_noprof((size_t)vma->page_count * sizeof(*pages), GFP_KERNEL | __GFP_ZERO);
    if (!pages) {
        return -ENOMEM;
    }
    for (uint64_t i = 0; i < vma->page_count; i++) {
        size_t offset = (size_t)(i * TASK_VMA_PAGE_SIZE);
        size_t remaining = offset < vma->image_size ? vma->image_size - offset : 0;

        pages[i] = mm_private_page_alloc((const unsigned char *)vma->image + offset, remaining);
        if (!pages[i]) {
            mm_private_pages_put(pages, vma->page_count);
            return -ENOMEM;
        }
    }
    kfree(vma->image);
    vma->image = pages[0]->image;
    vma->private_pages = pages;
    return 0;
}

long mm_private_vma_read_impl(struct task_vma *vma, uint64_t addr, void *buf, size_t count) {
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
        return -EFAULT;
    }
    page_offset = (size_t)(offset % TASK_VMA_PAGE_SIZE);
    to_copy = count;
    if (to_copy > vma->image_size - (size_t)offset) {
        to_copy = vma->image_size - (size_t)offset;
    }
    if (to_copy > TASK_VMA_PAGE_SIZE - page_offset) {
        to_copy = TASK_VMA_PAGE_SIZE - page_offset;
    }
    mm_vma_mark_resident(vma, page_index);
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
        return -EFAULT;
    }
    page = vma->private_pages[page_index];
    if (atomic_read(&page->refs) > 1) {
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
        return ERR_PTR(-EBADF);
    }

    kernel_mutex_lock(&mm_shared_mapping_lock);
    mapping = mm_shared_mapping_lookup_locked(file_identity, page_index);
    kernel_mutex_unlock(&mm_shared_mapping_lock);
    if (mapping) {
        return mapping;
    }

    mapping = __kmalloc_noprof(sizeof(*mapping), GFP_KERNEL | __GFP_ZERO);
    if (!mapping) {
        return ERR_PTR(-ENOMEM);
    }
    bytes = mm_fd_pread(fd, mapping->image, TASK_VMA_PAGE_SIZE,
                        (long long)(page_index * TASK_VMA_PAGE_SIZE));
    if (bytes < 0) {
        kfree(mapping);
        return ERR_PTR((int)bytes);
    }
    mapping->file_identity = file_identity;
    mapping->page_index = page_index;
    atomic_set(&mapping->refs, 1);

    kernel_mutex_lock(&mm_shared_mapping_lock);
    struct vm_shared_mapping *existing = mm_shared_mapping_lookup_locked(file_identity, page_index);
    if (existing) {
        kernel_mutex_unlock(&mm_shared_mapping_lock);
        kfree(mapping);
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
    kfree(pages);
}

static struct vm_shared_mapping **mm_shared_pages_get_or_create(int fd, uint64_t offset,
                                                                uint64_t page_count) {
    uint64_t file_identity = mm_fd_file_identity(fd);
    uint64_t first_page = offset / TASK_VMA_PAGE_SIZE;
    struct vm_shared_mapping **pages;

    if (file_identity == 0 || page_count == 0 || page_count > SIZE_MAX / sizeof(*pages)) {
        return ERR_PTR(file_identity == 0 ? -EBADF : -ENOMEM);
    }
    pages = mm_alloc_array(page_count, sizeof(*pages));
    if (!pages) {
        return ERR_PTR(-ENOMEM);
    }
    for (uint64_t i = 0; i < page_count; i++) {
        pages[i] = mm_shared_mapping_get_or_create(fd, file_identity, first_page + i);
        if (IS_ERR(pages[i])) {
            long err = PTR_ERR(pages[i]);
            mm_shared_pages_put(pages, i);
            return ERR_PTR(err);
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
                return -ENXIO;
            }
            return (long long)(mm_file_size_notes[i].size - file_offset);
        }
    }
    return (long long)(vma->image_size - offset);
}

long long mm_vma_file_remaining_impl(const struct task_vma *vma, size_t offset) {
    return mm_shared_file_remaining(vma, offset);
}

static long long mm_fd_size(int fd) {
    long long original;
    long long size;
    if (fd < 0) {
        return -EBADF;
    }
    original = lseek_impl(fd, 0, SEEK_CUR);
    if (original < 0) {
        return original;
    }
    size = lseek_impl(fd, 0, SEEK_END);
    if (lseek_impl(fd, original, SEEK_SET) < 0 && size >= 0) {
        return -EIO;
    }
    return size;
}

long long mm_vma_file_size_impl(const struct task_vma *vma) {
    uint64_t identity;

    if (!vma) {
        return -1;
    }
    identity = vma->backing_file_identity;
    if (identity == 0 && vma->backing_fd >= 0) {
        identity = mm_fd_file_identity(vma->backing_fd);
    }
    if (identity != 0) {
        for (size_t i = 0; i < sizeof(mm_file_size_notes) / sizeof(mm_file_size_notes[0]); i++) {
            if (mm_file_size_notes[i].file_identity != 0 &&
                mm_file_size_notes[i].file_identity == identity) {
                return (long long)mm_file_size_notes[i].size;
            }
        }
    }
    if (vma->backing_fd >= 0) {
        long long size = mm_fd_size(vma->backing_fd);

        if (size >= 0) {
            return size;
        }
    }
    return (long long)vma->image_size;
}

long mm_shared_vma_read_impl(struct task_vma *vma, uint64_t addr, void *buf, size_t count) {
    size_t offset;
    uint64_t page_index;
    size_t page_offset;
    size_t to_copy;
    long long file_remaining;
    long long file_size;

    if (!vma || !vma->shared_pages || addr < vma->start || addr >= vma->end) {
        return 0;
    }
    offset = (size_t)(addr - vma->start);
    file_size = mm_vma_file_size_impl(vma);
    if (file_size >= 0) {
        size_t page_start = offset - (offset % TASK_VMA_PAGE_SIZE);
        uint64_t file_page_start = vma->backing_offset + (uint64_t)page_start;
        uint64_t file_offset = vma->backing_offset + (uint64_t)offset;

        if (file_page_start >= (uint64_t)file_size) {
            return -ENXIO;
        }
        if (file_offset >= (uint64_t)file_size) {
            page_offset = offset % TASK_VMA_PAGE_SIZE;
            to_copy = count;
            if (to_copy > TASK_VMA_PAGE_SIZE - page_offset) {
                to_copy = TASK_VMA_PAGE_SIZE - page_offset;
            }
            mm_vma_mark_resident(vma, offset / TASK_VMA_PAGE_SIZE);
            memset(buf, 0, to_copy);
            return (long)to_copy;
        }
    }
    file_remaining = mm_shared_file_remaining(vma, offset);
    if (file_remaining < 0) {
        return (long)file_remaining;
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
    mm_vma_mark_resident(vma, page_index);
    memcpy(buf, vma->shared_pages[page_index]->image + page_offset, to_copy);
    return (long)to_copy;
}

long mm_shared_vma_write_impl(struct task_vma *vma, uint64_t addr, const void *buf, size_t count) {
    size_t offset;
    uint64_t page_index;
    size_t page_offset;
    size_t to_copy;
    long long file_remaining;
    long long file_size;

    if (!vma || !vma->shared_pages || addr < vma->start || addr >= vma->end) {
        return 0;
    }
    offset = (size_t)(addr - vma->start);
    file_size = mm_vma_file_size_impl(vma);
    file_remaining = -1;
    if (file_size >= 0) {
        size_t page_start = offset - (offset % TASK_VMA_PAGE_SIZE);
        uint64_t file_page_start = vma->backing_offset + (uint64_t)page_start;
        uint64_t file_offset = vma->backing_offset + (uint64_t)offset;

        if (file_page_start >= (uint64_t)file_size) {
            return -ENXIO;
        }
        if (file_offset < (uint64_t)file_size) {
            file_remaining = (long long)((uint64_t)file_size - file_offset);
        }
    } else {
        file_remaining = mm_shared_file_remaining(vma, offset);
        if (file_remaining < 0) {
            return (long)file_remaining;
        }
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
    if (file_remaining >= 0 && (long long)to_copy > file_remaining) {
        to_copy = (size_t)file_remaining;
    }
    if (to_copy == 0) {
        to_copy = count;
        if (to_copy > TASK_VMA_PAGE_SIZE - page_offset) {
            to_copy = TASK_VMA_PAGE_SIZE - page_offset;
        }
        if (to_copy > vma->image_size - offset) {
            to_copy = vma->image_size - offset;
        }
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
    if (value > U64_MAX - (align - 1)) {
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
        return -EINVAL;
    }
    if (((flags & MAP_PRIVATE) != 0) == ((flags & MAP_SHARED) != 0)) {
        return -EINVAL;
    }
    if ((flags & MAP_ANONYMOUS) != 0 && fd != -1) {
        return -EINVAL;
    }
    if ((flags & MAP_ANONYMOUS) == 0 && fd < 0) {
        return -EBADF;
    }
    return 0;
}

static int mm_validate_prot(int prot) {
    if ((prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC | PROT_NONE)) != 0) {
        return -EINVAL;
    }
    if ((prot & PROT_NONE) != 0 && (prot & (PROT_READ | PROT_WRITE | PROT_EXEC)) != 0) {
        return -EINVAL;
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
        return -EINVAL;
    }
}

static long long mm_fd_pread(int fd, void *buf, size_t count, long long offset) {
    long long bytes = pread_impl(fd, buf, count, offset);

    if (bytes >= 0) {
        return bytes;
    }

    long long original = lseek_impl(fd, 0, SEEK_CUR);
    if (original < 0) {
        return original;
    }
    if (lseek_impl(fd, offset, SEEK_SET) < 0) {
        return -ESPIPE;
    }
    bytes = read_impl(fd, buf, count);
    if (lseek_impl(fd, original, SEEK_SET) < 0 && bytes >= 0) {
        return -EIO;
    }
    return bytes;
}

static long long mm_fd_pwrite(int fd, const void *buf, size_t count, long long offset) {
    long long bytes = pwrite_impl(fd, buf, count, offset);

    if (bytes >= 0) {
        return bytes;
    }

    long long original = lseek_impl(fd, 0, SEEK_CUR);
    if (original < 0) {
        return original;
    }
    if (lseek_impl(fd, offset, SEEK_SET) < 0) {
        return -ESPIPE;
    }
    bytes = write_impl(fd, buf, count);
    if (lseek_impl(fd, original, SEEK_SET) < 0 && bytes >= 0) {
        return -EIO;
    }
    return bytes;
}

static long long mm_vma_pread(const struct task_vma *vma, void *buf, size_t count,
                              long long offset) {
    long long bytes;
    int reopened;

    if (!vma || !buf) {
        return -EINVAL;
    }

    if (vma->backing_fd >= 0) {
        bytes = mm_fd_pread(vma->backing_fd, buf, count, offset);
        if (bytes >= 0 || vma->backing_path[0] == '\0') {
            return bytes;
        }
    }
    if (vma->backing_path[0] == '\0') {
        return -EBADF;
    }

    reopened = open_impl(vma->backing_path, O_RDONLY, 0);
    if (reopened < 0) {
        return reopened;
    }
    bytes = mm_fd_pread(reopened, buf, count, offset);
    if (close_impl(reopened) != 0 && bytes >= 0) {
        return -EIO;
    }
    return bytes;
}

static long long mm_vma_pwrite(struct task_vma *vma, const void *buf, size_t count,
                               long long offset) {
    long long bytes;
    int reopened;

    if (!vma) {
        return -EINVAL;
    }

    if (vma->backing_fd >= 0) {
        bytes = mm_fd_pwrite(vma->backing_fd, buf, count, offset);
        if (bytes >= 0 || vma->backing_path[0] == '\0') {
            return bytes;
        }
    }
    if (vma->backing_path[0] == '\0') {
        if (vma->shared_pages) {
            return (long long)count;
        }
        return -EBADF;
    }

    reopened = open_impl(vma->backing_path, O_RDWR, 0);
    if (reopened < 0) {
        if (vma->shared_pages) {
            return (long long)count;
        }
        return reopened;
    }
    bytes = mm_fd_pwrite(reopened, buf, count, offset);
    if (close_impl(reopened) != 0 && bytes >= 0) {
        return -EIO;
    }
    return bytes;
}

static int mm_range_overlaps(struct memory_space *mm, uint64_t start, uint64_t end) {
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

static uint64_t mm_alloc_addr(struct memory_space *mm, uint64_t length) {
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

static int mm_add_vma(struct memory_space *mm, uint64_t start, uint64_t size, uint32_t pf_flags,
                      enum task_vma_kind kind, void *image) {
    struct task_vma *vma;
    uint64_t page_count;

    if (!mm || !image || size == 0 || size > U64_MAX - start || mm->vma_count >= TASK_EXEC_MAX_VMAS) {
        return -ENOMEM;
    }
    if (mm_range_overlaps(mm, start, start + size)) {
        return -EEXIST;
    }

    page_count = size / TASK_VMA_PAGE_SIZE;
    if ((size % TASK_VMA_PAGE_SIZE) != 0) {
        page_count++;
    }
    if (page_count == 0 || page_count > SIZE_MAX / sizeof(uint32_t)) {
        return -ENOMEM;
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
    vma->page_flags = mm_alloc_array(page_count, sizeof(*vma->page_flags));
    if (!vma->page_flags) {
        memset(vma, 0, sizeof(*vma));
        return -ENOMEM;
    }
    vma->resident_pages = mm_alloc_array(page_count, sizeof(*vma->resident_pages));
    if (!vma->resident_pages) {
        kfree(vma->page_flags);
        memset(vma, 0, sizeof(*vma));
        return -ENOMEM;
    }
    vma->dirty_pages = mm_alloc_array(page_count, sizeof(*vma->dirty_pages));
    if (!vma->dirty_pages) {
        kfree(vma->resident_pages);
        kfree(vma->page_flags);
        memset(vma, 0, sizeof(*vma));
        return -ENOMEM;
    }
    for (uint64_t i = 0; i < page_count; i++) {
        vma->page_flags[i] = pf_flags;
        vma->resident_pages[i] = 1;
    }
    mm->vma_count++;
    return 0;
}

static void mm_remove_vma_at(struct memory_space *mm, uint32_t index) {
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
            kfree(vma->image);
        }
    }
    kfree(vma->page_flags);
    kfree(vma->resident_pages);
    kfree(vma->dirty_pages);
    for (uint32_t i = index + 1; i < mm->vma_count; i++) {
        mm->vmas[i - 1] = mm->vmas[i];
    }
    mm->vma_count--;
    memset(&mm->vmas[mm->vma_count], 0, sizeof(mm->vmas[0]));
}

static int mm_find_brk_vma(struct memory_space *mm, uint32_t *index_out) {
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

static int mm_range_overlaps_except(struct memory_space *mm, uint64_t start, uint64_t end,
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
        return -EINVAL;
    }
    size = end - start;
    if (size > SIZE_MAX) {
        return -ENOMEM;
    }
    page_count = size / TASK_VMA_PAGE_SIZE;
    if ((size % TASK_VMA_PAGE_SIZE) != 0) {
        page_count++;
    }
    if (page_count == 0 || page_count > SIZE_MAX / sizeof(uint32_t)) {
        return -ENOMEM;
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
        dest->shared_pages = mm_alloc_array(page_count, sizeof(*dest->shared_pages));
        if (!dest->shared_pages) {
            return -ENOMEM;
        }
        for (uint64_t i = 0; i < page_count; i++) {
            dest->shared_pages[i] = source->shared_pages[first_page + i];
            mm_shared_mapping_get(dest->shared_pages[i]);
        }
        dest->image = dest->shared_pages[0] ? dest->shared_pages[0]->image : NULL;
    } else if (source->private_pages) {
        uint64_t first_page = (start - source->start) / TASK_VMA_PAGE_SIZE;
        dest->private_pages = mm_alloc_array(page_count, sizeof(*dest->private_pages));
        if (!dest->private_pages) {
            return -ENOMEM;
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
        dest->image = __kmalloc_noprof(dest->image_size, GFP_KERNEL | __GFP_ZERO);
        if (!dest->image) {
            return -ENOMEM;
        }
    }
    dest->page_flags = mm_alloc_array(page_count, sizeof(*dest->page_flags));
    if (!dest->page_flags) {
        if (dest->shared_pages) {
            mm_shared_pages_put(dest->shared_pages, page_count);
        } else if (dest->private_pages) {
            mm_private_pages_put(dest->private_pages, page_count);
        } else if (dest->shared_mapping) {
            mm_shared_mapping_put(dest->shared_mapping);
        } else {
            kfree(dest->image);
        }
        memset(dest, 0, sizeof(*dest));
        return -ENOMEM;
    }
    dest->resident_pages = mm_alloc_array(page_count, sizeof(*dest->resident_pages));
    if (!dest->resident_pages) {
        kfree(dest->page_flags);
        if (dest->shared_pages) {
            mm_shared_pages_put(dest->shared_pages, page_count);
        } else if (dest->private_pages) {
            mm_private_pages_put(dest->private_pages, page_count);
        } else if (dest->shared_mapping) {
            mm_shared_mapping_put(dest->shared_mapping);
        } else {
            kfree(dest->image);
        }
        memset(dest, 0, sizeof(*dest));
        return -ENOMEM;
    }
    dest->dirty_pages = mm_alloc_array(page_count, sizeof(*dest->dirty_pages));
    if (!dest->dirty_pages) {
        kfree(dest->resident_pages);
        kfree(dest->page_flags);
        if (dest->shared_pages) {
            mm_shared_pages_put(dest->shared_pages, page_count);
        } else if (dest->private_pages) {
            mm_private_pages_put(dest->private_pages, page_count);
        } else if (dest->shared_mapping) {
            mm_shared_mapping_put(dest->shared_mapping);
        } else {
            kfree(dest->image);
        }
        memset(dest, 0, sizeof(*dest));
        return -ENOMEM;
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
        return -ENOMEM;
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
        return -ENOMEM;
    }

    page_flags = __kmalloc_noprof((size_t)new_pages * sizeof(*page_flags), GFP_KERNEL | __GFP_ZERO);
    resident_pages = __kmalloc_noprof((size_t)new_pages * sizeof(*resident_pages), GFP_KERNEL | __GFP_ZERO);
    dirty_pages = __kmalloc_noprof((size_t)new_pages * sizeof(*dirty_pages), GFP_KERNEL | __GFP_ZERO);
    if (!page_flags || !resident_pages || !dirty_pages) {
        kfree(page_flags);
        kfree(resident_pages);
        kfree(dirty_pages);
        return -ENOMEM;
    }
    memcpy(page_flags, vma->page_flags, (size_t)old_pages * sizeof(*page_flags));
    memcpy(resident_pages, vma->resident_pages, (size_t)old_pages * sizeof(*resident_pages));
    memcpy(dirty_pages, vma->dirty_pages, (size_t)old_pages * sizeof(*dirty_pages));
    for (uint64_t i = old_pages; i < new_pages; i++) {
        page_flags[i] = vma->flags;
        resident_pages[i] = 1;
    }

    if (vma->private_pages) {
        private_pages = __kmalloc_noprof((size_t)new_pages * sizeof(*private_pages), GFP_KERNEL | __GFP_ZERO);
        if (!private_pages) {
            kfree(page_flags);
            kfree(resident_pages);
            kfree(dirty_pages);
            return -ENOMEM;
        }
        memcpy(private_pages, vma->private_pages, (size_t)old_pages * sizeof(*private_pages));
        for (uint64_t i = old_pages; i < new_pages; i++) {
            private_pages[i] = mm_private_page_alloc(NULL, 0);
            if (!private_pages[i]) {
                for (uint64_t j = old_pages; j < i; j++) {
                    mm_private_page_put_impl(private_pages[j]);
                }
                kfree(private_pages);
                kfree(page_flags);
                kfree(resident_pages);
                kfree(dirty_pages);
                return -ENOMEM;
            }
        }
    } else if (vma->shared_pages && vma->backing_fd >= 0) {
        shared_pages = __kmalloc_noprof((size_t)new_pages * sizeof(*shared_pages), GFP_KERNEL | __GFP_ZERO);
        if (!shared_pages) {
            kfree(page_flags);
            kfree(resident_pages);
            kfree(dirty_pages);
            return -ENOMEM;
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
                kfree(shared_pages);
                kfree(page_flags);
                kfree(resident_pages);
                kfree(dirty_pages);
                return -1;
            }
            shared_pages[i] = page[0];
            kfree(page);
        }
    } else if (vma->shared_mapping) {
        if (vma->backing_fd < 0) {
            kfree(page_flags);
            kfree(resident_pages);
            kfree(dirty_pages);
            return -EBADF;
        }
        shared_pages = mm_shared_pages_get_or_create(vma->backing_fd, vma->backing_offset, new_pages);
        if (IS_ERR(shared_pages)) {
            long err = PTR_ERR(shared_pages);
            kfree(page_flags);
            kfree(resident_pages);
            kfree(dirty_pages);
            return (int)err;
        }
    } else {
        new_image = __kmalloc_noprof((size_t)new_len, GFP_KERNEL | __GFP_ZERO);
        if (!new_image) {
            kfree(page_flags);
            kfree(resident_pages);
            kfree(dirty_pages);
            return -ENOMEM;
        }
        memcpy(new_image, vma->image, (size_t)old_len);
    }

    kfree(vma->page_flags);
    kfree(vma->resident_pages);
    kfree(vma->dirty_pages);
    vma->page_flags = page_flags;
    vma->resident_pages = resident_pages;
    vma->dirty_pages = dirty_pages;
    if (private_pages) {
        kfree(vma->private_pages);
        vma->private_pages = private_pages;
    } else if (shared_pages) {
        kfree(vma->shared_pages);
        vma->shared_pages = shared_pages;
        if (vma->shared_mapping) {
            mm_shared_mapping_put(vma->shared_mapping);
            vma->shared_mapping = NULL;
            vma->shared_mapping_offset = 0;
        }
        vma->image = shared_pages[0] ? shared_pages[0]->image : NULL;
    } else if (new_image) {
        kfree(vma->image);
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
    copy = __kmalloc_noprof(size, GFP_KERNEL);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, source, size);
    return copy;
}

struct memory_space *task_mm_dup_impl(const struct memory_space *source) {
    struct memory_space *copy;

    if (!source) {
        return NULL;
    }
    copy = __kmalloc_noprof(sizeof(*copy), GFP_KERNEL | __GFP_ZERO);
    if (!copy) {
        return NULL;
    }
    atomic_set(&copy->refs, 1);
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
    copy->signal_frame_restartable = source->signal_frame_restartable;
    copy->signal_frame_restart_return_pc = source->signal_frame_restart_return_pc;
    copy->signal_frame_restart_sp = source->signal_frame_restart_sp;
    copy->signal_frame_restart_signo = source->signal_frame_restart_signo;
    copy->signal_frame_restart_kind = source->signal_frame_restart_kind;
    copy->signal_frame_restart_arg0 = source->signal_frame_restart_arg0;
    copy->signal_frame_restart_arg1 = source->signal_frame_restart_arg1;
    copy->signal_frame_restart_arg2 = source->signal_frame_restart_arg2;
    copy->signal_frame_restart_arg3 = source->signal_frame_restart_arg3;
    copy->signal_frame_restart_arg4 = source->signal_frame_restart_arg4;
    copy->signal_frame_restart_arg5 = source->signal_frame_restart_arg5;

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
            kfree(vma->image);
        }
    }
    kfree(vma->page_flags);
    kfree(vma->resident_pages);
    kfree(vma->dirty_pages);
    memset(vma, 0, sizeof(*vma));
}

static int mm_replace_vma_with_slices(struct memory_space *mm, uint32_t index,
                                      const struct task_vma *left,
                                      const struct task_vma *right) {
    int has_left = left && left->start < left->end;
    int has_right = right && right->start < right->end;
    uint32_t replacement_count = (has_left ? 1U : 0U) + (has_right ? 1U : 0U);

    if (!mm || index >= mm->vma_count) {
        return -EINVAL;
    }
    if (mm->vma_count - 1 + replacement_count > TASK_EXEC_MAX_VMAS) {
        return -ENOMEM;
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

static int mm_insert_vma_sorted(struct memory_space *mm, const struct task_vma *vma) {
    uint32_t index = 0;

    if (!mm || !vma || vma->start >= vma->end) {
        return -EINVAL;
    }
    if (mm->vma_count >= TASK_EXEC_MAX_VMAS) {
        return -ENOMEM;
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

static void mm_sort_vmas_by_start(struct memory_space *mm) {
    if (!mm) {
        return;
    }
    for (uint32_t i = 1; i < mm->vma_count; i++) {
        struct task_vma current = mm->vmas[i];
        uint32_t j = i;

        while (j > 0 && mm->vmas[j - 1].start > current.start) {
            mm->vmas[j] = mm->vmas[j - 1];
            j--;
        }
        mm->vmas[j] = current;
    }
}

static int mm_vma_storage_class_matches(const struct task_vma *left,
                                        const struct task_vma *right) {
    return (!!left->shared_pages == !!right->shared_pages) &&
           (!!left->private_pages == !!right->private_pages) &&
           (!!left->shared_mapping == !!right->shared_mapping);
}

static int mm_vmas_can_merge(const struct task_vma *left, const struct task_vma *right) {
    uint64_t left_size;

    if (!left || !right || left->end != right->start ||
        left->kind != right->kind ||
        left->flags != right->flags ||
        left->shared != right->shared ||
        left->backing_fd != right->backing_fd ||
        left->backing_file_identity != right->backing_file_identity ||
        !left->page_flags || !right->page_flags ||
        !left->resident_pages || !right->resident_pages ||
        !left->dirty_pages || !right->dirty_pages ||
        strcmp(left->backing_path, right->backing_path) != 0 ||
        !mm_vma_storage_class_matches(left, right)) {
        return 0;
    }
    left_size = left->end - left->start;
    if (left->kind == TASK_VMA_FILE &&
        left->backing_offset + left_size != right->backing_offset) {
        return 0;
    }
    if (left->shared_mapping &&
        left->shared_mapping_offset + left_size != right->shared_mapping_offset) {
        return 0;
    }
    if (left->shared_mapping && left->shared_mapping != right->shared_mapping) {
        return 0;
    }
    return 1;
}

static int mm_merge_vma_pair(struct memory_space *mm, uint32_t index) {
    struct task_vma *left;
    struct task_vma *right;
    uint64_t left_pages;
    uint64_t right_pages;
    uint64_t merged_pages;
    uint64_t merged_size;
    uint32_t *page_flags;
    uint8_t *resident_pages;
    uint8_t *dirty_pages;
    void *image = NULL;
    struct vm_private_page **private_pages = NULL;
    struct vm_shared_mapping **shared_pages = NULL;

    if (!mm || index + 1 >= mm->vma_count) {
        return -EINVAL;
    }
    left = &mm->vmas[index];
    right = &mm->vmas[index + 1];
    if (!mm_vmas_can_merge(left, right)) {
        return 0;
    }
    if (left->end > U64_MAX - (right->end - right->start)) {
        return -ENOMEM;
    }
    left_pages = left->page_count;
    right_pages = right->page_count;
    merged_pages = left_pages + right_pages;
    merged_size = right->end - left->start;
    if (merged_pages == 0 ||
        merged_pages > SIZE_MAX / sizeof(*page_flags) ||
        merged_pages > SIZE_MAX / sizeof(*resident_pages) ||
        merged_pages > SIZE_MAX / sizeof(*dirty_pages) ||
        merged_size > SIZE_MAX) {
        return -ENOMEM;
    }

    page_flags = __kmalloc_noprof((size_t)merged_pages * sizeof(*page_flags), GFP_KERNEL | __GFP_ZERO);
    resident_pages = __kmalloc_noprof((size_t)merged_pages * sizeof(*resident_pages), GFP_KERNEL | __GFP_ZERO);
    dirty_pages = __kmalloc_noprof((size_t)merged_pages * sizeof(*dirty_pages), GFP_KERNEL | __GFP_ZERO);
    if (!page_flags || !resident_pages || !dirty_pages) {
        kfree(page_flags);
        kfree(resident_pages);
        kfree(dirty_pages);
        return -ENOMEM;
    }
    memcpy(page_flags, left->page_flags, (size_t)left_pages * sizeof(*page_flags));
    memcpy(page_flags + left_pages, right->page_flags, (size_t)right_pages * sizeof(*page_flags));
    memcpy(resident_pages, left->resident_pages, (size_t)left_pages * sizeof(*resident_pages));
    memcpy(resident_pages + left_pages, right->resident_pages,
           (size_t)right_pages * sizeof(*resident_pages));
    memcpy(dirty_pages, left->dirty_pages, (size_t)left_pages * sizeof(*dirty_pages));
    memcpy(dirty_pages + left_pages, right->dirty_pages, (size_t)right_pages * sizeof(*dirty_pages));

    if (left->shared_pages) {
        shared_pages = __kmalloc_noprof((size_t)merged_pages * sizeof(*shared_pages), GFP_KERNEL | __GFP_ZERO);
        if (!shared_pages) {
            kfree(page_flags);
            kfree(resident_pages);
            kfree(dirty_pages);
            return -ENOMEM;
        }
        memcpy(shared_pages, left->shared_pages, (size_t)left_pages * sizeof(*shared_pages));
        memcpy(shared_pages + left_pages, right->shared_pages,
               (size_t)right_pages * sizeof(*shared_pages));
        image = shared_pages[0] ? shared_pages[0]->image : NULL;
    } else if (left->private_pages) {
        private_pages = __kmalloc_noprof((size_t)merged_pages * sizeof(*private_pages), GFP_KERNEL | __GFP_ZERO);
        if (!private_pages) {
            kfree(page_flags);
            kfree(resident_pages);
            kfree(dirty_pages);
            return -ENOMEM;
        }
        memcpy(private_pages, left->private_pages, (size_t)left_pages * sizeof(*private_pages));
        memcpy(private_pages + left_pages, right->private_pages,
               (size_t)right_pages * sizeof(*private_pages));
        image = private_pages[0] ? private_pages[0]->image : NULL;
    } else if (left->shared_mapping) {
        image = left->shared_mapping->image + left->shared_mapping_offset;
    } else {
        image = __kmalloc_noprof((size_t)merged_size, GFP_KERNEL | __GFP_ZERO);
        if (!image) {
            kfree(page_flags);
            kfree(resident_pages);
            kfree(dirty_pages);
            return -ENOMEM;
        }
        memcpy(image, left->image, left->image_size);
        memcpy((unsigned char *)image + left->image_size, right->image, right->image_size);
    }

    if (!left->shared_pages && !left->private_pages && !left->shared_mapping) {
        kfree(left->image);
        kfree(right->image);
    }
    kfree(left->page_flags);
    kfree(left->resident_pages);
    kfree(left->dirty_pages);
    kfree(right->page_flags);
    kfree(right->resident_pages);
    kfree(right->dirty_pages);
    if (left->shared_pages || left->private_pages) {
        kfree(left->shared_pages);
        kfree(right->shared_pages);
        kfree(left->private_pages);
        kfree(right->private_pages);
    }

    left->end = right->end;
    left->image = image;
    left->image_size = (size_t)merged_size;
    left->page_count = merged_pages;
    left->page_flags = page_flags;
    left->resident_pages = resident_pages;
    left->dirty_pages = dirty_pages;
    left->private_pages = private_pages;
    left->shared_pages = shared_pages;

    for (uint32_t i = index + 2; i < mm->vma_count; i++) {
        mm->vmas[i - 1] = mm->vmas[i];
    }
    mm->vma_count--;
    memset(&mm->vmas[mm->vma_count], 0, sizeof(mm->vmas[0]));
    return 1;
}

static int mm_merge_adjacent_vmas(struct memory_space *mm) {
    if (!mm) {
        return -EINVAL;
    }
    for (uint32_t i = 0; i + 1 < mm->vma_count;) {
        int ret = mm_merge_vma_pair(mm, i);
        if (ret < 0) {
            return -1;
        }
        if (ret > 0) {
            continue;
        }
        i++;
    }
    return 0;
}

static int mm_remove_exact_vma(struct memory_space *mm, uint64_t start, uint64_t end) {
    if (!mm) {
        return -EINVAL;
    }
    for (uint32_t i = 0; i < mm->vma_count; i++) {
        if (mm->vmas[i].start == start && mm->vmas[i].end == end) {
            mm_remove_vma_at(mm, i);
            return 0;
        }
    }
    return -ENOENT;
}

static int mm_unmap_vma_range(struct memory_space *mm, uint32_t index,
                              uint64_t start, uint64_t end) {
    struct task_vma source;
    struct task_vma left;
    struct task_vma right;
    int has_left;
    int has_right;

    if (!mm || index >= mm->vma_count || start >= end) {
        return -EINVAL;
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
    struct task *task = task_current();
    uint64_t map_len;
    uint64_t map_addr;
    enum task_vma_kind kind;
    void *image;
    struct vm_shared_mapping *shared_mapping = NULL;
    struct vm_shared_mapping **shared_pages = NULL;
    long long bytes;

    if (!task) {
        return ERR_PTR(-ESRCH);
    }
    if (!task->mm) {
        task->mm = __kmalloc_noprof(sizeof(*task->mm), GFP_KERNEL | __GFP_ZERO);
        if (!task->mm) {
            return ERR_PTR(-ENOMEM);
        }
        atomic_set(&task->mm->refs, 1);
    }
    if (length == 0 || mm_validate_prot(prot) != 0) {
        return ERR_PTR(-EINVAL);
    }
    {
        int ret = mm_validate_mmap_flags(flags, fd);
        if (ret != 0) {
            return ERR_PTR(ret);
        }
    }
    if ((flags & MAP_ANONYMOUS) == 0 && ((uint64_t)offset % TASK_VMA_PAGE_SIZE) != 0) {
        return ERR_PTR(-EINVAL);
    }

    map_len = mm_align_up((uint64_t)length, TASK_VMA_PAGE_SIZE);
    if (map_len == 0 || map_len > SIZE_MAX) {
        return ERR_PTR(-ENOMEM);
    }

    if ((flags & (MAP_FIXED | MAP_FIXED_NOREPLACE)) != 0) {
        map_addr = (uint64_t)(unsigned long)addr;
        if ((map_addr % TASK_VMA_PAGE_SIZE) != 0 || map_addr == 0 ||
            map_len > U64_MAX - map_addr) {
            return ERR_PTR(-EINVAL);
        }
        if ((flags & MAP_FIXED_NOREPLACE) != 0 &&
            mm_range_overlaps(task->mm, map_addr, map_addr + map_len)) {
            return ERR_PTR(-EEXIST);
        }
    } else {
        map_addr = mm_alloc_addr(task->mm, map_len);
        if (map_addr == 0) {
            return ERR_PTR(-ENOMEM);
        }
    }

    if ((flags & MAP_FIXED) != 0 &&
        munmap_impl((void *)(unsigned long)map_addr, (size_t)map_len) != 0) {
        return ERR_PTR(-EFAULT);
    }

    kind = (flags & MAP_ANONYMOUS) != 0 ? TASK_VMA_ANON : TASK_VMA_FILE;
    int synthetic_zero = kind == TASK_VMA_FILE && mm_fd_is_synthetic_zero(fd);
    if (kind == TASK_VMA_FILE && (flags & MAP_SHARED) != 0 && !synthetic_zero) {
        shared_pages = mm_shared_pages_get_or_create(fd, (uint64_t)offset, map_len / TASK_VMA_PAGE_SIZE);
        if (IS_ERR(shared_pages)) {
            return ERR_CAST(shared_pages);
        }
        image = shared_pages[0] ? shared_pages[0]->image : NULL;
    } else {
        image = __kmalloc_noprof((size_t)map_len, GFP_KERNEL | __GFP_ZERO);
        if (!image) {
            return ERR_PTR(-ENOMEM);
        }
    }
    if (kind == TASK_VMA_FILE && !shared_pages && !synthetic_zero) {
        bytes = mm_fd_pread(fd, image, (size_t)map_len, (long long)offset);
        if (bytes < 0) {
            kfree(image);
            return ERR_PTR((int)bytes);
        }
    }
    {
        int ret = mm_add_vma(task->mm, map_addr, map_len, mm_prot_to_pf(prot), kind, image);
        if (ret != 0) {
            if (shared_pages) {
                mm_shared_pages_put(shared_pages, map_len / TASK_VMA_PAGE_SIZE);
            } else {
                kfree(image);
            }
            return ERR_PTR(ret);
        }
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
    mm_sort_vmas_by_start(task->mm);
    (void)mm_merge_adjacent_vmas(task->mm);
    task_mm_update_high_water_impl(task->mm);
    return (void *)(unsigned long)map_addr;
}

int mprotect_impl(void *addr, size_t len, int prot) {
    struct task *task = task_current();
    uint64_t start = (uint64_t)(unsigned long)addr;
    uint64_t size;

    if (!task || !task->mm) {
        return -EFAULT;
    }
    if (len == 0 || (start % TASK_VMA_PAGE_SIZE) != 0 || mm_validate_prot(prot) != 0) {
        return -EINVAL;
    }
    size = mm_align_up((uint64_t)len, TASK_VMA_PAGE_SIZE);
    if (size == 0 || size > U64_MAX - start) {
        return -ENOMEM;
    }
    return task_set_vma_page_flags_impl(task, start, size, mm_prot_to_pf(prot));
}

int munmap_impl(void *addr, size_t len) {
    struct task *task = task_current();
    uint64_t start = (uint64_t)(unsigned long)addr;
    uint64_t size;
    uint64_t end;
    int unmapped = 0;

    if (!task || !task->mm) {
        return -EFAULT;
    }
    if (len == 0 || (start % TASK_VMA_PAGE_SIZE) != 0) {
        return -EINVAL;
    }
    size = mm_align_up((uint64_t)len, TASK_VMA_PAGE_SIZE);
    if (size == 0 || size > U64_MAX - start) {
        return -ENOMEM;
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
                return -EFAULT;
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
        return -EINVAL;
    }

    memset(page_image, 0, TASK_VMA_PAGE_SIZE);
    image_offset = page_index * TASK_VMA_PAGE_SIZE;
    file_offset = vma->backing_offset + image_offset;
    if (vma->backing_fd >= 0 || vma->backing_path[0] != '\0') {
        bytes = mm_vma_pread(vma, page_image, TASK_VMA_PAGE_SIZE, (long long)file_offset);
        if (bytes < 0 && bytes != -ENXIO) {
            return (int)bytes;
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
        return -EINVAL;
    }
    image_offset = (size_t)(page_index * TASK_VMA_PAGE_SIZE);
    if (image_offset >= vma->image_size) {
        return -EINVAL;
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
    struct task *task = task_current();
    uint64_t start = (uint64_t)(unsigned long)addr;
    uint64_t size;
    uint64_t end;

    if (!task || !task->mm) {
        return -ENOMEM;
    }
    if (length == 0) {
        return 0;
    }
    if ((start % TASK_VMA_PAGE_SIZE) != 0 || mm_validate_madvise(advice) != 0) {
        return -EINVAL;
    }
    size = mm_align_up((uint64_t)length, TASK_VMA_PAGE_SIZE);
    if (size == 0 || size > U64_MAX - start) {
        return -ENOMEM;
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
            return -EFAULT;
        }
    }
    return 0;
}

int mincore_impl(void *addr, size_t length, unsigned char *vec) {
    struct task *task = task_current();
    uint64_t start = (uint64_t)(unsigned long)addr;
    uint64_t size;
    uint64_t page_count;

    if (!task || !task->mm) {
        return -ENOMEM;
    }
    if (!vec) {
        return -EFAULT;
    }
    if (length == 0) {
        return 0;
    }
    if ((start % TASK_VMA_PAGE_SIZE) != 0) {
        return -EINVAL;
    }
    size = mm_align_up((uint64_t)length, TASK_VMA_PAGE_SIZE);
    if (size == 0 || size > U64_MAX - start) {
        return -ENOMEM;
    }
    page_count = size / TASK_VMA_PAGE_SIZE;

    for (uint64_t page = 0; page < page_count; page++) {
        uint64_t addr_for_page = start + (page * TASK_VMA_PAGE_SIZE);
        const struct task_vma *vma = task_find_vma_impl(task, addr_for_page);
        uint64_t page_index;

        if (!vma) {
            return -ENOMEM;
        }
        page_index = (addr_for_page - vma->start) / TASK_VMA_PAGE_SIZE;
        if (page_index >= vma->page_count) {
            return -ENOMEM;
        }
        if (vma->kind == TASK_VMA_FILE) {
            long long file_size = mm_vma_file_size_impl(vma);
            uint64_t page_file_offset;

            if (page_index > (U64_MAX - vma->backing_offset) / TASK_VMA_PAGE_SIZE) {
                return -ENOMEM;
            }
            page_file_offset = vma->backing_offset + (page_index * TASK_VMA_PAGE_SIZE);
            if (file_size >= 0 && page_file_offset >= (uint64_t)file_size) {
                vec[page] = 0U;
                continue;
            }
        }
        vec[page] = (!vma->resident_pages || vma->resident_pages[page_index]) ? 1U : 0U;
    }

    return 0;
}

void *mremap_impl(void *old_address, size_t old_size, size_t new_size, int flags, void *new_address) {
    struct task *task = task_current();
    uint64_t old_start = (uint64_t)(unsigned long)old_address;
    uint64_t new_start = (uint64_t)(unsigned long)new_address;
    uint64_t old_len;
    uint64_t new_len;
    const struct task_vma *found;
    uint32_t vma_flags;
    void *mapped;
    size_t copy_len;

    if (!task || !task->mm) {
        return ERR_PTR(-EFAULT);
    }
    if ((flags & ~(MREMAP_MAYMOVE | MREMAP_FIXED)) != 0 ||
        ((flags & MREMAP_FIXED) != 0 && (flags & MREMAP_MAYMOVE) == 0)) {
        return ERR_PTR(-EINVAL);
    }
    if (old_size == 0 || new_size == 0 || (old_start % TASK_VMA_PAGE_SIZE) != 0) {
        return ERR_PTR(-EINVAL);
    }
    old_len = mm_align_up((uint64_t)old_size, TASK_VMA_PAGE_SIZE);
    new_len = mm_align_up((uint64_t)new_size, TASK_VMA_PAGE_SIZE);
    if (old_len == 0 || new_len == 0 || old_len > U64_MAX - old_start) {
        return ERR_PTR(-ENOMEM);
    }
    found = task_find_vma_impl(task, old_start);
    if (!found || old_start + old_len > found->end) {
        return ERR_PTR(-EFAULT);
    }
    if ((flags & MREMAP_FIXED) != 0 &&
        (new_start == 0 || (new_start % TASK_VMA_PAGE_SIZE) != 0 ||
         new_len > U64_MAX - new_start)) {
        return ERR_PTR(-EINVAL);
    }
    if ((flags & MREMAP_FIXED) != 0 &&
        new_start < old_start + old_len && new_start + new_len > old_start) {
        return ERR_PTR(-EINVAL);
    }
    if ((flags & MREMAP_FIXED) != 0 && old_len == new_len) {
        struct task_vma moved_vma;

        if (mm_copy_vma_slice((struct task_vma *)found, old_start, old_start + old_len, &moved_vma) != 0) {
            return ERR_PTR(-ENOMEM);
        }
        moved_vma.start = new_start;
        moved_vma.end = new_start + new_len;
        if (munmap_impl(new_address, (size_t)new_len) != 0) {
            mm_free_vma_contents(&moved_vma);
            return ERR_PTR(-EFAULT);
        }
        if (mm_insert_vma_sorted(task->mm, &moved_vma) != 0) {
            mm_free_vma_contents(&moved_vma);
            return ERR_PTR(-ENOMEM);
        }
        if (munmap_impl(old_address, (size_t)old_len) != 0) {
            mm_remove_exact_vma(task->mm, new_start, new_start + new_len);
            return ERR_PTR(-EFAULT);
        }
        (void)mm_merge_adjacent_vmas(task->mm);
        task_mm_update_high_water_impl(task->mm);
        return new_address;
    }
    if ((flags & MREMAP_FIXED) != 0 && new_len > old_len) {
        struct task_vma moved_vma;

        if (mm_copy_vma_slice((struct task_vma *)found, old_start, old_start + old_len, &moved_vma) != 0) {
            return ERR_PTR(-ENOMEM);
        }
        moved_vma.start = new_start;
        moved_vma.end = new_start + old_len;
        if (mm_extend_copied_vma(&moved_vma, new_len) != 0) {
            mm_free_vma_contents(&moved_vma);
            return ERR_PTR(-ENOMEM);
        }
        if (munmap_impl(new_address, (size_t)new_len) != 0) {
            mm_free_vma_contents(&moved_vma);
            return ERR_PTR(-EFAULT);
        }
        if (mm_insert_vma_sorted(task->mm, &moved_vma) != 0) {
            mm_free_vma_contents(&moved_vma);
            return ERR_PTR(-ENOMEM);
        }
        if (munmap_impl(old_address, (size_t)old_len) != 0) {
            mm_remove_exact_vma(task->mm, new_start, new_start + new_len);
            return ERR_PTR(-EFAULT);
        }
        (void)mm_merge_adjacent_vmas(task->mm);
        task_mm_update_high_water_impl(task->mm);
        return new_address;
    }
    if (new_len <= old_len) {
        if (new_len < old_len &&
            munmap_impl((void *)(unsigned long)(old_start + new_len), (size_t)(old_len - new_len)) != 0) {
            return ERR_PTR(-EFAULT);
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
        uint32_t *page_flags = __kmalloc_noprof((size_t)new_pages * sizeof(*page_flags), GFP_KERNEL | __GFP_ZERO);
        uint8_t *resident_pages = __kmalloc_noprof((size_t)new_pages * sizeof(*resident_pages), GFP_KERNEL | __GFP_ZERO);
        uint8_t *dirty_pages = __kmalloc_noprof((size_t)new_pages * sizeof(*dirty_pages), GFP_KERNEL | __GFP_ZERO);
        void *new_image = NULL;
        struct vm_private_page **private_pages = NULL;
        struct vm_shared_mapping **shared_pages = NULL;

        if (!page_flags || !resident_pages || !dirty_pages) {
            kfree(page_flags);
            kfree(resident_pages);
            kfree(dirty_pages);
            return ERR_PTR(-ENOMEM);
        }
        memcpy(page_flags, vma->page_flags, (size_t)old_pages * sizeof(*page_flags));
        memcpy(resident_pages, vma->resident_pages, (size_t)old_pages * sizeof(*resident_pages));
        memcpy(dirty_pages, vma->dirty_pages, (size_t)old_pages * sizeof(*dirty_pages));
        for (uint64_t i = old_pages; i < new_pages; i++) {
            page_flags[i] = vma->flags;
            resident_pages[i] = 1;
        }
        if (!vma->private_pages && !vma->shared_pages && !vma->shared_mapping) {
            new_image = __kmalloc_noprof((size_t)new_len, GFP_KERNEL | __GFP_ZERO);
            if (!new_image) {
                kfree(page_flags);
                kfree(resident_pages);
                kfree(dirty_pages);
                return ERR_PTR(-ENOMEM);
            }
            memcpy(new_image, vma->image, (size_t)old_len);
        } else if (vma->private_pages) {
            private_pages = __kmalloc_noprof((size_t)new_pages * sizeof(*private_pages), GFP_KERNEL | __GFP_ZERO);
            if (!private_pages) {
                kfree(page_flags);
                kfree(resident_pages);
                kfree(dirty_pages);
                return ERR_PTR(-ENOMEM);
            }
            memcpy(private_pages, vma->private_pages, (size_t)old_pages * sizeof(*private_pages));
            for (uint64_t i = old_pages; i < new_pages; i++) {
                private_pages[i] = mm_private_page_alloc(NULL, 0);
                if (!private_pages[i]) {
                    for (uint64_t j = old_pages; j < i; j++) {
                        mm_private_page_put_impl(private_pages[j]);
                    }
                    kfree(private_pages);
                    kfree(page_flags);
                    kfree(resident_pages);
                    kfree(dirty_pages);
                    return ERR_PTR(-ENOMEM);
                }
            }
        } else if (vma->shared_pages && vma->backing_fd >= 0) {
            shared_pages = __kmalloc_noprof((size_t)new_pages * sizeof(*shared_pages), GFP_KERNEL | __GFP_ZERO);
            if (!shared_pages) {
                kfree(page_flags);
                kfree(resident_pages);
                kfree(dirty_pages);
                return ERR_PTR(-ENOMEM);
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
                    kfree(shared_pages);
                    kfree(page_flags);
                    kfree(resident_pages);
                    kfree(dirty_pages);
                    return ERR_PTR(-ENOMEM);
                }
                shared_pages[i] = page[0];
                kfree(page);
            }
        } else if (vma->shared_mapping && vma->backing_fd >= 0) {
            shared_pages = mm_shared_pages_get_or_create(vma->backing_fd, vma->backing_offset, new_pages);
            if (IS_ERR(shared_pages)) {
                long err = PTR_ERR(shared_pages);
                kfree(page_flags);
                kfree(resident_pages);
                kfree(dirty_pages);
                return ERR_PTR(err);
            }
        } else {
            kfree(page_flags);
            kfree(resident_pages);
            kfree(dirty_pages);
            return ERR_PTR(-ENOSYS);
        }
        kfree(vma->page_flags);
        kfree(vma->resident_pages);
        kfree(vma->dirty_pages);
        vma->page_flags = page_flags;
        vma->resident_pages = resident_pages;
        vma->dirty_pages = dirty_pages;
        if (private_pages) {
            kfree(vma->private_pages);
            vma->private_pages = private_pages;
        } else if (shared_pages) {
            kfree(vma->shared_pages);
            vma->shared_pages = shared_pages;
            if (vma->shared_mapping) {
                mm_shared_mapping_put(vma->shared_mapping);
                vma->shared_mapping = NULL;
                vma->shared_mapping_offset = 0;
            }
            vma->image = shared_pages[0] ? shared_pages[0]->image : NULL;
        } else {
            kfree(vma->image);
            vma->image = new_image;
        }
        for (uint64_t i = old_pages; i < old_pages + added_pages; i++) {
            vma->dirty_pages[i] = 0;
        }
        vma->end = old_start + new_len;
        vma->image_size = (size_t)new_len;
        vma->page_count = old_pages + added_pages;
        (void)mm_merge_adjacent_vmas(task->mm);
        task_mm_update_high_water_impl(task->mm);
        return old_address;
    }
    if ((flags & MREMAP_MAYMOVE) == 0) {
        return ERR_PTR(-ENOMEM);
    }
    vma_flags = found->flags;
    mapped = mmap_impl((flags & MREMAP_FIXED) ? new_address : NULL, (size_t)new_len,
                       ((vma_flags & PF_R) ? PROT_READ : 0) |
                       ((vma_flags & PF_W) ? PROT_WRITE : 0) |
                       ((vma_flags & PF_X) ? PROT_EXEC : 0),
                       MAP_PRIVATE | MAP_ANONYMOUS | ((flags & MREMAP_FIXED) ? MAP_FIXED : 0),
                       -1, 0);
    if ((long)(unsigned long)mapped < 0) {
        return mapped;
    }
    copy_len = old_size < new_size ? old_size : new_size;
    if (copy_len > 0) {
        void *tmp = __kmalloc_noprof(copy_len, GFP_KERNEL);
        if (!tmp) {
            munmap_impl(mapped, new_len);
            return ERR_PTR(-ENOMEM);
        }
        if (task_read_virtual_memory_impl(task, old_start, tmp, copy_len) != (long)copy_len ||
            task_write_virtual_memory_impl(task, (uint64_t)(unsigned long)mapped, tmp, copy_len) != (long)copy_len) {
            kfree(tmp);
            munmap_impl(mapped, new_len);
            return ERR_PTR(-EFAULT);
        }
        kfree(tmp);
    }
    if (munmap_impl(old_address, old_len) != 0) {
        munmap_impl(mapped, new_len);
        return ERR_PTR(-EFAULT);
    }
    (void)mm_merge_adjacent_vmas(task->mm);
    task_mm_update_high_water_impl(task->mm);
    return mapped;
}

int msync_impl(void *addr, size_t len, int flags) {
    struct task *task = task_current();
    uint64_t start = (uint64_t)(unsigned long)addr;
    uint64_t size;
    uint64_t end;
    int synced = 0;

    if ((flags & ~(MS_ASYNC | MS_SYNC | MS_INVALIDATE)) != 0 ||
        ((flags & MS_ASYNC) != 0 && (flags & MS_SYNC) != 0)) {
        return -EINVAL;
    }
    if (!task || !task->mm) {
        return -ENOMEM;
    }
    if (len == 0) {
        return 0;
    }
    if ((start % TASK_VMA_PAGE_SIZE) != 0) {
        return -EINVAL;
    }
    size = mm_align_up((uint64_t)len, TASK_VMA_PAGE_SIZE);
    if (size == 0 || size > U64_MAX - start) {
        return -ENOMEM;
    }
    end = start + size;
    for (uint32_t i = 0; i < task->mm->vma_count; i++) {
        struct task_vma *vma = &task->mm->vmas[i];
        uint64_t overlap_start;
        uint64_t overlap_end;
        uint64_t start_page;
        uint64_t end_page;
        long long file_size;

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
        file_size = mm_vma_file_size_impl(vma);
        for (uint64_t page = start_page; page <= end_page && page < vma->page_count; page++) {
            uint64_t page_start = vma->start + (page * TASK_VMA_PAGE_SIZE);
            uint64_t page_end = page_start + TASK_VMA_PAGE_SIZE;
            uint64_t write_start = overlap_start > page_start ? overlap_start : page_start;
            uint64_t write_end = overlap_end < page_end ? overlap_end : page_end;
            size_t image_offset;
            size_t to_write;
            int full_page_writeback;
            long long written;

            if (vma->dirty_pages && vma->dirty_pages[page] == 0) {
                continue;
            }
            image_offset = (size_t)(write_start - vma->start);
            to_write = (size_t)(write_end - write_start);
            full_page_writeback = write_start == page_start && write_end == page_end;
            if (file_size >= 0) {
                uint64_t file_write_start = vma->backing_offset + (uint64_t)image_offset;

                if (file_write_start >= (uint64_t)file_size) {
                    continue;
                }
                if ((uint64_t)to_write > (uint64_t)file_size - file_write_start) {
                    to_write = (size_t)((uint64_t)file_size - file_write_start);
                    full_page_writeback = 0;
                }
            }
            if (to_write == 0) {
                continue;
            }
            const unsigned char *source = vma->shared_pages && vma->shared_pages[page]
                ? vma->shared_pages[page]->image + (image_offset % TASK_VMA_PAGE_SIZE)
                : (const unsigned char *)vma->image + image_offset;
            written = mm_vma_pwrite(vma, source, to_write,
                                    (long long)(vma->backing_offset + image_offset));
            if (written < 0) {
                return -1;
            }
            if ((size_t)written != to_write) {
                return -EIO;
            }
            if (vma->dirty_pages && full_page_writeback) {
                vma->dirty_pages[page] = 0;
            }
        }
        synced = 1;
    }
    if (!synced) {
        return -ENOMEM;
    }
    return 0;
}

void *brk_impl(void *addr) {
    struct task *task = task_current();
    uint64_t requested = (uint64_t)(unsigned long)addr;
    uint64_t aligned;
    uint32_t brk_index = 0;
    int has_brk_vma;

    if (!task) {
        return ERR_PTR(-ESRCH);
    }
    if (!task->mm) {
        task->mm = __kmalloc_noprof(sizeof(*task->mm), GFP_KERNEL | __GFP_ZERO);
        if (!task->mm) {
            return ERR_PTR(-ENOMEM);
        }
        atomic_set(&task->mm->refs, 1);
    }
    if (task->mm->brk_start == 0) {
        task->mm->brk_start = MM_BRK_BASE;
        task->mm->brk_current = MM_BRK_BASE;
    }
    if (requested == 0) {
        return (void *)(unsigned long)task->mm->brk_current;
    }
    if (requested < task->mm->brk_start || requested >= MM_USER_BASE) {
        return (void *)(unsigned long)task->mm->brk_current;
    }

    aligned = mm_align_up(requested - task->mm->brk_start, TASK_VMA_PAGE_SIZE);
    if (aligned == 0 && requested > task->mm->brk_start) {
        return (void *)(unsigned long)task->mm->brk_current;
    }

    has_brk_vma = mm_find_brk_vma(task->mm, &brk_index);
    if (aligned == 0) {
        if (has_brk_vma) {
            mm_remove_vma_at(task->mm, brk_index);
        }
        task->mm->brk_current = task->mm->brk_start;
        return (void *)(unsigned long)task->mm->brk_current;
    }

    if (!has_brk_vma) {
        void *image = __kmalloc_noprof((size_t)aligned, GFP_KERNEL | __GFP_ZERO);
        if (!image) {
            return (void *)(unsigned long)task->mm->brk_current;
        }
        if (mm_add_vma(task->mm, task->mm->brk_start, aligned, PF_R | PF_W, TASK_VMA_ANON, image) != 0) {
            kfree(image);
            return (void *)(unsigned long)task->mm->brk_current;
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
            return (void *)(unsigned long)task->mm->brk_current;
        }

        resized_image = __kmalloc_noprof((size_t)aligned, GFP_KERNEL | __GFP_ZERO);
        if (!resized_image) {
            return (void *)(unsigned long)task->mm->brk_current;
        }
        resized_flags = __kmalloc_noprof((size_t)page_count * sizeof(*resized_flags), GFP_KERNEL | __GFP_ZERO);
        if (!resized_flags) {
            kfree(resized_image);
            return (void *)(unsigned long)task->mm->brk_current;
        }
        copy_size = vma->image_size < (size_t)aligned ? vma->image_size : (size_t)aligned;
        memcpy(resized_image, vma->image, copy_size);
        for (uint64_t i = 0; i < page_count; i++) {
            resized_flags[i] = PF_R | PF_W;
        }
        kfree(vma->image);
        kfree(vma->page_flags);
        vma->image = resized_image;
        vma->image_size = (size_t)aligned;
        vma->page_flags = resized_flags;
        vma->page_count = page_count;
        vma->end = task->mm->brk_start + aligned;
    }

    task->mm->brk_current = requested;
    return (void *)(unsigned long)task->mm->brk_current;
}
