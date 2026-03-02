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
// BC7 texture compression helpers
// ============================================================

// BC7 blocks: 16 bytes per 4x4 texel block
static VkDeviceSize VK_BC7BlocksSize( int w, int h ) {
	return (VkDeviceSize)((w + 3) / 4) * ((h + 3) / 4) * 16;
}

// 2x2 box filter downsample: src (srcW x srcH RGBA8) -> dst ((srcW/2) x (srcH/2) RGBA8)
// For odd dimensions, pad with last row/col
static void VK_BoxFilterMip( byte *dst, const byte *src, int srcW, int srcH ) {
	int dstW = (srcW + 1) / 2;
	int dstH = (srcH + 1) / 2;

	for ( int y = 0; y < dstH; y++ ) {
		for ( int x = 0; x < dstW; x++ ) {
			int sx = x * 2;
			int sy = y * 2;

			// Clamp source coordinates (pad edge pixels)
			int x0 = sx, x1 = (sx + 1 < srcW) ? sx + 1 : sx;
			int y0 = sy, y1 = (sy + 1 < srcH) ? sy + 1 : sy;

			// 2x2 box filter: average 4 neighbors
			const byte *p00 = src + (y0 * srcW + x0) * 4;
			const byte *p10 = src + (y0 * srcW + x1) * 4;
			const byte *p01 = src + (y1 * srcW + x0) * 4;
			const byte *p11 = src + (y1 * srcW + x1) * 4;

			byte *d = dst + (y * dstW + x) * 4;
			for ( int c = 0; c < 4; c++ ) {
				int sum = p00[c] + p10[c] + p01[c] + p11[c];
				d[c] = (byte)((sum + 2) / 4);  // rounded average
			}
		}
	}
}

