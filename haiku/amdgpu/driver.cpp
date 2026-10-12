/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*	The Haiku driver around Linux's amdgpu: finds AMD GPUs on PCI, starts
	the DRM core and amdgpu (glue.c), publishes /dev/dri/cardN and
	/dev/dri/renderDN, and forwards open, ioctl, read and select to the DRM
	core. Buffer objects are mapped into teams by DRM_HAIKU_IOCTL_MAP.

	Settings (~/config/settings/kernel/drivers/amdgpu_drm): one Linux
	module parameter per line, "name value" (amdgpu's and drm's, e.g.
	"dc 1" or "debug 0x6"). */

#include <new>
#include <stdio.h>
#include <string.h>

#include <driver_settings.h>
#include <Drivers.h>
#include <graphic_driver.h>
#include <KernelExport.h>
#include <PCI.h>
#include <OS.h>
#include <SupportDefs.h>
#include <errno.h>

#include <lock.h>
#include <team.h>
#include <util/AutoLock.h>
#include <util/DoublyLinkedList.h>
#include <vm/vm.h>
#include <vm/VMAddressSpace.h>
#include <vm/VMArea.h>
#include <sys/uio.h>
#include <util/iovec_support.h>

#include <syscalls.h>

#include "glue.h"


#define DRIVER_NAME "amdgpu_drm"
#define TRACE(x...) dprintf(DRIVER_NAME ": " x)

#ifdef AMDGPU_DRM_ARMED_ONLY
// lab builds: steps that may stop the machine go to the synced log file too
extern "C" void lx_haiku_log(const char* text);
extern "C" void lx_trace_calls_start(void);
#	define LAB_LOG(x...) \
	do { \
		char _line[200]; \
		snprintf(_line, sizeof(_line), DRIVER_NAME ": " x); \
		lx_haiku_log(_line); \
	} while (0)
#else
#	define LAB_LOG(x...) do { } while (0)
#	define lx_trace_calls_start() do { } while (0)
#endif

// Linux's DRM ioctl numbers (asm-generic/ioctl.h) for the Haiku additions
#define LX_IOC(dir, type, nr, size) \
	(((uint32)(dir) << 30) | ((uint32)(size) << 16) | ((type) << 8) | (nr))
struct drm_haiku_map {
	uint64	offset;
	uint64	size;
	uint32	flags;
	int32	area;
	uint64	address;
};
struct drm_haiku_unmap {
	uint64	address;
};
static const uint32 kDrmHaikuMap = LX_IOC(3, 'd', 0xf0,
	sizeof(drm_haiku_map));
static const uint32 kDrmHaikuUnmap = LX_IOC(1, 'd', 0xf1,
	sizeof(drm_haiku_unmap));
struct drm_haiku_device_info {
	uint32	minor;
	uint32	node_type;
	uint16	domain;
	uint8	bus;
	uint8	dev;
	uint8	func;
	uint8	revision_id;
	uint16	vendor_id;
	uint16	device_id;
	uint16	subvendor_id;
	uint16	subdevice_id;
	uint32	class_code;
	uint32	file_id[2];
	uint32	reserved[2];
};
#ifdef AMDGPU_DRM_ARMED_ONLY
// lab builds: closes the driver's own device file, so that devfs can unload
// it (to test the teardown: then touch the binary)
static const uint32 kDrmHaikuLabUnpin = LX_IOC(0, 'd', 0xff, 0);
// and measures the CPU's bandwidth to VRAM through a kernel mapping
static const uint32 kDrmHaikuLabBench = LX_IOC(0, 'd', 0xfe, 0);
// and forces a DPM performance level (argument: the level, by value)
static const uint32 kDrmHaikuLabPerfLevel = LX_IOC(0, 'd', 0xfd, 0);
static void lab_unpin();
#endif
struct drm_haiku_wait_hotplug {
	uint32	generation;
	uint32	timeout_ms;
};
static const uint32 kDrmHaikuWaitHotplug = LX_IOC(3, 'd', 0xf3,
	sizeof(drm_haiku_wait_hotplug));
