/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_ORLIX_UACCESS_H
#define _ASM_ORLIX_UACCESS_H

#include <linux/compiler.h>

unsigned long raw_copy_from_user(void *to, const void __user *from,
				 unsigned long n);
unsigned long raw_copy_to_user(void __user *to, const void *from,
			       unsigned long n);
unsigned long __clear_user(void __user *to, unsigned long n);

#define INLINE_COPY_FROM_USER
#define INLINE_COPY_TO_USER
#define __clear_user	__clear_user

#include <asm-generic/uaccess.h>

#endif /* _ASM_ORLIX_UACCESS_H */
