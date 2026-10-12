#ifndef _LINUX_KASAN_CHECKS_H
#define _LINUX_KASAN_CHECKS_H
/* no KASAN */
static inline bool __kasan_check_read(const volatile void *p, unsigned int size) { return true; }
static inline bool __kasan_check_write(const volatile void *p, unsigned int size) { return true; }
#define kasan_check_read(p, size)	true
#define kasan_check_write(p, size)	true
#endif