static const uint32 kDrmHaikuDeviceInfo = LX_IOC(2, 'd', 0xf2,
	sizeof(drm_haiku_device_info));


int32 api_version = B_CUR_DRIVER_API_VERSION;

static const int kMaxDevices = 16;
static char* sDeviceNames[kMaxDevices + 1];
static int sDeviceMinors[kMaxDevices];
static int sDeviceCount;
static bool sStarted;


// #pragma mark - errors


static status_t
haiku_status(long linuxError)
{
	if (linuxError >= 0)
		return B_OK;
	switch (-linuxError) {
		case 1: return B_NOT_ALLOWED;			// EPERM
		case 2: return B_ENTRY_NOT_FOUND;		// ENOENT
		case 4: return B_INTERRUPTED;			// EINTR
		case 5: return B_IO_ERROR;				// EIO
		case 6: return ENXIO;
		case 7: return E2BIG;
		case 9: return B_FILE_ERROR;			// EBADF
		case 11: return B_WOULD_BLOCK;			// EAGAIN
		case 12: return B_NO_MEMORY;			// ENOMEM
		case 13: return B_PERMISSION_DENIED;	// EACCES
		case 14: return B_BAD_ADDRESS;			// EFAULT
		case 16: return B_BUSY;					// EBUSY
		case 17: return B_FILE_EXISTS;			// EEXIST
		case 19: return ENODEV;
		case 22: return B_BAD_VALUE;			// EINVAL
		case 24: return EMFILE;
		case 25: return ENOTTY;
		case 28: return B_DEVICE_FULL;			// ENOSPC
		case 34: return ERANGE;
		case 35: return EDEADLK;
		case 38: return ENOSYS;
		case 61: return ENODATA;
		case 62: return ETIME;
		case 71: return EPROTO;
		case 75: return EOVERFLOW;
		case 95: return EOPNOTSUPP;
		case 110: return B_TIMED_OUT;			// ETIMEDOUT
		case 114: return EALREADY;
		case 125: return ECANCELED;
		case 512: return B_INTERRUPTED;			// ERESTARTSYS
		case 524: return B_NOT_SUPPORTED;		// ENOTSUPP
	}
	return B_ERROR;
}


// #pragma mark - buffer mappings


struct BufferMapping : DoublyLinkedListLinkImpl<BufferMapping> {
	team_id		team;
	area_id		area;
	addr_t		address;
	void*		glueMapping;
};

typedef DoublyLinkedList<BufferMapping> MappingList;


struct SelectState : DoublyLinkedListLinkImpl<SelectState> {
	selectsync*	sync;
	uint8		event;
	void*		glueSelect;
};

typedef DoublyLinkedList<SelectState> SelectList;


struct DeviceCookie {
	void*		file;
	int			minor;
	mutex		lock;
	MappingList	mappings;
	SelectList	selects;
};


