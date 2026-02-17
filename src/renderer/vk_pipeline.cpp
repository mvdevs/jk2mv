/*
===========================================================================
vk_pipeline.cpp - Vulkan graphics pipeline creation and caching.

Replaces the OpenGL state machine (GL_State, blend modes, depth test, etc.)
with pipeline objects keyed by rendering state.
===========================================================================
*/

#include "tr_local.h"

#ifndef DEDICATED

#include "vk_local.h"

// ============================================================
// Map GLS blend bits to Vulkan blend factors
// ============================================================
static VkBlendFactor VK_BlendFactor( unsigned int glsBits ) {
	switch ( glsBits ) {
		case GLS_SRCBLEND_ZERO:
		case GLS_DSTBLEND_ZERO:
			return VK_BLEND_FACTOR_ZERO;
		case GLS_SRCBLEND_ONE:
		case GLS_DSTBLEND_ONE:
			return VK_BLEND_FACTOR_ONE;
		case GLS_SRCBLEND_DST_COLOR:
			return VK_BLEND_FACTOR_DST_COLOR;
		case GLS_DSTBLEND_SRC_COLOR:
			return VK_BLEND_FACTOR_SRC_COLOR;
		case GLS_SRCBLEND_ONE_MINUS_DST_COLOR:
			return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
		case GLS_DSTBLEND_ONE_MINUS_SRC_COLOR:
			return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
		case GLS_SRCBLEND_SRC_ALPHA:
		case GLS_DSTBLEND_SRC_ALPHA:
			return VK_BLEND_FACTOR_SRC_ALPHA;
		case GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA:
		case GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA:
			return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		case GLS_SRCBLEND_DST_ALPHA:
		case GLS_DSTBLEND_DST_ALPHA:
			return VK_BLEND_FACTOR_DST_ALPHA;
		case GLS_SRCBLEND_ONE_MINUS_DST_ALPHA:
		case GLS_DSTBLEND_ONE_MINUS_DST_ALPHA:
			return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
		case GLS_SRCBLEND_ALPHA_SATURATE:
			return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
		default:
			return VK_BLEND_FACTOR_ONE;
	}
}

// ============================================================
// Find or create a pipeline matching the given state
// ============================================================
VkPipeline VK_FindPipeline( const vkPipelineKey_t *key ) {
	// Search existing pipelines
	for ( int i = 0; i < vk.pipelineCount; i++ ) {
		if ( memcmp( &vk.pipelines[i].key, key, sizeof(vkPipelineKey_t) ) == 0 ) {
			return vk.pipelines[i].pipeline;
		}
	}

	if ( vk.pipelineCount >= VK_MAX_PIPELINES ) {
		ri.Error( ERR_FATAL, "VK_FindPipeline: max pipelines exceeded" );
		return VK_NULL_HANDLE;
	}

	// Create new pipeline
	VkPipeline pipeline = VK_CreatePipelineFromKey( key );

	vk.pipelines[vk.pipelineCount].key = *key;
	vk.pipelines[vk.pipelineCount].pipeline = pipeline;
	vk.pipelineCount++;

	return pipeline;
}

