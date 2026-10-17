/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*	The screen's frame buffer through Vulkan (VkImportScanoutMemoryHAIKU, as
	Summit's zink present asks for it): imports it on the first AMD GPU,
	prints its shape, copies its first rows into host memory with the GPU
	and prints a few pixels, then imports and frees it again and again to
	time that and to catch leaks. Nothing is written to the screen.

	vkscanout [imports]		default 100

	Loads the Vulkan loader at run time, so it needs no Vulkan library to
	link; cross-built with the air/OS SDK and Mesa's include/vulkan. */

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#define VK_STRUCTURE_TYPE_IMPORT_SCANOUT_MEMORY_HAIKU ((VkStructureType)1000399000)

typedef struct VkImportScanoutMemoryHAIKU {
	VkStructureType sType;
	void* pNext;
	uint32_t width;
	uint32_t height;
	uint32_t rowPitch;
	VkDeviceSize size;
} VkImportScanoutMemoryHAIKU;

#define FUNCTIONS(F) \
	F(EnumeratePhysicalDevices) F(GetPhysicalDeviceProperties) \
	F(GetPhysicalDeviceMemoryProperties) \
	F(GetPhysicalDeviceQueueFamilyProperties) F(CreateDevice) \
	F(GetDeviceQueue) F(AllocateMemory) F(FreeMemory) F(CreateBuffer) \
	F(DestroyBuffer) F(BindBufferMemory) F(GetBufferMemoryRequirements) \
	F(MapMemory) F(CreateCommandPool) F(AllocateCommandBuffers) \
	F(BeginCommandBuffer) F(CmdCopyBuffer) F(EndCommandBuffer) \
	F(QueueSubmit) F(QueueWaitIdle) F(DestroyCommandPool) F(DestroyDevice) \
	F(DestroyInstance)

#define DECLARE(name) static PFN_vk##name vk##name;
FUNCTIONS(DECLARE)

static VkDevice sDevice;
static VkPhysicalDeviceMemoryProperties sMemory;


static double
now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}


static uint32_t
memory_type(VkMemoryPropertyFlags flags, uint32_t allowed)
{
	for (uint32_t i = 0; i < sMemory.memoryTypeCount; i++) {
		if ((allowed & (1u << i)) != 0
			&& (sMemory.memoryTypes[i].propertyFlags & flags) == flags)
			return i;
	}
	return UINT32_MAX;
}


static VkResult
import_scanout(VkDeviceMemory* memory, VkImportScanoutMemoryHAIKU* scanout)
{
	memset(scanout, 0, sizeof(*scanout));
	scanout->sType = VK_STRUCTURE_TYPE_IMPORT_SCANOUT_MEMORY_HAIKU;
	VkMemoryAllocateInfo info = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.pNext = scanout,
		.allocationSize = 0,
		.memoryTypeIndex = memory_type(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, ~0u),
	};
	*memory = VK_NULL_HANDLE;
	return vkAllocateMemory(sDevice, &info, NULL, memory);
}


int
main(int argc, char** argv)
{
	int imports = argc > 1 ? atoi(argv[1]) : 100;
	void* loader = dlopen("libvulkan.so.1", RTLD_NOW);
	if (loader == NULL) {
		fprintf(stderr, "no Vulkan loader: %s\n", dlerror());
		return 1;
	}
	PFN_vkGetInstanceProcAddr getProc
		= (PFN_vkGetInstanceProcAddr)dlsym(loader, "vkGetInstanceProcAddr");
	PFN_vkCreateInstance createInstance
		= (PFN_vkCreateInstance)getProc(NULL, "vkCreateInstance");

	VkApplicationInfo app = {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = "vkscanout",
		.apiVersion = VK_API_VERSION_1_1,
	};
	VkInstanceCreateInfo instanceInfo = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &app,
	};
	VkInstance instance;
	if (createInstance(&instanceInfo, NULL, &instance) != VK_SUCCESS) {
		fprintf(stderr, "vkCreateInstance failed\n");
		return 1;
	}
