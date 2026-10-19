/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*	Userptr buffers (haiku/amdgpu/userptr.c) on the fake DRM device of a
	virtual machine ("haiku_selftest 2"), whose "userptr" ioctl wires a
	team's pages as amdgpu's does, writes a pattern into them through their
	physical addresses and can keep writing behind a fence, like GPU work.
	Covers a clone of a B_NO_LOCK area (app_server's back buffer, as Summit
	imports it), refusals, the original area deleted while a clone is
	imported (a display layout change), deleting the imported area itself,
	fork() with an import, and teams that die (exit, kill -9) while the
	device writes into their memory, with the buffer still open or already
	closed. A kernel that gets any of it wrong panics on the wired area when
	the team's address space goes, or hangs.

	fakeuserptr [rounds] [device]	default 100 rounds, /dev/dri/renderD128 */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include <OS.h>


#define LX_IOC(dir, type, nr, size) \
	(((uint32_t)(dir) << 30) | ((uint32_t)(size) << 16) | ((type) << 8) | (nr))

struct fake_userptr {
	uint64_t address;
	uint64_t size;
	uint32_t flags;		/* 1: read only, 2: anonymous memory only */
	uint32_t handle;
	uint32_t busy_ms;
	uint32_t pattern;
};

struct gem_close {
	uint32_t handle;
	uint32_t pad;
};

#define IOCTL_USERPTR	LX_IOC(3, 'd', 0x42, sizeof(struct fake_userptr))
#define IOCTL_GEM_CLOSE	LX_IOC(1, 'd', 0x09, sizeof(struct gem_close))

#define PAGES	256
#define SIZE	(PAGES * B_PAGE_SIZE)
#define WORDS	(SIZE / 4)

static const char* sDevice = "/dev/dri/renderD128";
static int sFailures;


#define FAIL(x...) \
	do { \
		printf("FAIL: " x); \
		printf("\n"); \
		sFailures++; \
	} while (0)


static int
open_device(void)
{
	int fd = open(sDevice, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		printf("cannot open %s: %s\n", sDevice, strerror(errno));
		exit(2);
	}
	return fd;
}


/* 0 or errno */
static int
import(int fd, void* address, size_t size, uint32_t flags, uint32_t busyMs,
	uint32_t pattern, uint32_t* handle)
{
	struct fake_userptr args = {
		(uintptr_t)address, size, flags, 0, busyMs, pattern
	};
	if (ioctl(fd, IOCTL_USERPTR, &args, sizeof(args)) != 0)
		return errno;
	*handle = args.handle;
	return 0;
}


static void
close_handle(int fd, uint32_t handle)
{
	struct gem_close args = { handle, 0 };
	if (ioctl(fd, IOCTL_GEM_CLOSE, &args, sizeof(args)) != 0)
		FAIL("GEM_CLOSE %u: %s", handle, strerror(errno));
}


/* the number of words that are not pattern + i (word 0 of each page is the
   device's counter while it is busy, so skipped then) */
static int
check_pattern(const uint32_t* words, uint32_t pattern, int skipCounters)
{
	int bad = 0;
	for (int i = 0; i < WORDS; i++) {
		if (skipCounters && i % (B_PAGE_SIZE / 4) == 0)
			continue;
		if (words[i] != pattern + (uint32_t)i)
			bad++;
	}
	return bad;
}


static area_id
create_buffer(const char* name, void** address, uint32 protection)
{
	area_id area = create_area(name, address, B_ANY_ADDRESS, SIZE, B_NO_LOCK,
		protection | B_CLONEABLE_AREA);
	if (area < 0) {
		printf("create_area: %s\n", strerror(area));
		exit(2);
	}
	return area;
}


