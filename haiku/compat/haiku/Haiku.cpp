/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*	The Haiku kernel services of lx/haiku.h. Nothing here knows Linux
	structures; errors are returned as Linux error numbers. */

#include <new>
#include <stdio.h>
#include <string.h>

#include <KernelExport.h>
#include <OS.h>
#include <fs_info.h>

#include <condition_variable.h>
#include <cpu.h>
#include <debug.h>
#include <kernel.h>
#include <lock.h>
#include <Notifications.h>
#include <smp.h>
#include <team.h>
#include <thread.h>
#include <util/AutoLock.h>
#include <util/Random.h>
#include <sys/uio.h>
#include <util/iovec_support.h>
#include <vfs.h>
#include <syscalls.h>
#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/VMAddressSpace.h>
#include <arch/x86/arch_cpu.h>

#include "../include/lx/haiku.h"


static_assert(sizeof(mutex) <= LX_HAIKU_MUTEX_SIZE, "mutex storage");
static_assert(sizeof(rw_lock) <= LX_HAIKU_RWLOCK_SIZE, "rw_lock storage");

// Linux error numbers used here
static const int kLinuxENOMEM = 12;
static const int kLinuxEFAULT = 14;
static const int kLinuxENOENT = 2;
static const int kLinuxEIO = 5;
static const int kLinuxETIMEDOUT = 110;


// #pragma mark - output, time, processors


/*	The log file must exist already, preallocated (zeros): it is overwritten
	from the start and never grows, so that a synced write changes no file
	system structure that a crash right after it could lose. */
static int sLogFile = -1;
static off_t sLogPosition;
static off_t sLogSize;
static mutex sLogFileLock = MUTEX_INITIALIZER("amdgpu_drm log file");


extern "C" int
lx_haiku_log_file(const char* path)
{
	MutexLocker locker(sLogFileLock);
	if (sLogFile >= 0) {
		_kern_close(sLogFile);
		sLogFile = -1;
	}
	if (path == NULL || path[0] == '\0')
		return 0;
	int fd = _kern_open(-1, path, O_WRONLY, 0);
	if (fd < 0)
		return -kLinuxEIO;
	struct stat st;
	if (_kern_read_stat(fd, NULL, false, &st, sizeof(st)) != B_OK
		|| st.st_size <= 0) {
		_kern_close(fd);
		return -kLinuxEIO;
	}
	sLogFile = fd;
	sLogPosition = 0;
	sLogSize = st.st_size;
	return 0;
}


extern "C" void
lx_haiku_log(const char* text)
{
	dprintf("%s", text);
	if (sLogFile >= 0 && are_interrupts_enabled()) {
		MutexLocker locker(sLogFileLock);
		size_t length = strlen(text);
		if (sLogFile >= 0 && sLogPosition + (off_t)length <= sLogSize) {
			_kern_write(sLogFile, sLogPosition, text, length);
			sLogPosition += length;
			_kern_fsync(sLogFile, true);
		}
	}
}


extern "C" void
lx_haiku_panic(const char* text)
{
	// the log file is the only record that survives on the lab machine
	lx_haiku_log(text);
	panic("%s", text);
	for (;;)
		;
}


extern "C" void
lx_haiku_dump_stack(void)
{
	// The kernel debugger's "bt" needs a KDL session; leave a marker.
	dprintf("amdgpu: (stack trace not available)\n");
}


extern "C" long long
lx_haiku_system_time(void)
{
	return system_time();
}


extern "C" long long
lx_haiku_real_time(void)
{
	return real_time_clock_usecs();
}


extern "C" void
lx_haiku_snooze(long long us)
{
	snooze(us);
}


extern "C" void
lx_haiku_spin(long long us)
{
	spin(us);
}


extern "C" int
lx_haiku_cpu_count(void)
{
	return smp_get_num_cpus();
}


extern "C" int
lx_haiku_current_cpu(void)
{
	return smp_get_current_cpu();
}


