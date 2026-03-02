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

static void VK_CheckFxaaCvarAndRebuildIfNeeded( void ) {
	// r_fxaa controls FXAA quality preset; apply changes immediately.
	if ( !vk.device || !vk.gamma.enabled ) return;

	int desired = VK_ClampFxaaQuality( Cvar_VariableIntegerValue( "r_fxaa" ) );
	if ( desired != vk.gamma.fxaaQuality ) {
		VK_DestroyGammaResources();
		VK_CreateGammaResources();
	}
}

// ============================================================
// Begin a frame - acquire swapchain image, begin command buffer
// ============================================================
void VK_BeginFrame( void ) {
	if ( vk.frameStarted ) {
		return;
	}

	// Allow FXAA quality changes to take effect immediately.
	VK_CheckFxaaCvarAndRebuildIfNeeded();

	vk.acquireSemaphoreConsumedThisFrame = qfalse;

	vkFrame_t *frame = &vk.frames[vk.currentFrame];

	// Wait for this frame's fence - guarantees the GPU is done with this
	// frame's previous submission, so we can safely free its deferred sets.
	vkWaitForFences( vk.device, 1, &frame->fence, VK_TRUE, UINT64_MAX );

	// Destroy image resources that were deferred during this frame's previous cycle
	if ( vk.deferredImageCount[vk.currentFrame] > 0 ) {
		for ( int i = 0; i < vk.deferredImageCount[vk.currentFrame]; i++ ) {
			vkDeferredImageDestroy_t *d = &vk.deferredImages[vk.currentFrame][i];
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
		vk.deferredImageCount[vk.currentFrame] = 0;
	}

	// Reset dynamic buffer offsets
	vk.dynBuffers[vk.currentFrame].vertexOffset = 0;
	vk.dynBuffers[vk.currentFrame].indexOffset = 0;
	vk.dynBuffers[vk.currentFrame].uniformOffset = 0;

	// Acquire next swapchain image
	// Use a bounded timeout instead of UINT64_MAX to avoid indefinite hangs on some Wayland/Linux setups.
	// If acquisition times out (or is not ready), retry once with a longer timeout to avoid visual "stuck"
	// frames (old image remains presented).
	VkResult result = VK_NOT_READY;
	for ( int attempt = 0; attempt < 2; attempt++ ) {
		uint64_t timeoutNs = ( attempt == 0 ) ? 1000000000ull : 5000000000ull; // 1s then 5s
		result = vkAcquireNextImageKHR( vk.device, vk.swapchain, timeoutNs,
			frame->imageAvailableSemaphore, VK_NULL_HANDLE, &vk.currentSwapchainImage );
		if ( result != VK_TIMEOUT && result != VK_NOT_READY ) {
			break;
		}
	}
	if ( ( result == VK_TIMEOUT || result == VK_NOT_READY ) && com_developer && com_developer->integer ) {
		ri.Printf( PRINT_DEVELOPER, "VK_BeginFrame: vkAcquireNextImageKHR timed out/not ready\n" );
	}

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
		// Still timed out/not ready after retries - skip this frame gracefully.
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

	// Command buffer state cache must be reset after vkResetCommandBuffer.
	vk.boundTextureSets[0] = VK_NULL_HANDLE;
	vk.boundTextureSets[1] = VK_NULL_HANDLE;
	vk.boundPipeline = VK_NULL_HANDLE;
	vk.boundUBOOffset = ~0u;

	vk.renderPassActive = qfalse;
	vk.uiPassActive = qfalse;
	vk.frameStarted = qtrue;
	vk.gamma.appliedThisFrame = qfalse;
	vk.gamma.sceneRenderedThisFrame = qfalse;
	vk.gamma.uiFirstThisFrame = qfalse;
	vk.pipelineRenderPass = VK_NULL_HANDLE;

	// Write zeroed GPU params once at frame start so VK_DrawIndexed callers
	// can rebind this offset without allocating new UBO space per draw.
	{
		vkDynamicBuffers_t *dyn = &vk.dynBuffers[vk.currentFrame];
		VkDeviceSize alignment = vk.uboAlignment;
		dyn->uniformOffset = (int)((dyn->uniformOffset + alignment - 1) & ~(alignment - 1));
		vk.zeroUBOOffset[vk.currentFrame] = dyn->uniformOffset;
		Com_Memset( dyn->uniformData + dyn->uniformOffset, 0, GPU_PARAMS_BASE_SIZE );
		dyn->uniformOffset += (int)((GPU_PARAMS_BASE_SIZE + alignment - 1) & ~(alignment - 1));
	}

	// Pre-allocate zeroed vertex regions for tc1 and normals.
	// These are shared by all draws that don't need tc1/normals,
	// eliminating per-draw memset of zero data.
	{
		vkDynamicBuffers_t *dyn = &vk.dynBuffers[vk.currentFrame];

		// Zeroed tc1 region: SHADER_MAX_VERTEXES * vec2
		VkDeviceSize tc1Size = SHADER_MAX_VERTEXES * sizeof(float) * 2;
		vk.zeroTc1Offset[vk.currentFrame] = dyn->vertexOffset;
		Com_Memset( dyn->vertexData + dyn->vertexOffset, 0, tc1Size );
		dyn->vertexOffset += (int)tc1Size;

		// Zeroed normal region: SHADER_MAX_VERTEXES * vec4
		VkDeviceSize normSize = SHADER_MAX_VERTEXES * sizeof(float) * 4;
		vk.zeroNormOffset[vk.currentFrame] = dyn->vertexOffset;
		Com_Memset( dyn->vertexData + dyn->vertexOffset, 0, normSize );
		dyn->vertexOffset += (int)normSize;
	}
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
	if ( vk.gamma.enabled ) {
		renderPassInfo.renderPass = vk.gamma.sceneRenderPass;
		renderPassInfo.framebuffer = vk.gamma.sceneFramebuffer;
		vk.gamma.sceneRenderedThisFrame = qtrue;
	} else {
		renderPassInfo.renderPass = vk.renderPass;
		renderPassInfo.framebuffer = vk.framebuffers[vk.currentSwapchainImage];
	}
	renderPassInfo.renderArea.offset = { 0, 0 };
	renderPassInfo.renderArea.extent = vk.swapchainExtent;
	renderPassInfo.clearValueCount = 2;
	renderPassInfo.pClearValues = clearValues;

	vkCmdBeginRenderPass( frame->commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE );
	vk.pipelineRenderPass = renderPassInfo.renderPass;

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
	vk.uiPassActive = qfalse;
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
	if ( vk.gamma.enabled ) {
		renderPassInfo.renderPass = vk.gamma.sceneRenderPassLoad;
		renderPassInfo.framebuffer = vk.gamma.sceneFramebuffer;
		vk.gamma.sceneRenderedThisFrame = qtrue;
	} else {
		renderPassInfo.renderPass = vk.renderPassLoad;
		renderPassInfo.framebuffer = vk.framebuffers[vk.currentSwapchainImage];
	}
	renderPassInfo.renderArea.offset = { 0, 0 };
	renderPassInfo.renderArea.extent = vk.swapchainExtent;
	renderPassInfo.clearValueCount = 0;
	renderPassInfo.pClearValues = NULL;

	vkCmdBeginRenderPass( frame->commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE );
	vk.pipelineRenderPass = renderPassInfo.renderPass;

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
	vk.uiPassActive = qfalse;
}

// ============================================================
// Begin the swapchain overlay render pass (UI drawn after FXAA+gamma)
// ============================================================
static void VK_BeginOverlayPass( void ) {
	if ( !vk.gamma.enabled || !vk.gamma.overlayRenderPass ) {
		return;
	}

	// Already recording into overlay pass.
	if ( vk.renderPassActive && vk.uiPassActive ) {
		return;
	}

	// Frame must be started.
	if ( !vk.frameStarted ) {
		VK_BeginFrame();
		if ( !vk.frameStarted ) return;
	}

	// End any active pass first.
	if ( vk.renderPassActive ) {
		VK_EndRenderPass();
	}

	vkFrame_t *frame = &vk.frames[vk.currentFrame];

	// The overlay render pass assumes the swapchain image starts in PRESENT layout (loadOp=LOAD).
	// Fresh swapchain images (never presented/rendered) are in UNDEFINED; transition them once.
	if ( vk.swapchainImageLayouts[vk.currentSwapchainImage] == VK_IMAGE_LAYOUT_UNDEFINED ) {
		VK_TransitionImageLayout( vk.swapchainImages[vk.currentSwapchainImage], vk.swapchainFormat,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, 1, frame->commandBuffer );
		vk.swapchainImageLayouts[vk.currentSwapchainImage] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	}

	VkRenderPassBeginInfo rpBegin = {};
	rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	rpBegin.renderPass = vk.gamma.overlayRenderPass;
	rpBegin.framebuffer = vk.gamma.overlayFramebuffers[vk.currentSwapchainImage];
	rpBegin.renderArea.offset = { 0, 0 };
	rpBegin.renderArea.extent = vk.swapchainExtent;
	rpBegin.clearValueCount = 0;
	rpBegin.pClearValues = NULL;

	vkCmdBeginRenderPass( frame->commandBuffer, &rpBegin, VK_SUBPASS_CONTENTS_INLINE );
	vk.pipelineRenderPass = rpBegin.renderPass;

	// Set default viewport and scissor
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
	vk.uiPassActive = qtrue;
}

// ============================================================
// End render pass (if active)
// ============================================================
void VK_EndRenderPass( void ) {
	if ( !vk.renderPassActive ) return;

	vkFrame_t *frame = &vk.frames[vk.currentFrame];
	vkCmdEndRenderPass( frame->commandBuffer );
	vk.renderPassActive = qfalse;
	vk.uiPassActive = qfalse;
}

// ============================================================
// End a frame - end command buffer, submit, present
// ============================================================
void VK_EndFrame( void ) {
	if ( !vk.frameStarted ) {
		return;
	}

	vkFrame_t *frame = &vk.frames[vk.currentFrame];

	// If gamma is enabled but the gamma pass wasn't applied yet (scene render pass still active),
	// force the gamma pass now to ensure the swapchain image is written.
	if ( vk.gamma.enabled && vk.renderPassActive && !vk.gamma.appliedThisFrame ) {
		VK_ApplyGammaCorrection();
	}

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
	// Be conservative: ensure *no* commands execute before the acquired swapchain image is available.
	// This avoids issues when we record explicit swapchain layout transitions (FXAA/gamma path) that
	// may otherwise run earlier than COLOR_ATTACHMENT_OUTPUT.
	VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT };
	VkSemaphore signalSemaphores[] = { vk.renderFinishedSemaphores[vk.currentSwapchainImage] };

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	if ( vk.acquireSemaphoreConsumedThisFrame ) {
		submitInfo.waitSemaphoreCount = 0;
		submitInfo.pWaitSemaphores = NULL;
		submitInfo.pWaitDstStageMask = NULL;
	} else {
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = waitSemaphores;
		submitInfo.pWaitDstStageMask = waitStages;
	}
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

	// Presented images are in PRESENT layout.
	if ( vk.currentSwapchainImage < VK_MAX_SWAPCHAIN_IMAGES ) {
		vk.swapchainImageLayouts[vk.currentSwapchainImage] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	}

	vk.frameStarted = qfalse;
	// Reset gamma frame-state flags now that the frame is fully submitted.
	// If a stale VK_Set2D call arrives (e.g. from R_SyncRenderThread) before
	// VK_BeginFrame is called for the next frame, it will see clean state and
	// correctly set uiFirstThisFrame rather than triggering the wrong code path
	// (FXAA on empty scene + UI in overlay) due to stale sceneRenderedThisFrame.
	vk.gamma.appliedThisFrame = qfalse;
	vk.gamma.sceneRenderedThisFrame = qfalse;
	vk.gamma.uiFirstThisFrame = qfalse;
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
	if ( vk.gamma.enabled ) {
		// Gamma path:
		// - If the frame starts with 3D (typical gameplay): apply gamma/FXAA to swapchain, then draw UI in overlay.
		// - If the frame starts with 2D (menus/loading): UI may interleave 2D and 3D (model panels). In that case,
		//   keep drawing into the offscreen scene target for the whole frame and apply gamma at end-of-frame.
		if ( !vk.gamma.sceneRenderedThisFrame && !vk.gamma.appliedThisFrame ) {
			vk.gamma.uiFirstThisFrame = qtrue;
		}

		if ( vk.gamma.uiFirstThisFrame ) {
			// Ensure we are recording into the scene render pass (offscreen, with depth) rather than the swapchain overlay.
			if ( vk.renderPassActive && vk.uiPassActive ) {
				VK_EndRenderPass();
			}
			VK_BeginRenderPass();
		} else {
			// Normal gameplay HUD: 3D already rendered into offscreen scene, then FXAA+gamma, then overlay UI.
			if ( !vk.gamma.appliedThisFrame ) {
				VK_ApplyGammaCorrection();
			}
			VK_BeginOverlayPass();
		}
	} else {
		VK_BeginRenderPass();
	}
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
	pc.texEnvMode = 0.0f; // modulate
	pc.alphaTestFunc = 0.0f;

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

	// Check dynamic buffer overflow before writing (pos + tc0 + color only)
	VkDeviceSize vertexNeeded = numVerts * (sizeof(float) * 4 + sizeof(float) * 2 + 4);
	VkDeviceSize indexNeeded = numIndexes * sizeof(glIndex_t);
	if ( dyn->vertexOffset + vertexNeeded > VK_VERTEX_BUFFER_SIZE ||
		 dyn->indexOffset + indexNeeded > VK_INDEX_BUFFER_SIZE ) {
		ri.Printf( PRINT_WARNING, "VK_DrawIndexed: dynamic buffer overflow (verts=%d, idx=%d)\n", numVerts, numIndexes );
		return;
	}

	// Position data: always 16-byte stride (vec4) matching tess.xyz layout.
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

	// tc1 and normals: use pre-zeroed regions from frame start (no upload)
	VkDeviceSize tc1Offset = vk.zeroTc1Offset[vk.currentFrame];
	VkDeviceSize normOffset = vk.zeroNormOffset[vk.currentFrame];

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

	// Bind vertex buffers (5: pos, tc0, tc1, color, normal)
	VkBuffer vertexBuffers[5];
	VkDeviceSize offsets[5];

	vertexBuffers[0] = dyn->vertexBuffer;
	offsets[0] = posOffset;

	vertexBuffers[1] = dyn->vertexBuffer;
	offsets[1] = tc0Offset;

	vertexBuffers[2] = dyn->vertexBuffer;
	offsets[2] = tc1Offset;

	vertexBuffers[3] = dyn->vertexBuffer;
	offsets[3] = colorOffset;

	vertexBuffers[4] = dyn->vertexBuffer;
	offsets[4] = normOffset;

	vkCmdBindVertexBuffers( cmd, 0, 5, vertexBuffers, offsets );

	// Bind index buffer
	vkCmdBindIndexBuffer( cmd, dyn->indexBuffer, idxOffset,
		sizeof(glIndex_t) == 4 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16 );

	// Re-bind the zeroed GPU params written at frame start (no new UBO upload)
	{
		uint32_t dynamicOffset = (uint32_t)vk.zeroUBOOffset[vk.currentFrame];
		if ( dynamicOffset != vk.boundUBOOffset ) {
			vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipelineLayout,
				2, 1, &vk.uboDescriptorSets[vk.currentFrame], 1, &dynamicOffset );
			vk.boundUBOOffset = dynamicOffset;
		}
	}

	// Draw!
	vkCmdDrawIndexed( cmd, numIndexes, 1, 0, 0, 0 );
}

