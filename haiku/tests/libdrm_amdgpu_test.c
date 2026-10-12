/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*	The amdgpu_drm driver through libdrm and libdrm_amdgpu, as Mesa uses
	them: device enumeration, node names, device and GPU information,
	buffers mapped with amdgpu_bo_cpu_map(), an error that must come back
	negative (libdrm on Haiku is built with positive POSIX errors), and a
	command submission whose WRITE_DATA packet the GPU executes.

	gcc -DB_USE_POSITIVE_POSIX_ERRORS -I<prefix>/develop/headers \
		-I<prefix>/develop/headers/libdrm libdrm_amdgpu_test.c \
		-L<prefix>/lib -ldrm_amdgpu -ldrm -lposix_error_mapper */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <xf86drm.h>
#include <amdgpu.h>
#include <amdgpu_drm.h>


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
	} while (0)


int
main(int argc, char** argv)
{
	drmDevicePtr devices[8];
	int count = drmGetDevices2(0, devices, 8);
	CHECK(count > 0, "drmGetDevices2: %d device(s)", count);
	const char* render = NULL;
	for (int i = 0; i < count; i++) {
		drmDevicePtr device = devices[i];
		printf("      %04x:%02x:%02x.%u %04x:%04x rev %02x, nodes:",
			device->businfo.pci->domain, device->businfo.pci->bus,
			device->businfo.pci->dev, device->businfo.pci->func,
			device->deviceinfo.pci->vendor_id,
			device->deviceinfo.pci->device_id,
			device->deviceinfo.pci->revision_id);
		for (int node = 0; node < DRM_NODE_MAX; node++) {
			if (device->available_nodes & (1 << node))
				printf(" %s", device->nodes[node]);
		}
		printf("\n");
		if (device->deviceinfo.pci->vendor_id == 0x1002
			&& (device->available_nodes & (1 << DRM_NODE_RENDER)))
			render = device->nodes[DRM_NODE_RENDER];
	}
	if (render == NULL) {
		printf("FAIL  no AMD render node\n");
		return 1;
	}

	int fd = open(render, O_RDWR | O_CLOEXEC);
	CHECK(fd >= 0, "open %s", render);
	if (fd < 0)
		return 1;

	char* primary = drmGetPrimaryDeviceNameFromFd(fd);
	char* renderName = drmGetRenderDeviceNameFromFd(fd);
	CHECK(primary != NULL && renderName != NULL
		&& strcmp(renderName, render) == 0,
		"names from the render fd: primary %s, render %s",
		primary ? primary : "(none)", renderName ? renderName : "(none)");
	CHECK(drmGetNodeTypeFromFd(fd) == DRM_NODE_RENDER, "node type render");
	drmDevicePtr fromFd = NULL;
	CHECK(drmGetDevice2(fd, 0, &fromFd) == 0 && fromFd != NULL
		&& fromFd->deviceinfo.pci->vendor_id == 0x1002, "drmGetDevice2(fd)");

	uint32_t major, minor;
	amdgpu_device_handle device;
	int r = amdgpu_device_initialize(fd, &major, &minor, &device);
	CHECK(r == 0, "amdgpu_device_initialize: DRM %u.%u", major, minor);
	if (r != 0)
		return 1;
	const char* marketing = amdgpu_get_marketing_name(device);
	struct amdgpu_gpu_info gpu;
	r = amdgpu_query_gpu_info(device, &gpu);
	CHECK(r == 0, "amdgpu_query_gpu_info: %s, family %u, chip external rev "
		"%u, %u CUs/SH x %u SH x %u SE", marketing ? marketing : "(no name)",
		gpu.family_id, gpu.chip_external_rev, gpu.cu_active_number /
			(gpu.num_shader_engines * gpu.num_shader_arrays_per_engine),
		gpu.num_shader_arrays_per_engine, gpu.num_shader_engines);

	// a negative error for an impossible buffer
	struct amdgpu_bo_alloc_request request;
	amdgpu_bo_handle huge;
	memset(&request, 0, sizeof(request));
	request.alloc_size = 1ULL << 50;
	request.phys_alignment = 4096;
	request.preferred_heap = AMDGPU_GEM_DOMAIN_VRAM;
	r = amdgpu_bo_alloc(device, &request, &huge);
	CHECK(r < 0, "an impossible buffer fails with a negative error: %d (%s)",
		r, strerror(-r));

	// buffers in VRAM and GTT, mapped
	const uint64_t size = 1 << 20;
	amdgpu_bo_handle vram, gtt, ib;
	request.alloc_size = size;
	request.flags = AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED;
	r = amdgpu_bo_alloc(device, &request, &vram);
	request.preferred_heap = AMDGPU_GEM_DOMAIN_GTT;
	request.flags = 0;
	r |= amdgpu_bo_alloc(device, &request, &gtt);
	request.alloc_size = 4096;
	r |= amdgpu_bo_alloc(device, &request, &ib);
	CHECK(r == 0, "amdgpu_bo_alloc: VRAM, GTT, IB");
	if (r != 0)
		return 1;
	uint32_t *vramMap, *gttMap, *ibMap;
	r = amdgpu_bo_cpu_map(vram, (void**)&vramMap);
	r |= amdgpu_bo_cpu_map(gtt, (void**)&gttMap);
	r |= amdgpu_bo_cpu_map(ib, (void**)&ibMap);
	CHECK(r == 0, "amdgpu_bo_cpu_map: %p %p %p", vramMap, gttMap, ibMap);
	if (r != 0)
		return 1;
	for (uint32_t i = 0; i < size / 4; i++) {
		vramMap[i] = 0x5a5a0000 + i;
		gttMap[i] = 0xa5a50000 + i;
	}
	int same = 1;
	for (uint32_t i = 0; i < size / 4; i++) {
		if (vramMap[i] != 0x5a5a0000 + i || gttMap[i] != 0xa5a50000 + i)
			same = 0;
	}
	CHECK(same, "CPU writes read back");

	// GPU addresses
	amdgpu_va_handle vaHandles[3];
	uint64_t addresses[3];
	amdgpu_bo_handle bos[3] = { vram, gtt, ib };
	r = 0;
	for (int i = 0; i < 3; i++) {
		uint64_t boSize = i == 2 ? 4096 : size;
		r |= amdgpu_va_range_alloc(device, amdgpu_gpu_va_range_general, boSize,
			4096, 0, &addresses[i], &vaHandles[i], 0);
		r |= amdgpu_bo_va_op(bos[i], 0, boSize, addresses[i], 0,
			AMDGPU_VA_OP_MAP);
	}
	CHECK(r == 0, "VA: VRAM %#llx, GTT %#llx, IB %#llx",
		(unsigned long long)addresses[0], (unsigned long long)addresses[1],
		(unsigned long long)addresses[2]);

	// a GFX submission: WRITE_DATA into both buffers
	amdgpu_context_handle context;
	r = amdgpu_cs_ctx_create(device, &context);
	CHECK(r == 0, "amdgpu_cs_ctx_create");
	const uint32_t magic = 0xbeef0000;
	uint32_t n = 0;
	for (int target = 0; target < 2; target++) {
		ibMap[n++] = 0xc0000000 | ((4 + 4 - 2) << 16) | (0x37 << 8);
		ibMap[n++] = (5 << 8) | (1 << 20);
		ibMap[n++] = (uint32_t)addresses[target];
		ibMap[n++] = (uint32_t)(addresses[target] >> 32);
		for (int i = 0; i < 4; i++)
			ibMap[n++] = magic + target * 16 + i;
	}
	while (n % 8 != 0)
		ibMap[n++] = 0xffff1000;

	amdgpu_bo_list_handle list;
	r = amdgpu_bo_list_create(device, 3, bos, NULL, &list);
	CHECK(r == 0, "amdgpu_bo_list_create");
	struct amdgpu_cs_ib_info ibInfo;
	memset(&ibInfo, 0, sizeof(ibInfo));
	ibInfo.ib_mc_address = addresses[2];
	ibInfo.size = n;
	struct amdgpu_cs_request cs;
	memset(&cs, 0, sizeof(cs));
	cs.ip_type = AMDGPU_HW_IP_GFX;
	cs.resources = list;
	cs.number_of_ibs = 1;
	cs.ibs = &ibInfo;
	r = amdgpu_cs_submit(context, 0, &cs, 1);
	CHECK(r == 0, "amdgpu_cs_submit: sequence %llu",
		(unsigned long long)cs.seq_no);

	struct amdgpu_cs_fence fence;
	memset(&fence, 0, sizeof(fence));
	fence.context = context;
	fence.ip_type = AMDGPU_HW_IP_GFX;
	fence.fence = cs.seq_no;
	uint32_t expired = 0;
	r = amdgpu_cs_query_fence_status(&fence, 2000000000ULL, 0, &expired);
	CHECK(r == 0 && expired, "amdgpu_cs_query_fence_status: %s",
		expired ? "signaled" : "not signaled");
	int written = 1;
	for (int i = 0; i < 4; i++) {
		if (vramMap[i] != magic + i || gttMap[i] != magic + 16 + i)
			written = 0;
	}
	CHECK(written && vramMap[4] == 0x5a5a0004 && gttMap[4] == 0xa5a50004,
		"GPU wrote VRAM %08x .. and GTT %08x .., nothing beyond", vramMap[0],
		gttMap[0]);

	// cleanup through the library
	amdgpu_bo_list_destroy(list);
	amdgpu_cs_ctx_free(context);
	for (int i = 0; i < 3; i++) {
		amdgpu_bo_va_op(bos[i], 0, i == 2 ? 4096 : size, addresses[i], 0,
			AMDGPU_VA_OP_UNMAP);
		amdgpu_va_range_free(vaHandles[i]);
		amdgpu_bo_cpu_unmap(bos[i]);
		amdgpu_bo_free(bos[i]);
	}
	r = amdgpu_device_deinitialize(device);
	CHECK(r == 0, "amdgpu_device_deinitialize");
	drmFreeDevice(&fromFd);
	drmFreeDevices(devices, count);
	free(primary);
	free(renderName);
	close(fd);
	printf("%d failure(s)\n", sFailures);
	return sFailures != 0;
}
