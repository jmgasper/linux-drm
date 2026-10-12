/* asm-generic/div64.h for 64-bit: do_div() divides n in place and returns
   the remainder. */
#ifndef _ASM_DIV64_H
#define _ASM_DIV64_H
#define do_div(n, base) ({ \
	uint32_t __base = (base); \
	uint32_t __rem; \
	__rem = ((uint64_t)(n)) % __base; \
	(n) = ((uint64_t)(n)) / __base; \
	__rem; \
})
#endif
