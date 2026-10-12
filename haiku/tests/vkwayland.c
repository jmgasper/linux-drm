/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*	Vulkan presenting in a window: an xdg-shell toplevel of Haiku's
	in-process Wayland server (a native window), a VK_KHR_wayland_surface
	swapchain on it, frames cleared to changing colours and presented.
	Prints the presentation rate.

	vkwayland [frames] [width height] [mailbox|fifo|immediate]

	Built with xdg-shell-client-protocol.h/.c from wayland-scanner. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"

#define VK_USE_PLATFORM_WAYLAND_KHR
#include <vulkan/vulkan.h>

#define CHECK(x, what) do { VkResult _r = (x); if (_r != VK_SUCCESS) { \
	fprintf(stderr, "FAIL %s: %d\n", what, _r); exit(1); } } while (0)


static struct wl_compositor* sCompositor;
static struct xdg_wm_base* sWmBase;
static int sConfigured;
static int sClosed;


static double
now_s(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec * 1e-9;
}


static void
wm_base_ping(void* data, struct xdg_wm_base* base, uint32_t serial)
{
	xdg_wm_base_pong(base, serial);
}

static const struct xdg_wm_base_listener kWmBaseListener = { wm_base_ping };


static void
registry_global(void* data, struct wl_registry* registry, uint32_t name,
	const char* interface, uint32_t version)
{
	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		sCompositor = wl_registry_bind(registry, name,
			&wl_compositor_interface, 4);
	} else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
		sWmBase = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(sWmBase, &kWmBaseListener, NULL);
	}
}

static void
registry_remove(void* data, struct wl_registry* registry, uint32_t name)
{
}

static const struct wl_registry_listener kRegistryListener
	= { registry_global, registry_remove };


static void
surface_configure(void* data, struct xdg_surface* surface, uint32_t serial)
{
	xdg_surface_ack_configure(surface, serial);
	sConfigured = 1;
}

static const struct xdg_surface_listener kSurfaceListener
	= { surface_configure };


static void
toplevel_configure(void* data, struct xdg_toplevel* toplevel, int32_t width,
	int32_t height, struct wl_array* states)
{
}

static void
toplevel_close(void* data, struct xdg_toplevel* toplevel)
{
	sClosed = 1;
}

static const struct xdg_toplevel_listener kToplevelListener
	= { toplevel_configure, toplevel_close };


