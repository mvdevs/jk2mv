/*
===========================================================================
vk_draw.cpp - Vulkan draw commands, frame management, 2D mode,
viewport/scissor management, and vertex data upload.

Replaces the direct OpenGL draw calls (glDrawElements, glDrawArrays,
glBegin/glEnd, etc.) with Vulkan command buffer recording.
===========================================================================
*/

#include "tr_local.h"

#ifndef DEDICATED

#include "vk_local.h"

// ============================================================
// Begin a frame - acquire swapchain image, begin command buffer
// ============================================================
void VK_BeginFrame( void ) {
	if ( vk.frameStarted ) {
		return;
	}

	if ( vk.deferredFreeCount > 0 ) {
		vkDeviceWaitIdle( vk.device );
		vkFreeDescriptorSets( vk.device, vk.descriptorPool, vk.deferredFreeCount, vk.deferredFreeSets );
		vk.deferredFreeCount = 0;
	}

	vkFrame_t *frame = &vk.frames[vk.currentFrame];

	// Wait for this frame's fence
	vkWaitForFences( vk.device, 1, &frame->fence, VK_TRUE, UINT64_MAX );

	// Reset dynamic buffer offsets
	vk.dynBuffers[vk.currentFrame].vertexOffset = 0;
	vk.dynBuffers[vk.currentFrame].indexOffset = 0;
	vk.dynBuffers[vk.currentFrame].uniformOffset = 0;

	// Acquire next swapchain image
	// Use 1 second timeout instead of UINT64_MAX to avoid hangs on Wayland/Linux
	VkResult result = vkAcquireNextImageKHR( vk.device, vk.swapchain, 1000000000,
		frame->imageAvailableSemaphore, VK_NULL_HANDLE, &vk.currentSwapchainImage );

	if ( result == VK_ERROR_OUT_OF_DATE_KHR ) {
		// Need to recreate swapchain (window resize)
		VK_RecreateSwapchain();
		// Retry acquiring the swapchain image after recreation
		result = vkAcquireNextImageKHR( vk.device, vk.swapchain, 1000000000,
			frame->imageAvailableSemaphore, VK_NULL_HANDLE, &vk.currentSwapchainImage );
		if ( result != VK_SUCCESS && result != VK_TIMEOUT ) {
			// Still failed after recreation, abort this frame
			return;
		}
	}

	if ( result == VK_TIMEOUT || result == VK_NOT_READY ) {
		// Timeout or not ready - skip this frame gracefully
		return;
	} else if ( result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR ) {
		// Acquisition failed for other reasons
		return;
	}

	vkResetFences( vk.device, 1, &frame->fence );
	vkResetCommandBuffer( frame->commandBuffer, 0 );

	// Begin command buffer
	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer( frame->commandBuffer, &beginInfo );

	vk.renderPassActive = qfalse;
	vk.frameStarted = qtrue;
}


// ============================================================
// Begin the main render pass (clear)
// ============================================================
void VK_BeginRenderPass( void ) {
	if ( vk.renderPassActive ) return;

	// Lazily start the frame if it hasn't been started yet.
	// This avoids prematurely acquiring swapchain images during
	// R_SyncRenderThread flushes that happen during map loading.
	if ( !vk.frameStarted ) {
		VK_BeginFrame();
		if ( !vk.frameStarted ) return; // VK_BeginFrame failed (e.g. swapchain out of date)
	}

	vkFrame_t *frame = &vk.frames[vk.currentFrame];

	VkClearValue clearValues[2] = {};
	// Respect r_clear cvar: use diagnostic purple when enabled to reveal overdraw gaps
	extern cvar_t *r_clear;
	if ( r_clear && r_clear->integer ) {
		clearValues[0].color = { { 0.5f, 0.0f, 0.5f, 1.0f } }; // diagnostic purple
	} else {
		clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
	}
	clearValues[1].depthStencil = { 1.0f, 0 };

	VkRenderPassBeginInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassInfo.renderPass = vk.renderPass;
	renderPassInfo.framebuffer = vk.framebuffers[vk.currentSwapchainImage];
	renderPassInfo.renderArea.offset = { 0, 0 };
	renderPassInfo.renderArea.extent = vk.swapchainExtent;
	renderPassInfo.clearValueCount = 2;
	renderPassInfo.pClearValues = clearValues;

	vkCmdBeginRenderPass( frame->commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE );

	// Set default viewport and scissor so early draws don't fail
	VkViewport viewport = {};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = (float)vk.swapchainExtent.width;
	viewport.height = (float)vk.swapchainExtent.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport( frame->commandBuffer, 0, 1, &viewport );

	VkRect2D scissor = {};
	scissor.offset = { 0, 0 };
	scissor.extent = vk.swapchainExtent;
	vkCmdSetScissor( frame->commandBuffer, 0, 1, &scissor );

	vk.renderPassActive = qtrue;
}

