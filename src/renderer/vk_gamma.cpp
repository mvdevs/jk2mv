/*
===========================================================================
vk_gamma.cpp - Gamma correction post-processing

Applies gamma/brightness/contrast as a fullscreen pass.
Uses an offscreen render target and a fullscreen gamma.frag post-process
pass.  All scene rendering is redirected to the offscreen image, then
gamma/brightness/contrast are applied when copying to the swapchain.
===========================================================================
*/

#include "tr_local.h"

#ifndef DEDICATED

#include "vk_local.h"

int VK_ClampFxaaQuality( int v ) {
	// r_fxaa is the FXAA quality preset:
	// 0 = off, 2/4/8/16 = increasing quality.
	if ( v <= 0 ) return 0;
	if ( v <= 2 ) return 2;
	if ( v <= 4 ) return 4;
	if ( v <= 8 ) return 8;
	return 16;
}

static void VK_FxaaPresetParams( int quality, float fxaaParams[4] ) {
	// fxaaParams: { subpix, edgeThreshold, edgeThresholdMin, maxSpan }
	// Tuned conservatively to avoid sky-color bleed/halos while still reducing shimmer.
	// NOTE: Over-aggressive subpix smoothing is a common cause of visible edge tinting.
	// Thresholds can be standard FXAA values now that sub-pixel is absent:
	// the edge-walk never blurs symmetric patterns (distNear==distTotal/2 → blend==0)
	// regardless of threshold, so there is no collateral texture blurring to guard against.
	switch ( quality ) {
	default:
	case 2:
		fxaaParams[0] = 0.50f; // blend quality (max edge-walk blend)
		fxaaParams[1] = 0.333f; // edgeThreshold (relative, vs lumaMax)
		fxaaParams[2] = 0.100f; // edgeThresholdMin (absolute)
		fxaaParams[3] = 6.0f;   // maxSpan (walk radius, pixels)
		break;
	case 4:
		fxaaParams[0] = 0.65f;
		fxaaParams[1] = 0.250f;
		fxaaParams[2] = 0.083f;
		fxaaParams[3] = 8.0f;
		break;
	case 8:
		fxaaParams[0] = 0.80f;
		fxaaParams[1] = 0.166f;
		fxaaParams[2] = 0.066f;
		fxaaParams[3] = 12.0f;
		break;
	case 16:
		fxaaParams[0] = 1.00f;
		fxaaParams[1] = 0.125f;
		fxaaParams[2] = 0.050f;
		fxaaParams[3] = 16.0f;
		break;
	}
}

