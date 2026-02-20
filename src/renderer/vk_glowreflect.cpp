// vk_glowreflect.cpp — Hardware ray-traced glow reflections
//
// Uses VK_KHR_ray_tracing_pipeline + VK_KHR_acceleration_structure to cast
// reflection rays from world surfaces.  When a reflected ray hits geometry,
// the hit position is projected back to screen space and the glow texture
// is sampled there — this captures the full shape of glow emitters
// (lightsaber blades, disruptor beams, etc.) rather than treating them as
// point lights.

#include "tr_local.h"
#include "../ghoul2/G2_local.h"

#ifndef DEDICATED

#include "vk_local.h"

// ============================================================
// Push constant layout (must match glow_reflect.rgen)
// ============================================================
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

// ============================================================
// Helpers
// ============================================================

static uint32_t alignUp( uint32_t v, uint32_t alignment ) {
	return (v + alignment - 1) & ~(alignment - 1);
}

/*
 * vkCmdUpdateBuffer is limited to 65536 bytes per call.
 * This helper splits larger uploads into conformant chunks.
 */
static void cmdUpdateBufferChunked( VkCommandBuffer cmd, VkBuffer buf,
	VkDeviceSize offset, VkDeviceSize size, const void *data )
{
	const VkDeviceSize kMaxChunk = 65536;
	const char *p = (const char *)data;
	for ( VkDeviceSize off = 0; off < size; off += kMaxChunk ) {
		VkDeviceSize chunk = ( size - off < kMaxChunk ) ? ( size - off ) : kMaxChunk;
		vkCmdUpdateBuffer( cmd, buf, offset + off, chunk, p + off );
	}
}

/*
 * Create a buffer with VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
 * and VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT so its address can be
 * queried for acceleration structure and SBT operations.
 */
static void createRTBuffer( VkDeviceSize size, VkBufferUsageFlags usage,
	VkMemoryPropertyFlags memProps, VkBuffer *buffer, VkDeviceMemory *memory )
{
	VkBufferCreateInfo bufInfo = {};
	bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufInfo.size = size;
	bufInfo.usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	vkCreateBuffer( vk.device, &bufInfo, NULL, buffer );

	VkMemoryRequirements memReqs;
	vkGetBufferMemoryRequirements( vk.device, *buffer, &memReqs );

	VkMemoryAllocateFlagsInfo allocFlags = {};
	allocFlags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
	allocFlags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

	VkMemoryAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.pNext = &allocFlags;
	allocInfo.allocationSize = memReqs.size;
	allocInfo.memoryTypeIndex = VK_FindMemoryType( memReqs.memoryTypeBits, memProps );

	vkAllocateMemory( vk.device, &allocInfo, NULL, memory );
	vkBindBufferMemory( vk.device, *buffer, *memory, 0 );
}

static VkDeviceAddress getBufferAddress( VkBuffer buffer ) {
	VkBufferDeviceAddressInfo addrInfo = {};
	addrInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	addrInfo.buffer = buffer;
	return vk.rtFuncs.vkGetBufferDeviceAddressKHR( vk.device, &addrInfo );
}

/*
 * Invert a 4x4 matrix using cofactor expansion.
 * Returns qfalse if the matrix is singular.
 */
static qboolean invertMatrix4x4( const float *m, float *out ) {
	float inv[16], det;
	int i;

	inv[0]  =  m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15]
	          + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
	inv[4]  = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15]
	          - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
	inv[8]  =  m[4]*m[9]*m[15]  - m[4]*m[11]*m[13] - m[8]*m[5]*m[15]
	          + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
	inv[12] = -m[4]*m[9]*m[14]  + m[4]*m[10]*m[13] + m[8]*m[5]*m[14]
	          - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];

	det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
	if ( det == 0.0f ) return qfalse;

	inv[1]  = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15]
	          - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
	inv[5]  =  m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15]
	          + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
	inv[9]  = -m[0]*m[9]*m[15]  + m[0]*m[11]*m[13] + m[8]*m[1]*m[15]
	          - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
	inv[13] =  m[0]*m[9]*m[14]  - m[0]*m[10]*m[13] - m[8]*m[1]*m[14]
	          + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];

	inv[2]  =  m[1]*m[6]*m[15]  - m[1]*m[7]*m[14] - m[5]*m[2]*m[15]
	          + m[5]*m[3]*m[14] + m[13]*m[2]*m[7]  - m[13]*m[3]*m[6];
	inv[6]  = -m[0]*m[6]*m[15]  + m[0]*m[7]*m[14] + m[4]*m[2]*m[15]
	          - m[4]*m[3]*m[14] - m[12]*m[2]*m[7]  + m[12]*m[3]*m[6];
	inv[10] =  m[0]*m[5]*m[15]  - m[0]*m[7]*m[13] - m[4]*m[1]*m[15]
	          + m[4]*m[3]*m[13] + m[12]*m[1]*m[7]  - m[12]*m[3]*m[5];
	inv[14] = -m[0]*m[5]*m[14]  + m[0]*m[6]*m[13] + m[4]*m[1]*m[14]
	          - m[4]*m[2]*m[13] - m[12]*m[1]*m[6]  + m[12]*m[2]*m[5];

	inv[3]  = -m[1]*m[6]*m[11]  + m[1]*m[7]*m[10] + m[5]*m[2]*m[11]
	          - m[5]*m[3]*m[10] - m[9]*m[2]*m[7]   + m[9]*m[3]*m[6];
	inv[7]  =  m[0]*m[6]*m[11]  - m[0]*m[7]*m[10] - m[4]*m[2]*m[11]
	          + m[4]*m[3]*m[10] + m[8]*m[2]*m[7]   - m[8]*m[3]*m[6];
	inv[11] = -m[0]*m[5]*m[11]  + m[0]*m[7]*m[9]  + m[4]*m[1]*m[11]
	          - m[4]*m[3]*m[9]  - m[8]*m[1]*m[7]   + m[8]*m[3]*m[5];
	inv[15] =  m[0]*m[5]*m[10]  - m[0]*m[6]*m[9]  - m[4]*m[1]*m[10]
	          + m[4]*m[2]*m[9]  + m[8]*m[1]*m[6]   - m[8]*m[2]*m[5];

	det = 1.0f / det;
	for ( i = 0; i < 16; i++ )
		out[i] = inv[i] * det;

	return qtrue;
}

// ============================================================
// Acceleration Structure Build
// ============================================================

