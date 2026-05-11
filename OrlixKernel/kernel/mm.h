/* OrlixKernel/kernel/mm.h
 * Private virtual memory syscall substrate for Linux-shaped tasks.
 */

#ifndef KERNEL_MM_H
#define KERNEL_MM_H

#include <linux/stddef.h>
#include <linux/types.h>

struct task_vma;

#ifdef __cplusplus
extern "C" {
#endif

void *mmap_impl(void *addr, size_t length, int prot, int flags, int fd, int64_t offset);
int mprotect_impl(void *addr, size_t len, int prot);
int munmap_impl(void *addr, size_t len);
void *mremap_impl(void *old_address, size_t old_size, size_t new_size, int flags, void *new_address);
int madvise_impl(void *addr, size_t length, int advice);
int mincore_impl(void *addr, size_t length, unsigned char *vec);
void mm_note_file_truncate_impl(int fd, int64_t length);
long long mm_vma_file_remaining_impl(const struct task_vma *vma, size_t offset);
long long mm_vma_file_size_impl(const struct task_vma *vma);
void *brk_impl(void *addr);
int msync_impl(void *addr, size_t len, int flags);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_MM_H */