extern "C" int
lx_haiku_cpu_has(int feature)
{
	// feature numbers of lx/mm.h
	switch (feature) {
		case 1: return x86_check_feature(IA32_FEATURE_CLFSH, FEATURE_COMMON);
		case 2: return x86_check_feature(IA32_FEATURE_CLFLUSHOPT, FEATURE_7_EBX);
		case 3: return x86_check_feature(IA32_FEATURE_PAT, FEATURE_COMMON);
		case 4: return x86_check_feature(IA32_FEATURE_EXT_HYPERVISOR, FEATURE_EXT);
		case 5: return x86_check_feature(IA32_FEATURE_SSE, FEATURE_COMMON);
		case 6: return x86_check_feature(IA32_FEATURE_SSE2, FEATURE_COMMON);
		case 7: return x86_check_feature(IA32_FEATURE_EXT_SSE4_1, FEATURE_EXT);
		case 8: return x86_check_feature(IA32_FEATURE_EXT_AVX, FEATURE_EXT);
	}
	return 0;
}


extern "C" void
lx_haiku_cpu_info(unsigned* family, unsigned* model, unsigned* stepping,
	unsigned* vendorIsAMD, unsigned* clflushSize)
{
	const arch_cpu_info& info = gCPU[0].arch;
	*family = info.family + info.extended_family;
	*model = info.model + (info.extended_model << 4);
	*stepping = info.stepping;
	*vendorIsAMD = info.vendor == VENDOR_AMD;
	*clflushSize = 64;
}


static void
wbinvd_on_cpu(void*, int)
{
	asm volatile("wbinvd" ::: "memory");
}


extern "C" void
lx_haiku_wbinvd_all(void)
{
	call_all_cpus_sync(wbinvd_on_cpu, NULL);
}


extern "C" void
lx_haiku_clflush_range(void* address, unsigned long size)
{
	addr_t start = (addr_t)address & ~(addr_t)63;
	addr_t end = (addr_t)address + size;
	asm volatile("mfence" ::: "memory");
	for (addr_t line = start; line < end; line += 64)
		asm volatile("clflush %0" : "+m" (*(volatile char*)line));
	asm volatile("mfence" ::: "memory");
}


extern "C" void
lx_haiku_random(void* buffer, unsigned long size)
{
	uint8* bytes = (uint8*)buffer;
	while (size > 0) {
		unsigned int value = secure_random_value();
		size_t chunk = min_c(size, sizeof(value));
		memcpy(bytes, &value, chunk);
		bytes += chunk;
		size -= chunk;
	}
}


// #pragma mark - locks


extern "C" void
lx_haiku_mutex_init(void* storage, const char* name)
{
	mutex_init((mutex*)storage, name != NULL ? name : "linux mutex");
}


extern "C" void
lx_haiku_mutex_destroy(void* storage)
{
	mutex_destroy((mutex*)storage);
}


extern "C" void
lx_haiku_mutex_lock(void* storage)
{
	mutex_lock((mutex*)storage);
}


extern "C" int
lx_haiku_mutex_trylock(void* storage)
{
	return mutex_trylock((mutex*)storage) == B_OK;
}


extern "C" void
lx_haiku_mutex_unlock(void* storage)
{
	mutex_unlock((mutex*)storage);
}


extern "C" void
lx_haiku_rwlock_init(void* storage, const char* name)
{
	rw_lock_init((rw_lock*)storage, name != NULL ? name : "linux rwsem");
}


extern "C" void
lx_haiku_rwlock_destroy(void* storage)
{
	rw_lock_destroy((rw_lock*)storage);
}


extern "C" void
lx_haiku_rwlock_read_lock(void* storage)
{
	rw_lock_read_lock((rw_lock*)storage);
}


extern "C" int
lx_haiku_rwlock_read_trylock(void* storage)
{
	return rw_lock_read_lock_with_timeout((rw_lock*)storage, B_RELATIVE_TIMEOUT,
		0) == B_OK;
}


extern "C" void
lx_haiku_rwlock_read_unlock(void* storage)
{
	rw_lock_read_unlock((rw_lock*)storage);
}


extern "C" void
lx_haiku_rwlock_write_lock(void* storage)
{
	rw_lock_write_lock((rw_lock*)storage);
}


