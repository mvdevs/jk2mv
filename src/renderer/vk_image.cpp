/*
===========================================================================
vk_image.cpp - Vulkan texture/image management.

Replaces the OpenGL texture calls (glTexImage2D, glGenTextures, etc.)
with Vulkan image creation, staging uploads, and descriptor set allocation.
===========================================================================
*/

#include "tr_local.h"

#ifndef DEDICATED

#include "vk_local.h"

// ============================================================
// Create a Vulkan image (texture)
// ============================================================
void VK_CreateImage( image_t *image, const byte *pic, int width, int height, qboolean mipmap, qboolean clampToEdge ) {
	if ( !pic ) {
		ri.Printf( PRINT_WARNING, "VK_CreateImage: NULL pic data (%dx%d)\n", width, height );
		return;
	}
	if ( !vk.staging.data ) {
		ri.Printf( PRINT_WARNING, "VK_CreateImage: staging buffer not initialized\n" );
		return;
	}

	VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
	int mipLevels = 1;

	if ( mipmap ) {
		int max_dim = width > height ? width : height;
		mipLevels = 1;
		while ( max_dim > 1 ) {
			max_dim >>= 1;
			mipLevels++;
		}
	}

	VkDeviceSize imageSize = (VkDeviceSize)width * height * 4;

	if ( imageSize > VK_STAGING_BUFFER_SIZE ) {
		ri.Printf( PRINT_WARNING, "VK_CreateImage: image %dx%d (%llu bytes) exceeds staging buffer (%d bytes)\n",
			width, height, (unsigned long long)imageSize, VK_STAGING_BUFFER_SIZE );
		return;
	}

	// Copy data to staging buffer
	Com_Memcpy( vk.staging.data, pic, imageSize );

	// Create image
	VkImageCreateInfo imageInfo = {};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.extent.width = (uint32_t)width;
	imageInfo.extent.height = (uint32_t)height;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = mipLevels;
	imageInfo.arrayLayers = 1;
	imageInfo.format = format;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

	vkCreateImage( vk.device, &imageInfo, NULL, &image->vkImage.image );

	VkMemoryRequirements memReqs;
	vkGetImageMemoryRequirements( vk.device, image->vkImage.image, &memReqs );

	VkMemoryAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memReqs.size;
	allocInfo.memoryTypeIndex = VK_FindMemoryType( memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );

	vkAllocateMemory( vk.device, &allocInfo, NULL, &image->vkImage.memory );
	vkBindImageMemory( vk.device, image->vkImage.image, image->vkImage.memory, 0 );

	// Transition to transfer dst, copy from staging, transition to shader read
	VK_TransitionImageLayout( image->vkImage.image, format,
		VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevels );

	VK_CopyBufferToImage( vk.staging.buffer, image->vkImage.image, (uint32_t)width, (uint32_t)height );

	if ( mipLevels > 1 ) {
		VK_GenerateMipmaps( image->vkImage.image, format, width, height, mipLevels );
	} else {
		VK_TransitionImageLayout( image->vkImage.image, format,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1 );
	}

	// Create image view
	VkImageViewCreateInfo viewInfo = {};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = image->vkImage.image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = format;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = mipLevels;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = 1;

	vkCreateImageView( vk.device, &viewInfo, NULL, &image->vkImage.view );

	// Choose sampler
	if ( mipmap ) {
		image->vkImage.sampler = clampToEdge ? vk.samplerMipClamp : vk.samplerMipRepeat;
	} else {
		image->vkImage.sampler = clampToEdge ? vk.samplerNoMipClamp : vk.samplerNoMipRepeat;
	}

	// Allocate descriptor set
	image->vkImage.descriptorSet = VK_AllocateImageDescriptor( image->vkImage.view, image->vkImage.sampler );
}

// ============================================================
// Update existing texture (sub-image upload for cinematics/video)
// ============================================================
void VK_UpdateImage( image_t *image, const byte *pic, int width, int height ) {
	if ( !pic || !vk.staging.data ) {
		return;
	}
	VkDeviceSize imageSize = (VkDeviceSize)width * height * 4;
	if ( imageSize > VK_STAGING_BUFFER_SIZE ) {
		ri.Printf( PRINT_WARNING, "VK_UpdateImage: image %dx%d exceeds staging buffer\n", width, height );
		return;
	}
	Com_Memcpy( vk.staging.data, pic, imageSize );

	VkCommandBuffer cmd = VK_BeginSingleTimeCommands();

	// Transition to transfer dst
	VkImageMemoryBarrier barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image->vkImage.image;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;
	barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

	vkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
		0, 0, NULL, 0, NULL, 1, &barrier );

	// Copy
	VkBufferImageCopy region = {};
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.mipLevel = 0;
	region.imageSubresource.baseArrayLayer = 0;
	region.imageSubresource.layerCount = 1;
	region.imageExtent = { (uint32_t)width, (uint32_t)height, 1 };

	vkCmdCopyBufferToImage( cmd, vk.staging.buffer, image->vkImage.image,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region );

	// Transition back to shader read
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

	vkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		0, 0, NULL, 0, NULL, 1, &barrier );

	VK_EndSingleTimeCommands( cmd );
}

