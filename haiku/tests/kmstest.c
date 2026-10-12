/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*	The KMS side of amdgpu_drm (Display Core) through libdrm.

	kmstest list                    connectors, modes, CRTCs, planes
	kmstest vblank [crtc index]     vblank counter and timing
	kmstest set <connector id> [WxH] [seconds]
	                                shows a test pattern on one display (the
	                                display is dark afterwards, until the
	                                next mode is set) */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <OS.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>


static const char*
connector_type(uint32_t type)
{
	switch (type) {
		case DRM_MODE_CONNECTOR_DisplayPort: return "DP";
		case DRM_MODE_CONNECTOR_HDMIA: return "HDMI-A";
		case DRM_MODE_CONNECTOR_DVID: return "DVI-D";
		case DRM_MODE_CONNECTOR_DVII: return "DVI-I";
		case DRM_MODE_CONNECTOR_VGA: return "VGA";
		case DRM_MODE_CONNECTOR_eDP: return "eDP";
		default: return "?";
	}
}


static int
list(int fd)
{
	drmModeRes* resources = drmModeGetResources(fd);
	if (resources == NULL) {
		printf("drmModeGetResources: %s\n", strerror(errno));
		return 1;
	}
	printf("%d connector(s), %d encoder(s), %d CRTC(s), %d framebuffer(s); "
		"%ux%u to %ux%u\n", resources->count_connectors,
		resources->count_encoders, resources->count_crtcs, resources->count_fbs,
		resources->min_width, resources->min_height, resources->max_width,
		resources->max_height);
	for (int i = 0; i < resources->count_connectors; i++) {
		drmModeConnector* connector = drmModeGetConnector(fd,
			resources->connectors[i]);
		if (connector == NULL)
			continue;
		printf("connector %u: %s-%u, %s, %ux%u mm, encoder %u, %d mode(s)\n",
			connector->connector_id, connector_type(connector->connector_type),
			connector->connector_type_id,
			connector->connection == DRM_MODE_CONNECTED ? "connected"
				: connector->connection == DRM_MODE_DISCONNECTED
					? "disconnected" : "unknown",
			connector->mmWidth, connector->mmHeight, connector->encoder_id,
			connector->count_modes);
		for (int m = 0; m < connector->count_modes && m < 6; m++) {
			drmModeModeInfo* mode = &connector->modes[m];
			printf("    %s %ux%u@%u%s, clock %u kHz\n", mode->name,
				mode->hdisplay, mode->vdisplay, mode->vrefresh,
				(mode->type & DRM_MODE_TYPE_PREFERRED) ? " (preferred)" : "",
				mode->clock);
		}
		for (int p = 0; p < connector->count_props; p++) {
			drmModePropertyRes* property = drmModeGetProperty(fd,
				connector->props[p]);
			if (property == NULL)
				continue;
			if (strcmp(property->name, "EDID") == 0) {
				drmModePropertyBlobRes* blob = drmModeGetPropertyBlob(fd,
					connector->prop_values[p]);
				printf("    EDID: %u bytes\n", blob != NULL ? blob->length : 0);
				drmModeFreePropertyBlob(blob);
			}
			drmModeFreeProperty(property);
		}
		drmModeFreeConnector(connector);
	}
	for (int i = 0; i < resources->count_crtcs; i++) {
		drmModeCrtc* crtc = drmModeGetCrtc(fd, resources->crtcs[i]);
		if (crtc == NULL)
			continue;
		printf("CRTC %u: %s, framebuffer %u, %ux%u at %u,%u\n", crtc->crtc_id,
			crtc->mode_valid ? crtc->mode.name : "(off)", crtc->buffer_id,
			crtc->width, crtc->height, crtc->x, crtc->y);
		drmModeFreeCrtc(crtc);
	}
	drmModePlaneRes* planes = drmModeGetPlaneResources(fd);
	if (planes != NULL) {
		printf("%u plane(s):", planes->count_planes);
		for (uint32_t i = 0; i < planes->count_planes; i++)
			printf(" %u", planes->planes[i]);
		printf("\n");
		drmModeFreePlaneResources(planes);
	}
	drmModeFreeResources(resources);
	return 0;
}


static int
vblank(int fd, int crtcIndex)
{
	drmVBlank request;
	memset(&request, 0, sizeof(request));
	request.request.type = DRM_VBLANK_RELATIVE
		| (crtcIndex << DRM_VBLANK_HIGH_CRTC_SHIFT);
	request.request.sequence = 0;
	if (drmWaitVBlank(fd, &request) != 0) {
		printf("drmWaitVBlank: %s\n", strerror(errno));
		return 1;
	}
	uint32_t first = request.reply.sequence;
	bigtime_t start = system_time();
	for (int i = 0; i < 60; i++) {
		memset(&request, 0, sizeof(request));
		request.request.type = DRM_VBLANK_RELATIVE
			| (crtcIndex << DRM_VBLANK_HIGH_CRTC_SHIFT);
		request.request.sequence = 1;
		if (drmWaitVBlank(fd, &request) != 0) {
			printf("drmWaitVBlank: %s\n", strerror(errno));
			return 1;
		}
	}
	bigtime_t elapsed = system_time() - start;
	printf("CRTC index %d: vblank %u -> %u in %.1f ms (%.2f Hz)\n", crtcIndex,
		first, request.reply.sequence, elapsed / 1000.0,
		(request.reply.sequence - first) * 1e6 / elapsed);
	return 0;
}


