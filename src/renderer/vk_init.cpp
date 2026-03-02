/*
===========================================================================
vk_init.cpp - Vulkan initialization, device setup, swapchain,
render pass, command buffers, sync objects, samplers, descriptors,
pipeline layout, shader modules.

This replaces GLimp_Init and the OpenGL extension loading.
===========================================================================
*/

#include "tr_local.h"

#ifndef DEDICATED

#include "vk_local.h"
#include "vk_shaders.h"
#include <SDL.h>
#include <SDL_vulkan.h>
#include <string.h>
#include <stdlib.h>

vkState_t vk;
static char vkEnabledExtensionsString[BIG_INFO_STRING];

static void VK_AppendExtensionToken( const char *token ) {
	if ( !token || !*token ) {
		return;
	}

	if ( vkEnabledExtensionsString[0] ) {
		Q_strcat( vkEnabledExtensionsString, sizeof( vkEnabledExtensionsString ), " " );
	}

	Q_strcat( vkEnabledExtensionsString, sizeof( vkEnabledExtensionsString ), token );
}

const char *VK_GetEnabledExtensionsString( void ) {
	return vkEnabledExtensionsString;
}

// ============================================================
// Memory type finder
// ============================================================
uint32_t VK_FindMemoryType( uint32_t typeFilter, VkMemoryPropertyFlags properties ) {
	for ( uint32_t i = 0; i < vk.memoryProperties.memoryTypeCount; i++ ) {
		if ( (typeFilter & (1 << i)) && (vk.memoryProperties.memoryTypes[i].propertyFlags & properties) == properties ) {
			return i;
		}
	}
	ri.Error( ERR_FATAL, "VK_FindMemoryType: failed to find suitable memory type" );
	return 0;
}

// ============================================================
// Depth format finder
// ============================================================
VkFormat VK_FindDepthFormat( void ) {
	VkFormat candidates[] = { VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT };
	for ( int i = 0; i < 3; i++ ) {
		VkFormatProperties props;
		vkGetPhysicalDeviceFormatProperties( vk.physicalDevice, candidates[i], &props );
		if ( props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT ) {
			return candidates[i];
		}
	}
	ri.Error( ERR_FATAL, "VK_FindDepthFormat: no suitable depth format" );
	return VK_FORMAT_D32_SFLOAT;
}

// ============================================================
// Single-time command buffer helpers
// ============================================================
VkCommandBuffer VK_BeginSingleTimeCommands( void ) {
	VkCommandBufferAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandPool = vk.commandPool;
	allocInfo.commandBufferCount = 1;

	VkCommandBuffer commandBuffer;
	vkAllocateCommandBuffers( vk.device, &allocInfo, &commandBuffer );

	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer( commandBuffer, &beginInfo );

	return commandBuffer;
}

void VK_EndSingleTimeCommands( VkCommandBuffer commandBuffer ) {
	vkEndCommandBuffer( commandBuffer );

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;

	vkQueueSubmit( vk.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE );
	vkQueueWaitIdle( vk.graphicsQueue );

	vkFreeCommandBuffers( vk.device, vk.commandPool, 1, &commandBuffer );
}

// ============================================================
// Image layout transitions
// ============================================================
void VK_TransitionImageLayout( VkImage image, VkFormat format,
	VkImageLayout oldLayout, VkImageLayout newLayout, int mipLevels, VkCommandBuffer cmdBuffer )
{
	VkCommandBuffer commandBuffer = cmdBuffer;
	bool singleTime = false;

	if ( commandBuffer == VK_NULL_HANDLE ) {
		commandBuffer = VK_BeginSingleTimeCommands();
		singleTime = true;
	}

	VkImageMemoryBarrier barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = mipLevels;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;

	if ( newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ) {
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		if ( format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT ) {
			barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
	} else {
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	}

	VkPipelineStageFlags sourceStage;
	VkPipelineStageFlags destinationStage;

	if ( oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL ) {
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
	} else if ( oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ) {
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	} else if ( oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ) {
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	} else if ( oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL ) {
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	} else if ( oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR ) {
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = 0;
		sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		destinationStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	} else if ( oldLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL ) {
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	} else {
		barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
		sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
	}

	vkCmdPipelineBarrier( commandBuffer, sourceStage, destinationStage, 0,
		0, NULL, 0, NULL, 1, &barrier );

	if ( singleTime ) {
		VK_EndSingleTimeCommands( commandBuffer );
	}
}

// ============================================================
// Buffer to image copy
// ============================================================
void VK_CopyBufferToImage( VkBuffer buffer, VkImage image, uint32_t width, uint32_t height ) {
	VkCommandBuffer commandBuffer = VK_BeginSingleTimeCommands();

	VkBufferImageCopy region = {};
	region.bufferOffset = 0;
	region.bufferRowLength = 0;
	region.bufferImageHeight = 0;
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.mipLevel = 0;
	region.imageSubresource.baseArrayLayer = 0;
	region.imageSubresource.layerCount = 1;
	region.imageOffset = { 0, 0, 0 };
	region.imageExtent = { width, height, 1 };

	vkCmdCopyBufferToImage( commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region );

	VK_EndSingleTimeCommands( commandBuffer );
}

// ============================================================
// Mipmap generation
// ============================================================
void VK_GenerateMipmaps( VkImage image, VkFormat format, int texWidth, int texHeight, int mipLevels ) {
	VkFormatProperties formatProperties;
	vkGetPhysicalDeviceFormatProperties( vk.physicalDevice, format, &formatProperties );

	if ( !(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) ) {
		// Can't do linear blit, just leave it
		return;
	}

	VkCommandBuffer commandBuffer = VK_BeginSingleTimeCommands();

	VkImageMemoryBarrier barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.image = image;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;
	barrier.subresourceRange.levelCount = 1;

	int mipWidth = texWidth;
	int mipHeight = texHeight;

	for ( int i = 1; i < mipLevels; i++ ) {
		barrier.subresourceRange.baseMipLevel = i - 1;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

		vkCmdPipelineBarrier( commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			0, 0, NULL, 0, NULL, 1, &barrier );

		VkImageBlit blit = {};
		blit.srcOffsets[0] = { 0, 0, 0 };
		blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
		blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.srcSubresource.mipLevel = i - 1;
		blit.srcSubresource.baseArrayLayer = 0;
		blit.srcSubresource.layerCount = 1;

		int nextWidth = mipWidth > 1 ? mipWidth / 2 : 1;
		int nextHeight = mipHeight > 1 ? mipHeight / 2 : 1;

		blit.dstOffsets[0] = { 0, 0, 0 };
		blit.dstOffsets[1] = { nextWidth, nextHeight, 1 };
		blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.dstSubresource.mipLevel = i;
		blit.dstSubresource.baseArrayLayer = 0;
		blit.dstSubresource.layerCount = 1;

		vkCmdBlitImage( commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR );

		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		vkCmdPipelineBarrier( commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0, 0, NULL, 0, NULL, 1, &barrier );

		mipWidth = nextWidth;
		mipHeight = nextHeight;
	}

	// Transition last mip level
	barrier.subresourceRange.baseMipLevel = mipLevels - 1;
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

	vkCmdPipelineBarrier( commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		0, 0, NULL, 0, NULL, 1, &barrier );

	VK_EndSingleTimeCommands( commandBuffer );
}

#ifndef NDEBUG
// ============================================================
// Debug callback for validation layers
// ============================================================
static VKAPI_ATTR VkBool32 VKAPI_CALL VK_DebugCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT messageType,
	const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
	void* pUserData) {

	const char *severity = "INFO";
	if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
		severity = "ERROR";
	} else if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
		severity = "WARNING";
	}

	ri.Printf(PRINT_ALL, "Vulkan %s: %s\n", severity, pCallbackData->pMessage);
	return VK_FALSE;
}
#endif

