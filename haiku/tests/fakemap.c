/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*	Mappings of buffers follow the buffer when it moves, tried on the fake
	DRM device of a virtual machine ("haiku_selftest 2"): its "move" ioctl
	gives a buffer new pages the way TTM moves one, and poisons the old
	pages, which a mapping that did not follow then reads. Covers a team's
	own mappings, a second mapping, a clone of the area, fork(), mappings
	the team deleted itself or left to close(), and moves racing with
	reads and writes from other threads.

	fakemap [seconds] [device]		default 10 s, /dev/dri/card0 */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <OS.h>


#define LX_IOC(dir, type, nr, size) \
	(((uint32_t)(dir) << 30) | ((uint32_t)(size) << 16) | ((type) << 8) | (nr))

struct drm_haiku_map {
	uint64_t offset;
	uint64_t size;
	uint32_t flags;
	int32_t area;
	uint64_t address;
};

struct drm_haiku_unmap {
	uint64_t address;
};

struct fake_create {
	uint64_t size;
	uint32_t handle;
	uint32_t pad;
	uint64_t offset;
};

struct fake_move {
	uint32_t handle;
	uint32_t pad;
};

#define IOCTL_MAP		LX_IOC(3, 'd', 0xf0, sizeof(struct drm_haiku_map))
#define IOCTL_UNMAP		LX_IOC(1, 'd', 0xf1, sizeof(struct drm_haiku_unmap))
#define IOCTL_CREATE	LX_IOC(3, 'd', 0x40, sizeof(struct fake_create))
#define IOCTL_MOVE		LX_IOC(1, 'd', 0x41, sizeof(struct fake_move))

#define PAGES	64
#define SIZE	(PAGES * 4096)
#define WORDS	(SIZE / 4)
#define POISON	0x5a5a5a5au


static int sFd;
static int sFailures;


#define FAIL(x...) \
	do { \
		printf("FAIL: " x); \
		printf("\n"); \
		sFailures++; \
	} while (0)


static uint32_t
pattern(uint32_t seed, uint32_t index)
{
	return index * 2654435761u ^ seed;
}


static void
fill(volatile uint32_t* words, uint32_t seed)
{
	for (uint32_t i = 0; i < WORDS; i++)
		words[i] = pattern(seed, i);
}


/* words that differ; *poisoned: of them, how many read the poison */
static uint32_t
check(const volatile uint32_t* words, uint32_t seed, uint32_t* poisoned)
{
	uint32_t wrong = 0, poison = 0;
	for (uint32_t i = 0; i < WORDS; i++) {
		uint32_t value = words[i];
		if (value != pattern(seed, i)) {
			wrong++;
			if (value == POISON)
				poison++;
		}
	}
	if (poisoned != NULL)
		*poisoned = poison;
	return wrong;
}


static void
expect(const char* what, const volatile uint32_t* words, uint32_t seed)
{
	uint32_t poisoned;
	uint32_t wrong = check(words, seed, &poisoned);
	if (wrong != 0)
		FAIL("%s: %u words wrong (%u poison)", what, wrong, poisoned);
	else
		printf("ok: %s\n", what);
}


static struct fake_create
create(void)
{
	struct fake_create args;
	memset(&args, 0, sizeof(args));
	args.size = SIZE;
	if (ioctl(sFd, IOCTL_CREATE, &args, sizeof(args)) != 0) {
		printf("create: %s\n", strerror(errno));
		exit(1);
	}
	return args;
}


static void
move(uint32_t handle)
{
	struct fake_move args;
	memset(&args, 0, sizeof(args));
	args.handle = handle;
	if (ioctl(sFd, IOCTL_MOVE, &args, sizeof(args)) != 0)
		FAIL("move: %s", strerror(errno));
}


static uint32_t*
map(uint64_t offset, uint32_t flags, area_id* area)
{
	struct drm_haiku_map args;
	memset(&args, 0, sizeof(args));
	args.offset = offset;
	args.size = SIZE;
	args.flags = flags;
	if (ioctl(sFd, IOCTL_MAP, &args, sizeof(args)) != 0) {
		printf("map: %s\n", strerror(errno));
		exit(1);
	}
	if (area != NULL)
		*area = args.area;
	return (uint32_t*)(uintptr_t)args.address;
}


static status_t
unmap(void* address)
{
	struct drm_haiku_unmap args;
	args.address = (uintptr_t)address;
	return ioctl(sFd, IOCTL_UNMAP, &args, sizeof(args)) == 0 ? B_OK : errno;
}


/* #pragma mark - moves racing with accesses */


static volatile int sStop;
static uint32_t sRaceHandle;
static uint64_t sRaceOffset;
static volatile uint32_t* sRaceWords;
static uint32_t sMoves, sRounds, sRemaps;


