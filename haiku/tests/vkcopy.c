/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*	Copy bandwidth between video and system memory, per queue family:
	vkCmdCopyBuffer of 64 MiB from a device-local buffer into a host-visible
	one (cached, or coherent) and back, on every queue family that can
	transfer. Shows which engine (shaders or CP DMA on the graphics and
	compute queues, SDMA on a transfer queue - RADV_PERFTEST=transfer_queue
	on older GPUs) is the limit.

	vkcopy [MiB] */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <vulkan/vulkan.h>

#define CHECK(x, what) do { VkResult _r = (x); if (_r != VK_SUCCESS) { \
	fprintf(stderr, "FAIL %s: %d\n", what, _r); exit(1); } } while (0)


static VkDevice sDevice;
static VkPhysicalDeviceMemoryProperties sMemory;


static double
now_s(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec * 1e-9;
}


static int
find_memory(uint32_t bits, VkMemoryPropertyFlags want, VkMemoryPropertyFlags avoid)
{
	for (uint32_t i = 0; i < sMemory.memoryTypeCount; i++) {
		VkMemoryPropertyFlags flags = sMemory.memoryTypes[i].propertyFlags;
		if ((bits & (1u << i)) && (flags & want) == want && (flags & avoid) == 0)
			return i;
	}
	return -1;
}


static VkBuffer
make_buffer(VkDeviceSize size, VkMemoryPropertyFlags want,
	VkMemoryPropertyFlags avoid, VkDeviceMemory* memory)
{
	VkBufferCreateInfo info = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size, .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT
			| VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE };
	VkBuffer buffer;
	CHECK(vkCreateBuffer(sDevice, &info, NULL, &buffer), "buffer");
	VkMemoryRequirements req;
	vkGetBufferMemoryRequirements(sDevice, buffer, &req);
	int type = find_memory(req.memoryTypeBits, want, avoid);
	if (type < 0)
		return VK_NULL_HANDLE;
	VkMemoryAllocateInfo allocate = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = req.size, .memoryTypeIndex = type };
	CHECK(vkAllocateMemory(sDevice, &allocate, NULL, memory), "memory");
	vkBindBufferMemory(sDevice, buffer, *memory, 0);
	return buffer;
}


static double
time_copy(VkQueue queue, VkCommandPool pool, VkBuffer from, VkBuffer to,
	VkDeviceSize size)
{
	VkCommandBufferAllocateInfo allocate = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1 };
	VkCommandBuffer cb;
	CHECK(vkAllocateCommandBuffers(sDevice, &allocate, &cb), "command buffer");
	VkCommandBufferBeginInfo begin = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	vkBeginCommandBuffer(cb, &begin);
	VkBufferCopy region = { .size = size };
	vkCmdCopyBuffer(cb, from, to, 1, &region);
	vkEndCommandBuffer(cb);
	VkSubmitInfo submit = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1, .pCommandBuffers = &cb };
	double best = 1e9;
	for (int pass = 0; pass < 5; pass++) {
		double start = now_s();
		CHECK(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE), "submit");
		vkQueueWaitIdle(queue);
		double elapsed = now_s() - start;
		if (elapsed < best)
			best = elapsed;
	}
	vkFreeCommandBuffers(sDevice, pool, 1, &cb);
	return size / best / 1e9;
}


int
main(int argc, char** argv)
{
	VkDeviceSize size = (VkDeviceSize)(argc > 1 ? atoi(argv[1]) : 64) << 20;
	setvbuf(stdout, NULL, _IONBF, 0);
	VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.apiVersion = VK_API_VERSION_1_1 };
	VkInstanceCreateInfo instanceInfo = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &app };
	VkInstance instance;
	CHECK(vkCreateInstance(&instanceInfo, NULL, &instance), "instance");
	uint32_t count = 1;
	VkPhysicalDevice physical;
	vkEnumeratePhysicalDevices(instance, &count, &physical);
	if (count == 0)
		return 1;
	vkGetPhysicalDeviceMemoryProperties(physical, &sMemory);
	VkQueueFamilyProperties families[8];
	uint32_t familyCount = 8;
	vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, families);

	float priority = 1.0f;
	VkDeviceQueueCreateInfo queues[8];
	for (uint32_t i = 0; i < familyCount; i++) {
		queues[i] = (VkDeviceQueueCreateInfo) {
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.queueFamilyIndex = i, .queueCount = 1,
			.pQueuePriorities = &priority };
	}
	VkDeviceCreateInfo deviceInfo = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.queueCreateInfoCount = familyCount, .pQueueCreateInfos = queues };
	CHECK(vkCreateDevice(physical, &deviceInfo, NULL, &sDevice), "device");

	VkDeviceMemory localMemory, cachedMemory, coherentMemory;
	VkBuffer local = make_buffer(size, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0,
		&localMemory);
	VkBuffer cached = make_buffer(size, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
		| VK_MEMORY_PROPERTY_HOST_CACHED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		&cachedMemory);
	VkBuffer coherent = make_buffer(size, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
		| VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		| VK_MEMORY_PROPERTY_HOST_CACHED_BIT, &coherentMemory);

	/* vkcopy MiB down|up SECONDS: copy one way for that long (to watch the
	   link while it runs) */
	if (argc > 3) {
		int down = strcmp(argv[2], "down") == 0;
		double seconds = atof(argv[3]);
		VkCommandPoolCreateInfo poolInfo = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.queueFamilyIndex = 0 };
		VkCommandPool pool;
		CHECK(vkCreateCommandPool(sDevice, &poolInfo, NULL, &pool), "pool");
		VkQueue queue;
		vkGetDeviceQueue(sDevice, 0, 0, &queue);
		double start = now_s(), worst = 1e9, best = 0;
		while (now_s() - start < seconds) {
			double rate = down ? time_copy(queue, pool, local, cached, size)
				: time_copy(queue, pool, cached, local, size);
			if (rate < worst)
				worst = rate;
			if (rate > best)
				best = rate;
		}
		printf("%s for %.0f s: best %.2f GB/s, worst %.2f GB/s\n",
			down ? "video->cached" : "cached->video", seconds, best, worst);
		return 0;
	}
	printf("%llu MiB copies (GB/s, best of 5):\n", (unsigned long long)(size >> 20));
	for (uint32_t i = 0; i < familyCount; i++) {
		VkQueueFlags flags = families[i].queueFlags;
		if (!(flags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT
				| VK_QUEUE_TRANSFER_BIT)))
			continue;
		printf("family %u (%s%s%s):", i,
			flags & VK_QUEUE_GRAPHICS_BIT ? "graphics " : "",
			flags & VK_QUEUE_COMPUTE_BIT ? "compute " : "",
			flags & VK_QUEUE_TRANSFER_BIT ? "transfer" : "");
		VkCommandPoolCreateInfo poolInfo = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.queueFamilyIndex = i };
		VkCommandPool pool;
		CHECK(vkCreateCommandPool(sDevice, &poolInfo, NULL, &pool), "pool");
		VkQueue queue;
		vkGetDeviceQueue(sDevice, i, 0, &queue);
		printf(" video->cached %.2f", time_copy(queue, pool, local, cached, size));
		if (coherent != VK_NULL_HANDLE) {
			printf(", video->coherent %.2f",
				time_copy(queue, pool, local, coherent, size));
		}
		printf(", cached->video %.2f\n", time_copy(queue, pool, cached, local,
			size));
		vkDestroyCommandPool(sDevice, pool, NULL);
	}
	vkDeviceWaitIdle(sDevice);
	vkDestroyDevice(sDevice, NULL);
	vkDestroyInstance(instance, NULL);
	return 0;
}
