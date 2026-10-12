/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _LX_ATOMIC_H
#define _LX_ATOMIC_H

/*	Linux's atomic_t, atomic64_t, atomic_long_t, cmpxchg()/xchg() and
	refcount_t over GCC's __atomic builtins. On x86-64 every locked operation
	is a full barrier, so the _relaxed/_acquire/_release variants all map to
	the sequentially consistent builtins. */

#define __lx_mo	__ATOMIC_SEQ_CST

/* #pragma mark - cmpxchg, xchg */

#define xchg(ptr, value)	__atomic_exchange_n((ptr), (value), __lx_mo)
#define xchg_relaxed		xchg
#define xchg_acquire		xchg
#define xchg_release		xchg

#define cmpxchg(ptr, old, new) ({ \
	__typeof__(*(ptr)) __lx_old = (old); \
	__atomic_compare_exchange_n((ptr), &__lx_old, (new), false, __lx_mo, \
		__lx_mo); \
	__lx_old; \
})
#define cmpxchg_relaxed		cmpxchg
#define cmpxchg_acquire		cmpxchg
#define cmpxchg_release		cmpxchg
#define cmpxchg64			cmpxchg
#define cmpxchg64_relaxed	cmpxchg
#define cmpxchg_local		cmpxchg
#define cmpxchg64_local		cmpxchg
#define sync_cmpxchg		cmpxchg

#define try_cmpxchg(ptr, oldp, new) \
	__atomic_compare_exchange_n((ptr), (oldp), (new), false, __lx_mo, __lx_mo)
#define try_cmpxchg_relaxed	try_cmpxchg
#define try_cmpxchg_acquire	try_cmpxchg
#define try_cmpxchg_release	try_cmpxchg
#define try_cmpxchg64		try_cmpxchg
#define try_cmpxchg_local	try_cmpxchg

/* #pragma mark - atomic_t, atomic64_t, atomic_long_t */

#define ATOMIC_INIT(i)		{ (i) }
#define ATOMIC64_INIT(i)	{ (i) }
#define ATOMIC_LONG_INIT(i)	{ (i) }

typedef atomic64_t atomic_long_t;

