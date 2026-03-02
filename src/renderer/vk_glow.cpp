/*
===========================================================================
vk_glow.cpp - Glow (bloom) post-processing

Renders glow objects to a full-res offscreen image (sharing the main depth
buffer for correct occlusion), blurs bright areas at half-res, and
composites back onto the main scene.  Supports both progressive bloom
(downsample/upsample mip chain) and legacy Gaussian blur paths.
===========================================================================
*/

#include "tr_local.h"

#ifndef DEDICATED

#include "vk_local.h"

// ============================================================
// Glow (bloom) post-processing
// Renders glow objects to a full-res offscreen image (sharing the main depth buffer
// for correct occlusion), blurs bright areas at half-res, and composites back onto
// the main scene.
// ============================================================
void VK_CreateGlowResources( void ) {
	if ( !vk.blurVertShader || !vk.blurFragShader || !vk.glowCompositeFragShader ) {
		ri.Printf( PRINT_ALL, "VK_CreateGlowResources: blur/glow shaders not loaded, glow disabled\n" );
		return;
	}

	uint32_t fullWidth = vk.swapchainExtent.width;
	uint32_t fullHeight = vk.swapchainExtent.height;

	// Use r_DynamicGlowWidth/Height cvars if set, otherwise default to swapchain/2.
	// Default is 0 (= automatic half-screen resolution for sharp bloom on modern displays).
	uint32_t halfWidth = r_DynamicGlowWidth->integer;
	uint32_t halfHeight = r_DynamicGlowHeight->integer;
	if ( halfWidth <= 0 || halfWidth > fullWidth ) halfWidth = fullWidth / 2;
	if ( halfHeight <= 0 || halfHeight > fullHeight ) halfHeight = fullHeight / 2;
	// Ensure minimum size
	if ( halfWidth < 32 ) halfWidth = 32;
	if ( halfHeight < 32 ) halfHeight = 32;

	// Store for use by VK_BlurGlowTexture
	vk.glow.halfWidth = halfWidth;
	vk.glow.halfHeight = halfHeight;

	// Create full-res glow scene render target (glow objects rendered here)
	VK_CreateRenderTargetImage( &vk.glow.glowImage, &vk.glow.glowImageMemory, &vk.glow.glowImageView,
		fullWidth, fullHeight, vk.sceneFormat,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT );
	vk.glow.glowDescriptorSet = VK_AllocateImageDescriptor( vk.glow.glowImageView, vk.samplerNoMipClamp );

	// Create half-res scene image (final blur output, used by composite)
	VK_CreateRenderTargetImage( &vk.glow.sceneImage, &vk.glow.sceneImageMemory, &vk.glow.sceneImageView,
		halfWidth, halfHeight, vk.sceneFormat,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT );
	vk.glow.sceneDescriptorSet = VK_AllocateImageDescriptor( vk.glow.sceneImageView, vk.samplerNoMipClamp );

	// Create half-res blur ping-pong render target
	VK_CreateRenderTargetImage( &vk.glow.blurImage, &vk.glow.blurImageMemory, &vk.glow.blurImageView,
		halfWidth, halfHeight, vk.sceneFormat,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT );
	vk.glow.blurDescriptorSet = VK_AllocateImageDescriptor( vk.glow.blurImageView, vk.samplerNoMipClamp );

	// ---- Glow scene render pass (full-res, color + depth) ----
	// Shares the main depth buffer for correct occlusion of glow objects
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
		depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;   // Load main scene depth for occlusion
		depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // Preserve for renderPassLoad
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

		vkCreateRenderPass( vk.device, &rpInfo, NULL, &vk.glow.glowRenderPass );
	}

	// ---- Blur render pass (half-res, color only, no depth) ----
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

		VkAttachmentReference colorRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

		VkSubpassDescription subpass = {};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorRef;

		VkSubpassDependency dep = {};
		dep.srcSubpass = VK_SUBPASS_EXTERNAL;
		dep.dstSubpass = 0;
		dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dep.srcAccessMask = 0;
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

		vkCreateRenderPass( vk.device, &rpInfo, NULL, &vk.glow.blurRenderPass );
	}

	// ---- Framebuffers ----
	// Glow scene framebuffer: full-res, glowImage + main depth
	{
		VkImageView atts[] = { vk.glow.glowImageView, vk.depthImageView };
		VkFramebufferCreateInfo fbInfo = {};
		fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fbInfo.renderPass = vk.glow.glowRenderPass;
		fbInfo.attachmentCount = 2;
		fbInfo.pAttachments = atts;
		fbInfo.width = fullWidth;
		fbInfo.height = fullHeight;
		fbInfo.layers = 1;
		vkCreateFramebuffer( vk.device, &fbInfo, NULL, &vk.glow.glowFramebuffer );
	}

	// Blur framebuffer: half-res, blurImage only
	{
		VkFramebufferCreateInfo fbInfo = {};
		fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fbInfo.renderPass = vk.glow.blurRenderPass;
		fbInfo.attachmentCount = 1;
		fbInfo.pAttachments = &vk.glow.blurImageView;
		fbInfo.width = halfWidth;
		fbInfo.height = halfHeight;
		fbInfo.layers = 1;
		vkCreateFramebuffer( vk.device, &fbInfo, NULL, &vk.glow.blurFramebuffer );
	}

	// Scene framebuffer: half-res, sceneImage only (blur pass 1 output)
	{
		VkFramebufferCreateInfo fbInfo = {};
		fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fbInfo.renderPass = vk.glow.blurRenderPass;
		fbInfo.attachmentCount = 1;
		fbInfo.pAttachments = &vk.glow.sceneImageView;
		fbInfo.width = halfWidth;
		fbInfo.height = halfHeight;
		fbInfo.layers = 1;
		vkCreateFramebuffer( vk.device, &fbInfo, NULL, &vk.glow.sceneFramebuffer );
	}

	// ---- Pipelines ----
	// Blur pipeline (fullscreen triangle, samples from texture with offset)
	VkPipelineShaderStageCreateInfo stages[2] = {};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = vk.blurVertShader;
	stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = vk.blurFragShader;
	stages[1].pName = "main";

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
	pipelineInfo.layout = vk.pipelineLayout;
	pipelineInfo.renderPass = vk.glow.blurRenderPass;  // blur uses color-only render pass
	pipelineInfo.subpass = 0;

	vkCreateGraphicsPipelines( vk.device, vk.pipelineCache, 1, &pipelineInfo, NULL, &vk.glow.blurPipeline );

	// Create glow composite pipeline (additive blend, renders to main render pass)
	blendAttachment.blendEnable = VK_TRUE;
	blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
	blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
	blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
	blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

	// Use glow composite shader instead of blur shader for composite pass
	stages[1].module = vk.glowCompositeFragShader;
	
	pipelineInfo.renderPass = vk.renderPassLoad;  // Use load render pass for compositing
	vkCreateGraphicsPipelines( vk.device, vk.pipelineCache, 1, &pipelineInfo, NULL, &vk.glow.glowCompositePipeline );

	// ================================================================
	// Progressive bloom (downsample/upsample mip chain)
	// ================================================================
	if ( vk.glowDownsampleFragShader && vk.glowUpsampleFragShader ) {

		// ---- Upsample render pass (loadOp=LOAD for additive blend) ----
		{
			VkAttachmentDescription colorAtt = {};
			colorAtt.format = vk.sceneFormat;
			colorAtt.samples = VK_SAMPLE_COUNT_1_BIT;
			colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
			colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			colorAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			colorAtt.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			colorAtt.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

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
			dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

			VkRenderPassCreateInfo rpInfo = {};
			rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			rpInfo.attachmentCount = 1;
			rpInfo.pAttachments = &colorAtt;
			rpInfo.subpassCount = 1;
			rpInfo.pSubpasses = &subpass;
			rpInfo.dependencyCount = 1;
			rpInfo.pDependencies = &dep;

			vkCreateRenderPass( vk.device, &rpInfo, NULL, &vk.glow.bloomUpsampleRenderPass );
		}

		// ---- Create bloom mip chain images ----
		{
			uint32_t mipW = halfWidth;
			uint32_t mipH = halfHeight;
			int mipCount = 0;

			for ( int i = 0; i < BLOOM_MAX_MIPS; i++ ) {
				if ( mipW < 2 || mipH < 2 ) break;

				bloomMipLevel_t *mip = &vk.glow.bloomMips[i];
				mip->width = mipW;
				mip->height = mipH;

				VK_CreateRenderTargetImage( &mip->image, &mip->memory, &mip->imageView,
					mipW, mipH, vk.sceneFormat,
					VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT );
				mip->descriptorSet = VK_AllocateImageDescriptor( mip->imageView, vk.samplerNoMipClamp );

				// Downsample framebuffer (uses blurRenderPass: loadOp=CLEAR)
				{
					VkFramebufferCreateInfo fbInfo = {};
					fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
					fbInfo.renderPass = vk.glow.blurRenderPass;
					fbInfo.attachmentCount = 1;
					fbInfo.pAttachments = &mip->imageView;
					fbInfo.width = mipW;
					fbInfo.height = mipH;
					fbInfo.layers = 1;
					vkCreateFramebuffer( vk.device, &fbInfo, NULL, &mip->framebuffer );
				}

				// Upsample framebuffer (uses bloomUpsampleRenderPass: loadOp=LOAD)
				// Separate from downsample FB because the render passes have different
				// subpass dependencies and the validation layer checks them.
				if ( vk.glow.bloomUpsampleRenderPass ) {
					VkFramebufferCreateInfo fbInfo = {};
					fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
					fbInfo.renderPass = vk.glow.bloomUpsampleRenderPass;
					fbInfo.attachmentCount = 1;
					fbInfo.pAttachments = &mip->imageView;
					fbInfo.width = mipW;
					fbInfo.height = mipH;
					fbInfo.layers = 1;
					vkCreateFramebuffer( vk.device, &fbInfo, NULL, &mip->upsampleFramebuffer );
				}

				mipCount++;
				mipW = mipW / 2;
				mipH = mipH / 2;
			}
			vk.glow.bloomMipCount = mipCount;
		}

		// ---- Create upsample framebuffers (uses bloomUpsampleRenderPass: loadOp=LOAD) ----
		// These are stored in a separate array indexed by mip level.
		// We reuse the mip image but create framebuffers compatible with the upsample render pass.
		// Actually, we can just use the same framebuffer if both render passes are compatible
		// (same attachment format/samples). But they have different loadOps so they are different
		// render passes. We need separate framebuffers for the upsample pass.
		// Store upsample framebuffers in a local array and assign after mip chain creation.
		// Actually, VkFramebuffers are compatible if created with compatible render passes.
		// Two render passes are compatible when they have the same attachment descriptions
		// (format, samples, etc.) - loadOp/storeOp don't affect compatibility.
		// So we CAN reuse the same framebuffers for both downsample and upsample!
		// The Vulkan spec says render pass compatibility only checks format and sample count.

		// ---- Downsample pipeline ----
		{
			blendAttachment.blendEnable = VK_FALSE;
			stages[1].module = vk.glowDownsampleFragShader;
			pipelineInfo.renderPass = vk.glow.blurRenderPass;
			vkCreateGraphicsPipelines( vk.device, vk.pipelineCache, 1, &pipelineInfo, NULL, &vk.glow.bloomDownsamplePipeline );
		}

		// ---- Upsample pipeline (additive blend) ----
		{
			blendAttachment.blendEnable = VK_TRUE;
			blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
			blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
			blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
			blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
			blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
			stages[1].module = vk.glowUpsampleFragShader;
			pipelineInfo.renderPass = vk.glow.bloomUpsampleRenderPass;
			vkCreateGraphicsPipelines( vk.device, vk.pipelineCache, 1, &pipelineInfo, NULL, &vk.glow.bloomUpsamplePipeline );
		}

		// Point sceneDescriptorSet to bloom mip[0] so the composite reads the final bloom result
		vk.glow.sceneDescriptorSet = vk.glow.bloomMips[0].descriptorSet;

		ri.Printf( PRINT_ALL, "Progressive bloom: %d mip levels, base %dx%d\n",
			vk.glow.bloomMipCount, vk.glow.bloomMips[0].width, vk.glow.bloomMips[0].height );
	}

	ri.Printf( PRINT_ALL, "Glow resources created (scene=%dx%d, blur=%dx%d)\n", fullWidth, fullHeight, halfWidth, halfHeight );
}