// ============================================================
// Upload and draw indexed geometry with normals
// Same as VK_DrawIndexed but also uploads per-vertex normals (binding 4)
// ============================================================
void VK_DrawIndexedWithNormals( int numVerts, const float *xyz, const float *normals,
	const float *texCoords0, const float *texCoords1,
	const byte *colors, int numIndexes, const glIndex_t *indexes )
{
	if ( numIndexes == 0 || numVerts == 0 ) return;
	if ( !vk.frameStarted ) return;

	VkCommandBuffer cmd = vk.frames[vk.currentFrame].commandBuffer;
	vkDynamicBuffers_t *dyn = &vk.dynBuffers[vk.currentFrame];

	// Check dynamic buffer overflow (pos + tc0 + tc1? + color + normals)
	VkDeviceSize vertexNeeded = numVerts * (sizeof(float) * 4 + sizeof(float) * 2 + 4 + sizeof(float) * 4);
	if ( texCoords1 ) vertexNeeded += numVerts * sizeof(float) * 2;
	VkDeviceSize indexNeeded = numIndexes * sizeof(glIndex_t);
	if ( dyn->vertexOffset + vertexNeeded > VK_VERTEX_BUFFER_SIZE ||
		 dyn->indexOffset + indexNeeded > VK_INDEX_BUFFER_SIZE ) {
		ri.Printf( PRINT_WARNING, "VK_DrawIndexedWithNormals: dynamic buffer overflow\n" );
		return;
	}

	// Upload position data (vec4 stride)
	VkDeviceSize posSize = numVerts * sizeof(float) * 4;
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

	// Upload texcoord1: if provided upload, otherwise use pre-zeroed region
	VkDeviceSize tc1Offset;
	if ( texCoords1 ) {
		tc1Offset = dyn->vertexOffset;
		Com_Memcpy( dyn->vertexData + dyn->vertexOffset, texCoords1, numVerts * sizeof(float) * 2 );
		dyn->vertexOffset += numVerts * sizeof(float) * 2;
	} else {
		tc1Offset = vk.zeroTc1Offset[vk.currentFrame];
	}

	// Upload colors
	VkDeviceSize colorSize = numVerts * 4;
	VkDeviceSize colorOffset = dyn->vertexOffset;
	if ( colors ) {
		Com_Memcpy( dyn->vertexData + dyn->vertexOffset, colors, colorSize );
	} else {
		Com_Memset( dyn->vertexData + dyn->vertexOffset, 255, colorSize );
	}
	dyn->vertexOffset += colorSize;

	// Upload normals (binding 4, vec4 stride matching tess.normal)
	VkDeviceSize normSize = numVerts * sizeof(float) * 4;
	VkDeviceSize normOffset = dyn->vertexOffset;
	if ( normals ) {
		Com_Memcpy( dyn->vertexData + dyn->vertexOffset, normals, normSize );
	} else {
		normOffset = vk.zeroNormOffset[vk.currentFrame];
	}
	if ( normals ) dyn->vertexOffset += normSize;

	// Align to 4 bytes
	dyn->vertexOffset = (dyn->vertexOffset + 3) & ~3;

	// Upload indices
	VkDeviceSize idxSize = numIndexes * sizeof(glIndex_t);
	VkDeviceSize idxOffset = dyn->indexOffset;
	Com_Memcpy( dyn->indexData + dyn->indexOffset, indexes, idxSize );
	dyn->indexOffset += idxSize;
	dyn->indexOffset = (dyn->indexOffset + 3) & ~3;

	// Bind vertex buffers (5: pos, tc0, tc1, color, normal)
	VkBuffer vertexBuffers[5];
	VkDeviceSize offsets[5];

	vertexBuffers[0] = dyn->vertexBuffer;  offsets[0] = posOffset;
	vertexBuffers[1] = dyn->vertexBuffer;  offsets[1] = tc0Offset;
	vertexBuffers[2] = dyn->vertexBuffer;  offsets[2] = tc1Offset;
	vertexBuffers[3] = dyn->vertexBuffer;  offsets[3] = colorOffset;
	vertexBuffers[4] = dyn->vertexBuffer;  offsets[4] = normOffset;

	vkCmdBindVertexBuffers( cmd, 0, 5, vertexBuffers, offsets );

	// Bind index buffer
	vkCmdBindIndexBuffer( cmd, dyn->indexBuffer, idxOffset,
		sizeof(glIndex_t) == 4 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16 );

	vkCmdDrawIndexed( cmd, numIndexes, 1, 0, 0, 0 );
}