// ============================================================
// Gamma correction post-processing
// Applies gamma/brightness/contrast as a fullscreen pass.
// Uses an offscreen render target and a fullscreen gamma.frag post-process pass.
// All scene rendering is redirected to the offscreen image, then gamma/brightness/contrast
// are applied when copying to the swapchain.
// ============================================================
void VK_CreateGammaResources( void ) {
	if ( !vk.gammaVertShader || !vk.gammaFragShader ) {
		ri.Printf( PRINT_ALL, "VK_CreateGammaResources: gamma shaders not loaded, gamma disabled\n" );
		return;
	}

	// Determine FXAA state from r_fxaa
	int fxaaQuality = VK_ClampFxaaQuality( Cvar_VariableIntegerValue( "r_fxaa" ) );
	vk.gamma.fxaaActive = ( fxaaQuality > 0 ) ? qtrue : qfalse;
	vk.gamma.fxaaQuality = fxaaQuality;
	vk.gamma.appliedThisFrame = qfalse;

	uint32_t width = vk.swapchainExtent.width;
	uint32_t height = vk.swapchainExtent.height;

	// ---- Offscreen scene image ----
	VK_CreateRenderTargetImage( &vk.gamma.sceneImage, &vk.gamma.sceneImageMemory, &vk.gamma.sceneImageView,
		width, height, vk.sceneFormat,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT );
	vk.gamma.sceneDescriptorSet = VK_AllocateImageDescriptor( vk.gamma.sceneImageView, vk.samplerNoMipClamp );

	// ---- Scene render pass (clear) ----
	// Same attachment format as vk.renderPass (pipeline-compatible) but finalLayout = SHADER_READ_ONLY
	{
		VkAttachmentDescription colorAtt = {};
		colorAtt.format = vk.sceneFormat;
		colorAtt.samples = VK_SAMPLE_COUNT_1_BIT;
		colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		colorAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		colorAtt.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkAttachmentDescription depthAtt = {};
		depthAtt.format = vk.depthFormat;
		depthAtt.samples = VK_SAMPLE_COUNT_1_BIT;
		depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		depthAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depthAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
		depthAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		depthAtt.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference colorRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
		VkAttachmentReference depthRef = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

		VkSubpassDescription subpass = {};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorRef;
		subpass.pDepthStencilAttachment = &depthRef;

		VkSubpassDependency dep = {};
		dep.srcSubpass = VK_SUBPASS_EXTERNAL;
		dep.dstSubpass = 0;
		dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		dep.srcAccessMask = 0;
		dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

		VkAttachmentDescription atts[] = { colorAtt, depthAtt };
		VkRenderPassCreateInfo rpInfo = {};
		rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		rpInfo.attachmentCount = 2;
		rpInfo.pAttachments = atts;
		rpInfo.subpassCount = 1;
		rpInfo.pSubpasses = &subpass;
		rpInfo.dependencyCount = 1;
		rpInfo.pDependencies = &dep;

		vkCreateRenderPass( vk.device, &rpInfo, NULL, &vk.gamma.sceneRenderPass );

		// Load variant (resume after glow without clearing)
		colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		colorAtt.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		depthAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		depthAtt.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		atts[0] = colorAtt;
		atts[1] = depthAtt;

		vkCreateRenderPass( vk.device, &rpInfo, NULL, &vk.gamma.sceneRenderPassLoad );
	}

	// ---- Scene framebuffer (offscreen color + shared depth) ----
	{
		VkImageView atts[] = { vk.gamma.sceneImageView, vk.depthImageView };
		VkFramebufferCreateInfo fbInfo = {};
		fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fbInfo.renderPass = vk.gamma.sceneRenderPass;
		fbInfo.attachmentCount = 2;
		fbInfo.pAttachments = atts;
		fbInfo.width = width;
		fbInfo.height = height;
		fbInfo.layers = 1;
		vkCreateFramebuffer( vk.device, &fbInfo, NULL, &vk.gamma.sceneFramebuffer );
	}

	// ---- Gamma render pass (color only, writes to swapchain) ----
	{
		VkAttachmentDescription colorAtt = {};
		colorAtt.format = vk.swapchainFormat;
		colorAtt.samples = VK_SAMPLE_COUNT_1_BIT;
		colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // fullscreen overwrite
		colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		// We transition the swapchain image explicitly to COLOR_ATTACHMENT_OPTIMAL
		// before starting this pass (see VK_ApplyGammaCorrection).
		colorAtt.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		colorAtt.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		VkAttachmentReference colorRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

		VkSubpassDescription subpass = {};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorRef;

		VkSubpassDependency dep = {};
		dep.srcSubpass = VK_SUBPASS_EXTERNAL;
		dep.dstSubpass = 0;
		dep.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dep.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		VkRenderPassCreateInfo rpInfo = {};
		rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		rpInfo.attachmentCount = 1;
		rpInfo.pAttachments = &colorAtt;
		rpInfo.subpassCount = 1;
		rpInfo.pSubpasses = &subpass;
		rpInfo.dependencyCount = 1;
		rpInfo.pDependencies = &dep;

		vkCreateRenderPass( vk.device, &rpInfo, NULL, &vk.gamma.gammaRenderPass );
	}

	// ---- Per-swapchain-image gamma framebuffers ----
	for ( uint32_t i = 0; i < vk.swapchainImageCount; i++ ) {
		VkFramebufferCreateInfo fbInfo = {};
		fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fbInfo.renderPass = vk.gamma.gammaRenderPass;
		fbInfo.attachmentCount = 1;
		fbInfo.pAttachments = &vk.swapchainImageViews[i];
		fbInfo.width = width;
		fbInfo.height = height;
		fbInfo.layers = 1;
		vkCreateFramebuffer( vk.device, &fbInfo, NULL, &vk.gamma.gammaFramebuffers[i] );
	}

	// ---- Swapchain overlay render pass + framebuffers (UI after gamma, unaffected by post-process) ----
	{
		VkAttachmentDescription colorAtt = {};
		colorAtt.format = vk.swapchainFormat;
		colorAtt.samples = VK_SAMPLE_COUNT_1_BIT;
		colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		colorAtt.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		colorAtt.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		VkAttachmentDescription depthAtt = {};
		depthAtt.format = vk.depthFormat;
		depthAtt.samples = VK_SAMPLE_COUNT_1_BIT;
		depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		depthAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		depthAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
		depthAtt.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		depthAtt.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference colorRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
		VkAttachmentReference depthRef = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

		VkSubpassDescription subpass = {};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorRef;
		subpass.pDepthStencilAttachment = &depthRef;

		VkSubpassDependency dep = {};
		dep.srcSubpass = VK_SUBPASS_EXTERNAL;
		dep.dstSubpass = 0;
		dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dep.srcAccessMask = 0;
		dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		VkAttachmentDescription atts[] = { colorAtt, depthAtt };
		VkRenderPassCreateInfo rpInfo = {};
		rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		rpInfo.attachmentCount = 2;
		rpInfo.pAttachments = atts;
		rpInfo.subpassCount = 1;
		rpInfo.pSubpasses = &subpass;
		rpInfo.dependencyCount = 1;
		rpInfo.pDependencies = &dep;

		vkCreateRenderPass( vk.device, &rpInfo, NULL, &vk.gamma.overlayRenderPass );

		for ( uint32_t i = 0; i < vk.swapchainImageCount; i++ ) {
			VkImageView fbAtts[] = { vk.swapchainImageViews[i], vk.depthImageView };
			VkFramebufferCreateInfo fbInfo = {};
			fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			fbInfo.renderPass = vk.gamma.overlayRenderPass;
			fbInfo.attachmentCount = 2;
			fbInfo.pAttachments = fbAtts;
			fbInfo.width = width;
			fbInfo.height = height;
			fbInfo.layers = 1;
			vkCreateFramebuffer( vk.device, &fbInfo, NULL, &vk.gamma.overlayFramebuffers[i] );
		}
	}

	// ---- Gamma pipeline layout (32-byte push constants: gammaParams + fxaaParams) ----
	{
		VkPushConstantRange pcRange = {};
		pcRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		pcRange.offset = 0;
		pcRange.size = 32; // vec4 gammaParams + vec4 fxaaParams (fxaaParams.x==0 disables FXAA)

		VkPipelineLayoutCreateInfo layoutInfo = {};
		layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutInfo.setLayoutCount = 1;
		layoutInfo.pSetLayouts = &vk.descriptorSetLayout; // reuse existing sampler layout
		layoutInfo.pushConstantRangeCount = 1;
		layoutInfo.pPushConstantRanges = &pcRange;

		vkCreatePipelineLayout( vk.device, &layoutInfo, NULL, &vk.gamma.gammaPipelineLayout );
	}

	// ---- Gamma pipeline(s) (fullscreen triangle, no depth test) ----
	// Two VkPipeline objects are created from the same shader module via specialization constants:
	//   gammaPipeline        -> FXAA_ENABLED=true  (full FXAA code present)
	//   gammaPipelineNoFxaa  -> FXAA_ENABLED=false (driver DCEs the entire FXAA path)
	{
		// Specialization constant 0: bool FXAA_ENABLED
		// GLSL bool constant_id maps to OpTypeInt 32 in SPIR-V; use VkBool32 (uint32_t).
		VkSpecializationMapEntry specEntry = {};
		specEntry.constantID = 0;
		specEntry.offset     = 0;
		specEntry.size       = sizeof(VkBool32);

		VkBool32 fxaaEnabledTrue  = VK_TRUE;
		VkBool32 fxaaEnabledFalse = VK_FALSE;

		VkSpecializationInfo specInfoFxaa = {};
		specInfoFxaa.mapEntryCount = 1;
		specInfoFxaa.pMapEntries   = &specEntry;
		specInfoFxaa.dataSize      = sizeof(VkBool32);
		specInfoFxaa.pData         = &fxaaEnabledTrue;

		VkSpecializationInfo specInfoNoFxaa = {};
		specInfoNoFxaa.mapEntryCount = 1;
		specInfoNoFxaa.pMapEntries   = &specEntry;
		specInfoNoFxaa.dataSize      = sizeof(VkBool32);
		specInfoNoFxaa.pData         = &fxaaEnabledFalse;

		VkPipelineShaderStageCreateInfo stages[2] = {};
		stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		stages[0].module = vk.gammaVertShader;
		stages[0].pName = "main";
		stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stages[1].module = vk.gammaFragShader;
		stages[1].pName = "main";
		// pSpecializationInfo set per pipeline below

		VkPipelineVertexInputStateCreateInfo vertexInput = {};
		vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

		VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
		inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		VkPipelineViewportStateCreateInfo viewportState = {};
		viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportState.viewportCount = 1;
		viewportState.scissorCount = 1;

		VkPipelineRasterizationStateCreateInfo rasterization = {};
		rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterization.polygonMode = VK_POLYGON_MODE_FILL;
		rasterization.cullMode = VK_CULL_MODE_NONE;
		rasterization.frontFace = VK_FRONT_FACE_CLOCKWISE;
		rasterization.lineWidth = 1.0f;

		VkPipelineMultisampleStateCreateInfo multisample = {};
		multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		VkPipelineColorBlendAttachmentState blendAttachment = {};
		blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
			VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		blendAttachment.blendEnable = VK_FALSE;

		VkPipelineColorBlendStateCreateInfo colorBlend = {};
		colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorBlend.attachmentCount = 1;
		colorBlend.pAttachments = &blendAttachment;

		VkPipelineDepthStencilStateCreateInfo depthStencil = {};
		depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depthStencil.depthTestEnable = VK_FALSE;
		depthStencil.depthWriteEnable = VK_FALSE;

		VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicState = {};
		dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamicState.dynamicStateCount = 2;
		dynamicState.pDynamicStates = dynamicStates;

		VkGraphicsPipelineCreateInfo pipelineInfo = {};
		pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineInfo.stageCount = 2;
		pipelineInfo.pStages = stages;
		pipelineInfo.pVertexInputState = &vertexInput;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState = &viewportState;
		pipelineInfo.pRasterizationState = &rasterization;
		pipelineInfo.pMultisampleState = &multisample;
		pipelineInfo.pDepthStencilState = &depthStencil;
		pipelineInfo.pColorBlendState = &colorBlend;
		pipelineInfo.pDynamicState = &dynamicState;
		pipelineInfo.layout = vk.gamma.gammaPipelineLayout;
		pipelineInfo.renderPass = vk.gamma.gammaRenderPass;
		pipelineInfo.subpass = 0;

		// FXAA-enabled pipeline (FXAA_ENABLED = true)
		stages[1].pSpecializationInfo = &specInfoFxaa;
		vkCreateGraphicsPipelines( vk.device, vk.pipelineCache, 1, &pipelineInfo, NULL, &vk.gamma.gammaPipeline );

		// No-FXAA pipeline (FXAA_ENABLED = false; compiler prunes FXAA dead code)
		stages[1].pSpecializationInfo = &specInfoNoFxaa;
		vkCreateGraphicsPipelines( vk.device, vk.pipelineCache, 1, &pipelineInfo, NULL, &vk.gamma.gammaPipelineNoFxaa );
	}

	vk.gamma.enabled = qtrue;

	if ( vk.gamma.fxaaActive ) {
		ri.Printf( PRINT_ALL, "Gamma correction resources created (offscreen %dx%d) + FXAA preset=%d + UI overlay\n",
			width, height, vk.gamma.fxaaQuality );
	} else {
		ri.Printf( PRINT_ALL, "Gamma correction resources created (offscreen %dx%d) + UI overlay\n", width, height );
	}
}

