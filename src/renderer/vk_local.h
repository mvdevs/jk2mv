/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.
Vulkan backend for JK2MV renderer

vk_local.h - Vulkan state, structures, and function declarations

This replaces the OpenGL backend. All rendering state that was managed
via GL calls is now managed through Vulkan objects.
===========================================================================
*/

#ifndef VK_LOCAL_H
#define VK_LOCAL_H

#ifndef DEDICATED

#include <vulkan/vulkan.h>

// Maximum frames in flight for double/triple buffering
#define VK_NUM_COMMAND_BUFFERS		2
#define VK_MAX_SWAPCHAIN_IMAGES		8
#define VK_MAX_PIPELINES			256
#define VK_VERTEX_BUFFER_SIZE		(8 * 1024 * 1024)
#define VK_INDEX_BUFFER_SIZE		(2 * 1024 * 1024)
#define VK_UNIFORM_BUFFER_SIZE		(4 * 1024 * 1024)
#define VK_STAGING_BUFFER_SIZE		(64 * 1024 * 1024) // for texture uploads and screenshots
#define VK_MAX_IMAGE_SLOTS			4096

// Per-draw push constants
typedef struct {
	float mvp[16];
	float texEnvMode;		// 0=modulate, 1=replace, 2=decal, 3=add
	float alphaTestFunc;	// 0=none, 1=GT0, 2=LT80, 3=GE80, 4=GEC0
} vkPushConstants_t;

// ============================================================
// GPU-side feature flags (bitfield for gpuParams_t::gpuFlags)
// These tell the vertex/fragment shaders which computations
// to perform on the GPU instead of the CPU.
// ============================================================
#define GPU_FLAG_NONE               0
#define GPU_FLAG_DIFFUSE_LIGHTING   (1 << 0)  // compute N.L diffuse in vertex shader
#define GPU_FLAG_SPECULAR_ALPHA     (1 << 1)  // compute specular in vertex shader -> alpha
#define GPU_FLAG_ENVMAP_TC          (1 << 2)  // compute environment map texcoords in vertex shader
#define GPU_FLAG_FOG                (1 << 3)  // compute fog and apply in fragment shader
#define GPU_FLAG_FOG_MODULATE_RGB   (1 << 4)  // modulate stage RGB by inverse fog factor
#define GPU_FLAG_FOG_MODULATE_ALPHA (1 << 5)  // modulate stage alpha by inverse fog factor
#define GPU_FLAG_FOG_PASS           (1 << 6)  // this draw IS the fog pass (blend fog color)
#define GPU_FLAG_DLIGHT_PASS        (1 << 7)  // dynamic light pass (compute TC + color from light)
#define GPU_FLAG_DLIGHT_BACKSIDES   (1 << 8)  // allow dlight on backfaces (r_dlightBacks)
#define GPU_FLAG_SKINNING           (1 << 9)  // GPU skeletal skinning (Ghoul2)
#define GPU_FLAG_MULTI_DLIGHT_PASS  (1 << 10) // single-pass multi-dlight (dlight data packed in boneMatrices region)

// Maximum number of bones for GPU skeletal skinning
#define GPU_MAX_BONES  72

// Per-draw UBO data (std140 layout, must match GLSL)
// Passed via dynamic uniform buffer (descriptor set 2)
typedef struct {
	uint32_t  gpuFlags;            // 0
	float     backlerp;            // 4
	float     identityLight;       // 8
	float     pad0;                // 12
	float     viewOrigin[4];       // 16 (xyz + pad) — eye position in model space
	float     ambientLight[4];     // 32 (rgb + ambientLightInt as float bits)
	float     directedLight[4];    // 48 (rgb + pad)
	float     entityLightDir[4];   // 64 (xyz + pad) — entity light direction
	float     fogDistVec[4];       // 80 — fog distance plane equation
	float     fogDepthVec[4];      // 96 — fog depth plane equation
	float     fogColor[4];         // 112 (rgba)
	float     fogEyeT;             // 128
	float     fogEyeOutside;       // 132 (0.0 or 1.0)
	float     pad1[2];             // 136
	float     dlightOrigin[4];     // 144 (xyz in model space, w = radius)
	float     dlightColor[4];      // 160 (rgb, w = additive flag)
	float     specLightOrigin[4];  // 176 (xyz for specular, w unused)
	// --- Bone matrices for GPU skinning (offset 192) ---
	// Each bone is a 3x4 matrix stored as 3 vec4 rows.
	// boneMatrices[boneIndex * 12 .. boneIndex * 12 + 11]
	float     boneMatrices[GPU_MAX_BONES * 12]; // 72 bones × 12 floats = 3456 bytes
} gpuParams_t;                    // 3648 bytes total

