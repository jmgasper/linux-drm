/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*	A buffer stays mapped while the kernel moves it. To make room in the
	CPU-visible part of VRAM, TTM evicts buffers the CPU has mapped (one that
	needs CPU access to GTT, others to the invisible part of VRAM, from
	where the next CPU access moves them back); the mapping has to follow
	the buffer - the driver unmaps its pages and the next access faults
	them in at the buffer's new place. Otherwise the program reads, and
	writes, memory that is no longer the buffer's.

	Per round: fills two mapped VRAM buffers (one with CPU_ACCESS_REQUIRED,
	one without), maps and fills enough other buffers to push them out of
	the visible window, then checks what the old mappings read, writes a
	new pattern through them, reads that back through new mappings, and
	checks that the other buffers kept theirs (a word per page).

	amdgpu_mapmove [rounds] [--read-only]

	--read-only only reads through the old mappings: safe with a driver
	whose mappings go stale (a write through one lands in memory that
	belongs to someone else by then). */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <amdgpu.h>
#include <amdgpu_drm.h>
#include <xf86drm.h>


#define TARGET_SIZE		(4 << 20)
#define PRESSURE_SIZE	(8 << 20)


struct buffer {
	amdgpu_bo_handle bo;
	uint64_t size;
	uint32_t* cpu;
};


static int sFd;
static amdgpu_device_handle sDevice;


static uint32_t
pattern(uint32_t seed, uint64_t index)
{
	return (uint32_t)(index * 2654435761u) ^ seed;
}


static int
allocate(struct buffer* buffer, uint64_t size, uint64_t flags)
{
	struct amdgpu_bo_alloc_request request;
	memset(&request, 0, sizeof(request));
	request.alloc_size = size;
	request.phys_alignment = 4096;
	request.preferred_heap = AMDGPU_GEM_DOMAIN_VRAM;
	request.flags = flags;
	buffer->cpu = NULL;
	buffer->size = size;
	return amdgpu_bo_alloc(sDevice, &request, &buffer->bo);
}


static void
fill(uint32_t* cpu, uint64_t size, uint32_t seed)
{
	for (uint64_t i = 0; i < size / 4; i++)
		cpu[i] = pattern(seed, i);
}


/* every \a step-th word: reads through the PCI BAR are slow (a few MB/s) */
static uint64_t
check_every(const volatile uint32_t* cpu, uint64_t size, uint32_t seed,
	uint64_t step)
{
	uint64_t wrong = 0;
	for (uint64_t i = 0; i < size / 4; i += step) {
		if (cpu[i] != pattern(seed, i))
			wrong++;
	}
	return wrong;
}


static uint64_t
check(const volatile uint32_t* cpu, uint64_t size, uint32_t seed)
{
	return check_every(cpu, size, seed, 1);
}


/* a mapping of the buffer's own, not libdrm's shared one */
static uint32_t*
map_again(struct buffer* buffer)
{
	uint32_t handle;
	if (amdgpu_bo_export(buffer->bo, amdgpu_bo_handle_type_kms, &handle) != 0)
		return NULL;
	union drm_amdgpu_gem_mmap args;
	memset(&args, 0, sizeof(args));
	args.in.handle = handle;
	if (drmCommandWriteRead(sFd, DRM_AMDGPU_GEM_MMAP, &args, sizeof(args))
			!= 0)
		return NULL;
	void* cpu = drmHaikuMmap(NULL, buffer->size, PROT_READ, MAP_SHARED, sFd,
		args.out.addr_ptr);
	return cpu == MAP_FAILED ? NULL : cpu;
}


