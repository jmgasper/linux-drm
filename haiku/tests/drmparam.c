/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*	Sets a module parameter of the running amdgpu_drm (DRM_HAIKU_IOCTL_SET_
	PARAM), as writing /sys/module/<module>/parameters/<name> does on Linux:
	"drmparam debug 0x16" logs what atomic commits are refused for, "drmparam
	debug 0" stops. The driver logs the change in the syslog.

	drmparam NAME VALUE */

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <xf86drm.h>
#include <drm/drm_haiku.h>


int
main(int argc, char** argv)
{
	if (argc != 3) {
		fprintf(stderr, "usage: drmparam NAME VALUE\n");
		return 2;
	}
	struct drm_haiku_set_param param;
	memset(&param, 0, sizeof(param));
	strncpy(param.name, argv[1], sizeof(param.name) - 1);
	strncpy(param.value, argv[2], sizeof(param.value) - 1);
	int fd = open("/dev/dri/card0", O_RDWR);
	if (fd < 0) {
		perror("/dev/dri/card0");
		return 1;
	}
	if (drmIoctl(fd, DRM_HAIKU_IOCTL_SET_PARAM, &param) != 0) {
		perror(argv[1]);
		return 1;
	}
	close(fd);
	return 0;
}
