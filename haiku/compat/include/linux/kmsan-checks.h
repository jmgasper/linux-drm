#ifndef _LINUX_KMSAN_CHECKS_H
#define _LINUX_KMSAN_CHECKS_H
/* no KMSAN */
#define kmsan_poison_memory(address, size, flags)	do { } while (0)
#define kmsan_unpoison_memory(address, size)	do { } while (0)
#define kmsan_check_memory(address, size)	do { } while (0)
#define kmsan_copy_to_user(to, from, to_copy, left)	do { } while (0)
#define kmsan_memmove(to, from, size)	do { } while (0)
#endif