int
main(int argc, char** argv)
{
	int rounds = 20;
	int readOnly = 0;
	setvbuf(stdout, NULL, _IOLBF, 0);
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--read-only") == 0)
			readOnly = 1;
		else
			rounds = atoi(argv[i]);
	}

	uint32_t major, minor;
	sFd = open("/dev/dri/renderD128", O_RDWR);
	if (sFd < 0 || amdgpu_device_initialize(sFd, &major, &minor, &sDevice)
			!= 0) {
		fprintf(stderr, "no amdgpu device\n");
		return 1;
	}
	struct amdgpu_heap_info visible;
	if (amdgpu_query_heap_info(sDevice, AMDGPU_GEM_DOMAIN_VRAM,
			AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED, &visible) != 0) {
		fprintf(stderr, "no heap info\n");
		return 1;
	}
	// a quarter more than the whole visible window
	int pressureCount = (int)(visible.heap_size * 5 / 4 / PRESSURE_SIZE);
	struct buffer* pressure = calloc(pressureCount, sizeof(struct buffer));
	printf("visible VRAM %llu MiB, %d buffers of %d MiB to push the mapped "
		"ones out%s\n", (unsigned long long)visible.heap_size >> 20,
		pressureCount, PRESSURE_SIZE >> 20,
		readOnly ? " (only reading through old mappings)" : "");

	uint64_t staleReads = 0, lostWrites = 0, damaged = 0;
	int failures = 0;
	for (int round = 0; round < rounds; round++) {
		struct buffer targets[2];
		const uint64_t targetFlags[2] = {
			AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED, 0 };
		int t;
		for (t = 0; t < 2; t++) {
			if (allocate(&targets[t], TARGET_SIZE, targetFlags[t]) != 0
				|| amdgpu_bo_cpu_map(targets[t].bo, (void**)&targets[t].cpu)
					!= 0) {
				fprintf(stderr, "round %d: no target buffer\n", round);
				return 1;
			}
			fill(targets[t].cpu, TARGET_SIZE, 0x1000 * round + t);
		}

		// push them out of the visible window
		int made = 0;
		for (int p = 0; p < pressureCount; p++) {
			void* cpu;
			if (allocate(&pressure[p], PRESSURE_SIZE,
					AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED) != 0)
				break;
			made++;
			if (amdgpu_bo_cpu_map(pressure[p].bo, &cpu) == 0) {
				fill(cpu, PRESSURE_SIZE, 0x50000000 + 0x100 * round + p);
				amdgpu_bo_cpu_unmap(pressure[p].bo);
			}
		}

		uint64_t roundStale = 0, roundLost = 0, roundDamaged = 0;
		for (t = 0; t < 2; t++) {
			roundStale += check(targets[t].cpu, TARGET_SIZE,
				0x1000 * round + t);
		}
		if (!readOnly) {
			for (t = 0; t < 2; t++)
				fill(targets[t].cpu, TARGET_SIZE, 0x2000 * round + t + 7);
			for (t = 0; t < 2; t++) {
				uint32_t* again = map_again(&targets[t]);
				if (again == NULL) {
					fprintf(stderr, "round %d: no second mapping (%s)\n",
						round, strerror(errno));
					failures++;
					continue;
				}
				roundLost += check(again, TARGET_SIZE,
					0x2000 * round + t + 7);
				drmHaikuMunmap(again, TARGET_SIZE);
			}
			for (int p = 0; p < made; p++) {
				void* cpu;
				if (amdgpu_bo_cpu_map(pressure[p].bo, &cpu) == 0) {
					// a word per page: a stale write damages whole pages
					roundDamaged += check_every(cpu, PRESSURE_SIZE,
						0x50000000 + 0x100 * round + p, 1024);
					amdgpu_bo_cpu_unmap(pressure[p].bo);
				}
			}
		}
		for (int p = 0; p < made; p++)
			amdgpu_bo_free(pressure[p].bo);
		for (t = 0; t < 2; t++) {
			amdgpu_bo_cpu_unmap(targets[t].bo);
			amdgpu_bo_free(targets[t].bo);
		}
		if (roundStale != 0 || roundLost != 0 || roundDamaged != 0) {
			printf("round %d: %llu stale words read, %llu written words "
				"lost, %llu words of other buffers damaged\n", round,
				(unsigned long long)roundStale, (unsigned long long)roundLost,
				(unsigned long long)roundDamaged);
		}
		staleReads += roundStale;
		lostWrites += roundLost;
		damaged += roundDamaged;
	}

	printf("amdgpu_mapmove: %d rounds, %llu stale words read, %llu written "
		"words lost, %llu words of other buffers damaged, %d failures\n",
		rounds, (unsigned long long)staleReads,
		(unsigned long long)lostWrites, (unsigned long long)damaged,
		failures);
	amdgpu_device_deinitialize(sDevice);
	return staleReads != 0 || lostWrites != 0 || damaged != 0 || failures != 0;
}