void VK_DestroyGlowResources( void ) {
	if ( !vk.device ) return;
	vkDeviceWaitIdle( vk.device );

	// Destroy progressive bloom mip chain
	for ( int i = 0; i < vk.glow.bloomMipCount; i++ ) {
		bloomMipLevel_t *mip = &vk.glow.bloomMips[i];
		if ( mip->descriptorSet ) vkFreeDescriptorSets( vk.device, vk.descriptorPool, 1, &mip->descriptorSet );
		if ( mip->framebuffer ) vkDestroyFramebuffer( vk.device, mip->framebuffer, NULL );
		if ( mip->upsampleFramebuffer ) vkDestroyFramebuffer( vk.device, mip->upsampleFramebuffer, NULL );
		if ( mip->imageView ) vkDestroyImageView( vk.device, mip->imageView, NULL );
		if ( mip->image ) vkDestroyImage( vk.device, mip->image, NULL );
		if ( mip->memory ) vkFreeMemory( vk.device, mip->memory, NULL );
	}
	if ( vk.glow.bloomDownsamplePipeline ) vkDestroyPipeline( vk.device, vk.glow.bloomDownsamplePipeline, NULL );
	if ( vk.glow.bloomUpsamplePipeline ) vkDestroyPipeline( vk.device, vk.glow.bloomUpsamplePipeline, NULL );
	if ( vk.glow.bloomUpsampleRenderPass ) vkDestroyRenderPass( vk.device, vk.glow.bloomUpsampleRenderPass, NULL );

	// sceneDescriptorSet may have been overwritten to point at bloomMips[0].descriptorSet
	// which was already freed above — only free the original if it's still distinct
	if ( vk.glow.sceneDescriptorSet && vk.glow.sceneDescriptorSet != vk.glow.bloomMips[0].descriptorSet )
		vkFreeDescriptorSets( vk.device, vk.descriptorPool, 1, &vk.glow.sceneDescriptorSet );
	if ( vk.glow.glowDescriptorSet ) vkFreeDescriptorSets( vk.device, vk.descriptorPool, 1, &vk.glow.glowDescriptorSet );
	if ( vk.glow.blurDescriptorSet ) vkFreeDescriptorSets( vk.device, vk.descriptorPool, 1, &vk.glow.blurDescriptorSet );

	if ( vk.glow.blurPipeline ) vkDestroyPipeline( vk.device, vk.glow.blurPipeline, NULL );
	if ( vk.glow.glowCompositePipeline ) vkDestroyPipeline( vk.device, vk.glow.glowCompositePipeline, NULL );
	if ( vk.glow.glowRenderPass ) vkDestroyRenderPass( vk.device, vk.glow.glowRenderPass, NULL );
	if ( vk.glow.blurRenderPass ) vkDestroyRenderPass( vk.device, vk.glow.blurRenderPass, NULL );
	if ( vk.glow.glowFramebuffer ) vkDestroyFramebuffer( vk.device, vk.glow.glowFramebuffer, NULL );
	if ( vk.glow.blurFramebuffer ) vkDestroyFramebuffer( vk.device, vk.glow.blurFramebuffer, NULL );
	if ( vk.glow.sceneFramebuffer ) vkDestroyFramebuffer( vk.device, vk.glow.sceneFramebuffer, NULL );
	if ( vk.glow.glowImageView ) vkDestroyImageView( vk.device, vk.glow.glowImageView, NULL );
	if ( vk.glow.glowImage ) vkDestroyImage( vk.device, vk.glow.glowImage, NULL );
	if ( vk.glow.glowImageMemory ) vkFreeMemory( vk.device, vk.glow.glowImageMemory, NULL );
	if ( vk.glow.sceneImageView ) vkDestroyImageView( vk.device, vk.glow.sceneImageView, NULL );
	if ( vk.glow.sceneImage ) vkDestroyImage( vk.device, vk.glow.sceneImage, NULL );
	if ( vk.glow.sceneImageMemory ) vkFreeMemory( vk.device, vk.glow.sceneImageMemory, NULL );
	if ( vk.glow.blurImageView ) vkDestroyImageView( vk.device, vk.glow.blurImageView, NULL );
	if ( vk.glow.blurImage ) vkDestroyImage( vk.device, vk.glow.blurImage, NULL );
	if ( vk.glow.blurImageMemory ) vkFreeMemory( vk.device, vk.glow.blurImageMemory, NULL );
	Com_Memset( &vk.glow, 0, sizeof(vk.glow) );
}