static status_t
map_buffer(DeviceCookie* cookie, drm_haiku_map* request)
{
	unsigned long* pfns;
	int caching;
	void* mapping = lx_glue_map(cookie->file, request->offset, request->size,
		(request->flags & 1) != 0, &pfns, &caching);
	if (int error = lx_glue_is_error(mapping))
		return haiku_status(error);

	uint32 count = request->size / B_PAGE_SIZE;
	generic_io_vec* vecs = new(std::nothrow) generic_io_vec[count];
	if (vecs == NULL) {
		lx_glue_unmap(mapping);
		return B_NO_MEMORY;
	}
	// merge physically contiguous pages
	uint32 vecCount = 0;
	for (uint32 i = 0; i < count; i++) {
		phys_addr_t physical = (phys_addr_t)pfns[i] * B_PAGE_SIZE;
		if (vecCount > 0 && vecs[vecCount - 1].base
				+ vecs[vecCount - 1].length == physical) {
			vecs[vecCount - 1].length += B_PAGE_SIZE;
			continue;
		}
		vecs[vecCount].base = physical;
		vecs[vecCount].length = B_PAGE_SIZE;
		vecCount++;
	}

	uint32 memoryType = caching == 1 ? B_WRITE_COMBINING_MEMORY
		: caching == 0 ? B_UNCACHED_MEMORY : B_WRITE_BACK_MEMORY;
	uint32 protection = B_READ_AREA | ((request->flags & 1) ? B_WRITE_AREA : 0)
		| ((request->flags & 2) ? B_CLONEABLE_AREA : 0);
	void* address = (void*)(addr_t)request->address;
	addr_t size = request->size;
	uint32 addressSpec = address != NULL ? B_EXACT_ADDRESS : B_ANY_ADDRESS;
	area_id area;
	if (vecCount == 1) {
		area = vm_map_physical_memory(team_get_current_team_id(),
			"drm buffer", &address, addressSpec | memoryType, size, protection,
			vecs[0].base, false);
	} else {
		area = vm_map_physical_memory_vecs(team_get_current_team_id(),
			"drm buffer", &address, addressSpec | memoryType, &size,
			protection, vecs, vecCount);
		if (area == B_NOT_SUPPORTED) {
			// older kernels: the default memory type; for PCI apertures the
			// firmware's MTRRs still make that uncached
			address = (void*)(addr_t)request->address;
			size = request->size;
			area = vm_map_physical_memory_vecs(team_get_current_team_id(),
				"drm buffer", &address, addressSpec, &size, protection, vecs,
				vecCount);
		}
	}
	phys_addr_t firstBase = vecs[0].base;
	delete[] vecs;
	if (area < 0) {
		lx_glue_unmap(mapping);
		return area;
	}

	BufferMapping* record = new(std::nothrow) BufferMapping;
	if (record == NULL) {
		delete_area(area);
		lx_glue_unmap(mapping);
		return B_NO_MEMORY;
	}
	record->team = team_get_current_team_id();
	record->area = area;
	record->address = (addr_t)address;
	record->glueMapping = mapping;
	{
		MutexLocker locker(cookie->lock);
		cookie->mappings.Add(record);
	}

#ifdef AMDGPU_DRM_ARMED_ONLY
	uint32 actualType = 0;
	{
		VMArea* vmArea = VMAreas::Lookup(area);
		if (vmArea != NULL)
			actualType = vmArea->MemoryType();
	}
	LAB_LOG("map 0x%" B_PRIx64 " size 0x%" B_PRIx64 ": area %" B_PRId32
		" at 0x%lx, %" B_PRIu32 " range(s) from 0x%" B_PRIx64 ", caching %d, "
		"memory type 0x%" B_PRIx32 "\n", request->offset, request->size, area,
		(addr_t)address, vecCount, (uint64)firstBase, caching, actualType);
#endif
	request->area = area;
	request->address = (addr_t)address;
	return B_OK;
}


static status_t
unmap_buffer(DeviceCookie* cookie, addr_t address)
{
	MutexLocker locker(cookie->lock);
	team_id team = team_get_current_team_id();
	for (MappingList::Iterator it = cookie->mappings.GetIterator();
			BufferMapping* record = it.Next();) {
		if (record->team != team || record->address != address)
			continue;
		it.Remove();
		locker.Unlock();
		lx_trace_calls_start();
		LAB_LOG("unmap 0x%lx: area %" B_PRId32 "\n", address, record->area);
		// the team may have deleted the area itself already
		area_info info;
		if (get_area_info(record->area, &info) == B_OK
			&& info.team == team && (addr_t)info.address == address)
			vm_delete_area(team, record->area, true);
		LAB_LOG("unmap 0x%lx: area deleted\n", address);
		lx_glue_unmap(record->glueMapping);
		LAB_LOG("unmap 0x%lx: done\n", address);
		delete record;
		return B_OK;
	}
	return B_BAD_VALUE;
}