// ============================================================
// Destroy a Vulkan image (takes image_t* - for allocated images)
// ============================================================
void VK_DestroyImage( image_t *image ) {
	VK_DestroyImageResources( &image->vkImage );
}

void VK_DestroyImageResources( vkImage_t *vkImg ) {
	if ( vkImg->descriptorSet == VK_NULL_HANDLE && vkImg->view == VK_NULL_HANDLE && vkImg->image == VK_NULL_HANDLE && vkImg->memory == VK_NULL_HANDLE ) {
		return;
	}

	vkDeviceWaitIdle( vk.device );

	if ( vkImg->descriptorSet != VK_NULL_HANDLE ) {
		if ( vk.frameStarted ) {
			if ( vk.deferredFreeCount < 1024 ) {
				vk.deferredFreeSets[vk.deferredFreeCount++] = vkImg->descriptorSet;
			}
		} else {
			vkFreeDescriptorSets( vk.device, vk.descriptorPool, 1, &vkImg->descriptorSet );
		}
		vkImg->descriptorSet = VK_NULL_HANDLE;
	}
	if ( vkImg->view != VK_NULL_HANDLE ) {
		vkDestroyImageView( vk.device, vkImg->view, NULL );
		vkImg->view = VK_NULL_HANDLE;
	}
	if ( vkImg->image != VK_NULL_HANDLE ) {
		vkDestroyImage( vk.device, vkImg->image, NULL );
		vkImg->image = VK_NULL_HANDLE;
	}
	if ( vkImg->memory != VK_NULL_HANDLE ) {
		vkFreeMemory( vk.device, vkImg->memory, NULL );
		vkImg->memory = VK_NULL_HANDLE;
	}
}

// ============================================================
// Update sub-region of a Vulkan image (for lightmap atlas uploads)
// ============================================================
void VK_UpdateImageSubRegion( vkImage_t *vkImg, int width, int height, int xoff, int yoff, const byte *pixels ) {
	if ( !pixels || !vk.staging.data ) {
		return;
	}
	VkDeviceSize imageSize = (VkDeviceSize)width * height * 4;
	if ( imageSize > VK_STAGING_BUFFER_SIZE ) {
		ri.Printf( PRINT_WARNING, "VK_UpdateImageSubRegion: %dx%d exceeds staging buffer\n", width, height );
		return;
	}
	Com_Memcpy( vk.staging.data, pixels, imageSize );

	VkCommandBuffer cmd = VK_BeginSingleTimeCommands();

	// Transition to transfer dst
	VkImageMemoryBarrier barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = vkImg->image;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;
	barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

	vkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
		0, 0, NULL, 0, NULL, 1, &barrier );

	// Copy to sub-region
	VkBufferImageCopy region = {};
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.mipLevel = 0;
	region.imageSubresource.baseArrayLayer = 0;
	region.imageSubresource.layerCount = 1;
	region.imageOffset = { xoff, yoff, 0 };
	region.imageExtent = { (uint32_t)width, (uint32_t)height, 1 };

	vkCmdCopyBufferToImage( cmd, vk.staging.buffer, vkImg->image,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region );

	// Transition back to shader read
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

	vkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		0, 0, NULL, 0, NULL, 1, &barrier );

	VK_EndSingleTimeCommands( cmd );
}

// ============================================================
// Create a Vulkan image for render-to-texture (off-screen)
// Used for glow/bloom passes
// ============================================================
void VK_CreateRenderTargetImage( VkImage *image, VkDeviceMemory *memory, VkImageView *view,
	uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage )
{
	VkImageCreateInfo imageInfo = {};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.extent.width = width;
	imageInfo.extent.height = height;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.format = format;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageInfo.usage = usage;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

	vkCreateImage( vk.device, &imageInfo, NULL, image );

	VkMemoryRequirements memReqs;
	vkGetImageMemoryRequirements( vk.device, *image, &memReqs );

	VkMemoryAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memReqs.size;
	allocInfo.memoryTypeIndex = VK_FindMemoryType( memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );

	vkAllocateMemory( vk.device, &allocInfo, NULL, memory );
	vkBindImageMemory( vk.device, *image, *memory, 0 );

	VkImageViewCreateInfo viewInfo = {};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = *image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = format;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = 1;

	vkCreateImageView( vk.device, &viewInfo, NULL, view );
}

