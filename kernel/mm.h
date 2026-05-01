/* IXLandSystem/kernel/mm.h
 * Private virtual memory syscall substrate for Linux-shaped tasks.
 */

#ifndef KERNEL_MM_H
#define KERNEL_MM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void *mmap_impl(void *addr, size_t length, int prot, int flags, int fd, int64_t offset);
int mprotect_impl(void *addr, size_t len, int prot);
int munmap_impl(void *addr, size_t len);
void *brk_impl(void *addr);
int msync_impl(void *addr, size_t len, int flags);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_MM_H */
