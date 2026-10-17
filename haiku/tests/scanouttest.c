/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*	DRM_HAIKU_IOCTL_SCANOUT on the fake DRM device of a virtual machine
	("haiku_selftest 2"): a buffer published on the primary node is handed
	to a render node client with its shape; publishing from a render node,
	a shape larger than the buffer and getting what nobody published are
	refused; a client's handle outlives the publisher; closing the
	publisher's file withdraws the buffer.

	scanouttest [primary] [render]	default /dev/dri/card0, renderD128 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>


#define LX_IOC(dir, type, nr, size) \
	(((uint32_t)(dir) << 30) | ((uint32_t)(size) << 16) | ((type) << 8) | (nr))

struct fake_create {
	uint64_t size;
	uint32_t handle;
	uint32_t flags;
	uint64_t offset;
};

struct gem_close {
	uint32_t handle;
	uint32_t pad;
};

struct scanout {
	uint32_t op;
	uint32_t handle;
	uint32_t width;
	uint32_t height;
	uint32_t pitch;
	uint32_t format;
	uint32_t generation;
	uint32_t reserved;
	uint64_t size;
};

#define IOCTL_CREATE	LX_IOC(3, 'd', 0x40, sizeof(struct fake_create))
#define IOCTL_GEM_CLOSE	LX_IOC(1, 'd', 0x09, sizeof(struct gem_close))
#define IOCTL_SCANOUT	LX_IOC(3, 'd', 0xf8, sizeof(struct scanout))
#define XRGB8888		0x34325258

static int sFailures;


static void
check(int condition, const char* what)
{
	printf("%s: %s\n", condition ? "ok" : "FAILED", what);
	if (!condition)
		sFailures++;
}


static int
scanout(int fd, struct scanout* request)
{
	return ioctl(fd, IOCTL_SCANOUT, request, sizeof(*request)) == 0
		? 0 : errno;
}


int
main(int argc, char** argv)
{
	const char* primaryPath = argc > 1 ? argv[1] : "/dev/dri/card0";
	const char* renderPath = argc > 2 ? argv[2] : "/dev/dri/renderD128";
	int primary = open(primaryPath, O_RDWR);
	int render = open(renderPath, O_RDWR);
	if (primary < 0 || render < 0) {
		perror("open");
		return 1;
	}

	struct scanout request = {};
	request.op = 2;
	check(scanout(render, &request) == ENOENT, "nothing published yet");

	struct fake_create create = {};
	create.size = 8 << 20;
	if (ioctl(primary, IOCTL_CREATE, &create, sizeof(create)) != 0) {
		perror("fake create");
		return 1;
	}

	memset(&request, 0, sizeof(request));
	request.op = 1;
	request.handle = create.handle;
	request.width = 2048;
	request.height = 1100;
	request.pitch = 8192;
	request.format = XRGB8888;
	check(scanout(primary, &request) == EINVAL, "a shape larger than the buffer");
	request.height = 1024;
	check(scanout(render, &request) != 0, "publishing from a render node");
	check(scanout(primary, &request) == 0, "publishing on the primary node");
	uint32_t generation = request.generation;

	memset(&request, 0, sizeof(request));
	request.op = 2;
	check(scanout(render, &request) == 0 && request.handle != 0
		&& request.width == 2048 && request.height == 1024
		&& request.pitch == 8192 && request.format == XRGB8888
		&& request.size == (8 << 20) && request.generation == generation,
		"a render node client gets it with its shape");
	uint32_t clientHandle = request.handle;

	struct scanout again = {};
	again.op = 2;
	check(scanout(render, &again) == 0 && again.handle != clientHandle,
		"a second get gives a second handle");
	struct gem_close close_request = {};
	close_request.handle = again.handle;
	check(ioctl(render, IOCTL_GEM_CLOSE, &close_request, sizeof(close_request))
		== 0, "closing the second handle");

	close(primary);
	memset(&request, 0, sizeof(request));
	request.op = 2;
	check(scanout(render, &request) == ENOENT,
		"withdrawn when the publisher's file closes");

	close_request.handle = clientHandle;
	check(ioctl(render, IOCTL_GEM_CLOSE, &close_request, sizeof(close_request))
		== 0, "the client's handle outlived the publisher");

	close(render);
	printf("%d failure(s)\n", sFailures);
	return sFailures != 0;
}