// Base param size (without bones) for non-skinned draws
#define GPU_PARAMS_BASE_SIZE  192
// Full param size (with bones) for skinned draws
#define GPU_PARAMS_FULL_SIZE  (GPU_PARAMS_BASE_SIZE + GPU_MAX_BONES * 12 * (int)sizeof(float)) // 3648
// Must be aligned to minUniformBufferOffsetAlignment
#define GPU_PARAMS_ALIGN 256  // safe default, actual alignment queried at init

// Cull mode values for pipeline key (custom enum, not VkCullModeFlagBits)
#define VKCULL_NONE		0
#define VKCULL_FRONT	1
#define VKCULL_BACK		2

// Pipeline state key - used to cache pipeline objects
typedef struct {
	VkRenderPass		renderPass;		// render pass this pipeline is compatible with
	unsigned int	srcBlend;		// GLS_SRCBLEND_xxx
	unsigned int	dstBlend;		// GLS_DSTBLEND_xxx
	int				depthWrite;
	int				depthTestDisable;
	int				depthTestEqual;
	int				wireframe;
	int				alphaTest;		// 0=none, shifted alpha test bits
	int				multiTexture;	// 0=single tex, 1=multi tex
	int				cullMode;		// VKCULL_NONE, VKCULL_FRONT, VKCULL_BACK
	qboolean		polygonOffset;
} vkPipelineKey_t;

// Cached pipeline entry
typedef struct {
	vkPipelineKey_t	key;
	VkPipeline		pipeline;
	qboolean		occupied;
} vkPipelineEntry_t;

// Hash table size for pipeline cache (must be power of 2, > VK_MAX_PIPELINES)
#define VK_PIPELINE_HASH_SIZE	512

// vkImage_t is defined in tr_local.h (inside image_t struct)
// It contains: VkImage image, VkDeviceMemory memory, VkImageView view, VkSampler sampler, VkDescriptorSet descriptorSet

// Deferred image resource destruction (freed after per-frame fence wait)
#define VK_MAX_DEFERRED_IMAGES 256
typedef struct {
	VkDescriptorSet		descriptorSet;
	VkImageView			view;
	VkImage				image;
	VkDeviceMemory		memory;
} vkDeferredImageDestroy_t;

// Per-frame command buffer and synchronization
typedef struct {
	VkCommandBuffer		commandBuffer;
	VkFence				fence;
	VkSemaphore			imageAvailableSemaphore;
	qboolean			active;		// currently recording
} vkFrame_t;

// Dynamic buffer for vertex/index data - per frame
typedef struct {
	VkBuffer			vertexBuffer;
	VkDeviceMemory		vertexMemory;
	byte				*vertexData;
	int					vertexOffset;

	VkBuffer			indexBuffer;
	VkDeviceMemory		indexMemory;
	byte				*indexData;
	int					indexOffset;

	VkBuffer			uniformBuffer;
	VkDeviceMemory		uniformMemory;
	byte				*uniformData;
	int					uniformOffset;
} vkDynamicBuffers_t;

// Staging buffer for texture uploads
typedef struct {
	VkBuffer			buffer;
	VkDeviceMemory		memory;
	byte				*data;
	int					offset;
} vkStagingBuffer_t;

// Maximum number of progressive bloom mip levels
#define BLOOM_MAX_MIPS 6

// Single mip level in the progressive bloom chain
typedef struct {
	VkImage				image;
	VkImageView			imageView;
	VkDeviceMemory		memory;
	VkDescriptorSet		descriptorSet;
	VkFramebuffer		framebuffer;			// created with blurRenderPass (downsample, loadOp=CLEAR)
	VkFramebuffer		upsampleFramebuffer;	// created with bloomUpsampleRenderPass (upsample, loadOp=LOAD)
	uint32_t			width;
	uint32_t			height;
} bloomMipLevel_t;