#define __LX_ATOMIC_OPS(prefix, atype, type) \
static __always_inline type prefix##_read(const atype *v) \
{ return __atomic_load_n(&v->counter, __ATOMIC_RELAXED); } \
static __always_inline type prefix##_read_acquire(const atype *v) \
{ return __atomic_load_n(&v->counter, __ATOMIC_ACQUIRE); } \
static __always_inline void prefix##_set(atype *v, type i) \
{ __atomic_store_n(&v->counter, i, __ATOMIC_RELAXED); } \
static __always_inline void prefix##_set_release(atype *v, type i) \
{ __atomic_store_n(&v->counter, i, __ATOMIC_RELEASE); } \
static __always_inline void prefix##_add(type i, atype *v) \
{ __atomic_fetch_add(&v->counter, i, __lx_mo); } \
static __always_inline void prefix##_sub(type i, atype *v) \
{ __atomic_fetch_sub(&v->counter, i, __lx_mo); } \
static __always_inline void prefix##_inc(atype *v) \
{ __atomic_fetch_add(&v->counter, 1, __lx_mo); } \
static __always_inline void prefix##_dec(atype *v) \
{ __atomic_fetch_sub(&v->counter, 1, __lx_mo); } \
static __always_inline void prefix##_and(type i, atype *v) \
{ __atomic_fetch_and(&v->counter, i, __lx_mo); } \
static __always_inline void prefix##_andnot(type i, atype *v) \
{ __atomic_fetch_and(&v->counter, ~i, __lx_mo); } \
static __always_inline void prefix##_or(type i, atype *v) \
{ __atomic_fetch_or(&v->counter, i, __lx_mo); } \
static __always_inline void prefix##_xor(type i, atype *v) \
{ __atomic_fetch_xor(&v->counter, i, __lx_mo); } \
static __always_inline type prefix##_add_return(type i, atype *v) \
{ return __atomic_add_fetch(&v->counter, i, __lx_mo); } \
static __always_inline type prefix##_sub_return(type i, atype *v) \
{ return __atomic_sub_fetch(&v->counter, i, __lx_mo); } \
static __always_inline type prefix##_inc_return(atype *v) \
{ return __atomic_add_fetch(&v->counter, 1, __lx_mo); } \
static __always_inline type prefix##_dec_return(atype *v) \
{ return __atomic_sub_fetch(&v->counter, 1, __lx_mo); } \
static __always_inline type prefix##_fetch_add(type i, atype *v) \
{ return __atomic_fetch_add(&v->counter, i, __lx_mo); } \
static __always_inline type prefix##_fetch_sub(type i, atype *v) \
{ return __atomic_fetch_sub(&v->counter, i, __lx_mo); } \
static __always_inline type prefix##_fetch_inc(atype *v) \
{ return __atomic_fetch_add(&v->counter, 1, __lx_mo); } \
static __always_inline type prefix##_fetch_dec(atype *v) \
{ return __atomic_fetch_sub(&v->counter, 1, __lx_mo); } \
static __always_inline type prefix##_fetch_and(type i, atype *v) \
{ return __atomic_fetch_and(&v->counter, i, __lx_mo); } \
static __always_inline type prefix##_fetch_andnot(type i, atype *v) \
{ return __atomic_fetch_and(&v->counter, ~i, __lx_mo); } \
static __always_inline type prefix##_fetch_or(type i, atype *v) \
{ return __atomic_fetch_or(&v->counter, i, __lx_mo); } \
static __always_inline type prefix##_fetch_xor(type i, atype *v) \
{ return __atomic_fetch_xor(&v->counter, i, __lx_mo); } \
static __always_inline type prefix##_xchg(atype *v, type i) \
{ return __atomic_exchange_n(&v->counter, i, __lx_mo); } \
static __always_inline type prefix##_cmpxchg(atype *v, type old, type new) \
{ \
	__atomic_compare_exchange_n(&v->counter, &old, new, false, __lx_mo, \
		__lx_mo); \
	return old; \
} \
static __always_inline bool prefix##_try_cmpxchg(atype *v, type *old, type new) \
{ \
	return __atomic_compare_exchange_n(&v->counter, old, new, false, __lx_mo, \
		__lx_mo); \
} \
static __always_inline bool prefix##_sub_and_test(type i, atype *v) \
{ return prefix##_sub_return(i, v) == 0; } \
static __always_inline bool prefix##_dec_and_test(atype *v) \
{ return prefix##_dec_return(v) == 0; } \
static __always_inline bool prefix##_inc_and_test(atype *v) \
{ return prefix##_inc_return(v) == 0; } \
static __always_inline bool prefix##_add_negative(type i, atype *v) \
{ return prefix##_add_return(i, v) < 0; } \
static __always_inline type prefix##_fetch_add_unless(atype *v, type a, type u) \
{ \
	type c = prefix##_read(v); \
	do { \
		if (unlikely(c == u)) \
			break; \
	} while (!prefix##_try_cmpxchg(v, &c, c + a)); \
	return c; \
} \
static __always_inline bool prefix##_add_unless(atype *v, type a, type u) \
{ return prefix##_fetch_add_unless(v, a, u) != u; } \
static __always_inline bool prefix##_inc_not_zero(atype *v) \
{ return prefix##_add_unless(v, 1, 0); } \
static __always_inline bool prefix##_inc_unless_negative(atype *v) \
{ \
	type c = prefix##_read(v); \
	do { \
		if (unlikely(c < 0)) \
			return false; \
	} while (!prefix##_try_cmpxchg(v, &c, c + 1)); \
	return true; \
} \
static __always_inline type prefix##_dec_if_positive(atype *v) \
{ \
	type dec, c = prefix##_read(v); \
	do { \
		dec = c - 1; \
		if (unlikely(dec < 0)) \
			break; \
	} while (!prefix##_try_cmpxchg(v, &c, dec)); \
	return dec; \
}

__LX_ATOMIC_OPS(atomic, atomic_t, int)
__LX_ATOMIC_OPS(atomic64, atomic64_t, s64)
__LX_ATOMIC_OPS(atomic_long, atomic_long_t, long)

#define __LX_ATOMIC_ORDER_ALIASES(prefix) \
	enum { prefix##__lx_unused };

/* the ordering variants */
#define __LX_RELAXED(op)	op
#define atomic_add_return_relaxed		atomic_add_return
#define atomic_add_return_acquire		atomic_add_return
#define atomic_add_return_release		atomic_add_return
#define atomic_sub_return_relaxed		atomic_sub_return
#define atomic_inc_return_relaxed		atomic_inc_return
#define atomic_inc_return_acquire		atomic_inc_return
#define atomic_inc_return_release		atomic_inc_return
#define atomic_dec_return_relaxed		atomic_dec_return
#define atomic_dec_return_acquire		atomic_dec_return
#define atomic_dec_return_release		atomic_dec_return
#define atomic_fetch_add_relaxed		atomic_fetch_add
#define atomic_fetch_add_acquire		atomic_fetch_add
#define atomic_fetch_add_release		atomic_fetch_add
#define atomic_fetch_sub_relaxed		atomic_fetch_sub
#define atomic_fetch_sub_release		atomic_fetch_sub
#define atomic_fetch_inc_relaxed		atomic_fetch_inc
#define atomic_fetch_dec_relaxed		atomic_fetch_dec
#define atomic_fetch_or_relaxed			atomic_fetch_or
#define atomic_fetch_or_acquire			atomic_fetch_or
#define atomic_fetch_and_relaxed		atomic_fetch_and
#define atomic_fetch_andnot_relaxed		atomic_fetch_andnot
#define atomic_fetch_andnot_release		atomic_fetch_andnot
#define atomic_xchg_relaxed				atomic_xchg
#define atomic_xchg_acquire				atomic_xchg
#define atomic_xchg_release				atomic_xchg
#define atomic_cmpxchg_relaxed			atomic_cmpxchg
#define atomic_cmpxchg_acquire			atomic_cmpxchg
#define atomic_cmpxchg_release			atomic_cmpxchg
#define atomic_try_cmpxchg_relaxed		atomic_try_cmpxchg
#define atomic_try_cmpxchg_acquire		atomic_try_cmpxchg
#define atomic_try_cmpxchg_release		atomic_try_cmpxchg
#define atomic64_add_return_relaxed		atomic64_add_return
#define atomic64_inc_return_relaxed		atomic64_inc_return
#define atomic64_dec_return_relaxed		atomic64_dec_return
#define atomic64_fetch_add_relaxed		atomic64_fetch_add
#define atomic64_fetch_inc_relaxed		atomic64_fetch_inc
#define atomic64_xchg_relaxed			atomic64_xchg
#define atomic64_cmpxchg_relaxed		atomic64_cmpxchg
#define atomic64_try_cmpxchg_relaxed	atomic64_try_cmpxchg
#define atomic_long_add_return_relaxed	atomic_long_add_return
#define atomic_long_inc_return_relaxed	atomic_long_inc_return
#define atomic_long_fetch_add_relaxed	atomic_long_fetch_add
#define atomic_long_xchg_relaxed		atomic_long_xchg
#define atomic_long_cmpxchg_relaxed		atomic_long_cmpxchg
#define atomic_long_try_cmpxchg_relaxed	atomic_long_try_cmpxchg
#define atomic_long_try_cmpxchg_acquire	atomic_long_try_cmpxchg
#define atomic_long_try_cmpxchg_release	atomic_long_try_cmpxchg
#define atomic_long_cmpxchg_acquire		atomic_long_cmpxchg
#define atomic_long_cmpxchg_release		atomic_long_cmpxchg
#define atomic_cond_read_relaxed(v, c)	smp_cond_load_relaxed(&(v)->counter, (c))
#define atomic_cond_read_acquire(v, c)	smp_cond_load_acquire(&(v)->counter, (c))

/* atomic bit flags on unsigned long words are in asm/bitops.h */

/* #pragma mark - refcount_t */

typedef struct refcount_struct {
	atomic_t refs;
} refcount_t;

#define REFCOUNT_INIT(n)	{ .refs = ATOMIC_INIT(n), }
#define REFCOUNT_MAX		INT_MAX
#define REFCOUNT_SATURATED	(INT_MIN / 2)

enum refcount_saturation_type {
	REFCOUNT_ADD_NOT_ZERO_OVF,
	REFCOUNT_ADD_OVF,
	REFCOUNT_ADD_UAF,
	REFCOUNT_SUB_UAF,
	REFCOUNT_DEC_LEAK,
};
void refcount_warn_saturate(refcount_t *r, enum refcount_saturation_type t);

static inline void refcount_set(refcount_t *r, int n) { atomic_set(&r->refs, n); }
static inline void refcount_set_release(refcount_t *r, int n)
{ atomic_set_release(&r->refs, n); }
static inline unsigned int refcount_read(const refcount_t *r)
{ return atomic_read(&r->refs); }

static inline __must_check bool refcount_add_not_zero(int i, refcount_t *r)
{
	int old = refcount_read(r);
	do {
		if (!old)
			break;
	} while (!atomic_try_cmpxchg(&r->refs, &old, old + i));
	if (unlikely(old < 0 || old + i < 0))
		refcount_warn_saturate(r, REFCOUNT_ADD_NOT_ZERO_OVF);
	return old;
}
static inline __must_check bool refcount_inc_not_zero(refcount_t *r)
{ return refcount_add_not_zero(1, r); }
#define refcount_inc_not_zero_acquire(r)	refcount_inc_not_zero(r)

static inline void refcount_add(int i, refcount_t *r)
{
	int old = atomic_fetch_add(i, &r->refs);
	if (unlikely(!old))
		refcount_warn_saturate(r, REFCOUNT_ADD_UAF);
	else if (unlikely(old < 0 || old + i < 0))
		refcount_warn_saturate(r, REFCOUNT_ADD_OVF);
}
static inline void refcount_inc(refcount_t *r) { refcount_add(1, r); }

static inline __must_check bool refcount_sub_and_test(int i, refcount_t *r)
{
	int old = atomic_fetch_sub(i, &r->refs);
	if (old == i)
		return true;
	if (unlikely(old < 0 || old - i < 0))
		refcount_warn_saturate(r, REFCOUNT_SUB_UAF);
	return false;
}
static inline __must_check bool refcount_dec_and_test(refcount_t *r)
{ return refcount_sub_and_test(1, r); }
static inline void refcount_dec(refcount_t *r)
{
	if (unlikely(atomic_fetch_sub(1, &r->refs) <= 1))
		refcount_warn_saturate(r, REFCOUNT_DEC_LEAK);
}
bool refcount_dec_not_one(refcount_t *r);
bool refcount_dec_if_one(refcount_t *r);

#endif /* _LX_ATOMIC_H */
