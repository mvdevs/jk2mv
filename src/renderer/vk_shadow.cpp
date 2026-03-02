/*
===========================================================================
vk_shadow.cpp - Stencil shadow volume pipelines

Three dedicated pipelines for the two-pass stencil shadow algorithm:
  1. Front face pass: depth test, write stencil INCR on depth pass
  2. Back face pass: depth test, write stencil DECR on depth pass
  3. Shadow finish: stencil test (!=0), draw darkening fullscreen quad
===========================================================================
*/

#include "tr_local.h"

#ifndef DEDICATED

#include "vk_local.h"

// ============================================================
// Stencil shadow volume pipelines
// Three dedicated pipelines for the two-pass stencil shadow algorithm:
//   1. Front face pass: depth test, write stencil INCR on depth pass
//   2. Back face pass: depth test, write stencil DECR on depth pass
//   3. Shadow finish: stencil test (!=0), draw darkening fullscreen quad
// ============================================================
void VK_CreateShadowPipelines( void ) {
	if ( !vk.singleTexVertShader || !vk.singleTexFragShader ) return;

	// Common state for all three pipelines
	VkPipelineShaderStageCreateInfo stages[2] = {};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = vk.singleTexVertShader;
	stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = vk.singleTexFragShader;
	stages[1].pName = "main";

	// Vertex input: 5-binding layout matching vertex shader (pos, tc0, tc1, color, normal)
	VkVertexInputBindingDescription bindings[5];
	VkVertexInputAttributeDescription attrs[5];
	// Binding 0: position (vec3 from vec4-strided data)
	bindings[0] = {}; bindings[0].binding = 0; bindings[0].stride = sizeof(float) * 4; bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	attrs[0] = {}; attrs[0].location = 0; attrs[0].binding = 0; attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[0].offset = 0;
	// Binding 1: texcoord0 (vec2)
	bindings[1] = {}; bindings[1].binding = 1; bindings[1].stride = sizeof(float) * 2; bindings[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	attrs[1] = {}; attrs[1].location = 1; attrs[1].binding = 1; attrs[1].format = VK_FORMAT_R32G32_SFLOAT; attrs[1].offset = 0;
	// Binding 2: texcoord1 (vec2)
	bindings[2] = {}; bindings[2].binding = 2; bindings[2].stride = sizeof(float) * 2; bindings[2].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	attrs[2] = {}; attrs[2].location = 2; attrs[2].binding = 2; attrs[2].format = VK_FORMAT_R32G32_SFLOAT; attrs[2].offset = 0;
	// Binding 3: color (rgba8)
	bindings[3] = {}; bindings[3].binding = 3; bindings[3].stride = sizeof(byte) * 4; bindings[3].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	attrs[3] = {}; attrs[3].location = 3; attrs[3].binding = 3; attrs[3].format = VK_FORMAT_R8G8B8A8_UNORM; attrs[3].offset = 0;
	// Binding 4: normal (vec4 — xyz normal + w bone weights)
	bindings[4] = {}; bindings[4].binding = 4; bindings[4].stride = sizeof(float) * 4; bindings[4].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	attrs[4] = {}; attrs[4].location = 4; attrs[4].binding = 4; attrs[4].format = VK_FORMAT_R32G32B32A32_SFLOAT; attrs[4].offset = 0;

	VkPipelineVertexInputStateCreateInfo vertexInput = {};
	vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInput.vertexBindingDescriptionCount = 5;
	vertexInput.pVertexBindingDescriptions = bindings;
	vertexInput.vertexAttributeDescriptionCount = 5;
	vertexInput.pVertexAttributeDescriptions = attrs;

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
	rasterization.frontFace = VK_FRONT_FACE_CLOCKWISE;
	rasterization.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo multisample = {};
	multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState blendAttachment = {};
	blendAttachment.colorWriteMask = 0;  // no color writes during stencil fill
	blendAttachment.blendEnable = VK_FALSE;

	VkPipelineColorBlendStateCreateInfo colorBlend = {};
	colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlend.attachmentCount = 1;
	colorBlend.pAttachments = &blendAttachment;

	VkPipelineDepthStencilStateCreateInfo depthStencil = {};
	depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = VK_TRUE;
	depthStencil.depthWriteEnable = VK_FALSE;
	depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	depthStencil.stencilTestEnable = VK_TRUE;

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
	pipelineInfo.renderPass = vk.renderPass;
	pipelineInfo.subpass = 0;

	// --- Pipeline 1: Front faces, stencil INCR on depth pass ---
	rasterization.cullMode = VK_CULL_MODE_BACK_BIT;
	depthStencil.front.failOp = VK_STENCIL_OP_KEEP;
	depthStencil.front.passOp = VK_STENCIL_OP_INCREMENT_AND_CLAMP;
	depthStencil.front.depthFailOp = VK_STENCIL_OP_KEEP;
	depthStencil.front.compareOp = VK_COMPARE_OP_ALWAYS;
	depthStencil.front.compareMask = 0xFF;
	depthStencil.front.writeMask = 0xFF;
	depthStencil.front.reference = 0;
	depthStencil.back = depthStencil.front;
	vkCreateGraphicsPipelines( vk.device, vk.pipelineCache, 1, &pipelineInfo, NULL, &vk.shadowStencilIncrPipeline );

	// --- Pipeline 2: Back faces, stencil DECR on depth pass ---
	rasterization.cullMode = VK_CULL_MODE_FRONT_BIT;
	depthStencil.front.passOp = VK_STENCIL_OP_DECREMENT_AND_CLAMP;
	depthStencil.back = depthStencil.front;
	vkCreateGraphicsPipelines( vk.device, vk.pipelineCache, 1, &pipelineInfo, NULL, &vk.shadowStencilDecrPipeline );

	// --- Pipeline 3: Shadow finish - draw darkening quad where stencil != 0 ---
	rasterization.cullMode = VK_CULL_MODE_NONE;
	depthStencil.depthTestEnable = VK_FALSE;
	depthStencil.front.failOp = VK_STENCIL_OP_KEEP;
	depthStencil.front.passOp = VK_STENCIL_OP_KEEP;
	depthStencil.front.depthFailOp = VK_STENCIL_OP_KEEP;
	depthStencil.front.compareOp = VK_COMPARE_OP_NOT_EQUAL;
	depthStencil.front.compareMask = 0xFF;
	depthStencil.front.writeMask = 0xFF;
	depthStencil.front.reference = 0;
	depthStencil.back = depthStencil.front;

	blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
		VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	blendAttachment.blendEnable = VK_TRUE;
	blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
	blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
	blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
	blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
	blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
	vkCreateGraphicsPipelines( vk.device, vk.pipelineCache, 1, &pipelineInfo, NULL, &vk.shadowFinishPipeline );

	ri.Printf( PRINT_ALL, "Stencil shadow pipelines created\n" );
}

void VK_DestroyShadowPipelines( void ) {
	if ( vk.shadowStencilIncrPipeline ) vkDestroyPipeline( vk.device, vk.shadowStencilIncrPipeline, NULL );
	if ( vk.shadowStencilDecrPipeline ) vkDestroyPipeline( vk.device, vk.shadowStencilDecrPipeline, NULL );
	if ( vk.shadowFinishPipeline ) vkDestroyPipeline( vk.device, vk.shadowFinishPipeline, NULL );
	vk.shadowStencilIncrPipeline = VK_NULL_HANDLE;
	vk.shadowStencilDecrPipeline = VK_NULL_HANDLE;
	vk.shadowFinishPipeline = VK_NULL_HANDLE;
}

#endif // !DEDICATED
