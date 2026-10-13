/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*	Bring-up aid: a call trace. The files a build lists in TRACE_CALLS are
	compiled with -finstrument-functions; while tracing is on, every call
	into one of their functions is logged (log_file keeps it across a crash)
	as "T<thread> <offset>", the function's distance from
	__cyg_profile_func_enter. haiku/tools/symbolize-trace.sh turns that back
	into names. Tracing starts at the first close of a DRM file when the
	trace_calls parameter is 1, when the driver stops (devfs unloads it)
	when it is 2, and stays on. */

#include <lx/haiku.h>


static int trace_calls;
module_param(trace_calls, int, 0644);

static int trace_active;


void lx_trace_calls_start(void);
void lx_trace_calls_start_stop(void);

void
lx_trace_calls_start(void)
{
	if (READ_ONCE(trace_calls) == 1 && !READ_ONCE(trace_active)) {
		printk("trace: calls from here on\n");
		WRITE_ONCE(trace_active, 1);
	}
}


void
lx_trace_calls_start_stop(void)
{
	if (READ_ONCE(trace_calls) == 2 && !READ_ONCE(trace_active)) {
		printk("trace: calls of the stop from here on\n");
		WRITE_ONCE(trace_active, 1);
	}
}


void __cyg_profile_func_enter(void *function, void *site)
	__attribute__((no_instrument_function));
void __cyg_profile_func_exit(void *function, void *site)
	__attribute__((no_instrument_function));


void
__cyg_profile_func_enter(void *function, void *site)
{
	if (likely(!READ_ONCE(trace_active)))
		return;
	printk("T%d %lx\n", lx_haiku_current_thread(),
		(unsigned long)function - (unsigned long)__cyg_profile_func_enter);
}


void
__cyg_profile_func_exit(void *function, void *site)
{
}
