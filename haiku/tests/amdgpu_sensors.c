/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*	The power management state of an amdgpu_drm GPU: clocks, temperature,
	load and power, as AMDGPU_INFO_SENSOR reports them. */

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include <amdgpu.h>
#include <amdgpu_drm.h>

static void
sensor(amdgpu_device_handle device, const char* name, uint32_t type,
	const char* unit, double scale)
{
	uint32_t value = 0;
	int r = amdgpu_query_sensor_info(device, type, sizeof(value), &value);
	if (r == 0)
		printf("%-22s %10.2f %s\n", name, value * scale, unit);
	else
		printf("%-22s (error %d)\n", name, r);
}

int
main(void)
{
	int fd = open("/dev/dri/renderD128", O_RDWR);
	uint32_t major, minor;
	amdgpu_device_handle device;
	if (fd < 0 || amdgpu_device_initialize(fd, &major, &minor, &device) != 0)
		return 1;
	sensor(device, "shader clock", AMDGPU_INFO_SENSOR_GFX_SCLK, "MHz", 1);
	sensor(device, "memory clock", AMDGPU_INFO_SENSOR_GFX_MCLK, "MHz", 1);
	sensor(device, "temperature", AMDGPU_INFO_SENSOR_GPU_TEMP, "C", 0.001);
	sensor(device, "load", AMDGPU_INFO_SENSOR_GPU_LOAD, "%", 1);
	sensor(device, "power", AMDGPU_INFO_SENSOR_GPU_AVG_POWER, "W", 1);
	sensor(device, "voltage", AMDGPU_INFO_SENSOR_VDDGFX, "mV", 1);
	sensor(device, "stable shader clock", AMDGPU_INFO_SENSOR_STABLE_PSTATE_GFX_SCLK, "MHz", 1);
	sensor(device, "stable memory clock", AMDGPU_INFO_SENSOR_STABLE_PSTATE_GFX_MCLK, "MHz", 1);
	amdgpu_device_deinitialize(device);
	close(fd);
	return 0;
}