// ============================================================
// Write GPU params UBO and bind the descriptor set
// ============================================================
qboolean VK_UpdateGPUParams( const gpuParams_t *params ) {
	if ( !vk.frameStarted ) return qfalse;

	vkDynamicBuffers_t *dyn = &vk.dynBuffers[vk.currentFrame];

	// Align the offset for UBO access
	VkDeviceSize alignment = vk.uboAlignment;
	dyn->uniformOffset = (int)((dyn->uniformOffset + alignment - 1) & ~(alignment - 1));

	// Determine how much data to write based on flags
	int writeSize = GPU_PARAMS_BASE_SIZE;
	if (params->gpuFlags & GPU_FLAG_SKINNING) {
		writeSize = GPU_PARAMS_FULL_SIZE;
	} else if (params->gpuFlags & GPU_FLAG_MULTI_DLIGHT_PASS) {
		// Write base params + dlight data packed in boneMatrices region
		// Each dlight = 2 vec4s (8 floats). numDlights stored in pad0.
		int numDlights = (int)params->pad0;
		if (numDlights < 0) numDlights = 0;
		if (numDlights > 32) numDlights = 32;
		writeSize = GPU_PARAMS_BASE_SIZE + numDlights * 8 * (int)sizeof(float);
		// Align to 16 bytes (vec4 boundary)
		writeSize = (writeSize + 15) & ~15;
		if (writeSize > GPU_PARAMS_FULL_SIZE) writeSize = GPU_PARAMS_FULL_SIZE;
	}

	// Check overflow: need GPU_PARAMS_FULL_SIZE addressable bytes at this offset
	// (descriptor range covers full size even for non-skinned draws)
	if ( dyn->uniformOffset + GPU_PARAMS_FULL_SIZE > VK_UNIFORM_BUFFER_SIZE ) {
		ri.Printf( PRINT_WARNING, "VK_UpdateGPUParams: uniform buffer overflow\n" );
		return qfalse;
	}

	// Copy data to mapped uniform buffer (only write what's needed)
	Com_Memcpy( dyn->uniformData + dyn->uniformOffset, params, writeSize );

	// Bind the UBO descriptor set (set 2) with dynamic offset
	uint32_t dynamicOffset = (uint32_t)dyn->uniformOffset;
	VkCommandBuffer cmd = vk.frames[vk.currentFrame].commandBuffer;
	vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipelineLayout,
		2, 1, &vk.uboDescriptorSets[vk.currentFrame], 1, &dynamicOffset );
	vk.boundUBOOffset = dynamicOffset;

	// Advance by aligned write size (not full size — non-skinned draws save space)
	dyn->uniformOffset += (int)((writeSize + alignment - 1) & ~(alignment - 1));
	return qtrue;
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
	if ( !vk.frameStarted ) {
		return;
	}
	vkFrame_t *frame = &vk.frames[vk.currentFrame];
	VkCommandBuffer cmd = frame->commandBuffer;

	// If we render the scene offscreen (gamma enabled), ensure the swapchain image
	// contains the final scene before we try to copy from it.
	if ( vk.gamma.enabled ) {
		VK_ApplyGammaCorrection();
	}

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
	if ( (size_t)width * height * 4 > VK_STAGING_BUFFER_SIZE ) {
		ri.Printf( PRINT_ALL, "VK_ReadPixels: image too large for staging buffer (%dx%d)\n", width, height );
	} else {
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
	}

	// Transition swapchain image back to PRESENT_SRC_KHR so it can be presented
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	barrier.dstAccessMask = 0;

	vkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1, &barrier );

	// Track swapchain layout for subsequent passes in this frame.
	if ( vk.currentSwapchainImage < VK_MAX_SWAPCHAIN_IMAGES ) {
		vk.swapchainImageLayouts[vk.currentSwapchainImage] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	}

	// End, submit with frame fence, and wait
	vkEndCommandBuffer( cmd );

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	// IMPORTANT: vkAcquireNextImageKHR signals imageAvailableSemaphore. If we submit
	// a mid-frame command buffer (screenshots), we must wait on it here; and since
	// binary semaphores are consumed by a wait, VK_EndFrame must not wait on it again.
	VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
	if ( !vk.acquireSemaphoreConsumedThisFrame ) {
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &frame->imageAvailableSemaphore;
		submitInfo.pWaitDstStageMask = &waitStage;
		vk.acquireSemaphoreConsumedThisFrame = qtrue;
	} else {
		submitInfo.waitSemaphoreCount = 0;
		submitInfo.pWaitSemaphores = NULL;
		submitInfo.pWaitDstStageMask = NULL;
	}
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &cmd;
	vkQueueSubmit( vk.graphicsQueue, 1, &submitInfo, frame->fence );
	// Ensure we wait for the fence to be signaled. Check return value just in case.
	if ( vkWaitForFences( vk.device, 1, &frame->fence, VK_TRUE, UINT64_MAX ) != VK_SUCCESS ) {
		ri.Printf( PRINT_WARNING, "VK_ReadPixels: vkWaitForFences failed\n" );
	}

	// Copy data from staging buffer (uncached/WC memory) to temporary cached buffer FIRST.
	// Reading byte-by-byte from uncached memory is extremely slow (can take seconds for 4K).
	// memcpy is optimized for linear reads even from WC memory.
	size_t dataSize = (size_t)width * height * 4;
	byte *tempBuf = (byte *)ri.Hunk_AllocateTempMemory( dataSize );
	
	if ( tempBuf ) {
		// Use a simple loop for safety or Memcpy if robust
		Com_Memcpy( tempBuf, vk.staging.data, dataSize );
		
		const byte *src = tempBuf;
		int srcStride = width * 4;
		int dstStride = width * 3;

		if ( format == IMGFMT_BGR ) {
			for ( int row = 0; row < height; row++ ) {
				const byte *srcRow = src + (height - 1 - row) * srcStride;
				byte *dstRow = buffer + row * dstStride;
				for ( int col = 0; col < width; col++ ) {
					dstRow[col * 3 + 0] = srcRow[col * 4 + 0]; // B
					dstRow[col * 3 + 1] = srcRow[col * 4 + 1]; // G
					dstRow[col * 3 + 2] = srcRow[col * 4 + 2]; // R
				}
			}
		} else { // IMGFMT_RGB
			for ( int row = 0; row < height; row++ ) {
				const byte *srcRow = src + (height - 1 - row) * srcStride;
				byte *dstRow = buffer + row * dstStride;
				for ( int col = 0; col < width; col++ ) {
					dstRow[col * 3 + 0] = srcRow[col * 4 + 2]; // R
					dstRow[col * 3 + 1] = srcRow[col * 4 + 1]; // G
					dstRow[col * 3 + 2] = srcRow[col * 4 + 0]; // B
				}
			}
		}
		
		ri.Hunk_FreeTempMemory( tempBuf );
	} else {
		ri.Printf( PRINT_WARNING, "VK_ReadPixels: failed to allocate temp buffer\n" );
	}

	// Re-begin the command buffer so remaining commands can proceed
	vkResetCommandBuffer( cmd, 0 );

	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer( cmd, &beginInfo );

	// Reset command-buffer state cache after reset/re-begin.
	vk.boundTextureSets[0] = VK_NULL_HANDLE;
	vk.boundTextureSets[1] = VK_NULL_HANDLE;
	vk.boundPipeline = VK_NULL_HANDLE;
	vk.boundUBOOffset = ~0u;

	vk.renderPassActive = qfalse;
	vk.uiPassActive = qfalse;
	vk.pixelsCapturedThisFrame = qtrue;
}

