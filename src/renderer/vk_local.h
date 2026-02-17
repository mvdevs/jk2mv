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
#define VK_STAGING_BUFFER_SIZE		(16 * 1024 * 1024) // for texture uploads
#define VK_MAX_IMAGE_SLOTS			4096

// Push constant structure matching the fixed-function pipeline transforms
typedef struct {
	float mvp[16];			// model-view-projection matrix
} vkUniformBlock_t;

// Per-draw push constants
typedef struct {
	float mvp[16];
	float color[4];			// global color modulation
	float texEnvMode;		// 0=modulate, 1=replace, 2=decal, 3=add
	float alphaTestFunc;	// 0=none, 1=GT0, 2=LT80, 3=GE80, 4=GEC0
	float alphaTestValue;
	float depthRange[2];	// near, far
	float gamma;			// gamma correction value (1.0 = no correction)
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
} vkPipelineEntry_t;

// vkImage_t is defined in tr_local.h (inside image_t struct)
// It contains: VkImage image, VkDeviceMemory memory, VkImageView view, VkSampler sampler, VkDescriptorSet descriptorSet

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

	// Half-res blur image (ping-pong target)
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
	VkRenderPass		blurRenderPass;		// half-res, color only (blur passes)

	VkPipeline			blurPipeline;
	VkPipeline			glowCompositePipeline;

	// Cached blur target dimensions (from r_DynamicGlowWidth/Height or swapchain/2)
	uint32_t			halfWidth;
	uint32_t			halfHeight;
} vkGlowResources_t;

// Maximum number of glow light sources uploaded to the GPU each frame
#define GLOW_RT_MAX_SOURCES		32

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

	// ---- Output image (RT writes here, composited onto scene) ----
	VkImage					outputImage;
	VkImageView				outputImageView;
	VkDeviceMemory			outputImageMemory;
	VkDescriptorSet			outputDescriptorSet;	// combined image sampler for compositing

	// ---- Depth-only image view for RT shader ----
	VkImageView				depthOnlyImageView;

	// ---- Glow sources uniform buffer (per-frame, uploaded from CPU) ----
	VkBuffer				glowSourcesBuffer;
	VkDeviceMemory			glowSourcesMemory;
	void					*glowSourcesMapped;

	// ---- RT properties (queried from device) ----
	uint32_t				shaderGroupHandleSize;
	uint32_t				shaderGroupHandleAlignment;
	uint32_t				shaderGroupBaseAlignment;

	int						lastFrameGhoul2Count;	// Ghoul2 entity count from previous frame (for TLAS skip)

	qboolean				asBuilt;				// true after AS built for current map
	qboolean				available;				// true if all resources created successfully
} vkGlowReflectResources_t;

// Gamma correction resources
typedef struct {
	VkImage				lutImage;
	VkImageView			lutImageView;
	VkDeviceMemory		lutImageMemory;
	VkDescriptorSet		lutDescriptorSet;
	VkPipeline			gammaPipeline;
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
	VkExtent2D			swapchainExtent;
	uint32_t			swapchainImageCount;
	VkImage				swapchainImages[VK_MAX_SWAPCHAIN_IMAGES];
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

	// Pipeline cache
	vkPipelineEntry_t	pipelines[VK_MAX_PIPELINES];
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
	VkShaderModule		glowCompositeFragShader;
	VkShaderModule		glowReflectRgenShader;
	VkShaderModule		glowReflectRmissShader;
	VkShaderModule		glowReflectRchitShader;

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

	// Projection matrix for 2D rendering
	float				projMatrix2D[16];

	// Current model-view-projection
	float				mvpMatrix[16];

	// Does the device support the features we need?
	qboolean			fragmentStoresAndAtomics;
	qboolean			fillModeNonSolid;
	qboolean			rayTracingSupported;		// VK_KHR_ray_tracing_pipeline available

	// RT extension function pointers
	vkRTFunctions_t		rtFuncs;

	qboolean			initialized;

	// Set when VK_ReadPixels submits the CB mid-frame
	qboolean			pixelsCapturedThisFrame;

	// Deferred deletion of descriptor sets to avoid freeing while recording
	VkDescriptorSet		deferredFreeSets[1024];
	int					deferredFreeCount;

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
void	VK_UpdateGPUParams( const gpuParams_t *params );void	VK_DrawQuad( float x0, float y0, float x1, float y1,
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
void	VK_CreateGammaResources( void );
void	VK_DestroyGammaResources( void );
void	VK_ApplyGammaCorrection( void );

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
								  VkImageLayout oldLayout, VkImageLayout newLayout, int mipLevels );
void	VK_CopyBufferToImage( VkBuffer buffer, VkImage image, uint32_t width, uint32_t height );
void	VK_GenerateMipmaps( VkImage image, VkFormat format, int texWidth, int texHeight, int mipLevels );

#endif // !DEDICATED

#endif // VK_LOCAL_H