static int
set(int fd, uint32_t connectorId, int width, int height, int seconds)
{
	drmModeRes* resources = drmModeGetResources(fd);
	drmModeConnector* connector = drmModeGetConnector(fd, connectorId);
	if (resources == NULL || connector == NULL) {
		printf("no connector %u\n", connectorId);
		return 1;
	}
	drmModeModeInfo* mode = NULL;
	for (int m = 0; m < connector->count_modes; m++) {
		drmModeModeInfo* candidate = &connector->modes[m];
		if ((width == 0 && (candidate->type & DRM_MODE_TYPE_PREFERRED))
			|| (candidate->hdisplay == width && candidate->vdisplay == height)) {
			mode = candidate;
			break;
		}
	}
	if (mode == NULL && connector->count_modes > 0)
		mode = &connector->modes[0];
	if (mode == NULL) {
		printf("no mode\n");
		return 1;
	}
	// a CRTC the connector's encoders can use
	uint32_t crtcId = 0;
	for (int e = 0; e < connector->count_encoders && crtcId == 0; e++) {
		drmModeEncoder* encoder = drmModeGetEncoder(fd, connector->encoders[e]);
		if (encoder == NULL)
			continue;
		for (int c = 0; c < resources->count_crtcs; c++) {
			if (encoder->possible_crtcs & (1 << c)) {
				crtcId = resources->crtcs[c];
				break;
			}
		}
		drmModeFreeEncoder(encoder);
	}
	struct drm_mode_create_dumb create;
	memset(&create, 0, sizeof(create));
	create.width = mode->hdisplay;
	create.height = mode->vdisplay;
	create.bpp = 32;
	if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) != 0) {
		printf("CREATE_DUMB: %s\n", strerror(errno));
		return 1;
	}
	uint32_t fb = 0;
	uint32_t handles[4] = { create.handle }, pitches[4] = { create.pitch },
		offsets[4] = { 0 };
	if (drmModeAddFB2(fd, mode->hdisplay, mode->vdisplay, DRM_FORMAT_XRGB8888,
			handles, pitches, offsets, &fb, 0) != 0) {
		printf("AddFB2: %s\n", strerror(errno));
		return 1;
	}
	struct drm_mode_map_dumb map;
	memset(&map, 0, sizeof(map));
	map.handle = create.handle;
	if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map) != 0) {
		printf("MAP_DUMB: %s\n", strerror(errno));
		return 1;
	}
	uint32_t* pixels = drmHaikuMmap(NULL, create.size, PROT_READ | PROT_WRITE,
		MAP_SHARED, fd, map.offset);
	if (pixels == MAP_FAILED) {
		printf("map: %s\n", strerror(errno));
		return 1;
	}
	// colour bars with a white frame
	static const uint32_t kBars[] = { 0xffffff, 0xffff00, 0x00ffff, 0x00ff00,
		0xff00ff, 0xff0000, 0x0000ff, 0x000000 };
	for (uint32_t y = 0; y < mode->vdisplay; y++) {
		uint32_t* row = pixels + y * (create.pitch / 4);
		for (uint32_t x = 0; x < mode->hdisplay; x++) {
			bool frame = x < 8 || y < 8 || x >= mode->hdisplay - 8
				|| y >= mode->vdisplay - 8;
			row[x] = frame ? 0xffffff : kBars[x * 8 / mode->hdisplay];
		}
	}
	bigtime_t start = system_time();
	int r = drmModeSetCrtc(fd, crtcId, fb, 0, 0, &connectorId, 1, mode);
	printf("SetCrtc %u: connector %u, %s, framebuffer %u (pitch %u): %s "
		"(%.1f ms)\n", crtcId, connectorId, mode->name, fb, create.pitch,
		r == 0 ? "ok" : strerror(errno), (system_time() - start) / 1000.0);
	if (r == 0) {
		drmModeCrtc* crtc = drmModeGetCrtc(fd, crtcId);
		if (crtc != NULL) {
			printf("CRTC %u now: %s, framebuffer %u\n", crtcId,
				crtc->mode_valid ? crtc->mode.name : "(off)", crtc->buffer_id);
			drmModeFreeCrtc(crtc);
		}
		for (int c = 0; c < resources->count_crtcs; c++) {
			if (resources->crtcs[c] == crtcId)
				vblank(fd, c);
		}
		sleep(seconds);
	}
	drmHaikuMunmap(pixels, create.size);
	drmModeRmFB(fd, fb);
	return r != 0;
}


int
main(int argc, char** argv)
{
	int fd = open("/dev/dri/card0", O_RDWR);
	if (fd < 0) {
		printf("open: %s\n", strerror(errno));
		return 1;
	}
	drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
	if (argc < 2 || strcmp(argv[1], "list") == 0)
		return list(fd);
	if (strcmp(argv[1], "vblank") == 0)
		return vblank(fd, argc > 2 ? atoi(argv[2]) : 0);
	if (strcmp(argv[1], "set") == 0 && argc > 2) {
		int width = 0, height = 0;
		if (argc > 3)
			sscanf(argv[3], "%dx%d", &width, &height);
		return set(fd, atoi(argv[2]), width, height, argc > 4 ? atoi(argv[4]) : 5);
	}
	printf("usage: kmstest list | vblank [crtc index] | set <connector> "
		"[WxH] [seconds]\n");
	return 1;
}