// ============================================================
// Begin the main render pass but with LOAD ops (resume without clearing)
// ============================================================
void VK_BeginRenderPassLoad( void ) {
	if ( vk.renderPassActive ) return;

	if ( !vk.frameStarted ) {
		VK_BeginFrame();
		if ( !vk.frameStarted ) return;
	}

	vkFrame_t *frame = &vk.frames[vk.currentFrame];

	VkRenderPassBeginInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassInfo.renderPass = vk.renderPassLoad;
	renderPassInfo.framebuffer = vk.framebuffers[vk.currentSwapchainImage];
	renderPassInfo.renderArea.offset = { 0, 0 };
	renderPassInfo.renderArea.extent = vk.swapchainExtent;
	renderPassInfo.clearValueCount = 0;
	renderPassInfo.pClearValues = NULL;

	vkCmdBeginRenderPass( frame->commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE );

	VkViewport viewport = {};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = (float)vk.swapchainExtent.width;
	viewport.height = (float)vk.swapchainExtent.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport( frame->commandBuffer, 0, 1, &viewport );

	VkRect2D scissor = {};
	scissor.offset = { 0, 0 };
	scissor.extent = vk.swapchainExtent;
	vkCmdSetScissor( frame->commandBuffer, 0, 1, &scissor );

	vk.renderPassActive = qtrue;
}

// ============================================================
// End render pass (if active)
// ============================================================
void VK_EndRenderPass( void ) {
	if ( !vk.renderPassActive ) return;

	vkFrame_t *frame = &vk.frames[vk.currentFrame];
	vkCmdEndRenderPass( frame->commandBuffer );
	vk.renderPassActive = qfalse;
}

// ============================================================
// End a frame - end command buffer, submit, present
// ============================================================
void VK_EndFrame( void ) {
	if ( !vk.frameStarted ) {
		return;
	}

	vkFrame_t *frame = &vk.frames[vk.currentFrame];

	VK_EndRenderPass();

	vkEndCommandBuffer( frame->commandBuffer );

	// If VK_ReadPixels already submitted and signaled the fence this frame,
	// we need to reset it before submitting again
	if ( vk.pixelsCapturedThisFrame ) {
		vkResetFences( vk.device, 1, &frame->fence );
		vk.pixelsCapturedThisFrame = qfalse;
	}

	// Submit
	VkSemaphore waitSemaphores[] = { frame->imageAvailableSemaphore };
	VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
	VkSemaphore signalSemaphores[] = { vk.renderFinishedSemaphores[vk.currentSwapchainImage] };

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = waitSemaphores;
	submitInfo.pWaitDstStageMask = waitStages;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &frame->commandBuffer;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = signalSemaphores;

	VkResult submitResult = vkQueueSubmit( vk.graphicsQueue, 1, &submitInfo, frame->fence );
	if ( submitResult != VK_SUCCESS ) {
		// If submission fails, the fence won't be signaled, causing a deadlock next frame.
		// Fatal error is the only safe option here.
		ri.Error( ERR_FATAL, "VK_EndFrame: vkQueueSubmit failed: %d", submitResult );
	}

	// Present
	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = signalSemaphores;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &vk.swapchain;
	presentInfo.pImageIndices = &vk.currentSwapchainImage;

	VkResult result = vkQueuePresentKHR( vk.presentQueue, &presentInfo );

	if ( result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ) {
		VK_RecreateSwapchain();
	}

	vk.frameStarted = qfalse;
	vk.currentFrame = (vk.currentFrame + 1) % VK_NUM_COMMAND_BUFFERS;
}

