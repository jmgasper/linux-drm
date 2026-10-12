/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*	Buffer objects coming and going without any GPU work of the program's
	own: a pool of VRAM (and some GTT) buffers, replaced at random, each
	written by the CPU once it is mapped. A VRAM buffer the CPU touches
	outside the visible part of VRAM is moved into it by the kernel (with
	SDMA), and freed VRAM is cleared before it is used again: what Vulkan
	programs make the kernel do, without the programs.

	amdgpu_churn [seconds] [pool size] */

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <amdgpu.h>
#include <amdgpu_drm.h>


struct slot {
	amdgpu_bo_handle bo;
	uint64_t size;
};


static void
release(struct slot* slot)
{
	if (slot->bo != NULL)
		amdgpu_bo_free(slot->bo);
	slot->bo = NULL;
}


static int
fill(amdgpu_device_handle device, struct slot* slot, unsigned seed)
{
	struct amdgpu_bo_alloc_request request;
	memset(&request, 0, sizeof(request));
	request.alloc_size = (uint64_t)(1 + seed % 16) << 20;
	request.phys_alignment = 4096;
	request.preferred_heap = seed % 5 == 0 ? AMDGPU_GEM_DOMAIN_GTT
		: AMDGPU_GEM_DOMAIN_VRAM;
	// Most without CPU access: placed outside the visible window, moved
	// into it when the CPU writes them.
	if (seed % 3 == 0)
		request.flags |= AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED;
	if (seed % 7 == 0)
		request.flags |= AMDGPU_GEM_CREATE_VRAM_CLEARED;
	if (amdgpu_bo_alloc(device, &request, &slot->bo) != 0)
		return -1;
	slot->size = request.alloc_size;
	void* cpu;
	if (amdgpu_bo_cpu_map(slot->bo, &cpu) == 0) {
		memset(cpu, seed & 0xff, slot->size);
		amdgpu_bo_cpu_unmap(slot->bo);
	}
	return 0;
}


int
main(int argc, char** argv)
{
	int seconds = argc > 1 ? atoi(argv[1]) : 60;
	int count = argc > 2 ? atoi(argv[2]) : 48;
	int fd = open("/dev/dri/renderD128", O_RDWR);
	uint32_t major, minor;
	amdgpu_device_handle device;
	if (fd < 0 || amdgpu_device_initialize(fd, &major, &minor, &device) != 0) {
		fprintf(stderr, "no amdgpu device\n");
		return 1;
	}
	struct slot* pool = calloc(count, sizeof(struct slot));
	time_t end = time(NULL) + seconds;
	unsigned seed = (unsigned)time(NULL), rounds = 0, failed = 0;
	while (time(NULL) < end) {
		seed = seed * 1103515245 + 12345;
		struct slot* slot = &pool[(seed >> 8) % count];
		release(slot);
		if (fill(device, slot, seed >> 4) != 0)
			failed++;
		rounds++;
	}
	for (int i = 0; i < count; i++)
		release(&pool[i]);
	printf("amdgpu_churn: %u buffers made, %u failed\n", rounds, failed);
	amdgpu_device_deinitialize(device);
	return 0;
}
