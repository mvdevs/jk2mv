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
// Helpers
// ============================================================

static uint32_t alignUp( uint32_t v, uint32_t alignment ) {
	return (v + alignment - 1) & ~(alignment - 1);
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

	// Only include world surfaces that behave like solid occluders.
	// This avoids obviously-wrong RT shadows where sky/nodraw/see-through
	// materials (grates, decals, fog, blends) block rays as if they were solid.
	auto isRTShadowOccluder = []( const msurface_t *surf ) -> qboolean {
		if ( !surf || !surf->shader ) return qfalse;
		const shader_t *sh = surf->shader;
		if ( sh->surfaceFlags & ( SURF_SKY | SURF_NODRAW | SURF_NODLIGHT ) ) return qfalse;
		if ( sh->isSky ) return qfalse;
		// Keep only portal/environment/opaque sorts; skip decals, see-through, fog, blends, etc.
		if ( sh->sort > SS_OPAQUE ) return qfalse;
		return qtrue;
	};
	auto isRTSeeThroughOccluder = []( const msurface_t *surf ) -> qboolean {
		if ( !surf || !surf->shader ) return qfalse;
		const shader_t *sh = surf->shader;
		if ( sh->surfaceFlags & ( SURF_SKY | SURF_NODRAW | SURF_NODLIGHT ) ) return qfalse;
		if ( sh->isSky ) return qfalse;
		// Grates/fences typically live in SS_SEE_THROUGH (alpha-test + small blended edges).
		if ( (int)sh->sort != SS_SEE_THROUGH ) return qfalse;
		return qtrue;
	};

	// ---- Step 1: Count geometry ----
	uint32_t totalVerts = 0, totalIndices = 0;
	uint32_t stVerts = 0, stIndices = 0;
	for ( int i = 0; i < tr.world->numsurfaces; i++ ) {
		msurface_t *surf = &tr.world->surfaces[i];
		qboolean isOpaque = isRTShadowOccluder( surf );
		qboolean isSeeThrough = isRTSeeThroughOccluder( surf );
		if ( !isOpaque && !isSeeThrough ) continue;
		switch ( *surf->data ) {
		case SF_FACE: {
			srfSurfaceFace_t *face = (srfSurfaceFace_t *)surf->data;
			if ( isOpaque ) {
				totalVerts += face->numPoints;
				totalIndices += face->numIndices;
			} else {
				stVerts += face->numPoints;
				stIndices += face->numIndices;
			}
			break;
		}
		case SF_TRIANGLES: {
			srfTriangles_t *tri = (srfTriangles_t *)surf->data;
			if ( isOpaque ) {
				totalVerts += tri->numVerts;
				totalIndices += tri->numIndexes;
			} else {
				stVerts += tri->numVerts;
				stIndices += tri->numIndexes;
			}
			break;
		}
		case SF_GRID: {
			srfGridMesh_t *grid = (srfGridMesh_t *)surf->data;
			uint32_t vCount = grid->width * grid->height;
			uint32_t iCount = (grid->width - 1) * (grid->height - 1) * 6;
			if ( isOpaque ) {
				totalVerts += vCount;
				totalIndices += iCount;
			} else {
				stVerts += vCount;
				stIndices += iCount;
			}
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
	float *stCpuVerts = NULL;
	uint32_t *stCpuIndices = NULL;
	if ( stVerts && stIndices ) {
		stCpuVerts = (float *)ri.Malloc( stVerts * 3 * sizeof(float), TAG_RENDERER, qfalse );
		stCpuIndices = (uint32_t *)ri.Malloc( stIndices * sizeof(uint32_t), TAG_RENDERER, qfalse );
	}

	uint32_t vertOff = 0, idxOff = 0;
	uint32_t stVertOff = 0, stIdxOff = 0;
	for ( int i = 0; i < tr.world->numsurfaces; i++ ) {
		msurface_t *surf = &tr.world->surfaces[i];
		qboolean isOpaque = isRTShadowOccluder( surf );
		qboolean isSeeThrough = ( stCpuVerts != NULL ) ? isRTSeeThroughOccluder( surf ) : qfalse;
		if ( !isOpaque && !isSeeThrough ) continue;
		uint32_t baseVert = isOpaque ? vertOff : stVertOff;
		switch ( *surf->data ) {
		case SF_FACE: {
			srfSurfaceFace_t *face = (srfSurfaceFace_t *)surf->data;
			for ( int v = 0; v < face->numPoints; v++ ) {
				float *dstV = isOpaque ? cpuVerts : stCpuVerts;
				uint32_t &o = isOpaque ? vertOff : stVertOff;
				dstV[o*3+0] = face->points[v][0];
				dstV[o*3+1] = face->points[v][1];
				dstV[o*3+2] = face->points[v][2];
				o++;
			}
			unsigned int *faceIdx = (unsigned int *)((char *)face + face->ofsIndices);
			for ( int j = 0; j < face->numIndices; j++ ) {
				uint32_t *dstI = isOpaque ? cpuIndices : stCpuIndices;
				uint32_t &io = isOpaque ? idxOff : stIdxOff;
				dstI[io++] = baseVert + faceIdx[j];
			}
			break;
		}
		case SF_TRIANGLES: {
			srfTriangles_t *tri = (srfTriangles_t *)surf->data;
			for ( int v = 0; v < tri->numVerts; v++ ) {
				float *dstV = isOpaque ? cpuVerts : stCpuVerts;
				uint32_t &o = isOpaque ? vertOff : stVertOff;
				dstV[o*3+0] = tri->verts[v].xyz[0];
				dstV[o*3+1] = tri->verts[v].xyz[1];
				dstV[o*3+2] = tri->verts[v].xyz[2];
				o++;
			}
			for ( int j = 0; j < tri->numIndexes; j++ ) {
				uint32_t *dstI = isOpaque ? cpuIndices : stCpuIndices;
				uint32_t &io = isOpaque ? idxOff : stIdxOff;
				dstI[io++] = baseVert + tri->indexes[j];
			}
			break;
		}
		case SF_GRID: {
			srfGridMesh_t *grid = (srfGridMesh_t *)surf->data;
			int w = grid->width, h = grid->height;
			for ( int v = 0; v < w * h; v++ ) {
				float *dstV = isOpaque ? cpuVerts : stCpuVerts;
				uint32_t &o = isOpaque ? vertOff : stVertOff;
				dstV[o*3+0] = grid->verts[v].xyz[0];
				dstV[o*3+1] = grid->verts[v].xyz[1];
				dstV[o*3+2] = grid->verts[v].xyz[2];
				o++;
			}
			for ( int row = 0; row < h - 1; row++ ) {
				for ( int col = 0; col < w - 1; col++ ) {
					uint32_t v0 = baseVert + row * w + col;
					uint32_t v1 = v0 + 1;
					uint32_t v2 = v0 + w;
					uint32_t v3 = v2 + 1;
					uint32_t *dstI = isOpaque ? cpuIndices : stCpuIndices;
					uint32_t &io = isOpaque ? idxOff : stIdxOff;
					dstI[io++] = v0;
					dstI[io++] = v2;
					dstI[io++] = v1;
					dstI[io++] = v1;
					dstI[io++] = v2;
					dstI[io++] = v3;
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
	vk.glowReflect.seeThroughNumVertices = stVertOff;
	vk.glowReflect.seeThroughNumIndices  = stIdxOff;

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

	// Upload see-through buffers (optional)
	if ( stCpuVerts && stCpuIndices && stVertOff && stIdxOff ) {
		VkDeviceSize stVertBufSize = (VkDeviceSize)stVertOff * 3 * sizeof(float);
		VkDeviceSize stIdxBufSize  = (VkDeviceSize)stIdxOff * sizeof(uint32_t);
		createRTBuffer( stVertBufSize,
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
			| VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&vk.glowReflect.seeThroughVertexBuffer, &vk.glowReflect.seeThroughVertexMemory );
		createRTBuffer( stIdxBufSize,
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
			| VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&vk.glowReflect.seeThroughIndexBuffer, &vk.glowReflect.seeThroughIndexMemory );
		void *stMapped;
		vkMapMemory( vk.device, vk.glowReflect.seeThroughVertexMemory, 0, stVertBufSize, 0, &stMapped );
		Com_Memcpy( stMapped, stCpuVerts, (size_t)stVertBufSize );
		vkUnmapMemory( vk.device, vk.glowReflect.seeThroughVertexMemory );
		vkMapMemory( vk.device, vk.glowReflect.seeThroughIndexMemory, 0, stIdxBufSize, 0, &stMapped );
		Com_Memcpy( stMapped, stCpuIndices, (size_t)stIdxBufSize );
		vkUnmapMemory( vk.device, vk.glowReflect.seeThroughIndexMemory );
	}

	ri.Free( cpuVerts );
	ri.Free( cpuIndices );
	if ( stCpuVerts ) ri.Free( stCpuVerts );
	if ( stCpuIndices ) ri.Free( stCpuIndices );

	// ---- Step 4: Build BLAS (opaque world) ----
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

	// Optional: build see-through BLAS (grates/fences)
	VkAccelerationStructureBuildSizesInfoKHR stSizeInfo = {};
	VkAccelerationStructureBuildGeometryInfoKHR stBuildInfo = {};
	VkAccelerationStructureGeometryKHR stGeometry = {};
	VkAccelerationStructureGeometryTrianglesDataKHR stTriangles = {};
	uint32_t stPrimitiveCount = 0;
	qboolean haveSeeThrough = ( vk.glowReflect.seeThroughVertexBuffer != VK_NULL_HANDLE
		&& vk.glowReflect.seeThroughIndexBuffer != VK_NULL_HANDLE
		&& vk.glowReflect.seeThroughNumIndices >= 3 ) ? qtrue : qfalse;
	if ( haveSeeThrough ) {
		stTriangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
		stTriangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
		stTriangles.vertexData.deviceAddress = getBufferAddress( vk.glowReflect.seeThroughVertexBuffer );
		stTriangles.vertexStride = 3 * sizeof(float);
		stTriangles.maxVertex = vk.glowReflect.seeThroughNumVertices - 1;
		stTriangles.indexType = VK_INDEX_TYPE_UINT32;
		stTriangles.indexData.deviceAddress = getBufferAddress( vk.glowReflect.seeThroughIndexBuffer );

		stGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
		stGeometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
		stGeometry.flags = 0; // NOT opaque; any-hit will handle stochastic opacity
		stGeometry.geometry.triangles = stTriangles;

		stBuildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
		stBuildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		stBuildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
		stBuildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		stBuildInfo.geometryCount = 1;
		stBuildInfo.pGeometries = &stGeometry;

		stPrimitiveCount = vk.glowReflect.seeThroughNumIndices / 3;
		stSizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
		vk.rtFuncs.vkGetAccelerationStructureBuildSizesKHR( vk.device,
			VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
			&stBuildInfo, &stPrimitiveCount, &stSizeInfo );

		createRTBuffer( stSizeInfo.accelerationStructureSize,
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&vk.glowReflect.blasSeeThroughBuffer, &vk.glowReflect.blasSeeThroughMemory );

		VkAccelerationStructureCreateInfoKHR stAsCreate = {};
		stAsCreate.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
		stAsCreate.buffer = vk.glowReflect.blasSeeThroughBuffer;
		stAsCreate.size = stSizeInfo.accelerationStructureSize;
		stAsCreate.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		vk.rtFuncs.vkCreateAccelerationStructureKHR( vk.device, &stAsCreate, NULL,
			&vk.glowReflect.blasSeeThrough );
	}

	// Scratch buffer for builds (size = max of opaque + see-through + later TLAS scratch)
	VkBuffer scratchBuf;
	VkDeviceMemory scratchMem;
	VkDeviceSize maxScratch = sizeInfo.buildScratchSize;
	if ( haveSeeThrough && stSizeInfo.buildScratchSize > maxScratch ) {
		maxScratch = stSizeInfo.buildScratchSize;
	}
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

	// Build see-through BLAS (optional)
	if ( haveSeeThrough ) {
		VkMemoryBarrier memBarrier2 = {};
		memBarrier2.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		memBarrier2.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
		memBarrier2.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
		vkCmdPipelineBarrier( cmd,
			VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
			VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
			0, 1, &memBarrier2, 0, NULL, 0, NULL );

		stBuildInfo.dstAccelerationStructure = vk.glowReflect.blasSeeThrough;
		stBuildInfo.scratchData.deviceAddress = getBufferAddress( scratchBuf );
		VkAccelerationStructureBuildRangeInfoKHR stRange = {};
		stRange.primitiveCount = stPrimitiveCount;
		const VkAccelerationStructureBuildRangeInfoKHR *pStRange = &stRange;
		vk.rtFuncs.vkCmdBuildAccelerationStructuresKHR( cmd, 1, &stBuildInfo, &pStRange );
	}

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
	// Each Ghoul2 entity is approximated by bone-driven prisms
	// (torso, head, arms, legs) for a smooth human-shaped shadow silhouette.
	// We use a 12-sided tapered prism per segment (24 verts) for a less blocky
	// silhouette than an octagon.
	// Buffers are DEVICE_LOCAL and updated each frame via vkCmdUpdateBuffer.
	{
		const uint32_t kSegments = 15;
		const uint32_t kSides = 24;
		const uint32_t kVertsPerSeg = kSides * 2;
		const uint32_t kIndicesPerSeg = (12 * kSides - 12); // 2 caps + sides (see generation below)
		const uint32_t kTrisPerSeg = kIndicesPerSeg / 3;
		uint32_t maxG2Verts   = GLOW_RT_MAX_GHOUL2_ENTITIES * kSegments * kVertsPerSeg;
		uint32_t maxG2Indices = GLOW_RT_MAX_GHOUL2_ENTITIES * kSegments * kIndicesPerSeg;
		uint32_t maxG2Tris    = GLOW_RT_MAX_GHOUL2_ENTITIES * kSegments * kTrisPerSeg;

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
		// Not opaque: allow any-hit shader to run so Ghoul2 occlusion can be softened.
		g2Geom.flags = 0;
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

	// ---- Step 6: Pre-allocate TLAS for up to 3 instances (opaque BSP + see-through BSP + Ghoul2) ----
	{
		uint32_t maxInstances = 3;

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

		// ---- Initial TLAS build with BSP-only instance(s) ----
		VkAccelerationStructureDeviceAddressInfoKHR blasAddrInfo = {};
		blasAddrInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
		blasAddrInfo.accelerationStructure = vk.glowReflect.blas;
		VkDeviceAddress blasAddress =
			vk.rtFuncs.vkGetAccelerationStructureDeviceAddressKHR( vk.device, &blasAddrInfo );

		VkAccelerationStructureInstanceKHR instancesInit[2] = {};
		uint32_t initCount = 1;

		// Instance 0: opaque BSP
		VkAccelerationStructureInstanceKHR &bspInstance = instancesInit[0];
		bspInstance.transform.matrix[0][0] = 1.0f;
		bspInstance.transform.matrix[1][1] = 1.0f;
		bspInstance.transform.matrix[2][2] = 1.0f;
		bspInstance.instanceCustomIndex = 0;
		bspInstance.mask = 0x01;
		bspInstance.instanceShaderBindingTableRecordOffset = 0;
		bspInstance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
		bspInstance.accelerationStructureReference = blasAddress;

		// Instance 1: see-through BSP (optional)
		if ( vk.glowReflect.blasSeeThrough ) {
			VkAccelerationStructureDeviceAddressInfoKHR stAddrInfo = {};
			stAddrInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
			stAddrInfo.accelerationStructure = vk.glowReflect.blasSeeThrough;
			VkDeviceAddress stAddress = vk.rtFuncs.vkGetAccelerationStructureDeviceAddressKHR( vk.device, &stAddrInfo );
			VkAccelerationStructureInstanceKHR &stInst = instancesInit[1];
			stInst.transform.matrix[0][0] = 1.0f;
			stInst.transform.matrix[1][1] = 1.0f;
			stInst.transform.matrix[2][2] = 1.0f;
			stInst.instanceCustomIndex = 1;
			stInst.mask = 0x04;
			stInst.instanceShaderBindingTableRecordOffset = 0;
			stInst.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
			stInst.accelerationStructureReference = stAddress;
			initCount = 2;
		}

		vkCmdUpdateBuffer( cmd, vk.glowReflect.tlasInstanceBuffer, 0,
			initCount * sizeof(VkAccelerationStructureInstanceKHR), instancesInit );

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

		uint32_t initInstanceCount = initCount;
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
	if ( !vk.glowReflectRahitShader ) {
		ri.Printf( PRINT_WARNING, "VK_CreateGlowReflectResources: any-hit shader not loaded, see-through occluders disabled\n" );
	}
	if ( !vk.glow.glowImage ) {
		ri.Printf( PRINT_ALL, "VK_CreateGlowReflectResources: glow resources not available\n" );
		return;
	}

	uint32_t width  = vk.swapchainExtent.width;
	uint32_t height = vk.swapchainExtent.height;

	// Apply render scale (0.5 = half-res, 1.0 = full-res).
	// Half-res is nearly invisible for a diffuse glow effect that gets
	// blurred anyway, but dispatches 4x fewer RT shader invocations.
	float scale = r_DynamicGlowReflectionScale ? Com_Clamp( 0.25f, 1.0f, r_DynamicGlowReflectionScale->value ) : 0.5f;
	uint32_t rtW = (uint32_t)(width  * scale);
	uint32_t rtH = (uint32_t)(height * scale);
	if ( rtW < 64 ) rtW = 64;
	if ( rtH < 64 ) rtH = 64;
	vk.glowReflect.rtWidth  = rtW;
	vk.glowReflect.rtHeight = rtH;

	// ---- Ping-pong output images (replaces separate output + history) ----
	// Both images have identical usage: STORAGE (RT writes) + SAMPLED (history/blur/composite).
	// Frame N writes to ppImage[pingPongIndex], reads history from ppImage[1-pingPongIndex].
	// After dispatch, pingPongIndex flips — no vkCmdCopyImage needed.
	for ( int pp = 0; pp < 2; pp++ ) {
		VK_CreateRenderTargetImage( &vk.glowReflect.ppImage[pp],
			&vk.glowReflect.ppImageMemory[pp],
			&vk.glowReflect.ppImageView[pp],
			rtW, rtH, vk.sceneFormat,
			VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT );
	}

	// Initialize both to GENERAL layout (ping-pong images always stay in GENERAL for simplicity)
	{
		VkCommandBuffer initCmd = VK_BeginSingleTimeCommands();

		VK_TransitionImageLayout( vk.glowReflect.ppImage[0], vk.sceneFormat,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 1, initCmd );

		// Clear ppImage[1] to black and keep in GENERAL layout
		VK_TransitionImageLayout( vk.glowReflect.ppImage[1], vk.sceneFormat,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, initCmd );
		VkClearColorValue clear = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		VkImageSubresourceRange range = {};
		range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		range.baseMipLevel = 0;
		range.levelCount = 1;
		range.baseArrayLayer = 0;
		range.layerCount = 1;
		vkCmdClearColorImage( initCmd, vk.glowReflect.ppImage[1],
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range );
		VkImageMemoryBarrier b = {};
		b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
		b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		b.image = vk.glowReflect.ppImage[1];
		b.subresourceRange = range;
		vkCmdPipelineBarrier( initCmd,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
			0, 0, NULL, 0, NULL, 1, &b );

		VK_EndSingleTimeCommands( initCmd );
	}

	vk.glowReflect.pingPongIndex = 0;

	// Create per-image composite descriptors (GENERAL layout for fullscreen triangle pass)
	// and blur-read descriptors (SHADER_READ_ONLY layout)
	for ( int pp = 0; pp < 2; pp++ ) {
		// Composite descriptor (GENERAL layout)
		vk.glowReflect.ppOutputDescriptorSet[pp] = VK_AllocateImageDescriptor(
			vk.glowReflect.ppImageView[pp], vk.samplerNoMipClamp );
		{
			VkDescriptorImageInfo imgInfo = {};
			imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
			imgInfo.imageView   = vk.glowReflect.ppImageView[pp];
			imgInfo.sampler     = vk.samplerNoMipClamp;

			VkWriteDescriptorSet write = {};
			write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.dstSet          = vk.glowReflect.ppOutputDescriptorSet[pp];
			write.dstBinding       = 0;
			write.dstArrayElement   = 0;
			write.descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			write.descriptorCount  = 1;
			write.pImageInfo       = &imgInfo;

			vkUpdateDescriptorSets( vk.device, 1, &write, 0, NULL );
		}

		// Blur-read descriptor: allocate and update with GENERAL layout (images stay in GENERAL)
		{
			VkDescriptorSetAllocateInfo allocInfo = {};
			allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocInfo.descriptorPool = vk.descriptorPool;
			allocInfo.descriptorSetCount = 1;
			allocInfo.pSetLayouts = &vk.descriptorSetLayout;
			vkAllocateDescriptorSets( vk.device, &allocInfo,
				&vk.glowReflect.ppOutputReadDescriptorSet[pp] );

			VkDescriptorImageInfo imgInfo = {};
			imgInfo.imageView   = vk.glowReflect.ppImageView[pp];
			imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;  // Keep in GENERAL (not SHADER_READ_ONLY)
			imgInfo.sampler     = vk.samplerNoMipClamp;

			VkWriteDescriptorSet write = {};
			write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.dstSet          = vk.glowReflect.ppOutputReadDescriptorSet[pp];
			write.dstBinding      = 0;
			write.dstArrayElement = 0;
			write.descriptorCount = 1;
			write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			write.pImageInfo      = &imgInfo;

			vkUpdateDescriptorSets( vk.device, 1, &write, 0, NULL );
		}
	}

	// ---- Blur resources: two images for H/V separable blur ----
	// Size matches RT dispatch resolution, not swapchain.
	{
		VK_CreateRenderTargetImage( &vk.glowReflect.blurTempImage,
			&vk.glowReflect.blurTempImageMemory,
			&vk.glowReflect.blurTempImageView,
			rtW, rtH, vk.sceneFormat,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT );
		vk.glowReflect.blurTempDescriptorSet = VK_AllocateImageDescriptor(
			vk.glowReflect.blurTempImageView, vk.samplerNoMipClamp );

		VK_CreateRenderTargetImage( &vk.glowReflect.blurOutputImage,
			&vk.glowReflect.blurOutputImageMemory,
			&vk.glowReflect.blurOutputImageView,
			rtW, rtH, vk.sceneFormat,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT );
		vk.glowReflect.blurOutputDescriptorSet = VK_AllocateImageDescriptor(
			vk.glowReflect.blurOutputImageView, vk.samplerNoMipClamp );

		if ( vk.glow.blurRenderPass ) {
			VkFramebufferCreateInfo fbInfo = {};
			fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			fbInfo.renderPass = vk.glow.blurRenderPass;
			fbInfo.attachmentCount = 1;
			fbInfo.width = rtW;
			fbInfo.height = rtH;
			fbInfo.layers = 1;

			fbInfo.pAttachments = &vk.glowReflect.blurTempImageView;
			vkCreateFramebuffer( vk.device, &fbInfo, NULL, &vk.glowReflect.blurTempFramebuffer );

			fbInfo.pAttachments = &vk.glowReflect.blurOutputImageView;
			vkCreateFramebuffer( vk.device, &fbInfo, NULL, &vk.glowReflect.blurOutputFramebuffer );
		}

		// Create alpha-masked blur pipeline (same as glow blur but uses glow_blur_masked.frag)
		if ( vk.glow.blurRenderPass && vk.blurVertShader && vk.blurMaskedFragShader ) {
			VkPipelineShaderStageCreateInfo stages[2] = {};
			stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
			stages[0].module = vk.blurVertShader;
			stages[0].pName = "main";
			stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			stages[1].module = vk.blurMaskedFragShader;
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
			pipelineInfo.renderPass = vk.glow.blurRenderPass;
			pipelineInfo.subpass = 0;

			vkCreateGraphicsPipelines( vk.device, vk.pipelineCache, 1, &pipelineInfo, NULL, &vk.glowReflect.blurMaskedPipeline );
		}

		// Alpha-masked composite pipeline: renders raw RT output using (ONE_MINUS_SRC_ALPHA, ONE)
		// so that only Ghoul2 pixels (alpha=0 → factor=1) are added; BSP (alpha=1 → factor=0) are skipped.
		if ( vk.renderPassLoad && vk.blurVertShader && vk.glowCompositeFragShader ) {
			VkPipelineShaderStageCreateInfo cStages[2] = {};
			cStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			cStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
			cStages[0].module = vk.blurVertShader;
			cStages[0].pName = "main";
			cStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			cStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			cStages[1].module = vk.glowCompositeFragShader;
			cStages[1].pName = "main";

			VkPipelineVertexInputStateCreateInfo vi = {};
			vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

			VkPipelineInputAssemblyStateCreateInfo ia = {};
			ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

			VkPipelineViewportStateCreateInfo vp = {};
			vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			vp.viewportCount = 1;
			vp.scissorCount = 1;

			VkPipelineRasterizationStateCreateInfo rs = {};
			rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			rs.polygonMode = VK_POLYGON_MODE_FILL;
			rs.cullMode = VK_CULL_MODE_NONE;
			rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
			rs.lineWidth = 1.0f;

			VkPipelineMultisampleStateCreateInfo ms = {};
			ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

			VkPipelineColorBlendAttachmentState ba = {};
			ba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
				VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
			ba.blendEnable = VK_TRUE;
			ba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
			ba.colorBlendOp = VK_BLEND_OP_ADD;
			ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
			ba.alphaBlendOp = VK_BLEND_OP_ADD;

			VkPipelineColorBlendStateCreateInfo cb = {};
			cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			cb.attachmentCount = 1;
			cb.pAttachments = &ba;

			VkPipelineDepthStencilStateCreateInfo ds = {};
			ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			ds.depthTestEnable = VK_FALSE;
			ds.depthWriteEnable = VK_FALSE;

			VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
			VkPipelineDynamicStateCreateInfo dyn = {};
			dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			dyn.dynamicStateCount = 2;
			dyn.pDynamicStates = dynStates;

			VkGraphicsPipelineCreateInfo pi = {};
			pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			pi.stageCount = 2;
			pi.pStages = cStages;
			pi.pVertexInputState = &vi;
			pi.pInputAssemblyState = &ia;
			pi.pViewportState = &vp;
			pi.pRasterizationState = &rs;
			pi.pMultisampleState = &ms;
			pi.pDepthStencilState = &ds;
			pi.pColorBlendState = &cb;
			pi.pDynamicState = &dyn;
			pi.layout = vk.pipelineLayout;
			pi.renderPass = vk.renderPassLoad;
			pi.subpass = 0;

			vkCreateGraphicsPipelines( vk.device, vk.pipelineCache, 1, &pi, NULL, &vk.glowReflect.g2CompositePipeline );
		}
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

	// ---- RT params uniform buffer (host-visible, persistently mapped) ----
	{
		// Matches RTParamsUBO in glow_reflect.rgen/.rahit
		VkDeviceSize uboSize = 16 * sizeof(float) + 4 * sizeof(uint32_t) + 4 * sizeof(float);
		VkBufferCreateInfo bufInfo = {};
		bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufInfo.size = uboSize;
		bufInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
		bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		vkCreateBuffer( vk.device, &bufInfo, NULL, &vk.glowReflect.rtParamsBuffer );

		VkMemoryRequirements memReqs;
		vkGetBufferMemoryRequirements( vk.device, vk.glowReflect.rtParamsBuffer, &memReqs );

		VkMemoryAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memReqs.size;
		allocInfo.memoryTypeIndex = VK_FindMemoryType( memReqs.memoryTypeBits,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT );
		vkAllocateMemory( vk.device, &allocInfo, NULL, &vk.glowReflect.rtParamsMemory );
		vkBindBufferMemory( vk.device, vk.glowReflect.rtParamsBuffer,
			vk.glowReflect.rtParamsMemory, 0 );

		vkMapMemory( vk.device, vk.glowReflect.rtParamsMemory, 0, uboSize, 0,
			&vk.glowReflect.rtParamsMapped );
		Com_Memset( vk.glowReflect.rtParamsMapped, 0, (size_t)uboSize );
		vk.glowReflect.rtFrameIndex = 0;
		Com_Memset( vk.glowReflect.prevViewProjection, 0, sizeof(vk.glowReflect.prevViewProjection) );
		vk.glowReflect.prevViewProjection[0] = 1.0f;
		vk.glowReflect.prevViewProjection[5] = 1.0f;
		vk.glowReflect.prevViewProjection[10] = 1.0f;
		vk.glowReflect.prevViewProjection[15] = 1.0f;
	}

	// ---- RT descriptor set layout ----
	{
		VkDescriptorSetLayoutBinding bindings[7] = {};

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

		bindings[5].binding = 5;
		bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bindings[5].descriptorCount = 1;
		bindings[5].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

		bindings[6].binding = 6;
		bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		bindings[6].descriptorCount = 1;
		bindings[6].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR;

		VkDescriptorSetLayoutCreateInfo layoutInfo = {};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = 7;
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
		VkPipelineShaderStageCreateInfo stages[4] = {};

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

		stages[3].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[3].stage = VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
		stages[3].module = vk.glowReflectRahitShader;
		stages[3].pName = "main";

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
		groups[2].anyHitShader = ( vk.glowReflectRahitShader != VK_NULL_HANDLE ) ? 3 : VK_SHADER_UNUSED_KHR;
		groups[2].intersectionShader = VK_SHADER_UNUSED_KHR;

		VkRayTracingPipelineCreateInfoKHR rtPipelineInfo = {};
		rtPipelineInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
		rtPipelineInfo.stageCount = ( vk.glowReflectRahitShader != VK_NULL_HANDLE ) ? 4 : 3;
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

	// ---- Allocate ping-pong RT descriptor sets (written once AS is built) ----
	// ppRtDescriptorSet[0]: output=ppImage[0], history=ppImage[1]
	// ppRtDescriptorSet[1]: output=ppImage[1], history=ppImage[0]
	{
		VkDescriptorSetLayout layouts[2] = { vk.glowReflect.rtDescriptorSetLayout, vk.glowReflect.rtDescriptorSetLayout };
		VkDescriptorSetAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = vk.descriptorPool;
		allocInfo.descriptorSetCount = 2;
		allocInfo.pSetLayouts = layouts;

		if ( vkAllocateDescriptorSets( vk.device, &allocInfo,
			vk.glowReflect.ppRtDescriptorSet ) != VK_SUCCESS ) {
			ri.Printf( PRINT_ALL, "VK_CreateGlowReflectResources: failed to allocate RT descriptor sets\n" );
			return;
		}
		// Keep rtDescriptorSet pointing to ppRtDescriptorSet[0] for writeRTDescriptorSet compatibility
		vk.glowReflect.rtDescriptorSet = vk.glowReflect.ppRtDescriptorSet[0];
	}

	vk.glowReflect.available = qtrue;
	ri.Printf( PRINT_ALL, "RT glow reflection resources created (%dx%d)\n", width, height );
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

	// See-through BSP BLAS (optional)
	if ( vk.glowReflect.blasSeeThrough && vk.rtFuncs.vkDestroyAccelerationStructureKHR )
		vk.rtFuncs.vkDestroyAccelerationStructureKHR( vk.device, vk.glowReflect.blasSeeThrough, NULL );
	if ( vk.glowReflect.blasSeeThroughBuffer ) vkDestroyBuffer( vk.device, vk.glowReflect.blasSeeThroughBuffer, NULL );
	if ( vk.glowReflect.blasSeeThroughMemory ) vkFreeMemory( vk.device, vk.glowReflect.blasSeeThroughMemory, NULL );
	if ( vk.glowReflect.seeThroughVertexBuffer ) vkDestroyBuffer( vk.device, vk.glowReflect.seeThroughVertexBuffer, NULL );
	if ( vk.glowReflect.seeThroughVertexMemory ) vkFreeMemory( vk.device, vk.glowReflect.seeThroughVertexMemory, NULL );
	if ( vk.glowReflect.seeThroughIndexBuffer ) vkDestroyBuffer( vk.device, vk.glowReflect.seeThroughIndexBuffer, NULL );
	if ( vk.glowReflect.seeThroughIndexMemory ) vkFreeMemory( vk.device, vk.glowReflect.seeThroughIndexMemory, NULL );

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
	vk.glowReflect.blasSeeThrough = VK_NULL_HANDLE;
	vk.glowReflect.blasSeeThroughBuffer = VK_NULL_HANDLE;
	vk.glowReflect.blasSeeThroughMemory = VK_NULL_HANDLE;
	vk.glowReflect.seeThroughVertexBuffer = VK_NULL_HANDLE;
	vk.glowReflect.seeThroughVertexMemory = VK_NULL_HANDLE;
	vk.glowReflect.seeThroughIndexBuffer = VK_NULL_HANDLE;
	vk.glowReflect.seeThroughIndexMemory = VK_NULL_HANDLE;
	vk.glowReflect.seeThroughNumVertices = 0;
	vk.glowReflect.seeThroughNumIndices = 0;
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

	// Ping-pong descriptor sets
	for ( int pp = 0; pp < 2; pp++ ) {
		if ( vk.glowReflect.ppRtDescriptorSet[pp] ) {
			vkFreeDescriptorSets( vk.device, vk.descriptorPool, 1, &vk.glowReflect.ppRtDescriptorSet[pp] );
		}
		if ( vk.glowReflect.ppOutputDescriptorSet[pp] ) {
			vkFreeDescriptorSets( vk.device, vk.descriptorPool, 1, &vk.glowReflect.ppOutputDescriptorSet[pp] );
		}
		if ( vk.glowReflect.ppOutputReadDescriptorSet[pp] ) {
			vkFreeDescriptorSets( vk.device, vk.descriptorPool, 1, &vk.glowReflect.ppOutputReadDescriptorSet[pp] );
		}
	}

	// Blur resources
	if ( vk.glowReflect.blurTempDescriptorSet ) {
		vkFreeDescriptorSets( vk.device, vk.descriptorPool, 1, &vk.glowReflect.blurTempDescriptorSet );
	}
	if ( vk.glowReflect.blurOutputDescriptorSet ) {
		vkFreeDescriptorSets( vk.device, vk.descriptorPool, 1, &vk.glowReflect.blurOutputDescriptorSet );
	}
	if ( vk.glowReflect.blurTempFramebuffer ) vkDestroyFramebuffer( vk.device, vk.glowReflect.blurTempFramebuffer, NULL );
	if ( vk.glowReflect.blurOutputFramebuffer ) vkDestroyFramebuffer( vk.device, vk.glowReflect.blurOutputFramebuffer, NULL );
	if ( vk.glowReflect.blurMaskedPipeline ) vkDestroyPipeline( vk.device, vk.glowReflect.blurMaskedPipeline, NULL );
	if ( vk.glowReflect.g2CompositePipeline ) vkDestroyPipeline( vk.device, vk.glowReflect.g2CompositePipeline, NULL );
	if ( vk.glowReflect.blurTempImageView ) vkDestroyImageView( vk.device, vk.glowReflect.blurTempImageView, NULL );
	if ( vk.glowReflect.blurTempImage ) vkDestroyImage( vk.device, vk.glowReflect.blurTempImage, NULL );
	if ( vk.glowReflect.blurTempImageMemory ) vkFreeMemory( vk.device, vk.glowReflect.blurTempImageMemory, NULL );
	if ( vk.glowReflect.blurOutputImageView ) vkDestroyImageView( vk.device, vk.glowReflect.blurOutputImageView, NULL );
	if ( vk.glowReflect.blurOutputImage ) vkDestroyImage( vk.device, vk.glowReflect.blurOutputImage, NULL );
	if ( vk.glowReflect.blurOutputImageMemory ) vkFreeMemory( vk.device, vk.glowReflect.blurOutputImageMemory, NULL );

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

	// See-through BSP BLAS (optional)
	if ( vk.glowReflect.blasSeeThrough && vk.rtFuncs.vkDestroyAccelerationStructureKHR )
		vk.rtFuncs.vkDestroyAccelerationStructureKHR( vk.device, vk.glowReflect.blasSeeThrough, NULL );
	if ( vk.glowReflect.blasSeeThroughBuffer ) vkDestroyBuffer( vk.device, vk.glowReflect.blasSeeThroughBuffer, NULL );
	if ( vk.glowReflect.blasSeeThroughMemory ) vkFreeMemory( vk.device, vk.glowReflect.blasSeeThroughMemory, NULL );
	if ( vk.glowReflect.seeThroughVertexBuffer ) vkDestroyBuffer( vk.device, vk.glowReflect.seeThroughVertexBuffer, NULL );
	if ( vk.glowReflect.seeThroughVertexMemory ) vkFreeMemory( vk.device, vk.glowReflect.seeThroughVertexMemory, NULL );
	if ( vk.glowReflect.seeThroughIndexBuffer ) vkDestroyBuffer( vk.device, vk.glowReflect.seeThroughIndexBuffer, NULL );
	if ( vk.glowReflect.seeThroughIndexMemory ) vkFreeMemory( vk.device, vk.glowReflect.seeThroughIndexMemory, NULL );

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

	if ( vk.glowReflect.rtParamsMapped ) {
		vkUnmapMemory( vk.device, vk.glowReflect.rtParamsMemory );
	}
	if ( vk.glowReflect.rtParamsBuffer ) vkDestroyBuffer( vk.device, vk.glowReflect.rtParamsBuffer, NULL );
	if ( vk.glowReflect.rtParamsMemory ) vkFreeMemory( vk.device, vk.glowReflect.rtParamsMemory, NULL );

	for ( int pp = 0; pp < 2; pp++ ) {
		if ( vk.glowReflect.ppImageView[pp] ) vkDestroyImageView( vk.device, vk.glowReflect.ppImageView[pp], NULL );
		if ( vk.glowReflect.ppImage[pp] ) vkDestroyImage( vk.device, vk.glowReflect.ppImage[pp], NULL );
		if ( vk.glowReflect.ppImageMemory[pp] ) vkFreeMemory( vk.device, vk.glowReflect.ppImageMemory[pp], NULL );
	}

	Com_Memset( &vk.glowReflect, 0, sizeof(vk.glowReflect) );
}


#endif // !DEDICATED