void VK_DestroyGammaResources( void ) {
	if ( !vk.device ) return;
	vkDeviceWaitIdle( vk.device );

	for ( uint32_t i = 0; i < vk.swapchainImageCount; i++ ) {
		if ( vk.gamma.overlayFramebuffers[i] ) vkDestroyFramebuffer( vk.device, vk.gamma.overlayFramebuffers[i], NULL );
	}
	if ( vk.gamma.overlayRenderPass ) vkDestroyRenderPass( vk.device, vk.gamma.overlayRenderPass, NULL );

	if ( vk.gamma.gammaPipeline )       vkDestroyPipeline( vk.device, vk.gamma.gammaPipeline, NULL );
	if ( vk.gamma.gammaPipelineNoFxaa ) vkDestroyPipeline( vk.device, vk.gamma.gammaPipelineNoFxaa, NULL );
	if ( vk.gamma.gammaPipelineLayout ) vkDestroyPipelineLayout( vk.device, vk.gamma.gammaPipelineLayout, NULL );

	for ( uint32_t i = 0; i < vk.swapchainImageCount; i++ ) {
		if ( vk.gamma.gammaFramebuffers[i] ) vkDestroyFramebuffer( vk.device, vk.gamma.gammaFramebuffers[i], NULL );
	}
	if ( vk.gamma.gammaRenderPass ) vkDestroyRenderPass( vk.device, vk.gamma.gammaRenderPass, NULL );

	if ( vk.gamma.sceneFramebuffer ) vkDestroyFramebuffer( vk.device, vk.gamma.sceneFramebuffer, NULL );
	if ( vk.gamma.sceneRenderPassLoad ) vkDestroyRenderPass( vk.device, vk.gamma.sceneRenderPassLoad, NULL );
	if ( vk.gamma.sceneRenderPass ) vkDestroyRenderPass( vk.device, vk.gamma.sceneRenderPass, NULL );

	if ( vk.gamma.sceneDescriptorSet ) vkFreeDescriptorSets( vk.device, vk.descriptorPool, 1, &vk.gamma.sceneDescriptorSet );
	if ( vk.gamma.sceneImageView ) vkDestroyImageView( vk.device, vk.gamma.sceneImageView, NULL );
	if ( vk.gamma.sceneImage ) vkDestroyImage( vk.device, vk.gamma.sceneImage, NULL );
	if ( vk.gamma.sceneImageMemory ) vkFreeMemory( vk.device, vk.gamma.sceneImageMemory, NULL );

	Com_Memset( &vk.gamma, 0, sizeof(vk.gamma) );
}