int
main(int argc, char** argv)
{
	int frames = argc > 1 ? atoi(argv[1]) : 300;
	uint32_t width = argc > 3 ? atoi(argv[2]) : 640;
	uint32_t height = argc > 3 ? atoi(argv[3]) : 480;
	VkPresentModeKHR wantMode = VK_PRESENT_MODE_FIFO_KHR;
	if (argc > 4 && strcmp(argv[4], "mailbox") == 0)
		wantMode = VK_PRESENT_MODE_MAILBOX_KHR;
	else if (argc > 4 && strcmp(argv[4], "immediate") == 0)
		wantMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
	setvbuf(stdout, NULL, _IONBF, 0);

	struct wl_display* display = wl_display_connect(NULL);
	if (display == NULL) {
		fprintf(stderr, "FAIL no Wayland display\n");
		return 1;
	}
	struct wl_registry* registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &kRegistryListener, NULL);
	wl_display_roundtrip(display);
	if (sCompositor == NULL || sWmBase == NULL) {
		fprintf(stderr, "FAIL compositor %p, xdg_wm_base %p\n", sCompositor,
			sWmBase);
		return 1;
	}
	struct wl_surface* surface = wl_compositor_create_surface(sCompositor);
	struct xdg_surface* xdgSurface = xdg_wm_base_get_xdg_surface(sWmBase,
		surface);
	xdg_surface_add_listener(xdgSurface, &kSurfaceListener, NULL);
	struct xdg_toplevel* toplevel = xdg_surface_get_toplevel(xdgSurface);
	xdg_toplevel_add_listener(toplevel, &kToplevelListener, NULL);
	xdg_toplevel_set_title(toplevel, "vkwayland (RADV)");
	wl_surface_commit(surface);
	while (!sConfigured && wl_display_dispatch(display) >= 0)
		;
	printf("window configured\n");

	const char* instanceExtensions[] = { VK_KHR_SURFACE_EXTENSION_NAME,
		VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME };
	VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.apiVersion = VK_API_VERSION_1_1 };
	VkInstanceCreateInfo instanceInfo = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &app, .enabledExtensionCount = 2,
		.ppEnabledExtensionNames = instanceExtensions };
	VkInstance instance;
	CHECK(vkCreateInstance(&instanceInfo, NULL, &instance), "vkCreateInstance");

	VkWaylandSurfaceCreateInfoKHR surfaceInfo = {
		.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
		.display = display, .surface = surface };
	VkSurfaceKHR vkSurface;
	CHECK(vkCreateWaylandSurfaceKHR(instance, &surfaceInfo, NULL, &vkSurface),
		"vkCreateWaylandSurfaceKHR");

	uint32_t count = 1;
	VkPhysicalDevice physicalDevice;
	vkEnumeratePhysicalDevices(instance, &count, &physicalDevice);
	if (count == 0) {
		fprintf(stderr, "FAIL no Vulkan device\n");
		return 1;
	}
	VkPhysicalDeviceProperties properties;
	vkGetPhysicalDeviceProperties(physicalDevice, &properties);
	printf("device: %s\n", properties.deviceName);

	VkQueueFamilyProperties families[8];
	uint32_t familyCount = 8;
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount,
		families);
	uint32_t family = UINT32_MAX;
	for (uint32_t i = 0; i < familyCount; i++) {
		VkBool32 present = VK_FALSE;
		vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, vkSurface,
			&present);
		if (present && (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
			family = i;
			break;
		}
	}
	if (family == UINT32_MAX) {
		fprintf(stderr, "FAIL no queue family presents to the surface\n");
		return 1;
	}

	const char* deviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
	float priority = 1.0f;
	VkDeviceQueueCreateInfo queueInfo = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.queueFamilyIndex = family, .queueCount = 1,
		.pQueuePriorities = &priority };
	VkDeviceCreateInfo deviceInfo = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.queueCreateInfoCount = 1, .pQueueCreateInfos = &queueInfo,
		.enabledExtensionCount = 1, .ppEnabledExtensionNames = deviceExtensions };
	VkDevice device;
	CHECK(vkCreateDevice(physicalDevice, &deviceInfo, NULL, &device),
		"vkCreateDevice");
	VkQueue queue;
	vkGetDeviceQueue(device, family, 0, &queue);

	VkSurfaceCapabilitiesKHR caps;
	CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, vkSurface,
		&caps), "surface capabilities");
	VkSurfaceFormatKHR formats[32];
	uint32_t formatCount = 32;
	vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, vkSurface,
		&formatCount, formats);
	VkPresentModeKHR modes[8];
	uint32_t modeCount = 8;
	vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, vkSurface,
		&modeCount, modes);
	VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
	printf("formats:");
	for (uint32_t i = 0; i < formatCount; i++)
		printf(" %d", formats[i].format);
	printf("; present modes:");
	for (uint32_t i = 0; i < modeCount; i++) {
		printf(" %d", modes[i]);
		if (modes[i] == wantMode)
			mode = wantMode;
	}
	printf("\n");

	if (caps.currentExtent.width != UINT32_MAX) {
		width = caps.currentExtent.width;
		height = caps.currentExtent.height;
	}
	uint32_t imageCount = caps.minImageCount + 1;
	if (caps.maxImageCount != 0 && imageCount > caps.maxImageCount)
		imageCount = caps.maxImageCount;
	VkSwapchainCreateInfoKHR swapchainInfo = {
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = vkSurface, .minImageCount = imageCount,
		.imageFormat = formats[0].format,
		.imageColorSpace = formats[0].colorSpace,
		.imageExtent = { width, height }, .imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT
			| VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode = mode, .clipped = VK_TRUE };
	VkSwapchainKHR swapchain;
	CHECK(vkCreateSwapchainKHR(device, &swapchainInfo, NULL, &swapchain),
		"vkCreateSwapchainKHR");
	VkImage images[8];
	uint32_t swapImageCount = 8;
	vkGetSwapchainImagesKHR(device, swapchain, &swapImageCount, images);
	printf("swapchain: %ux%u, %u images, present mode %d\n", width, height,
		swapImageCount, mode);

	VkCommandPoolCreateInfo poolInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex = family };
	VkCommandPool pool;
	CHECK(vkCreateCommandPool(device, &poolInfo, NULL, &pool), "command pool");
	VkCommandBuffer commandBuffers[8];
	VkCommandBufferAllocateInfo allocateInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = swapImageCount };
	CHECK(vkAllocateCommandBuffers(device, &allocateInfo, commandBuffers),
		"command buffers");

	VkSemaphoreCreateInfo semaphoreInfo = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
	VkFenceCreateInfo fenceInfo = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.flags = VK_FENCE_CREATE_SIGNALED_BIT };
	VkSemaphore acquired[8], rendered[8];
	VkFence inFlight[8];
	for (uint32_t i = 0; i < swapImageCount; i++) {
		vkCreateSemaphore(device, &semaphoreInfo, NULL, &acquired[i]);
		vkCreateSemaphore(device, &semaphoreInfo, NULL, &rendered[i]);
		vkCreateFence(device, &fenceInfo, NULL, &inFlight[i]);
	}

	double start = now_s();
	int presented = 0;
	for (int frame = 0; frame < frames && !sClosed; frame++) {
		uint32_t slot = frame % swapImageCount;
		vkWaitForFences(device, 1, &inFlight[slot], VK_TRUE, UINT64_MAX);
		uint32_t index;
		VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
			acquired[slot], VK_NULL_HANDLE, &index);
		if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
			fprintf(stderr, "FAIL acquire: %d\n", result);
			return 1;
		}
		vkResetFences(device, 1, &inFlight[slot]);

		VkCommandBuffer cb = commandBuffers[index];
		vkResetCommandBuffer(cb, 0);
		VkCommandBufferBeginInfo begin = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		vkBeginCommandBuffer(cb, &begin);
		VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		VkImageMemoryBarrier toTransfer = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = images[index], .subresourceRange = range };
		vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &toTransfer);
		float t = frame / 60.0f;
		VkClearColorValue color = { .float32 = {
			0.5f + 0.5f * (float)((frame % 120) / 120.0),
			0.25f + 0.25f * (frame % 2), 0.6f - 0.4f * (t - (int)t), 1.0f } };
		vkCmdClearColorImage(cb, images[index],
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &range);
		VkImageMemoryBarrier toPresent = toTransfer;
		toPresent.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		toPresent.dstAccessMask = 0;
		toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1,
			&toPresent);
		vkEndCommandBuffer(cb);

		VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		VkSubmitInfo submit = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.waitSemaphoreCount = 1, .pWaitSemaphores = &acquired[slot],
			.pWaitDstStageMask = &waitStage, .commandBufferCount = 1,
			.pCommandBuffers = &cb, .signalSemaphoreCount = 1,
			.pSignalSemaphores = &rendered[slot] };
		CHECK(vkQueueSubmit(queue, 1, &submit, inFlight[slot]), "submit");
		VkPresentInfoKHR present = { .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
			.waitSemaphoreCount = 1, .pWaitSemaphores = &rendered[slot],
			.swapchainCount = 1, .pSwapchains = &swapchain,
			.pImageIndices = &index };
		result = vkQueuePresentKHR(queue, &present);
		if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
			fprintf(stderr, "FAIL present: %d\n", result);
			return 1;
		}
		presented++;
		wl_display_dispatch_pending(display);
		if (frame == 59) {
			double elapsed = now_s() - start;
			printf("first 60 frames: %.1f fps\n", 60 / elapsed);
		}
	}
	vkDeviceWaitIdle(device);
	double elapsed = now_s() - start;
	printf("%d frames presented in %.2f s: %.1f fps\n", presented, elapsed,
		presented / elapsed);

	vkDestroySwapchainKHR(device, swapchain, NULL);
	vkDestroyDevice(device, NULL);
	vkDestroySurfaceKHR(instance, vkSurface, NULL);
	vkDestroyInstance(instance, NULL);
	xdg_toplevel_destroy(toplevel);
	xdg_surface_destroy(xdgSurface);
	wl_surface_destroy(surface);
	/* let the in-process server close the window before the program (and
	   with it the server's add-on) goes away: its window thread has been
	   seen running on into unloaded code otherwise */
	wl_display_roundtrip(display);
	wl_display_disconnect(display);
	printf("VKWAYLAND PASS\n");
	return 0;
}
