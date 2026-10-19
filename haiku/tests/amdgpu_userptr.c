/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*	Userptr buffers on a real GPU (DRM_AMDGPU_GEM_USERPTR, as libdrm's
	amdgpu_create_bo_from_user_mem() makes them for RADV's
	VK_EXT_external_memory_host), without libdrm:

	- a clone of a B_NO_LOCK area (app_server's back buffer, as Summit's web
	  process imports it) is imported, the GFX ring writes a word into each
	  of its pages, and the words are read through the original area;
	- the time to import (wire) a buffer the size of a three-monitor back
	  buffer;
	- children that keep the GPU writing into their imported memory die by
	  kill -9 (or exit), sometimes with the buffer already closed: the team's
	  pages must be let go once the GPU is done, before its address space
	  goes, or the kernel panics on the wired area.

	amdgpu_userptr [rounds [device]]	default 50, /dev/dri/renderD128

	cc -I<linux-drm>/linux/include/uapi -I<linux-drm>/haiku/compat/include \
		amdgpu_userptr.c -o amdgpu_userptr */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <OS.h>

#include <drm/drm.h>
#include <drm/amdgpu_drm.h>
#include <drm/drm_haiku.h>


#define PAGES		1024
#define SIZE		(PAGES * B_PAGE_SIZE)
#define USER_VA		0x10000000ULL
#define IB_VA		0x08000000ULL
#define IB_SIZE		(64 * 1024)

static const char* sDevice = "/dev/dri/renderD128";
static int sFailures;


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
		fflush(stdout); \
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


/* the handle, 0 on failure (errno set) */
static uint32_t
import(int fd, void* address, uint64_t size, uint32_t flags)
{
	struct drm_amdgpu_gem_userptr args;
	memset(&args, 0, sizeof(args));
	args.addr = (uintptr_t)address;
	args.size = size;
	args.flags = flags;
	if (drm_ioctl(fd, DRM_IOCTL_AMDGPU_GEM_USERPTR, &args) != 0)
		return 0;
	return args.handle;
}


static void
close_handle(int fd, uint32_t handle)
{
	struct drm_gem_close args = { handle, 0 };
	drm_ioctl(fd, DRM_IOCTL_GEM_CLOSE, &args);
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
	va.map_size = size;
	return drm_ioctl(fd, DRM_IOCTL_AMDGPU_GEM_VA, &va);
}


struct gpu {
	int fd;
	uint32_t ctx;
	uint32_t ib;
	uint32_t* ibMap;
	uint32_t ibDwords;
};


static int
gpu_open(struct gpu* gpu)
{
	memset(gpu, 0, sizeof(*gpu));
	gpu->fd = open(sDevice, O_RDWR | O_CLOEXEC);
	if (gpu->fd < 0)
		return -1;

	union drm_amdgpu_gem_create create;
	memset(&create, 0, sizeof(create));
	create.in.bo_size = IB_SIZE;
	create.in.alignment = 4096;
	create.in.domains = AMDGPU_GEM_DOMAIN_GTT;
	if (drm_ioctl(gpu->fd, DRM_IOCTL_AMDGPU_GEM_CREATE, &create) != 0)
		return -1;
	gpu->ib = create.out.handle;

	union drm_amdgpu_gem_mmap mmapArgs;
	memset(&mmapArgs, 0, sizeof(mmapArgs));
	mmapArgs.in.handle = gpu->ib;
	if (drm_ioctl(gpu->fd, DRM_IOCTL_AMDGPU_GEM_MMAP, &mmapArgs) != 0)
		return -1;
	struct drm_haiku_map map;
	memset(&map, 0, sizeof(map));
	map.offset = mmapArgs.out.addr_ptr;
	map.size = IB_SIZE;
	map.flags = DRM_HAIKU_MAP_WRITE;
	if (drm_ioctl(gpu->fd, DRM_HAIKU_IOCTL_MAP, &map) != 0)
		return -1;
	gpu->ibMap = (uint32_t*)(uintptr_t)map.address;
	if (map_gpu_va(gpu->fd, gpu->ib, IB_VA, IB_SIZE) != 0)
		return -1;

	union drm_amdgpu_ctx ctx;
	memset(&ctx, 0, sizeof(ctx));
	ctx.in.op = AMDGPU_CTX_OP_ALLOC_CTX;
	if (drm_ioctl(gpu->fd, DRM_IOCTL_AMDGPU_CTX, &ctx) != 0)
		return -1;
	gpu->ctx = ctx.out.alloc.ctx_id;
	return 0;
}


/* an IB that writes value + page into the first word of every page,
   \a repeat times over */
static void
gpu_build_writes(struct gpu* gpu, uint32_t value, int repeat)
{
	uint32_t n = 0;
	for (int r = 0; r < repeat; r++) {
		for (uint32_t page = 0; page < PAGES; page++) {
			uint64_t address = USER_VA + (uint64_t)page * B_PAGE_SIZE;
			gpu->ibMap[n++] = 0xc0000000 | ((5 - 2) << 16) | (0x37 << 8);
				// PACKET3(WRITE_DATA), one dword
			gpu->ibMap[n++] = (5 << 8) | (1 << 20);	// memory, WR_CONFIRM
			gpu->ibMap[n++] = (uint32_t)address;
			gpu->ibMap[n++] = (uint32_t)(address >> 32);
			gpu->ibMap[n++] = value + page;
		}
	}
	while (n % 8 != 0)
		gpu->ibMap[n++] = 0xffff1000;	// type 3 NOP
	gpu->ibDwords = n;
}