extern "C" int
lx_haiku_rwlock_write_trylock(void* storage)
{
	rw_lock* lock = (rw_lock*)storage;
	// Haiku has no timed write lock; a write lock is free when nobody holds
	// or waits for it, which only the lock's own spinlock can tell.
	InterruptsSpinLocker locker(lock->lock);
	if (lock->holder >= 0 || lock->count != 0 || lock->waiters != NULL)
		return 0;
	locker.Unlock();
	// A racing reader or writer can still get in first; then this waits
	// briefly, which Linux's callers tolerate.
	rw_lock_write_lock(lock);
	return 1;
}


extern "C" void
lx_haiku_rwlock_write_unlock(void* storage)
{
	rw_lock_write_unlock((rw_lock*)storage);
}


static spinlock sGlobalLock = B_SPINLOCK_INITIALIZER;


extern "C" unsigned long
lx_haiku_global_lock(void)
{
	cpu_status state = disable_interrupts();
	acquire_spinlock(&sGlobalLock);
	return state;
}


extern "C" void
lx_haiku_global_unlock(unsigned long state)
{
	release_spinlock(&sGlobalLock);
	restore_interrupts((cpu_status)state);
}


// #pragma mark - events


struct LinuxEvent {
	spinlock			lock;
	bool				signalled;
	ConditionVariable	condition;

	LinuxEvent()
		:
		signalled(false)
	{
		B_INITIALIZE_SPINLOCK(&lock);
		condition.Init(this, "linux event");
	}
};


extern "C" void*
lx_haiku_event_create(void)
{
	return new(std::nothrow) LinuxEvent;
}


extern "C" void
lx_haiku_event_delete(void* event)
{
	delete (LinuxEvent*)event;
}


extern "C" void
lx_haiku_event_signal(void* _event)
{
	LinuxEvent* event = (LinuxEvent*)_event;
	InterruptsSpinLocker locker(event->lock);
	event->signalled = true;
	event->condition.NotifyAll();
}


extern "C" int
lx_haiku_event_wait(void* _event, long long timeout)
{
	LinuxEvent* event = (LinuxEvent*)_event;

	InterruptsSpinLocker locker(event->lock);
	if (event->signalled) {
		event->signalled = false;
		return 0;
	}
	if (timeout == 0)
		return -kLinuxETIMEDOUT;

	ConditionVariableEntry entry;
	event->condition.Add(&entry);
	locker.Unlock();

	if (timeout < 0)
		entry.Wait();
	else
		entry.Wait(B_RELATIVE_TIMEOUT, timeout);

	locker.Lock();
	if (event->signalled) {
		event->signalled = false;
		return 0;
	}
	return -kLinuxETIMEDOUT;
}


// #pragma mark - threads


extern "C" lx_thread_id
lx_haiku_current_thread(void)
{
	return thread_get_current_thread_id();
}


extern "C" lx_thread_id
lx_haiku_current_team(void)
{
	return team_get_current_team_id();
}


extern "C" void
lx_haiku_team_name(lx_thread_id team, char* buffer, unsigned long size)
{
	team_info info;
	if (get_team_info(team, &info) == B_OK) {
		// the first word of the command line, without its directory
		char* args = info.args;
		char* end = strchr(args, ' ');
		if (end != NULL)
			*end = '\0';
		const char* name = strrchr(args, '/');
		strlcpy(buffer, name != NULL ? name + 1 : args, size);
	} else
		strlcpy(buffer, "kernel", size);
}


extern "C" void
lx_haiku_thread_name(lx_thread_id thread, char* buffer, unsigned long size)
{
	thread_info info;
	if (get_thread_info(thread, &info) == B_OK)
		strlcpy(buffer, info.name, size);
	else
		strlcpy(buffer, "?", size);
}


extern "C" lx_thread_id
lx_haiku_spawn(int (*entry)(void*), const char* name, int priority,
	void* argument)
{
	return spawn_kernel_thread((thread_func)entry, name, priority, argument);
}


extern "C" void
lx_haiku_resume(lx_thread_id thread)
{
	resume_thread(thread);
}