#define LOAD(name) vk##name = (PFN_vk##name)getProc(instance, "vk" #name);
	FUNCTIONS(LOAD)

	VkPhysicalDevice devices[8];
	uint32_t count = 8;
	vkEnumeratePhysicalDevices(instance, &count, devices);
	VkPhysicalDevice physical = VK_NULL_HANDLE;
	VkPhysicalDeviceProperties properties;
	for (uint32_t i = 0; i < count; i++) {
		vkGetPhysicalDeviceProperties(devices[i], &properties);
		if (properties.vendorID == 0x1002) {
			physical = devices[i];
			break;
		}
	}
	if (physical == VK_NULL_HANDLE) {
		fprintf(stderr, "no AMD GPU\n");
		return 1;
	}
	printf("device: %s\n", properties.deviceName);
	vkGetPhysicalDeviceMemoryProperties(physical, &sMemory);

	VkQueueFamilyProperties families[16];
	uint32_t familyCount = 16;
	vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, families);
	uint32_t family = 0;
	while (family < familyCount
		&& (families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0)
		family++;
	float priority = 1.0f;
	VkDeviceQueueCreateInfo queueInfo = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.queueFamilyIndex = family,
		.queueCount = 1,
		.pQueuePriorities = &priority,
	};
	VkDeviceCreateInfo deviceInfo = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.queueCreateInfoCount = 1,
		.pQueueCreateInfos = &queueInfo,
	};
	if (vkCreateDevice(physical, &deviceInfo, NULL, &sDevice) != VK_SUCCESS) {
		fprintf(stderr, "vkCreateDevice failed\n");
		return 1;
	}
	VkQueue queue;
	vkGetDeviceQueue(sDevice, family, 0, &queue);

	VkDeviceMemory scanoutMemory;
	VkImportScanoutMemoryHAIKU scanout;
	VkResult result = import_scanout(&scanoutMemory, &scanout);
	if (result != VK_SUCCESS) {
		printf("FAIL: no frame buffer to import (%d)\n", result);
		return 1;
	}
	printf("frame buffer: %ux%u, %u bytes per row, %llu bytes\n",
		scanout.width, scanout.height, scanout.rowPitch,
		(unsigned long long)scanout.size);
	int failures = 0;
	if (scanout.width == 0 || scanout.height == 0
		|| scanout.rowPitch < scanout.width * 4
		|| scanout.size < (VkDeviceSize)scanout.rowPitch * scanout.height) {
		printf("FAIL: the shape does not fit the size\n");
		failures++;
	}

	// the first rows, copied by the GPU into host memory
	const uint32_t rows = 8;
	VkDeviceSize bytes = (VkDeviceSize)scanout.rowPitch * rows;
	VkBufferCreateInfo bufferInfo = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = scanout.size,
		.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
	};
	VkBuffer source, target;
	vkCreateBuffer(sDevice, &bufferInfo, NULL, &source);
	if (vkBindBufferMemory(sDevice, source, scanoutMemory, 0) != VK_SUCCESS) {
		printf("FAIL: cannot bind a buffer to it\n");
		return 1;
	}
	bufferInfo.size = bytes;
	bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	vkCreateBuffer(sDevice, &bufferInfo, NULL, &target);
	VkMemoryRequirements requirements;
	vkGetBufferMemoryRequirements(sDevice, target, &requirements);
	VkMemoryAllocateInfo hostInfo = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = requirements.size,
		.memoryTypeIndex = memory_type(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
			| VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, requirements.memoryTypeBits),
	};
	VkDeviceMemory hostMemory;
	vkAllocateMemory(sDevice, &hostInfo, NULL, &hostMemory);
	vkBindBufferMemory(sDevice, target, hostMemory, 0);

	VkCommandPoolCreateInfo poolInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.queueFamilyIndex = family,
	};
	VkCommandPool pool;
	vkCreateCommandPool(sDevice, &poolInfo, NULL, &pool);
	VkCommandBufferAllocateInfo commandInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};
	VkCommandBuffer commands;
	vkAllocateCommandBuffers(sDevice, &commandInfo, &commands);
	VkCommandBufferBeginInfo begin = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	};
	vkBeginCommandBuffer(commands, &begin);
	VkBufferCopy region = { 0, 0, bytes };
	vkCmdCopyBuffer(commands, source, target, 1, &region);
	vkEndCommandBuffer(commands);
	VkSubmitInfo submit = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &commands,
	};
	if (vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS
		|| vkQueueWaitIdle(queue) != VK_SUCCESS) {
		printf("FAIL: the copy did not run\n");
		failures++;
	}
	uint32_t* pixels;
	vkMapMemory(sDevice, hostMemory, 0, bytes, 0, (void**)&pixels);
	uint32_t nonzero = 0;
	for (uint32_t i = 0; i < bytes / 4; i++)
		nonzero += pixels[i] != 0;
	printf("first %u rows: %u of %llu pixels not zero; at 0,0: %08x %08x %08x "
		"%08x\n", rows, nonzero, (unsigned long long)(bytes / 4), pixels[0],
		pixels[1], pixels[2], pixels[3]);
	if (nonzero == 0) {
		printf("FAIL: the rows read back empty (not the screen?)\n");
		failures++;
	}
	vkDestroyBuffer(sDevice, source, NULL);
	vkFreeMemory(sDevice, scanoutMemory, NULL);

	// again and again: every import is a new handle and address range
	double start = now_ms();
	for (int i = 0; i < imports; i++) {
		VkDeviceMemory memory;
		VkImportScanoutMemoryHAIKU again;
		if (import_scanout(&memory, &again) != VK_SUCCESS
			|| again.rowPitch != scanout.rowPitch) {
			printf("FAIL: import %d\n", i);
			failures++;
			break;
		}
		vkFreeMemory(sDevice, memory, NULL);
	}
	printf("%d imports and frees: %.3f ms each\n", imports,
		(now_ms() - start) / (imports > 0 ? imports : 1));

	vkDestroyCommandPool(sDevice, pool, NULL);
	vkDestroyBuffer(sDevice, target, NULL);
	vkFreeMemory(sDevice, hostMemory, NULL);
	vkDestroyDevice(sDevice, NULL);
	vkDestroyInstance(instance, NULL);
	printf("%s\n", failures == 0 ? "PASS" : "FAIL");
	return failures != 0;
}