/* the submission's sequence number, 0 on failure */
static uint64_t
gpu_submit(struct gpu* gpu, uint32_t userptr)
{
	struct drm_amdgpu_bo_list_entry entries[2] = {
		{ userptr, 0 }, { gpu->ib, 0 }
	};
	struct drm_amdgpu_cs_chunk_ib ibChunk;
	memset(&ibChunk, 0, sizeof(ibChunk));
	ibChunk.ip_type = AMDGPU_HW_IP_GFX;
	ibChunk.va_start = IB_VA;
	ibChunk.flags = AMDGPU_IB_FLAG_EMIT_MEM_SYNC;
		// the IB is rewritten in place: the GPU's caches may hold the last one
	ibChunk.ib_bytes = gpu->ibDwords * 4;
	struct drm_amdgpu_bo_list_in listIn;
	memset(&listIn, 0, sizeof(listIn));
	listIn.operation = ~0u;
	listIn.list_handle = ~0u;
	listIn.bo_number = 2;
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
	cs.in.ctx_id = gpu->ctx;
	cs.in.num_chunks = 2;
	cs.in.chunks = (uintptr_t)chunkPointers;
	if (drm_ioctl(gpu->fd, DRM_IOCTL_AMDGPU_CS, &cs) != 0)
		return 0;
	return cs.out.handle;
}


static int
gpu_wait(struct gpu* gpu, uint64_t handle)
{
	union drm_amdgpu_wait_cs wait;
	memset(&wait, 0, sizeof(wait));
	wait.in.handle = handle;
	wait.in.ip_type = AMDGPU_HW_IP_GFX;
	wait.in.ctx_id = gpu->ctx;
	wait.in.timeout = (uint64_t)(system_time() + 5000000) * 1000;
	if (drm_ioctl(gpu->fd, DRM_IOCTL_AMDGPU_WAIT_CS, &wait) != 0)
		return -1;
	return wait.out.status == 0 ? 0 : -1;
}


static area_id
create_buffer(const char* name, void** address, size_t size)
{
	return create_area(name, address, B_ANY_ADDRESS, size, B_NO_LOCK,
		B_READ_AREA | B_WRITE_AREA | B_CLONEABLE_AREA);
}


static const uint32_t kLibdrmFlags = AMDGPU_GEM_USERPTR_ANONONLY
	| AMDGPU_GEM_USERPTR_REGISTER | AMDGPU_GEM_USERPTR_VALIDATE;


static void
test_clone(struct gpu* gpu)
{
	void* original;
	area_id area = create_buffer("back buffer", &original, SIZE);
	void* address;
	area_id clone = clone_area("back buffer clone", &address, B_ANY_ADDRESS,
		B_READ_AREA | B_WRITE_AREA, area);
	CHECK(area >= 0 && clone >= 0, "B_NO_LOCK area and its clone");
	if (area < 0 || clone < 0)
		return;
	memset(original, 0, SIZE);

	uint32_t handle = import(gpu->fd, address, SIZE, kLibdrmFlags);
	CHECK(handle != 0, "GEM_USERPTR of the clone%s%s", handle ? "" : ": ",
		handle ? "" : strerror(errno));
	if (handle == 0)
		goto out;
	CHECK(map_gpu_va(gpu->fd, handle, USER_VA, SIZE) == 0, "GEM_VA");

	gpu_build_writes(gpu, 0xabc00000, 1);
	uint64_t seq = gpu_submit(gpu, handle);
	CHECK(seq != 0, "CS with the userptr buffer%s%s", seq ? "" : ": ",
		seq ? "" : strerror(errno));
	CHECK(seq != 0 && gpu_wait(gpu, seq) == 0, "WAIT_CS");
	int bad = 0;
	for (uint32_t page = 0; page < PAGES; page++) {
		uint32_t* words = (uint32_t*)((char*)original + page * B_PAGE_SIZE);
		if (words[0] != 0xabc00000 + page || words[1] != 0)
			bad++;
	}
	CHECK(bad == 0, "the GPU's writes read through the original area "
		"(%d of %d pages wrong; first word %08x)", bad, PAGES,
		*(uint32_t*)original);

	// again, after the CPU wrote: the same pages, nothing stale
	memset(original, 0, SIZE);
	gpu_build_writes(gpu, 0xdef00000, 1);
	seq = gpu_submit(gpu, handle);
	int submitError = seq == 0 ? errno : 0;
	int waited = seq != 0 ? gpu_wait(gpu, seq) : -1;
	uint32_t word = *(uint32_t*)((char*)original + 7 * B_PAGE_SIZE);
	CHECK(seq != 0 && waited == 0 && word == 0xdef00007,
		"a second submission writes the same pages (CS %s, wait %d, "
		"page 7: %08x)", seq != 0 ? "ok" : strerror(submitError), waited,
		word);
	close_handle(gpu->fd, handle);

out:
	delete_area(clone);
	delete_area(area);
}