// Glow/post-process resources
typedef struct {
	// Half-res scene image (final blur output, used by composite)
	VkImage				sceneImage;
	VkImageView			sceneImageView;
	VkDeviceMemory		sceneImageMemory;
	VkDescriptorSet		sceneDescriptorSet;

	// Full-res glow image (glow objects rendered here with main depth)
	VkImage				glowImage;
	VkImageView			glowImageView;
	VkDeviceMemory		glowImageMemory;
	VkDescriptorSet		glowDescriptorSet;

	// Half-res blur image (ping-pong target, used by legacy Gaussian blur)
	VkImage				blurImage;
	VkImageView			blurImageView;
	VkDeviceMemory		blurImageMemory;
	VkDescriptorSet		blurDescriptorSet;

	// Full-res framebuffer: glowImage + main depth (for glow scene rendering)
	VkFramebuffer		glowFramebuffer;
	// Half-res framebuffers (for blur passes, color only)
	VkFramebuffer		blurFramebuffer;
	VkFramebuffer		sceneFramebuffer;

	// Render passes
	VkRenderPass		glowRenderPass;		// full-res, color+depth (scene rendering)
	VkRenderPass		blurRenderPass;		// color only, loadOp=CLEAR (downsample)
	VkRenderPass		bloomUpsampleRenderPass; // color only, loadOp=LOAD (additive upsample)

	VkPipeline			blurPipeline;
	VkPipeline			glowCompositePipeline;

	// Progressive bloom (downsample/upsample chain)
	VkPipeline			bloomDownsamplePipeline;
	VkPipeline			bloomUpsamplePipeline;
	bloomMipLevel_t		bloomMips[BLOOM_MAX_MIPS];
	int					bloomMipCount;

	// Cached blur target dimensions (from r_DynamicGlowWidth/Height or swapchain/2)
	uint32_t			halfWidth;
	uint32_t			halfHeight;
} vkGlowResources_t;

// Maximum number of glow light sources uploaded to the GPU each frame
#define GLOW_RT_MAX_SOURCES		32

// Push constant layout for glow reflect RT shaders (must match glow_reflect.rgen)
typedef struct {
	float	inverseVP[16];				// mat4  (64 bytes)
	float	viewProjection[16];			// mat4  (64 bytes): for reprojecting hit pos to screen UV
	float	cameraPosAndIntensity[4];	// vec4  (16 bytes): xyz=pos, w=intensity
	int		numSources;					// int   (4 bytes)
	float	bias;						// float (4 bytes)
	float	falloffExponent;				// float (4 bytes): distance falloff curve steepness
	float	g2ReflectScale;				// float (4 bytes): reflection scale for Ghoul2 model surfaces
	float	shadowIntensity;			// float (4 bytes): visibility when shadow ray is occluded
} glowReflectPC_t;						// Total: 164 bytes

// Glow source upload struct (must match GLSL GlowSource)
typedef struct {
	float	posAndRadius[4];			// vec4: xyz=start pos (or point pos), w=effect radius
	float	colorAndPower[4];			// vec4: xyz=RGB color, w=intensity
	float	endPosAndType[4];			// vec4: xyz=end pos, w=0 (point) or 1 (line)
} glowSourceData_t;						// 48 bytes

// RT function pointers (loaded dynamically since they're extension functions)
typedef struct {
	PFN_vkGetAccelerationStructureBuildSizesKHR  vkGetAccelerationStructureBuildSizesKHR;
	PFN_vkCreateAccelerationStructureKHR         vkCreateAccelerationStructureKHR;
	PFN_vkDestroyAccelerationStructureKHR        vkDestroyAccelerationStructureKHR;
	PFN_vkCmdBuildAccelerationStructuresKHR      vkCmdBuildAccelerationStructuresKHR;
	PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR;
	PFN_vkCreateRayTracingPipelinesKHR           vkCreateRayTracingPipelinesKHR;
	PFN_vkGetRayTracingShaderGroupHandlesKHR     vkGetRayTracingShaderGroupHandlesKHR;
	PFN_vkCmdTraceRaysKHR                        vkCmdTraceRaysKHR;
	PFN_vkGetBufferDeviceAddressKHR              vkGetBufferDeviceAddressKHR;
} vkRTFunctions_t;

// Maximum number of Ghoul2 entities represented as proxy boxes in the TLAS
#define GLOW_RT_MAX_GHOUL2_ENTITIES	32