// ============================================================
// Create Vulkan instance
// ============================================================
static qboolean VK_CreateInstance( SDL_Window *window ) {
	VkApplicationInfo appInfo = {};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "JK2MV";
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.pEngineName = "JK2MV Vulkan";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = VK_API_VERSION_1_2;

	// Get required extensions from SDL
	unsigned int extensionCount = 0;
	SDL_Vulkan_GetInstanceExtensions( window, &extensionCount, NULL );

	const char **extensionNames = (const char **)ri.Malloc( sizeof(const char *) * (extensionCount + 2), TAG_RENDERER, qfalse );
	SDL_Vulkan_GetInstanceExtensions( window, &extensionCount, extensionNames );

	VK_AppendExtensionToken( "instance:" );
	for ( unsigned int i = 0; i < extensionCount; i++ ) {
		VK_AppendExtensionToken( extensionNames[i] );
	}

#ifndef NDEBUG
	// Add debug extension
	extensionNames[extensionCount++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
	VK_AppendExtensionToken( VK_EXT_DEBUG_UTILS_EXTENSION_NAME );
#endif

	VkInstanceCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	createInfo.pApplicationInfo = &appInfo;
	createInfo.enabledExtensionCount = extensionCount;
	createInfo.ppEnabledExtensionNames = extensionNames;
	createInfo.enabledLayerCount = 0;

#ifndef NDEBUG
	// Enable validation layers in debug builds
	const char *validationLayers[] = { "VK_LAYER_KHRONOS_validation" };
	uint32_t layerCount;
	vkEnumerateInstanceLayerProperties( &layerCount, NULL );
	
	VkLayerProperties *availableLayers = (VkLayerProperties *)ri.Malloc( sizeof(VkLayerProperties) * layerCount, TAG_RENDERER, qfalse );
	vkEnumerateInstanceLayerProperties( &layerCount, availableLayers );

	qboolean validationFound = qfalse;
	for ( uint32_t i = 0; i < layerCount; i++ ) {
		if ( strcmp( availableLayers[i].layerName, validationLayers[0] ) == 0 ) {
			validationFound = qtrue;
			break;
		}
	}
	ri.Free( availableLayers );

	if ( validationFound ) {
		createInfo.enabledLayerCount = 1;
		createInfo.ppEnabledLayerNames = validationLayers;
		ri.Printf( PRINT_ALL, "Vulkan: validation layer enabled\n" );
	}
#endif

	VkResult result = vkCreateInstance( &createInfo, NULL, &vk.instance );
	ri.Free( (void *)extensionNames );

	if ( result != VK_SUCCESS ) {
		ri.Printf( PRINT_ALL, "VK_CreateInstance failed: %d\n", result );
		return qfalse;
	}

#ifndef NDEBUG
	// Create debug messenger
	if ( validationFound ) {
		VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = {};
		debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		debugCreateInfo.messageSeverity =
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		debugCreateInfo.messageType =
			VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		debugCreateInfo.pfnUserCallback = VK_DebugCallback;

		PFN_vkCreateDebugUtilsMessengerEXT func = (PFN_vkCreateDebugUtilsMessengerEXT)
			vkGetInstanceProcAddr(vk.instance, "vkCreateDebugUtilsMessengerEXT");

		if (func != NULL) {
			func(vk.instance, &debugCreateInfo, NULL, &vk.debugMessenger);
			ri.Printf(PRINT_ALL, "Vulkan: debug messenger created\n");
		}
	}
#endif

	return qtrue;
}

// ============================================================
// Select physical device
// ============================================================
static qboolean VK_SelectPhysicalDevice( void ) {
	uint32_t deviceCount = 0;
	vkEnumeratePhysicalDevices( vk.instance, &deviceCount, NULL );
	if ( deviceCount == 0 ) {
		ri.Printf( PRINT_ALL, "No Vulkan physical devices found\n" );
		return qfalse;
	}

	VkPhysicalDevice *devices = (VkPhysicalDevice *)ri.Malloc( sizeof(VkPhysicalDevice) * deviceCount, TAG_RENDERER, qfalse );
	vkEnumeratePhysicalDevices( vk.instance, &deviceCount, devices );

	// Prefer discrete GPU
	vk.physicalDevice = devices[0];
	for ( uint32_t i = 0; i < deviceCount; i++ ) {
		VkPhysicalDeviceProperties props;
		vkGetPhysicalDeviceProperties( devices[i], &props );
		if ( props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ) {
			vk.physicalDevice = devices[i];
			break;
		}
	}

	vkGetPhysicalDeviceProperties( vk.physicalDevice, &vk.deviceProperties );
	vkGetPhysicalDeviceMemoryProperties( vk.physicalDevice, &vk.memoryProperties );

	VkPhysicalDeviceFeatures features;
	vkGetPhysicalDeviceFeatures( vk.physicalDevice, &features );
	vk.fillModeNonSolid = features.fillModeNonSolid ? qtrue : qfalse;
	vk.bcSupported = features.textureCompressionBC ? qtrue : qfalse;

	ri.Printf( PRINT_ALL, "Vulkan device: %s\n", vk.deviceProperties.deviceName );
	ri.Free( devices );

	return qtrue;
}

// ============================================================
// Find queue families
// ============================================================
static qboolean VK_FindQueueFamilies( void ) {
	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties( vk.physicalDevice, &queueFamilyCount, NULL );

	VkQueueFamilyProperties *families = (VkQueueFamilyProperties *)ri.Malloc(
		sizeof(VkQueueFamilyProperties) * queueFamilyCount, TAG_RENDERER, qfalse );
	vkGetPhysicalDeviceQueueFamilyProperties( vk.physicalDevice, &queueFamilyCount, families );

	qboolean foundGraphics = qfalse, foundPresent = qfalse;

	for ( uint32_t i = 0; i < queueFamilyCount; i++ ) {
		if ( families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT ) {
			vk.graphicsQueueFamily = i;
			foundGraphics = qtrue;
		}

		VkBool32 presentSupport = VK_FALSE;
		vkGetPhysicalDeviceSurfaceSupportKHR( vk.physicalDevice, i, vk.surface, &presentSupport );
		if ( presentSupport ) {
			vk.presentQueueFamily = i;
			foundPresent = qtrue;
		}

		if ( foundGraphics && foundPresent ) break;
	}

	ri.Free( families );
	return (foundGraphics && foundPresent) ? qtrue : qfalse;
}

// ============================================================
// Create logical device
// ============================================================
static qboolean VK_CheckRTExtensionSupport( void ) {
	uint32_t extCount = 0;
	vkEnumerateDeviceExtensionProperties( vk.physicalDevice, NULL, &extCount, NULL );
	if ( extCount == 0 ) return qfalse;

	VkExtensionProperties *exts = (VkExtensionProperties *)ri.Malloc(
		sizeof(VkExtensionProperties) * extCount, TAG_RENDERER, qfalse );
	vkEnumerateDeviceExtensionProperties( vk.physicalDevice, NULL, &extCount, exts );

	qboolean hasAccelStruct = qfalse, hasRTPipeline = qfalse;
	qboolean hasDeferredOps = qfalse, hasBufAddr = qfalse;
	for ( uint32_t i = 0; i < extCount; i++ ) {
		if ( !strcmp( exts[i].extensionName, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME ) ) hasAccelStruct = qtrue;
		if ( !strcmp( exts[i].extensionName, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME ) ) hasRTPipeline = qtrue;
		if ( !strcmp( exts[i].extensionName, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME ) ) hasDeferredOps = qtrue;
		if ( !strcmp( exts[i].extensionName, VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME ) ) hasBufAddr = qtrue;
	}
	ri.Free( exts );

	return (hasAccelStruct && hasRTPipeline && hasDeferredOps && hasBufAddr) ? qtrue : qfalse;
}

static void VK_LoadRTFunctions( void ) {
	vk.rtFuncs.vkGetAccelerationStructureBuildSizesKHR = (PFN_vkGetAccelerationStructureBuildSizesKHR)
		vkGetDeviceProcAddr( vk.device, "vkGetAccelerationStructureBuildSizesKHR" );
	vk.rtFuncs.vkCreateAccelerationStructureKHR = (PFN_vkCreateAccelerationStructureKHR)
		vkGetDeviceProcAddr( vk.device, "vkCreateAccelerationStructureKHR" );
	vk.rtFuncs.vkDestroyAccelerationStructureKHR = (PFN_vkDestroyAccelerationStructureKHR)
		vkGetDeviceProcAddr( vk.device, "vkDestroyAccelerationStructureKHR" );
	vk.rtFuncs.vkCmdBuildAccelerationStructuresKHR = (PFN_vkCmdBuildAccelerationStructuresKHR)
		vkGetDeviceProcAddr( vk.device, "vkCmdBuildAccelerationStructuresKHR" );
	vk.rtFuncs.vkGetAccelerationStructureDeviceAddressKHR = (PFN_vkGetAccelerationStructureDeviceAddressKHR)
		vkGetDeviceProcAddr( vk.device, "vkGetAccelerationStructureDeviceAddressKHR" );
	vk.rtFuncs.vkCreateRayTracingPipelinesKHR = (PFN_vkCreateRayTracingPipelinesKHR)
		vkGetDeviceProcAddr( vk.device, "vkCreateRayTracingPipelinesKHR" );
	vk.rtFuncs.vkGetRayTracingShaderGroupHandlesKHR = (PFN_vkGetRayTracingShaderGroupHandlesKHR)
		vkGetDeviceProcAddr( vk.device, "vkGetRayTracingShaderGroupHandlesKHR" );
	vk.rtFuncs.vkCmdTraceRaysKHR = (PFN_vkCmdTraceRaysKHR)
		vkGetDeviceProcAddr( vk.device, "vkCmdTraceRaysKHR" );
	vk.rtFuncs.vkGetBufferDeviceAddressKHR = (PFN_vkGetBufferDeviceAddressKHR)
		vkGetDeviceProcAddr( vk.device, "vkGetBufferDeviceAddress" );
}

static qboolean VK_CreateDevice( void ) {
	float queuePriority = 1.0f;
	
	VkDeviceQueueCreateInfo queueCreateInfos[2] = {};
	int queueCount = 1;

	queueCreateInfos[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queueCreateInfos[0].queueFamilyIndex = vk.graphicsQueueFamily;
	queueCreateInfos[0].queueCount = 1;
	queueCreateInfos[0].pQueuePriorities = &queuePriority;

	if ( vk.graphicsQueueFamily != vk.presentQueueFamily ) {
		queueCreateInfos[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreateInfos[1].queueFamilyIndex = vk.presentQueueFamily;
		queueCreateInfos[1].queueCount = 1;
		queueCreateInfos[1].pQueuePriorities = &queuePriority;
		queueCount = 2;
	}

	VkPhysicalDeviceFeatures deviceFeatures = {};
	deviceFeatures.fillModeNonSolid = vk.fillModeNonSolid;
	deviceFeatures.samplerAnisotropy = VK_TRUE;
	deviceFeatures.shaderStorageImageWriteWithoutFormat = VK_TRUE;
	deviceFeatures.textureCompressionBC = vk.bcSupported ? VK_TRUE : VK_FALSE;

	// Check for ray tracing extension support
	vk.rayTracingSupported = VK_CheckRTExtensionSupport();

	// Build extension list
	const char *deviceExtensions[8];
	int numExtensions = 0;
	deviceExtensions[numExtensions++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
	VK_AppendExtensionToken( "device:" );
	VK_AppendExtensionToken( VK_KHR_SWAPCHAIN_EXTENSION_NAME );

	// Chain structs for Vulkan 1.2 features and RT features
	VkPhysicalDeviceVulkan12Features features12 = {};
	features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	features12.bufferDeviceAddress = VK_TRUE;
	features12.descriptorIndexing = VK_TRUE;

	VkPhysicalDeviceVulkan11Features features11 = {};
	features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
	features11.pNext = &features12;

	VkPhysicalDeviceAccelerationStructureFeaturesKHR accelFeatures = {};
	VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtPipelineFeatures = {};

	if ( vk.rayTracingSupported ) {
		ri.Printf( PRINT_ALL, "Ray tracing extensions available, enabling VK_KHR_ray_tracing_pipeline\n" );
		deviceExtensions[numExtensions++] = VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME;
		deviceExtensions[numExtensions++] = VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME;
		deviceExtensions[numExtensions++] = VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME;
		VK_AppendExtensionToken( VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME );
		VK_AppendExtensionToken( VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME );
		VK_AppendExtensionToken( VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME );

		accelFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
		accelFeatures.accelerationStructure = VK_TRUE;

		rtPipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
		rtPipelineFeatures.rayTracingPipeline = VK_TRUE;
		rtPipelineFeatures.pNext = &accelFeatures;

		features12.pNext = &rtPipelineFeatures;
	}

	VkPhysicalDeviceFeatures2 features2 = {};
	features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	features2.features = deviceFeatures;
	features2.pNext = &features11;

	VkDeviceCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	createInfo.queueCreateInfoCount = queueCount;
	createInfo.pQueueCreateInfos = queueCreateInfos;
	createInfo.pNext = &features2;			// use VkPhysicalDeviceFeatures2 chain
	createInfo.pEnabledFeatures = NULL;		// must be NULL when using pNext features2
	createInfo.enabledExtensionCount = numExtensions;
	createInfo.ppEnabledExtensionNames = deviceExtensions;

	if ( vkCreateDevice( vk.physicalDevice, &createInfo, NULL, &vk.device ) != VK_SUCCESS ) {
		ri.Printf( PRINT_ALL, "Failed to create Vulkan logical device\n" );
		return qfalse;
	}

	vkGetDeviceQueue( vk.device, vk.graphicsQueueFamily, 0, &vk.graphicsQueue );
	vkGetDeviceQueue( vk.device, vk.presentQueueFamily, 0, &vk.presentQueue );

	// Load RT function pointers after device creation
	if ( vk.rayTracingSupported ) {
		VK_LoadRTFunctions();

		// Query RT pipeline properties
		VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProps = {};
		rtProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
		VkPhysicalDeviceProperties2 deviceProps2 = {};
		deviceProps2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
		deviceProps2.pNext = &rtProps;
		vkGetPhysicalDeviceProperties2( vk.physicalDevice, &deviceProps2 );

		vk.glowReflect.shaderGroupHandleSize = rtProps.shaderGroupHandleSize;
		vk.glowReflect.shaderGroupHandleAlignment = rtProps.shaderGroupHandleAlignment;
		vk.glowReflect.shaderGroupBaseAlignment = rtProps.shaderGroupBaseAlignment;

		ri.Printf( PRINT_ALL, "RT pipeline properties: handleSize=%u handleAlign=%u baseAlign=%u\n",
			rtProps.shaderGroupHandleSize, rtProps.shaderGroupHandleAlignment,
			rtProps.shaderGroupBaseAlignment );
	}

	return qtrue;
}

// ============================================================
// Create swapchain
// ============================================================
void VK_CreateSwapchain( void ) {
	VkSurfaceCapabilitiesKHR capabilities;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR( vk.physicalDevice, vk.surface, &capabilities );

	uint32_t formatCount;
	vkGetPhysicalDeviceSurfaceFormatsKHR( vk.physicalDevice, vk.surface, &formatCount, NULL );
	VkSurfaceFormatKHR *formats = (VkSurfaceFormatKHR *)ri.Malloc( sizeof(VkSurfaceFormatKHR) * formatCount, TAG_RENDERER, qfalse );
	vkGetPhysicalDeviceSurfaceFormatsKHR( vk.physicalDevice, vk.surface, &formatCount, formats );

	// Choose B8G8R8A8_UNORM + SRGB_NONLINEAR if available.
	// Avoid _SRGB pixel formats — the game applies gamma correction
	// manually via a post-processing shader pass, so an SRGB swapchain
	// format would cause double gamma application.
	VkSurfaceFormatKHR chosenFormat = formats[0];
	qboolean foundPreferred = qfalse;

	// First pass: exact preferred match
	for ( uint32_t i = 0; i < formatCount; i++ ) {
		if ( formats[i].format == VK_FORMAT_B8G8R8A8_UNORM && formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR ) {
			chosenFormat = formats[i];
			foundPreferred = qtrue;
			break;
		}
	}

	// Second pass: any UNORM format (avoid SRGB pixel formats)
	if ( !foundPreferred ) {
		for ( uint32_t i = 0; i < formatCount; i++ ) {
			if ( formats[i].format == VK_FORMAT_B8G8R8A8_UNORM ||
				 formats[i].format == VK_FORMAT_R8G8B8A8_UNORM ) {
				chosenFormat = formats[i];
				foundPreferred = qtrue;
				break;
			}
		}
	}

	if ( !foundPreferred ) {
		ri.Printf( PRINT_WARNING, "WARNING: No UNORM swapchain format available, gamma may be incorrect\n" );
	}

	ri.Free( formats );

	vk.swapchainFormat = chosenFormat.format;

	// Choose present mode based on r_swapInterval cvar
	// 0 = no vsync (MAILBOX or IMMEDIATE), 1 = vsync (FIFO)
	cvar_t *r_swapInterval = ri.Cvar_Get( "r_swapInterval", "0", 0 );
	VkPresentModeKHR desiredMode = VK_PRESENT_MODE_FIFO_KHR;
	if ( r_swapInterval && r_swapInterval->integer == 0 ) {
		desiredMode = VK_PRESENT_MODE_MAILBOX_KHR; // prefer mailbox (triple-buffered no-vsync)
	}

	// Check if desired mode is available
	uint32_t modeCount;
	vkGetPhysicalDeviceSurfacePresentModesKHR( vk.physicalDevice, vk.surface, &modeCount, NULL );
	VkPresentModeKHR *modes = (VkPresentModeKHR *)ri.Malloc( sizeof(VkPresentModeKHR) * modeCount, TAG_RENDERER, qfalse );
	vkGetPhysicalDeviceSurfacePresentModesKHR( vk.physicalDevice, vk.surface, &modeCount, modes );

	VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR; // always available fallback
	for ( uint32_t i = 0; i < modeCount; i++ ) {
		if ( modes[i] == desiredMode ) {
			presentMode = desiredMode;
			break;
		}
	}
	// If mailbox not available and vsync is off, try immediate
	if ( presentMode != desiredMode && desiredMode == VK_PRESENT_MODE_MAILBOX_KHR ) {
		for ( uint32_t i = 0; i < modeCount; i++ ) {
			if ( modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR ) {
				presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
				break;
			}
		}
	}
	ri.Free( modes );

	if ( presentMode == VK_PRESENT_MODE_FIFO_KHR ) {
		ri.Printf( PRINT_ALL, "Vulkan present mode: FIFO (vsync)\n" );
	} else if ( presentMode == VK_PRESENT_MODE_MAILBOX_KHR ) {
		ri.Printf( PRINT_ALL, "Vulkan present mode: Mailbox (no vsync, triple buffered)\n" );
	} else if ( presentMode == VK_PRESENT_MODE_IMMEDIATE_KHR ) {
		ri.Printf( PRINT_ALL, "Vulkan present mode: Immediate (no vsync)\n" );
	}

	// Extent
	if ( capabilities.currentExtent.width != UINT32_MAX ) {
		vk.swapchainExtent = capabilities.currentExtent;
	} else {
		vk.swapchainExtent.width = glConfig.vidWidth;
		vk.swapchainExtent.height = glConfig.vidHeight;
	}

	uint32_t imageCount = capabilities.minImageCount + 1;
	if ( capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount ) {
		imageCount = capabilities.maxImageCount;
	}

	VkSwapchainCreateInfoKHR createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	createInfo.surface = vk.surface;
	createInfo.minImageCount = imageCount;
	createInfo.imageFormat = chosenFormat.format;
	createInfo.imageColorSpace = chosenFormat.colorSpace;
	createInfo.imageExtent = vk.swapchainExtent;
	createInfo.imageArrayLayers = 1;
	createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

	if ( vk.graphicsQueueFamily != vk.presentQueueFamily ) {
		uint32_t families[] = { vk.graphicsQueueFamily, vk.presentQueueFamily };
		createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
		createInfo.queueFamilyIndexCount = 2;
		createInfo.pQueueFamilyIndices = families;
	} else {
		createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	}

	createInfo.preTransform = capabilities.currentTransform;
	createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	createInfo.presentMode = presentMode;
	createInfo.clipped = VK_TRUE;
	createInfo.oldSwapchain = VK_NULL_HANDLE;

	if ( vkCreateSwapchainKHR( vk.device, &createInfo, NULL, &vk.swapchain ) != VK_SUCCESS ) {
		ri.Error( ERR_FATAL, "Failed to create Vulkan swapchain" );
	}

	vkGetSwapchainImagesKHR( vk.device, vk.swapchain, &vk.swapchainImageCount, NULL );
	vkGetSwapchainImagesKHR( vk.device, vk.swapchain, &vk.swapchainImageCount, vk.swapchainImages );
	for ( uint32_t i = 0; i < vk.swapchainImageCount; i++ ) {
		vk.swapchainImageLayouts[i] = VK_IMAGE_LAYOUT_UNDEFINED;
	}

	// Create image views
	for ( uint32_t i = 0; i < vk.swapchainImageCount; i++ ) {
		VkImageViewCreateInfo viewInfo = {};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = vk.swapchainImages[i];
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = vk.swapchainFormat;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;

		if ( vkCreateImageView( vk.device, &viewInfo, NULL, &vk.swapchainImageViews[i] ) != VK_SUCCESS ) {
			ri.Error( ERR_FATAL, "Failed to create swapchain image view" );
		}
	}
}

void VK_DestroySwapchain( void ) {
	for ( uint32_t i = 0; i < vk.swapchainImageCount; i++ ) {
		if ( vk.swapchainImageViews[i] ) {
			vkDestroyImageView( vk.device, vk.swapchainImageViews[i], NULL );
			vk.swapchainImageViews[i] = VK_NULL_HANDLE;
		}
		vk.swapchainImageLayouts[i] = VK_IMAGE_LAYOUT_UNDEFINED;
	}
	if ( vk.swapchain ) {
		vkDestroySwapchainKHR( vk.device, vk.swapchain, NULL );
		vk.swapchain = VK_NULL_HANDLE;
	}
}

// ============================================================
// Create depth buffer
// ============================================================
void VK_CreateDepthBuffer( void ) {
	vk.depthFormat = VK_FindDepthFormat();

	VkImageCreateInfo imageInfo = {};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.extent.width = vk.swapchainExtent.width;
	imageInfo.extent.height = vk.swapchainExtent.height;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.format = vk.depthFormat;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	vkCreateImage( vk.device, &imageInfo, NULL, &vk.depthImage );

	VkMemoryRequirements memReqs;
	vkGetImageMemoryRequirements( vk.device, vk.depthImage, &memReqs );

	VkMemoryAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memReqs.size;
	allocInfo.memoryTypeIndex = VK_FindMemoryType( memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );

	vkAllocateMemory( vk.device, &allocInfo, NULL, &vk.depthImageMemory );
	vkBindImageMemory( vk.device, vk.depthImage, vk.depthImageMemory, 0 );

	VkImageViewCreateInfo viewInfo = {};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = vk.depthImage;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = vk.depthFormat;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	if ( vk.depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT || vk.depthFormat == VK_FORMAT_D24_UNORM_S8_UINT ) {
		viewInfo.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
	}
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = 1;

	vkCreateImageView( vk.device, &viewInfo, NULL, &vk.depthImageView );

	VK_TransitionImageLayout( vk.depthImage, vk.depthFormat,
		VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 1 );
}

// ============================================================
// Create render pass
// ============================================================
void VK_CreateRenderPass( void ) {
	VkAttachmentDescription colorAttachment = {};
	colorAttachment.format = vk.sceneFormat;
	colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentDescription depthAttachment = {};
	depthAttachment.format = vk.depthFormat;
	depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;  // STORE so glow scene render pass can LOAD depth for occlusion
	depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
	depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference colorAttachmentRef = {};
	colorAttachmentRef.attachment = 0;
	colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depthAttachmentRef = {};
	depthAttachmentRef.attachment = 1;
	depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorAttachmentRef;
	subpass.pDepthStencilAttachment = &depthAttachmentRef;

	VkSubpassDependency dependency = {};
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	dependency.srcAccessMask = 0;
	dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

	VkAttachmentDescription attachments[] = { colorAttachment, depthAttachment };

	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = 2;
	renderPassInfo.pAttachments = attachments;
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;
	renderPassInfo.dependencyCount = 1;
	renderPassInfo.pDependencies = &dependency;

	if ( vkCreateRenderPass( vk.device, &renderPassInfo, NULL, &vk.renderPass ) != VK_SUCCESS ) {
		ri.Error( ERR_FATAL, "Failed to create render pass" );
	}

	// Create a secondary render pass with LOAD instead of CLEAR
	// This is used to resume rendering after glow without clearing the framebuffer
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	colorAttachment.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	depthAttachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	// Update the attachments array with the LOAD ops
	attachments[0] = colorAttachment;
	attachments[1] = depthAttachment;

	if ( vkCreateRenderPass( vk.device, &renderPassInfo, NULL, &vk.renderPassLoad ) != VK_SUCCESS ) {
		ri.Error( ERR_FATAL, "Failed to create load render pass" );
	}
}

// ============================================================
// Create framebuffers
// ============================================================
void VK_CreateFramebuffers( void ) {
	// When HDR is on and scene format differs from swapchain, these framebuffers
	// are unused (gamma redirect draws to offscreen image). Skip creation to
	// avoid format mismatch between render pass and swapchain image views.
	if ( vk.sceneFormat != vk.swapchainFormat ) {
		return;
	}

	for ( uint32_t i = 0; i < vk.swapchainImageCount; i++ ) {
		VkImageView attachments[] = { vk.swapchainImageViews[i], vk.depthImageView };

		VkFramebufferCreateInfo framebufferInfo = {};
		framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebufferInfo.renderPass = vk.renderPass;
		framebufferInfo.attachmentCount = 2;
		framebufferInfo.pAttachments = attachments;
		framebufferInfo.width = vk.swapchainExtent.width;
		framebufferInfo.height = vk.swapchainExtent.height;
		framebufferInfo.layers = 1;

		if ( vkCreateFramebuffer( vk.device, &framebufferInfo, NULL, &vk.framebuffers[i] ) != VK_SUCCESS ) {
			ri.Error( ERR_FATAL, "Failed to create framebuffer" );
		}
	}
}

// ============================================================
// Create command pool and buffers
// ============================================================
void VK_CreateCommandPool( void ) {
	VkCommandPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.queueFamilyIndex = vk.graphicsQueueFamily;
	poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

	if ( vkCreateCommandPool( vk.device, &poolInfo, NULL, &vk.commandPool ) != VK_SUCCESS ) {
		ri.Error( ERR_FATAL, "Failed to create command pool" );
	}
}

void VK_CreateCommandBuffers( void ) {
	VkCommandBufferAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = vk.commandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = 1;

	for ( int i = 0; i < VK_NUM_COMMAND_BUFFERS; i++ ) {
		if ( vkAllocateCommandBuffers( vk.device, &allocInfo, &vk.frames[i].commandBuffer ) != VK_SUCCESS ) {
			ri.Error( ERR_FATAL, "Failed to allocate command buffer" );
		}
	}
}

// ============================================================
// Create synchronization objects
// ============================================================
void VK_CreateSyncObjects( void ) {
	VkSemaphoreCreateInfo semaphoreInfo = {};
	semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	VkFenceCreateInfo fenceInfo = {};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	for ( int i = 0; i < VK_NUM_COMMAND_BUFFERS; i++ ) {
		vkCreateSemaphore( vk.device, &semaphoreInfo, NULL, &vk.frames[i].imageAvailableSemaphore );
		vkCreateFence( vk.device, &fenceInfo, NULL, &vk.frames[i].fence );
	}

	for ( int i = 0; i < VK_MAX_SWAPCHAIN_IMAGES; i++ ) {
		vkCreateSemaphore( vk.device, &semaphoreInfo, NULL, &vk.renderFinishedSemaphores[i] );
	}
}

// ============================================================
// Create dynamic vertex/index/uniform buffers
// ============================================================
static void VK_CreateBuffer( VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
	VkBuffer *buffer, VkDeviceMemory *memory )
{
	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = size;
	bufferInfo.usage = usage;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	vkCreateBuffer( vk.device, &bufferInfo, NULL, buffer );

	VkMemoryRequirements memReqs;
	vkGetBufferMemoryRequirements( vk.device, *buffer, &memReqs );

	VkMemoryAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memReqs.size;
	allocInfo.memoryTypeIndex = VK_FindMemoryType( memReqs.memoryTypeBits, properties );

	vkAllocateMemory( vk.device, &allocInfo, NULL, memory );
	vkBindBufferMemory( vk.device, *buffer, *memory, 0 );
}

void VK_CreateDynamicBuffers( void ) {
	VkMemoryPropertyFlags hostVisible = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

	for ( int i = 0; i < VK_NUM_COMMAND_BUFFERS; i++ ) {
		VK_CreateBuffer( VK_VERTEX_BUFFER_SIZE, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, hostVisible,
			&vk.dynBuffers[i].vertexBuffer, &vk.dynBuffers[i].vertexMemory );
		vkMapMemory( vk.device, vk.dynBuffers[i].vertexMemory, 0, VK_VERTEX_BUFFER_SIZE, 0,
			(void **)&vk.dynBuffers[i].vertexData );

		VK_CreateBuffer( VK_INDEX_BUFFER_SIZE, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, hostVisible,
			&vk.dynBuffers[i].indexBuffer, &vk.dynBuffers[i].indexMemory );
		vkMapMemory( vk.device, vk.dynBuffers[i].indexMemory, 0, VK_INDEX_BUFFER_SIZE, 0,
			(void **)&vk.dynBuffers[i].indexData );

		VK_CreateBuffer( VK_UNIFORM_BUFFER_SIZE, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, hostVisible,
			&vk.dynBuffers[i].uniformBuffer, &vk.dynBuffers[i].uniformMemory );
		vkMapMemory( vk.device, vk.dynBuffers[i].uniformMemory, 0, VK_UNIFORM_BUFFER_SIZE, 0,
			(void **)&vk.dynBuffers[i].uniformData );
	}
}

// ============================================================
// Create staging buffer
// ============================================================
void VK_CreateStagingBuffer( void ) {
	VkMemoryPropertyFlags hostVisible = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	VK_CreateBuffer( VK_STAGING_BUFFER_SIZE, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, hostVisible,
		&vk.staging.buffer, &vk.staging.memory );
	vkMapMemory( vk.device, vk.staging.memory, 0, VK_STAGING_BUFFER_SIZE, 0, (void **)&vk.staging.data );
}

// ============================================================
// Create samplers
// ============================================================
void VK_CreateSamplers( void ) {
	VkSamplerCreateInfo samplerInfo = {};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;

	// Parse initial texture mode from r_textureMode cvar
	extern cvar_t *r_textureMode;
	const char *mode = r_textureMode ? r_textureMode->string : "GL_LINEAR_MIPMAP_NEAREST";
	VkFilter initMinFilter = VK_FILTER_LINEAR;
	VkFilter initMagFilter = VK_FILTER_LINEAR;
	VkSamplerMipmapMode initMipMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

	if ( !Q_stricmp( mode, "GL_NEAREST" ) || !Q_stricmp( mode, "GL_NEAREST_MIPMAP_NEAREST" ) ) {
		initMinFilter = VK_FILTER_NEAREST;
		initMagFilter = VK_FILTER_NEAREST;
		initMipMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	} else if ( !Q_stricmp( mode, "GL_LINEAR" ) || !Q_stricmp( mode, "GL_LINEAR_MIPMAP_NEAREST" ) ) {
		initMinFilter = VK_FILTER_LINEAR;
		initMagFilter = VK_FILTER_LINEAR;
		initMipMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	} else if ( !Q_stricmp( mode, "GL_NEAREST_MIPMAP_LINEAR" ) ) {
		initMinFilter = VK_FILTER_NEAREST;
		initMagFilter = VK_FILTER_NEAREST;
		initMipMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	} else if ( !Q_stricmp( mode, "GL_LINEAR_MIPMAP_LINEAR" ) ) {
		initMinFilter = VK_FILTER_LINEAR;
		initMagFilter = VK_FILTER_LINEAR;
		initMipMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	}

	samplerInfo.magFilter = initMagFilter;
	samplerInfo.minFilter = initMinFilter;
	samplerInfo.mipmapMode = initMipMode;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	// Respect r_ext_texture_filter_anisotropic cvar
	extern cvar_t *r_ext_texture_filter_anisotropic;
	int anisoLevel = r_ext_texture_filter_anisotropic ? r_ext_texture_filter_anisotropic->integer : 0;
	if ( anisoLevel > 0 ) {
		samplerInfo.anisotropyEnable = VK_TRUE;
		float maxAniso = vk.deviceProperties.limits.maxSamplerAnisotropy;
		samplerInfo.maxAnisotropy = (float)anisoLevel < maxAniso ? (float)anisoLevel : maxAniso;
	} else {
		samplerInfo.anisotropyEnable = VK_FALSE;
		samplerInfo.maxAnisotropy = 1.0f;
	}
	samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
	samplerInfo.unnormalizedCoordinates = VK_FALSE;
	samplerInfo.compareEnable = VK_FALSE;
	samplerInfo.minLod = 0.0f;
	samplerInfo.maxLod = 16.0f;
	// Respect r_textureLODBias cvar
	extern cvar_t *r_textureLODBias;
	samplerInfo.mipLodBias = r_textureLODBias ? r_textureLODBias->value : 0.0f;
	vkCreateSampler( vk.device, &samplerInfo, NULL, &vk.samplerMipRepeat );

	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	vkCreateSampler( vk.device, &samplerInfo, NULL, &vk.samplerMipClamp );

	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	samplerInfo.maxLod = 0.0f;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	vkCreateSampler( vk.device, &samplerInfo, NULL, &vk.samplerNoMipRepeat );

	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	vkCreateSampler( vk.device, &samplerInfo, NULL, &vk.samplerNoMipClamp );

	samplerInfo.magFilter = VK_FILTER_NEAREST;
	samplerInfo.minFilter = VK_FILTER_NEAREST;
	samplerInfo.anisotropyEnable = VK_FALSE;
	vkCreateSampler( vk.device, &samplerInfo, NULL, &vk.samplerNearest );
}

// ============================================================
// Create descriptor pool and layout
// ============================================================
void VK_CreateDescriptorPool( void ) {
	VkDescriptorPoolSize poolSizes[5] = {};
	poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	// RT needs: 3 in RT set (depth, glow, history) + output + outputRead + blurTemp + blurOutput = 7 extra
	// Progressive bloom needs up to BLOOM_MAX_MIPS (6) descriptor sets for the mip chain
	poolSizes[0].descriptorCount = VK_MAX_IMAGE_SLOTS + BLOOM_MAX_MIPS + ( vk.rayTracingSupported ? 7 : 0 );
	poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	poolSizes[1].descriptorCount = VK_NUM_COMMAND_BUFFERS;
	poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	// Ping-pong RT: 2 storage images (one per descriptor set)
	poolSizes[2].descriptorCount = 2;
	poolSizes[3].type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
	// Ping-pong RT: 2 acceleration structures (one per descriptor set)
	poolSizes[3].descriptorCount = 2;
	poolSizes[4].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	// Ping-pong RT: 2 sets × 2 UBOs each (GlowSourcesUBO + RTParamsUBO)
	poolSizes[4].descriptorCount = 4;

	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.poolSizeCount = vk.rayTracingSupported ? 5 : 2;
	poolInfo.pPoolSizes = poolSizes;
	// RT adds: 2 ping-pong RT descriptor sets + 4 image-only sets (2×output + 2×outputRead) + blurTemp + blurOutput
	// Progressive bloom adds up to BLOOM_MAX_MIPS sets for the mip chain
	poolInfo.maxSets = VK_MAX_IMAGE_SLOTS + VK_NUM_COMMAND_BUFFERS + BLOOM_MAX_MIPS + ( vk.rayTracingSupported ? 8 : 0 );
	poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

	if ( vkCreateDescriptorPool( vk.device, &poolInfo, NULL, &vk.descriptorPool ) != VK_SUCCESS ) {
		ri.Error( ERR_FATAL, "Failed to create descriptor pool" );
	}
}

void VK_CreateDescriptorSetLayout( void ) {
	// Texture sampler layout (set 0 and set 1)
	VkDescriptorSetLayoutBinding binding = {};
	binding.binding = 0;
	binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	binding.descriptorCount = 1;
	binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = 1;
	layoutInfo.pBindings = &binding;

	if ( vkCreateDescriptorSetLayout( vk.device, &layoutInfo, NULL, &vk.descriptorSetLayout ) != VK_SUCCESS ) {
		ri.Error( ERR_FATAL, "Failed to create descriptor set layout" );
	}

	// UBO layout (set 2) - dynamic uniform buffer for per-draw GPU params
	VkDescriptorSetLayoutBinding uboBinding = {};
	uboBinding.binding = 0;
	uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	uboBinding.descriptorCount = 1;
	uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo uboLayoutInfo = {};
	uboLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	uboLayoutInfo.bindingCount = 1;
	uboLayoutInfo.pBindings = &uboBinding;

	if ( vkCreateDescriptorSetLayout( vk.device, &uboLayoutInfo, NULL, &vk.uboDescriptorSetLayout ) != VK_SUCCESS ) {
		ri.Error( ERR_FATAL, "Failed to create UBO descriptor set layout" );
	}

	// Store UBO alignment requirement
	vk.uboAlignment = vk.deviceProperties.limits.minUniformBufferOffsetAlignment;
	if ( vk.uboAlignment < GPU_PARAMS_ALIGN ) {
		vk.uboAlignment = GPU_PARAMS_ALIGN;
	}
	// Ensure alignment is at least minUniformBufferOffsetAlignment
	VkDeviceSize minAlign = vk.deviceProperties.limits.minUniformBufferOffsetAlignment;
	if ( vk.uboAlignment < minAlign ) {
		vk.uboAlignment = minAlign;
	}
	// Round up to next multiple of minUniformBufferOffsetAlignment
	vk.uboAlignment = (vk.uboAlignment + minAlign - 1) & ~(minAlign - 1);

	// Allocate UBO descriptor sets (one per frame)
	for ( int i = 0; i < VK_NUM_COMMAND_BUFFERS; i++ ) {
		VkDescriptorSetAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = vk.descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &vk.uboDescriptorSetLayout;

		if ( vkAllocateDescriptorSets( vk.device, &allocInfo, &vk.uboDescriptorSets[i] ) != VK_SUCCESS ) {
			ri.Error( ERR_FATAL, "Failed to allocate UBO descriptor set" );
		}

		// Point descriptor at the dynamic uniform buffer
		VkDescriptorBufferInfo bufInfo = {};
		bufInfo.buffer = vk.dynBuffers[i].uniformBuffer;
		bufInfo.offset = 0;
		bufInfo.range = GPU_PARAMS_FULL_SIZE;  // Must cover max possible read (including bones)

		VkWriteDescriptorSet write = {};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = vk.uboDescriptorSets[i];
		write.dstBinding = 0;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
		write.pBufferInfo = &bufInfo;

		vkUpdateDescriptorSets( vk.device, 1, &write, 0, NULL );
	}
}

// ============================================================
// Create pipeline layout
// ============================================================
void VK_CreatePipelineLayout( void ) {
	// Three descriptor sets: set 0 = texture 0, set 1 = texture 1, set 2 = GPU params UBO
	VkDescriptorSetLayout setLayouts[] = { vk.descriptorSetLayout, vk.descriptorSetLayout, vk.uboDescriptorSetLayout };

	VkPushConstantRange pushConstantRange = {};
	pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	pushConstantRange.offset = 0;
	pushConstantRange.size = sizeof(vkPushConstants_t);

	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 3;
	pipelineLayoutInfo.pSetLayouts = setLayouts;
	pipelineLayoutInfo.pushConstantRangeCount = 1;
	pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

	if ( vkCreatePipelineLayout( vk.device, &pipelineLayoutInfo, NULL, &vk.pipelineLayout ) != VK_SUCCESS ) {
		ri.Error( ERR_FATAL, "Failed to create pipeline layout" );
	}
}

// ============================================================
// Create shader modules from pre-compiled SPIR-V files.
// The GLSL sources live in assets/shaders/spirv/ and are
// compiled to .spv by glslangValidator during the build.
// ============================================================
static VkShaderModule VK_CreateShaderModuleFromSPV( const uint32_t *code, size_t codeSize ) {
	VkShaderModuleCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.codeSize = codeSize;
	createInfo.pCode = code;

	VkShaderModule shaderModule;
	if ( vkCreateShaderModule( vk.device, &createInfo, NULL, &shaderModule ) != VK_SUCCESS ) {
		ri.Printf( PRINT_ALL, "Failed to create shader module\n" );
		return VK_NULL_HANDLE;
	}
	return shaderModule;
}

static VkShaderModule VK_LoadShaderFromFile( const char *filename ) {
	void *buffer = NULL;
	int len = ri.FS_ReadFile( filename, &buffer );
	if ( !buffer || len <= 0 ) {
		ri.Printf( PRINT_ALL, "Failed to load shader: %s\n", filename );
		return VK_NULL_HANDLE;
	}

	VkShaderModule module = VK_CreateShaderModuleFromSPV( (const uint32_t *)buffer, len );
	ri.FS_FreeFile( buffer );
	return module;
}

void VK_CreateShaderModules( void ) {
	// Load pre-compiled SPIR-V shaders from pk3/assets
	vk.singleTexVertShader = VK_LoadShaderFromFile( "shaders/single_tex_vert.spv" );
	vk.singleTexFragShader = VK_LoadShaderFromFile( "shaders/single_tex_frag.spv" );
	vk.multiTexFragShader = VK_LoadShaderFromFile( "shaders/multi_tex_frag.spv" );
	vk.gammaVertShader = VK_LoadShaderFromFile( "shaders/gamma_vert.spv" );
	vk.gammaFragShader = VK_LoadShaderFromFile( "shaders/gamma_frag.spv" );
	vk.blurVertShader = VK_LoadShaderFromFile( "shaders/glow_vert.spv" );
	vk.blurFragShader = VK_LoadShaderFromFile( "shaders/glow_frag.spv" );
	vk.blurMaskedFragShader = VK_LoadShaderFromFile( "shaders/glow_blur_masked_frag.spv" );
	vk.glowCompositeFragShader = VK_LoadShaderFromFile( "shaders/glow_composite_frag.spv" );
	vk.glowDownsampleFragShader = VK_LoadShaderFromFile( "shaders/glow_downsample_frag.spv" );
	vk.glowUpsampleFragShader = VK_LoadShaderFromFile( "shaders/glow_upsample_frag.spv" );

	// Load ray tracing shaders (optional - only if RT is supported)
	if ( vk.rayTracingSupported ) {
		vk.glowReflectRgenShader = VK_LoadShaderFromFile( "shaders/glow_reflect_rgen.spv" );
		vk.glowReflectRmissShader = VK_LoadShaderFromFile( "shaders/glow_reflect_rmiss.spv" );
		vk.glowReflectRchitShader = VK_LoadShaderFromFile( "shaders/glow_reflect_rchit.spv" );
		vk.glowReflectRahitShader = VK_LoadShaderFromFile( "shaders/glow_reflect_rahit.spv" );
		if ( !vk.glowReflectRgenShader || !vk.glowReflectRmissShader || !vk.glowReflectRchitShader ) {
			ri.Printf( PRINT_WARNING, "WARNING: RT glow shaders not found, disabling RT glow reflections\n" );
			vk.rayTracingSupported = qfalse;
		}
	}

	// Developer visibility: print which shader modules were successfully loaded
	ri.Printf( PRINT_DEVELOPER, "VK_CreateShaderModules: singleTexVert=%p singleTexFrag=%p multiTexFrag=%p\n",
		(void*)vk.singleTexVertShader, (void*)vk.singleTexFragShader, (void*)vk.multiTexFragShader );
	ri.Printf( PRINT_DEVELOPER, "VK_CreateShaderModules: gammaVert=%p gammaFrag=%p blurVert=%p blurFrag=%p glowComposite=%p\n",
		(void*)vk.gammaVertShader, (void*)vk.gammaFragShader, (void*)vk.blurVertShader, (void*)vk.blurFragShader, (void*)vk.glowCompositeFragShader );

	if ( vk.singleTexVertShader == VK_NULL_HANDLE || vk.singleTexFragShader == VK_NULL_HANDLE ) {
		ri.Error( ERR_FATAL, "Failed to load required SPIR-V shaders (single_tex_vert.spv / single_tex_frag.spv).\n"
			"Make sure the Vulkan shaders are compiled and included in assetsmv.pk3." );
	}
	if ( vk.multiTexFragShader == VK_NULL_HANDLE ) {
		ri.Printf( PRINT_WARNING, "WARNING: multi_tex_frag.spv not loaded, multitexture will use single-texture fallback\n" );
	}

	if ( vk.glowCompositeFragShader == VK_NULL_HANDLE ) {
		ri.Printf( PRINT_WARNING, "WARNING: glow_composite_frag.spv not loaded. Dynamic glow not enabled.\n" );
	}
}

// ============================================================
// Allocate descriptor set for a texture
// ============================================================
VkDescriptorSet VK_AllocateImageDescriptor( VkImageView view, VkSampler sampler ) {
	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = vk.descriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &vk.descriptorSetLayout;

	VkDescriptorSet descriptorSet;
	if ( vkAllocateDescriptorSets( vk.device, &allocInfo, &descriptorSet ) != VK_SUCCESS ) {
		ri.Printf( PRINT_ERROR, "ERROR: Failed to allocate descriptor set (pool exhausted, max %d). Possible descriptor leak!\n", VK_MAX_IMAGE_SLOTS );
		return VK_NULL_HANDLE;
	}

	VkDescriptorImageInfo imageInfo = {};
	imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	imageInfo.imageView = view;
	imageInfo.sampler = sampler;

	VkWriteDescriptorSet descriptorWrite = {};
	descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrite.dstSet = descriptorSet;
	descriptorWrite.dstBinding = 0;
	descriptorWrite.dstArrayElement = 0;
	descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descriptorWrite.descriptorCount = 1;
	descriptorWrite.pImageInfo = &imageInfo;

	vkUpdateDescriptorSets( vk.device, 1, &descriptorWrite, 0, NULL );

	return descriptorSet;
}

// ============================================================
// Main Vulkan init
// ============================================================
qboolean VK_Init( void ) {
	extern SDL_Window *screen;

	Com_Memset( &vk, 0, sizeof(vk) );
	vkEnabledExtensionsString[0] = '\0';

	ri.Printf( PRINT_ALL, "------- Vulkan Init -------\n" );

	if ( !VK_CreateInstance( screen ) ) {
		return qfalse;
	}

	if ( !SDL_Vulkan_CreateSurface( screen, vk.instance, &vk.surface ) ) {
		ri.Printf( PRINT_ALL, "SDL_Vulkan_CreateSurface failed: %s\n", SDL_GetError() );
		return qfalse;
	}

	if ( !VK_SelectPhysicalDevice() ) {
		return qfalse;
	}

	if ( !VK_FindQueueFamilies() ) {
		ri.Printf( PRINT_ALL, "Failed to find suitable queue families\n" );
		return qfalse;
	}

	if ( !VK_CreateDevice() ) {
		return qfalse;
	}

	VK_CreateCommandPool();
	VK_CreateSwapchain();

	// Set scene rendering format: HDR uses float16, otherwise match swapchain
	if ( r_hdr && r_hdr->integer ) {
		vk.sceneFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
		ri.Printf( PRINT_ALL, "HDR rendering enabled (R16G16B16A16_SFLOAT)\n" );
	} else {
		vk.sceneFormat = vk.swapchainFormat;
	}

	VK_CreateDepthBuffer();
	VK_CreateRenderPass();
	VK_CreateFramebuffers();
	VK_CreateCommandBuffers();
	VK_CreateSyncObjects();
	VK_CreateDynamicBuffers();
	VK_CreateStagingBuffer();
	VK_CreateSamplers();
	VK_CreateDescriptorPool();
	VK_CreateDescriptorSetLayout();
	VK_CreatePipelineLayout();
	VK_CreateShaderModules();

	// Create pipeline cache
	VkPipelineCacheCreateInfo cacheInfo = {};
	cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	vkCreatePipelineCache( vk.device, &cacheInfo, NULL, &vk.pipelineCache );

	// Create post-processing resources
	VK_CreateGlowResources();
	VK_CreateGlowReflectResources();
	VK_CreateGammaResources();
	VK_CreateShadowPipelines();

	// Update texture compression status
	glConfig.textureCompression = vk.bcSupported ? TC_S3TC : TC_NONE;
	if ( vk.bcSupported ) {
		ri.Printf( PRINT_ALL, "BC7 texture compression enabled\n" );
	}

	vk.initialized = qtrue;

	ri.Printf( PRINT_ALL, "Vulkan initialized successfully\n" );
	ri.Printf( PRINT_ALL, "  Device: %s\n", vk.deviceProperties.deviceName );
	ri.Printf( PRINT_ALL, "  Swapchain: %dx%d, %d images\n",
		vk.swapchainExtent.width, vk.swapchainExtent.height, vk.swapchainImageCount );

	return qtrue;
}

// ============================================================
// Flush pending deferred image resource destruction
// Called before shutdown or texture deletion to free queued resources
// ============================================================
void VK_FlushDeferredResources( void ) {
	if ( !vk.device ) return;
	vkDeviceWaitIdle( vk.device );
	for ( int f = 0; f < VK_NUM_COMMAND_BUFFERS; f++ ) {
		for ( int i = 0; i < vk.deferredImageCount[f]; i++ ) {
			vkDeferredImageDestroy_t *d = &vk.deferredImages[f][i];
			if ( d->descriptorSet != VK_NULL_HANDLE ) {
				vkFreeDescriptorSets( vk.device, vk.descriptorPool, 1, &d->descriptorSet );
			}
			if ( d->view != VK_NULL_HANDLE ) {
				vkDestroyImageView( vk.device, d->view, NULL );
			}
			if ( d->image != VK_NULL_HANDLE ) {
				vkDestroyImage( vk.device, d->image, NULL );
			}
			if ( d->memory != VK_NULL_HANDLE ) {
				vkFreeMemory( vk.device, d->memory, NULL );
			}
		}
		vk.deferredImageCount[f] = 0;
	}
}

// ============================================================
// Vulkan shutdown
// ============================================================
void VK_Shutdown( void ) {
	if ( !vk.initialized ) return;

	VK_FlushDeferredResources();

	// Destroy post-processing resources
	VK_DestroyGlowReflectResources();
	VK_DestroyGlowResources();
	VK_DestroyGammaResources();
	VK_DestroyShadowPipelines();

	// Destroy pipelines (hash table — iterate all slots)
	for ( int i = 0; i < VK_PIPELINE_HASH_SIZE; i++ ) {
		if ( vk.pipelines[i].occupied ) {
			vkDestroyPipeline( vk.device, vk.pipelines[i].pipeline, NULL );
		}
	}

	// Destroy shader modules
	if ( vk.singleTexVertShader ) vkDestroyShaderModule( vk.device, vk.singleTexVertShader, NULL );
	if ( vk.singleTexFragShader ) vkDestroyShaderModule( vk.device, vk.singleTexFragShader, NULL );
	if ( vk.multiTexFragShader ) vkDestroyShaderModule( vk.device, vk.multiTexFragShader, NULL );
	if ( vk.gammaVertShader ) vkDestroyShaderModule( vk.device, vk.gammaVertShader, NULL );
	if ( vk.gammaFragShader ) vkDestroyShaderModule( vk.device, vk.gammaFragShader, NULL );
	if ( vk.blurVertShader ) vkDestroyShaderModule( vk.device, vk.blurVertShader, NULL );
	if ( vk.blurFragShader ) vkDestroyShaderModule( vk.device, vk.blurFragShader, NULL );
	if ( vk.blurMaskedFragShader ) vkDestroyShaderModule( vk.device, vk.blurMaskedFragShader, NULL );
	if ( vk.glowCompositeFragShader ) vkDestroyShaderModule( vk.device, vk.glowCompositeFragShader, NULL );
	if ( vk.glowDownsampleFragShader ) vkDestroyShaderModule( vk.device, vk.glowDownsampleFragShader, NULL );
	if ( vk.glowUpsampleFragShader ) vkDestroyShaderModule( vk.device, vk.glowUpsampleFragShader, NULL );
	if ( vk.glowReflectRgenShader ) vkDestroyShaderModule( vk.device, vk.glowReflectRgenShader, NULL );
	if ( vk.glowReflectRmissShader ) vkDestroyShaderModule( vk.device, vk.glowReflectRmissShader, NULL );
	if ( vk.glowReflectRchitShader ) vkDestroyShaderModule( vk.device, vk.glowReflectRchitShader, NULL );
	if ( vk.glowReflectRahitShader ) vkDestroyShaderModule( vk.device, vk.glowReflectRahitShader, NULL );

	// Destroy pipeline cache / layout
	if ( vk.pipelineCache ) vkDestroyPipelineCache( vk.device, vk.pipelineCache, NULL );
	if ( vk.pipelineLayout ) vkDestroyPipelineLayout( vk.device, vk.pipelineLayout, NULL );
	if ( vk.uboDescriptorSetLayout ) vkDestroyDescriptorSetLayout( vk.device, vk.uboDescriptorSetLayout, NULL );
	if ( vk.descriptorSetLayout ) vkDestroyDescriptorSetLayout( vk.device, vk.descriptorSetLayout, NULL );
	if ( vk.descriptorPool ) vkDestroyDescriptorPool( vk.device, vk.descriptorPool, NULL );

	// Destroy samplers
	if ( vk.samplerMipRepeat ) vkDestroySampler( vk.device, vk.samplerMipRepeat, NULL );
	if ( vk.samplerMipClamp ) vkDestroySampler( vk.device, vk.samplerMipClamp, NULL );
	if ( vk.samplerNoMipRepeat ) vkDestroySampler( vk.device, vk.samplerNoMipRepeat, NULL );
	if ( vk.samplerNoMipClamp ) vkDestroySampler( vk.device, vk.samplerNoMipClamp, NULL );
	if ( vk.samplerNearest ) vkDestroySampler( vk.device, vk.samplerNearest, NULL );

	// Destroy static world buffers
	VK_DestroyStaticWorldBuffers();

	// Destroy dynamic buffers
	for ( int i = 0; i < VK_NUM_COMMAND_BUFFERS; i++ ) {
		vkUnmapMemory( vk.device, vk.dynBuffers[i].vertexMemory );
		vkDestroyBuffer( vk.device, vk.dynBuffers[i].vertexBuffer, NULL );
		vkFreeMemory( vk.device, vk.dynBuffers[i].vertexMemory, NULL );

		vkUnmapMemory( vk.device, vk.dynBuffers[i].indexMemory );
		vkDestroyBuffer( vk.device, vk.dynBuffers[i].indexBuffer, NULL );
		vkFreeMemory( vk.device, vk.dynBuffers[i].indexMemory, NULL );

		vkUnmapMemory( vk.device, vk.dynBuffers[i].uniformMemory );
		vkDestroyBuffer( vk.device, vk.dynBuffers[i].uniformBuffer, NULL );
		vkFreeMemory( vk.device, vk.dynBuffers[i].uniformMemory, NULL );
	}

	// Destroy staging buffer
	vkUnmapMemory( vk.device, vk.staging.memory );
	vkDestroyBuffer( vk.device, vk.staging.buffer, NULL );
	vkFreeMemory( vk.device, vk.staging.memory, NULL );

	// Destroy sync objects
	for ( int i = 0; i < VK_NUM_COMMAND_BUFFERS; i++ ) {
		vkDestroySemaphore( vk.device, vk.frames[i].imageAvailableSemaphore, NULL );
		vkDestroyFence( vk.device, vk.frames[i].fence, NULL );
	}

	for ( int i = 0; i < VK_MAX_SWAPCHAIN_IMAGES; i++ ) {
		if ( vk.renderFinishedSemaphores[i] ) {
			vkDestroySemaphore( vk.device, vk.renderFinishedSemaphores[i], NULL );
		}
	}

	// Destroy framebuffers
	for ( uint32_t i = 0; i < vk.swapchainImageCount; i++ ) {
		vkDestroyFramebuffer( vk.device, vk.framebuffers[i], NULL );
	}

	// Destroy render pass
	vkDestroyRenderPass( vk.device, vk.renderPass, NULL );
	if ( vk.renderPassLoad ) vkDestroyRenderPass( vk.device, vk.renderPassLoad, NULL );

	// Destroy depth buffer
	vkDestroyImageView( vk.device, vk.depthImageView, NULL );
	vkDestroyImage( vk.device, vk.depthImage, NULL );
	vkFreeMemory( vk.device, vk.depthImageMemory, NULL );

	// Destroy swapchain
	VK_DestroySwapchain();

	// Destroy command pool
	vkDestroyCommandPool( vk.device, vk.commandPool, NULL );

	// Destroy device
	vkDestroyDevice( vk.device, NULL );

	// Destroy surface
	vkDestroySurfaceKHR( vk.instance, vk.surface, NULL );

#ifndef NDEBUG
	// Destroy debug messenger
	if ( vk.debugMessenger ) {
		PFN_vkDestroyDebugUtilsMessengerEXT func = (PFN_vkDestroyDebugUtilsMessengerEXT)
			vkGetInstanceProcAddr(vk.instance, "vkDestroyDebugUtilsMessengerEXT");
		if (func != NULL) {
			func(vk.instance, vk.debugMessenger, NULL);
		}
	}
#endif

	// Destroy instance
	vkDestroyInstance( vk.instance, NULL );

	Com_Memset( &vk, 0, sizeof(vk) );
}


#endif // !DEDICATED
