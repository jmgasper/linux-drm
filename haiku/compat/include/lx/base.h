/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _LX_BASE_H
#define _LX_BASE_H

/*	Types, compiler helpers, errors, printing, strings, arithmetic. */

#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/compiler.h>
#include <linux/build_bug.h>
#include <linux/stringify.h>
#include <linux/const.h>
#include <linux/limits.h>
#include <linux/array_size.h>
#include <linux/args.h>
#include <linux/typecheck.h>
#include <linux/container_of.h>
#include <linux/overflow.h>
#include <linux/minmax.h>
#include <linux/bits.h>
#include <linux/align.h>
#include <linux/wordpart.h>
#include <linux/kern_levels.h>
#include <linux/cleanup.h>
#include <linux/swab.h>
#include <linux/byteorder/little_endian.h>
#include <asm/div64.h>
#include <linux/math.h>
#include <linux/math64.h>
#include <linux/err.h>
#include <linux/poison.h>
#include <linux/sizes.h>
#include <linux/once_lite.h>
#include <linux/instruction_pointer.h>
#include <linux/stdarg.h>


/* #pragma mark - errors */

/* Linux's kernel-internal error numbers, beyond the UAPI ones in
   asm-generic/errno.h (via linux/err.h). Never seen by userland. */
#define ERESTARTSYS		512
#define ERESTARTNOINTR	513
#define ERESTARTNOHAND	514
#define ENOIOCTLCMD		515
#define ERESTART_RESTARTBLOCK 516
#define EPROBE_DEFER	517
#define EOPENSTALE		518
#define ENOPARAM		519
#define EBADHANDLE		521
#define ENOTSYNC		522
#define EBADCOOKIE		523
#define ENOTSUPP		524
#define ETOOSMALL		525
#define ESERVERFAULT	526
#define EBADTYPE		527
#define EJUKEBOX		528
#define EIOCBQUEUED		529
#define ERECALLCONFLICT	530
#define ENOGRACE		531


/* #pragma mark - printing */

__printf(1, 2) int _printk(const char *fmt, ...);
__printf(1, 0) int vprintk(const char *fmt, va_list args);
#define printk(fmt, ...)	_printk(fmt, ##__VA_ARGS__)
#define printk_deferred(fmt, ...) _printk(fmt, ##__VA_ARGS__)

