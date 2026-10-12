/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*	Lab builds of amdgpu_drm: logs the CPU's bandwidth to VRAM through a
	kernel mapping (see lx_glue_lab_bench()), after forcing a DPM
	performance level when one is given (auto, low, high).

	lab_bench [auto|low|high] */

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

#define LAB_BENCH		((0u << 30) | ('d' << 8) | 0xfe)
#define LAB_PERF_LEVEL	((0u << 30) | ('d' << 8) | 0xfd)

int
main(int argc, char** argv)
{
	int fd = open("/dev/dri/renderD128", O_RDWR);
	if (fd < 0)
		return 1;
	if (argc > 1) {
		/* AMD_DPM_FORCED_LEVEL_AUTO, _LOW, _HIGH (kgd_pp_interface.h) */
		unsigned long level = strcmp(argv[1], "low") == 0 ? 0x4
			: strcmp(argv[1], "high") == 0 ? 0x8 : 0x1;
		if (ioctl(fd, LAB_PERF_LEVEL, (void*)level, 0) != 0)
			printf("performance level: %s\n", strerror(errno));
	}
	if (ioctl(fd, LAB_BENCH, NULL, 0) != 0) {
		printf("bench: %s\n", strerror(errno));
		return 1;
	}
	close(fd);
	return 0;
}