void VK_ApplyGammaCorrection( void ) {
	if ( !vk.gamma.enabled || !vk.frameStarted ) return;
	if ( vk.gamma.appliedThisFrame ) return;

	vkFrame_t *frame = &vk.frames[vk.currentFrame];
	VkCommandBuffer cmd = frame->commandBuffer;

	// End the active render pass (scene/overlay) — ensures offscreen image is in SHADER_READ_ONLY_OPTIMAL
	VK_EndRenderPass();

	// Explicitly transition swapchain image to COLOR_ATTACHMENT_OPTIMAL.
	// This avoids relying on implicit PRESENT->COLOR transitions, which can be
	// driver-sensitive and (in this codebase) only hits when FXAA/gamma is active.
	VkImageLayout oldLayout = vk.swapchainImageLayouts[vk.currentSwapchainImage];
	if ( oldLayout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL ) {
		if ( oldLayout != VK_IMAGE_LAYOUT_UNDEFINED && oldLayout != VK_IMAGE_LAYOUT_PRESENT_SRC_KHR ) {
			// Layout tracking should keep us in UNDEFINED/PRESENT here; fall back safely.
			oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		}
		VK_TransitionImageLayout( vk.swapchainImages[vk.currentSwapchainImage], vk.swapchainFormat,
			oldLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 1, cmd );
		vk.swapchainImageLayouts[vk.currentSwapchainImage] = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	}

	// Begin gamma render pass on swapchain framebuffer
	VkRenderPassBeginInfo rpBegin = {};
	rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	rpBegin.renderPass = vk.gamma.gammaRenderPass;
	rpBegin.framebuffer = vk.gamma.gammaFramebuffers[vk.currentSwapchainImage];
	rpBegin.renderArea.offset = { 0, 0 };
	rpBegin.renderArea.extent = vk.swapchainExtent;
	rpBegin.clearValueCount = 0;
	rpBegin.pClearValues = NULL;

	vkCmdBeginRenderPass( cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE );

	// Set viewport and scissor
	VkViewport viewport = {};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = (float)vk.swapchainExtent.width;
	viewport.height = (float)vk.swapchainExtent.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport( cmd, 0, 1, &viewport );

	VkRect2D scissor = {};
	scissor.offset = { 0, 0 };
	scissor.extent = vk.swapchainExtent;
	vkCmdSetScissor( cmd, 0, 1, &scissor );

	VkPipeline activePipeline = vk.gamma.fxaaActive
		? vk.gamma.gammaPipeline
		: vk.gamma.gammaPipelineNoFxaa;
	vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, activePipeline );
	vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.gamma.gammaPipelineLayout,
		0, 1, &vk.gamma.sceneDescriptorSet, 0, NULL );

	// Push 32-byte constants: gammaParams + fxaaParams (fxaaParams.x==0 disables FXAA in shader)
	float gamma = r_gamma ? r_gamma->value : 1.0f;
	if ( gamma < 0.5f ) gamma = 0.5f;
	if ( gamma > 3.0f ) gamma = 3.0f;
	float exposure = r_hdr_exposure ? r_hdr_exposure->value : 0.0f;
	float gammaPC[4] = { 1.0f / gamma, 0.0f, 1.0f, exposure };
	float fxaaPC[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	if ( vk.gamma.fxaaActive ) {
		VK_FxaaPresetParams( vk.gamma.fxaaQuality, fxaaPC );
	}
	float postPC[8] = {
		gammaPC[0], gammaPC[1], gammaPC[2], gammaPC[3],
		fxaaPC[0], fxaaPC[1], fxaaPC[2], fxaaPC[3]
	};
	vkCmdPushConstants( cmd, vk.gamma.gammaPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 32, postPC );

	// Draw fullscreen triangle
	vkCmdDraw( cmd, 3, 1, 0, 0 );

	// End gamma render pass — swapchain image transitions to PRESENT_SRC_KHR
	vkCmdEndRenderPass( cmd );
	vk.gamma.appliedThisFrame = qtrue;
	vk.swapchainImageLayouts[vk.currentSwapchainImage] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	// Scene render pass is no longer active
	vk.renderPassActive = qfalse;
	vk.uiPassActive = qfalse;
	vk.pipelineRenderPass = VK_NULL_HANDLE;
}

#endif // !DEDICATED
