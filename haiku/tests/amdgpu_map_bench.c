/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*	CPU bandwidth of buffer object mappings through libdrm_amdgpu, by
	placement: GTT cached, GTT write-combined, VRAM. Reads from cached GTT
	should run at memory speed; reads from write-combined or VRAM mappings
	are slow by design. */

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <OS.h>

#include <amdgpu.h>
#include <amdgpu_drm.h>


static void
bench(amdgpu_device_handle device, const char* name, uint32_t heap,
	uint64_t flags)
{
	const uint64_t size = 16 << 20;
	struct amdgpu_bo_alloc_request request;
	amdgpu_bo_handle bo;
	memset(&request, 0, sizeof(request));
	request.alloc_size = size;
	request.phys_alignment = 4096;
	request.preferred_heap = heap;
	request.flags = flags;
	if (amdgpu_bo_alloc(device, &request, &bo) != 0) {
		printf("%-16s alloc failed\n", name);
		return;
	}
	volatile uint64_t* map;
	if (amdgpu_bo_cpu_map(bo, (void**)&map) != 0) {
		printf("%-16s map failed\n", name);
		amdgpu_bo_free(bo);
		return;
	}
	bigtime_t start = system_time();
	for (uint64_t i = 0; i < size / 8; i++)
		map[i] = i;
	bigtime_t written = system_time();
	uint64_t sum = 0;
	for (uint64_t i = 0; i < size / 8; i++)
		sum += map[i];
	bigtime_t read = system_time();
	printf("%-16s write %7.1f MB/s, read %7.1f MB/s (%s)\n", name,
		size / (double)(written - start), size / (double)(read - written),
		sum == (size / 8) * (size / 8 - 1) / 2 ? "ok" : "WRONG");
	amdgpu_bo_cpu_unmap(bo);
	amdgpu_bo_free(bo);
}


int
main(void)
{
	int fd = open("/dev/dri/renderD128", O_RDWR);
	uint32_t major, minor;
	amdgpu_device_handle device;
	if (fd < 0 || amdgpu_device_initialize(fd, &major, &minor, &device) != 0)
		return 1;
	bench(device, "GTT cached", AMDGPU_GEM_DOMAIN_GTT, 0);
	bench(device, "GTT USWC", AMDGPU_GEM_DOMAIN_GTT,
		AMDGPU_GEM_CREATE_CPU_GTT_USWC);
	bench(device, "VRAM", AMDGPU_GEM_DOMAIN_VRAM,
		AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED);
	amdgpu_device_deinitialize(device);
	close(fd);
	return 0;
}