static void
test_clone(int fd)
{
	void* original;
	area_id area = create_buffer("back buffer", &original,
		B_READ_AREA | B_WRITE_AREA);
	void* address;
	area_id clone = clone_area("back buffer clone", &address, B_ANY_ADDRESS,
		B_READ_AREA | B_WRITE_AREA, area);
	if (clone < 0) {
		FAIL("clone_area: %s", strerror(clone));
		return;
	}

	uint32_t handle;
	int error = import(fd, address, SIZE, 2, 0, 0x10000000, &handle);
	if (error != 0)
		FAIL("import of a clone: %s", strerror(error));
	else {
		int bad = check_pattern((uint32_t*)original, 0x10000000, 0);
		if (bad != 0)
			FAIL("the device's writes through the clone: %d words wrong", bad);
		close_handle(fd, handle);
	}

	// the original
	error = import(fd, original, SIZE, 2, 0, 0x20000000, &handle);
	if (error != 0)
		FAIL("import of the original: %s", strerror(error));
	else {
		if (check_pattern((uint32_t*)address, 0x20000000, 0) != 0)
			FAIL("the device's writes into the original");
		close_handle(fd, handle);
	}

	// part of it, at an offset
	error = import(fd, (char*)address + 16 * B_PAGE_SIZE, 8 * B_PAGE_SIZE, 2, 0,
		0x30000000, &handle);
	if (error != 0)
		FAIL("import of a part: %s", strerror(error));
	else {
		uint32_t* words = (uint32_t*)original + 16 * B_PAGE_SIZE / 4;
		if (words[0] != 0x30000000 || words[8 * B_PAGE_SIZE / 4 - 1]
				!= 0x30000000 + 8 * B_PAGE_SIZE / 4 - 1)
			FAIL("the device's writes into a part");
		close_handle(fd, handle);
	}

	// the original goes, as app_server's back buffer does on a layout
	// change, while the clone is imported and the device writes
	error = import(fd, address, SIZE, 2, 300, 0x40000000, &handle);
	if (error != 0)
		FAIL("import for the layout change: %s", strerror(error));
	else {
		delete_area(area);
		area = -1;
		snooze(100000);
		uint32_t before = ((uint32_t*)address)[0];
		snooze(50000);
		if (((uint32_t*)address)[0] == before)
			FAIL("the device does not write while busy");
		if (check_pattern((uint32_t*)address, 0x40000000, 1) != 0)
			FAIL("the clone after its original was deleted");
		close_handle(fd, handle);
	}

	if (area >= 0)
		delete_area(area);
	delete_area(clone);
}


struct closer {
	int fd;
	uint32_t handle;
};


static void*
close_later(void* data)
{
	struct closer* closer = data;
	snooze(500000);
	close_handle(closer->fd, closer->handle);
	return NULL;
}


/*	The team deletes the area it imported: the deletion waits for the
	import to go (the pages are wired). */
static void
test_delete_imported(int fd)
{
	void* address;
	area_id area = create_buffer("imported", &address,
		B_READ_AREA | B_WRITE_AREA);
	struct closer closer = { fd, 0 };
	int error = import(fd, address, SIZE, 2, 0, 0x50000000, &closer.handle);
	if (error != 0) {
		FAIL("import to delete: %s", strerror(error));
		delete_area(area);
		return;
	}
	pthread_t thread;
	pthread_create(&thread, NULL, close_later, &closer);
	bigtime_t start = system_time();
	status_t status = delete_area(area);
	bigtime_t waited = system_time() - start;
	pthread_join(thread, NULL);
	if (status != B_OK)
		FAIL("delete_area of an imported area: %s", strerror(status));
	if (waited < 400000)
		FAIL("delete_area of an imported area returned after %lld us",
			(long long)waited);
	printf("delete_area of an imported area waited %lld ms\n",
		(long long)waited / 1000);
}


static void
test_refusals(int fd)
{
	uint32_t handle;
	void* address;

	// writable import of read-only memory
	area_id area = create_buffer("read only", &address, B_READ_AREA);
	int error = import(fd, address, SIZE, 0, 0, 0, &handle);
	if (error == 0) {
		FAIL("writable import of read-only memory");
		close_handle(fd, handle);
	}
	error = import(fd, address, SIZE, 1, 0, 0, &handle);
	if (error != 0)
		FAIL("read-only import of read-only memory: %s", strerror(error));
	else
		close_handle(fd, handle);
	delete_area(area);

	// a mapped file
	int file = open("/boot/system/lib/libroot.so", O_RDONLY);
	void* mapped = mmap(NULL, 4 * B_PAGE_SIZE, PROT_READ, MAP_PRIVATE, file, 0);
	if (mapped == MAP_FAILED)
		FAIL("mmap: %s", strerror(errno));
	else {
		error = import(fd, mapped, 4 * B_PAGE_SIZE, 1 | 2, 0, 0, &handle);
		if (error != EPERM) {
			FAIL("anonymous-only import of a file: %s", strerror(error));
			if (error == 0)
				close_handle(fd, handle);
		}
		error = import(fd, mapped, 4 * B_PAGE_SIZE, 1, 0, 0, &handle);
		if (error != 0)
			FAIL("read-only import of a file: %s", strerror(error));
		else
			close_handle(fd, handle);
		munmap(mapped, 4 * B_PAGE_SIZE);
	}
	close(file);

	// not user memory, not mapped, not aligned
	if (import(fd, (void*)0xffffffff80000000ULL, SIZE, 1, 0, 0, &handle) == 0)
		FAIL("import of kernel memory");
	if (import(fd, (void*)0x1000, SIZE, 2, 0, 0, &handle) == 0)
		FAIL("import of unmapped memory");
	area = create_buffer("unaligned", &address, B_READ_AREA | B_WRITE_AREA);
	if (import(fd, (char*)address + 8, B_PAGE_SIZE, 2, 0, 0, &handle) == 0)
		FAIL("import at an unaligned address");
	// beyond the area's end
	if (import(fd, address, SIZE + B_PAGE_SIZE, 2, 0, 0, &handle) == 0)
		FAIL("import beyond the area");
	delete_area(area);
}


