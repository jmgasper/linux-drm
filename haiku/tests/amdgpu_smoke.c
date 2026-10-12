/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*	A smoke test of the amdgpu_drm ioctls Mesa depends on, without libdrm:
	version and device info, buffers in GTT and VRAM mapped into the team,
	GPU virtual addresses, and a command submission on the GFX ring whose
	WRITE_DATA packet the GPU must execute.

	cc -I<linux-drm>/linux/include/uapi -I<linux-drm>/haiku/compat/include \
		amdgpu_smoke.c -o amdgpu_smoke */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <OS.h>

#include <drm/drm.h>
#include <drm/amdgpu_drm.h>
#include <drm/drm_haiku.h>


static int sFailures;
static int sLastStage = 99;
static int sProgress = -1;
static off_t sProgressOffset;


/*	A line in the progress file, on disk before the step it announces runs,
	so the step that stopped the machine is known afterwards. */
static void
progress(const char* line)
{
	int length = strlen(line);
	if (sProgress >= 0) {
		// the file is preallocated: overwriting changes no file system
		// structure a crash could lose
		pwrite(sProgress, line, length, sProgressOffset);
		sProgressOffset += length;
		fsync(sProgress);
	}
	printf("%s", line);
	fflush(stdout);
}


/*	Before each stage; false past the last stage asked for. */
static int
stage(int number, const char* name)
{
	char line[128];
	if (number > sLastStage)
		return 0;
	snprintf(line, sizeof(line), "stage %d: %s\n", number, name);
	progress(line);
	return 1;
}

#define CHECK(condition, ...) \
	do { \
		if (condition) { \
			printf("ok    "); \
		} else { \
			printf("FAIL  "); \
			sFailures++; \
		} \
		printf(__VA_ARGS__); \
		printf("\n"); \
	} while (0)


static int
drm_ioctl(int fd, unsigned long request, void* arg)
{
	int result;
	do {
		result = ioctl(fd, request, arg, 0);
	} while (result == -1 && (errno == EINTR || errno == EAGAIN));
	return result;
}


static uint32_t
create_buffer(int fd, uint64_t size, uint32_t domain, uint64_t flags)
{
	union drm_amdgpu_gem_create create;
	memset(&create, 0, sizeof(create));
	create.in.bo_size = size;
	create.in.alignment = 4096;
	create.in.domains = domain;
	create.in.domain_flags = flags;
	if (drm_ioctl(fd, DRM_IOCTL_AMDGPU_GEM_CREATE, &create) != 0)
		return 0;
	return create.out.handle;
}


static void*
map_buffer(int fd, uint32_t handle, uint64_t size)
{
	union drm_amdgpu_gem_mmap mmapArgs;
	struct drm_haiku_map map;
	memset(&mmapArgs, 0, sizeof(mmapArgs));
	mmapArgs.in.handle = handle;
	if (drm_ioctl(fd, DRM_IOCTL_AMDGPU_GEM_MMAP, &mmapArgs) != 0) {
		printf("      GEM_MMAP: %s\n", strerror(errno));
		return NULL;
	}
	memset(&map, 0, sizeof(map));
	map.offset = mmapArgs.out.addr_ptr;
	map.size = size;
	map.flags = DRM_HAIKU_MAP_WRITE;
	if (drm_ioctl(fd, DRM_HAIKU_IOCTL_MAP, &map) != 0) {
		printf("      HAIKU_MAP: %s\n", strerror(errno));
		return NULL;
	}
	return (void*)(uintptr_t)map.address;
}


static int
map_gpu_va(int fd, uint32_t handle, uint64_t address, uint64_t size)
{
	struct drm_amdgpu_gem_va va;
	memset(&va, 0, sizeof(va));
	va.handle = handle;
	va.operation = AMDGPU_VA_OP_MAP;
	va.flags = AMDGPU_VM_PAGE_READABLE | AMDGPU_VM_PAGE_WRITEABLE
		| AMDGPU_VM_PAGE_EXECUTABLE;
	va.va_address = address;
	va.offset_in_bo = 0;
	va.map_size = size;
	return drm_ioctl(fd, DRM_IOCTL_AMDGPU_GEM_VA, &va);
}