static void
unmap_all(DeviceCookie* cookie)
{
	MutexLocker locker(cookie->lock);
	while (BufferMapping* record = cookie->mappings.RemoveHead()) {
		area_info info;
		if (get_area_info(record->area, &info) == B_OK
			&& info.team == record->team
			&& (addr_t)info.address == record->address)
			vm_delete_area(record->team, record->area, true);
		lx_glue_unmap(record->glueMapping);
		delete record;
	}
}


// #pragma mark - device hooks


static int
minor_of(const char* name)
{
	for (int i = 0; i < sDeviceCount; i++) {
		if (strcmp(sDeviceNames[i], name) == 0)
			return sDeviceMinors[i];
	}
	return -1;
}


static status_t
device_open(const char* name, uint32 flags, void** _cookie)
{
	int minor = minor_of(name);
	if (minor < 0)
		return B_ENTRY_NOT_FOUND;

	DeviceCookie* cookie = new(std::nothrow) DeviceCookie;
	if (cookie == NULL)
		return B_NO_MEMORY;
	mutex_init(&cookie->lock, "amdgpu file");
	cookie->minor = minor;

	int error;
	cookie->file = lx_glue_open(minor, flags & O_ACCMODE, &error);
	if (cookie->file == NULL) {
		mutex_destroy(&cookie->lock);
		delete cookie;
		return haiku_status(error);
	}
	*_cookie = cookie;
	return B_OK;
}


static status_t
device_close(void* _cookie)
{
	return B_OK;
}


static status_t
device_free(void* _cookie)
{
	DeviceCookie* cookie = (DeviceCookie*)_cookie;
	LAB_LOG("free minor %d: mappings\n", cookie->minor);
	unmap_all(cookie);
	LAB_LOG("free minor %d: close\n", cookie->minor);
	lx_glue_close(cookie->file);
	LAB_LOG("free minor %d: done\n", cookie->minor);
	mutex_destroy(&cookie->lock);
	delete cookie;
	return B_OK;
}


