#ifndef _LINUX_KCSAN_CHECKS_H
#define _LINUX_KCSAN_CHECKS_H
/* no KCSAN */
#define kcsan_check_access(ptr, size, type)	do { } while (0)
#define kcsan_check_read(ptr, size)		do { } while (0)
#define kcsan_check_write(ptr, size)	do { } while (0)
#define kcsan_check_atomic_read(ptr, size)	do { } while (0)
#define kcsan_check_atomic_write(ptr, size)	do { } while (0)
#define kcsan_check_read_write(ptr, size)	do { } while (0)
#define kcsan_check_atomic_read_write(ptr, size)	do { } while (0)
#define kcsan_release()	do { } while (0)
#define kcsan_mb()		do { } while (0)
#define kcsan_wmb()		do { } while (0)
#define kcsan_rmb()		do { } while (0)
#define __kcsan_check_access(ptr, size, type)	do { } while (0)
#define ASSERT_EXCLUSIVE_WRITER(var)	do { } while (0)
#define ASSERT_EXCLUSIVE_ACCESS(var)	do { } while (0)
#define ASSERT_EXCLUSIVE_BITS(var, mask)	do { } while (0)
#define kcsan_enable_current()	do { } while (0)
#define kcsan_disable_current()	do { } while (0)
#endif