#define no_printk(fmt, ...) \
	({ if (0) _printk(fmt, ##__VA_ARGS__); 0; })

#ifndef pr_fmt
#	define pr_fmt(fmt) fmt
#endif

#define pr_emerg(fmt, ...)	printk(KERN_EMERG pr_fmt(fmt), ##__VA_ARGS__)
#define pr_alert(fmt, ...)	printk(KERN_ALERT pr_fmt(fmt), ##__VA_ARGS__)
#define pr_crit(fmt, ...)	printk(KERN_CRIT pr_fmt(fmt), ##__VA_ARGS__)
#define pr_err(fmt, ...)	printk(KERN_ERR pr_fmt(fmt), ##__VA_ARGS__)
#define pr_warn(fmt, ...)	printk(KERN_WARNING pr_fmt(fmt), ##__VA_ARGS__)
#define pr_warning			pr_warn
#define pr_notice(fmt, ...)	printk(KERN_NOTICE pr_fmt(fmt), ##__VA_ARGS__)
#define pr_info(fmt, ...)	printk(KERN_INFO pr_fmt(fmt), ##__VA_ARGS__)
#define pr_cont(fmt, ...)	printk(KERN_CONT fmt, ##__VA_ARGS__)
#define pr_devel(fmt, ...)	no_printk(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__)
#define pr_debug(fmt, ...)	no_printk(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__)

#define printk_once(fmt, ...)	DO_ONCE_LITE(printk, fmt, ##__VA_ARGS__)
#define pr_emerg_once(fmt, ...)	DO_ONCE_LITE(pr_emerg, fmt, ##__VA_ARGS__)
#define pr_err_once(fmt, ...)	DO_ONCE_LITE(pr_err, fmt, ##__VA_ARGS__)
#define pr_warn_once(fmt, ...)	DO_ONCE_LITE(pr_warn, fmt, ##__VA_ARGS__)
#define pr_notice_once(fmt, ...) DO_ONCE_LITE(pr_notice, fmt, ##__VA_ARGS__)
#define pr_info_once(fmt, ...)	DO_ONCE_LITE(pr_info, fmt, ##__VA_ARGS__)
#define pr_debug_once(fmt, ...)	no_printk(fmt, ##__VA_ARGS__)

/* rate limiting: everything is printed; Haiku's syslog keeps up */
struct ratelimit_state {
	int interval;
	int burst;
	int printed;
	int missed;
	unsigned long begin;
	unsigned long flags;
};
#define RATELIMIT_STATE_INIT(name, interval_init, burst_init) \
	{ .interval = (interval_init), .burst = (burst_init) }
#define DEFINE_RATELIMIT_STATE(name, interval_init, burst_init) \
	struct ratelimit_state name = RATELIMIT_STATE_INIT(name, interval_init, burst_init)
#define DEFAULT_RATELIMIT_INTERVAL	(5 * HZ)
#define DEFAULT_RATELIMIT_BURST		10
#define RATELIMIT_MSG_ON_RELEASE	1
static inline void ratelimit_state_init(struct ratelimit_state *rs,
	int interval, int burst) { rs->interval = interval; rs->burst = burst; }
static inline void ratelimit_set_flags(struct ratelimit_state *rs,
	unsigned long flags) { rs->flags = flags; }
#define __ratelimit(state)	(1)
#define ratelimit_state_reset_interval(rs, ival)	((rs)->interval = (ival))
#define printk_ratelimit()	(1)
#define printk_ratelimited(fmt, ...)	printk(fmt, ##__VA_ARGS__)
#define pr_err_ratelimited(fmt, ...)	pr_err(fmt, ##__VA_ARGS__)
#define pr_warn_ratelimited(fmt, ...)	pr_warn(fmt, ##__VA_ARGS__)
#define pr_info_ratelimited(fmt, ...)	pr_info(fmt, ##__VA_ARGS__)
#define pr_notice_ratelimited(fmt, ...)	pr_notice(fmt, ##__VA_ARGS__)
#define pr_debug_ratelimited(fmt, ...)	pr_debug(fmt, ##__VA_ARGS__)

enum {
	DUMP_PREFIX_NONE,
	DUMP_PREFIX_ADDRESS,
	DUMP_PREFIX_OFFSET
};
int hex_dump_to_buffer(const void *buf, size_t len, int rowsize, int groupsize,
	char *linebuf, size_t linebuflen, bool ascii);
void print_hex_dump(const char *level, const char *prefix_str, int prefix_type,
	int rowsize, int groupsize, const void *buf, size_t len, bool ascii);
#define print_hex_dump_debug(...)	do { } while (0)
#define print_hex_dump_bytes(prefix, type, buf, len) \
	print_hex_dump(KERN_DEBUG, prefix, type, 16, 1, buf, len, true)
#include <linux/hex.h>

void dump_stack(void);
void lx_panic(const char *fmt, ...) __printf(1, 2) __noreturn;
#define panic(fmt, ...)	lx_panic(fmt, ##__VA_ARGS__)

/* vsprintf: Linux's formats, including %pV, %pe, %pS, %pa, %ph, %p4cc */
__printf(3, 4) int snprintf(char *buf, size_t size, const char *fmt, ...);
__printf(3, 0) int vsnprintf(char *buf, size_t size, const char *fmt, va_list args);
__printf(3, 4) int scnprintf(char *buf, size_t size, const char *fmt, ...);
__printf(3, 0) int vscnprintf(char *buf, size_t size, const char *fmt, va_list args);
__printf(2, 3) int sprintf(char *buf, const char *fmt, ...);
__printf(2, 0) int vsprintf(char *buf, const char *fmt, va_list args);
__printf(2, 3) char *kasprintf(unsigned int gfp, const char *fmt, ...);
__printf(2, 0) char *kvasprintf(unsigned int gfp, const char *fmt, va_list args);
const char *kvasprintf_const(unsigned int gfp, const char *fmt, va_list args);
__scanf(2, 3) int sscanf(const char *buf, const char *fmt, ...);
__scanf(2, 0) int vsscanf(const char *buf, const char *fmt, va_list args);
struct va_format {
	const char *fmt;
	va_list *va;
};


/* #pragma mark - bugs and warnings */

void lx_warn(const char *file, int line, const char *func, const char *cond);
void lx_warn_printf(const char *file, int line, const char *fmt, ...) __printf(3, 4);

#define BUG() lx_panic("BUG at %s:%d %s()", __FILE__, __LINE__, __func__)
#define BUG_ON(condition)	do { if (unlikely(condition)) BUG(); } while (0)
#define WARN_ON(condition) ({ \
	int __ret_warn_on = !!(condition); \
	if (unlikely(__ret_warn_on)) \
		lx_warn(__FILE__, __LINE__, __func__, #condition); \
	unlikely(__ret_warn_on); \
})
#define WARN(condition, fmt...) ({ \
	int __ret_warn_on = !!(condition); \
	if (unlikely(__ret_warn_on)) \
		lx_warn_printf(__FILE__, __LINE__, fmt); \
	unlikely(__ret_warn_on); \
})
#define WARN_ON_ONCE(condition) ({ \
	static bool __section(".data..once") __warned; \
	int __ret_warn_once = !!(condition); \
	if (unlikely(__ret_warn_once && !__warned)) { \
		__warned = true; \
		lx_warn(__FILE__, __LINE__, __func__, #condition); \
	} \
	unlikely(__ret_warn_once); \
})
#define WARN_ONCE(condition, fmt...) ({ \
	static bool __section(".data..once") __warned; \
	int __ret_warn_once = !!(condition); \
	if (unlikely(__ret_warn_once && !__warned)) { \
		__warned = true; \
		lx_warn_printf(__FILE__, __LINE__, fmt); \
	} \
	unlikely(__ret_warn_once); \
})
#define WARN_TAINT(condition, taint, fmt...)	WARN(condition, fmt)
#define WARN_TAINT_ONCE(condition, taint, fmt...) WARN_ONCE(condition, fmt)
#define BUILD_BUG_ON_INVALID(e)	((void)(sizeof((__force long)(e))))
#define VM_BUG_ON(cond)		BUILD_BUG_ON_INVALID(cond)
#define VM_WARN_ON(cond)	BUILD_BUG_ON_INVALID(cond)
#define VM_WARN_ON_ONCE(cond)	BUILD_BUG_ON_INVALID(cond)
#define TAINT_WARN			9
#define TAINT_SOFTLOCKUP	14
#define TAINT_CPU_OUT_OF_SPEC	2
#define TAINT_MACHINE_CHECK	4
#define LOCKDEP_STILL_OK	1
#define LOCKDEP_NOW_UNRELIABLE 0
static inline void add_taint(unsigned flag, int lockdep_ok) { }


