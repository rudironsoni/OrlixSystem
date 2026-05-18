/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_ORLIX_ATOMIC_H
#define _ASM_ORLIX_ATOMIC_H

#include <linux/types.h>
#include <asm-generic/atomic.h>
#include <asm/cmpxchg.h>

#define ATOMIC64_INIT(i)	{ (i) }

#define arch_atomic64_read(v) READ_ONCE((v)->counter)
#define arch_atomic64_set(v, i) WRITE_ONCE(((v)->counter), (i))

#define ATOMIC64_OP(op, c_op)                                      \
static inline void arch_atomic64_##op(s64 i, atomic64_t *v)         \
{                                                                  \
	s64 c, old;                                                       \
	c = arch_atomic64_read(v);                                       \
	while ((old = arch_cmpxchg64(&v->counter, c, c c_op i)) != c)    \
		c = old;                                                       \
}

#define ATOMIC64_OP_RETURN(op, c_op)                               \
static inline s64 arch_atomic64_##op##_return(s64 i, atomic64_t *v) \
{                                                                  \
	s64 c, old, ret;                                                  \
	c = arch_atomic64_read(v);                                       \
	for (;;) {                                                       \
		ret = c c_op i;                                                \
		old = arch_cmpxchg64(&v->counter, c, ret);                     \
		if (old == c)                                                  \
			return ret;                                                  \
		c = old;                                                       \
	}                                                                \
}

#define ATOMIC64_FETCH_OP(op, c_op)                                \
static inline s64 arch_atomic64_fetch_##op(s64 i, atomic64_t *v)    \
{                                                                  \
	s64 c, old;                                                       \
	c = arch_atomic64_read(v);                                       \
	while ((old = arch_cmpxchg64(&v->counter, c, c c_op i)) != c)    \
		c = old;                                                       \
	return c;                                                        \
}

ATOMIC64_OP(add, +)
ATOMIC64_OP(sub, -)
ATOMIC64_OP(and, &)
ATOMIC64_OP(or, |)
ATOMIC64_OP(xor, ^)

ATOMIC64_OP_RETURN(add, +)
ATOMIC64_OP_RETURN(sub, -)

ATOMIC64_FETCH_OP(add, +)
ATOMIC64_FETCH_OP(sub, -)
ATOMIC64_FETCH_OP(and, &)
ATOMIC64_FETCH_OP(or, |)
ATOMIC64_FETCH_OP(xor, ^)

#define arch_atomic64_add_return arch_atomic64_add_return
#define arch_atomic64_add_return_acquire arch_atomic64_add_return
#define arch_atomic64_add_return_release arch_atomic64_add_return
#define arch_atomic64_add_return_relaxed arch_atomic64_add_return
#define arch_atomic64_sub_return arch_atomic64_sub_return
#define arch_atomic64_sub_return_acquire arch_atomic64_sub_return
#define arch_atomic64_sub_return_release arch_atomic64_sub_return
#define arch_atomic64_sub_return_relaxed arch_atomic64_sub_return

#define arch_atomic64_fetch_add arch_atomic64_fetch_add
#define arch_atomic64_fetch_add_acquire arch_atomic64_fetch_add
#define arch_atomic64_fetch_add_release arch_atomic64_fetch_add
#define arch_atomic64_fetch_add_relaxed arch_atomic64_fetch_add
#define arch_atomic64_fetch_sub arch_atomic64_fetch_sub
#define arch_atomic64_fetch_sub_acquire arch_atomic64_fetch_sub
#define arch_atomic64_fetch_sub_release arch_atomic64_fetch_sub
#define arch_atomic64_fetch_sub_relaxed arch_atomic64_fetch_sub
#define arch_atomic64_fetch_and arch_atomic64_fetch_and
#define arch_atomic64_fetch_and_acquire arch_atomic64_fetch_and
#define arch_atomic64_fetch_and_release arch_atomic64_fetch_and
#define arch_atomic64_fetch_and_relaxed arch_atomic64_fetch_and
#define arch_atomic64_fetch_or arch_atomic64_fetch_or
#define arch_atomic64_fetch_or_acquire arch_atomic64_fetch_or
#define arch_atomic64_fetch_or_release arch_atomic64_fetch_or
#define arch_atomic64_fetch_or_relaxed arch_atomic64_fetch_or
#define arch_atomic64_fetch_xor arch_atomic64_fetch_xor
#define arch_atomic64_fetch_xor_acquire arch_atomic64_fetch_xor
#define arch_atomic64_fetch_xor_release arch_atomic64_fetch_xor
#define arch_atomic64_fetch_xor_relaxed arch_atomic64_fetch_xor

#undef ATOMIC64_FETCH_OP
#undef ATOMIC64_OP_RETURN
#undef ATOMIC64_OP

static inline s64 arch_atomic64_cmpxchg(atomic64_t *v, s64 old, s64 new)
{
	return arch_cmpxchg64(&v->counter, old, new);
}

static inline s64 arch_atomic64_xchg(atomic64_t *v, s64 new)
{
	return arch_xchg(&v->counter, new);
}

static inline s64 arch_atomic64_fetch_add_unless(atomic64_t *v, s64 a, s64 u)
{
	s64 c, old;

	c = arch_atomic64_read(v);
	for (;;) {
		if (c == u)
			return c;
		old = arch_cmpxchg64(&v->counter, c, c + a);
		if (old == c)
			return c;
		c = old;
	}
}

static inline s64 arch_atomic64_dec_if_positive(atomic64_t *v)
{
	s64 c, old, dec;

	c = arch_atomic64_read(v);
	for (;;) {
		dec = c - 1;
		if (dec < 0)
			return dec;
		old = arch_cmpxchg64(&v->counter, c, dec);
		if (old == c)
			return dec;
		c = old;
	}
}

#endif /* _ASM_ORLIX_ATOMIC_H */