static void*
mover(void* unused)
{
	while (!sStop) {
		move(sRaceHandle);
		sMoves++;
	}
	return NULL;
}


/* writes every word and reads it back, then checks the whole buffer */
static void*
writer(void* unused)
{
	uint32_t seed = 1;
	while (!sStop) {
		seed++;
		for (uint32_t i = 0; i < WORDS; i++) {
			sRaceWords[i] = pattern(seed, i);
			if (sRaceWords[i] != pattern(seed, i)) {
				FAIL("race: word %u read back 0x%x", i, sRaceWords[i]);
				sStop = 1;
				return NULL;
			}
		}
		uint32_t poisoned, wrong = check(sRaceWords, seed, &poisoned);
		if (wrong != 0) {
			FAIL("race: %u words lost (%u poison) in round %u", wrong,
				poisoned, sRounds);
			sStop = 1;
			return NULL;
		}
		sRounds++;
	}
	return NULL;
}


/* maps the buffer again and again, and reads it */
static void*
remapper(void* unused)
{
	while (!sStop) {
		uint32_t* words = map(sRaceOffset, 0, NULL);
		uint32_t sum = 0;
		for (uint32_t i = 0; i < WORDS; i += 1024)
			sum += words[i];
		(void)sum;
		if (unmap(words) != B_OK)
			FAIL("race: unmap");
		sRemaps++;
	}
	return NULL;
}


int
main(int argc, char** argv)
{
	int seconds = argc > 1 ? atoi(argv[1]) : 10;
	const char* device = argc > 2 ? argv[2] : "/dev/dri/card0";
	sFd = open(device, O_RDWR);
	if (sFd < 0) {
		printf("%s: %s\n", device, strerror(errno));
		return 1;
	}

	// a team's mapping, after a move
	struct fake_create bo = create();
	area_id area;
	uint32_t* words = map(bo.offset, 3, &area);
	fill(words, 1);
	expect("mapped buffer", words, 1);
	move(bo.handle);
	expect("mapping after a move", words, 1);
	fill(words, 2);
	move(bo.handle);
	uint32_t* second = map(bo.offset, 0, NULL);
	expect("written after a move, read by a second mapping", second, 2);
	if (unmap(second) != B_OK)
		FAIL("unmap second mapping");

	// a clone of the area
	void* cloneAddress;
	area_id clone = clone_area("fakemap clone", &cloneAddress, B_ANY_ADDRESS,
		B_READ_AREA | B_WRITE_AREA, area);
	if (clone < 0)
		FAIL("clone_area: %s", strerror(clone));
	else {
		uint32_t* cloned = cloneAddress;
		expect("clone", cloned, 2);
		move(bo.handle);
		expect("clone after a move", cloned, 2);
		fill(cloned, 3);
		expect("written through the clone, read by the mapping", words, 3);
		delete_area(clone);
	}

	// fork(): the child shares the mapping
	fill(words, 3);
	pid_t child = fork();
	if (child == 0) {
		uint32_t wrong = check(words, 3, NULL);
		move(bo.handle);
		wrong += check(words, 3, NULL);
		fill(words, 4);
		_exit(wrong != 0 ? 1 : 0);
	}
	int status = -1;
	waitpid(child, &status, 0);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		FAIL("fork: the child read the wrong data (status 0x%x)", status);
	else
		printf("ok: fork child read the buffer\n");
	expect("written by the child", words, 4);
	move(bo.handle);
	expect("after the child, and a move", words, 4);

	// a mapping the team deletes itself, then unmaps
	uint32_t* third = map(bo.offset, 1, &area);
	third[0] = 0;
	delete_area(area);
	if (unmap(third) != B_OK)
		FAIL("unmap of a deleted area");
	else
		printf("ok: unmap of an area the team deleted\n");
	move(bo.handle);

	// moves racing with accesses
	struct fake_create race = create();
	sRaceHandle = race.handle;
	sRaceOffset = race.offset;
	sRaceWords = map(race.offset, 1, NULL);
	pthread_t threads[4];
	pthread_create(&threads[0], NULL, mover, NULL);
	pthread_create(&threads[1], NULL, writer, NULL);
	pthread_create(&threads[2], NULL, remapper, NULL);
	pthread_create(&threads[3], NULL, mover, NULL);
	for (int i = 0; i < seconds * 10 && !sStop; i++)
		usleep(100000);
	sStop = 1;
	for (int i = 0; i < 4; i++)
		pthread_join(threads[i], NULL);
	printf("race: %u moves, %u write rounds, %u mappings made\n", sMoves,
		sRounds, sRemaps);
	if (sRounds == 0 || sMoves == 0)
		FAIL("race: nothing happened");

	// left mapped: close() unmaps them
	printf("fakemap: %d failure(s)\n", sFailures);
	return sFailures != 0;
}