// GPU-accelerated glow reflection resources (VK_KHR_ray_tracing_pipeline)
typedef struct {
	// ---- Acceleration Structure (BSP — static, built once per map) ----
	VkAccelerationStructureKHR	blas;				// bottom-level (world geometry)
	VkBuffer				blasBuffer;
	VkDeviceMemory			blasMemory;

	// Optional BLAS for "see-through" world surfaces (grates, fences, etc.)
	// Approximated via any-hit stochastic opacity.
	VkAccelerationStructureKHR	blasSeeThrough;
	VkBuffer				blasSeeThroughBuffer;
	VkDeviceMemory			blasSeeThroughMemory;
	VkBuffer				seeThroughVertexBuffer;
	VkDeviceMemory			seeThroughVertexMemory;
	VkBuffer				seeThroughIndexBuffer;
	VkDeviceMemory			seeThroughIndexMemory;
	uint32_t				seeThroughNumVertices;
	uint32_t				seeThroughNumIndices;

	VkAccelerationStructureKHR	tlas;				// top-level (BSP + Ghoul2 instances)
	VkBuffer				tlasBuffer;
	VkDeviceMemory			tlasMemory;

	// Geometry buffers (persistent, built once per map load)
	VkBuffer				vertexBuffer;
	VkDeviceMemory			vertexMemory;
	VkBuffer				indexBuffer;
	VkDeviceMemory			indexMemory;
	uint32_t				numVertices;
	uint32_t				numIndices;

	// ---- Dynamic Ghoul2 proxy geometry (rebuilt every frame) ----
	VkAccelerationStructureKHR	ghoul2Blas;
	VkBuffer				ghoul2BlasBuffer;
	VkDeviceMemory			ghoul2BlasMemory;
	VkBuffer				ghoul2VertexBuffer;		// DEVICE_LOCAL, updated via vkCmdUpdateBuffer
	VkDeviceMemory			ghoul2VertexMemory;
	VkBuffer				ghoul2IndexBuffer;		// DEVICE_LOCAL, updated via vkCmdUpdateBuffer
	VkDeviceMemory			ghoul2IndexMemory;
	VkBuffer				ghoul2ScratchBuffer;
	VkDeviceMemory			ghoul2ScratchMemory;

	// ---- Per-frame TLAS rebuild resources ----
	VkBuffer				tlasInstanceBuffer;		// DEVICE_LOCAL, updated via vkCmdUpdateBuffer
	VkDeviceMemory			tlasInstanceMemory;
	VkBuffer				tlasScratchBuffer;
	VkDeviceMemory			tlasScratchMemory;

	// ---- RT Pipeline ----
	VkDescriptorSetLayout	rtDescriptorSetLayout;
	VkPipelineLayout		rtPipelineLayout;
	VkPipeline				rtPipeline;
	VkDescriptorSet			rtDescriptorSet;

	// ---- Shader Binding Table (SBT) ----
	VkBuffer				sbtBuffer;
	VkDeviceMemory			sbtMemory;
	VkStridedDeviceAddressRegionKHR rgenRegion;
	VkStridedDeviceAddressRegionKHR missRegion;
	VkStridedDeviceAddressRegionKHR hitRegion;
	VkStridedDeviceAddressRegionKHR callRegion;	// unused but required

	// ---- Ping-pong output images (eliminates per-frame copy for temporal) ----
	// Frame N writes to ppImage[pingPongIndex], reads history from ppImage[1-pingPongIndex].
	// After dispatch, pingPongIndex flips. No vkCmdCopyImage needed.
	VkImage					ppImage[2];
	VkImageView				ppImageView[2];
	VkDeviceMemory			ppImageMemory[2];
	VkDescriptorSet			ppRtDescriptorSet[2];		// RT descriptor sets with swapped output/history bindings
	VkDescriptorSet			ppOutputDescriptorSet[2];	// combined image sampler (GENERAL layout) for compositing
	VkDescriptorSet			ppOutputReadDescriptorSet[2]; // combined image sampler (SHADER_READ_ONLY) for blur sampling
	int						pingPongIndex;				// 0 or 1, flipped each frame

	// ---- Screen-space blur for RT output (softens hard shadow edges) ----
	VkImage					blurTempImage;			// H pass output
	VkImageView				blurTempImageView;
	VkDeviceMemory			blurTempImageMemory;
	VkDescriptorSet			blurTempDescriptorSet;
	VkFramebuffer			blurTempFramebuffer;

	VkImage					blurOutputImage;		// V pass output (final blurred)
	VkImageView				blurOutputImageView;
	VkDeviceMemory			blurOutputImageMemory;
	VkDescriptorSet			blurOutputDescriptorSet;
	VkFramebuffer			blurOutputFramebuffer;

	VkPipeline				blurMaskedPipeline;		// alpha-masked blur (skips Ghoul2 pixels)
	VkPipeline				g2CompositePipeline;	// (ONE_MINUS_SRC_ALPHA, ONE) for raw Ghoul2 overlay

	// (History image removed — ping-pong ppImage[2] replaces output + history)

	// ---- Depth-only image view for RT shader ----
	VkImageView				depthOnlyImageView;

	// ---- Glow sources uniform buffer (per-frame, uploaded from CPU) ----
	VkBuffer				glowSourcesBuffer;
	VkDeviceMemory			glowSourcesMemory;
	void					*glowSourcesMapped;

	// ---- Per-frame RT params UBO (prev VP, frame index, temporal settings) ----
	VkBuffer				rtParamsBuffer;
	VkDeviceMemory			rtParamsMemory;
	void					*rtParamsMapped;
	uint32_t				rtFrameIndex;
	float					prevViewProjection[16];

	// ---- RT properties (queried from device) ----
	uint32_t				shaderGroupHandleSize;
	uint32_t				shaderGroupHandleAlignment;
	uint32_t				shaderGroupBaseAlignment;

	int						lastFrameGhoul2Count;	// Ghoul2 entity count from previous frame (for TLAS skip)

	uint32_t				rtWidth;				// RT dispatch resolution (may be less than swapchain)
	uint32_t				rtHeight;

	qboolean				asBuilt;				// true after AS built for current map
	qboolean				available;				// true if all resources created successfully
	qboolean				blurActive;				// true when blur was performed this frame
	qboolean				hasGhoul2;				// true when G2 was in the TLAS this frame
} vkGlowReflectResources_t;

