/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_ORLIX_CMPXCHG_H
#define _ASM_ORLIX_CMPXCHG_H

#include <linux/build_bug.h>
#include <linux/compiler.h>
#include <linux/types.h>

static __always_inline unsigned long __orlix_xchg(unsigned long x,
						 volatile void *ptr, int size)
{
	switch (size) {
	case 1:
		return __atomic_exchange_n((volatile u8 *)ptr, (u8)x,
					   __ATOMIC_SEQ_CST);
	case 2:
		return __atomic_exchange_n((volatile u16 *)ptr, (u16)x,
					   __ATOMIC_SEQ_CST);
	case 4:
		return __atomic_exchange_n((volatile u32 *)ptr, (u32)x,
					   __ATOMIC_SEQ_CST);
	case 8:
		return __atomic_exchange_n((volatile u64 *)ptr, (u64)x,
					   __ATOMIC_SEQ_CST);
	default:
		BUILD_BUG();
	}

	unreachable();
}

#define arch_xchg(ptr, x) \
({ \
	__typeof__(*(ptr)) __ret; \
	__ret = (__typeof__(*(ptr)))__orlix_xchg((unsigned long)(x), (ptr), \
						  sizeof(*(ptr))); \
	__ret; \
})

static __always_inline unsigned long __orlix_cmpxchg(volatile void *ptr,
						    unsigned long old,
						    unsigned long new,
						    int size)
{
	switch (size) {
	case 1: {
		u8 expected = old;

		__atomic_compare_exchange_n((volatile u8 *)ptr, &expected, (u8)new,
					    false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
		return expected;
	}
	case 2: {
		u16 expected = old;

		__atomic_compare_exchange_n((volatile u16 *)ptr, &expected, (u16)new,
					    false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
		return expected;
	}
	case 4: {
		u32 expected = old;

		__atomic_compare_exchange_n((volatile u32 *)ptr, &expected, (u32)new,
					    false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
		return expected;
	}
	case 8: {
		u64 expected = old;

		__atomic_compare_exchange_n((volatile u64 *)ptr, &expected, (u64)new,
					    false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
		return expected;
	}
	default:
		BUILD_BUG();
	}

	unreachable();
}

#define arch_cmpxchg(ptr, old, new) \
({ \
	__typeof__(*(ptr)) __ret; \
	__ret = (__typeof__(*(ptr)))__orlix_cmpxchg((ptr), (unsigned long)(old), \
						     (unsigned long)(new), sizeof(*(ptr))); \
	__ret; \
})

#define arch_xchg_relaxed	arch_xchg
#define arch_xchg_acquire	arch_xchg
#define arch_xchg_release	arch_xchg

#define arch_cmpxchg_relaxed	arch_cmpxchg
#define arch_cmpxchg_acquire	arch_cmpxchg
#define arch_cmpxchg_release	arch_cmpxchg
#define arch_cmpxchg_local	arch_cmpxchg_relaxed

#define arch_cmpxchg64		arch_cmpxchg
#define arch_cmpxchg64_relaxed	arch_cmpxchg_relaxed
#define arch_cmpxchg64_acquire	arch_cmpxchg_acquire
#define arch_cmpxchg64_release	arch_cmpxchg_release
#define arch_cmpxchg64_local	arch_cmpxchg_local

#endif /* _ASM_ORLIX_CMPXCHG_H */