/* #pragma mark - kernel.h odds and ends */

#define _RET_IP_		(unsigned long)__builtin_return_address(0)
#define _THIS_IP_		({ __label__ __here; __here: (unsigned long)&&__here; })
#define STACK_MAGIC		0xdeadbeef
#define REPEAT_BYTE(x)	((~0ul / 0xff) * (x))
#define lower_16_bits(n) ((u16)((n) & 0xffff))
#define upper_16_bits(n) ((u16)((n) >> 16))
#define PTR_IF(cond, ptr)	((cond) ? (ptr) : NULL)
#define might_sleep()		do { } while (0)
#define might_sleep_if(c)	do { } while (0)
#define might_fault()		do { } while (0)
#define might_lock(lock)	do { } while (0)
#define might_lock_nested(lock, sub) do { } while (0)
#define cant_sleep()		do { } while (0)
#define sched_annotate_sleep() do { } while (0)
#define non_block_start()	do { } while (0)
#define non_block_end()		do { } while (0)
#define SYSTEM_RUNNING		2
#define SYSTEM_POWER_OFF	4
#define SYSTEM_RESTART		5
#define SYSTEM_HALT			3
#define system_state		SYSTEM_RUNNING
#define oops_in_progress	0
#define kmsg_dump(reason)	do { } while (0)
#define in_dbg_master()		0
#define kgdb_breakpoint()	do { } while (0)
#define kcsan_check_access(ptr, size, type) do { } while (0)

static inline const char *kbasename(const char *path)
{
	const char *tail = __builtin_strrchr(path, '/');
	return tail ? tail + 1 : path;
}

/* lib/kstrtox.c */
#include <linux/kstrtox.h>
unsigned long simple_strtoul(const char *cp, char **endp, unsigned int base);
long simple_strtol(const char *cp, char **endp, unsigned int base);
unsigned long long simple_strtoull(const char *cp, char **endp, unsigned int base);
long long simple_strtoll(const char *cp, char **endp, unsigned int base);

int lx_num_online_cpus(void);
#define num_online_cpus()	lx_num_online_cpus()
#define num_possible_cpus()	lx_num_online_cpus()
#define num_present_cpus()	lx_num_online_cpus()
#define nr_cpu_ids			lx_num_online_cpus()
int lx_smp_processor_id(void);
#define smp_processor_id()		lx_smp_processor_id()
#define raw_smp_processor_id()	lx_smp_processor_id()
#define get_cpu()			lx_smp_processor_id()
#define put_cpu()			do { } while (0)
#define for_each_possible_cpu(cpu) \
	for ((cpu) = 0; (cpu) < lx_num_online_cpus(); (cpu)++)
#define for_each_online_cpu(cpu)	for_each_possible_cpu(cpu)
#define for_each_present_cpu(cpu)	for_each_possible_cpu(cpu)
#define cpu_online(cpu)		((cpu) < lx_num_online_cpus())


/* #pragma mark - strings (Haiku's kernel libc plus Linux's own) */

#include <lx/string.h>


/* #pragma mark - bit operations */

#include <lx/bitops.h>

#include <linux/log2.h>
#include <linux/bitfield.h>
#include <linux/gcd.h>
#include <linux/unaligned.h>
#include <linux/units.h>
#include <linux/util_macros.h>


/* #pragma mark - lists, trees, sorting (unchanged Linux headers) */

#include <lx/barrier.h>
#include <linux/list.h>
#include <linux/list_sort.h>
#include <linux/bsearch.h>
#include <linux/hash.h>
#include <linux/jhash.h>
#include <linux/circ_buf.h>

#endif /* _LX_BASE_H */