/*
================
VK_BuildGlowReflectAccelStruct

Build a bottom-level acceleration structure from the BSP world geometry
and wrap it in a top-level acceleration structure.  Called lazily on the
first frame that needs it after a map load.
================
*/
void VK_BuildGlowReflectAccelStruct( void ) {
	if ( !vk.rayTracingSupported || !tr.world ) return;
	if ( vk.glowReflect.asBuilt ) return;

	// ---- Step 1: Count geometry ----
	uint32_t totalVerts = 0, totalIndices = 0;
	for ( int i = 0; i < tr.world->numsurfaces; i++ ) {
		msurface_t *surf = &tr.world->surfaces[i];
		switch ( *surf->data ) {
		case SF_FACE: {
			srfSurfaceFace_t *face = (srfSurfaceFace_t *)surf->data;
			totalVerts += face->numPoints;
			totalIndices += face->numIndices;
			break;
		}
		case SF_TRIANGLES: {
			srfTriangles_t *tri = (srfTriangles_t *)surf->data;
			totalVerts += tri->numVerts;
			totalIndices += tri->numIndexes;
			break;
		}
		case SF_GRID: {
			srfGridMesh_t *grid = (srfGridMesh_t *)surf->data;
			totalVerts += grid->width * grid->height;
			totalIndices += (grid->width - 1) * (grid->height - 1) * 6;
			break;
		}
		default:
			break;
		}
	}

	if ( totalVerts == 0 || totalIndices == 0 ) {
		ri.Printf( PRINT_ALL, "VK_BuildGlowReflectAccelStruct: no world geometry\n" );
		return;
	}

	ri.Printf( PRINT_ALL, "Building RT acceleration structure: %u vertices, %u indices\n",
		totalVerts, totalIndices );

	// ---- Step 2: Fill CPU-side arrays ----
	float *cpuVerts = (float *)ri.Malloc( totalVerts * 3 * sizeof(float), TAG_RENDERER, qfalse );
	uint32_t *cpuIndices = (uint32_t *)ri.Malloc( totalIndices * sizeof(uint32_t), TAG_RENDERER, qfalse );

	uint32_t vertOff = 0, idxOff = 0;
	for ( int i = 0; i < tr.world->numsurfaces; i++ ) {
		msurface_t *surf = &tr.world->surfaces[i];
		uint32_t baseVert = vertOff;
		switch ( *surf->data ) {
		case SF_FACE: {
			srfSurfaceFace_t *face = (srfSurfaceFace_t *)surf->data;
			for ( int v = 0; v < face->numPoints; v++ ) {
				cpuVerts[vertOff*3+0] = face->points[v][0];
				cpuVerts[vertOff*3+1] = face->points[v][1];
				cpuVerts[vertOff*3+2] = face->points[v][2];
				vertOff++;
			}
			unsigned int *faceIdx = (unsigned int *)((char *)face + face->ofsIndices);
			for ( int j = 0; j < face->numIndices; j++ ) {
				cpuIndices[idxOff++] = baseVert + faceIdx[j];
			}
			break;
		}
		case SF_TRIANGLES: {
			srfTriangles_t *tri = (srfTriangles_t *)surf->data;
			for ( int v = 0; v < tri->numVerts; v++ ) {
				cpuVerts[vertOff*3+0] = tri->verts[v].xyz[0];
				cpuVerts[vertOff*3+1] = tri->verts[v].xyz[1];
				cpuVerts[vertOff*3+2] = tri->verts[v].xyz[2];
				vertOff++;
			}
			for ( int j = 0; j < tri->numIndexes; j++ ) {
				cpuIndices[idxOff++] = baseVert + tri->indexes[j];
			}
			break;
		}
		case SF_GRID: {
			srfGridMesh_t *grid = (srfGridMesh_t *)surf->data;
			int w = grid->width, h = grid->height;
			for ( int v = 0; v < w * h; v++ ) {
				cpuVerts[vertOff*3+0] = grid->verts[v].xyz[0];
				cpuVerts[vertOff*3+1] = grid->verts[v].xyz[1];
				cpuVerts[vertOff*3+2] = grid->verts[v].xyz[2];
				vertOff++;
			}
			for ( int row = 0; row < h - 1; row++ ) {
				for ( int col = 0; col < w - 1; col++ ) {
					uint32_t v0 = baseVert + row * w + col;
					uint32_t v1 = v0 + 1;
					uint32_t v2 = v0 + w;
					uint32_t v3 = v2 + 1;
					cpuIndices[idxOff++] = v0;
					cpuIndices[idxOff++] = v2;
					cpuIndices[idxOff++] = v1;
					cpuIndices[idxOff++] = v1;
					cpuIndices[idxOff++] = v2;
					cpuIndices[idxOff++] = v3;
				}
			}
			break;
		}
		default:
			break;
		}
	}

	vk.glowReflect.numVertices = vertOff;
	vk.glowReflect.numIndices  = idxOff;

	// ---- Step 3: Upload to GPU buffers ----
	VkDeviceSize vertBufSize = (VkDeviceSize)vertOff * 3 * sizeof(float);
	VkDeviceSize idxBufSize  = (VkDeviceSize)idxOff * sizeof(uint32_t);

	createRTBuffer( vertBufSize,
		VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
		| VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		&vk.glowReflect.vertexBuffer, &vk.glowReflect.vertexMemory );

	createRTBuffer( idxBufSize,
		VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
		| VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		&vk.glowReflect.indexBuffer, &vk.glowReflect.indexMemory );

	void *mapped;
	vkMapMemory( vk.device, vk.glowReflect.vertexMemory, 0, vertBufSize, 0, &mapped );
	Com_Memcpy( mapped, cpuVerts, (size_t)vertBufSize );
	vkUnmapMemory( vk.device, vk.glowReflect.vertexMemory );

	vkMapMemory( vk.device, vk.glowReflect.indexMemory, 0, idxBufSize, 0, &mapped );
	Com_Memcpy( mapped, cpuIndices, (size_t)idxBufSize );
	vkUnmapMemory( vk.device, vk.glowReflect.indexMemory );

	ri.Free( cpuVerts );
	ri.Free( cpuIndices );

	// ---- Step 4: Build BLAS ----
	VkAccelerationStructureGeometryTrianglesDataKHR trianglesData = {};
	trianglesData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
	trianglesData.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
	trianglesData.vertexData.deviceAddress = getBufferAddress( vk.glowReflect.vertexBuffer );
	trianglesData.vertexStride = 3 * sizeof(float);
	trianglesData.maxVertex = vertOff - 1;
	trianglesData.indexType = VK_INDEX_TYPE_UINT32;
	trianglesData.indexData.deviceAddress = getBufferAddress( vk.glowReflect.indexBuffer );

	VkAccelerationStructureGeometryKHR geometry = {};
	geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
	geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
	geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
	geometry.geometry.triangles = trianglesData;

	VkAccelerationStructureBuildGeometryInfoKHR buildInfo = {};
	buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	buildInfo.geometryCount = 1;
	buildInfo.pGeometries = &geometry;

	uint32_t primitiveCount = idxOff / 3;

	VkAccelerationStructureBuildSizesInfoKHR sizeInfo = {};
	sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
	vk.rtFuncs.vkGetAccelerationStructureBuildSizesKHR( vk.device,
		VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		&buildInfo, &primitiveCount, &sizeInfo );

	// Create BLAS buffer
	createRTBuffer( sizeInfo.accelerationStructureSize,
		VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		&vk.glowReflect.blasBuffer, &vk.glowReflect.blasMemory );

	VkAccelerationStructureCreateInfoKHR asCreateInfo = {};
	asCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
	asCreateInfo.buffer = vk.glowReflect.blasBuffer;
	asCreateInfo.size = sizeInfo.accelerationStructureSize;
	asCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	vk.rtFuncs.vkCreateAccelerationStructureKHR( vk.device, &asCreateInfo, NULL,
		&vk.glowReflect.blas );

	// Scratch buffer for build
	VkBuffer scratchBuf;
	VkDeviceMemory scratchMem;
	VkDeviceSize maxScratch = sizeInfo.buildScratchSize;
	createRTBuffer( maxScratch,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		&scratchBuf, &scratchMem );

	buildInfo.dstAccelerationStructure = vk.glowReflect.blas;
	buildInfo.scratchData.deviceAddress = getBufferAddress( scratchBuf );

	VkAccelerationStructureBuildRangeInfoKHR rangeInfo = {};
	rangeInfo.primitiveCount = primitiveCount;
	const VkAccelerationStructureBuildRangeInfoKHR *pRangeInfos = &rangeInfo;

	// Record + submit BLAS build
	VkCommandBuffer cmd = VK_BeginSingleTimeCommands();
	vk.rtFuncs.vkCmdBuildAccelerationStructuresKHR( cmd, 1, &buildInfo, &pRangeInfos );

	// Memory barrier between BLAS and TLAS builds
	VkMemoryBarrier memBarrier = {};
	memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	memBarrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
	memBarrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
	vkCmdPipelineBarrier( cmd,
		VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
		VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
		0, 1, &memBarrier, 0, NULL, 0, NULL );

	// ---- Step 5: Pre-allocate Ghoul2 proxy BLAS ----
	// Each Ghoul2 entity is approximated by 12 bone-driven octagonal prisms
	// (torso, head, arms, legs) for a smooth human-shaped shadow silhouette.
	// Each prism has 16 verts (8-point ring × 2 ends) and 28 tris.
	// Buffers are DEVICE_LOCAL and updated each frame via vkCmdUpdateBuffer.
	{
		const uint32_t kSegments = 12;
		uint32_t maxG2Verts   = GLOW_RT_MAX_GHOUL2_ENTITIES * kSegments * 16;
		uint32_t maxG2Indices = GLOW_RT_MAX_GHOUL2_ENTITIES * kSegments * 84;
		uint32_t maxG2Tris    = GLOW_RT_MAX_GHOUL2_ENTITIES * kSegments * 28;

		VkDeviceSize g2VertSize = (VkDeviceSize)maxG2Verts * 3 * sizeof(float);
		VkDeviceSize g2IdxSize  = (VkDeviceSize)maxG2Indices * sizeof(uint32_t);

		createRTBuffer( g2VertSize,
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
			| VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&vk.glowReflect.ghoul2VertexBuffer, &vk.glowReflect.ghoul2VertexMemory );

		createRTBuffer( g2IdxSize,
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
			| VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&vk.glowReflect.ghoul2IndexBuffer, &vk.glowReflect.ghoul2IndexMemory );

		// Query BLAS size for max primitives
		VkAccelerationStructureGeometryTrianglesDataKHR g2Tri = {};
		g2Tri.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
		g2Tri.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
		g2Tri.vertexData.deviceAddress = getBufferAddress( vk.glowReflect.ghoul2VertexBuffer );
		g2Tri.vertexStride = 3 * sizeof(float);
		g2Tri.maxVertex = maxG2Verts - 1;
		g2Tri.indexType = VK_INDEX_TYPE_UINT32;
		g2Tri.indexData.deviceAddress = getBufferAddress( vk.glowReflect.ghoul2IndexBuffer );

		VkAccelerationStructureGeometryKHR g2Geom = {};
		g2Geom.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
		g2Geom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
		g2Geom.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
		g2Geom.geometry.triangles = g2Tri;

		VkAccelerationStructureBuildGeometryInfoKHR g2Build = {};
		g2Build.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
		g2Build.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		g2Build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
		g2Build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		g2Build.geometryCount = 1;
		g2Build.pGeometries = &g2Geom;

		VkAccelerationStructureBuildSizesInfoKHR g2Size = {};
		g2Size.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
		vk.rtFuncs.vkGetAccelerationStructureBuildSizesKHR( vk.device,
			VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
			&g2Build, &maxG2Tris, &g2Size );

		createRTBuffer( g2Size.accelerationStructureSize,
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&vk.glowReflect.ghoul2BlasBuffer, &vk.glowReflect.ghoul2BlasMemory );

		VkAccelerationStructureCreateInfoKHR g2AsCreate = {};
		g2AsCreate.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
		g2AsCreate.buffer = vk.glowReflect.ghoul2BlasBuffer;
		g2AsCreate.size = g2Size.accelerationStructureSize;
		g2AsCreate.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		vk.rtFuncs.vkCreateAccelerationStructureKHR( vk.device, &g2AsCreate, NULL,
			&vk.glowReflect.ghoul2Blas );

		createRTBuffer( g2Size.buildScratchSize,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&vk.glowReflect.ghoul2ScratchBuffer, &vk.glowReflect.ghoul2ScratchMemory );
	}

	// ---- Step 6: Pre-allocate TLAS for up to 2 instances (BSP + Ghoul2) ----
	{
		uint32_t maxInstances = 2;

		// Instance buffer (DEVICE_LOCAL, updated each frame via vkCmdUpdateBuffer)
		createRTBuffer( maxInstances * sizeof(VkAccelerationStructureInstanceKHR),
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
			| VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&vk.glowReflect.tlasInstanceBuffer, &vk.glowReflect.tlasInstanceMemory );

		VkAccelerationStructureGeometryInstancesDataKHR instancesData = {};
		instancesData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
		instancesData.arrayOfPointers = VK_FALSE;
		instancesData.data.deviceAddress = getBufferAddress( vk.glowReflect.tlasInstanceBuffer );

		VkAccelerationStructureGeometryKHR tlasGeometry = {};
		tlasGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
		tlasGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
		tlasGeometry.geometry.instances = instancesData;

		VkAccelerationStructureBuildGeometryInfoKHR tlasBuildInfo = {};
		tlasBuildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
		tlasBuildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
		tlasBuildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
		tlasBuildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		tlasBuildInfo.geometryCount = 1;
		tlasBuildInfo.pGeometries = &tlasGeometry;

		VkAccelerationStructureBuildSizesInfoKHR tlasSizeInfo = {};
		tlasSizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
		vk.rtFuncs.vkGetAccelerationStructureBuildSizesKHR( vk.device,
			VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
			&tlasBuildInfo, &maxInstances, &tlasSizeInfo );

		createRTBuffer( tlasSizeInfo.accelerationStructureSize,
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&vk.glowReflect.tlasBuffer, &vk.glowReflect.tlasMemory );

		VkAccelerationStructureCreateInfoKHR tlasCreateInfo = {};
		tlasCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
		tlasCreateInfo.buffer = vk.glowReflect.tlasBuffer;
		tlasCreateInfo.size = tlasSizeInfo.accelerationStructureSize;
		tlasCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
		vk.rtFuncs.vkCreateAccelerationStructureKHR( vk.device, &tlasCreateInfo, NULL,
			&vk.glowReflect.tlas );

		createRTBuffer( tlasSizeInfo.buildScratchSize,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&vk.glowReflect.tlasScratchBuffer, &vk.glowReflect.tlasScratchMemory );

		// ---- Initial TLAS build with BSP-only instance ----
		VkAccelerationStructureDeviceAddressInfoKHR blasAddrInfo = {};
		blasAddrInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
		blasAddrInfo.accelerationStructure = vk.glowReflect.blas;
		VkDeviceAddress blasAddress =
			vk.rtFuncs.vkGetAccelerationStructureDeviceAddressKHR( vk.device, &blasAddrInfo );

		VkAccelerationStructureInstanceKHR bspInstance = {};
		bspInstance.transform.matrix[0][0] = 1.0f;
		bspInstance.transform.matrix[1][1] = 1.0f;
		bspInstance.transform.matrix[2][2] = 1.0f;
		bspInstance.mask = 0xFF;
		bspInstance.instanceShaderBindingTableRecordOffset = 0;
		bspInstance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
		bspInstance.accelerationStructureReference = blasAddress;

		vkCmdUpdateBuffer( cmd, vk.glowReflect.tlasInstanceBuffer, 0,
			sizeof(bspInstance), &bspInstance );

		// Barrier: transfer write → AS build read
		VkMemoryBarrier xferBarrier = {};
		xferBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		xferBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		xferBarrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
		vkCmdPipelineBarrier( cmd,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
			0, 1, &xferBarrier, 0, NULL, 0, NULL );

		tlasBuildInfo.dstAccelerationStructure = vk.glowReflect.tlas;
		tlasBuildInfo.scratchData.deviceAddress = getBufferAddress( vk.glowReflect.tlasScratchBuffer );

		uint32_t initInstanceCount = 1;
		VkAccelerationStructureBuildRangeInfoKHR tlasRangeInfo = {};
		tlasRangeInfo.primitiveCount = initInstanceCount;
		const VkAccelerationStructureBuildRangeInfoKHR *pTlasRangeInfos = &tlasRangeInfo;

		vk.rtFuncs.vkCmdBuildAccelerationStructuresKHR( cmd, 1, &tlasBuildInfo, &pTlasRangeInfos );
	}

	VK_EndSingleTimeCommands( cmd );

	// Clean up temporary BLAS scratch buffer
	vkDestroyBuffer( vk.device, scratchBuf, NULL );
	vkFreeMemory( vk.device, scratchMem, NULL );

	vk.glowReflect.asBuilt = qtrue;
	ri.Printf( PRINT_ALL, "RT acceleration structure built (%u BSP tris, Ghoul2 proxy enabled)\n", primitiveCount );
}