static status_t
device_control(void* _cookie, uint32 op, void* buffer, size_t length)
{
	DeviceCookie* cookie = (DeviceCookie*)_cookie;

	if (op == kDrmHaikuMap) {
		drm_haiku_map request;
		if (!IS_USER_ADDRESS(buffer)
			|| user_memcpy(&request, buffer, sizeof(request)) != B_OK)
			return B_BAD_ADDRESS;
		status_t status = map_buffer(cookie, &request);
		if (status != B_OK)
			return status;
		if (user_memcpy(buffer, &request, sizeof(request)) != B_OK)
			return B_BAD_ADDRESS;
		return B_OK;
	}
	if (op == kDrmHaikuUnmap) {
		drm_haiku_unmap request;
		if (!IS_USER_ADDRESS(buffer)
			|| user_memcpy(&request, buffer, sizeof(request)) != B_OK)
			return B_BAD_ADDRESS;
		return unmap_buffer(cookie, request.address);
	}

	if (op == kDrmHaikuWaitHotplug) {
		drm_haiku_wait_hotplug request;
		if (!IS_USER_ADDRESS(buffer)
			|| user_memcpy(&request, buffer, sizeof(request)) != B_OK)
			return B_BAD_ADDRESS;
		unsigned int generation = request.generation;
		int error = lx_glue_wait_hotplug(&generation, request.timeout_ms);
		request.generation = generation;
		if (user_memcpy(buffer, &request, sizeof(request)) != B_OK)
			return B_BAD_ADDRESS;
		return haiku_status(error);
	}

	if (op == kDrmHaikuDeviceInfo) {
		drm_haiku_device_info info = {};
		int bus, device, function;
		if (lx_glue_minor_location(cookie->minor, &bus, &device, &function)
				!= 0)
			return ENODEV;
		info.minor = cookie->minor;
		info.node_type = cookie->minor >= 128 ? 2 : 0;
		// one cookie per open file: dup()ed descriptors share it
		info.file_id[0] = (uint32)(addr_t)cookie;
		info.file_id[1] = (uint32)((uint64)(addr_t)cookie >> 32);
		info.bus = bus;
		info.dev = device;
		info.func = function;
		pci_module_info* pci;
		if (get_module(B_PCI_MODULE_NAME, (module_info**)&pci) == B_OK) {
			pci_info pciInfo;
			for (int32 i = 0; pci->get_nth_pci_info(i, &pciInfo) == B_OK; i++) {
				if (pciInfo.bus != bus || pciInfo.device != device
					|| pciInfo.function != function)
					continue;
				info.vendor_id = pciInfo.vendor_id;
				info.device_id = pciInfo.device_id;
				info.revision_id = pciInfo.revision;
				info.class_code = (pciInfo.class_base << 16)
					| (pciInfo.class_sub << 8) | pciInfo.class_api;
				info.subvendor_id = pciInfo.u.h0.subsystem_vendor_id;
				info.subdevice_id = pciInfo.u.h0.subsystem_id;
				break;
			}
			put_module(B_PCI_MODULE_NAME);
		}
		if (!IS_USER_ADDRESS(buffer)
			|| user_memcpy(buffer, &info, sizeof(info)) != B_OK)
			return B_BAD_ADDRESS;
		return B_OK;
	}

#ifdef AMDGPU_DRM_ARMED_ONLY
	if (op == kDrmHaikuLabUnpin) {
		lab_unpin();
		return B_OK;
	}
	if (op == kDrmHaikuLabBench)
		return haiku_status(lx_glue_lab_bench(cookie->file));
	if (op == kDrmHaikuLabPerfLevel) {
		return haiku_status(lx_glue_lab_perf_level(cookie->file,
			(unsigned int)(addr_t)buffer));
	}
#endif

	if (op == B_GET_ACCELERANT_SIGNATURE) {
		static const char kSignature[] = "amdgpu_kms.accelerant";
		if (!IS_USER_ADDRESS(buffer)
			|| user_strlcpy((char*)buffer, kSignature, length) < B_OK)
			return B_BAD_ADDRESS;
		return B_OK;
	}

	// everything of type 'd' is a DRM ioctl
	if (((op >> 8) & 0xff) == 'd')
		return haiku_status(lx_glue_ioctl(cookie->file, op, buffer));

	return B_DEV_INVALID_IOCTL;
}


static status_t
device_read(void* _cookie, off_t position, void* buffer, size_t* _length)
{
	DeviceCookie* cookie = (DeviceCookie*)_cookie;
	long result = lx_glue_read(cookie->file, buffer, *_length);
	if (result < 0) {
		*_length = 0;
		return haiku_status(result);
	}
	*_length = result;
	return B_OK;
}


static status_t
device_write(void* cookie, off_t position, const void* buffer, size_t* _length)
{
	*_length = 0;
	return B_NOT_ALLOWED;
}


static void
select_notify(void* _state, unsigned int events)
{
	SelectState* state = (SelectState*)_state;
	// POLLIN 0x1, POLLRDNORM 0x40
	if (state->event == B_SELECT_READ && (events & (0x1 | 0x40)) != 0)
		notify_select_event(state->sync, state->event);
}


static status_t
device_select(void* _cookie, uint8 event, uint32 ref, selectsync* sync)
{
	DeviceCookie* cookie = (DeviceCookie*)_cookie;
	if (event != B_SELECT_READ)
		return B_BAD_VALUE;

	SelectState* state = new(std::nothrow) SelectState;
	if (state == NULL)
		return B_NO_MEMORY;
	state->sync = sync;
	state->event = event;

	unsigned int ready = 0;
	state->glueSelect = lx_glue_select(cookie->file, select_notify, state,
		&ready);
	if (state->glueSelect == NULL) {
		delete state;
		return B_NO_MEMORY;
	}
	// the selectsync identifies the state when deselecting
	MutexLocker locker(cookie->lock);
	cookie->selects.Add(state);
	locker.Unlock();

	if ((ready & (0x1 | 0x40)) != 0)
		notify_select_event(sync, event);
	return B_OK;
}