static void
unmap_buffer(int fd, void* address)
{
	struct drm_haiku_unmap unmap;
	if (address == NULL)
		return;
	unmap.address = (uintptr_t)address;
	CHECK(drm_ioctl(fd, DRM_HAIKU_IOCTL_UNMAP, &unmap) == 0, "HAIKU_UNMAP %p",
		address);
}


/*	amdgpu_smoke [device [last stage [progress file [hold seconds]]]]
	After the last stage, waits the hold time, then unmaps the buffers and
	closes the device, each step announced in the progress file. */
int
main(int argc, char** argv)
{
	const char* path = argc > 1 ? argv[1] : "/dev/dri/renderD128";
	int hold = 0;
	int fd = -1;
	uint32_t* gttMap = NULL;
	uint32_t* vramMap = NULL;
	uint32_t* ibMap = NULL;
	if (argc > 2)
		sLastStage = atoi(argv[2]);
	if (argc > 3)
		sProgress = open(argv[3], O_WRONLY);
	if (argc > 4)
		hold = atoi(argv[4]);
	if (!stage(1, "open and info"))
		return 0;
	fd = open(path, O_RDWR);
	if (fd < 0) {
		printf("FAIL  open %s: %s\n", path, strerror(errno));
		return 1;
	}

	// version
	char name[64] = "", date[64] = "", desc[128] = "";
	struct drm_version version;
	memset(&version, 0, sizeof(version));
	version.name = name;
	version.name_len = sizeof(name) - 1;
	version.date = date;
	version.date_len = sizeof(date) - 1;
	version.desc = desc;
	version.desc_len = sizeof(desc) - 1;
	CHECK(drm_ioctl(fd, DRM_IOCTL_VERSION, &version) == 0
		&& strcmp(name, "amdgpu") == 0, "DRM_IOCTL_VERSION: %s %d.%d.%d (%s)",
		name, version.version_major, version.version_minor,
		version.version_patchlevel, desc);

	// Haiku device info
	struct drm_haiku_device_info info;
	memset(&info, 0, sizeof(info));
	CHECK(drm_ioctl(fd, DRM_HAIKU_IOCTL_DEVICE_INFO, &info) == 0
		&& info.vendor_id == 0x1002, "DRM_HAIKU_IOCTL_DEVICE_INFO: minor %u "
		"type %u %02x:%02x.%u %04x:%04x sub %04x:%04x rev %02x", info.minor,
		info.node_type, info.bus, info.dev, info.func, info.vendor_id,
		info.device_id, info.subvendor_id, info.subdevice_id,
		info.revision_id);

	// amdgpu device info
	struct drm_amdgpu_info_device device;
	struct drm_amdgpu_info request;
	memset(&device, 0, sizeof(device));
	memset(&request, 0, sizeof(request));
	request.return_pointer = (uintptr_t)&device;
	request.return_size = sizeof(device);
	request.query = AMDGPU_INFO_DEV_INFO;
	CHECK(drm_ioctl(fd, DRM_IOCTL_AMDGPU_INFO, &request) == 0,
		"AMDGPU_INFO_DEV_INFO: family %u, %u CUs, %u shader engines, "
		"max engine clock %llu kHz", device.family, device.cu_active_number,
		device.num_shader_engines,
		(unsigned long long)device.max_engine_clock);

	struct drm_amdgpu_memory_info memory;
	memset(&memory, 0, sizeof(memory));
	request.return_pointer = (uintptr_t)&memory;
	request.return_size = sizeof(memory);
	request.query = AMDGPU_INFO_MEMORY;
	CHECK(drm_ioctl(fd, DRM_IOCTL_AMDGPU_INFO, &request) == 0,
		"AMDGPU_INFO_MEMORY: VRAM %llu MB (%llu MB visible), GTT %llu MB",
		(unsigned long long)memory.vram.total_heap_size >> 20,
		(unsigned long long)memory.cpu_accessible_vram.total_heap_size >> 20,
		(unsigned long long)memory.gtt.total_heap_size >> 20);

	// buffers
	if (!stage(2, "GEM_CREATE"))
		goto done;
	const uint64_t size = 64 * 1024;
	uint32_t gtt = create_buffer(fd, size, AMDGPU_GEM_DOMAIN_GTT, 0);
	uint32_t vram = create_buffer(fd, size, AMDGPU_GEM_DOMAIN_VRAM,
		AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED);
	uint32_t ib = create_buffer(fd, 4096, AMDGPU_GEM_DOMAIN_GTT, 0);
	CHECK(gtt != 0 && vram != 0 && ib != 0, "GEM_CREATE GTT, VRAM, IB");

	if (!stage(3, "map GTT"))
		goto done;
	gttMap = map_buffer(fd, gtt, size);
	if (!stage(4, "map VRAM and IB"))
		goto done;
	vramMap = map_buffer(fd, vram, size);
	ibMap = map_buffer(fd, ib, 4096);
	CHECK(gttMap != NULL && vramMap != NULL && ibMap != NULL,
		"mapped: GTT %p, VRAM %p, IB %p", gttMap, vramMap, ibMap);
	if (gttMap == NULL || vramMap == NULL || ibMap == NULL)
		goto done;

	if (!stage(5, "CPU writes GTT"))
		goto done;
	for (uint32_t i = 0; i < size / 4; i++)
		gttMap[i] = 0x11110000 + i;
	if (!stage(6, "CPU writes VRAM"))
		goto done;
	for (uint32_t i = 0; i < size / 4; i++)
		vramMap[i] = 0x22220000 + i;
	int same = 1;
	for (uint32_t i = 0; i < size / 4; i++) {
		if (gttMap[i] != 0x11110000 + i || vramMap[i] != 0x22220000 + i)
			same = 0;
	}
	CHECK(same, "CPU writes read back from GTT and VRAM");

	// GPU addresses
	if (!stage(7, "GEM_VA"))
		goto done;
	const uint64_t gttAddress = 0x100000, vramAddress = 0x200000,
		ibAddress = 0x300000;
	CHECK(map_gpu_va(fd, gtt, gttAddress, size) == 0
		&& map_gpu_va(fd, vram, vramAddress, size) == 0
		&& map_gpu_va(fd, ib, ibAddress, 4096) == 0, "GEM_VA map");

	// context
	if (!stage(8, "CTX"))
		goto done;
	union drm_amdgpu_ctx ctx;
	memset(&ctx, 0, sizeof(ctx));
	ctx.in.op = AMDGPU_CTX_OP_ALLOC_CTX;
	CHECK(drm_ioctl(fd, DRM_IOCTL_AMDGPU_CTX, &ctx) == 0, "CTX alloc: %u",
		ctx.out.alloc.ctx_id);
	uint32_t ctxId = ctx.out.alloc.ctx_id;

	// IB: WRITE_DATA of 4 dwords into GTT and VRAM
	uint32_t n = 0;
	const uint32_t magic = 0xc0ffee00;
	for (int target = 0; target < 2; target++) {
		uint64_t address = target == 0 ? gttAddress : vramAddress;
		ibMap[n++] = 0xc0000000 | ((4 + 4 - 2) << 16) | (0x37 << 8);
			// PACKET3(WRITE_DATA, 6)
		ibMap[n++] = (5 << 8) | (1 << 20);	// DST_SEL memory, WR_CONFIRM
		ibMap[n++] = (uint32_t)address;
		ibMap[n++] = (uint32_t)(address >> 32);
		for (int i = 0; i < 4; i++)
			ibMap[n++] = magic + target * 16 + i;
	}
	while (n % 8 != 0)
		ibMap[n++] = 0xffff1000;	// type 3 NOP

	// submission: the buffers in a list, one IB
	struct drm_amdgpu_bo_list_entry entries[3] = {
		{ gtt, 0 }, { vram, 0 }, { ib, 0 }
	};
	struct drm_amdgpu_cs_chunk_ib ibChunk;
	memset(&ibChunk, 0, sizeof(ibChunk));
	ibChunk.ip_type = AMDGPU_HW_IP_GFX;
	ibChunk.va_start = ibAddress;
	ibChunk.ib_bytes = n * 4;
	struct drm_amdgpu_bo_list_in listIn;
	memset(&listIn, 0, sizeof(listIn));
	listIn.operation = ~0u;
	listIn.list_handle = ~0u;
	listIn.bo_number = 3;
	listIn.bo_info_size = sizeof(entries[0]);
	listIn.bo_info_ptr = (uintptr_t)entries;
	struct drm_amdgpu_cs_chunk chunks[2];
	chunks[0].chunk_id = AMDGPU_CHUNK_ID_IB;
	chunks[0].length_dw = sizeof(ibChunk) / 4;
	chunks[0].chunk_data = (uintptr_t)&ibChunk;
	chunks[1].chunk_id = AMDGPU_CHUNK_ID_BO_HANDLES;
	chunks[1].length_dw = sizeof(listIn) / 4;
	chunks[1].chunk_data = (uintptr_t)&listIn;
	uint64_t chunkPointers[2] = { (uintptr_t)&chunks[0],
		(uintptr_t)&chunks[1] };
	union drm_amdgpu_cs cs;
	memset(&cs, 0, sizeof(cs));
	cs.in.ctx_id = ctxId;
	cs.in.num_chunks = 2;
	cs.in.chunks = (uintptr_t)chunkPointers;
	if (!stage(9, "CS"))
		goto done;
	int result = drm_ioctl(fd, DRM_IOCTL_AMDGPU_CS, &cs);
	CHECK(result == 0, "CS submit on GFX: handle %llu%s%s",
		(unsigned long long)cs.out.handle, result != 0 ? " " : "",
		result != 0 ? strerror(errno) : "");

	if (!stage(10, "WAIT_CS"))
		goto done;
	union drm_amdgpu_wait_cs wait;
	memset(&wait, 0, sizeof(wait));
	wait.in.handle = cs.out.handle;
	wait.in.ip_type = AMDGPU_HW_IP_GFX;
	wait.in.ctx_id = ctxId;
	// absolute, on the kernel's monotonic clock: system_time() in ns
	wait.in.timeout = (uint64_t)(system_time() + 2000000) * 1000;
	result = drm_ioctl(fd, DRM_IOCTL_AMDGPU_WAIT_CS, &wait);
	CHECK(result == 0 && wait.out.status == 0, "WAIT_CS: %s, status %llu",
		result == 0 ? "done" : strerror(errno),
		(unsigned long long)wait.out.status);

	int written = 1;
	for (int i = 0; i < 4; i++) {
		if (gttMap[i] != magic + i || vramMap[i] != magic + 16 + i)
			written = 0;
	}
	CHECK(written, "GPU wrote GTT %08x %08x .. and VRAM %08x %08x ..",
		gttMap[0], gttMap[1], vramMap[0], vramMap[1]);
	CHECK(gttMap[4] == 0x11110004 && vramMap[4] == 0x22220004,
		"and nothing beyond");

done:
	if (hold > 0) {
		progress("exit: hold\n");
		sleep(hold);
	}
	progress("exit: unmap\n");
	unmap_buffer(fd, gttMap);
	unmap_buffer(fd, vramMap);
	unmap_buffer(fd, ibMap);
	progress("exit: close\n");
	close(fd);
	progress("exit: done\n");
	printf("%d failure(s)\n", sFailures);
	return sFailures != 0;
}
