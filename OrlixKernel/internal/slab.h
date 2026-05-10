#ifndef INTERNAL_SLAB_H
#define INTERNAL_SLAB_H

#include <linux/gfp_types.h>
#include <linux/stdarg.h>
#include <linux/types.h>

void *__kmalloc_noprof(size_t size, gfp_t flags);
void kfree(const void *ptr);
char *kstrdup(const char *src, gfp_t flags);

int vsnprintf(char *buf, size_t size, const char *fmt, va_list args);
int snprintf(char *buf, size_t size, const char *fmt, ...);
int vscnprintf(char *buf, size_t size, const char *fmt, va_list args);
int scnprintf(char *buf, size_t size, const char *fmt, ...);

#endif
