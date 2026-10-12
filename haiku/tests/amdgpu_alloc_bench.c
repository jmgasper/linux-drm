/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*	What a short-lived buffer object costs: allocating one in GTT, mapping
	it for the CPU, touching every page, unmapping and freeing it, timed
	step by step (radeonsi makes a staging texture like this for every
	readback).

	amdgpu_alloc_bench [MB] [rounds] */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <amdgpu.h>
#include <amdgpu_drm.h>


static double
now_ms()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}


int
main(int argc, char** argv)
{
	size_t size = (argc > 1 ? atoi(argv[1]) : 2) << 20;
	int rounds = argc > 2 ? atoi(argv[2]) : 50;
	int fd = open("/dev/dri/renderD128", O_RDWR);
	if (fd < 0) {
		perror("renderD128");
		return 1;
	}
	uint32_t major, minor;
	amdgpu_device_handle device;
	if (amdgpu_device_initialize(fd, &major, &minor, &device) != 0) {
		fprintf(stderr, "amdgpu_device_initialize failed\n");
		return 1;
	}
	double alloc = 0, map = 0, touch = 0, unmap = 0, release = 0;
	for (int i = 0; i < rounds; i++) {
		struct amdgpu_bo_alloc_request request;
		memset(&request, 0, sizeof(request));
		request.alloc_size = size;
		request.phys_alignment = 4096;
		request.preferred_heap = AMDGPU_GEM_DOMAIN_GTT;
		amdgpu_bo_handle bo;
		double t0 = now_ms();
		if (amdgpu_bo_alloc(device, &request, &bo) != 0) {
			fprintf(stderr, "amdgpu_bo_alloc failed\n");
			return 1;
		}
		double t1 = now_ms();
		void* cpu;
		if (amdgpu_bo_cpu_map(bo, &cpu) != 0) {
			fprintf(stderr, "amdgpu_bo_cpu_map failed\n");
			return 1;
		}
		double t2 = now_ms();
		volatile unsigned sum = 0;
		for (size_t offset = 0; offset < size; offset += 4096)
			sum += ((volatile unsigned char*)cpu)[offset];
		double t3 = now_ms();
		amdgpu_bo_cpu_unmap(bo);
		double t4 = now_ms();
		amdgpu_bo_free(bo);
		double t5 = now_ms();
		alloc += t1 - t0;
		map += t2 - t1;
		touch += t3 - t2;
		unmap += t4 - t3;
		release += t5 - t4;
	}
	printf("%zu MB GTT, per round: alloc %.3f ms, map %.3f ms, touch %.3f ms, "
		"unmap %.3f ms, free %.3f ms\n", size >> 20, alloc / rounds,
		map / rounds, touch / rounds, unmap / rounds, release / rounds);
	amdgpu_device_deinitialize(device);
	close(fd);
	return 0;
}