static status_t
device_deselect(void* _cookie, uint8 event, selectsync* sync)
{
	DeviceCookie* cookie = (DeviceCookie*)_cookie;
	MutexLocker locker(cookie->lock);
	SelectState* state = NULL;
	for (SelectList::Iterator it = cookie->selects.GetIterator();
			SelectState* candidate = it.Next();) {
		if (candidate->sync == sync && candidate->event == event) {
			it.Remove();
			state = candidate;
			break;
		}
	}
	locker.Unlock();
	if (state == NULL)
		return B_BAD_VALUE;
	lx_glue_deselect(state->glueSelect);
	delete state;
	return B_OK;
}


static device_hooks sDeviceHooks = {
	device_open,
	device_close,
	device_free,
	device_control,
	device_read,
	device_write,
	device_select,
	device_deselect,
	NULL,
	NULL
};


// #pragma mark - driver


static int sSelfTest;


/* 0: none, 1: test the compat layer and stop, 2: also run a fake DRM device */
static int
self_test_requested()
{
	void* handle = load_driver_settings(DRIVER_NAME);
	if (handle == NULL)
		return 0;
	const char* value = get_driver_parameter(handle, "haiku_selftest", "0",
		"1");
	int selfTest = strtol(value, NULL, 10);
	unload_driver_settings(handle);
	return selfTest;
}


static void
apply_settings()
{
	void* handle = load_driver_settings(DRIVER_NAME);
	if (handle == NULL)
		return;
	const driver_settings* settings = get_driver_settings(handle);
	for (int i = 0; settings != NULL && i < settings->parameter_count; i++) {
		const driver_parameter& parameter = settings->parameters[i];
		if (parameter.value_count > 0
			&& strcmp(parameter.name, "haiku_selftest") != 0)
			lx_glue_set_param(parameter.name, parameter.values[0]);
	}
	unload_driver_settings(handle);
}


static bool
is_amd_gpu(const pci_info& info)
{
	return info.vendor_id == 0x1002 && info.class_base == PCI_display;
}


/*	Lab builds (AMDGPU_DRM_ARMED_ONLY) start only when the arm file was made
	during this boot: devfs may reload the driver any number of times in the
	boot it was armed in, but a start that brought the machine down cannot
	repeat on the next boot, when nobody may be there to pick another boot
	option. A stale arm file is deleted. */
#ifdef AMDGPU_DRM_ARMED_ONLY
static const char* kArmFile
	= "/boot/home/config/settings/kernel/drivers/amdgpu_drm.arm";

static bool
armed_for_this_boot()
{
	struct stat st;
	if (stat(kArmFile, &st) != 0)
		return false;
	time_t bootTime = real_time_clock() - system_time() / 1000000;
	if (st.st_mtime + 5 >= bootTime)
		return true;
	_kern_unlink(-1, kArmFile);
	_kern_sync();
	return false;
}
#endif


#ifndef AMDGPU_DRM_ARMED_ONLY
/*	A start that brings the machine down must not repeat at every boot: there
	may be nobody at the keyboard to pick another boot option (the lab X399
	has none that works). A marker file exists from the start until the GPU
	has run for half a minute; finding it means that the last start did not
	get that far, and the driver stays off (the framebuffer driver drives the
	display) until the marker is deleted. */
static const char* kStartMarker
	= "/boot/home/config/settings/kernel/drivers/amdgpu_drm.starting";
static const bigtime_t kStableAfter = 30000000;
static sem_id sStableSem = -1;
static thread_id sStableThread = -1;


static bool
previous_start_failed()
{
	struct stat st;
	return stat(kStartMarker, &st) == 0;
}