// ============================================================
// Set viewport
// ============================================================
void VK_SetViewport( float x, float y, float width, float height, float minDepth, float maxDepth ) {
	if ( !vk.frameStarted ) return;
	VkCommandBuffer cmd = vk.frames[vk.currentFrame].commandBuffer;

	VkViewport viewport = {};
	viewport.x = x;
	viewport.y = y;
	viewport.width = width;
	viewport.height = height;
	viewport.minDepth = minDepth;
	viewport.maxDepth = maxDepth;

	vkCmdSetViewport( cmd, 0, 1, &viewport );
}

// ============================================================
// Set scissor rect
// ============================================================
void VK_SetScissor( int x, int y, int width, int height ) {
	if ( !vk.frameStarted ) return;
	VkCommandBuffer cmd = vk.frames[vk.currentFrame].commandBuffer;

	if ( x < 0 ) {
		width += x;
		x = 0;
	}
	if ( y < 0 ) {
		height += y;
		y = 0;
	}

	if ( width < 0 ) width = 0;
	if ( height < 0 ) height = 0;

	VkRect2D scissor = {};
	scissor.offset.x = x;
	scissor.offset.y = y;
	scissor.extent.width = (uint32_t)width;
	scissor.extent.height = (uint32_t)height;

	vkCmdSetScissor( cmd, 0, 1, &scissor );
}

// ============================================================
// Set 2D rendering mode (orthographic projection)
// ============================================================
void VK_Set2D( void ) {
	VK_BeginRenderPass();
	if ( !vk.frameStarted ) return;

	VK_SetViewport( 0, 0, (float)glConfig.vidWidth, (float)glConfig.vidHeight, 0.0f, 1.0f );
	VK_SetScissor( 0, 0, glConfig.vidWidth, glConfig.vidHeight );

	// Set up orthographic projection in push constants
	vkPushConstants_t pc = {};
	// Orthographic matrix: maps [0,640] x [0,480] to Vulkan NDC
	// Vulkan NDC: X [-1,1] left-to-right, Y [-1,1] top-to-bottom, Z [0,1]
	// Screen Y=0 should be at top (NDC Y=-1), Y=480 at bottom (NDC Y=1)
	// Z=0 must map to depth 0.0 (nearest) so 2D elements always pass
	// depth test even after the 3D scene has written to the depth buffer.
	// This matches the OpenGL glOrtho(0,640,480,0,0,1) behavior where
	// z=0 maps to NDC z=-1 -> depth 0.0.
	// Row 0
	pc.mvp[0] = 2.0f / 640.0f;
	pc.mvp[1] = 0.0f;
	pc.mvp[2] = 0.0f;
	pc.mvp[3] = 0.0f;
	// Row 1: Y maps [0,480] to [-1,1] (top-to-bottom)
	pc.mvp[4] = 0.0f;
	pc.mvp[5] = 2.0f / 480.0f;
	pc.mvp[6] = 0.0f;
	pc.mvp[7] = 0.0f;
	// Row 2
	pc.mvp[8] = 0.0f;
	pc.mvp[9] = 0.0f;
	pc.mvp[10] = 0.001f;
	pc.mvp[11] = 0.0f;
	// Row 3
	pc.mvp[12] = -1.0f;
	pc.mvp[13] = -1.0f;
	pc.mvp[14] = 0.0f;
	pc.mvp[15] = 1.0f;

	// Alpha test and tex env mode defaults
	pc.alphaTestValue = 0.0f;
	pc.texEnvMode = 0; // modulate

	// Set gamma correction value
	extern cvar_t *r_gamma;
	float gammaValue = r_gamma->value;
	if ( gammaValue < 0.5f ) gammaValue = 0.5f;
	if ( gammaValue > 3.0f ) gammaValue = 3.0f;
	pc.gamma = 1.0f / gammaValue;  // compute inverse gamma for shader

	VkCommandBuffer cmd = vk.frames[vk.currentFrame].commandBuffer;
	vkCmdPushConstants( cmd, vk.pipelineLayout,
		VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		0, sizeof(pc), &pc );
}