extern "C" void
lx_haiku_wait_for_thread(lx_thread_id thread)
{
	status_t result;
	wait_for_thread(thread, &result);
}


extern "C" void
lx_haiku_set_priority(lx_thread_id thread, int priority)
{
	set_thread_priority(thread, priority);
}


extern "C" void
lx_haiku_yield(void)
{
	thread_yield();
}


// #pragma mark - tasks of threads


/*	thread_id -> Linux task, an open addressing table under the global
	spinlock. Lookups happen on every "current"; threads are few. */

static const int32 kTaskSlots = 8192;
struct TaskSlot {
	thread_id	thread;		// 0 free, -1 deleted
	void*		task;
};
static TaskSlot sTaskSlots[kTaskSlots];
static void (*sTaskExitHook)(void*) = NULL;


static inline uint32
task_hash(thread_id thread)
{
	return ((uint32)thread * 2654435761u) % kTaskSlots;
}


extern "C" void*
lx_haiku_task_lookup(lx_thread_id thread)
{
	unsigned long state = lx_haiku_global_lock();
	uint32 index = task_hash(thread);
	void* task = NULL;
	for (int32 i = 0; i < kTaskSlots; i++) {
		TaskSlot& slot = sTaskSlots[(index + i) % kTaskSlots];
		if (slot.thread == 0)
			break;
		if (slot.thread == thread) {
			task = slot.task;
			break;
		}
	}
	lx_haiku_global_unlock(state);
	return task;
}


extern "C" int
lx_haiku_task_register(lx_thread_id thread, void* task)
{
	unsigned long state = lx_haiku_global_lock();
	uint32 index = task_hash(thread);
	int result = -kLinuxENOMEM;
	for (int32 i = 0; i < kTaskSlots; i++) {
		TaskSlot& slot = sTaskSlots[(index + i) % kTaskSlots];
		if (slot.thread == 0 || slot.thread == -1 || slot.thread == thread) {
			slot.thread = thread;
			slot.task = task;
			result = 0;
			break;
		}
	}
	lx_haiku_global_unlock(state);
	return result;
}


extern "C" void
lx_haiku_task_unregister(lx_thread_id thread)
{
	unsigned long state = lx_haiku_global_lock();
	uint32 index = task_hash(thread);
	for (int32 i = 0; i < kTaskSlots; i++) {
		TaskSlot& slot = sTaskSlots[(index + i) % kTaskSlots];
		if (slot.thread == 0)
			break;
		if (slot.thread == thread) {
			slot.thread = -1;
			slot.task = NULL;
			break;
		}
	}
	lx_haiku_global_unlock(state);
}


class TaskExitListener : public NotificationListener {
public:
	virtual void EventOccurred(NotificationService& service,
		const KMessage* event)
	{
		int32 eventCode;
		thread_id thread;
		if (event->FindInt32("event", &eventCode) != B_OK
			|| eventCode != THREAD_REMOVED
			|| event->FindInt32("thread", &thread) != B_OK) {
			return;
		}
		void* task = lx_haiku_task_lookup(thread);
		if (task == NULL)
			return;
		lx_haiku_task_unregister(thread);
		if (sTaskExitHook != NULL)
			sTaskExitHook(task);
	}
};


static TaskExitListener* sTaskExitListener = NULL;


extern "C" void
lx_haiku_set_task_exit_hook(void (*hook)(void*))
{
	sTaskExitHook = hook;
	if (hook != NULL && sTaskExitListener == NULL) {
		sTaskExitListener = new(std::nothrow) TaskExitListener;
		if (sTaskExitListener != NULL) {
			NotificationManager::Manager().AddListener("threads",
				THREAD_REMOVED, *sTaskExitListener);
		}
	} else if (hook == NULL && sTaskExitListener != NULL) {
		NotificationManager::Manager().RemoveListener("threads", NULL,
			*sTaskExitListener);
		delete sTaskExitListener;
		sTaskExitListener = NULL;
	}
}


// #pragma mark - kernel heap and areas