VkPipeline VK_CreatePipelineFromKey( const vkPipelineKey_t *key ) {
	// --- Shader stages ---
	VkPipelineShaderStageCreateInfo shaderStages[2] = {};
	int stageCount = 2;

	shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	shaderStages[0].module = vk.singleTexVertShader;
	shaderStages[0].pName = "main";

	shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	shaderStages[1].pName = "main";

	if ( key->multiTexture ) {
		shaderStages[1].module = vk.multiTexFragShader;
	} else {
		shaderStages[1].module = vk.singleTexFragShader;
	}

	// --- Vertex input ---
	// Vertex layout: 5 bindings
	// position(3f) + texcoord0(2f) + texcoord1(2f) + color(4ub) + normal(3f)
	VkVertexInputBindingDescription bindingDescriptions[5] = {};
	VkVertexInputAttributeDescription attributeDescriptions[5] = {};

	// Binding 0: position (vec4, full read including .w for bone indices)
	bindingDescriptions[0].binding = 0;
	bindingDescriptions[0].stride = sizeof(float) * 4; // vec4 stride to match tess.xyz layout
	bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	attributeDescriptions[0].binding = 0;
	attributeDescriptions[0].location = 0;
	attributeDescriptions[0].format = VK_FORMAT_R32G32B32A32_SFLOAT; // read full vec4 (xyz + bone data in w)
	attributeDescriptions[0].offset = 0;

	// Binding 1: texcoord0 (vec2)
	bindingDescriptions[1].binding = 1;
	bindingDescriptions[1].stride = sizeof(float) * 2;
	bindingDescriptions[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	attributeDescriptions[1].binding = 1;
	attributeDescriptions[1].location = 1;
	attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
	attributeDescriptions[1].offset = 0;

	// Binding 2: texcoord1 (vec2) - always present for shader compatibility
	bindingDescriptions[2].binding = 2;
	bindingDescriptions[2].stride = sizeof(float) * 2;
	bindingDescriptions[2].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	attributeDescriptions[2].binding = 2;
	attributeDescriptions[2].location = 2;
	attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
	attributeDescriptions[2].offset = 0;

	// Binding 3: color (vec4 as R8G8B8A8_UNORM)
	bindingDescriptions[3].binding = 3;
	bindingDescriptions[3].stride = sizeof(uint8_t) * 4;
	bindingDescriptions[3].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	attributeDescriptions[3].binding = 3;
	attributeDescriptions[3].location = 3;
	attributeDescriptions[3].format = VK_FORMAT_R8G8B8A8_UNORM;
	attributeDescriptions[3].offset = 0;

	// Binding 4: normal (vec4, full read including .w for bone weights)
	bindingDescriptions[4].binding = 4;
	bindingDescriptions[4].stride = sizeof(float) * 4; // vec4 stride
	bindingDescriptions[4].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	attributeDescriptions[4].binding = 4;
	attributeDescriptions[4].location = 4;
	attributeDescriptions[4].format = VK_FORMAT_R32G32B32A32_SFLOAT; // read full vec4 (xyz + bone weights in w)
	attributeDescriptions[4].offset = 0;

	VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
	vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInputInfo.vertexBindingDescriptionCount = 5;
	vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions;
	vertexInputInfo.vertexAttributeDescriptionCount = 5;
	vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions;

	// --- Input assembly ---
	VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	inputAssembly.primitiveRestartEnable = VK_FALSE;

	// --- Viewport state (dynamic) ---
	VkPipelineViewportStateCreateInfo viewportState = {};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;

	// --- Rasterization ---
	VkPipelineRasterizationStateCreateInfo rasterizer = {};
	rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.depthClampEnable = VK_FALSE;
	rasterizer.rasterizerDiscardEnable = VK_FALSE;
	rasterizer.lineWidth = 1.0f;
	rasterizer.depthBiasEnable = key->polygonOffset ? VK_TRUE : VK_FALSE;
	rasterizer.depthBiasConstantFactor = key->polygonOffset ? -1.0f : 0.0f;
	rasterizer.depthBiasSlopeFactor = key->polygonOffset ? -1.0f : 0.0f;

	// Polygon mode
	if ( key->wireframe && vk.fillModeNonSolid ) {
		rasterizer.polygonMode = VK_POLYGON_MODE_LINE;
	} else {
		rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	}

	// Cull mode
	switch ( key->cullMode ) {
		case VKCULL_FRONT:
			rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;
			break;
		case VKCULL_BACK:
			rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
			break;
		default:
			rasterizer.cullMode = VK_CULL_MODE_NONE;
			break;
	}
	rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

	// --- Multisample ---
	VkPipelineMultisampleStateCreateInfo multisampling = {};
	multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.sampleShadingEnable = VK_FALSE;
	multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	// --- Depth/stencil ---
	VkPipelineDepthStencilStateCreateInfo depthStencil = {};
	depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = key->depthTestDisable ? VK_FALSE : VK_TRUE;
	depthStencil.depthWriteEnable = key->depthWrite ? VK_TRUE : VK_FALSE;
	depthStencil.depthBoundsTestEnable = VK_FALSE;
	depthStencil.stencilTestEnable = VK_FALSE;

	if ( key->depthTestEqual ) {
		depthStencil.depthCompareOp = VK_COMPARE_OP_EQUAL;
	} else {
		depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	}

	// --- Color blend ---
	VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
	colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
		VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	if ( key->srcBlend != 0 || key->dstBlend != 0 ) {
		colorBlendAttachment.blendEnable = VK_TRUE;
		colorBlendAttachment.srcColorBlendFactor = VK_BlendFactor( key->srcBlend );
		colorBlendAttachment.dstColorBlendFactor = VK_BlendFactor( key->dstBlend );
		colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
		colorBlendAttachment.srcAlphaBlendFactor = VK_BlendFactor( key->srcBlend );
		colorBlendAttachment.dstAlphaBlendFactor = VK_BlendFactor( key->dstBlend );
		colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
	} else {
		colorBlendAttachment.blendEnable = VK_FALSE;
	}

	VkPipelineColorBlendStateCreateInfo colorBlending = {};
	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.logicOpEnable = VK_FALSE;
	colorBlending.attachmentCount = 1;
	colorBlending.pAttachments = &colorBlendAttachment;

	// --- Dynamic state ---
	VkDynamicState dynamicStates[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
		VK_DYNAMIC_STATE_DEPTH_BIAS,
	};

	VkPipelineDynamicStateCreateInfo dynamicState = {};
	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount = key->polygonOffset ? 3 : 2;
	dynamicState.pDynamicStates = dynamicStates;

	// --- Pipeline creation ---
	VkGraphicsPipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.stageCount = stageCount;
	pipelineInfo.pStages = shaderStages;
	pipelineInfo.pVertexInputState = &vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &rasterizer;
	pipelineInfo.pMultisampleState = &multisampling;
	pipelineInfo.pDepthStencilState = &depthStencil;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.pDynamicState = &dynamicState;
	pipelineInfo.layout = vk.pipelineLayout;
	pipelineInfo.renderPass = vk.renderPass;
	pipelineInfo.subpass = 0;

	VkPipeline pipeline;
	if ( vkCreateGraphicsPipelines( vk.device, vk.pipelineCache, 1, &pipelineInfo, NULL, &pipeline ) != VK_SUCCESS ) {
		ri.Error( ERR_FATAL, "Failed to create graphics pipeline" );
	}

	return pipeline;
}

// ============================================================
// Build pipeline key from current GL state bits
// ============================================================
vkPipelineKey_t VK_PipelineKeyFromState( unsigned int stateBits, int cullType, qboolean multiTexture, qboolean polygonOffset ) {
	vkPipelineKey_t key;
	Com_Memset( &key, 0, sizeof(key) );

	key.srcBlend = stateBits & GLS_SRCBLEND_BITS;
	key.dstBlend = stateBits & GLS_DSTBLEND_BITS;
	key.depthWrite = (stateBits & GLS_DEPTHMASK_TRUE) ? 1 : 0;
	key.depthTestDisable = (stateBits & GLS_DEPTHTEST_DISABLE) ? 1 : 0;
	key.depthTestEqual = (stateBits & GLS_DEPTHFUNC_EQUAL) ? 1 : 0;
	key.wireframe = (stateBits & GLS_POLYMODE_LINE) ? 1 : 0;
	key.alphaTest = 0;  // Alpha test is done via push constants, not pipeline state
	key.multiTexture = multiTexture ? 1 : 0;
	key.polygonOffset = polygonOffset ? qtrue : qfalse;

	switch ( cullType ) {
		case CT_FRONT_SIDED:
			key.cullMode = VKCULL_BACK;
			break;
		case CT_BACK_SIDED:
			key.cullMode = VKCULL_FRONT;
			break;
		default:
			key.cullMode = VKCULL_NONE;
			break;
	}

	return key;
}

// ============================================================
// Bind pipeline for current rendering state
// ============================================================
void VK_BindPipeline( unsigned int stateBits, int cullType, qboolean multiTexture, qboolean polygonOffset ) {
	if ( !vk.frameStarted ) return;

	vkPipelineKey_t key = VK_PipelineKeyFromState( stateBits, cullType, multiTexture, polygonOffset );
	VkPipeline pipeline = VK_FindPipeline( &key );

	VkCommandBuffer cmd = vk.frames[vk.currentFrame].commandBuffer;
	vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );

	// Push safe fragment shader defaults so direct VK_DrawIndexed callers
	// (sky, beams, quicksprites, world effects, etc.) that bypass R_DrawElements
	// don't inherit stale texEnvMode/alphaTest from previous shader stages.
	// R_DrawElements overrides these immediately after with correct per-stage values.
	struct {
		float texEnvMode;
		float alphaTestFunc;
		float alphaTestValue;
	} fragDefaults = { 0.0f, 0.0f, 0.0f };  // MODULATE, no alpha test
	vkCmdPushConstants( cmd, vk.pipelineLayout,
		VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		80, sizeof(fragDefaults), &fragDefaults );

	// If polygon offset is enabled, set the dynamic depth bias
	if ( polygonOffset ) {
		vkCmdSetDepthBias( cmd, r_offsetFactor->value, 0.0f, r_offsetUnits->value );
	}
}

#endif // !DEDICATED