// ============================================================
// Set model-view-projection matrix
// ============================================================
void VK_SetMVP( const float *mvp ) {
	if ( !vk.frameStarted ) return;
	VkCommandBuffer cmd = vk.frames[vk.currentFrame].commandBuffer;

	// Convert OpenGL clip space to Vulkan clip space:
	// 1) Flip Y: OpenGL Y-up -> Vulkan Y-down (negate row 1)
	// 2) Z range: OpenGL [-1,1] -> Vulkan [0,1] (scale+bias row 2)
	float m[16];
	Com_Memcpy( m, mvp, sizeof(m) );

	// Flip Y (negate row 1 of the matrix)
	m[1]  = -m[1];
	m[5]  = -m[5];
	m[9]  = -m[9];
	m[13] = -m[13];

	// Z range: NewRow2 = 0.5 * Row2 + 0.5 * Row3
	m[2]  = 0.5f * m[2]  + 0.5f * m[3];
	m[6]  = 0.5f * m[6]  + 0.5f * m[7];
	m[10] = 0.5f * m[10] + 0.5f * m[11];
	m[14] = 0.5f * m[14] + 0.5f * m[15];

	// Push just the MVP matrix portion
	vkCmdPushConstants( cmd, vk.pipelineLayout,
		VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		0, sizeof(float) * 16, m );
}

