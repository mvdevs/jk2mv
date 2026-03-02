/*
===========================================================================
vk_glowreflect_dispatch.cpp - Per-frame RT glow reflection dispatch

Traces shadow rays from world surfaces toward glow sources, applies
screen-space blur, and composites the result onto the scene.
Separated from vk_glowreflect.cpp (resource creation / accel struct build).
===========================================================================
*/

#include "tr_local.h"
#include "../ghoul2/G2_local.h"

#ifndef DEDICATED

#include "vk_local.h"

// ============================================================
// Helpers (duplicated from vk_glowreflect.cpp where shared)
// ============================================================

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
// RT Descriptor Set Update
// ============================================================

static void writeRTDescriptorSet( void ) {
	// Write BOTH ping-pong RT descriptor sets.
	// ppRtDescriptorSet[0]: output=ppImage[0], history=ppImage[1]
	// ppRtDescriptorSet[1]: output=ppImage[1], history=ppImage[0]

	VkWriteDescriptorSetAccelerationStructureKHR asWrite = {};
	asWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
	asWrite.accelerationStructureCount = 1;
	asWrite.pAccelerationStructures = &vk.glowReflect.tlas;

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

	VkDescriptorBufferInfo paramsInfo = {};
	paramsInfo.buffer = vk.glowReflect.rtParamsBuffer;
	paramsInfo.offset = 0;
	paramsInfo.range  = 16 * sizeof(float) + 4 * sizeof(uint32_t) + 4 * sizeof(float);

	for ( int pp = 0; pp < 2; pp++ ) {
		int outputIdx  = pp;      // this frame's output image
		int historyIdx = 1 - pp;  // previous frame's output (now history)

		VkDescriptorImageInfo outputImgInfo = {};
		outputImgInfo.imageView  = vk.glowReflect.ppImageView[outputIdx];
		outputImgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

		VkDescriptorImageInfo historyImgInfo = {};
		historyImgInfo.imageView  = vk.glowReflect.ppImageView[historyIdx];
		historyImgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;  // Both images stay in GENERAL
		historyImgInfo.sampler     = vk.samplerNoMipClamp;

		VkWriteDescriptorSet writes[7] = {};

		writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[0].pNext = &asWrite;
		writes[0].dstSet = vk.glowReflect.ppRtDescriptorSet[pp];
		writes[0].dstBinding = 0;
		writes[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
		writes[0].descriptorCount = 1;

		writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[1].dstSet = vk.glowReflect.ppRtDescriptorSet[pp];
		writes[1].dstBinding = 1;
		writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		writes[1].descriptorCount = 1;
		writes[1].pImageInfo = &outputImgInfo;

		writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[2].dstSet = vk.glowReflect.ppRtDescriptorSet[pp];
		writes[2].dstBinding = 2;
		writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[2].descriptorCount = 1;
		writes[2].pImageInfo = &depthImgInfo;

		writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[3].dstSet = vk.glowReflect.ppRtDescriptorSet[pp];
		writes[3].dstBinding = 3;
		writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[3].descriptorCount = 1;
		writes[3].pImageInfo = &glowImgInfo;

		writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[4].dstSet = vk.glowReflect.ppRtDescriptorSet[pp];
		writes[4].dstBinding = 4;
		writes[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		writes[4].descriptorCount = 1;
		writes[4].pBufferInfo = &uboInfo;

		writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[5].dstSet = vk.glowReflect.ppRtDescriptorSet[pp];
		writes[5].dstBinding = 5;
		writes[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[5].descriptorCount = 1;
		writes[5].pImageInfo = &historyImgInfo;

		writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[6].dstSet = vk.glowReflect.ppRtDescriptorSet[pp];
		writes[6].dstBinding = 6;
		writes[6].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		writes[6].descriptorCount = 1;
		writes[6].pBufferInfo = &paramsInfo;

		vkUpdateDescriptorSets( vk.device, 7, writes, 0, NULL );
	}
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
	uint32_t width  = vk.glowReflect.rtWidth  ? vk.glowReflect.rtWidth  : vk.swapchainExtent.width;
	uint32_t height = vk.glowReflect.rtHeight ? vk.glowReflect.rtHeight : vk.swapchainExtent.height;

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

		// Clip the blade segment against solid world geometry so we don't treat the
		// portion inside a wall as an emitting light source.
		// This fixes "weird" shadows when the blade tip penetrates a surface.
		trace_t bladeTrace;
		vec3_t mins = { 0, 0, 0 };
		vec3_t maxs = { 0, 0, 0 };
		ri.CM_BoxTrace( &bladeTrace, sabers[numSabers].start, sabers[numSabers].end,
			mins, maxs, 0, CONTENTS_SOLID, qfalse );
		if ( bladeTrace.startsolid || bladeTrace.allsolid ) {
			continue;
		}
		if ( bladeTrace.fraction < 1.0f ) {
			vec3_t dir;
			VectorSubtract( sabers[numSabers].end, sabers[numSabers].start, dir );
			VectorMA( sabers[numSabers].start, bladeTrace.fraction, dir, sabers[numSabers].end );
			// Pull back a tiny amount to avoid numerical "inside" cases.
			VectorMA( sabers[numSabers].end, -1.0f, ent->e.axis[0], sabers[numSabers].end );
		}
		// If clipping collapsed the segment, skip it.
		vec3_t seg;
		VectorSubtract( sabers[numSabers].end, sabers[numSabers].start, seg );
		if ( VectorLength( seg ) < 0.5f ) {
			continue;
		}
		// Midpoint used to match saber entities to dlight midpoints.
		VectorAdd( sabers[numSabers].start, sabers[numSabers].end, sabers[numSabers].mid );
		VectorScale( sabers[numSabers].mid, 0.5f, sabers[numSabers].mid );
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
		// No dynamic lights — clear output and skip RT dispatch.
		// Still flip ping-pong to keep state consistent (overlay will composite the cleared image).
		int pp = vk.glowReflect.pingPongIndex;
		VkClearColorValue clear = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		VkImageSubresourceRange range = {};
		range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		range.baseMipLevel = 0;
		range.levelCount = 1;
		range.baseArrayLayer = 0;
		range.layerCount = 1;
		vkCmdClearColorImage( cmd, vk.glowReflect.ppImage[pp],
			VK_IMAGE_LAYOUT_GENERAL, &clear, 1, &range );

		vk.glowReflect.blurActive = qfalse;
		vk.glowReflect.pingPongIndex = 1 - pp;  // Flip to maintain state consistency
		vk.glowReflect.rtFrameIndex++;
		return;
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

	// ---- Update RT params UBO (prev VP, frame index, temporal settings) ----
	if ( vk.glowReflect.rtParamsMapped ) {
		struct rtParamsUBO_t {
			float prevViewProjection[16];
			uint32_t params0[4];
			float params1[4];
		};
		rtParamsUBO_t *p = (rtParamsUBO_t *)vk.glowReflect.rtParamsMapped;

		// Use stored previous VP; on first frame, seed it with current VP to avoid ghosting.
		if ( vk.glowReflect.rtFrameIndex == 0 ) {
			Com_Memcpy( vk.glowReflect.prevViewProjection, pc.viewProjection, sizeof(vk.glowReflect.prevViewProjection) );
		}
		Com_Memcpy( p->prevViewProjection, vk.glowReflect.prevViewProjection, sizeof(p->prevViewProjection) );
		p->params0[0] = vk.glowReflect.rtFrameIndex;
		p->params0[1] = 0;
		p->params0[2] = 0;
		p->params0[3] = 0;
		p->params1[0] = 0.35f; // seeThroughOpacity
		p->params1[1] = 1.0f;  // emitterEpsilon
		p->params1[2] = 0.70f; // temporalBlend
		p->params1[3] = r_DynamicGlowReflectionG2Opacity ? Com_Clamp( 0.0f, 1.0f, r_DynamicGlowReflectionG2Opacity->value ) : 0.65f;
	}

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

		// Bone names for the skeletal proxy (key joints). Some models may not expose
		// the same bolt names, so allow a small fallback list per joint.
		typedef struct { const char *n0, *n1; } proxyBoneName_t;
		static const proxyBoneName_t proxyBoneNames[] = {
			{ "pelvis",       "hips" },  // 0: hip center
			{ "lower_lumbar", "spine" }, // 1: waist
			{ "thoracic",     "torso" }, // 2: chest
			{ "cervical",     "neck" },  // 3: neck
			{ "cranium",      "head" },  // 4: head top
			{ NULL,           NULL }, // 5: head top (synthesized)
			{ "rhumerus",     NULL }, // 6: right shoulder
			{ "lhumerus",     NULL }, // 7: left shoulder
			{ "rradius",      NULL }, // 8: right elbow
			{ "lradius",      NULL }, // 9: left elbow
			{ "rhand",        "r_hand" }, // 10: right hand
			{ "lhand",        "l_hand" }, // 11: left hand
			{ "rtibia",       NULL }, // 12: right knee
			{ "ltibia",       NULL }, // 13: left knee
			{ "rtalus",       NULL }, // 14: right ankle
			{ "ltalus",       NULL }, // 15: left ankle
		};
		enum { kNumProxyBones = (int)ARRAY_LEN( proxyBoneNames ) };

		// Body segments: each is a tapered prism between two bone positions
		// { boneA, boneB, radiusA, radiusB }
		static const struct { int a, b; float rA, rB; } proxySegments[] = {
			{ 0, 1, 7.5f, 7.0f },   // pelvis → waist
			{ 1, 2, 7.0f, 6.2f },   // waist → chest
			{ 2, 3, 6.0f, 4.8f },   // chest → neck
			{ 3, 4, 2.8f, 2.4f },   // neck → head center
			{ 4, 5, 2.4f, 0.8f },   // head center → head top (tapered skull)
			{ 2, 6, 4.0f, 3.0f },   // chest → right shoulder
			{ 2, 7, 4.0f, 3.0f },   // chest → left shoulder
			{ 6, 8, 3.0f, 2.3f },   // right shoulder → elbow
			{ 7, 9, 3.0f, 2.3f },   // left shoulder → elbow
			{ 8, 10, 2.2f, 1.4f },  // right elbow → hand (forearm)
			{ 9, 11, 2.2f, 1.4f },  // left elbow → hand (forearm)
			{ 0, 12, 4.8f, 4.0f },  // pelvis → right knee
			{ 0, 13, 4.8f, 4.0f },  // pelvis → left knee
			{ 12, 14, 3.6f, 2.9f }, // right knee → ankle
			{ 13, 15, 3.6f, 2.9f }, // left knee → ankle
		};
		enum { kNumSegments = (int)ARRAY_LEN( proxySegments ) };

		// 24-sided ring for a rounder silhouette (head benefits most).
		static const int kSides = 24;
		static const float ringCos[kSides] = {
			 1.0000000f,  0.9659258f,  0.8660254f,  0.7071068f,
			 0.5000000f,  0.2588190f,  0.0000000f, -0.2588190f,
			-0.5000000f, -0.7071068f, -0.8660254f, -0.9659258f,
			-1.0000000f, -0.9659258f, -0.8660254f, -0.7071068f,
			-0.5000000f, -0.2588190f,  0.0000000f,  0.2588190f,
			 0.5000000f,  0.7071068f,  0.8660254f,  0.9659258f
		};
		static const float ringSin[kSides] = {
			 0.0000000f,  0.2588190f,  0.5000000f,  0.7071068f,
			 0.8660254f,  0.9659258f,  1.0000000f,  0.9659258f,
			 0.8660254f,  0.7071068f,  0.5000000f,  0.2588190f,
			 0.0000000f, -0.2588190f, -0.5000000f, -0.7071068f,
			-0.8660254f, -0.9659258f, -1.0000000f, -0.9659258f,
			-0.8660254f, -0.7071068f, -0.5000000f, -0.2588190f
		};

		// Generate bone-driven proxy for all Ghoul2 entities
		const int kVertsPerSegment   = kSides * 2;
		const int kIndicesPerSegment = (12 * kSides - 12);
		const int kTrisPerSegment    = kIndicesPerSegment / 3;
		const int kVertsPerEntity    = kNumSegments * kVertsPerSegment;
		const int kIndicesPerEntity  = kNumSegments * kIndicesPerSegment;
		// Static to avoid large stack allocation each frame (not re-entrant, single-threaded renderer)
		// Size for kNumSegments; keep a small cushion to avoid churn when tweaking proxySegments.
		static float    g2VertsCPU[GLOW_RT_MAX_GHOUL2_ENTITIES * 16 * (24 * 2) * 3];
		static uint32_t g2IdxCPU[GLOW_RT_MAX_GHOUL2_ENTITIES * 16 * (12 * 24 - 12)];
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
			vec3_t bonePos[kNumProxyBones];
			qboolean boneValid[kNumProxyBones];
			int numValid = 0;

			static const vec3_t zeroVec = { 0, 0, 0 };

			for ( int b = 0; b < kNumProxyBones; b++ ) {
				boneValid[b] = qfalse;
				int boltIdx = -1;
				if ( proxyBoneNames[b].n0 ) {
					boltIdx = G2API_AddBolt( ent->e.ghoul2, 0, proxyBoneNames[b].n0 );
				}
				if ( boltIdx < 0 && proxyBoneNames[b].n1 ) {
					boltIdx = G2API_AddBolt( ent->e.ghoul2, 0, proxyBoneNames[b].n1 );
				}
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

			// Some player models (including Kyle variants) don't expose distinct
			// chest/neck/head bolts. Synthesize a reasonable chain so the proxy
			// always has a head silhouette.
			// Indices: 0 pelvis, 1 waist, 2 chest, 3 neck, 4 head.
			vec3_t upAxis = { ent->e.axis[2][0], ent->e.axis[2][1], ent->e.axis[2][2] };
			if ( VectorLength( upAxis ) < 0.25f ) {
				upAxis[0] = 0.0f; upAxis[1] = 0.0f; upAxis[2] = 1.0f;
			}
			// Seed pelvis/waist from entity origin if missing, so the rest can be synthesized.
			if ( !boneValid[0] ) {
				VectorCopy( ent->e.origin, bonePos[0] );
				boneValid[0] = qtrue;
				numValid++;
			}
			if ( !boneValid[1] ) {
				VectorMA( bonePos[0], 12.0f, upAxis, bonePos[1] );
				boneValid[1] = qtrue;
				numValid++;
			}
			if ( !boneValid[2] ) {
				if ( boneValid[1] ) {
					VectorMA( bonePos[1], 14.0f, upAxis, bonePos[2] );
					boneValid[2] = qtrue;
					numValid++;
				} else if ( boneValid[0] ) {
					VectorMA( bonePos[0], 28.0f, upAxis, bonePos[2] );
					boneValid[2] = qtrue;
					numValid++;
				}
			}
			if ( !boneValid[3] && boneValid[2] ) {
				VectorMA( bonePos[2], 10.0f, upAxis, bonePos[3] );
				boneValid[3] = qtrue;
				numValid++;
			}
			if ( !boneValid[4] && boneValid[3] ) {
				// Prefer chest→neck direction if available to follow pose; otherwise use up-axis.
				vec3_t dir;
				if ( boneValid[2] ) {
					VectorSubtract( bonePos[3], bonePos[2], dir );
					float len = VectorNormalize( dir );
					if ( len < 0.1f ) {
						VectorCopy( upAxis, dir );
					}
				} else {
					VectorCopy( upAxis, dir );
				}
				VectorMA( bonePos[3], 12.0f, dir, bonePos[4] );
				boneValid[4] = qtrue;
				numValid++;
			}
			// Bone 5 is a synthesized head-top used to taper the skull.
			if ( !boneValid[5] && boneValid[4] ) {
				VectorMA( bonePos[4], 6.0f, upAxis, bonePos[5] );
				boneValid[5] = qtrue;
				numValid++;
			}

			if ( numValid < 3 ) continue; // not a humanoid model, skip

			// Scale proxy thickness by average modelScale to better match non-1.0 scaled Ghoul2.
			float scale = ( ent->e.modelScale[0] + ent->e.modelScale[1] + ent->e.modelScale[2] ) * ( 1.0f / 3.0f );
			if ( scale <= 0.0f ) scale = 1.0f;

			int entityVertBase = numGhoul2 * kVertsPerEntity;
			int entityIdxBase  = numGhoul2 * kIndicesPerEntity;

			// Build oriented prisms between bone pairs
			for ( int seg = 0; seg < kNumSegments; seg++ ) {
				int bA = proxySegments[seg].a;
				int bB = proxySegments[seg].b;
				float radiusA = proxySegments[seg].rA * scale;
				float radiusB = proxySegments[seg].rB * scale;

				vec3_t posA, posB;
				if ( boneValid[bA] ) { VectorCopy( bonePos[bA], posA ); }
				else { VectorMA( ent->e.origin, 32.0f, upAxis, posA ); }
				if ( boneValid[bB] ) { VectorCopy( bonePos[bB], posB ); }
				else { VectorMA( posA, 8.0f, upAxis, posB ); }

				// If the head segment still collapses (bad bones), force a minimum length.
				if ( bA == 3 && bB == 4 ) {
					vec3_t d;
					VectorSubtract( posB, posA, d );
					if ( VectorLength( d ) < 6.0f ) {
						VectorMA( posA, 14.0f, upAxis, posB );
					}
				}

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

				// kSides verts at posA (bottom ring), kSides at posB (top ring)
				for ( int c = 0; c < kVertsPerSegment; c++ ) {
					int ring = c % kSides;
					float r = ( c < kSides ) ? radiusA : radiusB;
					float sx = r * ringCos[ring];
					float sy = r * ringSin[ring];
					float *base = ( c < kSides ) ? posA : posB;

					int vi = ( segVertBase + c ) * 3;
					g2VertsCPU[vi + 0] = base[0] + sx * perp1[0] + sy * perp2[0];
					g2VertsCPU[vi + 1] = base[1] + sx * perp1[1] + sy * perp2[1];
					g2VertsCPU[vi + 2] = base[2] + sx * perp1[2] + sy * perp2[2];
				}

				// Procedural index generation for a kSides-sided prism.
				// Layout: bottom ring [0..kSides-1], top ring [kSides..2*kSides-1].
				int w = 0;
				// Bottom cap fan from vertex 0
				for ( int iFan = 1; iFan < kSides - 1; iFan++ ) {
					g2IdxCPU[segIdxBase + w++] = (uint32_t)segVertBase + 0;
					g2IdxCPU[segIdxBase + w++] = (uint32_t)segVertBase + (uint32_t)iFan;
					g2IdxCPU[segIdxBase + w++] = (uint32_t)segVertBase + (uint32_t)( iFan + 1 );
				}
				// Top cap fan from vertex kSides (flip winding)
				for ( int iFan = 1; iFan < kSides - 1; iFan++ ) {
					g2IdxCPU[segIdxBase + w++] = (uint32_t)segVertBase + (uint32_t)kSides;
					g2IdxCPU[segIdxBase + w++] = (uint32_t)segVertBase + (uint32_t)( kSides + iFan + 1 );
					g2IdxCPU[segIdxBase + w++] = (uint32_t)segVertBase + (uint32_t)( kSides + iFan );
				}
				// Side quads
				for ( int iSide = 0; iSide < kSides; iSide++ ) {
					int i0 = iSide;
					int i1 = ( iSide + 1 ) % kSides;
					int j0 = i0 + kSides;
					int j1 = i1 + kSides;
					g2IdxCPU[segIdxBase + w++] = (uint32_t)segVertBase + (uint32_t)i0;
					g2IdxCPU[segIdxBase + w++] = (uint32_t)segVertBase + (uint32_t)j1;
					g2IdxCPU[segIdxBase + w++] = (uint32_t)segVertBase + (uint32_t)i1;
					g2IdxCPU[segIdxBase + w++] = (uint32_t)segVertBase + (uint32_t)i0;
					g2IdxCPU[segIdxBase + w++] = (uint32_t)segVertBase + (uint32_t)j0;
					g2IdxCPU[segIdxBase + w++] = (uint32_t)segVertBase + (uint32_t)j1;
				}
				// Sanity: w should match kIndicesPerSegment
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

			// Prepare TLAS instance data (opaque BSP always; optional see-through BSP; Ghoul2 if present)
			VkAccelerationStructureDeviceAddressInfoKHR addrInfo = {};
			addrInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
			addrInfo.accelerationStructure = vk.glowReflect.blas;
			VkDeviceAddress bspBlasAddr =
				vk.rtFuncs.vkGetAccelerationStructureDeviceAddressKHR( vk.device, &addrInfo );

			VkAccelerationStructureInstanceKHR instancesCPU[3] = {};

			// Instance 0: BSP world geometry
			instancesCPU[0].transform.matrix[0][0] = 1.0f;
			instancesCPU[0].transform.matrix[1][1] = 1.0f;
			instancesCPU[0].transform.matrix[2][2] = 1.0f;
			instancesCPU[0].instanceCustomIndex = 0;
			instancesCPU[0].mask = 0x01;  // BSP-only mask (probed separately from Ghoul2)
			instancesCPU[0].instanceShaderBindingTableRecordOffset = 0;
			instancesCPU[0].flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
			instancesCPU[0].accelerationStructureReference = bspBlasAddr;

			uint32_t instanceCount = 1;

			// Optional see-through BSP instance
			if ( vk.glowReflect.blasSeeThrough ) {
				addrInfo.accelerationStructure = vk.glowReflect.blasSeeThrough;
				VkDeviceAddress stBlasAddr =
					vk.rtFuncs.vkGetAccelerationStructureDeviceAddressKHR( vk.device, &addrInfo );
				instancesCPU[1].transform.matrix[0][0] = 1.0f;
				instancesCPU[1].transform.matrix[1][1] = 1.0f;
				instancesCPU[1].transform.matrix[2][2] = 1.0f;
				instancesCPU[1].instanceCustomIndex = 1;
				instancesCPU[1].mask = 0x04;
				instancesCPU[1].instanceShaderBindingTableRecordOffset = 0;
				instancesCPU[1].flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
				instancesCPU[1].accelerationStructureReference = stBlasAddr;
				instanceCount = 2;
			}

			if ( numGhoul2 > 0 ) {
				addrInfo.accelerationStructure = vk.glowReflect.ghoul2Blas;
				VkDeviceAddress g2BlasAddr =
					vk.rtFuncs.vkGetAccelerationStructureDeviceAddressKHR( vk.device, &addrInfo );

				// Next instance slot: Ghoul2 proxy geometry
				uint32_t g2Slot = instanceCount;
				instancesCPU[g2Slot].transform.matrix[0][0] = 1.0f;
				instancesCPU[g2Slot].transform.matrix[1][1] = 1.0f;
				instancesCPU[g2Slot].transform.matrix[2][2] = 1.0f;
				instancesCPU[g2Slot].instanceCustomIndex = 2;
				instancesCPU[g2Slot].mask = 0x02;  // Ghoul2 mask (not matched by BSP detection probe)
				instancesCPU[g2Slot].instanceShaderBindingTableRecordOffset = 0;
				instancesCPU[g2Slot].flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
				instancesCPU[g2Slot].accelerationStructureReference = g2BlasAddr;

				instanceCount++;
			}

			// Track whether G2 is in the TLAS this frame
			vk.glowReflect.hasGhoul2 = (numGhoul2 > 0) ? qtrue : qfalse;

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
				uint32_t g2TriCount  = (uint32_t)numGhoul2 * kNumSegments * (uint32_t)kTrisPerSegment;
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
	int pp = vk.glowReflect.pingPongIndex;
	vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, vk.glowReflect.rtPipeline );
	vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
		vk.glowReflect.rtPipelineLayout, 0, 1,
		&vk.glowReflect.ppRtDescriptorSet[pp], 0, NULL );
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

	// ---- Barrier: ensure RT writes are visible to blur/composite ----
	// ppImage[pp] (output) stays in GENERAL for composite.
	// ppImage[1-pp] (history) stays in SHADER_READ_ONLY (descriptor expects it there).
	// Layout transitions for NEXT frame happen at frame end (overlay function).
	{
		VkImageMemoryBarrier barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = vk.glowReflect.ppImage[pp];
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;

		vkCmdPipelineBarrier( cmd,
			VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0, 0, NULL, 0, NULL, 1, &barrier );
	}

	// Advance frame state for temporal reprojection and flip ping-pong
	Com_Memcpy( vk.glowReflect.prevViewProjection, pc.viewProjection, sizeof(vk.glowReflect.prevViewProjection) );
	vk.glowReflect.rtFrameIndex++;
	vk.glowReflect.pingPongIndex = 1 - pp;
}

/*
================
VK_BlurGlowReflectOutput

Runs a separable Gaussian blur (H then V) on the RT reflection output
to soften hard shadow edges.  Reuses the glow blur pipeline + render pass.
Must be called OUTSIDE a render pass, after VK_DispatchGlowReflect.
================
*/
void VK_BlurGlowReflectOutput( void ) {
	if ( !vk.glowReflect.available ) return;
	if ( !r_DynamicGlowReflections || !r_DynamicGlowReflections->integer ) return;
	if ( !r_DynamicGlowReflectionBlur || r_DynamicGlowReflectionBlur->value <= 0.0f ) return;
	if ( !vk.glow.blurRenderPass ) return;
	if ( !vk.glowReflect.blurTempFramebuffer || !vk.glowReflect.blurOutputFramebuffer ) return;

	// Prefer the alpha-masked pipeline (skips Ghoul2 pixels); fall back to generic blur
	VkPipeline blurPipeline = vk.glowReflect.blurMaskedPipeline ? vk.glowReflect.blurMaskedPipeline : vk.glow.blurPipeline;
	if ( !blurPipeline ) return;

	VkCommandBuffer cmd = vk.frames[vk.currentFrame].commandBuffer;
	uint32_t width  = vk.glowReflect.rtWidth  ? vk.glowReflect.rtWidth  : vk.swapchainExtent.width;
	uint32_t height = vk.glowReflect.rtHeight ? vk.glowReflect.rtHeight : vk.swapchainExtent.height;

	float softness = r_DynamicGlowReflectionBlur->value;

	// The current output is the image that was just written by dispatch.
	// After dispatch flipped pingPongIndex, the output is at [1 - pingPongIndex].
	int ppOut = 1 - vk.glowReflect.pingPongIndex;

	// Visibility barrier: ensure RT writes are visible to blur reads (keep in GENERAL)
	{
		VkImageMemoryBarrier barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;  // Stay in GENERAL
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = vk.glowReflect.ppImage[ppOut];
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;

		vkCmdPipelineBarrier( cmd,
			VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0, 0, NULL, 0, NULL, 1, &barrier );
	}

	VkViewport viewport = {};
	viewport.width  = (float)width;
	viewport.height = (float)height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	VkRect2D scissor = {};
	scissor.extent.width  = width;
	scissor.extent.height = height;

	// --- Horizontal pass: outputImage -> blurTempImage ---
	{
		VkClearValue clearValue = {};
		clearValue.color = { { 0.0f, 0.0f, 0.0f, 0.0f } };

		VkRenderPassBeginInfo rpBegin = {};
		rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		rpBegin.renderPass  = vk.glow.blurRenderPass;
		rpBegin.framebuffer = vk.glowReflect.blurTempFramebuffer;
		rpBegin.clearValueCount = 1;
		rpBegin.pClearValues = &clearValue;
		rpBegin.renderArea.extent.width  = width;
		rpBegin.renderArea.extent.height = height;

		vk.renderPassActive = qtrue;
		vkCmdBeginRenderPass( cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE );
		vkCmdSetViewport( cmd, 0, 1, &viewport );
		vkCmdSetScissor( cmd, 0, 1, &scissor );
		vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, blurPipeline );

		vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
			vk.pipelineLayout, 0, 1, &vk.glowReflect.ppOutputReadDescriptorSet[ppOut], 0, NULL );

		float blurPC[4] = { 1.0f / (float)width, 0.0f, softness, 0.0f };
		vkCmdPushConstants( cmd, vk.pipelineLayout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(blurPC), blurPC );

		vkCmdDraw( cmd, 3, 1, 0, 0 );
		vkCmdEndRenderPass( cmd );
		vk.renderPassActive = qfalse;
	}

	// Barrier: blurTemp write -> shader read
	{
		VkImageMemoryBarrier barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = vk.glowReflect.blurTempImage;
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

	// --- Vertical pass: blurTempImage -> blurOutputImage ---
	{
		VkClearValue clearValue = {};
		clearValue.color = { { 0.0f, 0.0f, 0.0f, 0.0f } };

		VkRenderPassBeginInfo rpBegin = {};
		rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		rpBegin.renderPass  = vk.glow.blurRenderPass;
		rpBegin.framebuffer = vk.glowReflect.blurOutputFramebuffer;
		rpBegin.clearValueCount = 1;
		rpBegin.pClearValues = &clearValue;
		rpBegin.renderArea.extent.width  = width;
		rpBegin.renderArea.extent.height = height;

		vk.renderPassActive = qtrue;
		vkCmdBeginRenderPass( cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE );
		vkCmdSetViewport( cmd, 0, 1, &viewport );
		vkCmdSetScissor( cmd, 0, 1, &scissor );
		vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, blurPipeline );

		vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
			vk.pipelineLayout, 0, 1, &vk.glowReflect.blurTempDescriptorSet, 0, NULL );

		float blurPC[4] = { 0.0f, 1.0f / (float)height, softness, 0.0f };
		vkCmdPushConstants( cmd, vk.pipelineLayout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(blurPC), blurPC );

		vkCmdDraw( cmd, 3, 1, 0, 0 );
		vkCmdEndRenderPass( cmd );
		vk.renderPassActive = qfalse;
	}

	// Barrier: blurOutput write -> shader read for composite
	{
		VkImageMemoryBarrier barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = vk.glowReflect.blurOutputImage;
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

	// Mark that blur was performed this frame, so overlay reads the blurred output
	vk.glowReflect.blurActive = qtrue;
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
	if ( !vk.glow.glowCompositePipeline ) return;

	// The current output is at [1 - pingPongIndex] (dispatch already flipped)
	int ppOut = 1 - vk.glowReflect.pingPongIndex;
	if ( !vk.glowReflect.ppOutputDescriptorSet[ppOut] ) return;

	if ( !vk.renderPassActive ) {
		ri.Printf( PRINT_WARNING, "VK_DrawGlowReflectOverlay: called outside render pass\n" );
		return;
	}

	VkCommandBuffer cmd = vk.frames[vk.currentFrame].commandBuffer;
	float intensity = 1.0f;

	if ( vk.glowReflect.blurActive && vk.glowReflect.blurOutputDescriptorSet ) {
		// --- Pass 1: blurred BSP reflections (Ghoul2 pixels are zeroed in blur output) ---
		vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.glow.glowCompositePipeline );
		vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
			vk.pipelineLayout, 0, 1, &vk.glowReflect.blurOutputDescriptorSet, 0, NULL );
		vkCmdPushConstants( cmd, vk.pipelineLayout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(intensity), &intensity );
		vkCmdDraw( cmd, 3, 1, 0, 0 );

		// --- Pass 2: sharp Ghoul2 reflections from raw RT output ---
		if ( vk.glowReflect.g2CompositePipeline && vk.glowReflect.hasGhoul2 ) {
			vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.glowReflect.g2CompositePipeline );
			vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
				vk.pipelineLayout, 0, 1, &vk.glowReflect.ppOutputDescriptorSet[ppOut], 0, NULL );
			vkCmdPushConstants( cmd, vk.pipelineLayout,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(intensity), &intensity );
			vkCmdDraw( cmd, 3, 1, 0, 0 );
		}
	} else {
		// No blur: composite raw output directly
		vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.glow.glowCompositePipeline );
		vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
			vk.pipelineLayout, 0, 1, &vk.glowReflect.ppOutputDescriptorSet[ppOut], 0, NULL );
		vkCmdPushConstants( cmd, vk.pipelineLayout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(intensity), &intensity );
		vkCmdDraw( cmd, 3, 1, 0, 0 );
	}

	// Reset blur flag for next frame
	vk.glowReflect.blurActive = qfalse;
}

#endif // !DEDICATED