static void
mark_start()
{
	int fd = _kern_open(-1, kStartMarker, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) {
		// a read-only boot volume: no protection, but no harm either
		TRACE("cannot create %s: %s\n", kStartMarker, strerror(fd));
		return;
	}
	static const char kText[] = "amdgpu_drm was starting; delete this file "
		"to let it start again\n";
	_kern_write(fd, 0, kText, sizeof(kText) - 1);
	_kern_fsync(fd, false);
	_kern_close(fd);
	_kern_sync();
}


static status_t
stable_thread(void*)
{
	// released early when the driver is unloaded
	if (acquire_sem_etc(sStableSem, 1, B_RELATIVE_TIMEOUT, kStableAfter)
			== B_TIMED_OUT) {
		_kern_unlink(-1, kStartMarker);
		_kern_sync();
		TRACE("running stably\n");
	}
	return B_OK;
}


static void
start_stable_timer()
{
	sStableSem = create_sem(0, "amdgpu_drm stable");
	if (sStableSem < 0)
		return;
	sStableThread = spawn_kernel_thread(stable_thread, "amdgpu_drm stable",
		B_LOW_PRIORITY, NULL);
	if (sStableThread >= 0)
		resume_thread(sStableThread);
}


static void
stop_stable_timer()
{
	if (sStableSem < 0)
		return;
	release_sem(sStableSem);
	if (sStableThread >= 0) {
		status_t result;
		wait_for_thread(sStableThread, &result);
		sStableThread = -1;
	}
	delete_sem(sStableSem);
	sStableSem = -1;
}
#endif


#ifdef AMDGPU_DRM_ARMED_ONLY
/*	devfs reloads a driver none of whose devices is open whenever a file
	system event marks its binary as changed, and the lab's installation and
	arming cause such events. Like app_server would, keep a device open. */
static int sPinFile = -1;

static status_t
pin_thread(void*)
{
	snooze(200000);
	// the render node: the first opener of card0 becomes DRM master, which
	// is for app_server's accelerant to be
	int fd = _kern_open(-1, "/dev/dri/renderD128", O_RDWR, 0);
	if (fd < 0)
		TRACE("could not keep /dev/dri/renderD128 open: %s\n", strerror(fd));
	else
		sPinFile = fd;
	return B_OK;
}


static status_t
unpin_thread(void*)
{
	// a kernel thread: the descriptor is the kernel team's
	if (sPinFile >= 0) {
		_kern_close(sPinFile);
		sPinFile = -1;
		LAB_LOG("unpinned\n");
	}
	return B_OK;
}


static void
lab_unpin()
{
	thread_id thread = spawn_kernel_thread(unpin_thread, "amdgpu_drm unpin",
		B_NORMAL_PRIORITY, NULL);
	if (thread >= 0) {
		resume_thread(thread);
		status_t result;
		wait_for_thread(thread, &result);
	}
}
#endif


status_t
init_hardware(void)
{

	pci_module_info* pci;
	if (get_module(B_PCI_MODULE_NAME, (module_info**)&pci) != B_OK)
		return B_ERROR;
	pci_info info;
	status_t status = B_ENTRY_NOT_FOUND;
	if (self_test_requested())
		status = B_OK;
	for (int32 i = 0; pci->get_nth_pci_info(i, &info) == B_OK; i++) {
		if (is_amd_gpu(info)) {
			status = B_OK;
			break;
		}
	}
	put_module(B_PCI_MODULE_NAME);
	return status;
}