// ============================================================
// Upload and draw indexed geometry from tess buffers
// ============================================================
void VK_DrawIndexed( int numVerts, const float *xyz, const float *texCoords0,
	const float *texCoords1, const byte *colors,
	int numIndexes, const glIndex_t *indexes )
{
	if ( numIndexes == 0 || numVerts == 0 ) return;
	if ( !vk.frameStarted ) return;

	VkCommandBuffer cmd = vk.frames[vk.currentFrame].commandBuffer;
	vkDynamicBuffers_t *dyn = &vk.dynBuffers[vk.currentFrame];

	// Check dynamic buffer overflow before writing
	VkDeviceSize vertexNeeded = numVerts * (sizeof(float) * 4 + sizeof(float) * 2 + sizeof(float) * 2 + 4);
	VkDeviceSize indexNeeded = numIndexes * sizeof(glIndex_t);
	if ( dyn->vertexOffset + vertexNeeded > VK_VERTEX_BUFFER_SIZE ||
		 dyn->indexOffset + indexNeeded > VK_INDEX_BUFFER_SIZE ) {
		ri.Printf( PRINT_WARNING, "VK_DrawIndexed: dynamic buffer overflow (verts=%d, idx=%d)\n", numVerts, numIndexes );
		return;
	}

	// Position data: always 16-byte stride (vec4) matching tess.xyz layout.
	// The vertex input format is R32G32B32_SFLOAT so only xyz are read; w is ignored.
	VkDeviceSize posStride = sizeof(float) * 4;
	VkDeviceSize posSize = numVerts * posStride;
	VkDeviceSize posOffset = dyn->vertexOffset;
	Com_Memcpy( dyn->vertexData + dyn->vertexOffset, xyz, posSize );
	dyn->vertexOffset += posSize;

	// Upload texcoord0
	VkDeviceSize tc0Size = numVerts * sizeof(float) * 2;
	VkDeviceSize tc0Offset = dyn->vertexOffset;
	if ( texCoords0 ) {
		Com_Memcpy( dyn->vertexData + dyn->vertexOffset, texCoords0, tc0Size );
	} else {
		Com_Memset( dyn->vertexData + dyn->vertexOffset, 0, tc0Size );
	}
	dyn->vertexOffset += tc0Size;

	// Upload texcoord1 (always - zeroed if not provided, for shader compatibility)
	VkDeviceSize tc1Size = numVerts * sizeof(float) * 2;
	VkDeviceSize tc1Offset = dyn->vertexOffset;
	if ( texCoords1 ) {
		Com_Memcpy( dyn->vertexData + dyn->vertexOffset, texCoords1, tc1Size );
	} else {
		Com_Memset( dyn->vertexData + dyn->vertexOffset, 0, tc1Size );
	}
	dyn->vertexOffset += tc1Size;

	// Upload colors (always 4 bytes per vertex: RGBA)
	VkDeviceSize colorSize = numVerts * 4;
	VkDeviceSize colorOffset = dyn->vertexOffset;
	if ( colors ) {
		Com_Memcpy( dyn->vertexData + dyn->vertexOffset, colors, colorSize );
	} else {
		// Default to white if no colors provided
		Com_Memset( dyn->vertexData + dyn->vertexOffset, 255, colorSize );
	}
	dyn->vertexOffset += colorSize;

	// Align to 4 bytes
	dyn->vertexOffset = (dyn->vertexOffset + 3) & ~3;

	// Upload indices
	VkDeviceSize idxSize = numIndexes * sizeof(glIndex_t);
	VkDeviceSize idxOffset = dyn->indexOffset;
	Com_Memcpy( dyn->indexData + dyn->indexOffset, indexes, idxSize );
	dyn->indexOffset += idxSize;
	dyn->indexOffset = (dyn->indexOffset + 3) & ~3;

	// Bind vertex buffers (always 4: pos, tc0, tc1, color)
	VkBuffer vertexBuffers[4];
	VkDeviceSize offsets[4];

	vertexBuffers[0] = dyn->vertexBuffer;
	offsets[0] = posOffset;

	vertexBuffers[1] = dyn->vertexBuffer;
	offsets[1] = tc0Offset;

	vertexBuffers[2] = dyn->vertexBuffer;
	offsets[2] = tc1Offset;

	vertexBuffers[3] = dyn->vertexBuffer;
	offsets[3] = colorOffset;

	vkCmdBindVertexBuffers( cmd, 0, 4, vertexBuffers, offsets );

	// Bind index buffer
	vkCmdBindIndexBuffer( cmd, dyn->indexBuffer, idxOffset,
		sizeof(glIndex_t) == 4 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16 );

	// Draw!
	vkCmdDrawIndexed( cmd, numIndexes, 1, 0, 0, 0 );
}

// ============================================================
// Draw a simple quad (for 2D drawing, stretch pic, etc.)
// ============================================================
void VK_DrawQuad( float x0, float y0, float x1, float y1,
	float s0, float t0, float s1, float t1,
	const byte *color )
{
	// Build 4 vertices (vec4 to match tess.xyz stride)
	float positions[4 * 4] = {
		x0, y0, 0.0f, 1.0f,
		x1, y0, 0.0f, 1.0f,
		x1, y1, 0.0f, 1.0f,
		x0, y1, 0.0f, 1.0f,
	};

	float texcoords[4 * 2] = {
		s0, t0,
		s1, t0,
		s1, t1,
		s0, t1,
	};

	byte colors[4 * 4];
	for ( int i = 0; i < 4; i++ ) {
		colors[i * 4 + 0] = color[0];
		colors[i * 4 + 1] = color[1];
		colors[i * 4 + 2] = color[2];
		colors[i * 4 + 3] = color[3];
	}

	glIndex_t indexes[6] = { 0, 1, 2, 0, 2, 3 };

	VK_DrawIndexed( 4, positions, texcoords, NULL, colors, 6, indexes );
}