// ============================================================
// Bind texture for drawing (update descriptor set binding)
// ============================================================
void VK_BindImage( int textureUnit, image_t *image ) {
	if ( !image || image->vkImage.descriptorSet == VK_NULL_HANDLE ) {
		return;
	}
	if ( !vk.frameStarted ) return;

	VkCommandBuffer cmd = vk.frames[vk.currentFrame].commandBuffer;
	vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipelineLayout,
		textureUnit, 1, &image->vkImage.descriptorSet, 0, NULL );
}

// ============================================================
// Set texture filter mode (rebuild sampler if needed)
// ============================================================
void VK_SetTextureMode( const char *string ) {
	// Parse the mode string to determine Vulkan filter/mipmap settings.
	// Supported modes (matching GL texture mode strings):
	//   GL_NEAREST, GL_LINEAR,
	//   GL_NEAREST_MIPMAP_NEAREST, GL_LINEAR_MIPMAP_NEAREST,
	//   GL_NEAREST_MIPMAP_LINEAR, GL_LINEAR_MIPMAP_LINEAR

	VkFilter minFilter = VK_FILTER_LINEAR;
	VkFilter magFilter = VK_FILTER_LINEAR;
	VkSamplerMipmapMode mipMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

	if ( !Q_stricmp( string, "GL_NEAREST" ) ) {
		minFilter = VK_FILTER_NEAREST;
		magFilter = VK_FILTER_NEAREST;
		mipMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	} else if ( !Q_stricmp( string, "GL_LINEAR" ) ) {
		minFilter = VK_FILTER_LINEAR;
		magFilter = VK_FILTER_LINEAR;
		mipMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	} else if ( !Q_stricmp( string, "GL_NEAREST_MIPMAP_NEAREST" ) ) {
		minFilter = VK_FILTER_NEAREST;
		magFilter = VK_FILTER_NEAREST;
		mipMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	} else if ( !Q_stricmp( string, "GL_LINEAR_MIPMAP_NEAREST" ) ) {
		minFilter = VK_FILTER_LINEAR;
		magFilter = VK_FILTER_LINEAR;
		mipMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	} else if ( !Q_stricmp( string, "GL_NEAREST_MIPMAP_LINEAR" ) ) {
		minFilter = VK_FILTER_NEAREST;
		magFilter = VK_FILTER_NEAREST;
		mipMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	} else if ( !Q_stricmp( string, "GL_LINEAR_MIPMAP_LINEAR" ) ) {
		minFilter = VK_FILTER_LINEAR;
		magFilter = VK_FILTER_LINEAR;
		mipMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	} else {
		ri.Printf( PRINT_ALL, "VK_SetTextureMode: unknown mode '%s'\n", string );
		return;
	}

	ri.Printf( PRINT_ALL, "VK_SetTextureMode: %s\n", string );

	vkDeviceWaitIdle( vk.device );

	// Destroy and recreate the mip samplers with new filter modes
	if ( vk.samplerMipRepeat ) vkDestroySampler( vk.device, vk.samplerMipRepeat, NULL );
	if ( vk.samplerMipClamp ) vkDestroySampler( vk.device, vk.samplerMipClamp, NULL );

	VkSamplerCreateInfo samplerInfo = {};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = magFilter;
	samplerInfo.minFilter = minFilter;
	samplerInfo.mipmapMode = mipMode;
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

	// Update all mipped images to use the new samplers
	R_Images_StartIteration();
	image_t *img;
	while ( (img = R_Images_GetNextIteration()) != NULL ) {
		if ( img->vkImage.view == VK_NULL_HANDLE ) continue;
		if ( !img->mipmap ) continue;  // only mipped textures are affected

		qboolean clamped = (img->wrapClampMode != 0) ? qtrue : qfalse;
		VkSampler newSampler = clamped ? vk.samplerMipClamp : vk.samplerMipRepeat;

		img->vkImage.sampler = newSampler;

		// Re-create descriptor set with new sampler
		if ( img->vkImage.descriptorSet != VK_NULL_HANDLE ) {
			if ( vk.frameStarted ) {
				if ( vk.deferredFreeCount < 1024 ) {
					vk.deferredFreeSets[vk.deferredFreeCount++] = img->vkImage.descriptorSet;
				}
			} else {
				vkFreeDescriptorSets( vk.device, vk.descriptorPool, 1, &img->vkImage.descriptorSet );
			}
		}
		img->vkImage.descriptorSet = VK_AllocateImageDescriptor( img->vkImage.view, newSampler );
	}
}

#endif // !DEDICATED