status_t
init_driver(void)
{
#ifdef AMDGPU_DRM_ARMED_ONLY
	// every start, also devfs reloading the driver later
	if (!armed_for_this_boot()) {
		TRACE("not armed (%s), not starting\n", kArmFile);
		return B_ERROR;
	}
#else
	if (previous_start_failed()) {
		dprintf(DRIVER_NAME ": the last start did not finish (%s exists); not "
			"starting\n", kStartMarker);
		return B_ERROR;
	}
#endif

	pci_module_info* pci;
	if (get_module(B_PCI_MODULE_NAME, (module_info**)&pci) != B_OK)
		return B_ERROR;

	sSelfTest = self_test_requested();
	TRACE("starting Linux DRM and amdgpu\n");
	apply_settings();
	int error = lx_glue_init();
	if (error != 0) {
		TRACE("start failed: %d\n", error);
		put_module(B_PCI_MODULE_NAME);
		return haiku_status(error);
	}
	sStarted = true;

	pci_info info;
	int gpus = 0;
	if (sSelfTest != 0) {
		int failures = lx_glue_selftest();
		if (sSelfTest == 1) {
			TRACE("self test: %d failure(s); stopping again\n", failures);
			lx_glue_uninit();
			sStarted = false;
			put_module(B_PCI_MODULE_NAME);
			TRACE("self test: stopped\n");
			return B_ERROR;
		}
		if (lx_glue_fake_device_start() == 0)
			gpus++;
	}

#ifndef AMDGPU_DRM_ARMED_ONLY
	bool marked = false;
#endif
	for (int32 i = 0; sSelfTest == 0
			&& pci->get_nth_pci_info(i, &info) == B_OK; i++) {
		if (!is_amd_gpu(info))
			continue;
#ifndef AMDGPU_DRM_ARMED_ONLY
		if (!marked) {
			mark_start();
			marked = true;
		}
#endif
		if (lx_glue_probe(info.bus, info.device, info.function) == 0)
			gpus++;
	}
	put_module(B_PCI_MODULE_NAME);

	if (gpus == 0) {
		// nothing to drive: stop again, so that devfs unloads the driver
		TRACE("no GPU started\n");
		lx_glue_uninit();
		sStarted = false;
#ifndef AMDGPU_DRM_ARMED_ONLY
		if (marked) {
			// it failed, but it did not bring the machine down
			_kern_unlink(-1, kStartMarker);
			_kern_sync();
		}
#endif
		return B_ENTRY_NOT_FOUND;
	}

	int minors[kMaxDevices / 2];
	int count = lx_glue_minors(minors, kMaxDevices / 2);
	for (int i = 0; i < count; i++) {
		char name[32];
		if (minors[i] >= 128)
			snprintf(name, sizeof(name), "dri/renderD%d", minors[i]);
		else
			snprintf(name, sizeof(name), "dri/card%d", minors[i]);
		sDeviceNames[sDeviceCount] = strdup(name);
		sDeviceMinors[sDeviceCount] = minors[i];
		sDeviceCount++;
		if (minors[i] < 128) {
			// app_server looks for displays in /dev/graphics only: the same
			// primary node there, which names the KMS accelerant
			snprintf(name, sizeof(name), "graphics/amdgpu_drm_%d", minors[i]);
			sDeviceNames[sDeviceCount] = strdup(name);
			sDeviceMinors[sDeviceCount] = minors[i];
			sDeviceCount++;
		}
	}
	sDeviceNames[sDeviceCount] = NULL;
	TRACE("%d GPU(s), %d device(s)\n", gpus, sDeviceCount);
#ifdef AMDGPU_DRM_ARMED_ONLY
	// after publish_devices(), which follows init_driver()
	thread_id pin = spawn_kernel_thread(pin_thread, "amdgpu_drm pin",
		B_NORMAL_PRIORITY, NULL);
	if (pin >= 0)
		resume_thread(pin);
#else
	start_stable_timer();
#endif
	return B_OK;
}


void
uninit_driver(void)
{
#ifdef AMDGPU_DRM_ARMED_ONLY
	if (sPinFile >= 0) {
		_kern_close(sPinFile);
		sPinFile = -1;
	}
#else
	stop_stable_timer();
#endif
	for (int i = 0; i < sDeviceCount; i++)
		free(sDeviceNames[i]);
	sDeviceCount = 0;
	if (sStarted)
		lx_glue_uninit();
	sStarted = false;
}


const char**
publish_devices(void)
{
	return (const char**)sDeviceNames;
}


device_hooks*
find_device(const char* name)
{
	return minor_of(name) >= 0 ? &sDeviceHooks : NULL;
}
