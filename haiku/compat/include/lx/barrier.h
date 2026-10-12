/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _LX_BARRIER_H
#define _LX_BARRIER_H

/* x86-64 memory ordering, as arch/x86/include/asm/barrier.h */

#define mb()	asm volatile("mfence" ::: "memory")
#define rmb()	asm volatile("lfence" ::: "memory")
#define wmb()	asm volatile("sfence" ::: "memory")
#define dma_rmb()	barrier()
#define dma_wmb()	barrier()
#define dma_mb()	mb()
#define smp_mb()	asm volatile("lock; addl $0,-4(%%rsp)" ::: "memory", "cc")
#define smp_rmb()	barrier()
#define smp_wmb()	barrier()
#define __smp_mb()	smp_mb()
#define __smp_rmb()	smp_rmb()
#define __smp_wmb()	smp_wmb()
#define smp_mb__before_atomic()	barrier()
#define smp_mb__after_atomic()	barrier()
#define smp_mb__after_spinlock()	smp_mb()
#define smp_mb__after_unlock_lock()	do { } while (0)
#define smp_store_mb(var, value)	do { WRITE_ONCE(var, value); smp_mb(); } while (0)
#define smp_store_release(p, v)	do { barrier(); WRITE_ONCE(*(p), v); } while (0)
#define smp_load_acquire(p) ({ \
	__unqual_scalar_typeof(*(p)) ___p1 = READ_ONCE(*(p)); \
	barrier(); \
	(typeof(*(p)))___p1; \
})
#define virt_mb()	smp_mb()
#define virt_rmb()	smp_rmb()
#define virt_wmb()	smp_wmb()
#define smp_cond_load_relaxed(ptr, cond_expr) ({ \
	typeof(ptr) __PTR = (ptr); \
	__unqual_scalar_typeof(*ptr) VAL; \
	for (;;) { \
		VAL = READ_ONCE(*__PTR); \
		if (cond_expr) \
			break; \
		cpu_relax(); \
	} \
	(typeof(*ptr))VAL; \
})
#define smp_cond_load_acquire(ptr, cond_expr) ({ \
	__unqual_scalar_typeof(*ptr) _val = smp_cond_load_relaxed(ptr, cond_expr); \
	barrier(); \
	(typeof(*ptr))_val; \
})
#define smp_acquire__after_ctrl_dep()	barrier()
#define array_index_mask_nospec(index, size) ({ \
	unsigned long __mask; \
	asm volatile("cmp %1,%2; sbb %0,%0;" \
		: "=r" (__mask) : "g" ((unsigned long)(size)), "r" ((unsigned long)(index)) \
		: "cc"); \
	__mask; \
})
#define barrier_nospec()	asm volatile("lfence" ::: "memory")

static __always_inline void rep_nop(void)
{
	asm volatile("rep; nop" ::: "memory");
}
#define cpu_relax()	rep_nop()

#endif /* _LX_BARRIER_H */
