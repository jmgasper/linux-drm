/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _LX_TIME_H
#define _LX_TIME_H

/*	Time, delays and timers. jiffies count milliseconds of Haiku's
	system_time() (HZ = 1000). timer_list and hrtimer callbacks run on one
	timer thread (haiku/compat/haiku/Timer.cpp), not in interrupt context. */

#include <linux/time64.h>

#define HZ		CONFIG_HZ
#define USER_HZ	100
#define MAX_JIFFY_OFFSET	((LONG_MAX >> 1) - 1)
#define MAX_SCHEDULE_TIMEOUT	LONG_MAX

s64 lx_system_time_ns(void);		/* monotonic, since boot */
s64 lx_real_time_ns(void);			/* wall clock */

static inline unsigned long lx_jiffies(void)
{
	return (unsigned long)(lx_system_time_ns() / (NSEC_PER_SEC / HZ));
}
#define jiffies			lx_jiffies()
#define jiffies_64		((u64)lx_jiffies())
#define get_jiffies_64()	((u64)lx_jiffies())
#define INITIAL_JIFFIES	0

#define time_after(a, b)		((long)((b) - (a)) < 0)
#define time_before(a, b)		time_after(b, a)
#define time_after_eq(a, b)		((long)((a) - (b)) >= 0)
#define time_before_eq(a, b)	time_after_eq(b, a)
#define time_in_range(a, b, c)	(time_after_eq(a, b) && time_before_eq(a, c))
#define time_after64			time_after
#define time_before64			time_before
#define time_after_eq64			time_after_eq
#define time_before_eq64		time_before_eq
#define time_is_before_jiffies(a)	time_after(jiffies, a)
#define time_is_after_jiffies(a)	time_before(jiffies, a)
#define time_is_before_eq_jiffies(a)	time_after_eq(jiffies, a)
#define time_is_after_eq_jiffies(a)	time_before_eq(jiffies, a)

static inline unsigned long msecs_to_jiffies(const unsigned int m)
{
	return m;
}
static inline unsigned long usecs_to_jiffies(const unsigned int u)
{
	return DIV_ROUND_UP(u, 1000);
}
static inline unsigned long nsecs_to_jiffies(u64 n)
{
	return DIV_ROUND_UP_ULL(n, NSEC_PER_MSEC);
}
#define nsecs_to_jiffies64(n)	((u64)nsecs_to_jiffies(n))
static inline unsigned int jiffies_to_msecs(const unsigned long j)
{
	return j;
}
static inline unsigned int jiffies_to_usecs(const unsigned long j)
{
	return j * 1000;
}
static inline u64 jiffies_to_nsecs(const unsigned long j)
{
	return (u64)j * NSEC_PER_MSEC;
}
#define jiffies64_to_nsecs(j)	jiffies_to_nsecs(j)
#define jiffies64_to_msecs(j)	jiffies_to_msecs(j)
#define secs_to_jiffies(s)	((unsigned long)((s) * HZ))
#define round_jiffies(j)			(j)
#define round_jiffies_relative(j)	(j)
#define round_jiffies_up(j)			(j)
#define round_jiffies_up_relative(j) (j)
#define jiffies_delta_to_msecs(delta)	jiffies_to_msecs(max(0L, (long)(delta)))

/* #pragma mark - ktime */

#define KTIME_MAX		((s64)~((u64)1 << 63))
#define KTIME_SEC_MAX	(KTIME_MAX / NSEC_PER_SEC)

