/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2026, air/OS.
 *
 * Linux's <linux/string.h> for the compat layer. Its declarations and a few
 * of its small macros follow Linux's include/linux/string.h, which is under
 * the GPL-2.0, and so is this file.
 */
#ifndef _LINUX_STRING_H_
#define _LINUX_STRING_H_

/*	Linux's string API: the C library functions Haiku's kernel exports, and
	Linux's own (compat/src/string.c). */

#include <linux/args.h>
#include <linux/array_size.h>
#include <linux/cleanup.h>
#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/overflow.h>
#include <linux/stdarg.h>

void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
int bcmp(const void *a, const void *b, size_t n);
void *memchr(const void *s, int c, size_t n);
void *memscan(void *addr, int c, size_t size);
size_t strlen(const char *s);
size_t strnlen(const char *s, size_t maxlen);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
int strcasecmp(const char *a, const char *b);
int strncasecmp(const char *a, const char *b, size_t n);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t n);
char *strcat(char *dst, const char *src);
char *strncat(char *dst, const char *src, size_t n);
size_t strlcpy(char *dst, const char *src, size_t size);
size_t strlcat(char *dst, const char *src, size_t size);
char *strchr(const char *s, int c);
char *strchrnul(const char *s, int c);
char *strnchr(const char *s, size_t count, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
char *strnstr(const char *haystack, const char *needle, size_t len);
char *strpbrk(const char *s, const char *accept);
size_t strspn(const char *s, const char *accept);
size_t strcspn(const char *s, const char *reject);
char *strsep(char **stringp, const char *delim);

/* Linux's own */
ssize_t sized_strscpy(char *dst, const char *src, size_t count);
ssize_t lx_strscpy_pad(char *dst, const char *src, size_t count);
#define __strscpy0(dst, src, ...)	sized_strscpy(dst, src, sizeof(dst))
#define __strscpy1(dst, src, size)	sized_strscpy(dst, src, size)
#define strscpy(dst, src, ...) \
	CONCATENATE(__strscpy, COUNT_ARGS(__VA_ARGS__))(dst, src, __VA_ARGS__)
#define __strscpy_pad0(dst, src, ...)	lx_strscpy_pad(dst, src, sizeof(dst))
#define __strscpy_pad1(dst, src, size)	lx_strscpy_pad(dst, src, size)
#define strscpy_pad(dst, src, ...) \
	CONCATENATE(__strscpy_pad, COUNT_ARGS(__VA_ARGS__))(dst, src, __VA_ARGS__)
#define strtomem(dest, src)	memcpy(dest, src, strnlen(src, sizeof(dest)))
#define strtomem_pad(dest, src, pad) do { \
	const size_t _dest_len = sizeof(dest); \
	const size_t _src_len = strnlen(src, _dest_len); \
	memcpy(dest, src, _src_len); \
	memset((char *)(dest) + _src_len, pad, _dest_len - _src_len); \
} while (0)
void *memchr_inv(const void *s, int c, size_t n);
static inline bool mem_is_zero(const void *s, size_t n)
{
	return !memchr_inv(s, 0, n);
}
void *memset16(uint16_t *s, uint16_t v, size_t count);
void *memset32(uint32_t *s, uint32_t v, size_t count);
void *memset64(uint64_t *s, uint64_t v, size_t count);
static inline void *memset_l(unsigned long *p, unsigned long v, size_t n)
{
	return memset64((uint64_t *)p, v, n);
}
static inline void *memset_p(void **p, void *v, size_t n)
{
	return memset64((uint64_t *)p, (uintptr_t)v, n);
}
static inline void memzero_explicit(void *s, size_t count)
{
	memset(s, 0, count);
	barrier_data(s);
}
void memcpy_and_pad(void *dest, size_t dest_len, const void *src, size_t count,
	int pad);
#define unsafe_memcpy(dst, src, bytes, justification)	memcpy(dst, src, bytes)
char *strim(char *s);
static inline __must_check char *strstrip(char *str) { return strim(str); }
char *skip_spaces(const char *str);
bool sysfs_streq(const char *s1, const char *s2);
int match_string(const char * const *array, size_t n, const char *string);
int __sysfs_match_string(const char * const *array, size_t n, const char *s);
#define sysfs_match_string(_a, _s) __sysfs_match_string(_a, ARRAY_SIZE(_a), _s)
char *strreplace(char *str, char old, char new);
static __always_inline size_t str_has_prefix(const char *str, const char *prefix)
{
	size_t len = strlen(prefix);
	return strncmp(str, prefix, len) == 0 ? len : 0;
}
char *kstrdup(const char *s, unsigned int gfp) __malloc;
const char *kstrdup_const(const char *s, unsigned int gfp);
char *kstrndup(const char *s, size_t len, unsigned int gfp);
void *kmemdup_noprof(const void *src, size_t len, unsigned int gfp) __malloc;
#define kmemdup(src, len, gfp)	kmemdup_noprof(src, len, gfp)
#define kmemdup_array(src, count, element_size, gfp) \
	kmemdup(src, size_mul(count, element_size), gfp)
char *kmemdup_nul(const char *s, size_t len, unsigned int gfp);
void kfree_const(const void *x);
void *memdup_user(const void __user *, size_t);
void *vmemdup_user(const void __user *, size_t);
void *memdup_user_nul(const void __user *, size_t);
char *strndup_user(const char __user *, long);
static inline void *memdup_array_user(const void __user *src, size_t n, size_t size)
{
	size_t nbytes;
	if (check_mul_overflow(n, size, &nbytes))
		return ERR_PTR(-EOVERFLOW);
	return memdup_user(src, nbytes);
}
static inline void *vmemdup_array_user(const void __user *src, size_t n, size_t size)
{
	size_t nbytes;
	if (check_mul_overflow(n, size, &nbytes))
		return ERR_PTR(-EOVERFLOW);
	return vmemdup_user(src, nbytes);
}
int kstrtobool(const char *s, bool *res);
void **__memcat_p(void **a, void **b);
#define memcat_p(a, b) ({ \
	BUILD_BUG_ON_MSG(!__same_type(*(a), *(b)), "type mismatch in memcat_p()"); \
	(typeof(*a) *)__memcat_p((void **)(a), (void **)(b)); \
})
#define strends(str, suffix) ({ \
	size_t __sl = strlen(str), __xl = strlen(suffix); \
	__sl >= __xl && !memcmp((str) + __sl - __xl, suffix, __xl); \
})

#include <linux/string_choices.h>

#endif /* _LINUX_STRING_H_ */