void VK_BlurGlowTexture( void ) {
	// Use progressive bloom if available, otherwise fall back to legacy Gaussian
	const qboolean useProgressiveBloom = ( vk.glow.bloomDownsamplePipeline && vk.glow.bloomMipCount >= 2 ) ? qtrue : qfalse;

	if ( !useProgressiveBloom && !vk.glow.blurPipeline ) return;

	// Ensure any active render pass is ended before starting blur passes
	VK_EndRenderPass();

	VkCommandBuffer cmd = vk.frames[vk.currentFrame].commandBuffer;

	// Barrier: make glow render pass writes to glowImage visible for shader reads.
	{
		VkImageMemoryBarrier barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = vk.glow.glowImage;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;

		vkCmdPipelineBarrier( cmd,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0, 0, NULL, 0, NULL, 1, &barrier );
	}

	if ( useProgressiveBloom ) {
		// ================================================================
		// Progressive bloom: downsample chain then upsample chain
		// ================================================================
		int mipCount = vk.glow.bloomMipCount;
		float threshold = r_DynamicGlowDelta->value;
		if ( threshold < 0.0f ) threshold = 0.0f;
		float softness = r_DynamicGlowSoft->value;
		if ( softness < 1.0f ) softness = 1.0f;

		// ---- Downsample chain ----
		// Pass 0: glowImage (full-res) → bloomMips[0] (half-res)
		// Pass i: bloomMips[i-1]       → bloomMips[i]
		for ( int i = 0; i < mipCount; i++ ) {
			bloomMipLevel_t *dstMip = &vk.glow.bloomMips[i];

			VkClearValue clearValue = {};
			clearValue.color = { { 0.0f, 0.0f, 0.0f, 0.0f } };

			VkRenderPassBeginInfo rpBegin = {};
			rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			rpBegin.renderPass = vk.glow.blurRenderPass;
			rpBegin.framebuffer = dstMip->framebuffer;
			rpBegin.clearValueCount = 1;
			rpBegin.pClearValues = &clearValue;
			rpBegin.renderArea.extent.width = dstMip->width;
			rpBegin.renderArea.extent.height = dstMip->height;

			VkDescriptorSet srcDescriptor;
			float srcWidth, srcHeight;
			if ( i == 0 ) {
				srcDescriptor = vk.glow.glowDescriptorSet;
				srcWidth = (float)vk.swapchainExtent.width;
				srcHeight = (float)vk.swapchainExtent.height;
			} else {
				srcDescriptor = vk.glow.bloomMips[i - 1].descriptorSet;
				srcWidth = (float)vk.glow.bloomMips[i - 1].width;
				srcHeight = (float)vk.glow.bloomMips[i - 1].height;
			}

			VkViewport viewport = {};
			viewport.width = (float)dstMip->width;
			viewport.height = (float)dstMip->height;
			viewport.minDepth = 0.0f;
			viewport.maxDepth = 1.0f;

			VkRect2D scissor = {};
			scissor.extent.width = dstMip->width;
			scissor.extent.height = dstMip->height;

			vk.renderPassActive = qtrue;
			vkCmdBeginRenderPass( cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE );
			vkCmdSetViewport( cmd, 0, 1, &viewport );
			vkCmdSetScissor( cmd, 0, 1, &scissor );
			vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.glow.bloomDownsamplePipeline );
			vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipelineLayout, 0, 1, &srcDescriptor, 0, NULL );

			// Push constants: {srcTexelSize, isFirstMip, threshold}
			float pc[4];
			pc[0] = 1.0f / srcWidth;
			pc[1] = 1.0f / srcHeight;
			pc[2] = ( i == 0 ) ? 1.0f : 0.0f;  // isFirstMip
			pc[3] = threshold;
			vkCmdPushConstants( cmd, vk.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), pc );

			vkCmdDraw( cmd, 3, 1, 0, 0 );
			vkCmdEndRenderPass( cmd );
			vk.renderPassActive = qfalse;

			// Barrier: downsample output → shader read for next pass
			{
				VkImageMemoryBarrier barrier = {};
				barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
				barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.image = dstMip->image;
				barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				barrier.subresourceRange.baseMipLevel = 0;
				barrier.subresourceRange.levelCount = 1;
				barrier.subresourceRange.baseArrayLayer = 0;
				barrier.subresourceRange.layerCount = 1;

				vkCmdPipelineBarrier( cmd,
					VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					0, 0, NULL, 0, NULL, 1, &barrier );
			}
		}

		// ---- Upsample chain ----
		// Start from the second-to-last mip and work upward.
		// Each pass reads bloomMips[i+1] (smaller, deeper), renders additively
		// into bloomMips[i] (which already contains its downsample result).
		// Uses bloomUpsampleRenderPass (loadOp=LOAD) + additive blend pipeline.
		for ( int i = mipCount - 2; i >= 0; i-- ) {
			bloomMipLevel_t *dstMip = &vk.glow.bloomMips[i];
			bloomMipLevel_t *srcMip = &vk.glow.bloomMips[i + 1];

			VkRenderPassBeginInfo rpBegin = {};
			rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			rpBegin.renderPass = vk.glow.bloomUpsampleRenderPass;
			rpBegin.framebuffer = dstMip->upsampleFramebuffer;
			rpBegin.clearValueCount = 0;
			rpBegin.renderArea.extent.width = dstMip->width;
			rpBegin.renderArea.extent.height = dstMip->height;

			VkViewport viewport = {};
			viewport.width = (float)dstMip->width;
			viewport.height = (float)dstMip->height;
			viewport.minDepth = 0.0f;
			viewport.maxDepth = 1.0f;

			VkRect2D scissor = {};
			scissor.extent.width = dstMip->width;
			scissor.extent.height = dstMip->height;

			vk.renderPassActive = qtrue;
			vkCmdBeginRenderPass( cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE );
			vkCmdSetViewport( cmd, 0, 1, &viewport );
			vkCmdSetScissor( cmd, 0, 1, &scissor );
			vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.glow.bloomUpsamplePipeline );
			vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipelineLayout, 0, 1, &srcMip->descriptorSet, 0, NULL );

			// Push constants: {srcTexelSize, radius, pad}
			float pc[4];
			pc[0] = 1.0f / (float)srcMip->width;
			pc[1] = 1.0f / (float)srcMip->height;
			pc[2] = softness;  // filter radius multiplier
			pc[3] = 0.0f;
			vkCmdPushConstants( cmd, vk.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), pc );

			vkCmdDraw( cmd, 3, 1, 0, 0 );
			vkCmdEndRenderPass( cmd );
			vk.renderPassActive = qfalse;

			// Barrier: upsample output → shader read for next pass (or composite)
			{
				VkImageMemoryBarrier barrier = {};
				barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
				barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.image = dstMip->image;
				barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				barrier.subresourceRange.baseMipLevel = 0;
				barrier.subresourceRange.levelCount = 1;
				barrier.subresourceRange.baseArrayLayer = 0;
				barrier.subresourceRange.layerCount = 1;

				vkCmdPipelineBarrier( cmd,
					VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					0, 0, NULL, 0, NULL, 1, &barrier );
			}
		}

		// bloomMips[0] now holds the final composited bloom.
		// sceneDescriptorSet already points to bloomMips[0].descriptorSet
		// so VK_DrawGlowOverlay() will read it directly.

	} else {
		// ================================================================
		// Legacy Gaussian blur (ping-pong between blurImage and sceneImage)
		// ================================================================
		uint32_t width = vk.glow.halfWidth;
		uint32_t height = vk.glow.halfHeight;

		VkViewport viewport = {};
		viewport.width = (float)width;
		viewport.height = (float)height;
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;

		VkRect2D scissor = {};
		scissor.extent.width = width;
		scissor.extent.height = height;

		int numPasses = r_DynamicGlowPasses->integer;
		if ( numPasses < 1 ) numPasses = 1;
		if ( numPasses > 10 ) numPasses = 10;

		float softness = r_DynamicGlowSoft->value;
		if ( softness < 0.0f ) softness = 0.0f;

		float delta = r_DynamicGlowDelta->value;
		if ( delta < 0.0f ) delta = 0.0f;

		for ( int iter = 0; iter < numPasses; iter++ ) {
			for ( int dir = 0; dir < 2; dir++ ) {
				VkClearValue clearValue = {};
				clearValue.color = { { 0.0f, 0.0f, 0.0f, 0.0f } };

				VkRenderPassBeginInfo rpBegin = {};
				rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
				rpBegin.renderPass = vk.glow.blurRenderPass;
				rpBegin.clearValueCount = 1;
				rpBegin.pClearValues = &clearValue;
				rpBegin.renderArea.extent.width = width;
				rpBegin.renderArea.extent.height = height;

				VkDescriptorSet srcDescriptor;
				if ( dir == 0 ) {
					rpBegin.framebuffer = vk.glow.blurFramebuffer;
					if ( iter == 0 ) {
						srcDescriptor = vk.glow.glowDescriptorSet;
					} else {
						srcDescriptor = vk.glow.sceneDescriptorSet;
					}
				} else {
					rpBegin.framebuffer = vk.glow.sceneFramebuffer;
					srcDescriptor = vk.glow.blurDescriptorSet;
				}

				vk.renderPassActive = qtrue;
				vkCmdBeginRenderPass( cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE );
				vkCmdSetViewport( cmd, 0, 1, &viewport );
				vkCmdSetScissor( cmd, 0, 1, &scissor );
				vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.glow.blurPipeline );
				vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipelineLayout, 0, 1, &srcDescriptor, 0, NULL );

				float blurPC[4];
				if ( dir == 0 ) {
					blurPC[0] = 1.0f / (float)width;
					blurPC[1] = 0.0f;
				} else {
					blurPC[0] = 0.0f;
					blurPC[1] = 1.0f / (float)height;
				}
				blurPC[2] = softness;
				blurPC[3] = ( iter == 0 ) ? delta : 0.0f;
				vkCmdPushConstants( cmd, vk.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(blurPC), blurPC );

				vkCmdDraw( cmd, 3, 1, 0, 0 );
				vkCmdEndRenderPass( cmd );
				vk.renderPassActive = qfalse;

				{
					VkImage barrierImage = ( dir == 0 ) ? vk.glow.blurImage : vk.glow.sceneImage;
					VkImageMemoryBarrier barrier = {};
					barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
					barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
					barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
					barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
					barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
					barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
					barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
					barrier.image = barrierImage;
					barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					barrier.subresourceRange.baseMipLevel = 0;
					barrier.subresourceRange.levelCount = 1;
					barrier.subresourceRange.baseArrayLayer = 0;
					barrier.subresourceRange.layerCount = 1;

					vkCmdPipelineBarrier( cmd,
						VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
						VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
						0, 0, NULL, 0, NULL, 1, &barrier );
				}
			}
		}
	}
}