// Gamma correction resources
typedef struct {
	// Offscreen scene image (main rendering draws here instead of swapchain)
	VkImage				sceneImage;
	VkImageView			sceneImageView;
	VkDeviceMemory		sceneImageMemory;
	VkDescriptorSet		sceneDescriptorSet;

	// Scene render passes (same attachment format as vk.renderPass, but finalLayout = SHADER_READ_ONLY)
	VkRenderPass		sceneRenderPass;		// clear variant
	VkRenderPass		sceneRenderPassLoad;	// load variant (resume after glow)

	// Single framebuffer for scene rendering (offscreen color + shared depth)
	VkFramebuffer		sceneFramebuffer;

	// Gamma-specific render pass (color only, writes to swapchain, finalLayout = PRESENT_SRC)
	VkRenderPass		gammaRenderPass;

	// Swapchain overlay render pass for UI (color + depth, LOAD swapchain, finalLayout = PRESENT_SRC)
	VkRenderPass		overlayRenderPass;
	VkFramebuffer		overlayFramebuffers[VK_MAX_SWAPCHAIN_IMAGES];

	// Per-swapchain-image framebuffers for the gamma render pass (swapchain color only)
	VkFramebuffer		gammaFramebuffers[VK_MAX_SWAPCHAIN_IMAGES];

	// Gamma-specific pipeline layout (16-byte push constants, 1 descriptor set)
	VkPipelineLayout	gammaPipelineLayout;

	VkPipeline			gammaPipeline;       // FXAA_ENABLED=true  variant
	VkPipeline			gammaPipelineNoFxaa; // FXAA_ENABLED=false variant (prunes FXAA dead code)

	// FXAA (scene-only) pipeline
	int					fxaaQuality; // 0=off, otherwise preset driven by r_fxaa
	qboolean				fxaaActive;
	qboolean				appliedThisFrame;
	qboolean				sceneRenderedThisFrame;
	qboolean				uiFirstThisFrame; // first draws were 2D (menus/loading) -> keep UI in scene pass for correctness
	qboolean			enabled;		// true if all gamma resources created successfully
} vkGammaResources_t;