static inline ktime_t ktime_set(const s64 secs, const unsigned long nsecs)
{
	if (unlikely(secs >= KTIME_SEC_MAX))
		return KTIME_MAX;
	return secs * NSEC_PER_SEC + (s64)nsecs;
}
#define ktime_sub(lhs, rhs)		((lhs) - (rhs))
#define ktime_add(lhs, rhs)		((lhs) + (rhs))
#define ktime_add_unsafe(lhs, rhs)	((u64)(lhs) + (rhs))
#define ktime_add_ns(kt, nsval)	((kt) + (nsval))
#define ktime_sub_ns(kt, nsval)	((kt) - (nsval))
#define ktime_to_ns(kt)			(kt)
#define ns_to_ktime(ns)			((ktime_t)(ns))
#define ms_to_ktime(ms)			((ktime_t)(ms) * NSEC_PER_MSEC)
#define us_to_ktime(us)			((ktime_t)(us) * NSEC_PER_USEC)
#define ktime_to_us(kt)			((s64)(kt) / NSEC_PER_USEC)
#define ktime_to_ms(kt)			((s64)(kt) / NSEC_PER_MSEC)
#define ktime_divns(kt, div)	((s64)(kt) / (div))
#define ktime_add_us(kt, us)	((kt) + (s64)(us) * NSEC_PER_USEC)
#define ktime_add_ms(kt, ms)	((kt) + (s64)(ms) * NSEC_PER_MSEC)
#define ktime_sub_us(kt, us)	((kt) - (s64)(us) * NSEC_PER_USEC)
#define ktime_us_delta(later, earlier)	ktime_to_us(ktime_sub(later, earlier))
#define ktime_ms_delta(later, earlier)	ktime_to_ms(ktime_sub(later, earlier))
#define ktime_compare(a, b)		((a) < (b) ? -1 : (a) > (b) ? 1 : 0)
#define ktime_after(a, b)		((a) > (b))
#define ktime_before(a, b)		((a) < (b))
#define ktime_to_timespec64(kt)	ns_to_timespec64(kt)
#define timespec64_to_ktime(ts)	ktime_set((ts).tv_sec, (ts).tv_nsec)
#define ktime_to_timeval(kt)	ns_to_timeval(kt)
#define ktime_to_ns(kt)			(kt)

static inline ktime_t ktime_get(void) { return lx_system_time_ns(); }
#define ktime_get_raw()			ktime_get()
#define ktime_get_boottime()	ktime_get()
#define ktime_get_ns()			((u64)ktime_get())
#define ktime_get_raw_ns()		((u64)ktime_get())
#define ktime_get_boottime_ns()	((u64)ktime_get())
#define ktime_get_mono_fast_ns()	((u64)ktime_get())
#define ktime_get_raw_fast_ns()	((u64)ktime_get())
#define ktime_get_boot_fast_ns()	((u64)ktime_get())
#define ktime_get_real()		((ktime_t)lx_real_time_ns())
#define ktime_get_real_ns()		((u64)lx_real_time_ns())
#define ktime_get_real_seconds()	((time64_t)(lx_real_time_ns() / NSEC_PER_SEC))
#define ktime_get_seconds()		((time64_t)(ktime_get() / NSEC_PER_SEC))
#define ktime_get_with_offset(o)	ktime_get()
#define ktime_mono_to_real(kt)	((kt) + (lx_real_time_ns() - lx_system_time_ns()))
static inline void ktime_get_ts64(struct timespec64 *ts)
{
	*ts = ns_to_timespec64(ktime_get());
}
static inline void ktime_get_real_ts64(struct timespec64 *ts)
{
	*ts = ns_to_timespec64(lx_real_time_ns());
}
#define ktime_get_raw_ts64(ts)	ktime_get_ts64(ts)
#define ktime_get_boottime_ts64(ts)	ktime_get_ts64(ts)
#define local_clock()			((u64)ktime_get())
#define sched_clock()			((u64)ktime_get())
#define get_cycles()			((u64)ktime_get())
struct tm {
	int tm_sec;
	int tm_min;
	int tm_hour;
	int tm_mday;
	int tm_mon;
	long tm_year;
	int tm_wday;
	int tm_yday;
};
void time64_to_tm(time64_t totalsecs, int offset, struct tm *result);

/* #pragma mark - delays */

void lx_spin_us(u64 us);
void lx_sleep_us(u64 us);
#define udelay(us)		lx_spin_us(us)
#define ndelay(ns)		lx_spin_us(DIV_ROUND_UP((u64)(ns), 1000))
#define mdelay(ms)		lx_spin_us((u64)(ms) * 1000)
#define msleep(ms)		lx_sleep_us((u64)(ms) * 1000)
#define ssleep(s)		lx_sleep_us((u64)(s) * 1000000)
#define usleep_range(min, max)	lx_sleep_us(min)
#define usleep_range_state(min, max, state)	lx_sleep_us(min)
#define usleep_idle_range(min, max)	lx_sleep_us(min)
#define fsleep(us)		lx_sleep_us(us)
#define msleep_interruptible(ms)	({ lx_sleep_us((u64)(ms) * 1000); 0UL; })

/* #pragma mark - timer_list */

struct timer_list {
	struct list_head entry;
	unsigned long expires;
	void (*function)(struct timer_list *);
	u32 flags;
	int queued;
	int running;
};

#define TIMER_DEFERRABLE	0x00080000
#define TIMER_PINNED		0x00100000
#define TIMER_IRQSAFE		0x00200000

void timer_setup(struct timer_list *timer, void (*func)(struct timer_list *),
	unsigned int flags);
