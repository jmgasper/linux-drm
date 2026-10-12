/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*	Copy bandwidth of the SDMA engine (the one amdgpu moves buffers with)
	between video and system memory, through libdrm_amdgpu: linear copy
	packets of up to 4 MB on the DMA ring, timed from submission to fence.
	For comparison with the graphics and compute queues (vkcopy).

	amdgpu_sdma_bench [MB] */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <amdgpu.h>
#include <amdgpu_drm.h>


#define SDMA_COPY_LINEAR	0x00000001	/* op 1, sub op 0 */
#define SDMA_NOP			0x00000000
#define SDMA_COPY_MAX		0x3fffe0


static amdgpu_device_handle sDevice;


static double
now_s(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec * 1e-9;
}


struct buffer {
	amdgpu_bo_handle bo;
	amdgpu_va_handle va;
	uint64_t address;
	uint64_t size;
};


static int
make_buffer(struct buffer* buffer, uint64_t size, uint32_t heap,
	uint64_t flags)
{
	struct amdgpu_bo_alloc_request request;
	memset(&request, 0, sizeof(request));
	request.alloc_size = size;
	request.phys_alignment = 4096;
	request.preferred_heap = heap;
	request.flags = flags;
	if (amdgpu_bo_alloc(sDevice, &request, &buffer->bo) != 0)
		return -1;
	if (amdgpu_va_range_alloc(sDevice, amdgpu_gpu_va_range_general, size, 4096,
			0, &buffer->address, &buffer->va, 0) != 0
		|| amdgpu_bo_va_op(buffer->bo, 0, size, buffer->address, 0,
			AMDGPU_VA_OP_MAP) != 0)
		return -1;
	buffer->size = size;
	return 0;
}


static double
time_copy(amdgpu_context_handle context, struct buffer* ib,
	struct buffer* from, struct buffer* to, uint64_t size)
{
	uint32_t* commands;
	if (amdgpu_bo_cpu_map(ib->bo, (void**)&commands) != 0)
		return -1;
	uint32_t n = 0;
	for (uint64_t offset = 0; offset < size; offset += SDMA_COPY_MAX) {
		uint64_t count = size - offset < SDMA_COPY_MAX
			? size - offset : SDMA_COPY_MAX;
		uint64_t source = from->address + offset;
		uint64_t destination = to->address + offset;
		commands[n++] = SDMA_COPY_LINEAR;
		commands[n++] = (uint32_t)count;
		commands[n++] = 0;
		commands[n++] = (uint32_t)source;
		commands[n++] = (uint32_t)(source >> 32);
		commands[n++] = (uint32_t)destination;
		commands[n++] = (uint32_t)(destination >> 32);
	}
	while (n % 8 != 0)
		commands[n++] = SDMA_NOP;
	amdgpu_bo_cpu_unmap(ib->bo);

	amdgpu_bo_handle handles[3] = { ib->bo, from->bo, to->bo };
	amdgpu_bo_list_handle list;
	if (amdgpu_bo_list_create(sDevice, 3, handles, NULL, &list) != 0)
		return -1;
	struct amdgpu_cs_ib_info ibInfo;
	memset(&ibInfo, 0, sizeof(ibInfo));
	ibInfo.ib_mc_address = ib->address;
	ibInfo.size = n;
	struct amdgpu_cs_request request;
	memset(&request, 0, sizeof(request));
	request.ip_type = AMDGPU_HW_IP_DMA;
	request.resources = list;
	request.number_of_ibs = 1;
	request.ibs = &ibInfo;

	double best = 1e9;
	for (int pass = 0; pass < 5; pass++) {
		double start = now_s();
		if (amdgpu_cs_submit(context, 0, &request, 1) != 0)
			return -1;
		struct amdgpu_cs_fence fence;
		memset(&fence, 0, sizeof(fence));
		fence.context = context;
		fence.ip_type = AMDGPU_HW_IP_DMA;
		fence.fence = request.seq_no;
		uint32_t expired = 0;
		if (amdgpu_cs_query_fence_status(&fence, 10000000000ULL, 0, &expired)
				!= 0 || !expired)
			return -1;
		double elapsed = now_s() - start;
		if (elapsed < best)
			best = elapsed;
	}
	amdgpu_bo_list_destroy(list);
	return size / best / 1e9;
}


int
main(int argc, char** argv)
{
	uint64_t size = (uint64_t)(argc > 1 ? atoi(argv[1]) : 64) << 20;
	int fd = open("/dev/dri/renderD128", O_RDWR);
	if (fd < 0) {
		perror("/dev/dri/renderD128");
		return 1;
	}
	uint32_t major, minor;
	if (amdgpu_device_initialize(fd, &major, &minor, &sDevice) != 0)
		return 1;

	struct buffer vram, vram2, gtt, ib;
	if (make_buffer(&vram, size, AMDGPU_GEM_DOMAIN_VRAM, 0) != 0
		|| make_buffer(&vram2, size, AMDGPU_GEM_DOMAIN_VRAM, 0) != 0
		|| make_buffer(&gtt, size, AMDGPU_GEM_DOMAIN_GTT, 0) != 0
		|| make_buffer(&ib, 64 * 1024, AMDGPU_GEM_DOMAIN_GTT, 0) != 0) {
		fprintf(stderr, "FAIL buffers\n");
		return 1;
	}
	amdgpu_context_handle context;
	if (amdgpu_cs_ctx_create(sDevice, &context) != 0)
		return 1;

	printf("SDMA, %llu MB copies (GB/s, best of 5):\n",
		(unsigned long long)(size >> 20));
	printf("  video->system %.2f\n", time_copy(context, &ib, &vram, &gtt, size));
	printf("  system->video %.2f\n", time_copy(context, &ib, &gtt, &vram, size));
	printf("  video->video  %.2f\n", time_copy(context, &ib, &vram, &vram2,
		size));

	/* the copy really happened */
	uint32_t* map;
	if (amdgpu_bo_cpu_map(gtt.bo, (void**)&map) == 0) {
		for (uint64_t i = 0; i < size / 4; i += 1024)
			map[i] = (uint32_t)i;
		amdgpu_bo_cpu_unmap(gtt.bo);
	}
	time_copy(context, &ib, &gtt, &vram, size);
	if (amdgpu_bo_cpu_map(gtt.bo, (void**)&map) == 0) {
		memset(map, 0, size);
		amdgpu_bo_cpu_unmap(gtt.bo);
	}
	time_copy(context, &ib, &vram, &gtt, size);
	int good = 1;
	if (amdgpu_bo_cpu_map(gtt.bo, (void**)&map) == 0) {
		for (uint64_t i = 0; i < size / 4; i += 1024) {
			if (map[i] != (uint32_t)i)
				good = 0;
		}
		amdgpu_bo_cpu_unmap(gtt.bo);
	}
	printf("round trip through video memory: %s\n", good ? "OK" : "WRONG");
	amdgpu_cs_ctx_free(context);
	amdgpu_device_deinitialize(sDevice);
	close(fd);
	return good ? 0 : 1;
}
