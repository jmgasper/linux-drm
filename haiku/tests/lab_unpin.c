/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*	Lab builds of amdgpu_drm keep /dev/dri/card0 open themselves; this asks
	the driver to close it, so that devfs can unload the driver (which a
	touch of its binary then does). */

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#define LAB_UNPIN	((0u << 30) | ('d' << 8) | 0xff)

int
main(void)
{
	int fd = open("/dev/dri/card0", O_RDWR);
	if (fd < 0) {
		printf("open /dev/dri/card0: %s\n", strerror(errno));
		return 1;
	}
	int result = ioctl(fd, LAB_UNPIN, NULL, 0);
	close(fd);
	if (result != 0) {
		printf("unpin: %s\n", strerror(errno));
		return 1;
	}
	printf("unpinned\n");
	return 0;
}
