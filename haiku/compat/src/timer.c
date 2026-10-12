/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*	Time and timers. timer_list and hrtimer callbacks run on one thread,
	in deadline order; each kind is kept on its own list sorted by deadline
	in nanoseconds. The thread sleeps until the earliest deadline. */

#include <lx/haiku.h>


s64
lx_system_time_ns(void)
{
	return lx_haiku_system_time() * NSEC_PER_USEC;
}


s64
lx_real_time_ns(void)
{
	return lx_haiku_real_time() * NSEC_PER_USEC;
}


void
lx_spin_us(u64 us)
{
	lx_haiku_spin(us);
}


void
lx_sleep_us(u64 us)
{
	lx_haiku_snooze(us);
}


struct timespec64
ns_to_timespec64(s64 nsec)
{
	struct timespec64 ts = { 0, 0 };
	s32 rem;
	if (likely(nsec > 0)) {
		ts.tv_sec = div_u64_rem(nsec, NSEC_PER_SEC, &rem);
		ts.tv_nsec = rem;
	} else if (nsec < 0) {
		ts.tv_sec = -div_u64_rem(-nsec - 1, NSEC_PER_SEC, &rem) - 1;
		ts.tv_nsec = NSEC_PER_SEC - rem - 1;
	}
	return ts;
}


/* kernel/time/timeconv.c, simplified: proleptic Gregorian calendar */
void
time64_to_tm(time64_t totalsecs, int offset, struct tm *result)
{
	static const unsigned short days_before_month[2][13] = {
		{ 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 },
		{ 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366 },
	};
	s64 days, rem;
	long year = 1970;
	int leap, month;

	totalsecs += offset;
	days = div_s64_rem(totalsecs, 86400, (s32 *)&rem);
	if (rem < 0) {
		rem += 86400;
		days--;
	}
	result->tm_hour = rem / 3600;
	rem %= 3600;
	result->tm_min = rem / 60;
	result->tm_sec = rem % 60;
	result->tm_wday = (4 + days) % 7;
	if (result->tm_wday < 0)
		result->tm_wday += 7;

	for (;;) {
		long ydays;
		leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
		ydays = leap ? 366 : 365;
		if (days < 0) {
			year--;
			leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
			days += leap ? 366 : 365;
			continue;
		}
		if (days < ydays)
			break;
		days -= ydays;
		year++;
	}
	result->tm_year = year - 1900;
	result->tm_yday = days;
	for (month = 1; month < 12; month++) {
		if (days < days_before_month[leap][month])
			break;
	}
	result->tm_mon = month - 1;
	result->tm_mday = days - days_before_month[leap][month - 1] + 1;
}


/* #pragma mark - the timer thread */


static DEFINE_SPINLOCK(timer_lock);
static LIST_HEAD(timer_list_head);		/* struct timer_list, by expires */
static LIST_HEAD(hrtimer_list_head);	/* struct hrtimer, by expires */
static s64 *timer_deadlines;			/* unused */
static struct task_struct *timer_thread;
static void *timer_event;
static DECLARE_WAIT_QUEUE_HEAD(timer_callback_done);

static s64
timer_list_deadline(const struct timer_list *timer)
{
	return (s64)timer->expires * (NSEC_PER_SEC / HZ);
}


static int
timer_thread_main(void *unused)
{
	for (;;) {
		struct timer_list *timer = NULL;
		struct hrtimer *hrtimer = NULL;
		s64 now = lx_system_time_ns();
		s64 next = S64_MAX;

		spin_lock(&timer_lock);
		if (!list_empty(&timer_list_head)) {
			struct timer_list *first = list_first_entry(&timer_list_head,
				struct timer_list, entry);
			if (timer_list_deadline(first) <= now)
				timer = first;
			else
				next = timer_list_deadline(first);
		}
		if (timer == NULL && !list_empty(&hrtimer_list_head)) {
			struct hrtimer *first = list_first_entry(&hrtimer_list_head,
				struct hrtimer, entry);
			if (first->node.expires <= now)
				hrtimer = first;
			else
				next = min(next, first->node.expires);
		}
		if (timer != NULL) {
			list_del_init(&timer->entry);
			timer->queued = 0;
			timer->running = 1;
		} else if (hrtimer != NULL) {
			list_del_init(&hrtimer->entry);
			hrtimer->queued = 0;
			hrtimer->running = 1;
		}
		spin_unlock(&timer_lock);

		if (timer != NULL) {
			timer->function(timer);
			spin_lock(&timer_lock);
			timer->running = 0;
			spin_unlock(&timer_lock);
			wake_up_all(&timer_callback_done);
			continue;
		}
		if (hrtimer != NULL) {
			enum hrtimer_restart restart = HRTIMER_NORESTART;
			if (hrtimer->function != NULL)
				restart = hrtimer->function(hrtimer);
			spin_lock(&timer_lock);
			hrtimer->running = 0;
			if (restart == HRTIMER_RESTART && !hrtimer->queued) {
				struct hrtimer *pos;
				hrtimer->queued = 1;
				list_for_each_entry(pos, &hrtimer_list_head, entry) {
					if (pos->node.expires > hrtimer->node.expires)
						break;
				}
				list_add_tail(&hrtimer->entry, &pos->entry);
			}
			spin_unlock(&timer_lock);
			wake_up_all(&timer_callback_done);
			continue;
		}

		lx_haiku_event_wait(timer_event, next == S64_MAX ? -1
			: max_t(s64, (next - now) / NSEC_PER_USEC, 1));
	}
	return 0;
}


