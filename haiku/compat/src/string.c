/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*	Linux's own string functions (lib/string.c, mm/util.c). The standard C
	ones come from Haiku's kernel. */

#include <lx/haiku.h>


ssize_t
sized_strscpy(char *dst, const char *src, size_t count)
{
	size_t length;
	if (count == 0)
		return -E2BIG;
	length = strnlen(src, count);
	if (length == count) {
		memcpy(dst, src, count - 1);
		dst[count - 1] = '\0';
		return -E2BIG;
	}
	memcpy(dst, src, length + 1);
	return length;
}


ssize_t
lx_strscpy_pad(char *dst, const char *src, size_t count)
{
	ssize_t written = sized_strscpy(dst, src, count);
	if (written < 0 || (size_t)written + 1 >= count)
		return written;
	memset(dst + written + 1, 0, count - written - 1);
	return written;
}


void *
memchr_inv(const void *start, int c, size_t bytes)
{
	const u8 *p = start;
	u8 value = c;
	while (bytes-- > 0) {
		if (*p != value)
			return (void *)p;
		p++;
	}
	return NULL;
}


void *
memset16(uint16_t *s, uint16_t v, size_t count)
{
	uint16_t *p = s;
	while (count--)
		*p++ = v;
	return s;
}


void *
memset32(uint32_t *s, uint32_t v, size_t count)
{
	uint32_t *p = s;
	while (count--)
		*p++ = v;
	return s;
}


void *
memset64(uint64_t *s, uint64_t v, size_t count)
{
	uint64_t *p = s;
	while (count--)
		*p++ = v;
	return s;
}


void *
memscan(void *addr, int c, size_t size)
{
	unsigned char *p = addr;
	while (size) {
		if (*p == (unsigned char)c)
			return p;
		p++;
		size--;
	}
	return p;
}


char *
strchrnul(const char *s, int c)
{
	while (*s && *s != (char)c)
		s++;
	return (char *)s;
}


char *
strnchr(const char *s, size_t count, int c)
{
	while (count--) {
		if (*s == (char)c)
			return (char *)s;
		if (*s++ == '\0')
			break;
	}
	return NULL;
}


char *
strnstr(const char *s1, const char *s2, size_t len)
{
	size_t l2 = strlen(s2);
	if (!l2)
		return (char *)s1;
	while (len >= l2) {
		len--;
		if (!memcmp(s1, s2, l2))
			return (char *)s1;
		s1++;
	}
	return NULL;
}


char *
strsep(char **s, const char *ct)
{
	char *sbegin = *s;
	char *end;
	if (sbegin == NULL)
		return NULL;
	end = strpbrk(sbegin, ct);
	if (end)
		*end++ = '\0';
	*s = end;
	return sbegin;
}


char *
kstrdup(const char *s, unsigned int gfp)
{
	size_t length;
	char *buf;
	if (!s)
		return NULL;
	length = strlen(s) + 1;
	buf = kmalloc(length, gfp);
	if (buf)
		memcpy(buf, s, length);
	return buf;
}


const char *
kstrdup_const(const char *s, unsigned int gfp)
{
	return kstrdup(s, gfp);
}


void
kfree_const(const void *x)
{
	kfree(x);
}


char *
kstrndup(const char *s, size_t max, unsigned int gfp)
{
	size_t length;
	char *buf;
	if (!s)
		return NULL;
	length = strnlen(s, max);
	buf = kmalloc(length + 1, gfp);
	if (buf) {
		memcpy(buf, s, length);
		buf[length] = '\0';
	}
	return buf;
}


void *
kmemdup_noprof(const void *src, size_t length, unsigned int gfp)
{
	void *p = kmalloc(length, gfp);
	if (p)
		memcpy(p, src, length);
	return p;
}


char *
kmemdup_nul(const char *s, size_t length, unsigned int gfp)
{
	char *buf;
	if (!s)
		return NULL;
	buf = kmalloc(length + 1, gfp);
	if (buf) {
		memcpy(buf, s, length);
		buf[length] = '\0';
	}
	return buf;
}


void *
memdup_user(const void __user *src, size_t length)
{
	void *p = kmalloc(length, GFP_KERNEL);
	if (!p)
		return ERR_PTR(-ENOMEM);
	if (copy_from_user(p, src, length)) {
		kfree(p);
		return ERR_PTR(-EFAULT);
	}
	return p;
}


void *
vmemdup_user(const void __user *src, size_t length)
{
	return memdup_user(src, length);
}


void *
memdup_user_nul(const void __user *src, size_t length)
{
	char *p = kmalloc(length + 1, GFP_KERNEL);
	if (!p)
		return ERR_PTR(-ENOMEM);
	if (copy_from_user(p, src, length)) {
		kfree(p);
		return ERR_PTR(-EFAULT);
	}
	p[length] = '\0';
	return p;
}


char *
strndup_user(const char __user *s, long n)
{
	char *p = kmalloc(n, GFP_KERNEL);
	long length;
	if (!p)
		return ERR_PTR(-ENOMEM);
	length = strncpy_from_user(p, s, n);
	if (length < 0) {
		kfree(p);
		return ERR_PTR(length);
	}
	if (length >= n) {
		kfree(p);
		return ERR_PTR(-EINVAL);
	}
	p[length] = '\0';
	return p;
}


void **
__memcat_p(void **a, void **b)
{
	void **p = a, **new;
	int nr;
	for (nr = 0, p = a; *p; nr++, p++)
		;
	for (p = b; *p; nr++, p++)
		;
	nr++;
	new = kmalloc_array(nr, sizeof(void *), GFP_KERNEL);
	if (!new)
		return NULL;
	for (nr--; nr >= 0; nr--, p = p == b ? &a[nr] : p - 1)
		new[nr] = *p;
	return new;
}