// ============================================================
// Clear (color, depth, stencil)
// ============================================================
void VK_Clear( unsigned int clearBits, float r, float g, float b, float a ) {
	if ( !vk.renderPassActive ) {
		VK_BeginRenderPass();
	}
	if ( !vk.frameStarted ) return;

	VkCommandBuffer cmd = vk.frames[vk.currentFrame].commandBuffer;

	if ( clearBits & 0x01 ) { // color
		VkClearAttachment clearAttachment = {};
		clearAttachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		clearAttachment.colorAttachment = 0;
		clearAttachment.clearValue.color = { { r, g, b, a } };

		VkClearRect clearRect = {};
		clearRect.rect.offset = { 0, 0 };
		clearRect.rect.extent = vk.swapchainExtent;
		clearRect.baseArrayLayer = 0;
		clearRect.layerCount = 1;

		vkCmdClearAttachments( cmd, 1, &clearAttachment, 1, &clearRect );
	}

	if ( clearBits & 0x02 ) { // depth
		VkClearAttachment clearAttachment = {};
		clearAttachment.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		clearAttachment.clearValue.depthStencil = { 1.0f, 0 };

		VkClearRect clearRect = {};
		clearRect.rect.offset = { 0, 0 };
		clearRect.rect.extent = vk.swapchainExtent;
		clearRect.baseArrayLayer = 0;
		clearRect.layerCount = 1;

		vkCmdClearAttachments( cmd, 1, &clearAttachment, 1, &clearRect );
	}

	if ( clearBits & 0x04 ) { // stencil
		VkClearAttachment clearAttachment = {};
		clearAttachment.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
		clearAttachment.clearValue.depthStencil = { 1.0f, 0 };

		VkClearRect clearRect = {};
		clearRect.rect.offset = { 0, 0 };
		clearRect.rect.extent = vk.swapchainExtent;
		clearRect.baseArrayLayer = 0;
		clearRect.layerCount = 1;

		vkCmdClearAttachments( cmd, 1, &clearAttachment, 1, &clearRect );
	}
}

// ============================================================
// Read pixels from the framebuffer (screenshots)
// format: IMGFMT_BGR (3bpp) or IMGFMT_RGB (3bpp)
// ============================================================
void VK_ReadPixels( int x, int y, int width, int height, int format, byte *buffer ) {
	vkFrame_t *frame = &vk.frames[vk.currentFrame];
	VkCommandBuffer cmd = frame->commandBuffer;

	VK_EndRenderPass();

	VkImage srcImage = vk.swapchainImages[vk.currentSwapchainImage];

	// Transition swapchain image to transfer source
	// After VK_EndRenderPass, the render pass finalLayout transitions the image to PRESENT_SRC_KHR
	VkImageMemoryBarrier barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = srcImage;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;
	barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

	vkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier );

	// Copy to staging buffer
	VkBufferImageCopy region = {};
	region.bufferOffset = 0;
	region.bufferRowLength = 0;
	region.bufferImageHeight = 0;
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.mipLevel = 0;
	region.imageSubresource.baseArrayLayer = 0;
	region.imageSubresource.layerCount = 1;
	region.imageOffset = { x, y, 0 };
	region.imageExtent = { (uint32_t)width, (uint32_t)height, 1 };

	vkCmdCopyImageToBuffer( cmd, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		vk.staging.buffer, 1, &region );

	// No need to transition back - the next render pass uses initialLayout=UNDEFINED
	// which accepts any layout

	// End, submit with frame fence, and wait
	vkEndCommandBuffer( cmd );

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &cmd;
	vkQueueSubmit( vk.graphicsQueue, 1, &submitInfo, frame->fence );
	vkWaitForFences( vk.device, 1, &frame->fence, VK_TRUE, UINT64_MAX );

	// Convert from B8G8R8A8 (4bpp) to the requested 3bpp format
	int pixelCount = width * height;
	const byte *src = vk.staging.data;

	if ( format == 0x80E0 ) { // IMGFMT_BGR
		for ( int i = 0; i < pixelCount; i++ ) {
			buffer[i * 3 + 0] = src[i * 4 + 0]; // B
			buffer[i * 3 + 1] = src[i * 4 + 1]; // G
			buffer[i * 3 + 2] = src[i * 4 + 2]; // R
		}
	} else { // IMGFMT_RGB
		for ( int i = 0; i < pixelCount; i++ ) {
			buffer[i * 3 + 0] = src[i * 4 + 2]; // R
			buffer[i * 3 + 1] = src[i * 4 + 1]; // G
			buffer[i * 3 + 2] = src[i * 4 + 0]; // B
		}
	}

	// Re-begin the command buffer so remaining commands can proceed
	vkResetCommandBuffer( cmd, 0 );

	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer( cmd, &beginInfo );

	vk.renderPassActive = qfalse;
	vk.pixelsCapturedThisFrame = qtrue;
}