static void
timer_start_thread(void)
{
	static DEFINE_MUTEX(start_lock);
	mutex_lock(&start_lock);
	if (timer_thread == NULL) {
		struct task_struct *thread;
		timer_event = lx_haiku_event_create();
		thread = kthread_create(timer_thread_main, NULL, "linux timers");
		if (!IS_ERR(thread)) {
			lx_haiku_set_priority(thread->pid, LX_HAIKU_PRIORITY_URGENT);
			wake_up_process(thread);
			smp_store_release(&timer_thread, thread);
		}
	}
	mutex_unlock(&start_lock);
	(void)timer_deadlines;
}


static inline void
timer_kick(void)
{
	if (unlikely(smp_load_acquire(&timer_thread) == NULL))
		timer_start_thread();
	lx_haiku_event_signal(timer_event);
}


/* #pragma mark - timer_list */


void
timer_setup(struct timer_list *timer, void (*func)(struct timer_list *),
	unsigned int flags)
{
	INIT_LIST_HEAD(&timer->entry);
	timer->function = func;
	timer->flags = flags;
	timer->queued = 0;
	timer->running = 0;
	timer->expires = 0;
}


static void
timer_enqueue_locked(struct timer_list *timer)
{
	struct timer_list *pos;
	list_for_each_entry(pos, &timer_list_head, entry) {
		if (time_after(pos->expires, timer->expires))
			break;
	}
	list_add_tail(&timer->entry, &pos->entry);
	timer->queued = 1;
}


int
mod_timer(struct timer_list *timer, unsigned long expires)
{
	int was_pending;
	if (timer->entry.next == NULL)
		INIT_LIST_HEAD(&timer->entry);
	spin_lock(&timer_lock);
	was_pending = timer->queued;
	if (was_pending)
		list_del_init(&timer->entry);
	timer->expires = expires;
	timer_enqueue_locked(timer);
	spin_unlock(&timer_lock);
	timer_kick();
	return was_pending;
}


void
add_timer(struct timer_list *timer)
{
	mod_timer(timer, timer->expires);
}


int
timer_delete(struct timer_list *timer)
{
	int was_pending;
	if (timer->entry.next == NULL)
		return 0;
	spin_lock(&timer_lock);
	was_pending = timer->queued;
	if (was_pending) {
		list_del_init(&timer->entry);
		timer->queued = 0;
	}
	spin_unlock(&timer_lock);
	return was_pending;
}


int
timer_delete_sync(struct timer_list *timer)
{
	int was_pending = timer_delete(timer);
	if (READ_ONCE(timer_thread) != NULL && current != timer_thread) {
		wait_event(timer_callback_done, ({
			int running;
			spin_lock(&timer_lock);
			running = timer->running;
			spin_unlock(&timer_lock);
			!running;
		}));
	}
	return was_pending;
}


/* #pragma mark - hrtimer */


void
hrtimer_setup(struct hrtimer *timer,
	enum hrtimer_restart (*function)(struct hrtimer *), int clock_id,
	enum hrtimer_mode mode)
{
	memset(timer, 0, sizeof(*timer));
	INIT_LIST_HEAD(&timer->entry);
	timer->function = function;
	timer->clock = clock_id;
}


void
hrtimer_start_range_ns(struct hrtimer *timer, ktime_t tim, u64 range_ns,
	const enum hrtimer_mode mode)
{
	struct hrtimer *pos;
	if (timer->entry.next == NULL)
		INIT_LIST_HEAD(&timer->entry);
	if (mode & HRTIMER_MODE_REL)
		tim = ktime_add(ktime_get(), tim);
	else if (timer->clock == CLOCK_REALTIME)
		tim = tim - lx_real_time_ns() + ktime_get();

	spin_lock(&timer_lock);
	if (timer->queued)
		list_del_init(&timer->entry);
	timer->node.expires = tim;
	list_for_each_entry(pos, &hrtimer_list_head, entry) {
		if (pos->node.expires > tim)
			break;
	}
	list_add_tail(&timer->entry, &pos->entry);
	timer->queued = 1;
	spin_unlock(&timer_lock);
	timer_kick();
}


int
hrtimer_try_to_cancel(struct hrtimer *timer)
{
	int result;
	if (timer->entry.next == NULL)
		return 0;
	spin_lock(&timer_lock);
	if (timer->running)
		result = -1;
	else if (timer->queued) {
		list_del_init(&timer->entry);
		timer->queued = 0;
		result = 1;
	} else
		result = 0;
	spin_unlock(&timer_lock);
	return result;
}


int
hrtimer_cancel(struct hrtimer *timer)
{
	for (;;) {
		int result = hrtimer_try_to_cancel(timer);
		if (result >= 0)
			return result;
		if (current == timer_thread)
			return 0;
		wait_event(timer_callback_done, !READ_ONCE(timer->running));
	}
}


u64
hrtimer_forward(struct hrtimer *timer, ktime_t now, ktime_t interval)
{
	u64 orun = 1;
	ktime_t delta = ktime_sub(now, timer->node.expires);

	if (delta < 0)
		return 0;
	if (interval < 1)
		interval = 1;
	if (unlikely(delta >= interval)) {
		s64 incr = interval;
		orun = ktime_divns(delta, incr);
		timer->node.expires = ktime_add_ns(timer->node.expires, incr * orun);
		if (timer->node.expires > now)
			return orun;
		orun++;
	}
	timer->node.expires = ktime_add(timer->node.expires, interval);
	return orun;
}