void VK_DrawGlowOverlay( void ) {
	if ( !vk.glow.glowCompositePipeline || !vk.glow.sceneDescriptorSet ) {
		ri.Printf( PRINT_WARNING, "VK_DrawGlowOverlay: missing composite pipeline or scene descriptor (pipeline=%p desc=%p)\n",
			(void*)vk.glow.glowCompositePipeline, (void*)vk.glow.sceneDescriptorSet );
		return;
	}

	// Ensure we're inside a render pass before drawing
	if ( !vk.renderPassActive ) {
		ri.Printf( PRINT_WARNING, "VK_DrawGlowOverlay: called outside render pass\n" );
		return;
	}

	VkCommandBuffer cmd = vk.frames[vk.currentFrame].commandBuffer;

	// We should be inside the main render pass already
	vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.glow.glowCompositePipeline );

	VkDescriptorSet glowDesc = vk.glow.sceneDescriptorSet;
	vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipelineLayout, 0, 1, &glowDesc, 0, NULL );

	// Push intensity as a push constant
	float intensity = r_DynamicGlowIntensity->value;
	if ( intensity < 0.0f ) intensity = 0.0f;
	vkCmdPushConstants( cmd, vk.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(intensity), &intensity );

	// Draw fullscreen triangle with additive blend
	vkCmdDraw( cmd, 3, 1, 0, 0 );
}

#endif // !DEDICATED