// ============================================================
// Recreate swapchain (window resize)
// ============================================================
void VK_RecreateSwapchain( void ) {
	vkDeviceWaitIdle( vk.device );

	// Destroy framebuffers
	for ( uint32_t i = 0; i < vk.swapchainImageCount; i++ ) {
		if ( vk.framebuffers[i] ) {
			vkDestroyFramebuffer( vk.device, vk.framebuffers[i], NULL );
			vk.framebuffers[i] = VK_NULL_HANDLE;
		}
	}

	// Destroy depth buffer
	if ( vk.depthImageView ) vkDestroyImageView( vk.device, vk.depthImageView, NULL );
	if ( vk.depthImage ) vkDestroyImage( vk.device, vk.depthImage, NULL );
	if ( vk.depthImageMemory ) vkFreeMemory( vk.device, vk.depthImageMemory, NULL );

	// Destroy render passes
	if ( vk.renderPass ) vkDestroyRenderPass( vk.device, vk.renderPass, NULL );
	if ( vk.renderPassLoad ) vkDestroyRenderPass( vk.device, vk.renderPassLoad, NULL );

	// Destroy old swapchain
	VK_DestroySwapchain();

	// Recreate
	VK_CreateSwapchain();
	VK_CreateDepthBuffer();
	VK_CreateRenderPass();
	VK_CreateFramebuffers();
}

// ============================================================
// Set depth range (for weapon rendering, etc.)
// ============================================================
void VK_SetDepthRange( float minDepth, float maxDepth ) {
	if ( !vk.frameStarted ) return;

	// Vulkan viewport includes depth range, so we just update via dynamic state
	VkCommandBuffer cmd = vk.frames[vk.currentFrame].commandBuffer;

	VkViewport viewport = {};
	viewport.x = (float)backEnd.viewParms.viewportX;
	viewport.y = (float)backEnd.viewParms.viewportY;
	viewport.width = (float)backEnd.viewParms.viewportWidth;
	viewport.height = (float)backEnd.viewParms.viewportHeight;
	viewport.minDepth = minDepth;
	viewport.maxDepth = maxDepth;

	vkCmdSetViewport( cmd, 0, 1, &viewport );
}

// ============================================================
// Push constants update
// ============================================================
void VK_SetPushConstants( const vkPushConstants_t *pc ) {
	if ( !vk.frameStarted ) return;
	VkCommandBuffer cmd = vk.frames[vk.currentFrame].commandBuffer;
	vkCmdPushConstants( cmd, vk.pipelineLayout,
		VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		0, sizeof(vkPushConstants_t), pc );
}

#endif // !DEDICATED
