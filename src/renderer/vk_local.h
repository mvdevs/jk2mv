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
#define VK_VERTEX_BUFFER_SIZE		(4 * 1024 * 1024)
#define VK_INDEX_BUFFER_SIZE		(2 * 1024 * 1024)
#define VK_UNIFORM_BUFFER_SIZE		(256 * 1024)
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

	// Glow and gamma correction
	vkGlowResources_t	glow;
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

	qboolean			initialized;

	// Set when VK_ReadPixels submits the CB mid-frame
	qboolean			pixelsCapturedThisFrame;

	// Deferred deletion of descriptor sets to avoid freeing while recording
	VkDescriptorSet		deferredFreeSets[1024];
	int					deferredFreeCount;
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
						int numIndexes, const glIndex_t *indexes );
void	VK_DrawQuad( float x0, float y0, float x1, float y1,
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