#define timer_setup_on_stack(t, f, fl)	timer_setup(t, f, fl)
#define destroy_timer_on_stack(t)		do { } while (0)
#define from_timer(var, callback_timer, timer_fieldname) \
	container_of(callback_timer, typeof(*var), timer_fieldname)
#define timer_container_of(var, callback_timer, timer_fieldname) \
	container_of(callback_timer, typeof(*var), timer_fieldname)
int mod_timer(struct timer_list *timer, unsigned long expires);
void add_timer(struct timer_list *timer);
int timer_delete(struct timer_list *timer);
int timer_delete_sync(struct timer_list *timer);
#define del_timer(t)		timer_delete(t)
#define del_timer_sync(t)	timer_delete_sync(t)
#define timer_shutdown_sync(t)	timer_delete_sync(t)
#define timer_shutdown(t)	timer_delete(t)
static inline int timer_pending(const struct timer_list *timer)
{
	return READ_ONCE(timer->queued);
}
#define DEFINE_TIMER(_name, _function) \
	struct timer_list _name = { .entry = LIST_HEAD_INIT(_name.entry), \
		.function = (_function) }

/* #pragma mark - hrtimer */

enum hrtimer_mode {
	HRTIMER_MODE_ABS = 0x00,
	HRTIMER_MODE_REL = 0x01,
	HRTIMER_MODE_PINNED = 0x02,
	HRTIMER_MODE_SOFT = 0x04,
	HRTIMER_MODE_HARD = 0x08,
	HRTIMER_MODE_ABS_PINNED = 0x02,
	HRTIMER_MODE_REL_PINNED = 0x03,
	HRTIMER_MODE_ABS_SOFT = 0x04,
	HRTIMER_MODE_REL_SOFT = 0x05,
	HRTIMER_MODE_ABS_HARD = 0x08,
	HRTIMER_MODE_REL_HARD = 0x09,
};

enum hrtimer_restart {
	HRTIMER_NORESTART,
	HRTIMER_RESTART,
};

typedef int clockid_t_lx;
#define CLOCK_REALTIME		0
#define CLOCK_MONOTONIC		1
#define CLOCK_BOOTTIME		7

struct timerqueue_node {
	struct rb_node_lx { unsigned long unused[3]; } node;
	ktime_t expires;
};

struct hrtimer {
	struct timerqueue_node node;
	struct list_head entry;
	enum hrtimer_restart (*function)(struct hrtimer *);
	int queued;
	int running;
	int clock;
};

struct hrtimer_sleeper {
	struct hrtimer timer;
	struct task_struct *task;
};

void hrtimer_setup(struct hrtimer *timer,
	enum hrtimer_restart (*function)(struct hrtimer *), int clock_id,
	enum hrtimer_mode mode);
#define hrtimer_init(timer, clock, mode)	hrtimer_setup(timer, NULL, clock, mode)
void hrtimer_start_range_ns(struct hrtimer *timer, ktime_t tim, u64 range_ns,
	const enum hrtimer_mode mode);
#define hrtimer_start(timer, tim, mode) hrtimer_start_range_ns(timer, tim, 0, mode)
int hrtimer_cancel(struct hrtimer *timer);
int hrtimer_try_to_cancel(struct hrtimer *timer);
u64 hrtimer_forward(struct hrtimer *timer, ktime_t now, ktime_t interval);
#define hrtimer_forward_now(timer, interval) \
	hrtimer_forward(timer, ktime_get(), interval)
#define hrtimer_get_expires(timer)		((timer)->node.expires)
#define hrtimer_get_softexpires(timer)	((timer)->node.expires)
#define hrtimer_get_expires_tv64(timer)	((timer)->node.expires)
#define hrtimer_get_expires_ns(timer)	((timer)->node.expires)
#define hrtimer_set_expires(timer, time)	((timer)->node.expires = (time))
#define hrtimer_set_expires_range_ns(timer, time, delta) ((timer)->node.expires = (time))
#define hrtimer_add_expires_ns(timer, ns)	((timer)->node.expires += (ns))
#define hrtimer_is_queued(timer)	READ_ONCE((timer)->queued)
#define hrtimer_active(timer)	(READ_ONCE((timer)->queued) || READ_ONCE((timer)->running))
#define hrtimer_callback_running(timer)	READ_ONCE((timer)->running)
#define hrtimer_cb_get_time(timer)	ktime_get()
#define hrtimer_get_remaining(timer)	ktime_sub((timer)->node.expires, ktime_get())
#define hrtimer_expires_remaining(timer)	ktime_sub((timer)->node.expires, ktime_get())
#define hrtimer_resolution	1

#endif /* _LX_TIME_H */