// ============================================================
// Resource Create / Destroy
// ============================================================

/*
================
VK_CreateGlowReflectResources

Create everything needed for the hardware RT glow reflection:
- Output storage image
- Depth-only image view
- Glow sources uniform buffer
- RT descriptor set layout, pipeline layout, pipeline
- Shader binding table
================
*/
void VK_CreateGlowReflectResources( void ) {
	// Save RT properties set during device init before zeroing the struct
	uint32_t savedHandleSize  = vk.glowReflect.shaderGroupHandleSize;
	uint32_t savedHandleAlign = vk.glowReflect.shaderGroupHandleAlignment;
	uint32_t savedBaseAlign   = vk.glowReflect.shaderGroupBaseAlignment;

	Com_Memset( &vk.glowReflect, 0, sizeof(vk.glowReflect) );

	vk.glowReflect.shaderGroupHandleSize      = savedHandleSize;
	vk.glowReflect.shaderGroupHandleAlignment  = savedHandleAlign;
	vk.glowReflect.shaderGroupBaseAlignment    = savedBaseAlign;

	if ( !vk.rayTracingSupported ) {
		ri.Printf( PRINT_ALL, "VK_CreateGlowReflectResources: RT not supported, glow reflections disabled\n" );
		return;
	}
	if ( !vk.glowReflectRgenShader || !vk.glowReflectRmissShader || !vk.glowReflectRchitShader ) {
		ri.Printf( PRINT_ALL, "VK_CreateGlowReflectResources: RT shaders not loaded\n" );
		return;
	}
	if ( !vk.glow.glowImage ) {
		ri.Printf( PRINT_ALL, "VK_CreateGlowReflectResources: glow resources not available\n" );
		return;
	}

	uint32_t width  = vk.swapchainExtent.width;
	uint32_t height = vk.swapchainExtent.height;

	// ---- Output storage image ----
	VK_CreateRenderTargetImage( &vk.glowReflect.outputImage,
		&vk.glowReflect.outputImageMemory,
		&vk.glowReflect.outputImageView,
		width, height, vk.swapchainFormat,
		VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT );

	VK_TransitionImageLayout( vk.glowReflect.outputImage, vk.swapchainFormat,
		VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 1 );

	// Composite descriptor (sampled image for the fullscreen triangle pass)
	// We keep the output image in GENERAL layout at all times, so write the
	// descriptor with GENERAL rather than the SHADER_READ_ONLY_OPTIMAL that
	// VK_AllocateImageDescriptor would use.
	vk.glowReflect.outputDescriptorSet = VK_AllocateImageDescriptor(
		vk.glowReflect.outputImageView, vk.samplerNoMipClamp );

	// Overwrite with GENERAL layout
	{
		VkDescriptorImageInfo imgInfo = {};
		imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		imgInfo.imageView   = vk.glowReflect.outputImageView;
		imgInfo.sampler     = vk.samplerNoMipClamp;

		VkWriteDescriptorSet write = {};
		write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet          = vk.glowReflect.outputDescriptorSet;
		write.dstBinding       = 0;
		write.dstArrayElement   = 0;
		write.descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.descriptorCount  = 1;
		write.pImageInfo       = &imgInfo;

		vkUpdateDescriptorSets( vk.device, 1, &write, 0, NULL );
	}

	// ---- Depth-only image view ----
	{
		VkImageViewCreateInfo viewInfo = {};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = vk.depthImage;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = vk.depthFormat;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;

		if ( vkCreateImageView( vk.device, &viewInfo, NULL,
			&vk.glowReflect.depthOnlyImageView ) != VK_SUCCESS ) {
			ri.Printf( PRINT_ALL, "VK_CreateGlowReflectResources: failed to create depth-only image view\n" );
			return;
		}
	}

	// ---- Glow sources uniform buffer (host-visible, persistently mapped) ----
	{
		VkDeviceSize uboSize = GLOW_RT_MAX_SOURCES * sizeof(glowSourceData_t);
		VkBufferCreateInfo bufInfo = {};
		bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufInfo.size = uboSize;
		bufInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
		bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		vkCreateBuffer( vk.device, &bufInfo, NULL, &vk.glowReflect.glowSourcesBuffer );

		VkMemoryRequirements memReqs;
		vkGetBufferMemoryRequirements( vk.device, vk.glowReflect.glowSourcesBuffer, &memReqs );

		VkMemoryAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memReqs.size;
		allocInfo.memoryTypeIndex = VK_FindMemoryType( memReqs.memoryTypeBits,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT );
		vkAllocateMemory( vk.device, &allocInfo, NULL, &vk.glowReflect.glowSourcesMemory );
		vkBindBufferMemory( vk.device, vk.glowReflect.glowSourcesBuffer,
			vk.glowReflect.glowSourcesMemory, 0 );

		vkMapMemory( vk.device, vk.glowReflect.glowSourcesMemory, 0, uboSize, 0,
			&vk.glowReflect.glowSourcesMapped );
		Com_Memset( vk.glowReflect.glowSourcesMapped, 0, (size_t)uboSize );
	}

	// ---- RT descriptor set layout ----
	{
		VkDescriptorSetLayoutBinding bindings[5] = {};

		bindings[0].binding = 0;
		bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
		bindings[0].descriptorCount = 1;
		bindings[0].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

		bindings[1].binding = 1;
		bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		bindings[1].descriptorCount = 1;
		bindings[1].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

		bindings[2].binding = 2;
		bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bindings[2].descriptorCount = 1;
		bindings[2].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

		bindings[3].binding = 3;
		bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bindings[3].descriptorCount = 1;
		bindings[3].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

		bindings[4].binding = 4;
		bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		bindings[4].descriptorCount = 1;
		bindings[4].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

		VkDescriptorSetLayoutCreateInfo layoutInfo = {};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = 5;
		layoutInfo.pBindings = bindings;

		if ( vkCreateDescriptorSetLayout( vk.device, &layoutInfo, NULL,
			&vk.glowReflect.rtDescriptorSetLayout ) != VK_SUCCESS ) {
			ri.Printf( PRINT_ALL, "VK_CreateGlowReflectResources: failed to create RT descriptor set layout\n" );
			return;
		}
	}

	// ---- RT pipeline layout ----
	{
		VkPushConstantRange pushRange = {};
		pushRange.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
		pushRange.offset = 0;
		pushRange.size = sizeof(glowReflectPC_t);

		VkPipelineLayoutCreateInfo layoutInfo = {};
		layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutInfo.setLayoutCount = 1;
		layoutInfo.pSetLayouts = &vk.glowReflect.rtDescriptorSetLayout;
		layoutInfo.pushConstantRangeCount = 1;
		layoutInfo.pPushConstantRanges = &pushRange;

		if ( vkCreatePipelineLayout( vk.device, &layoutInfo, NULL,
			&vk.glowReflect.rtPipelineLayout ) != VK_SUCCESS ) {
			ri.Printf( PRINT_ALL, "VK_CreateGlowReflectResources: failed to create RT pipeline layout\n" );
			return;
		}
	}

	// ---- RT Pipeline ----
	{
		VkPipelineShaderStageCreateInfo stages[3] = {};

		stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[0].stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
		stages[0].module = vk.glowReflectRgenShader;
		stages[0].pName = "main";

		stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[1].stage = VK_SHADER_STAGE_MISS_BIT_KHR;
		stages[1].module = vk.glowReflectRmissShader;
		stages[1].pName = "main";

		stages[2].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[2].stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
		stages[2].module = vk.glowReflectRchitShader;
		stages[2].pName = "main";

		VkRayTracingShaderGroupCreateInfoKHR groups[3] = {};

		// Group 0: ray generation
		groups[0].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
		groups[0].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
		groups[0].generalShader = 0;
		groups[0].closestHitShader = VK_SHADER_UNUSED_KHR;
		groups[0].anyHitShader = VK_SHADER_UNUSED_KHR;
		groups[0].intersectionShader = VK_SHADER_UNUSED_KHR;

		// Group 1: miss
		groups[1].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
		groups[1].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
		groups[1].generalShader = 1;
		groups[1].closestHitShader = VK_SHADER_UNUSED_KHR;
		groups[1].anyHitShader = VK_SHADER_UNUSED_KHR;
		groups[1].intersectionShader = VK_SHADER_UNUSED_KHR;

		// Group 2: hit (triangles)
		groups[2].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
		groups[2].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
		groups[2].generalShader = VK_SHADER_UNUSED_KHR;
		groups[2].closestHitShader = 2;
		groups[2].anyHitShader = VK_SHADER_UNUSED_KHR;
		groups[2].intersectionShader = VK_SHADER_UNUSED_KHR;

		VkRayTracingPipelineCreateInfoKHR rtPipelineInfo = {};
		rtPipelineInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
		rtPipelineInfo.stageCount = 3;
		rtPipelineInfo.pStages = stages;
		rtPipelineInfo.groupCount = 3;
		rtPipelineInfo.pGroups = groups;
		rtPipelineInfo.maxPipelineRayRecursionDepth = 1;
		rtPipelineInfo.layout = vk.glowReflect.rtPipelineLayout;

		if ( vk.rtFuncs.vkCreateRayTracingPipelinesKHR( vk.device, VK_NULL_HANDLE,
			vk.pipelineCache, 1, &rtPipelineInfo, NULL,
			&vk.glowReflect.rtPipeline ) != VK_SUCCESS ) {
			ri.Printf( PRINT_ALL, "VK_CreateGlowReflectResources: failed to create RT pipeline\n" );
			return;
		}
	}

	// ---- Shader Binding Table ----
	{
		uint32_t handleSize   = vk.glowReflect.shaderGroupHandleSize;
		uint32_t handleAlign  = vk.glowReflect.shaderGroupHandleAlignment;
		uint32_t baseAlign    = vk.glowReflect.shaderGroupBaseAlignment;
		uint32_t handleStride = alignUp( handleSize, handleAlign );

		uint32_t rgenSize = handleStride;
		uint32_t missOff  = alignUp( rgenSize, baseAlign );
		uint32_t missSize = handleStride;
		uint32_t hitOff   = alignUp( missOff + missSize, baseAlign );
		uint32_t hitSize  = handleStride;
		uint32_t sbtTotal = hitOff + hitSize;

		// Get all group handles
		uint32_t numGroups = 3;
		uint8_t *handleData = (uint8_t *)ri.Malloc( numGroups * handleSize, TAG_RENDERER, qfalse );
		vk.rtFuncs.vkGetRayTracingShaderGroupHandlesKHR( vk.device,
			vk.glowReflect.rtPipeline, 0, numGroups,
			numGroups * handleSize, handleData );

		createRTBuffer( sbtTotal,
			VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&vk.glowReflect.sbtBuffer, &vk.glowReflect.sbtMemory );

		void *sbtMapped;
		vkMapMemory( vk.device, vk.glowReflect.sbtMemory, 0, sbtTotal, 0, &sbtMapped );
		Com_Memset( sbtMapped, 0, sbtTotal );
		Com_Memcpy( (uint8_t *)sbtMapped,              handleData + 0 * handleSize, handleSize );
		Com_Memcpy( (uint8_t *)sbtMapped + missOff,     handleData + 1 * handleSize, handleSize );
		Com_Memcpy( (uint8_t *)sbtMapped + hitOff,      handleData + 2 * handleSize, handleSize );
		vkUnmapMemory( vk.device, vk.glowReflect.sbtMemory );

		ri.Free( handleData );

		VkDeviceAddress sbtAddr = getBufferAddress( vk.glowReflect.sbtBuffer );

		vk.glowReflect.rgenRegion.deviceAddress = sbtAddr;
		vk.glowReflect.rgenRegion.stride = handleStride;
		vk.glowReflect.rgenRegion.size   = handleStride;

		vk.glowReflect.missRegion.deviceAddress = sbtAddr + missOff;
		vk.glowReflect.missRegion.stride = handleStride;
		vk.glowReflect.missRegion.size   = handleStride;

		vk.glowReflect.hitRegion.deviceAddress = sbtAddr + hitOff;
		vk.glowReflect.hitRegion.stride = handleStride;
		vk.glowReflect.hitRegion.size   = handleStride;

		Com_Memset( &vk.glowReflect.callRegion, 0, sizeof(vk.glowReflect.callRegion) );
	}

	// ---- Allocate RT descriptor set (written once AS is built) ----
	{
		VkDescriptorSetAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = vk.descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &vk.glowReflect.rtDescriptorSetLayout;

		if ( vkAllocateDescriptorSets( vk.device, &allocInfo,
			&vk.glowReflect.rtDescriptorSet ) != VK_SUCCESS ) {
			ri.Printf( PRINT_ALL, "VK_CreateGlowReflectResources: failed to allocate RT descriptor set\n" );
			return;
		}
	}

	vk.glowReflect.available = qtrue;
	ri.Printf( PRINT_ALL, "RT glow reflection resources created (%dx%d)\n", width, height );
}