/* fork() copies the area on write; the parent's wired pages stay its own */
static void
test_fork(int fd)
{
	void* address;
	area_id area = create_area("private", &address, B_ANY_ADDRESS, SIZE,
		B_NO_LOCK, B_READ_AREA | B_WRITE_AREA);
	memset(address, 0, SIZE);
	uint32_t handle;
	int error = import(fd, address, SIZE, 2, 300, 0x60000000, &handle);
	if (error != 0) {
		FAIL("import before fork: %s", strerror(error));
		delete_area(area);
		return;
	}
	fflush(stdout);
	pid_t child = fork();
	if (child == 0) {
		memset(address, 0xee, SIZE);
		_exit(0);
	}
	int status;
	waitpid(child, &status, 0);
	snooze(50000);
	uint32_t before = ((uint32_t*)address)[0];
	snooze(50000);
	if (((uint32_t*)address)[0] == before)
		FAIL("the parent does not see the device's writes after fork()");
	if (check_pattern((uint32_t*)address, 0x60000000, 1) != 0)
		FAIL("the parent's pages after fork()");
	close_handle(fd, handle);
	delete_area(area);
}


/*	A child that imports memory the device keeps writing and then dies in
	one of several ways; \a mode: 0 exit, 1 kill -9, 2 close the buffer and
	kill -9, 3 kill -9 at once (maybe still in the ioctl). */
static void
dying_child(int mode, int round)
{
	int ready[2];
	pipe(ready);
	fflush(stdout);
	pid_t child = fork();
	if (child == 0) {
		close(ready[0]);
		int fd = open_device();
		void* original;
		area_id area = create_buffer("dying", &original,
			B_READ_AREA | B_WRITE_AREA);
		void* address;
		clone_area("dying clone", &address, B_ANY_ADDRESS,
			B_READ_AREA | B_WRITE_AREA, area);
		uint32_t handle = 0;
		int error = import(fd, address, SIZE, 2, 200 + round % 7 * 30,
			0x70000000, &handle);
		if (error != 0) {
			printf("child: import: %s\n", strerror(error));
			_exit(3);
		}
		if (mode == 2)
			close_handle(fd, handle);
		write(ready[1], "", 1);
		if (mode == 0)
			_exit(0);
		for (;;)
			snooze(1000000);
	}
	close(ready[1]);
	if (mode != 3) {
		char byte;
		read(ready[0], &byte, 1);
		snooze((round * 7919) % 100 * 1000);
	} else
		snooze((round * 7919) % 20 * 100);
	if (mode != 0)
		kill(child, SIGKILL);
	close(ready[0]);
	int status;
	if (waitpid(child, &status, 0) != child)
		FAIL("waitpid: %s", strerror(errno));
	else if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
		FAIL("child (mode %d) exited with %d", mode, WEXITSTATUS(status));
}


int
main(int argc, char** argv)
{
	int rounds = argc > 1 ? atoi(argv[1]) : 100;
	if (argc > 2)
		sDevice = argv[2];

	int fd = open_device();
	test_clone(fd);
	test_delete_imported(fd);
	test_refusals(fd);
	test_fork(fd);
	printf("basic tests: %d failure(s)\n", sFailures);

	bigtime_t start = system_time();
	for (int round = 0; round < rounds; round++) {
		dying_child(round % 4, round);
		if ((round + 1) % 50 == 0)
			printf("%d children died, %d failure(s)\n", round + 1, sFailures);
	}
	printf("%d rounds in %lld ms\n", rounds,
		(long long)(system_time() - start) / 1000);

	// everything was let go: an import of the same memory still works
	test_clone(fd);
	close(fd);
	printf("%s: %d failure(s)\n", sFailures == 0 ? "PASS" : "FAIL", sFailures);
	return sFailures == 0 ? 0 : 1;
}