// The main Vulkan state structure
typedef struct {
	VkInstance			instance;
	VkDebugUtilsMessengerEXT	debugMessenger;
	VkPhysicalDevice	physicalDevice;
	VkDevice			device;

	VkPhysicalDeviceProperties		deviceProperties;
	VkPhysicalDeviceMemoryProperties memoryProperties;

	uint32_t			graphicsQueueFamily;
	uint32_t			presentQueueFamily;
	VkQueue				graphicsQueue;
	VkQueue				presentQueue;

	VkSurfaceKHR		surface;
	VkSwapchainKHR		swapchain;
	VkFormat			swapchainFormat;
	VkFormat			sceneFormat;		// HDR: R16G16B16A16_SFLOAT, otherwise same as swapchainFormat
	VkExtent2D			swapchainExtent;
	uint32_t			swapchainImageCount;
	VkImage				swapchainImages[VK_MAX_SWAPCHAIN_IMAGES];
	VkImageLayout		swapchainImageLayouts[VK_MAX_SWAPCHAIN_IMAGES];
	VkImageView			swapchainImageViews[VK_MAX_SWAPCHAIN_IMAGES];

	VkImage				depthImage;
	VkImageView			depthImageView;
	VkDeviceMemory		depthImageMemory;
	VkFormat			depthFormat;

	VkImage				stencilImage;
	VkImageView			stencilImageView;

	VkRenderPass		renderPass;
	VkRenderPass		renderPassLoad;
	VkFramebuffer		framebuffers[VK_MAX_SWAPCHAIN_IMAGES];

	// Active render pass used for pipeline compatibility (set by VK_Begin*RenderPass).
	VkRenderPass		pipelineRenderPass;

	VkDescriptorPool	descriptorPool;
	VkDescriptorSetLayout descriptorSetLayout;
	VkPipelineLayout	pipelineLayout;
	VkPipelineCache		pipelineCache;

	VkCommandPool		commandPool;

	// Per-frame resources
	vkFrame_t			frames[VK_NUM_COMMAND_BUFFERS];
	VkSemaphore			renderFinishedSemaphores[VK_MAX_SWAPCHAIN_IMAGES];
	int					currentFrame;
	uint32_t			currentSwapchainImage;
	qboolean			frameStarted;

	// Dynamic buffers (per frame)
	vkDynamicBuffers_t	dynBuffers[VK_NUM_COMMAND_BUFFERS];

	// Staging buffer for texture uploads
	vkStagingBuffer_t	staging;
	VkCommandBuffer		stagingCommandBuffer;

	// Sampler objects
	VkSampler			samplerMipRepeat;
	VkSampler			samplerMipClamp;
	VkSampler			samplerNoMipRepeat;
	VkSampler			samplerNoMipClamp;
	VkSampler			samplerNearest;

	// Image slots for descriptors
	int					imageCount;

	// Pipeline cache (open-addressing hash table)
	vkPipelineEntry_t	pipelines[VK_PIPELINE_HASH_SIZE];
	int					pipelineCount;

	// UBO descriptor set layout and per-frame descriptor sets
	VkDescriptorSetLayout uboDescriptorSetLayout;
	VkDescriptorSet		uboDescriptorSets[VK_NUM_COMMAND_BUFFERS];
	VkDeviceSize		uboAlignment;		// minUniformBufferOffsetAlignment
	int					zeroUBOOffset[VK_NUM_COMMAND_BUFFERS];	// offset of zeroed UBO written at frame start
	int					zeroTc1Offset[VK_NUM_COMMAND_BUFFERS];	// offset of zeroed tc1 region in vertex buffer
	int					zeroNormOffset[VK_NUM_COMMAND_BUFFERS];	// offset of zeroed normal region in vertex buffer

	// Shader modules
	VkShaderModule		singleTexVertShader;
	VkShaderModule		singleTexFragShader;
	VkShaderModule		multiTexFragShader;
	VkShaderModule		multiTexAddFragShader;

	// Post-process shaders
	VkShaderModule		gammaVertShader;
	VkShaderModule		gammaFragShader;
	VkShaderModule		blurVertShader;
	VkShaderModule		blurFragShader;
	VkShaderModule		blurMaskedFragShader;
	VkShaderModule		glowCompositeFragShader;
	VkShaderModule		glowDownsampleFragShader;
	VkShaderModule		glowUpsampleFragShader;
	VkShaderModule		glowReflectRgenShader;
	VkShaderModule		glowReflectRmissShader;
	VkShaderModule		glowReflectRchitShader;
	VkShaderModule		glowReflectRahitShader;

	// Glow and gamma correction
	vkGlowResources_t	glow;
	vkGlowReflectResources_t glowReflect;
	vkGammaResources_t  gamma;

	// Stencil shadow volume pipelines
	VkPipeline			shadowStencilIncrPipeline;	// front faces: incr stencil on depth pass
	VkPipeline			shadowStencilDecrPipeline;	// back faces: decr stencil on depth pass
	VkPipeline			shadowFinishPipeline;		// draw darkening quad where stencil != 0

	// Current state for draw calls
	float				clearColor[4];
	qboolean			depthWriteEnabled;
	qboolean			renderPassActive;
	qboolean			uiPassActive;

	// Projection matrix for 2D rendering
	float				projMatrix2D[16];

	// Current model-view-projection
	float				mvpMatrix[16];

	// Does the device support the features we need?
	qboolean			fragmentStoresAndAtomics;
	qboolean			fillModeNonSolid;
	qboolean			rayTracingSupported;		// VK_KHR_ray_tracing_pipeline available
	qboolean			bcSupported;				// VK_FORMAT_BC7_UNORM_BLOCK available

	// RT extension function pointers
	vkRTFunctions_t		rtFuncs;

	qboolean			initialized;

	// Set when VK_ReadPixels submits the CB mid-frame
	qboolean			pixelsCapturedThisFrame;

	// Set when a submission has already waited on the swapchain acquire semaphore
	// for the current frame (VK_ReadPixels submits mid-frame and consumes it).
	qboolean			acquireSemaphoreConsumedThisFrame;

	// Per-frame deferred image resource destruction (freed after fence wait)
	vkDeferredImageDestroy_t	deferredImages[VK_NUM_COMMAND_BUFFERS][VK_MAX_DEFERRED_IMAGES];
	int							deferredImageCount[VK_NUM_COMMAND_BUFFERS];

	// Cached bindings to skip redundant Vulkan calls during recording
	VkDescriptorSet		boundTextureSets[2];
	VkPipeline			boundPipeline;
	uint32_t			boundUBOOffset;

	// ============================================================
	// Static world geometry buffers (device-local, uploaded at map load)
	// ============================================================
	VkBuffer			staticVertexBuffer;
	VkDeviceMemory		staticVertexMemory;
	VkBuffer			staticIndexBuffer;
	VkDeviceMemory		staticIndexMemory;

	// Byte offsets of each attribute section within staticVertexBuffer
	VkDeviceSize		staticPosOffset;	// positions (vec4)
	VkDeviceSize		staticTc0Offset;	// texcoord0 (vec2)
	VkDeviceSize		staticTc1Offset;	// texcoord1/lightmap (vec2)
	VkDeviceSize		staticColorOffset;	// vertex colors (RGBA8)
	VkDeviceSize		staticNormOffset;	// normals (vec4)

	int					staticTotalVertices;
	int					staticTotalIndexes;
	qboolean			staticBuffersValid;
} vkState_t;