// BC7 Mode 6 block encoder
// Encodes one 4x4 block of RGBA8 pixels into 16 bytes (BC7 mode 6)
static void VK_BC7EncodeMode6Block( const byte pixels[16 * 4], byte out[16] ) {
	// BC7 Mode 6: 1 subset, 7-bit endpoints for all channels, 4-bit indices, p-bits for endpoint expansion
	// Bit layout:
	// [6:0]: mode (0b1000000 for Mode 6)
	// [13:7]: R0[6:0]
	// [20:14]: R1[6:0]
	// [27:21]: G0[6:0]
	// [34:28]: G1[6:0]
	// [41:35]: B0[6:0]
	// [48:42]: B1[6:0]
	// [55:49]: A0[6:0]
	// [62:56]: A1[6:0]
	// [63]: P0
	// [64]: P1
	// [127:65]: 4-bit indices (first texel is anchor, 3-bit)

	// Find min/max per channel (bounding box endpoint selection)
	byte minR = 255, maxR = 0, minG = 255, maxG = 0, minB = 255, maxB = 0, minA = 255, maxA = 0;

	for ( int i = 0; i < 16; i++ ) {
		const byte *p = pixels + i * 4;
		if ( p[0] < minR ) minR = p[0];
		if ( p[0] > maxR ) maxR = p[0];
		if ( p[1] < minG ) minG = p[1];
		if ( p[1] > maxG ) maxG = p[1];
		if ( p[2] < minB ) minB = p[2];
		if ( p[2] > maxB ) maxB = p[2];
		if ( p[3] < minA ) minA = p[3];
		if ( p[3] > maxA ) maxA = p[3];
	}

	// Quantize endpoints to 7 bits, set p-bits = 1 to maximize range
	byte r0 = (minR >> 1) & 0x7F;
	byte r1 = (maxR >> 1) & 0x7F;
	byte g0 = (minG >> 1) & 0x7F;
	byte g1 = (maxG >> 1) & 0x7F;
	byte b0 = (minB >> 1) & 0x7F;
	byte b1 = (maxB >> 1) & 0x7F;
	byte a0 = (minA >> 1) & 0x7F;
	byte a1 = (maxA >> 1) & 0x7F;

	int p0 = 1, p1 = 1;  // p-bits expand endpoints: final = (ep << 1) | p

	// Reconstruct palette from endpoints (BC7 Mode 6 uses 16 weights)
	static const int BC7_WEIGHTS4[16] = { 0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64 };

	byte palR[16], palG[16], palB[16], palA[16];
	int r0_full = (r0 << 1) | p0;
	int r1_full = (r1 << 1) | p1;
	int g0_full = (g0 << 1) | p0;
	int g1_full = (g1 << 1) | p1;
	int b0_full = (b0 << 1) | p0;
	int b1_full = (b1 << 1) | p1;
	int a0_full = (a0 << 1) | p0;
	int a1_full = (a1 << 1) | p1;

	for ( int i = 0; i < 16; i++ ) {
		int w = BC7_WEIGHTS4[i];
		palR[i] = (byte)((r0_full * (64 - w) + r1_full * w + 32) >> 6);
		palG[i] = (byte)((g0_full * (64 - w) + g1_full * w + 32) >> 6);
		palB[i] = (byte)((b0_full * (64 - w) + b1_full * w + 32) >> 6);
		palA[i] = (byte)((a0_full * (64 - w) + a1_full * w + 32) >> 6);
	}

	// Assign best index for each pixel (minimize sum-of-squared-RGBA error)
	byte indices[16];
	for ( int i = 0; i < 16; i++ ) {
		const byte *pixel = pixels + i * 4;
		int bestIdx = 0;
		int bestErr = INT_MAX;

		for ( int j = 0; j < 16; j++ ) {
			int dr = (int)pixel[0] - palR[j];
			int dg = (int)pixel[1] - palG[j];
			int db = (int)pixel[2] - palB[j];
			int da = (int)pixel[3] - palA[j];
			int err = dr*dr + dg*dg + db*db + da*da;

			if ( err < bestErr ) {
				bestErr = err;
				bestIdx = j;
			}
		}
		indices[i] = bestIdx;
	}

	// Pack BC7 mode 6 block
	memset( out, 0, 16 );

	// Byte 0: mode indicator (bit 6 = 1)
	out[0] = 0x40;

	// Pack endpoints into bytes 0-7 (starting from bit 7)
	// Using bit-packing: shift endpoints and pack them sequentially
	uint64_t *block_lo = (uint64_t *)out;
	uint64_t *block_hi = (uint64_t *)(out + 8);

	// Lower 64 bits: mode(1) + R0(7) + R1(7) + G0(7) + G1(7) + B0(7) + B1(7) + A0(7) + A1(7) + p0(1) + p1(1) = 58 bits + 6 bits = 64 bits
	*block_lo = 0x40 |  // mode bit 6
		(((uint64_t)r0 & 0x7F) << 7) |
		(((uint64_t)r1 & 0x7F) << 14) |
		(((uint64_t)g0 & 0x7F) << 21) |
		(((uint64_t)g1 & 0x7F) << 28) |
		(((uint64_t)b0 & 0x7F) << 35) |
		(((uint64_t)b1 & 0x7F) << 42) |
		(((uint64_t)a0 & 0x7F) << 49) |
		(((uint64_t)a1 & 0x7F) << 56) |
		(((uint64_t)p0 & 0x01) << 63);

	// Higher 64 bits: p1(1) + indices(63)
	// First texel (anchor) uses 3 bits, rest use 4 bits
	uint64_t idx_bits = (((uint64_t)p1 & 0x01) << 0);
	idx_bits |= (((uint64_t)indices[0] & 0x07) << 1);  // 3-bit anchor
	for ( int i = 1; i < 16; i++ ) {
		idx_bits |= (((uint64_t)indices[i] & 0x0F) << (1 + 3 + (i - 1) * 4));
	}
	*block_hi = idx_bits;
}

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

	// Determine if we can use BC7 compression
	qboolean useBC7 = (vk.bcSupported && mipmap && width >= 4 && height >= 4) ? qtrue : qfalse;

	VkDeviceSize imageSize;
	if ( useBC7 ) {
		// BC7: all mip levels compressed
		imageSize = 0;
		for ( int m = 0; m < mipLevels; m++ ) {
			int mW = width >> m;
			int mH = height >> m;
			if ( mW < 1 ) mW = 1;
			if ( mH < 1 ) mH = 1;
			imageSize += VK_BC7BlocksSize( mW, mH );
		}
		format = VK_FORMAT_BC7_UNORM_BLOCK;
	} else {
		// RGBA8: base mip only (mipmaps generated on GPU)
		imageSize = (VkDeviceSize)width * height * 4;
	}

	if ( imageSize > VK_STAGING_BUFFER_SIZE ) {
		ri.Printf( PRINT_WARNING, "VK_CreateImage: image %dx%d (%llu bytes) exceeds staging buffer (%d bytes)\n",
			width, height, (unsigned long long)imageSize, VK_STAGING_BUFFER_SIZE );
		return;
	}

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

	// Upload data
	if ( useBC7 ) {
		// BC7: allocate persistent buffers for mip chain generation
		VkDeviceSize maxMipSize = (VkDeviceSize)width * height * 4;
		byte *mipA = (byte *)ri.Malloc( maxMipSize, TAG_RENDERER, qfalse );
		byte *mipB = (byte *)ri.Malloc( maxMipSize, TAG_RENDERER, qfalse );
		if ( !mipA || !mipB ) {
			ri.Printf( PRINT_WARNING, "VK_CreateImage: failed to allocate mip buffers\n" );
			if ( mipA ) ri.Free( mipA );
			if ( mipB ) ri.Free( mipB );
			return;
		}

		// Copy base mip
		Com_Memcpy( mipA, pic, width * height * 4 );

		// Generate and compress all mips
		VkDeviceSize stagingOffset = 0;
		VkBufferImageCopy regions[32];  // MAX_MIP_LEVELS
		int regionCount = 0;

		for ( int m = 0; m < mipLevels; m++ ) {
			int mW = width >> m;
			int mH = height >> m;
			if ( mW < 1 ) mW = 1;
			if ( mH < 1 ) mH = 1;

			// Get source and destination mips (ping-pong between mipA and mipB)
			byte *srcMip = (m == 0) ? mipA : ((m & 1) ? mipA : mipB);
			byte *nextMip = (m & 1) ? mipB : mipA;

			// If not base mip, generate it first
			if ( m > 0 ) {
				int prevW = width >> (m - 1);
				int prevH = height >> (m - 1);
				if ( prevW < 1 ) prevW = 1;
				if ( prevH < 1 ) prevH = 1;
				VK_BoxFilterMip( nextMip, srcMip, prevW, prevH );
				srcMip = nextMip;
			}

			// Encode each 4x4 block to BC7
			VkDeviceSize mipSize = VK_BC7BlocksSize( mW, mH );
			byte *stagingPtr = vk.staging.data + stagingOffset;

			for ( int by = 0; by < mH; by += 4 ) {
				for ( int bx = 0; bx < mW; bx += 4 ) {
					byte blockPixels[16 * 4];

					// Gather 4x4 pixels (pad if needed)
					for ( int py = 0; py < 4; py++ ) {
						for ( int px = 0; px < 4; px++ ) {
							int sx = bx + px;
							int sy = by + py;
							// Clamp to valid mip range
							if ( sx >= mW ) sx = mW - 1;
							if ( sy >= mH ) sy = mH - 1;

							const byte *srcPixel = srcMip + (sy * mW + sx) * 4;
							byte *dstPixel = blockPixels + (py * 4 + px) * 4;
							dstPixel[0] = srcPixel[0];
							dstPixel[1] = srcPixel[1];
							dstPixel[2] = srcPixel[2];
							dstPixel[3] = srcPixel[3];
						}
					}

					// Encode block
					VK_BC7EncodeMode6Block( blockPixels, stagingPtr );
					stagingPtr += 16;
				}
			}

			// Record region for this mip
			regions[regionCount].bufferOffset = stagingOffset;
			regions[regionCount].bufferRowLength = 0;
			regions[regionCount].bufferImageHeight = 0;
			regions[regionCount].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			regions[regionCount].imageSubresource.mipLevel = m;
			regions[regionCount].imageSubresource.baseArrayLayer = 0;
			regions[regionCount].imageSubresource.layerCount = 1;
			regions[regionCount].imageOffset = { 0, 0, 0 };
			regions[regionCount].imageExtent = { (uint32_t)mW, (uint32_t)mH, 1 };

			stagingOffset += mipSize;
			regionCount++;
		}

		ri.Free( mipA );
		ri.Free( mipB );

		// Upload all mips at once
		VK_TransitionImageLayout( image->vkImage.image, format,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevels );

		VkCommandBuffer commandBuffer = VK_BeginSingleTimeCommands();
		vkCmdCopyBufferToImage( commandBuffer, vk.staging.buffer, image->vkImage.image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, regionCount, regions );
		VK_EndSingleTimeCommands( commandBuffer );

		VK_TransitionImageLayout( image->vkImage.image, format,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, mipLevels );
	} else {
		// RGBA8: standard path with GPU mip generation
		// Copy data to staging buffer
		Com_Memcpy( vk.staging.data, pic, imageSize );

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

	if ( vk.frameStarted ) {
		// GPU may still reference these resources — defer all destruction
		int f = vk.currentFrame;
		if ( vk.deferredImageCount[f] < VK_MAX_DEFERRED_IMAGES ) {
			vkDeferredImageDestroy_t *d = &vk.deferredImages[f][vk.deferredImageCount[f]++];
			d->descriptorSet = vkImg->descriptorSet;
			d->view = vkImg->view;
			d->image = vkImg->image;
			d->memory = vkImg->memory;
		} else {
			ri.Printf( PRINT_WARNING, "WARNING: Deferred image destroy queue full (%d), resources leaked!\n", VK_MAX_DEFERRED_IMAGES );
		}
	} else {
		// Not rendering — safe to destroy immediately
		if ( vkImg->descriptorSet != VK_NULL_HANDLE ) {
			vkFreeDescriptorSets( vk.device, vk.descriptorPool, 1, &vkImg->descriptorSet );
		}
		if ( vkImg->view != VK_NULL_HANDLE ) {
			vkDestroyImageView( vk.device, vkImg->view, NULL );
		}
		if ( vkImg->image != VK_NULL_HANDLE ) {
			vkDestroyImage( vk.device, vkImg->image, NULL );
		}
		if ( vkImg->memory != VK_NULL_HANDLE ) {
			vkFreeMemory( vk.device, vkImg->memory, NULL );
		}
	}
	vkImg->descriptorSet = VK_NULL_HANDLE;
	vkImg->view = VK_NULL_HANDLE;
	vkImg->image = VK_NULL_HANDLE;
	vkImg->memory = VK_NULL_HANDLE;
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
	if ( !image ) {
		return;
	}
	if ( image->vkImage.descriptorSet == VK_NULL_HANDLE ) {
		ri.Printf( PRINT_DEVELOPER, "VK_BindImage: image '%s' has NULL descriptor set, skipping bind\n", image->imgName );
		return;
	}
	if ( !vk.frameStarted ) return;

	// Avoid redundant binds: most surfaces reuse the same textures across many draws.
	if ( textureUnit == 0 || textureUnit == 1 ) {
		if ( vk.boundTextureSets[textureUnit] == image->vkImage.descriptorSet ) {
			return;
		}
	}

	VkCommandBuffer cmd = vk.frames[vk.currentFrame].commandBuffer;
	vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipelineLayout,
		textureUnit, 1, &image->vkImage.descriptorSet, 0, NULL );

	if ( textureUnit == 0 || textureUnit == 1 ) {
		vk.boundTextureSets[textureUnit] = image->vkImage.descriptorSet;
	}
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

		qboolean clamped = (img->wrapClampMode != TEXWRAP_REPEAT) ? qtrue : qfalse;
		VkSampler newSampler = clamped ? vk.samplerMipClamp : vk.samplerMipRepeat;

		img->vkImage.sampler = newSampler;

		// Re-create descriptor set with new sampler (only descriptor set changes, not the image)
		if ( img->vkImage.descriptorSet != VK_NULL_HANDLE ) {
			if ( vk.frameStarted ) {
				int f = vk.currentFrame;
				if ( vk.deferredImageCount[f] < VK_MAX_DEFERRED_IMAGES ) {
					vkDeferredImageDestroy_t *d = &vk.deferredImages[f][vk.deferredImageCount[f]++];
					d->descriptorSet = img->vkImage.descriptorSet;
					d->view = VK_NULL_HANDLE;
					d->image = VK_NULL_HANDLE;
					d->memory = VK_NULL_HANDLE;
				}
			} else {
				vkFreeDescriptorSets( vk.device, vk.descriptorPool, 1, &img->vkImage.descriptorSet );
			}
		}
		img->vkImage.descriptorSet = VK_AllocateImageDescriptor( img->vkImage.view, newSampler );
	}
}

#endif // !DEDICATED