extern "C" void*
lx_haiku_malloc(unsigned long size)
{
	return malloc(size);
}


extern "C" void*
lx_haiku_realloc(void* pointer, unsigned long size)
{
	return realloc(pointer, size);
}


extern "C" void
lx_haiku_free(void* pointer)
{
	free(pointer);
}


static const char* kAreaName = "linux vmalloc";


extern "C" void*
lx_haiku_area_alloc(unsigned long size, int clear)
{
	void* address = NULL;
	area_id area = create_area(kAreaName, &address, B_ANY_KERNEL_ADDRESS,
		ROUNDUP(size, B_PAGE_SIZE), B_FULL_LOCK,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
	if (area < 0)
		return NULL;
	if (clear)
		memset(address, 0, size);
	return address;
}


extern "C" int
lx_haiku_is_area_alloc(const void* address)
{
	area_id area = area_for((void*)address);
	if (area < 0)
		return 0;
	area_info info;
	if (get_area_info(area, &info) != B_OK)
		return 0;
	return strcmp(info.name, kAreaName) == 0 && info.address == address;
}


extern "C" int
lx_haiku_area_free(void* address)
{
	if (!lx_haiku_is_area_alloc(address))
		return -kLinuxENOENT;
	delete_area(area_for(address));
	return 0;
}


// #pragma mark - physical pages


extern "C" void*
lx_haiku_pages_alloc(unsigned long count, unsigned long long maxAddress,
	int clear, unsigned long long* physical)
{
	vm_page* page;
	uint32 flags = PAGE_STATE_WIRED | (clear ? VM_PAGE_ALLOC_CLEAR : 0);
	if (count == 1 && maxAddress == 0) {
		vm_page_reservation reservation;
		if (!vm_page_try_reserve_pages(&reservation, 1, VM_PRIORITY_SYSTEM))
			return NULL;
		page = vm_page_allocate_page(&reservation, flags);
		vm_page_unreserve_pages(&reservation);
	} else {
		physical_address_restrictions restrictions = {};
		restrictions.high_address = maxAddress;
		// Linux allocations of order n are aligned to their size
		restrictions.alignment = count * B_PAGE_SIZE;
		if ((count & (count - 1)) != 0)
			restrictions.alignment = 0;
		page = vm_page_allocate_page_run(flags, count, &restrictions,
			VM_PRIORITY_SYSTEM);
	}
	if (page == NULL)
		return NULL;
	// With DEBUG_PAGE_ACCESS (KDEBUG_LEVEL 2), a page belongs to the thread
	// that allocated it until that thread ends its access; Linux frees pages
	// on any thread (TTM destroys busy buffers on a worker, for instance).
	for (unsigned long i = 0; i < count; i++)
		DEBUG_PAGE_ACCESS_END(&page[i]);
	*physical = (unsigned long long)page->physical_page_number * B_PAGE_SIZE;
	return page;
}


extern "C" void
lx_haiku_pages_free(void* firstPage, unsigned long count)
{
	vm_page* page = (vm_page*)firstPage;
	page_num_t first = page->physical_page_number;
	for (unsigned long i = 0; i < count; i++) {
		vm_page* current = i == 0 ? page : vm_lookup_page(first + i);
		if (current == NULL)
			continue;
		DEBUG_PAGE_ACCESS_START(current);
		vm_page_free(NULL, current);
	}
}


extern "C" unsigned long long
lx_haiku_total_pages(void)
{
	return vm_page_num_pages();
}


extern "C" unsigned long long
lx_haiku_free_pages(void)
{
	return vm_page_num_free_pages();
}


// #pragma mark - mappings


static uint32
memory_type(int type)
{
	switch (type) {
		case LX_HAIKU_MAP_WC:
			return B_WRITE_COMBINING_MEMORY;
		case LX_HAIKU_MAP_WB:
			return B_WRITE_BACK_MEMORY;
		default:
			return B_UNCACHED_MEMORY;
	}
}


extern "C" void*
lx_haiku_map_physical(unsigned long long physical, unsigned long size,
	int type)
{
	phys_addr_t base = physical & ~(phys_addr_t)(B_PAGE_SIZE - 1);
	size_t offset = physical - base;
	void* address = NULL;
	area_id area = map_physical_memory("linux ioremap", base,
		ROUNDUP(size + offset, B_PAGE_SIZE),
		B_ANY_KERNEL_ADDRESS | memory_type(type),
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, &address);
	if (area < 0)
		return NULL;
	return (uint8*)address + offset;
}


extern "C" void
lx_haiku_unmap_physical(void* address)
{
	area_id area = area_for(address);
	if (area >= 0)
		delete_area(area);
}


extern "C" void*
lx_haiku_vmap(const unsigned long long* physical, unsigned long count,
	int type)
{
	generic_io_vec* vecs = (generic_io_vec*)malloc(sizeof(generic_io_vec)
		* count);
	if (vecs == NULL)
		return NULL;
	for (unsigned long i = 0; i < count; i++) {
		vecs[i].base = physical[i];
		vecs[i].length = B_PAGE_SIZE;
	}
	void* address = NULL;
	addr_t size = count * B_PAGE_SIZE;
	// Older kernels map scattered pages only with the default memory type
	area_id area = vm_map_physical_memory_vecs(VMAddressSpace::KernelID(),
		"linux vmap", &address, B_ANY_KERNEL_ADDRESS | memory_type(type),
		&size, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, vecs, count);
	if (area == B_NOT_SUPPORTED) {
		address = NULL;
		size = count * B_PAGE_SIZE;
		area = vm_map_physical_memory_vecs(VMAddressSpace::KernelID(),
			"linux vmap", &address, B_ANY_KERNEL_ADDRESS, &size,
			B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, vecs, count);
	}
	free(vecs);
	if (area < 0)
		return NULL;
	return address;
}


extern "C" void
lx_haiku_vunmap(void* address)
{
	area_id area = area_for(address);
	if (area >= 0)
		delete_area(area);
}


extern "C" unsigned long long
lx_haiku_virt_to_phys(const void* address)
{
	addr_t virt = (addr_t)address;
	if (virt >= LX_HAIKU_PMAP_BASE
		&& virt < LX_HAIKU_PMAP_BASE + KERNEL_PMAP_SIZE) {
		return virt - LX_HAIKU_PMAP_BASE;
	}
	physical_entry entry;
	if (get_memory_map(address, 1, &entry, 1) != B_OK)
		return 0;
	return entry.address;
}


// #pragma mark - team memory


extern "C" int
lx_haiku_user_memcpy(void* to, const void* from, unsigned long size)
{
	if (size == 0)
		return 0;
	if (!IS_USER_ADDRESS(to) && !IS_USER_ADDRESS(from)) {
		// Linux calls copy_{from,to}_user() with kernel buffers too
		// (ioctls the kernel issues itself)
		memcpy(to, from, size);
		return 0;
	}
	return user_memcpy(to, from, size) == B_OK ? 0 : -kLinuxEFAULT;
}


extern "C" long
lx_haiku_user_strlcpy(char* to, const char* from, unsigned long size)
{
	if (!IS_USER_ADDRESS(from))
		return strlcpy(to, from, size);
	ssize_t length = user_strlcpy(to, from, size);
	return length < 0 ? -kLinuxEFAULT : length;
}


extern "C" int
lx_haiku_is_user_address(const void* address, unsigned long size)
{
	return is_user_address_range(address, size);
}


// #pragma mark - files


extern "C" int
lx_haiku_read_file(const char* path, void** _data, unsigned long* _size)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return -kLinuxENOENT;
	struct stat st;
	if (fstat(fd, &st) != 0 || st.st_size <= 0) {
		close(fd);
		return -kLinuxEIO;
	}
	void* data = malloc(st.st_size);
	if (data == NULL) {
		close(fd);
		return -kLinuxENOMEM;
	}
	ssize_t bytesRead = read(fd, data, st.st_size);
	close(fd);
	if (bytesRead != st.st_size) {
		free(data);
		return -kLinuxEIO;
	}
	*_data = data;
	*_size = st.st_size;
	return 0;
}