/*
 * Write the RT descriptor set (called once after AS build).
 */
static void writeRTDescriptorSet( void ) {
	VkWriteDescriptorSetAccelerationStructureKHR asWrite = {};
	asWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
	asWrite.accelerationStructureCount = 1;
	asWrite.pAccelerationStructures = &vk.glowReflect.tlas;

	VkDescriptorImageInfo outputImgInfo = {};
	outputImgInfo.imageView  = vk.glowReflect.outputImageView;
	outputImgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

	VkDescriptorImageInfo depthImgInfo = {};
	depthImgInfo.imageView  = vk.glowReflect.depthOnlyImageView;
	depthImgInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
	depthImgInfo.sampler     = vk.samplerNoMipClamp;

	VkDescriptorImageInfo glowImgInfo = {};
	glowImgInfo.imageView  = vk.glow.glowImageView;
	glowImgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	glowImgInfo.sampler     = vk.samplerNoMipClamp;

	VkDescriptorBufferInfo uboInfo = {};
	uboInfo.buffer = vk.glowReflect.glowSourcesBuffer;
	uboInfo.offset = 0;
	uboInfo.range  = GLOW_RT_MAX_SOURCES * sizeof(glowSourceData_t);

	VkWriteDescriptorSet writes[5] = {};

	writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[0].pNext = &asWrite;
	writes[0].dstSet = vk.glowReflect.rtDescriptorSet;
	writes[0].dstBinding = 0;
	writes[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
	writes[0].descriptorCount = 1;

	writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[1].dstSet = vk.glowReflect.rtDescriptorSet;
	writes[1].dstBinding = 1;
	writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	writes[1].descriptorCount = 1;
	writes[1].pImageInfo = &outputImgInfo;

	writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[2].dstSet = vk.glowReflect.rtDescriptorSet;
	writes[2].dstBinding = 2;
	writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	writes[2].descriptorCount = 1;
	writes[2].pImageInfo = &depthImgInfo;

	writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[3].dstSet = vk.glowReflect.rtDescriptorSet;
	writes[3].dstBinding = 3;
	writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	writes[3].descriptorCount = 1;
	writes[3].pImageInfo = &glowImgInfo;

	writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[4].dstSet = vk.glowReflect.rtDescriptorSet;
	writes[4].dstBinding = 4;
	writes[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	writes[4].descriptorCount = 1;
	writes[4].pBufferInfo = &uboInfo;

	vkUpdateDescriptorSets( vk.device, 5, writes, 0, NULL );
}

/*
================
VK_InvalidateGlowReflectAccelStruct

Destroy only the acceleration structure resources (BLAS, TLAS, geometry buffers)
so they will be rebuilt from the new map's BSP on the next dispatch.
Called during map changes when the Vulkan device stays alive but the world
geometry has changed.  Pipeline, SBT, output image, and descriptor sets
are kept intact.
================
*/
void VK_InvalidateGlowReflectAccelStruct( void ) {
	if ( !vk.device ) return;
	if ( !vk.glowReflect.asBuilt ) return;

	vkDeviceWaitIdle( vk.device );

	// BSP BLAS
	if ( vk.glowReflect.blas && vk.rtFuncs.vkDestroyAccelerationStructureKHR )
		vk.rtFuncs.vkDestroyAccelerationStructureKHR( vk.device, vk.glowReflect.blas, NULL );
	if ( vk.glowReflect.blasBuffer ) vkDestroyBuffer( vk.device, vk.glowReflect.blasBuffer, NULL );
	if ( vk.glowReflect.blasMemory ) vkFreeMemory( vk.device, vk.glowReflect.blasMemory, NULL );
	if ( vk.glowReflect.vertexBuffer ) vkDestroyBuffer( vk.device, vk.glowReflect.vertexBuffer, NULL );
	if ( vk.glowReflect.vertexMemory ) vkFreeMemory( vk.device, vk.glowReflect.vertexMemory, NULL );
	if ( vk.glowReflect.indexBuffer ) vkDestroyBuffer( vk.device, vk.glowReflect.indexBuffer, NULL );
	if ( vk.glowReflect.indexMemory ) vkFreeMemory( vk.device, vk.glowReflect.indexMemory, NULL );

	// Ghoul2 proxy BLAS
	if ( vk.glowReflect.ghoul2Blas && vk.rtFuncs.vkDestroyAccelerationStructureKHR )
		vk.rtFuncs.vkDestroyAccelerationStructureKHR( vk.device, vk.glowReflect.ghoul2Blas, NULL );
	if ( vk.glowReflect.ghoul2BlasBuffer ) vkDestroyBuffer( vk.device, vk.glowReflect.ghoul2BlasBuffer, NULL );
	if ( vk.glowReflect.ghoul2BlasMemory ) vkFreeMemory( vk.device, vk.glowReflect.ghoul2BlasMemory, NULL );
	if ( vk.glowReflect.ghoul2VertexBuffer ) vkDestroyBuffer( vk.device, vk.glowReflect.ghoul2VertexBuffer, NULL );
	if ( vk.glowReflect.ghoul2VertexMemory ) vkFreeMemory( vk.device, vk.glowReflect.ghoul2VertexMemory, NULL );
	if ( vk.glowReflect.ghoul2IndexBuffer ) vkDestroyBuffer( vk.device, vk.glowReflect.ghoul2IndexBuffer, NULL );
	if ( vk.glowReflect.ghoul2IndexMemory ) vkFreeMemory( vk.device, vk.glowReflect.ghoul2IndexMemory, NULL );
	if ( vk.glowReflect.ghoul2ScratchBuffer ) vkDestroyBuffer( vk.device, vk.glowReflect.ghoul2ScratchBuffer, NULL );
	if ( vk.glowReflect.ghoul2ScratchMemory ) vkFreeMemory( vk.device, vk.glowReflect.ghoul2ScratchMemory, NULL );

	// TLAS + per-frame rebuild resources
	if ( vk.glowReflect.tlas && vk.rtFuncs.vkDestroyAccelerationStructureKHR )
		vk.rtFuncs.vkDestroyAccelerationStructureKHR( vk.device, vk.glowReflect.tlas, NULL );
	if ( vk.glowReflect.tlasBuffer ) vkDestroyBuffer( vk.device, vk.glowReflect.tlasBuffer, NULL );
	if ( vk.glowReflect.tlasMemory ) vkFreeMemory( vk.device, vk.glowReflect.tlasMemory, NULL );
	if ( vk.glowReflect.tlasInstanceBuffer ) vkDestroyBuffer( vk.device, vk.glowReflect.tlasInstanceBuffer, NULL );
	if ( vk.glowReflect.tlasInstanceMemory ) vkFreeMemory( vk.device, vk.glowReflect.tlasInstanceMemory, NULL );
	if ( vk.glowReflect.tlasScratchBuffer ) vkDestroyBuffer( vk.device, vk.glowReflect.tlasScratchBuffer, NULL );
	if ( vk.glowReflect.tlasScratchMemory ) vkFreeMemory( vk.device, vk.glowReflect.tlasScratchMemory, NULL );

	// Zero only the AS-related fields (preserve pipeline, SBT, output image, etc.)
	vk.glowReflect.blas = VK_NULL_HANDLE;
	vk.glowReflect.blasBuffer = VK_NULL_HANDLE;
	vk.glowReflect.blasMemory = VK_NULL_HANDLE;
	vk.glowReflect.vertexBuffer = VK_NULL_HANDLE;
	vk.glowReflect.vertexMemory = VK_NULL_HANDLE;
	vk.glowReflect.indexBuffer = VK_NULL_HANDLE;
	vk.glowReflect.indexMemory = VK_NULL_HANDLE;
	vk.glowReflect.numVertices = 0;
	vk.glowReflect.numIndices = 0;
	vk.glowReflect.ghoul2Blas = VK_NULL_HANDLE;
	vk.glowReflect.ghoul2BlasBuffer = VK_NULL_HANDLE;
	vk.glowReflect.ghoul2BlasMemory = VK_NULL_HANDLE;
	vk.glowReflect.ghoul2VertexBuffer = VK_NULL_HANDLE;
	vk.glowReflect.ghoul2VertexMemory = VK_NULL_HANDLE;
	vk.glowReflect.ghoul2IndexBuffer = VK_NULL_HANDLE;
	vk.glowReflect.ghoul2IndexMemory = VK_NULL_HANDLE;
	vk.glowReflect.ghoul2ScratchBuffer = VK_NULL_HANDLE;
	vk.glowReflect.ghoul2ScratchMemory = VK_NULL_HANDLE;
	vk.glowReflect.tlas = VK_NULL_HANDLE;
	vk.glowReflect.tlasBuffer = VK_NULL_HANDLE;
	vk.glowReflect.tlasMemory = VK_NULL_HANDLE;
	vk.glowReflect.tlasInstanceBuffer = VK_NULL_HANDLE;
	vk.glowReflect.tlasInstanceMemory = VK_NULL_HANDLE;
	vk.glowReflect.tlasScratchBuffer = VK_NULL_HANDLE;
	vk.glowReflect.tlasScratchMemory = VK_NULL_HANDLE;

	vk.glowReflect.lastFrameGhoul2Count = 0;
	vk.glowReflect.asBuilt = qfalse;

	ri.Printf( PRINT_ALL, "RT acceleration structure invalidated (map change)\n" );
}

/*
================
VK_DestroyGlowReflectResources
================
*/
void VK_DestroyGlowReflectResources( void ) {
	if ( !vk.device ) return;
	vkDeviceWaitIdle( vk.device );

	if ( vk.glowReflect.rtDescriptorSet ) {
		vkFreeDescriptorSets( vk.device, vk.descriptorPool, 1, &vk.glowReflect.rtDescriptorSet );
	}
	if ( vk.glowReflect.outputDescriptorSet ) {
		vkFreeDescriptorSets( vk.device, vk.descriptorPool, 1, &vk.glowReflect.outputDescriptorSet );
	}

	if ( vk.glowReflect.rtPipeline ) vkDestroyPipeline( vk.device, vk.glowReflect.rtPipeline, NULL );
	if ( vk.glowReflect.rtPipelineLayout ) vkDestroyPipelineLayout( vk.device, vk.glowReflect.rtPipelineLayout, NULL );
	if ( vk.glowReflect.rtDescriptorSetLayout ) vkDestroyDescriptorSetLayout( vk.device, vk.glowReflect.rtDescriptorSetLayout, NULL );

	if ( vk.glowReflect.sbtBuffer ) vkDestroyBuffer( vk.device, vk.glowReflect.sbtBuffer, NULL );
	if ( vk.glowReflect.sbtMemory ) vkFreeMemory( vk.device, vk.glowReflect.sbtMemory, NULL );

	// TLAS + per-frame rebuild resources
	if ( vk.glowReflect.tlas && vk.rtFuncs.vkDestroyAccelerationStructureKHR )
		vk.rtFuncs.vkDestroyAccelerationStructureKHR( vk.device, vk.glowReflect.tlas, NULL );
	if ( vk.glowReflect.tlasBuffer ) vkDestroyBuffer( vk.device, vk.glowReflect.tlasBuffer, NULL );
	if ( vk.glowReflect.tlasMemory ) vkFreeMemory( vk.device, vk.glowReflect.tlasMemory, NULL );
	if ( vk.glowReflect.tlasInstanceBuffer ) vkDestroyBuffer( vk.device, vk.glowReflect.tlasInstanceBuffer, NULL );
	if ( vk.glowReflect.tlasInstanceMemory ) vkFreeMemory( vk.device, vk.glowReflect.tlasInstanceMemory, NULL );
	if ( vk.glowReflect.tlasScratchBuffer ) vkDestroyBuffer( vk.device, vk.glowReflect.tlasScratchBuffer, NULL );
	if ( vk.glowReflect.tlasScratchMemory ) vkFreeMemory( vk.device, vk.glowReflect.tlasScratchMemory, NULL );

	// BSP BLAS
	if ( vk.glowReflect.blas && vk.rtFuncs.vkDestroyAccelerationStructureKHR )
		vk.rtFuncs.vkDestroyAccelerationStructureKHR( vk.device, vk.glowReflect.blas, NULL );
	if ( vk.glowReflect.blasBuffer ) vkDestroyBuffer( vk.device, vk.glowReflect.blasBuffer, NULL );
	if ( vk.glowReflect.blasMemory ) vkFreeMemory( vk.device, vk.glowReflect.blasMemory, NULL );

	if ( vk.glowReflect.vertexBuffer ) vkDestroyBuffer( vk.device, vk.glowReflect.vertexBuffer, NULL );
	if ( vk.glowReflect.vertexMemory ) vkFreeMemory( vk.device, vk.glowReflect.vertexMemory, NULL );
	if ( vk.glowReflect.indexBuffer ) vkDestroyBuffer( vk.device, vk.glowReflect.indexBuffer, NULL );
	if ( vk.glowReflect.indexMemory ) vkFreeMemory( vk.device, vk.glowReflect.indexMemory, NULL );

	// Ghoul2 BLAS + buffers
	if ( vk.glowReflect.ghoul2Blas && vk.rtFuncs.vkDestroyAccelerationStructureKHR )
		vk.rtFuncs.vkDestroyAccelerationStructureKHR( vk.device, vk.glowReflect.ghoul2Blas, NULL );
	if ( vk.glowReflect.ghoul2BlasBuffer ) vkDestroyBuffer( vk.device, vk.glowReflect.ghoul2BlasBuffer, NULL );
	if ( vk.glowReflect.ghoul2BlasMemory ) vkFreeMemory( vk.device, vk.glowReflect.ghoul2BlasMemory, NULL );
	if ( vk.glowReflect.ghoul2VertexBuffer ) vkDestroyBuffer( vk.device, vk.glowReflect.ghoul2VertexBuffer, NULL );
	if ( vk.glowReflect.ghoul2VertexMemory ) vkFreeMemory( vk.device, vk.glowReflect.ghoul2VertexMemory, NULL );
	if ( vk.glowReflect.ghoul2IndexBuffer ) vkDestroyBuffer( vk.device, vk.glowReflect.ghoul2IndexBuffer, NULL );
	if ( vk.glowReflect.ghoul2IndexMemory ) vkFreeMemory( vk.device, vk.glowReflect.ghoul2IndexMemory, NULL );
	if ( vk.glowReflect.ghoul2ScratchBuffer ) vkDestroyBuffer( vk.device, vk.glowReflect.ghoul2ScratchBuffer, NULL );
	if ( vk.glowReflect.ghoul2ScratchMemory ) vkFreeMemory( vk.device, vk.glowReflect.ghoul2ScratchMemory, NULL );

	if ( vk.glowReflect.glowSourcesMapped ) {
		vkUnmapMemory( vk.device, vk.glowReflect.glowSourcesMemory );
	}
	if ( vk.glowReflect.glowSourcesBuffer ) vkDestroyBuffer( vk.device, vk.glowReflect.glowSourcesBuffer, NULL );
	if ( vk.glowReflect.glowSourcesMemory ) vkFreeMemory( vk.device, vk.glowReflect.glowSourcesMemory, NULL );

	if ( vk.glowReflect.depthOnlyImageView ) vkDestroyImageView( vk.device, vk.glowReflect.depthOnlyImageView, NULL );

	if ( vk.glowReflect.outputImageView ) vkDestroyImageView( vk.device, vk.glowReflect.outputImageView, NULL );
	if ( vk.glowReflect.outputImage ) vkDestroyImage( vk.device, vk.glowReflect.outputImage, NULL );
	if ( vk.glowReflect.outputImageMemory ) vkFreeMemory( vk.device, vk.glowReflect.outputImageMemory, NULL );

	Com_Memset( &vk.glowReflect, 0, sizeof(vk.glowReflect) );
}

// ============================================================
// Per-frame dispatch
// ============================================================

/*
================
VK_DispatchGlowReflect

Trace shadow rays from world surfaces toward glow sources.
Must be called OUTSIDE a render pass, after the glow scene has been rendered.
================
*/
void VK_DispatchGlowReflect( void ) {
	if ( !vk.glowReflect.available ) return;
	if ( !r_DynamicGlowReflections || !r_DynamicGlowReflections->integer ) return;

	// Lazily build the acceleration structure on first use after map load
	if ( !vk.glowReflect.asBuilt && tr.world ) {
		VK_BuildGlowReflectAccelStruct();
		if ( vk.glowReflect.asBuilt ) {
			writeRTDescriptorSet();
		}
	}
	if ( !vk.glowReflect.asBuilt ) return;

	VkCommandBuffer cmd = vk.frames[vk.currentFrame].commandBuffer;
	uint32_t width  = vk.swapchainExtent.width;
	uint32_t height = vk.swapchainExtent.height;

	// ---- Collect glow sources from dynamic lights, with saber line-segment enhancement ----
	int numSources = 0;
	glowSourceData_t *dst = (glowSourceData_t *)vk.glowReflect.glowSourcesMapped;

	// Build a small list of saber blade geometries from RT_SABER_GLOW entities
	struct {
		vec3_t start;		// hilt position
		vec3_t end;			// blade tip
		vec3_t mid;			// midpoint (for matching with dlight)
		qboolean matched;
	} sabers[GLOW_RT_MAX_SOURCES];
	int numSabers = 0;

	for ( int i = 0; i < backEnd.refdef.num_entities && numSabers < GLOW_RT_MAX_SOURCES; i++ ) {
		trRefEntity_t *ent = &backEnd.refdef.entities[i];
		if ( ent->e.reType != RT_SABER_GLOW ) continue;
		if ( ent->e.saberLength < 0.5f ) continue;

		VectorCopy( ent->e.origin, sabers[numSabers].start );
		VectorMA( ent->e.origin, ent->e.saberLength, ent->e.axis[0], sabers[numSabers].end );
		VectorMA( ent->e.origin, ent->e.saberLength * 0.5f, ent->e.axis[0], sabers[numSabers].mid );
		sabers[numSabers].matched = qfalse;
		numSabers++;
	}

	// Process dlights, matching each with a saber entity when possible
	for ( int i = 0; i < backEnd.refdef.num_dlights && numSources < GLOW_RT_MAX_SOURCES; i++ ) {
		dlight_t *dl = &backEnd.refdef.dlights[i];
		if ( dl->radius <= 0.0f ) continue;
		float luminance = dl->color[0] * 0.299f + dl->color[1] * 0.587f + dl->color[2] * 0.114f;
		if ( luminance < 0.01f ) continue;

		// Try to match this dlight with a saber entity (dlight origin is at blade midpoint)
		int bestSaber = -1;
		float bestDistSq = 10000.0f; // 100-unit matching tolerance
		for ( int s = 0; s < numSabers; s++ ) {
			if ( sabers[s].matched ) continue;
			vec3_t diff;
			VectorSubtract( dl->origin, sabers[s].mid, diff );
			float distSq = DotProduct( diff, diff );
			if ( distSq < bestDistSq ) {
				bestDistSq = distSq;
				bestSaber = s;
			}
		}

		if ( bestSaber >= 0 ) {
			// Line-segment source: hilt to tip with dlight color
			sabers[bestSaber].matched = qtrue;
			dst[numSources].posAndRadius[0] = sabers[bestSaber].start[0];
			dst[numSources].posAndRadius[1] = sabers[bestSaber].start[1];
			dst[numSources].posAndRadius[2] = sabers[bestSaber].start[2];
			dst[numSources].posAndRadius[3] = dl->radius * r_DynamicGlowReflectionRadius->value;
			dst[numSources].colorAndPower[0] = dl->color[0];
			dst[numSources].colorAndPower[1] = dl->color[1];
			dst[numSources].colorAndPower[2] = dl->color[2];
			dst[numSources].colorAndPower[3] = 1.0f;
			dst[numSources].endPosAndType[0] = sabers[bestSaber].end[0];
			dst[numSources].endPosAndType[1] = sabers[bestSaber].end[1];
			dst[numSources].endPosAndType[2] = sabers[bestSaber].end[2];
			dst[numSources].endPosAndType[3] = 1.0f; // line source
		} else {
			// Point source: no matching saber entity
			dst[numSources].posAndRadius[0] = dl->origin[0];
			dst[numSources].posAndRadius[1] = dl->origin[1];
			dst[numSources].posAndRadius[2] = dl->origin[2];
			dst[numSources].posAndRadius[3] = dl->radius * r_DynamicGlowReflectionRadius->value;
			dst[numSources].colorAndPower[0] = dl->color[0];
			dst[numSources].colorAndPower[1] = dl->color[1];
			dst[numSources].colorAndPower[2] = dl->color[2];
			dst[numSources].colorAndPower[3] = 1.0f;
			dst[numSources].endPosAndType[0] = dl->origin[0];
			dst[numSources].endPosAndType[1] = dl->origin[1];
			dst[numSources].endPosAndType[2] = dl->origin[2];
			dst[numSources].endPosAndType[3] = 0.0f; // point source
		}
		numSources++;
	}

	if ( numSources == 0 ) {
		// No dynamic lights, but glow texture may still have emitters
		// (e.g. lightsaber blades) — continue to dispatch RT pass.
	}

	// ---- Compute VP and inverse VP matrices ----
	glowReflectPC_t pc = {};

	float vp[16];
	myGlMultMatrix( backEnd.viewParms.world.modelMatrix,
		backEnd.viewParms.projectionMatrix, vp );

	// Convert to Vulkan clip space (same transforms as VK_SetMVP)
	vp[1]  = -vp[1];  vp[5]  = -vp[5];  vp[9]  = -vp[9];  vp[13] = -vp[13];
	vp[2]  = 0.5f * vp[2]  + 0.5f * vp[3];
	vp[6]  = 0.5f * vp[6]  + 0.5f * vp[7];
	vp[10] = 0.5f * vp[10] + 0.5f * vp[11];
	vp[14] = 0.5f * vp[14] + 0.5f * vp[15];

	if ( !invertMatrix4x4( vp, pc.inverseVP ) ) {
		return;
	}

	// Forward VP matrix for projecting reflected hit positions to screen UV
	Com_Memcpy( pc.viewProjection, vp, sizeof(float) * 16 );

	pc.cameraPosAndIntensity[0] = backEnd.viewParms.ori.origin[0];
	pc.cameraPosAndIntensity[1] = backEnd.viewParms.ori.origin[1];
	pc.cameraPosAndIntensity[2] = backEnd.viewParms.ori.origin[2];
	pc.cameraPosAndIntensity[3] = r_DynamicGlowReflectionIntensity->value;
	pc.numSources = numSources;
	pc.bias = 4.0f;
	pc.falloffExponent = r_DynamicGlowReflectionFalloff->value;
	pc.g2ReflectScale = r_DynamicGlowReflectionG2Scale->value;
	pc.shadowIntensity = r_DynamicGlowReflectionShadowIntensity->value;

	// ================================================================
	// Per-frame Ghoul2 proxy generation + TLAS rebuild
	// Generate oriented bounding boxes for each Ghoul2 entity so
	// shadow rays are blocked by player model proxy geometry.
	//
	// When no Ghoul2 entities are present AND the previous frame was
	// also BSP-only, we skip the entire TLAS rebuild to avoid the
	// GPU sync barrier + build overhead (common case on empty servers
	// or when players are far from any lightsaber).
	// ================================================================
	int &lastFrameGhoul2Count = vk.glowReflect.lastFrameGhoul2Count;
	{

		// Bone names for the skeletal proxy (13 key joints)
		static const char *proxyBoneNames[] = {
			"pelvis",        // 0: hip center
			"lower_lumbar",  // 1: waist
			"thoracic",      // 2: chest
			"cervical",      // 3: neck
			"cranium",       // 4: head top
			"rhumerus",      // 5: right shoulder
			"lhumerus",      // 6: left shoulder
			"rradius",       // 7: right elbow
			"lradius",       // 8: left elbow
			"rtibia",        // 9: right knee
			"ltibia",        // 10: left knee
			"rtalus",        // 11: right ankle
			"ltalus",        // 12: left ankle
		};
		static const int kNumProxyBones = 13;

		// Body segments: each is a prism between two bone positions
		// { boneA, boneB, radius }
		static const struct { int a, b; float r; } proxySegments[] = {
			{ 0, 1, 7.0f },   // pelvis → waist
			{ 1, 2, 6.5f },   // waist → chest
			{ 2, 3, 5.0f },   // chest → neck
			{ 3, 4, 4.5f },   // neck → head
			{ 2, 5, 3.5f },   // chest → right shoulder
			{ 2, 6, 3.5f },   // chest → left shoulder
			{ 5, 7, 2.5f },   // right shoulder → elbow
			{ 6, 8, 2.5f },   // left shoulder → elbow
			{ 0, 9, 4.5f },   // pelvis → right knee
			{ 0, 10, 4.5f },  // pelvis → left knee
			{ 9, 11, 3.0f },  // right knee → ankle
			{ 10, 12, 3.0f }, // left knee → ankle
		};
		static const int kNumSegments = 12;

		// Octagonal prism index template (16 verts: 0-7 bottom ring, 8-15 top ring)
		// 28 triangles = 84 indices per segment
		static const uint32_t octPrismIndices[84] = {
			// Bottom cap fan from vertex 0
			0,1,2,  0,2,3,  0,3,4,  0,4,5,  0,5,6,  0,6,7,
			// Top cap fan from vertex 8
			8,10,9,  8,11,10,  8,12,11,  8,13,12,  8,14,13,  8,15,14,
			// Side quads (8 quads × 2 tris)
			0,9,1,   0,8,9,
			1,10,2,  1,9,10,
			2,11,3,  2,10,11,
			3,12,4,  3,11,12,
			4,13,5,  4,12,13,
			5,14,6,  5,13,14,
			6,15,7,  6,14,15,
			7,8,0,   7,15,8,
		};

		// Octagonal ring: cos/sin at 45° increments
		static const float octCos[8] = {
			 1.000f,  0.707f,  0.000f, -0.707f,
			-1.000f, -0.707f,  0.000f,  0.707f,
		};
		static const float octSin[8] = {
			 0.000f,  0.707f,  1.000f,  0.707f,
			 0.000f, -0.707f, -1.000f, -0.707f,
		};

		// Generate bone-driven proxy for all Ghoul2 entities
		const int kVertsPerSegment   = 16;
		const int kIndicesPerSegment = 84;
		const int kVertsPerEntity    = kNumSegments * kVertsPerSegment;
		const int kIndicesPerEntity  = kNumSegments * kIndicesPerSegment;
		// Static to avoid ~200KB stack allocation each frame (not re-entrant, single-threaded renderer)
		static float    g2VertsCPU[GLOW_RT_MAX_GHOUL2_ENTITIES * 12 * 16 * 3];
		static uint32_t g2IdxCPU[GLOW_RT_MAX_GHOUL2_ENTITIES * 12 * 84];
		int numGhoul2 = 0;

		for ( int i = 0; i < backEnd.refdef.num_entities && numGhoul2 < GLOW_RT_MAX_GHOUL2_ENTITIES; i++ ) {
			trRefEntity_t *ent = &backEnd.refdef.entities[i];
			if ( ent->e.reType != RT_MODEL ) continue;
			if ( !ent->e.ghoul2 ) continue;
			if ( ent->e.renderfx & RF_FIRST_PERSON ) continue;

			// Retrieve bone positions in model space, then transform to world
			// space using ent->e.axis + ent->e.origin (same transform the GPU
			// renderer uses).  G2API_GetBoltMatrix internally builds a world
			// matrix from ent->e.angles via AnglesToAxis, which can differ from
			// the actual ent->e.axis (e.g. when modelScale is baked in, or the
			// game code sets axis directly).  By querying the bolt with zero
			// angles/origin we get the model-space position from the already-
			// built skeleton cache, then apply the entity transform ourselves.
			vec3_t bonePos[13];
			qboolean boneValid[13];
			int numValid = 0;

			static const vec3_t zeroVec = { 0, 0, 0 };

			for ( int b = 0; b < kNumProxyBones; b++ ) {
				boneValid[b] = qfalse;
				int boltIdx = G2API_AddBolt( ent->e.ghoul2, 0, proxyBoneNames[b] );
				if ( boltIdx >= 0 ) {
					mdxaBone_t boltMatrix;
					// Get model-space bolt position (skeleton already cached this frame)
					if ( G2API_GetBoltMatrix( ent->e.ghoul2, 0, boltIdx, &boltMatrix,
							zeroVec, zeroVec, backEnd.refdef.time,
							NULL, ent->e.modelScale ) ) {
						// boltMatrix[*][3] is now in model space — transform to world
						// using the entity's actual axis + origin (matches GPU rendering)
						float mx = boltMatrix.matrix[0][3];
						float my = boltMatrix.matrix[1][3];
						float mz = boltMatrix.matrix[2][3];
						bonePos[b][0] = ent->e.origin[0]
							+ mx * ent->e.axis[0][0] + my * ent->e.axis[1][0] + mz * ent->e.axis[2][0];
						bonePos[b][1] = ent->e.origin[1]
							+ mx * ent->e.axis[0][1] + my * ent->e.axis[1][1] + mz * ent->e.axis[2][1];
						bonePos[b][2] = ent->e.origin[2]
							+ mx * ent->e.axis[0][2] + my * ent->e.axis[1][2] + mz * ent->e.axis[2][2];
						boneValid[b] = qtrue;
						numValid++;
					}
				}
			}

			if ( numValid < 3 ) continue; // not a humanoid model, skip

			int entityVertBase = numGhoul2 * kVertsPerEntity;
			int entityIdxBase  = numGhoul2 * kIndicesPerEntity;

			// Build oriented prisms between bone pairs
			for ( int seg = 0; seg < kNumSegments; seg++ ) {
				int bA = proxySegments[seg].a;
				int bB = proxySegments[seg].b;
				float radius = proxySegments[seg].r;

				vec3_t posA, posB;
				if ( boneValid[bA] ) { VectorCopy( bonePos[bA], posA ); }
				else { VectorCopy( ent->e.origin, posA ); posA[2] += 32.0f; }
				if ( boneValid[bB] ) { VectorCopy( bonePos[bB], posB ); }
				else { VectorCopy( posA, posB ); posB[2] += 8.0f; }

				// Direction A→B and perpendicular basis
				vec3_t dir, perp1, perp2;
				VectorSubtract( posB, posA, dir );
				float len = VectorNormalize( dir );
				if ( len < 0.1f ) {
					dir[0] = 0; dir[1] = 0; dir[2] = 1.0f;
				}

				vec3_t up = { 0, 0, 1 };
				if ( fabs( dir[2] ) > 0.9f ) { up[0] = 1.0f; up[1] = 0; up[2] = 0; }
				CrossProduct( dir, up, perp1 );
				VectorNormalize( perp1 );
				CrossProduct( dir, perp1, perp2 );
				VectorNormalize( perp2 );

				int segVertBase = entityVertBase + seg * kVertsPerSegment;
				int segIdxBase  = entityIdxBase  + seg * kIndicesPerSegment;

				// 8 verts at posA (bottom ring), 8 at posB (top ring)
				for ( int c = 0; c < 16; c++ ) {
					int ring = c & 7;
					float sx = radius * octCos[ring];
					float sy = radius * octSin[ring];
					float *base = ( c < 8 ) ? posA : posB;

					int vi = ( segVertBase + c ) * 3;
					g2VertsCPU[vi + 0] = base[0] + sx * perp1[0] + sy * perp2[0];
					g2VertsCPU[vi + 1] = base[1] + sx * perp1[1] + sy * perp2[1];
					g2VertsCPU[vi + 2] = base[2] + sx * perp1[2] + sy * perp2[2];
				}

				for ( int t = 0; t < kIndicesPerSegment; t++ ) {
					g2IdxCPU[segIdxBase + t] = (uint32_t)segVertBase + octPrismIndices[t];
				}
			}

			numGhoul2++;
		}

		// Upload proxy data to device-local buffers via command buffer
		if ( numGhoul2 > 0 ) {
			VkDeviceSize vertBytes = (VkDeviceSize)numGhoul2 * kVertsPerEntity * 3 * sizeof(float);
			VkDeviceSize idxBytes  = (VkDeviceSize)numGhoul2 * kIndicesPerEntity * sizeof(uint32_t);

			cmdUpdateBufferChunked( cmd, vk.glowReflect.ghoul2VertexBuffer, 0, vertBytes, g2VertsCPU );
			cmdUpdateBufferChunked( cmd, vk.glowReflect.ghoul2IndexBuffer, 0, idxBytes, g2IdxCPU );
		}

		// Skip the entire TLAS rebuild when BSP-only and previous frame was
		// also BSP-only — the TLAS hasn't changed so all barriers + builds
		// are wasted GPU work.  This is the common case on empty servers or
		// when players are far from lightsabers.
		qboolean needTlasRebuild = ( numGhoul2 > 0 || lastFrameGhoul2Count > 0 ) ? qtrue : qfalse;
		lastFrameGhoul2Count = numGhoul2;

		if ( needTlasRebuild ) {
			// GPU-side sync: ensure previous frame's AS builds + traces are
			// complete before we overwrite scratch/data buffers.
			VkMemoryBarrier asSyncBarrier = {};
			asSyncBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
			asSyncBarrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR
				| VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
			asSyncBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			vkCmdPipelineBarrier( cmd,
				VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR
				| VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				0, 1, &asSyncBarrier, 0, NULL, 0, NULL );

			// Prepare TLAS instance data (BSP always, Ghoul2 if present)
			VkAccelerationStructureDeviceAddressInfoKHR addrInfo = {};
			addrInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
			addrInfo.accelerationStructure = vk.glowReflect.blas;
			VkDeviceAddress bspBlasAddr =
				vk.rtFuncs.vkGetAccelerationStructureDeviceAddressKHR( vk.device, &addrInfo );

			VkAccelerationStructureInstanceKHR instancesCPU[2] = {};

			// Instance 0: BSP world geometry
			instancesCPU[0].transform.matrix[0][0] = 1.0f;
			instancesCPU[0].transform.matrix[1][1] = 1.0f;
			instancesCPU[0].transform.matrix[2][2] = 1.0f;
			instancesCPU[0].mask = 0x01;  // BSP-only mask (probed separately from Ghoul2)
			instancesCPU[0].instanceShaderBindingTableRecordOffset = 0;
			instancesCPU[0].flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
			instancesCPU[0].accelerationStructureReference = bspBlasAddr;

			uint32_t instanceCount = 1;

			if ( numGhoul2 > 0 ) {
				addrInfo.accelerationStructure = vk.glowReflect.ghoul2Blas;
				VkDeviceAddress g2BlasAddr =
					vk.rtFuncs.vkGetAccelerationStructureDeviceAddressKHR( vk.device, &addrInfo );

				// Instance 1: Ghoul2 proxy geometry
				instancesCPU[1].transform.matrix[0][0] = 1.0f;
				instancesCPU[1].transform.matrix[1][1] = 1.0f;
				instancesCPU[1].transform.matrix[2][2] = 1.0f;
				instancesCPU[1].mask = 0x02;  // Ghoul2 mask (not matched by BSP detection probe)
				instancesCPU[1].instanceShaderBindingTableRecordOffset = 0;
				instancesCPU[1].flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
				instancesCPU[1].accelerationStructureReference = g2BlasAddr;

				instanceCount = 2;
			}

			// Upload TLAS instances
			vkCmdUpdateBuffer( cmd, vk.glowReflect.tlasInstanceBuffer, 0,
				instanceCount * sizeof(VkAccelerationStructureInstanceKHR), instancesCPU );

			// Barrier: transfer writes → AS build reads
			VkMemoryBarrier xferToAsBarrier = {};
			xferToAsBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
			xferToAsBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			xferToAsBarrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR
				| VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
			vkCmdPipelineBarrier( cmd,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
				0, 1, &xferToAsBarrier, 0, NULL, 0, NULL );

			// Build Ghoul2 BLAS (only if entities are present)
			if ( numGhoul2 > 0 ) {
				uint32_t g2TriCount  = (uint32_t)numGhoul2 * kNumSegments * 28;
				uint32_t g2VertCount = (uint32_t)numGhoul2 * kVertsPerEntity;

				VkAccelerationStructureGeometryTrianglesDataKHR g2Tri = {};
				g2Tri.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
				g2Tri.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
				g2Tri.vertexData.deviceAddress = getBufferAddress( vk.glowReflect.ghoul2VertexBuffer );
				g2Tri.vertexStride = 3 * sizeof(float);
				g2Tri.maxVertex = g2VertCount - 1;
				g2Tri.indexType = VK_INDEX_TYPE_UINT32;
				g2Tri.indexData.deviceAddress = getBufferAddress( vk.glowReflect.ghoul2IndexBuffer );

				VkAccelerationStructureGeometryKHR g2Geom = {};
				g2Geom.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
				g2Geom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
				g2Geom.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
				g2Geom.geometry.triangles = g2Tri;

				VkAccelerationStructureBuildGeometryInfoKHR g2Build = {};
				g2Build.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
				g2Build.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
				g2Build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
				g2Build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
				g2Build.geometryCount = 1;
				g2Build.pGeometries = &g2Geom;
				g2Build.dstAccelerationStructure = vk.glowReflect.ghoul2Blas;
				g2Build.scratchData.deviceAddress = getBufferAddress( vk.glowReflect.ghoul2ScratchBuffer );

				VkAccelerationStructureBuildRangeInfoKHR g2Range = {};
				g2Range.primitiveCount = g2TriCount;
				const VkAccelerationStructureBuildRangeInfoKHR *pG2Range = &g2Range;

				vk.rtFuncs.vkCmdBuildAccelerationStructuresKHR( cmd, 1, &g2Build, &pG2Range );

				// Barrier: BLAS build complete → TLAS can read it
				VkMemoryBarrier blasToTlasBarrier = {};
				blasToTlasBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
				blasToTlasBarrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
				blasToTlasBarrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
				vkCmdPipelineBarrier( cmd,
					VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
					VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
					0, 1, &blasToTlasBarrier, 0, NULL, 0, NULL );
			}

			// Rebuild TLAS with current instances
			{
				VkAccelerationStructureGeometryInstancesDataKHR instData = {};
				instData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
				instData.arrayOfPointers = VK_FALSE;
				instData.data.deviceAddress = getBufferAddress( vk.glowReflect.tlasInstanceBuffer );

				VkAccelerationStructureGeometryKHR tlasGeom = {};
				tlasGeom.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
				tlasGeom.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
				tlasGeom.geometry.instances = instData;

				VkAccelerationStructureBuildGeometryInfoKHR tlasBuild = {};
				tlasBuild.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
				tlasBuild.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
				tlasBuild.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
				tlasBuild.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
				tlasBuild.geometryCount = 1;
				tlasBuild.pGeometries = &tlasGeom;
				tlasBuild.dstAccelerationStructure = vk.glowReflect.tlas;
				tlasBuild.scratchData.deviceAddress = getBufferAddress( vk.glowReflect.tlasScratchBuffer );

				VkAccelerationStructureBuildRangeInfoKHR tlasRange = {};
				tlasRange.primitiveCount = instanceCount;
				const VkAccelerationStructureBuildRangeInfoKHR *pTlasRange = &tlasRange;

				vk.rtFuncs.vkCmdBuildAccelerationStructuresKHR( cmd, 1, &tlasBuild, &pTlasRange );
			}

			// Barrier: TLAS build → ray tracing shader read
			VkMemoryBarrier asToRtBarrier = {};
			asToRtBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
			asToRtBarrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
			asToRtBarrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
			vkCmdPipelineBarrier( cmd,
				VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
				VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
				0, 1, &asToRtBarrier, 0, NULL, 0, NULL );
		}
		// else: BSP-only, unchanged from previous frame — TLAS is already valid
	}

	// ---- Barrier: depth for shader read ----
	{
		VkImageMemoryBarrier depthBarrier = {};
		depthBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		depthBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		depthBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		depthBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		depthBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		depthBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		depthBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		depthBarrier.image = vk.depthImage;
		depthBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		if ( vk.depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT || vk.depthFormat == VK_FORMAT_D24_UNORM_S8_UINT ) {
			depthBarrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
		depthBarrier.subresourceRange.baseMipLevel = 0;
		depthBarrier.subresourceRange.levelCount = 1;
		depthBarrier.subresourceRange.baseArrayLayer = 0;
		depthBarrier.subresourceRange.layerCount = 1;

		vkCmdPipelineBarrier( cmd,
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
			0, 0, NULL, 0, NULL, 1, &depthBarrier );
	}

	// ---- Barrier: glow image readable ----
	{
		VkImageMemoryBarrier glowBarrier = {};
		glowBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		glowBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		glowBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		glowBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		glowBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		glowBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		glowBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		glowBarrier.image = vk.glow.glowImage;
		glowBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		glowBarrier.subresourceRange.baseMipLevel = 0;
		glowBarrier.subresourceRange.levelCount = 1;
		glowBarrier.subresourceRange.baseArrayLayer = 0;
		glowBarrier.subresourceRange.layerCount = 1;

		vkCmdPipelineBarrier( cmd,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
			0, 0, NULL, 0, NULL, 1, &glowBarrier );
	}

	// ---- Bind RT pipeline and dispatch ----
	vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, vk.glowReflect.rtPipeline );
	vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
		vk.glowReflect.rtPipelineLayout, 0, 1,
		&vk.glowReflect.rtDescriptorSet, 0, NULL );
	vkCmdPushConstants( cmd, vk.glowReflect.rtPipelineLayout,
		VK_SHADER_STAGE_RAYGEN_BIT_KHR, 0, sizeof(pc), &pc );

	vk.rtFuncs.vkCmdTraceRaysKHR( cmd,
		&vk.glowReflect.rgenRegion,
		&vk.glowReflect.missRegion,
		&vk.glowReflect.hitRegion,
		&vk.glowReflect.callRegion,
		width, height, 1 );

	// ---- Barrier: depth back to attachment ----
	{
		VkImageMemoryBarrier depthBarrier = {};
		depthBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		depthBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		depthBarrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT
			| VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		depthBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		depthBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		depthBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		depthBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		depthBarrier.image = vk.depthImage;
		depthBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		if ( vk.depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT || vk.depthFormat == VK_FORMAT_D24_UNORM_S8_UINT ) {
			depthBarrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
		depthBarrier.subresourceRange.baseMipLevel = 0;
		depthBarrier.subresourceRange.levelCount = 1;
		depthBarrier.subresourceRange.baseArrayLayer = 0;
		depthBarrier.subresourceRange.layerCount = 1;

		vkCmdPipelineBarrier( cmd,
			VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			0, 0, NULL, 0, NULL, 1, &depthBarrier );
	}

	// ---- Barrier: output image RT write → fragment shader read (stay in GENERAL) ----
	{
		VkImageMemoryBarrier outputBarrier = {};
		outputBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		outputBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		outputBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		outputBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		outputBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		outputBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		outputBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		outputBarrier.image = vk.glowReflect.outputImage;
		outputBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		outputBarrier.subresourceRange.baseMipLevel = 0;
		outputBarrier.subresourceRange.levelCount = 1;
		outputBarrier.subresourceRange.baseArrayLayer = 0;
		outputBarrier.subresourceRange.layerCount = 1;

		vkCmdPipelineBarrier( cmd,
			VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0, 0, NULL, 0, NULL, 1, &outputBarrier );
	}
}

/*
================
VK_DrawGlowReflectOverlay

Composite the RT output onto the scene using additive blending.
Must be called INSIDE the load render pass (after VK_BeginRenderPassLoad).
================
*/
void VK_DrawGlowReflectOverlay( void ) {
	if ( !vk.glowReflect.available ) return;
	if ( !r_DynamicGlowReflections || !r_DynamicGlowReflections->integer ) return;
	if ( !vk.glow.glowCompositePipeline || !vk.glowReflect.outputDescriptorSet ) return;

	if ( !vk.renderPassActive ) {
		ri.Printf( PRINT_WARNING, "VK_DrawGlowReflectOverlay: called outside render pass\n" );
		return;
	}

	VkCommandBuffer cmd = vk.frames[vk.currentFrame].commandBuffer;

	vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.glow.glowCompositePipeline );
	vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
		vk.pipelineLayout, 0, 1, &vk.glowReflect.outputDescriptorSet, 0, NULL );

	float intensity = 1.0f;
	vkCmdPushConstants( cmd, vk.pipelineLayout,
		VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		0, sizeof(intensity), &intensity );

	vkCmdDraw( cmd, 3, 1, 0, 0 );

	// Image stays in GENERAL — no layout transition needed.
}

#endif // !DEDICATED
