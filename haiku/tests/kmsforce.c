/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*	Monitors coming and going without anybody at the machine: forces a
	connector disconnected or connected, or back to what the hardware says,
	as writing its status in sysfs does on Linux (DRM_HAIKU_IOCTL_FORCE_
	CONNECTOR). Without arguments, lists the connectors.

	kmsforce [CONNECTOR off|on|detect]		CONNECTOR: a name (DP-1) or an ID */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm/drm_haiku.h>


static const char*
connection_name(drmModeConnection connection)
{
	return connection == DRM_MODE_CONNECTED ? "connected"
		: connection == DRM_MODE_DISCONNECTED ? "disconnected" : "unknown";
}


int
main(int argc, char** argv)
{
	int fd = open("/dev/dri/card0", O_RDWR);
	if (fd < 0) {
		perror("/dev/dri/card0");
		return 1;
	}
	drmModeResPtr resources = drmModeGetResources(fd);
	if (resources == NULL) {
		fprintf(stderr, "no KMS resources\n");
		return 1;
	}
	uint32_t target = 0;
	for (int i = 0; i < resources->count_connectors; i++) {
		drmModeConnectorPtr connector = drmModeGetConnector(fd,
			resources->connectors[i]);
		if (connector == NULL)
			continue;
		char name[32];
		snprintf(name, sizeof(name), "%s-%u",
			drmModeGetConnectorTypeName(connector->connector_type),
			connector->connector_type_id);
		if (argc < 3) {
			printf("%u %s: %s, %d mode(s)\n", connector->connector_id, name,
				connection_name(connector->connection), connector->count_modes);
		} else if (strcmp(argv[1], name) == 0
			|| (uint32_t)strtoul(argv[1], NULL, 0) == connector->connector_id)
			target = connector->connector_id;
		drmModeFreeConnector(connector);
	}
	drmModeFreeResources(resources);
	if (argc < 3)
		return 0;
	if (target == 0) {
		fprintf(stderr, "no connector %s\n", argv[1]);
		return 1;
	}
	struct drm_haiku_force_connector force = { target, 0 };
	if (strcmp(argv[2], "off") == 0)
		force.force = 1;
	else if (strcmp(argv[2], "on") == 0)
		force.force = 2;
	else if (strcmp(argv[2], "detect") != 0) {
		fprintf(stderr, "off, on or detect\n");
		return 2;
	}
	if (drmIoctl(fd, DRM_HAIKU_IOCTL_FORCE_CONNECTOR, &force) != 0) {
		perror("DRM_HAIKU_IOCTL_FORCE_CONNECTOR");
		return 1;
	}
	drmModeConnectorPtr connector = drmModeGetConnector(fd, target);
	if (connector != NULL) {
		printf("%s %s: %s\n", argv[1], argv[2],
			connection_name(connector->connection));
		drmModeFreeConnector(connector);
	}
	close(fd);
	return 0;
}
