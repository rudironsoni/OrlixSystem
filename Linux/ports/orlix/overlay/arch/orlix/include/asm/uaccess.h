/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_ORLIX_UACCESS_H
#define _ASM_ORLIX_UACCESS_H

#include <linux/compiler.h>
#include <linux/string.h>

static inline unsigned long raw_copy_from_user(void *to,
						       const void __user *from,
						       unsigned long n)
{
	memcpy(to, (const void __force *)from, n);
	return 0;
}

static inline unsigned long raw_copy_to_user(void __user *to,
						     const void *from,
						     unsigned long n)
{
	memcpy((void __force *)to, from, n);
	return 0;
}

#define INLINE_COPY_FROM_USER
#define INLINE_COPY_TO_USER

#include <asm-generic/uaccess.h>

#endif /* _ASM_ORLIX_UACCESS_H */