// ============================================================
// Recreate swapchain (window resize)
// ============================================================
void VK_RecreateSwapchain( void ) {
	vkDeviceWaitIdle( vk.device );

	// Post-process resources depend on swapchain extent, swapchain image views, and depth buffer.
	// Recreate them around swapchain recreation.
	VK_DestroyGlowReflectResources();
	VK_DestroyGlowResources();
	VK_DestroyGammaResources();

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

	VK_CreateGlowResources();
	VK_CreateGlowReflectResources();
	VK_CreateGammaResources();
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


// ============================================================
// Static World VBO Management
// ============================================================

/*
=================
VK_CreateStaticWorldBuffers

Creates device-local vertex and index buffers for static world geometry.
vertexData contains all attribute sections packed contiguously.
indexData contains all indices as uint32.
=================
*/
void VK_CreateStaticWorldBuffers( int totalVertices, int totalIndexes,
	const byte *vertexData, VkDeviceSize vertexDataSize,
	const byte *indexData, VkDeviceSize indexDataSize )
{
	if ( totalVertices == 0 || totalIndexes == 0 ) {
		return;
	}

	VkResult result;

	// --- Create vertex buffer (device-local) ---
	{
		VkBufferCreateInfo bufInfo = {};
		bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufInfo.size = vertexDataSize;
		bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		result = vkCreateBuffer( vk.device, &bufInfo, NULL, &vk.staticVertexBuffer );
		if ( result != VK_SUCCESS ) {
			ri.Printf( PRINT_WARNING, "VK_CreateStaticWorldBuffers: failed to create vertex buffer\n" );
			return;
		}

		VkMemoryRequirements memReqs;
		vkGetBufferMemoryRequirements( vk.device, vk.staticVertexBuffer, &memReqs );

		VkMemoryAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memReqs.size;
		allocInfo.memoryTypeIndex = VK_FindMemoryType( memReqs.memoryTypeBits,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );

		result = vkAllocateMemory( vk.device, &allocInfo, NULL, &vk.staticVertexMemory );
		if ( result != VK_SUCCESS ) {
			vkDestroyBuffer( vk.device, vk.staticVertexBuffer, NULL );
			vk.staticVertexBuffer = VK_NULL_HANDLE;
			ri.Printf( PRINT_WARNING, "VK_CreateStaticWorldBuffers: failed to alloc vertex memory\n" );
			return;
		}
		vkBindBufferMemory( vk.device, vk.staticVertexBuffer, vk.staticVertexMemory, 0 );
	}

	// --- Create index buffer (device-local) ---
	{
		VkBufferCreateInfo bufInfo = {};
		bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufInfo.size = indexDataSize;
		bufInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		result = vkCreateBuffer( vk.device, &bufInfo, NULL, &vk.staticIndexBuffer );
		if ( result != VK_SUCCESS ) {
			vkFreeMemory( vk.device, vk.staticVertexMemory, NULL );
			vkDestroyBuffer( vk.device, vk.staticVertexBuffer, NULL );
			vk.staticVertexBuffer = VK_NULL_HANDLE;
			vk.staticVertexMemory = VK_NULL_HANDLE;
			ri.Printf( PRINT_WARNING, "VK_CreateStaticWorldBuffers: failed to create index buffer\n" );
			return;
		}

		VkMemoryRequirements memReqs;
		vkGetBufferMemoryRequirements( vk.device, vk.staticIndexBuffer, &memReqs );

		VkMemoryAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memReqs.size;
		allocInfo.memoryTypeIndex = VK_FindMemoryType( memReqs.memoryTypeBits,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );

		result = vkAllocateMemory( vk.device, &allocInfo, NULL, &vk.staticIndexMemory );
		if ( result != VK_SUCCESS ) {
			vkDestroyBuffer( vk.device, vk.staticIndexBuffer, NULL );
			vkFreeMemory( vk.device, vk.staticVertexMemory, NULL );
			vkDestroyBuffer( vk.device, vk.staticVertexBuffer, NULL );
			vk.staticVertexBuffer = VK_NULL_HANDLE;
			vk.staticVertexMemory = VK_NULL_HANDLE;
			vk.staticIndexBuffer = VK_NULL_HANDLE;
			vk.staticIndexMemory = VK_NULL_HANDLE;
			ri.Printf( PRINT_WARNING, "VK_CreateStaticWorldBuffers: failed to alloc index memory\n" );
			return;
		}
		vkBindBufferMemory( vk.device, vk.staticIndexBuffer, vk.staticIndexMemory, 0 );
	}

	// --- Upload via staging buffer ---
	{
		VkCommandBuffer cmd = VK_BeginSingleTimeCommands();

		// Upload vertex data
		VkDeviceSize vertRemaining = vertexDataSize;
		VkDeviceSize vertSrcOffset = 0;
		while ( vertRemaining > 0 ) {
			VkDeviceSize chunkSize = vertRemaining;
			if ( chunkSize > VK_STAGING_BUFFER_SIZE ) {
				chunkSize = VK_STAGING_BUFFER_SIZE;
			}
			Com_Memcpy( vk.staging.data, vertexData + vertSrcOffset, chunkSize );

			VkBufferCopy copyRegion = {};
			copyRegion.srcOffset = 0;
			copyRegion.dstOffset = vertSrcOffset;
			copyRegion.size = chunkSize;
			vkCmdCopyBuffer( cmd, vk.staging.buffer, vk.staticVertexBuffer, 1, &copyRegion );

			vertSrcOffset += chunkSize;
			vertRemaining -= chunkSize;

			// If more chunks needed, submit and start a new command buffer
			if ( vertRemaining > 0 ) {
				VK_EndSingleTimeCommands( cmd );
				cmd = VK_BeginSingleTimeCommands();
			}
		}

		// Upload index data
		VkDeviceSize idxRemaining = indexDataSize;
		VkDeviceSize idxSrcOffset = 0;

		// Always submit vertex commands before starting index upload,
		// because the staging buffer will be overwritten with index data.
		VK_EndSingleTimeCommands( cmd );
		cmd = VK_BeginSingleTimeCommands();

		while ( idxRemaining > 0 ) {
			VkDeviceSize chunkSize = idxRemaining;
			if ( chunkSize > VK_STAGING_BUFFER_SIZE ) {
				chunkSize = VK_STAGING_BUFFER_SIZE;
			}
			Com_Memcpy( vk.staging.data, indexData + idxSrcOffset, chunkSize );

			VkBufferCopy copyRegion = {};
			copyRegion.srcOffset = 0;
			copyRegion.dstOffset = idxSrcOffset;
			copyRegion.size = chunkSize;
			vkCmdCopyBuffer( cmd, vk.staging.buffer, vk.staticIndexBuffer, 1, &copyRegion );

			idxSrcOffset += chunkSize;
			idxRemaining -= chunkSize;

			if ( idxRemaining > 0 ) {
				VK_EndSingleTimeCommands( cmd );
				cmd = VK_BeginSingleTimeCommands();
			}
		}

		VK_EndSingleTimeCommands( cmd );
	}

	vk.staticTotalVertices = totalVertices;
	vk.staticTotalIndexes = totalIndexes;
	vk.staticBuffersValid = qtrue;

	ri.Printf( PRINT_ALL, "Static world VBO: %d vertices (%.2f MB), %d indices (%.2f MB)\n",
		totalVertices, (float)vertexDataSize / (1024.0f * 1024.0f),
		totalIndexes, (float)indexDataSize / (1024.0f * 1024.0f) );
}


/*
=================
VK_DestroyStaticWorldBuffers
=================
*/
void VK_DestroyStaticWorldBuffers( void ) {
	if ( vk.staticVertexBuffer != VK_NULL_HANDLE ) {
		vkDestroyBuffer( vk.device, vk.staticVertexBuffer, NULL );
		vk.staticVertexBuffer = VK_NULL_HANDLE;
	}
	if ( vk.staticVertexMemory != VK_NULL_HANDLE ) {
		vkFreeMemory( vk.device, vk.staticVertexMemory, NULL );
		vk.staticVertexMemory = VK_NULL_HANDLE;
	}
	if ( vk.staticIndexBuffer != VK_NULL_HANDLE ) {
		vkDestroyBuffer( vk.device, vk.staticIndexBuffer, NULL );
		vk.staticIndexBuffer = VK_NULL_HANDLE;
	}
	if ( vk.staticIndexMemory != VK_NULL_HANDLE ) {
		vkFreeMemory( vk.device, vk.staticIndexMemory, NULL );
		vk.staticIndexMemory = VK_NULL_HANDLE;
	}
	vk.staticTotalVertices = 0;
	vk.staticTotalIndexes = 0;
	vk.staticBuffersValid = qfalse;
}


// ============================================================
// Optimization #2: Cached geometry across shader stages
// ============================================================

/*
=================
VK_CacheTessGeometry

Uploads tess.xyz, tess.normal, and tess.indexes to the dynamic buffer
once per tess batch. Subsequent stage draws reuse these offsets.
=================
*/
void VK_CacheTessGeometry( void ) {
	if ( tess.cachedGeo.valid ) {
		return; // already cached
	}
	if ( !r_cachedGeo->integer ) {
		return; // cvar disabled
	}
	if ( !vk.frameStarted || tess.numVertexes == 0 ) {
		return;
	}

	vkDynamicBuffers_t *dyn = &vk.dynBuffers[vk.currentFrame];

	int numVerts = tess.numVertexes;
	int numIndexes = tess.numIndexes;

	// Upload positions (vec4, 16 bytes each)
	VkDeviceSize posSize = numVerts * sizeof(float) * 4;
	VkDeviceSize normSize = numVerts * sizeof(float) * 4;
	VkDeviceSize idxSize = numIndexes * sizeof(glIndex_t);

	if ( dyn->vertexOffset + posSize + normSize > VK_VERTEX_BUFFER_SIZE ||
		 dyn->indexOffset + idxSize > VK_INDEX_BUFFER_SIZE ) {
		ri.Printf( PRINT_WARNING, "VK_CacheTessGeometry: buffer overflow\n" );
		return;
	}

	// Positions
	tess.cachedGeo.posOffset = dyn->vertexOffset;
	Com_Memcpy( dyn->vertexData + dyn->vertexOffset, tess.xyz, posSize );
	dyn->vertexOffset += posSize;

	// Normals (check if any shader stage needs them)
	qboolean hasNormals = (tess.shader && tess.shader->needsNormal) ? qtrue : qfalse;
	if ( hasNormals ) {
		tess.cachedGeo.normOffset = dyn->vertexOffset;
		Com_Memcpy( dyn->vertexData + dyn->vertexOffset, tess.normal, normSize );
		dyn->vertexOffset += normSize;
		tess.cachedGeo.hasNormals = qtrue;
	} else {
		tess.cachedGeo.normOffset = vk.zeroNormOffset[vk.currentFrame];
		tess.cachedGeo.hasNormals = qfalse;
	}

	// Indices
	tess.cachedGeo.idxOffset = dyn->indexOffset;
	Com_Memcpy( dyn->indexData + dyn->indexOffset, tess.indexes, idxSize );
	dyn->indexOffset += idxSize;
	dyn->indexOffset = (dyn->indexOffset + 3) & ~3;

	tess.cachedGeo.valid = qtrue;
}


/*
=================
VK_DrawWithCachedGeo

Draw using previously cached pos/normal/idx offsets.
Only uploads texcoords and colors to the dynamic buffer.
=================
*/
void VK_DrawWithCachedGeo( int numVerts, const float *tc0, const float *tc1,
	const byte *colors, int numIndexes, const glIndex_t *indexes )
{
	if ( !tess.cachedGeo.valid || numIndexes == 0 || numVerts == 0 ) {
		return;
	}
	if ( !vk.frameStarted ) return;

	VkCommandBuffer cmd = vk.frames[vk.currentFrame].commandBuffer;
	vkDynamicBuffers_t *dyn = &vk.dynBuffers[vk.currentFrame];

	// Check overflow for varying data only
	VkDeviceSize tc0Size = numVerts * sizeof(float) * 2;
	VkDeviceSize tc1DataSize = tc1 ? numVerts * sizeof(float) * 2 : 0;
	VkDeviceSize colorSize = numVerts * 4;
	VkDeviceSize needed = tc0Size + tc1DataSize + colorSize;

	if ( dyn->vertexOffset + needed > VK_VERTEX_BUFFER_SIZE ) {
		ri.Printf( PRINT_WARNING, "VK_DrawWithCachedGeo: vertex buffer overflow\n" );
		return;
	}

	// Upload texcoord0
	VkDeviceSize tc0Offset = dyn->vertexOffset;
	if ( tc0 ) {
		Com_Memcpy( dyn->vertexData + dyn->vertexOffset, tc0, tc0Size );
	} else {
		Com_Memset( dyn->vertexData + dyn->vertexOffset, 0, tc0Size );
	}
	dyn->vertexOffset += tc0Size;

	// Upload texcoord1
	VkDeviceSize tc1Offset;
	if ( tc1 ) {
		tc1Offset = dyn->vertexOffset;
		Com_Memcpy( dyn->vertexData + dyn->vertexOffset, tc1, numVerts * sizeof(float) * 2 );
		dyn->vertexOffset += numVerts * sizeof(float) * 2;
	} else {
		tc1Offset = vk.zeroTc1Offset[vk.currentFrame];
	}

	// Upload colors
	VkDeviceSize colorOffset = dyn->vertexOffset;
	if ( colors ) {
		Com_Memcpy( dyn->vertexData + dyn->vertexOffset, colors, colorSize );
	} else {
		Com_Memset( dyn->vertexData + dyn->vertexOffset, 255, colorSize );
	}
	dyn->vertexOffset += colorSize;

	// Align
	dyn->vertexOffset = (dyn->vertexOffset + 3) & ~3;

	// Bind vertex buffers: pos/norm from cache, tc/colors fresh
	VkBuffer vertexBuffers[5];
	VkDeviceSize offsets[5];

	vertexBuffers[0] = dyn->vertexBuffer;  offsets[0] = tess.cachedGeo.posOffset;
	vertexBuffers[1] = dyn->vertexBuffer;  offsets[1] = tc0Offset;
	vertexBuffers[2] = dyn->vertexBuffer;  offsets[2] = tc1Offset;
	vertexBuffers[3] = dyn->vertexBuffer;  offsets[3] = colorOffset;
	vertexBuffers[4] = dyn->vertexBuffer;  offsets[4] = tess.cachedGeo.normOffset;

	vkCmdBindVertexBuffers( cmd, 0, 5, vertexBuffers, offsets );

	// Bind index buffer from cache
	vkCmdBindIndexBuffer( cmd, dyn->indexBuffer, tess.cachedGeo.idxOffset,
		sizeof(glIndex_t) == 4 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16 );

	vkCmdDrawIndexed( cmd, numIndexes, 1, 0, 0, 0 );
}


/*
=================
VK_DrawFromStaticBuffers

Draw geometry from the static world VBO.
When useDynamicVaryings is true, tc0/tc1/colors come from dynTc0/dynTc1/dynColors
(uploaded to dynamic buffer). Otherwise all attributes come from static buffer.
=================
*/
void VK_DrawFromStaticBuffers( int firstVertex, int numVertices,
	int firstIndex, int numIndexes,
	const float *dynTc0, const float *dynTc1, const byte *dynColors,
	qboolean useDynamicVaryings )
{
	if ( numIndexes == 0 || numVertices == 0 ) return;
	if ( !vk.frameStarted || !vk.staticBuffersValid ) return;

	VkCommandBuffer cmd = vk.frames[vk.currentFrame].commandBuffer;
	vkDynamicBuffers_t *dyn = &vk.dynBuffers[vk.currentFrame];

	VkBuffer vertexBuffers[5];
	VkDeviceSize offsets[5];

	// Binding 0: positions from static buffer
	vertexBuffers[0] = vk.staticVertexBuffer;
	offsets[0] = vk.staticPosOffset + (VkDeviceSize)firstVertex * sizeof(float) * 4;

	// Binding 4: normals from static buffer
	vertexBuffers[4] = vk.staticVertexBuffer;
	offsets[4] = vk.staticNormOffset + (VkDeviceSize)firstVertex * sizeof(float) * 4;

	if ( useDynamicVaryings ) {
		// Upload tc0, tc1, colors to dynamic buffer
		VkDeviceSize tc0Size = numVertices * sizeof(float) * 2;
		VkDeviceSize colorSize = numVertices * 4;
		VkDeviceSize needed = tc0Size + (dynTc1 ? tc0Size : 0) + colorSize;

		if ( dyn->vertexOffset + needed > VK_VERTEX_BUFFER_SIZE ) {
			ri.Printf( PRINT_WARNING, "VK_DrawFromStaticBuffers: dynamic buffer overflow\n" );
			return;
		}

		// tc0
		VkDeviceSize tc0Off = dyn->vertexOffset;
		if ( dynTc0 ) {
			Com_Memcpy( dyn->vertexData + dyn->vertexOffset, dynTc0, tc0Size );
		} else {
			Com_Memset( dyn->vertexData + dyn->vertexOffset, 0, tc0Size );
		}
		dyn->vertexOffset += tc0Size;

		// tc1
		VkDeviceSize tc1Off;
		if ( dynTc1 ) {
			tc1Off = dyn->vertexOffset;
			Com_Memcpy( dyn->vertexData + dyn->vertexOffset, dynTc1, tc0Size );
			dyn->vertexOffset += tc0Size;
		} else {
			tc1Off = vk.zeroTc1Offset[vk.currentFrame];
		}

		// colors
		VkDeviceSize colorOff = dyn->vertexOffset;
		if ( dynColors ) {
			Com_Memcpy( dyn->vertexData + dyn->vertexOffset, dynColors, colorSize );
		} else {
			Com_Memset( dyn->vertexData + dyn->vertexOffset, 255, colorSize );
		}
		dyn->vertexOffset += colorSize;
		dyn->vertexOffset = (dyn->vertexOffset + 3) & ~3;

		vertexBuffers[1] = dyn->vertexBuffer;  offsets[1] = tc0Off;
		vertexBuffers[2] = dyn->vertexBuffer;  offsets[2] = tc1Off;
		vertexBuffers[3] = dyn->vertexBuffer;  offsets[3] = colorOff;
	} else {
		// All attributes from static buffer
		vertexBuffers[1] = vk.staticVertexBuffer;
		offsets[1] = vk.staticTc0Offset + (VkDeviceSize)firstVertex * sizeof(float) * 2;

		vertexBuffers[2] = vk.staticVertexBuffer;
		offsets[2] = vk.staticTc1Offset + (VkDeviceSize)firstVertex * sizeof(float) * 2;

		vertexBuffers[3] = vk.staticVertexBuffer;
		offsets[3] = vk.staticColorOffset + (VkDeviceSize)firstVertex * 4;
	}

	vkCmdBindVertexBuffers( cmd, 0, 5, vertexBuffers, offsets );

	// Bind index buffer from static buffer
	VkDeviceSize idxOffset = (VkDeviceSize)firstIndex * sizeof(glIndex_t);
	vkCmdBindIndexBuffer( cmd, vk.staticIndexBuffer, idxOffset,
		sizeof(glIndex_t) == 4 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16 );

	vkCmdDrawIndexed( cmd, numIndexes, 1, 0, 0, 0 );
}

#endif // !DEDICATED
