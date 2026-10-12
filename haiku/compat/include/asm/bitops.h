/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _ASM_X86_BITOPS_H
#define _ASM_X86_BITOPS_H

/*	The architecture part of linux/bitops.h: atomic bit operations with GCC's
	__atomic builtins, the non-atomic ones from asm-generic, and the bit
	searches with GCC's builtins. */

#ifndef _LINUX_BITOPS_H
#	error only <linux/bitops.h> can be included directly
#endif

#define BIT_WORD_PTR(nr, addr)	(((volatile unsigned long *)(addr)) + BIT_WORD(nr))

static __always_inline void set_bit(long nr, volatile unsigned long *addr)
{
	__atomic_fetch_or(BIT_WORD_PTR(nr, addr), BIT_MASK(nr), __ATOMIC_SEQ_CST);
}

static __always_inline void clear_bit(long nr, volatile unsigned long *addr)
{
	__atomic_fetch_and(BIT_WORD_PTR(nr, addr), ~BIT_MASK(nr), __ATOMIC_SEQ_CST);
}

static __always_inline void change_bit(long nr, volatile unsigned long *addr)
{
	__atomic_fetch_xor(BIT_WORD_PTR(nr, addr), BIT_MASK(nr), __ATOMIC_SEQ_CST);
}

static __always_inline bool test_and_set_bit(long nr, volatile unsigned long *addr)
{
	return (__atomic_fetch_or(BIT_WORD_PTR(nr, addr), BIT_MASK(nr),
		__ATOMIC_SEQ_CST) & BIT_MASK(nr)) != 0;
}

static __always_inline bool test_and_clear_bit(long nr, volatile unsigned long *addr)
{
	return (__atomic_fetch_and(BIT_WORD_PTR(nr, addr), ~BIT_MASK(nr),
		__ATOMIC_SEQ_CST) & BIT_MASK(nr)) != 0;
}

static __always_inline bool test_and_change_bit(long nr, volatile unsigned long *addr)
{
	return (__atomic_fetch_xor(BIT_WORD_PTR(nr, addr), BIT_MASK(nr),
		__ATOMIC_SEQ_CST) & BIT_MASK(nr)) != 0;
}

#define test_and_set_bit_lock(nr, addr)	test_and_set_bit(nr, addr)
#define clear_bit_unlock(nr, addr)		clear_bit(nr, addr)
#define __clear_bit_unlock(nr, addr)	clear_bit(nr, addr)
#define clear_bit_unlock_is_negative_byte(nr, addr) \
	({ clear_bit(nr, addr); (*(volatile signed char *)(addr)) < 0; })
#define set_mask_bits(ptr, mask, bits) ({ \
	const typeof(*(ptr)) mask__ = (mask), bits__ = (bits); \
	typeof(*(ptr)) old__, new__; \
	old__ = READ_ONCE(*(ptr)); \
	do { \
		new__ = (old__ & ~mask__) | bits__; \
	} while (!__atomic_compare_exchange_n((ptr), &old__, new__, false, \
		__ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)); \
	old__; \
})

/* the non-atomic and test variants, generic C */
#define arch___set_bit				generic___set_bit
#define arch___clear_bit			generic___clear_bit
#define arch___change_bit			generic___change_bit
#define arch___test_and_set_bit		generic___test_and_set_bit
#define arch___test_and_clear_bit	generic___test_and_clear_bit
#define arch___test_and_change_bit	generic___test_and_change_bit
#define arch_test_bit				generic_test_bit
#define arch_test_bit_acquire		generic_test_bit_acquire
#include <asm-generic/bitops/instrumented-non-atomic.h>

static __always_inline unsigned long __ffs(unsigned long word)
{
	return __builtin_ctzl(word);
}

static __always_inline unsigned long ffz(unsigned long word)
{
	return __builtin_ctzl(~word);
}

static __always_inline unsigned long __fls(unsigned long word)
{
	return BITS_PER_LONG - 1 - __builtin_clzl(word);
}

static __always_inline int ffs(int x)
{
	return __builtin_ffs(x);
}

static __always_inline int fls(unsigned int x)
{
	return x ? 32 - __builtin_clz(x) : 0;
}

static __always_inline int fls64(__u64 x)
{
	return x ? 64 - __builtin_clzll(x) : 0;
}

static __always_inline unsigned int __arch_hweight32(unsigned int w)
{
	return __builtin_popcount(w);
}

static __always_inline unsigned int __arch_hweight16(unsigned int w)
{
	return __builtin_popcount(w & 0xffff);
}

static __always_inline unsigned int __arch_hweight8(unsigned int w)
{
	return __builtin_popcount(w & 0xff);
}

static __always_inline unsigned long __arch_hweight64(__u64 w)
{
	return __builtin_popcountll(w);
}

#include <asm-generic/bitops/const_hweight.h>
#include <asm-generic/bitops/le.h>
#include <asm-generic/bitops/sched.h>

#endif /* _ASM_X86_BITOPS_H */