static void
test_refusals(struct gpu* gpu)
{
	void* address;
	area_id area = create_area("read only", &address, B_ANY_ADDRESS,
		16 * B_PAGE_SIZE, B_NO_LOCK, B_READ_AREA);
	uint32_t handle = import(gpu->fd, address, 16 * B_PAGE_SIZE, kLibdrmFlags);
	CHECK(handle == 0, "writable import of read-only memory refused (%s)",
		handle ? "imported" : strerror(errno));
	if (handle != 0)
		close_handle(gpu->fd, handle);
	delete_area(area);
	handle = import(gpu->fd, (void*)0xffffffff80000000ULL, 16 * B_PAGE_SIZE,
		kLibdrmFlags);
	CHECK(handle == 0, "import of kernel memory refused (%s)",
		handle ? "imported" : strerror(errno));
}


/* what wiring app_server's back buffer for three monitors costs */
static void
test_big(struct gpu* gpu)
{
	const size_t size = (size_t)9984 * 4 * 2160;
	void* original;
	area_id area = create_buffer("big back buffer", &original, size);
	void* address;
	area_id clone = clone_area("big clone", &address, B_ANY_ADDRESS,
		B_READ_AREA | B_WRITE_AREA, area);
	bigtime_t start = system_time();
	uint32_t handle = import(gpu->fd, address, size, kLibdrmFlags);
	bigtime_t imported = system_time();
	if (handle != 0)
		close_handle(gpu->fd, handle);
	bigtime_t closed = system_time();
	CHECK(handle != 0, "import of %zu MiB: %lld ms, close %lld ms", size >> 20,
		(long long)(imported - start) / 1000,
		(long long)(closed - imported) / 1000);
	delete_area(clone);
	delete_area(area);
}


/*	A child that keeps the GPU writing into its imported memory and dies:
	mode 0 exit, 1 kill -9, 2 close the buffer and kill -9. */
static void
dying_child(int mode, int round)
{
	int ready[2];
	pipe(ready);
	fflush(stdout);
	pid_t child = fork();
	if (child == 0) {
		close(ready[0]);
		struct gpu gpu;
		if (gpu_open(&gpu) != 0)
			_exit(3);
		void* original;
		area_id area = create_buffer("dying", &original, SIZE);
		void* address;
		clone_area("dying clone", &address, B_ANY_ADDRESS,
			B_READ_AREA | B_WRITE_AREA, area);
		uint32_t handle = import(gpu.fd, address, SIZE, kLibdrmFlags);
		if (handle == 0 || map_gpu_va(gpu.fd, handle, USER_VA, SIZE) != 0)
			_exit(4);
		gpu_build_writes(&gpu, round << 16, 2);
		// keep a few submissions in flight
		uint64_t seqs[4] = {};
		for (int i = 0; ; i++) {
			if (seqs[i % 4] != 0)
				gpu_wait(&gpu, seqs[i % 4]);
			seqs[i % 4] = gpu_submit(&gpu, handle);
			if (seqs[i % 4] == 0)
				_exit(5);
			if (i == 8) {
				if (mode == 2)
					close_handle(gpu.fd, handle);
				write(ready[1], "", 1);
				if (mode == 0)
					_exit(0);
				if (mode == 2)
					for (;;)
						snooze(1000000);
			}
		}
	}
	close(ready[1]);
	char byte;
	if (read(ready[0], &byte, 1) != 1) {
		int status;
		waitpid(child, &status, 0);
		CHECK(0, "child (mode %d) failed before it was ready: %d", mode,
			WIFEXITED(status) ? WEXITSTATUS(status) : -1);
		close(ready[0]);
		return;
	}
	close(ready[0]);
	snooze((round * 7919) % 20 * 1000);
	if (mode != 0)
		kill(child, SIGKILL);
	int status;
	waitpid(child, &status, 0);
	if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
		CHECK(0, "child (mode %d) exited with %d", mode, WEXITSTATUS(status));
}


int
main(int argc, char** argv)
{
	int rounds = argc > 1 ? atoi(argv[1]) : 50;
	if (argc > 2)
		sDevice = argv[2];

	struct gpu gpu;
	if (gpu_open(&gpu) != 0) {
		printf("FAIL  cannot set up %s: %s\n", sDevice, strerror(errno));
		return 2;
	}
	test_clone(&gpu);
	test_refusals(&gpu);
	test_big(&gpu);

	bigtime_t start = system_time();
	for (int round = 0; round < rounds; round++)
		dying_child(round % 3, round);
	CHECK(1, "%d children died while the GPU wrote into their memory "
		"(%lld ms)", rounds, (long long)(system_time() - start) / 1000);

	// the GPU still works for this team's own import afterwards
	test_clone(&gpu);
	printf("%d failure(s)\n", sFailures);
	return sFailures != 0;
}