extern vkState_t vk;

// ============================================================
// Vulkan initialization and shutdown
// ============================================================
qboolean	VK_Init( void );
void		VK_Shutdown( void );
void		VK_FlushDeferredResources( void );
const char *VK_GetEnabledExtensionsString( void );
void		VK_CreateSwapchain( void );
void		VK_DestroySwapchain( void );
void		VK_CreateDepthBuffer( void );
void		VK_CreateRenderPass( void );
void		VK_CreateFramebuffers( void );
void		VK_CreateCommandPool( void );
void		VK_CreateCommandBuffers( void );
void		VK_CreateSyncObjects( void );
void		VK_CreateDynamicBuffers( void );
void		VK_CreateStagingBuffer( void );
void		VK_CreateSamplers( void );
void		VK_CreateDescriptorPool( void );
void		VK_CreateDescriptorSetLayout( void );
void		VK_CreatePipelineLayout( void );
void		VK_CreateShaderModules( void );

// ============================================================
// Vulkan pipeline management
// ============================================================
VkPipeline	VK_FindPipeline( const vkPipelineKey_t *key );
VkPipeline	VK_CreatePipelineFromKey( const vkPipelineKey_t *key );
vkPipelineKey_t VK_PipelineKeyFromState( unsigned int stateBits, int cullType, qboolean multiTexture, qboolean polygonOffset );
void		VK_BindPipeline( unsigned int stateBits, int cullType, qboolean multiTexture, qboolean polygonOffset );

// ============================================================
// Vulkan image/texture management
// ============================================================
void	VK_CreateImage( image_t *image, const byte *pic, int width, int height, qboolean mipmap, qboolean clampToEdge );
void	VK_UpdateImage( image_t *image, const byte *pic, int width, int height );
void	VK_UpdateImageSubRegion( vkImage_t *vkImg, int width, int height, int xoff, int yoff, const byte *pixels );
void	VK_DestroyImage( image_t *image );
void	VK_DestroyImageResources( vkImage_t *vkImg );
VkDescriptorSet VK_AllocateImageDescriptor( VkImageView view, VkSampler sampler );
void	VK_CreateRenderTargetImage( VkImage *image, VkDeviceMemory *memory, VkImageView *view,
									uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage );

// ============================================================
// Vulkan frame management
// ============================================================
void	VK_BeginFrame( void );
void	VK_EndFrame( void );
void	VK_BeginRenderPass( void );void		VK_BeginRenderPassLoad( void );void	VK_EndRenderPass( void );
void	VK_RecreateSwapchain( void );

// ============================================================
// Vulkan draw calls - replacing GL immediate mode and state
// ============================================================
void	VK_DrawIndexed( int numVerts, const float *xyz, const float *texCoords0,
						const float *texCoords1, const byte *colors,
						int numIndexes, const glIndex_t *indexes );void	VK_DrawIndexedWithNormals( int numVerts, const float *xyz, const float *normals,
					const float *texCoords0, const float *texCoords1,
					const byte *colors, int numIndexes, const glIndex_t *indexes );
qboolean	VK_UpdateGPUParams( const gpuParams_t *params );void	VK_DrawQuad( float x0, float y0, float x1, float y1,
					 float s0, float t0, float s1, float t1,
					 const byte *color );
void	VK_SetViewport( float x, float y, float width, float height, float minDepth, float maxDepth );
void	VK_SetScissor( int x, int y, int width, int height );
void	VK_SetDepthRange( float minDepth, float maxDepth );
void	VK_Clear( unsigned int clearBits, float r, float g, float b, float a );
void	VK_SetMVP( const float *mvp );
void	VK_Set2D( void );
void	VK_SetPushConstants( const vkPushConstants_t *pc );
void	VK_ReadPixels( int x, int y, int width, int height, int format, byte *buffer );

// ============================================================
// Static world VBO management
// ============================================================
void	VK_CreateStaticWorldBuffers( int totalVertices, int totalIndexes,
			const byte *vertexData, VkDeviceSize vertexDataSize,
			const byte *indexData, VkDeviceSize indexDataSize );
void	VK_DestroyStaticWorldBuffers( void );

// ============================================================
// Cached geometry (avoid re-uploading pos/normal/idx across stages)
// ============================================================
void	VK_CacheTessGeometry( void );
void	VK_DrawWithCachedGeo( int numVerts, const float *tc0, const float *tc1,
			const byte *colors, int numIndexes, const glIndex_t *indexes );
void	VK_DrawFromStaticBuffers( int firstVertex, int numVertices,
			int firstIndex, int numIndexes,
			const float *dynTc0, const float *dynTc1, const byte *dynColors,
			qboolean useDynamicVaryings );

// ============================================================
// Vulkan texture binding (replacing GL_Bind)
// ============================================================
void	VK_BindImage( int textureUnit, image_t *image );
void	VK_SetTextureMode( const char *string );

// ============================================================
// Glow and gamma post-processing
// ============================================================
void	VK_CreateGlowResources( void );
void	VK_DestroyGlowResources( void );
void	VK_BlurGlowTexture( void );
void	VK_DrawGlowOverlay( void );
void	VK_CreateGlowReflectResources( void );
void	VK_DestroyGlowReflectResources( void );
void	VK_InvalidateGlowReflectAccelStruct( void );
void	VK_BuildGlowReflectAccelStruct( void );
void	VK_DispatchGlowReflect( void );
void	VK_DrawGlowReflectOverlay( void );
void	VK_BlurGlowReflectOutput( void );
void	VK_CreateGammaResources( void );
void	VK_DestroyGammaResources( void );
void	VK_ApplyGammaCorrection( void );
int		VK_ClampFxaaQuality( int v );

// ============================================================
// Stencil shadow pipelines
// ============================================================
void	VK_CreateShadowPipelines( void );
void	VK_DestroyShadowPipelines( void );

// ============================================================
// Utility functions
// ============================================================
uint32_t VK_FindMemoryType( uint32_t typeFilter, VkMemoryPropertyFlags properties );
VkFormat VK_FindDepthFormat( void );
VkCommandBuffer VK_BeginSingleTimeCommands( void );
void	VK_EndSingleTimeCommands( VkCommandBuffer commandBuffer );
void	VK_TransitionImageLayout( VkImage image, VkFormat format,
								  VkImageLayout oldLayout, VkImageLayout newLayout, int mipLevels, VkCommandBuffer cmdBuffer = VK_NULL_HANDLE );
void	VK_CopyBufferToImage( VkBuffer buffer, VkImage image, uint32_t width, uint32_t height );
void	VK_GenerateMipmaps( VkImage image, VkFormat format, int texWidth, int texHeight, int mipLevels );

#endif // !DEDICATED

#endif // VK_LOCAL_H
