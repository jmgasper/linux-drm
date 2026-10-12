/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*	VA-API on amdgpu_drm: the driver libva loads and the decoding profiles
	it offers. */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <va/va.h>
#include <va/va_drm.h>
#include <va/va_str.h>

int
main(int argc, char** argv)
{
	const char* path = argc > 1 ? argv[1] : "/dev/dri/renderD128";
	int fd = open(path, O_RDWR);
	if (fd < 0) {
		perror(path);
		return 1;
	}
	VADisplay display = vaGetDisplayDRM(fd);
	int major, minor;
	VAStatus status = vaInitialize(display, &major, &minor);
	if (status != VA_STATUS_SUCCESS) {
		printf("vaInitialize: %s\n", vaErrorStr(status));
		return 1;
	}
	printf("VA-API %d.%d, %s\n", major, minor, vaQueryVendorString(display));
	int count = vaMaxNumProfiles(display);
	VAProfile* profiles = malloc(count * sizeof(VAProfile));
	vaQueryConfigProfiles(display, profiles, &count);
	for (int i = 0; i < count; i++) {
		int entryCount = vaMaxNumEntrypoints(display);
		VAEntrypoint* entries = malloc(entryCount * sizeof(VAEntrypoint));
		vaQueryConfigEntrypoints(display, profiles[i], entries, &entryCount);
		printf("  %-32s", vaProfileStr(profiles[i]));
		for (int e = 0; e < entryCount; e++)
			printf(" %s", vaEntrypointStr(entries[e]));
		printf("\n");
		free(entries);
	}
	free(profiles);
	vaTerminate(display);
	close(fd);
	return 0;
}
