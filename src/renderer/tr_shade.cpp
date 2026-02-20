// tr_shade.c

#include "tr_local.h"
#include "vk_local.h"

#include "tr_quicksprite.h"

/*

  THIS ENTIRE FILE IS BACK END

  This file deals with applying shaders to surface data in the tess struct.
*/

shaderCommands_t	tess;

color4ub_t	styleColors[MAX_LIGHT_STYLES];

extern bool g_bRenderGlowingObjects;

// ============================================================
// GPU offload: per-draw UBO params accumulated during
// ComputeColors / ComputeTexCoords and consumed by R_DrawElements.
// ============================================================
static gpuParams_t currentGPUParams;

// Fixed specular light origin (world space) — matches tr_shade_calc.cpp
extern vec3_t lightOrigin; // { -960, 1980, 96 }

/*
=================
GPU_SetupFogParams

Compute fog plane equations from current entity orientation
and fog brush. Stores results into currentGPUParams fog fields.
Must be called per-surface when tess.fogNum is set.
=================
*/
static void GPU_SetupFogParams( void ) {
	fog_t	*fog;
	vec3_t	local;
	float	eyeT;

	if ( !tess.fogNum || !tr.world || tess.fogNum < 0 || tess.fogNum >= tr.world->numfogs ) {
		return;
	}

	fog = tr.world->fogs + tess.fogNum;

	// Fog distance vector (based on entity model matrix)
	VectorSubtract( backEnd.ori.origin, backEnd.viewParms.ori.origin, local );
	currentGPUParams.fogDistVec[0] = -backEnd.ori.modelMatrix[2];
	currentGPUParams.fogDistVec[1] = -backEnd.ori.modelMatrix[6];
	currentGPUParams.fogDistVec[2] = -backEnd.ori.modelMatrix[10];
	currentGPUParams.fogDistVec[3] = DotProduct( local, backEnd.viewParms.ori.axis[0] );

	// Scale by fog thickness
	currentGPUParams.fogDistVec[0] *= fog->tcScale;
	currentGPUParams.fogDistVec[1] *= fog->tcScale;
	currentGPUParams.fogDistVec[2] *= fog->tcScale;
	currentGPUParams.fogDistVec[3] *= fog->tcScale;

	// Bias (matches CPU RB_CalcFogTexCoords)
	currentGPUParams.fogDistVec[3] += 1.0f / 512;

	// Fog depth vector
	if ( fog->hasSurface ) {
		currentGPUParams.fogDepthVec[0] = fog->surface[0] * backEnd.ori.axis[0][0] +
			fog->surface[1] * backEnd.ori.axis[0][1] + fog->surface[2] * backEnd.ori.axis[0][2];
		currentGPUParams.fogDepthVec[1] = fog->surface[0] * backEnd.ori.axis[1][0] +
			fog->surface[1] * backEnd.ori.axis[1][1] + fog->surface[2] * backEnd.ori.axis[1][2];
		currentGPUParams.fogDepthVec[2] = fog->surface[0] * backEnd.ori.axis[2][0] +
			fog->surface[1] * backEnd.ori.axis[2][1] + fog->surface[2] * backEnd.ori.axis[2][2];
		currentGPUParams.fogDepthVec[3] = -fog->surface[3] + DotProduct( backEnd.ori.origin, fog->surface );

		eyeT = DotProduct( backEnd.ori.viewOrigin, currentGPUParams.fogDepthVec ) + currentGPUParams.fogDepthVec[3];
		currentGPUParams.fogEyeT = eyeT;
		currentGPUParams.fogEyeOutside = ( eyeT < 0 ) ? 1.0f : 0.0f;
	} else {
		currentGPUParams.fogDepthVec[0] = 0;
		currentGPUParams.fogDepthVec[1] = 0;
		currentGPUParams.fogDepthVec[2] = 0;
		currentGPUParams.fogDepthVec[3] = 0;
		currentGPUParams.fogEyeT = 1.0f;
		currentGPUParams.fogEyeOutside = 0.0f;
	}

	// Fog color (pre-scaled by identityLight, matches ColorBytes4 in R_LoadFogs)
	currentGPUParams.fogColor[0] = fog->parms.color[0] * tr.identityLight;
	currentGPUParams.fogColor[1] = fog->parms.color[1] * tr.identityLight;
	currentGPUParams.fogColor[2] = fog->parms.color[2] * tr.identityLight;
	currentGPUParams.fogColor[3] = 1.0f;
}

/*
=================
GPU_ResetParams

Reset GPU params for a new draw call. Call once per stage before
ComputeColors / ComputeTexCoords.
=================
*/
static void GPU_ResetParams( void ) {
	Com_Memset( &currentGPUParams, 0, GPU_PARAMS_BASE_SIZE );
	currentGPUParams.identityLight = (float)tr.identityLight;
	VectorCopy( backEnd.ori.viewOrigin, currentGPUParams.viewOrigin );

	// If the current batch uses GPU skinning, upload bone matrices
	if ( tess.gpuSkinning ) {
		currentGPUParams.gpuFlags |= GPU_FLAG_SKINNING;
		int numBones = tess.gpuNumBones;
		if ( numBones > GPU_MAX_BONES ) numBones = GPU_MAX_BONES;
		Com_Memcpy( currentGPUParams.boneMatrices, tess.gpuBoneMatrices,
			numBones * 12 * sizeof(float) );
	}
}

/*
==================
R_DrawElements

In Vulkan, we always draw indexed triangles via the dynamic vertex/index buffers.
The tess struct contains the vertex data that was set up by the caller.
==================
*/
static void R_DrawElements( int numIndexes, const glIndex_t *indexes ) {
	if ( numIndexes <= 0 ) {
		return;
	}

	// Bind the correct Vulkan pipeline for the current rendering state
	VK_BindPipeline( renderState.stateBits, renderState.faceCulling, renderState.multiTexture, renderState.polygonOffset );

	// Push fragment constants: alpha test function and tex env mode
	{
		float alphaTestFunc = 0.0f;
		unsigned int atest = renderState.stateBits & GLS_ATEST_BITS;
		if ( atest == GLS_ATEST_GT_0 ) {
			alphaTestFunc = 1.0f;
		} else if ( atest == GLS_ATEST_LT_80 ) {
			alphaTestFunc = 2.0f;
		} else if ( atest == GLS_ATEST_GE_80 ) {
			alphaTestFunc = 3.0f;
		} else if ( atest == GLS_ATEST_GE_C0 ) {
			alphaTestFunc = 4.0f;
		}

		float texEnvMode = 0.0f; // modulate
		if ( renderState.multiTexture ) {
			if ( tess.shader && tess.shader->multitextureEnv == TEXENV_ADD ) {
				texEnvMode = 3.0f; // add
			}
		}

		// Push alphaTestFunc and texEnvMode at their respective offsets
		// Layout: mvp(64) + texEnvMode(4) + alphaTestFunc(4)
		float fragConstants[2] = { texEnvMode, alphaTestFunc };
		VkCommandBuffer cmd = vk.frames[vk.currentFrame].commandBuffer;
		vkCmdPushConstants( cmd, vk.pipelineLayout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			64, sizeof(float) * 2, fragConstants );
	}

	// Upload GPU params UBO and bind descriptor set 2
	if ( !VK_UpdateGPUParams( &currentGPUParams ) ) {
		return;
	}

	// Optimization #2: if geometry base (pos/normal/idx) is already cached,
	// only upload the varying data (texcoords, colors) per stage
	if ( tess.cachedGeo.valid ) {
		VK_DrawWithCachedGeo(
			tess.numVertexes,
			(float *)tess.svars.texcoords[0],
			renderState.multiTexture ? (float *)tess.svars.texcoords[1] : NULL,
			(unsigned char *)tess.svars.colors,
			numIndexes,
			indexes
		);
	} else {
		VK_DrawIndexedWithNormals(
			tess.numVertexes,
			(float *)tess.xyz,
			(float *)tess.normal,
			(float *)tess.svars.texcoords[0],
			renderState.multiTexture ? (float *)tess.svars.texcoords[1] : NULL,
			(unsigned char *)tess.svars.colors,
			numIndexes,
			indexes
		);
	}
}




/*
=============================================================

SURFACE SHADERS

=============================================================
*/

/*
=================
R_BindAnimatedImage

=================
*/
// de-static'd because tr_quicksprite wants it
void R_BindAnimatedImage( textureBundle_t *bundle ) {
	int64_t		index;

	if ( bundle->isVideoMap ) {
		ri.CIN_RunCinematic(bundle->videoMapHandle);
		ri.CIN_UploadCinematic(bundle->videoMapHandle);
		return;
	}

	if ((r_fullbright->value) && bundle->isLightmap)
	{
		R_BindImage( tr.whiteImage );
		return;
	}

	if ( bundle->numImageAnimations <= 1 ) {
		R_BindImage( bundle->image[0] );
		return;
	}

	// it is necessary to do this messy calc to make sure animations line up
	// exactly with waveforms of the same frequency
	index = Q_dtol(tess.shaderTime * bundle->imageAnimationSpeed * FUNCTABLE_SIZE);
	index >>= FUNCTABLE_SIZE2;

	if ( index < 0 ) {
		index = 0;	// may happen with shader time offsets
	}
	if ( bundle->oneShotAnimMap )
	{
		if ( index >= bundle->numImageAnimations )
		{
			// stick on last frame
			index = bundle->numImageAnimations - 1;
		}
	}
	else
	{
		// loop
		index %= bundle->numImageAnimations;
	}

	R_BindImage( bundle->image[ index ] );
}

/*
================
DrawTris

Draws triangle outlines for debugging
================
*/
static void DrawTris (shaderCommands_t *input) {
	// Vulkan: wireframe debug drawing
	// For now, just use the regular draw path with a white texture
	// A proper implementation would use a wireframe pipeline
	R_BindImage( tr.whiteImage );

	// Set white color for all vertices
	Com_Memset( tess.svars.colors, 0xff, tess.numVertexes * 4 );

	R_SetStateBits( GLS_POLYMODE_LINE | GLS_DEPTHMASK_TRUE );
	VK_SetDepthRange( 0.0f, 0.0f );

	R_DrawElements( input->numIndexes, input->indexes );

	VK_SetDepthRange( 0.0f, 1.0f );
}


/*
================
DrawNormals

Draws vertex normals for debugging
================
*/
static void DrawNormals (shaderCommands_t *input) {
	// Vulkan: line drawing for normals debug
	// This would require a line-drawing pipeline.
	// For now, this is a stub - normals debug display is not implemented
	// in the Vulkan backend.
	(void)input;
}

/*
==============
RB_BeginSurface

We must set some things up before beginning any tesselation,
because a surface may be forced to perform a RB_End due
to overflow.
==============
*/
void RB_BeginSurface( shader_t *shader, int fogNum ) {

	shader_t *state = (shader->remappedShaderAdvanced ? shader->remappedShaderAdvanced : (shader->remappedShader ? shader->remappedShader : shader));
	if ( state->defaultShader ) state = tr.defaultShader;

	tess.numIndexes = 0;
	tess.numVertexes = 0;
	tess.shader = state;
	tess.fogNum = fogNum;
	tess.dlightBits = 0;		// will be OR'd in by surface functions
	tess.xstages = state->stages;
	tess.numPasses = state->numUnfoggedPasses;
	tess.currentStageIteratorFunc = state->optimalStageIteratorFunc;

	tess.shaderTime = backEnd.refdef.floatTime - tess.shader->timeOffset;
	if (tess.shader->clampTime && tess.shaderTime >= tess.shader->clampTime) {
		tess.shaderTime = tess.shader->clampTime;
	}

	// Clear GPU skinning state — will be set by RB_SurfaceGhoul if needed
	tess.gpuSkinning = qfalse;
	tess.gpuNumBones = 0;
	tess.wComponentsInitialized = qfalse;

	// Clear cached geometry — will be set by VK_CacheTessGeometry
	tess.cachedGeo.valid = qfalse;
	tess.vboMesh = NULL;
}

static void RB_InitTessWComponents( void ) {
	if ( tess.wComponentsInitialized ) {
		return;
	}
	// Ghoul2 GPU skinning packs bone indices/weights into xyz.w/normal.w.
	if ( tess.gpuSkinning ) {
		tess.wComponentsInitialized = qtrue;
		return;
	}
	for ( int i = 0; i < tess.numVertexes; i++ ) {
		tess.xyz[i][3] = 1.0f;
		tess.normal[i][3] = 0.0f;
	}
	tess.wComponentsInitialized = qtrue;
}

/*
===================
DrawMultitextured

output = t0 * t1 or t0 + t1

t0 = most upstream according to spec
t1 = most downstream according to spec
===================
*/
static void DrawMultitextured( shaderCommands_t *input, int stage ) {
	shaderStage_t	*pStage;

	pStage = tess.xstages[stage];

	R_SetStateBits( pStage->stateBits );
	renderState.multiTexture = qtrue;

	//
	// base
	//
	R_SelectTexture( 0 );
	R_BindAnimatedImage( &pStage->bundle[0] );

	//
	// lightmap/secondary pass
	//
	R_SelectTexture( 1 );

	R_BindAnimatedImage( &pStage->bundle[1] );

	// Vulkan: multi-texture is handled via the multi-tex pipeline
	// The pipeline key system will select the correct shader based on
	// whether we're doing modulate or add multitexture
	R_DrawElements( input->numIndexes, input->indexes );

	renderState.multiTexture = qfalse;
	R_SelectTexture( 0 );
}

/*
===================
ProjectDlightTexture

Two-pass multi-dlight: pack affecting dlights into the UBO boneMatrices
region and issue up to two draw calls — one for non-additive lights
(SRC_DST_COLOR + DST_ONE) and one for additive lights (SRC_ONE + DST_ONE).
This replaces the old per-dlight-per-surface approach (N passes → 2 max).
===================
*/

static int PackDlightsForBlendMode( qboolean wantAdditive ) {
	int numPacked = 0;

	for ( int l = 0; l < backEnd.refdef.num_dlights && numPacked < 32; l++ ) {
		if ( !( tess.dlightBits & ( 1 << l ) ) ) {
			continue;
		}

		dlight_t *dl = &backEnd.refdef.dlights[l];

		// Filter by additive flag
		qboolean isAdditive = dl->additive ? qtrue : qfalse;
		if ( isAdditive != wantAdditive ) {
			continue;
		}

		// Pack into boneMatrices region: 2 vec4s per dlight
		int base = numPacked * 8;
		currentGPUParams.boneMatrices[base + 0] = dl->transformed[0];
		currentGPUParams.boneMatrices[base + 1] = dl->transformed[1];
		currentGPUParams.boneMatrices[base + 2] = dl->transformed[2];
		currentGPUParams.boneMatrices[base + 3] = dl->radius;
		currentGPUParams.boneMatrices[base + 4] = dl->color[0];
		currentGPUParams.boneMatrices[base + 5] = dl->color[1];
		currentGPUParams.boneMatrices[base + 6] = dl->color[2];
		currentGPUParams.boneMatrices[base + 7] = dl->additive ? 1.0f : 0.0f;
		numPacked++;
	}

	return numPacked;
}

static void ProjectDlightTexture( void ) {

	if ( !backEnd.refdef.num_dlights ) {
		return;
	}

	// Check if any dlights affect this surface at all
	if ( !tess.dlightBits ) {
		return;
	}

	// Bind dlight texture — fragment shader samples it per-light for XY attenuation
	R_BindImage( tr.dlightImage );
	renderState.multiTexture = qfalse;

	// texcoords[0] and colors already contain valid data from the main shader stage.
	// The GPU multi-dlight shader overrides both entirely, so no memset needed.

	// Sub-pass 1: Non-additive dlights (SRC_DST_COLOR + DST_ONE)
	// This is the standard dlight blending — brightens surfaces proportionally.
	// Sabers, blaster bolts, etc. use this mode.
	{
		GPU_ResetParams();
		currentGPUParams.gpuFlags |= GPU_FLAG_MULTI_DLIGHT_PASS;
		if ( r_dlightBacks->integer ) {
			currentGPUParams.gpuFlags |= GPU_FLAG_DLIGHT_BACKSIDES;
		}

		int numNonAdditive = PackDlightsForBlendMode( qfalse );
		if ( numNonAdditive > 0 ) {
			currentGPUParams.pad0 = (float)numNonAdditive;
			R_SetStateBits( GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL );
			R_DrawElements( tess.numIndexes, tess.indexes );
			backEnd.pc.c_totalIndexes += tess.numIndexes;
			backEnd.pc.c_dlightIndexes += tess.numIndexes * numNonAdditive;
		}
	}

	// Sub-pass 2: Additive dlights (SRC_ONE + DST_ONE)
	// Explosions, force effects, etc. use pure additive blending.
	{
		GPU_ResetParams();
		currentGPUParams.gpuFlags |= GPU_FLAG_MULTI_DLIGHT_PASS;
		if ( r_dlightBacks->integer ) {
			currentGPUParams.gpuFlags |= GPU_FLAG_DLIGHT_BACKSIDES;
		}

		int numAdditive = PackDlightsForBlendMode( qtrue );
		if ( numAdditive > 0 ) {
			currentGPUParams.pad0 = (float)numAdditive;
			R_SetStateBits( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL );
			R_DrawElements( tess.numIndexes, tess.indexes );
			backEnd.pc.c_totalIndexes += tess.numIndexes;
			backEnd.pc.c_dlightIndexes += tess.numIndexes * numAdditive;
		}
	}
}


/*
===================
RB_FogPass

Blends a fog texture on top of everything else
===================
*/
static void RB_FogPass( void ) {
	fog_t		*fog;

	fog = tr.world->fogs + tess.fogNum;

	// GPU: vertex shader computes fog texcoords and sets fog color
	GPU_ResetParams();
	currentGPUParams.gpuFlags |= GPU_FLAG_FOG_PASS;
	GPU_SetupFogParams();

	// Fill dummy vertex colors — GPU overrides with fogColor
	for ( int i = 0; i < tess.numVertexes; i++ ) {
		tess.svars.colorsui[i] = fog->colorInt;
	}

	// Fill dummy texcoords — GPU overrides with computed fog ST
	Com_Memset( tess.svars.texcoords[0], 0, tess.numVertexes * sizeof( tess.svars.texcoords[0][0] ) );

	R_BindImage( tr.fogImage );

	if ( tess.shader->fogPass == FP_EQUAL ) {
		R_SetStateBits( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHFUNC_EQUAL );
	} else {
		R_SetStateBits( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA );
	}

	renderState.multiTexture = qfalse;
	R_DrawElements( tess.numIndexes, tess.indexes );
}

/*
===============
ComputeColors
===============
*/
static void ComputeColors( shaderStage_t *pStage, int forceRGBGen )
{
	int			i;
	qboolean killGen = qfalse;

	if ( tess.shader != tr.projectionShadowShader && tess.shader != tr.shadowShader &&
			( backEnd.currentEntity->e.renderfx & (RF_DISINTEGRATE1|RF_DISINTEGRATE2)))
	{
		RB_CalcDisintegrateColors( (unsigned char *)tess.svars.colors );
		RB_CalcDisintegrateVertDeform();

		// We've done some custom alpha and color stuff, so we can skip the rest.  Let it do fog though
		killGen = qtrue;
	}

	//
	// rgbGen
	//
	if ( !forceRGBGen )
	{
		forceRGBGen = pStage->rgbGen;
	}

	if ( backEnd.currentEntity->e.renderfx & RF_VOLUMETRIC ) // does not work for rotated models, technically, this should also be a CGEN type, but that would entail adding new shader commands....which is too much work for one thing
	{
		int			i;
		float		*normal, dot;
		unsigned char *color;
		int			numVertexes;

		normal = tess.normal[0];
		color = tess.svars.colors[0];

		numVertexes = tess.numVertexes;

		for ( i = 0 ; i < numVertexes ; i++, normal += 4, color += 4)
		{
			dot = DotProduct( normal, backEnd.refdef.viewaxis[0] );

			dot *= dot * dot * dot;

			if ( dot < 0.2f ) // so low, so just clamp it
			{
				dot = 0.0f;
			}

			color[0] = color[1] = color[2] = color[3] = Q_ftol(backEnd.currentEntity->e.shaderRGBA[0] * (1 - dot));

		}

		killGen = qtrue;
	}

	if (killGen)
	{
		goto avoidGen;
	}

	//
	// rgbGen
	//
	switch ( forceRGBGen )
	{
		case CGEN_IDENTITY:
			Com_Memset( tess.svars.colors, 0xff, tess.numVertexes * 4 );
			break;
		default:
		case CGEN_IDENTITY_LIGHTING:
			Com_Memset( tess.svars.colors, tr.identityLightByte, tess.numVertexes * 4 );
			break;
		case CGEN_LIGHTING_DIFFUSE:
			// GPU: offload diffuse vertex lighting to vertex shader
			currentGPUParams.gpuFlags |= GPU_FLAG_DIFFUSE_LIGHTING;
			{
				trRefEntity_t *ent = backEnd.currentEntity;
				VectorCopy( ent->ambientLight, currentGPUParams.ambientLight );
				VectorCopy( ent->directedLight, currentGPUParams.directedLight );
				VectorCopy( ent->lightDir, currentGPUParams.entityLightDir );
			}
			// Fill white — GPU overrides RGB, alphaGen may set alpha below
			Com_Memset( tess.svars.colors, 0xff, tess.numVertexes * 4 );
			break;
		case CGEN_EXACT_VERTEX:
			Com_Memcpy( tess.svars.colors, tess.vertexColors, tess.numVertexes * sizeof( tess.vertexColors[0] ) );
			break;
		case CGEN_CONST:
			for ( i = 0; i < tess.numVertexes; i++ ) {
				tess.svars.colorsui[i] = pStage->constantColorui;
			}
			break;
		case CGEN_VERTEX:
			if ( tr.identityLight == 1 )
			{
				Com_Memcpy( tess.svars.colors, tess.vertexColors, tess.numVertexes * sizeof( tess.vertexColors[0] ) );
			}
			else
			{
				for ( i = 0; i < tess.numVertexes; i++ )
				{
					tess.svars.colors[i][0] = tess.vertexColors[i][0] * tr.identityLight;
					tess.svars.colors[i][1] = tess.vertexColors[i][1] * tr.identityLight;
					tess.svars.colors[i][2] = tess.vertexColors[i][2] * tr.identityLight;
					tess.svars.colors[i][3] = tess.vertexColors[i][3];
				}
			}
			break;
		case CGEN_ONE_MINUS_VERTEX:
			if ( tr.identityLight == 1 )
			{
				for ( i = 0; i < tess.numVertexes; i++ )
				{
					tess.svars.colors[i][0] = 255 - tess.vertexColors[i][0];
					tess.svars.colors[i][1] = 255 - tess.vertexColors[i][1];
					tess.svars.colors[i][2] = 255 - tess.vertexColors[i][2];
				}
			}
			else
			{
				for ( i = 0; i < tess.numVertexes; i++ )
				{
					tess.svars.colors[i][0] = ( 255 - tess.vertexColors[i][0] ) * tr.identityLight;
					tess.svars.colors[i][1] = ( 255 - tess.vertexColors[i][1] ) * tr.identityLight;
					tess.svars.colors[i][2] = ( 255 - tess.vertexColors[i][2] ) * tr.identityLight;
				}
			}
			break;
		case CGEN_FOG:
			{
				fog_t		*fog;

				fog = tr.world->fogs + tess.fogNum;

				for ( i = 0; i < tess.numVertexes; i++ ) {
					tess.svars.colorsui[i] = fog->colorInt;
				}
			}
			break;
		case CGEN_WAVEFORM:
			RB_CalcWaveColor( &pStage->rgbWave, tess.svars.colorsui );
			break;
		case CGEN_ENTITY:
			RB_CalcColorFromEntity( tess.svars.colorsui );
			break;
		case CGEN_ONE_MINUS_ENTITY:
			RB_CalcColorFromOneMinusEntity( tess.svars.colorsui );
			break;
		case CGEN_LIGHTMAP0:
			memset( tess.svars.colors, 0xff, tess.numVertexes * 4 );
			break;
		case CGEN_LIGHTMAP1:
		case CGEN_LIGHTMAP2:
		case CGEN_LIGHTMAP3:
			uint32_t tempColor;

			memcpy(&tempColor, styleColors[pStage->lightmapStyle], 4);

			for ( i = 0; i < tess.numVertexes; i++ )
			{
				tess.svars.colorsui[i] = tempColor;
			}
			break;
	}

	//
	// alphaGen
	//
	switch ( pStage->alphaGen )
	{
	case AGEN_SKIP:
		break;
	case AGEN_IDENTITY:
		if ( forceRGBGen != CGEN_IDENTITY ) {
			if ( ( forceRGBGen == CGEN_VERTEX && tr.identityLight != 1 ) ||
				 forceRGBGen != CGEN_VERTEX ) {
				for ( i = 0; i < tess.numVertexes; i++ ) {
					tess.svars.colors[i][3] = 0xff;
				}
			}
		}
		break;
	case AGEN_CONST:
		if ( forceRGBGen != CGEN_CONST ) {
			for ( i = 0; i < tess.numVertexes; i++ ) {
				tess.svars.colors[i][3] = pStage->constantColor[3];
			}
		}
		break;
	case AGEN_WAVEFORM:
		RB_CalcWaveAlpha( &pStage->alphaWave, ( unsigned char * ) tess.svars.colors );
		break;
	case AGEN_LIGHTING_SPECULAR:
		// GPU: offload specular alpha to vertex shader
		currentGPUParams.gpuFlags |= GPU_FLAG_SPECULAR_ALPHA;
		{
			qboolean useEntDir = (qboolean)(backEnd.currentEntity &&
				(backEnd.currentEntity->e.hModel || backEnd.currentEntity->e.ghoul2));
			if ( useEntDir ) {
				VectorCopy( backEnd.currentEntity->lightDir, currentGPUParams.entityLightDir );
				currentGPUParams.specLightOrigin[3] = 1.0f; // flag: use entity lightDir
			} else {
				VectorCopy( lightOrigin, currentGPUParams.specLightOrigin );
				currentGPUParams.specLightOrigin[3] = 0.0f; // flag: use lightOrigin per-vertex
			}
		}
		break;
	case AGEN_ENTITY:
		RB_CalcAlphaFromEntity( ( unsigned char * ) tess.svars.colors );
		break;
	case AGEN_ONE_MINUS_ENTITY:
		RB_CalcAlphaFromOneMinusEntity( ( unsigned char * ) tess.svars.colors );
		break;
    case AGEN_VERTEX:
		if ( forceRGBGen != CGEN_VERTEX ) {
			for ( i = 0; i < tess.numVertexes; i++ ) {
				tess.svars.colors[i][3] = tess.vertexColors[i][3];
			}
		}
        break;
    case AGEN_ONE_MINUS_VERTEX:
        for ( i = 0; i < tess.numVertexes; i++ )
        {
			tess.svars.colors[i][3] = 255 - tess.vertexColors[i][3];
        }
        break;
	case AGEN_PORTAL:
		{
			unsigned char alpha;

			for ( i = 0; i < tess.numVertexes; i++ )
			{
				float len;
				vec3_t v;

				VectorSubtract( tess.xyz[i], backEnd.viewParms.ori.origin, v );
				len = VectorLength( v );

				len /= tess.shader->portalRange;

				if ( len < 0 )
				{
					alpha = 0;
				}
				else if ( len > 1 )
				{
					alpha = 0xff;
				}
				else
				{
					alpha = len * 0xff;
				}

				tess.svars.colors[i][3] = alpha;
			}
		}
		break;
	case AGEN_BLEND:
		if ( forceRGBGen != CGEN_VERTEX )
		{
			for ( i = 0; i < tess.numVertexes; i++ )
			{
				//colors[i][3] = tess.vertexAlphas[i][pStage->index];	// only used on SOF2, needs implementing if you want it
			}
		}
		break;
	}
avoidGen:
	//
	// fog adjustment for colors to fade out as fog increases
	// GPU: set flags and let the vertex shader modulate
	//
	if ( tess.fogNum )
	{
		switch ( pStage->adjustColorsForFog )
		{
		case ACFF_MODULATE_RGB:
			GPU_SetupFogParams();
			currentGPUParams.gpuFlags |= GPU_FLAG_FOG_MODULATE_RGB;
			break;
		case ACFF_MODULATE_ALPHA:
			GPU_SetupFogParams();
			currentGPUParams.gpuFlags |= GPU_FLAG_FOG_MODULATE_ALPHA;
			break;
		case ACFF_MODULATE_RGBA:
			GPU_SetupFogParams();
			currentGPUParams.gpuFlags |= GPU_FLAG_FOG_MODULATE_RGB | GPU_FLAG_FOG_MODULATE_ALPHA;
			break;
		case ACFF_NONE:
			break;
		}
	}
}

/*
===============
ComputeTexCoords
===============
*/
static void ComputeTexCoords( shaderStage_t *pStage ) {
	int		i;
	int		b;
    float	*texcoords;

	for ( b = 0; b < NUM_TEXTURE_BUNDLES; b++ ) {
		int tm;

		// skip unused texture bundles (most stages are single-texture)
		if ( b > 0 && !pStage->bundle[b].image[0] )
			break;

        texcoords = (float *)tess.svars.texcoords[b];
		//
		// generate the texture coordinates
		//
		switch ( pStage->bundle[b].tcGen )
		{
		case TCGEN_IDENTITY:
			Com_Memset( tess.svars.texcoords[b], 0, sizeof( float ) * 2 * tess.numVertexes );
			break;
		case TCGEN_TEXTURE:
			Com_Memcpy( texcoords, tess.texCoords[0], tess.numVertexes * sizeof( tess.texCoords[0][0] ) );
			break;
		case TCGEN_LIGHTMAP:
			Com_Memcpy( texcoords, tess.texCoords[1], tess.numVertexes * sizeof( tess.texCoords[0][0] ) );
			break;
		case TCGEN_LIGHTMAP1:
			Com_Memcpy( texcoords, tess.texCoords[2], tess.numVertexes * sizeof( tess.texCoords[0][0] ) );
			break;
		case TCGEN_LIGHTMAP2:
			Com_Memcpy( texcoords, tess.texCoords[3], tess.numVertexes * sizeof( tess.texCoords[0][0] ) );
			break;
		case TCGEN_LIGHTMAP3:
			Com_Memcpy( texcoords, tess.texCoords[4], tess.numVertexes * sizeof( tess.texCoords[0][0] ) );
			break;
		case TCGEN_VECTOR:
			for ( i = 0 ; i < tess.numVertexes ; i++ ) {
				tess.svars.texcoords[b][i][0] = DotProduct( tess.xyz[i], pStage->bundle[b].tcGenVectors[0] );
				tess.svars.texcoords[b][i][1] = DotProduct( tess.xyz[i], pStage->bundle[b].tcGenVectors[1] );
			}
			break;
		case TCGEN_FOG:
			RB_CalcFogTexCoords( ( float * ) tess.svars.texcoords[b] );
			break;
		case TCGEN_ENVIRONMENT_MAPPED:
			if ( r_environmentMapping->integer ) {
				// GPU: offload environment map TC to vertex shader
				currentGPUParams.gpuFlags |= GPU_FLAG_ENVMAP_TC;
				VectorCopy( backEnd.ori.viewOrigin, currentGPUParams.viewOrigin );
				// Still need to fill something for svars — GPU overrides fragTexCoord0
				Com_Memset( texcoords, 0, sizeof( float ) * 2 * tess.numVertexes );
			} else {
				Com_Memset( texcoords, 0, sizeof( float ) * 2 * tess.numVertexes );
			}
			break;
		case TCGEN_BAD:
			return;
		}

		//
		// alter texture coordinates
		//
		for ( tm = 0; tm < pStage->bundle[b].numTexMods ; tm++ ) {
			switch ( pStage->bundle[b].texMods[tm].type )
			{
			case TMOD_NONE:
				tm = TR_MAX_TEXMODS;		// break out of for loop
				break;

			case TMOD_TURBULENT:
				RB_CalcTurbulentTexCoords( &pStage->bundle[b].texMods[tm].wave,
						                 ( float * ) tess.svars.texcoords[b] );
				break;

			case TMOD_ENTITY_TRANSLATE:
				RB_CalcScrollTexCoords( backEnd.currentEntity->e.shaderTexCoord,
									 ( float * ) tess.svars.texcoords[b] );
				break;

			case TMOD_SCROLL:
				RB_CalcScrollTexCoords( pStage->bundle[b].texMods[tm].scroll,
										 ( float * ) tess.svars.texcoords[b] );
				break;

			case TMOD_SCALE:
				RB_CalcScaleTexCoords( pStage->bundle[b].texMods[tm].scale,
									 ( float * ) tess.svars.texcoords[b] );
				break;

			case TMOD_STRETCH:
				RB_CalcStretchTexCoords( &pStage->bundle[b].texMods[tm].wave,
						               ( float * ) tess.svars.texcoords[b] );
				break;

			case TMOD_TRANSFORM:
				RB_CalcTransformTexCoords( &pStage->bundle[b].texMods[tm],
						                 ( float * ) tess.svars.texcoords[b] );
				break;

			case TMOD_ROTATE:
				RB_CalcRotateTexCoords( pStage->bundle[b].texMods[tm].rotateSpeed,
										( float * ) tess.svars.texcoords[b] );
				break;

			default:
				ri.Error( ERR_DROP, "ERROR: unknown texmod '%d' in shader '%s'", pStage->bundle[b].texMods[tm].type, tess.shader->name );
				break;
			}
		}
	}
}

void ForceAlpha(unsigned char *dstColors, int TR_ForceEntAlpha)
{
	int	i;

	dstColors += 3;

	for ( i = 0; i < tess.numVertexes; i++, dstColors += 4 )
	{
		*dstColors = TR_ForceEntAlpha;
	}
}

/*
** RB_IterateStagesGeneric
*/
static void RB_IterateStagesGeneric( shaderCommands_t *input )
{
	int stage;

	for ( stage = 0; stage < MAX_SHADER_STAGES; stage++ )
	{
		shaderStage_t *pStage = tess.xstages[stage];
		int forceRGBGen = 0;
		int stateBits = 0;

		if ( !pStage )
		{
			break;
		}

		// Reject this stage if it's not a glow stage but we are doing a glow pass.
		if ( g_bRenderGlowingObjects && !pStage->glow )
		{
			continue;
		}

		if ( stage && r_lightmap->integer && !( pStage->bundle[0].isLightmap || pStage->bundle[1].isLightmap || pStage->bundle[0].vertexLightmap ) )
		{
			break;
		}

		stateBits = pStage->stateBits;

		if ( backEnd.currentEntity )
		{
			if ( backEnd.currentEntity->e.renderfx & RF_DISINTEGRATE1 )
			{
				// we want to be able to rip a hole in the thing being disintegrated, and by doing the depth-testing it avoids some kinds of artefacts, but will probably introduce others?
				stateBits = GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHMASK_TRUE | GLS_ATEST_GE_C0;
			}

			if ( backEnd.currentEntity->e.renderfx & RF_RGB_TINT )
			{//want to use RGBGen from ent
				forceRGBGen = CGEN_ENTITY;
			}
		}

		if (pStage->ss.surfaceSpriteType)
		{
			// We check for surfacesprites AFTER drawing everything else
			continue;
		}

		// Reset GPU params for this stage — flags accumulate in Compute*
		GPU_ResetParams();

		ComputeColors( pStage, forceRGBGen );
		ComputeTexCoords( pStage );

		//
		// do multitexture
		//
		if ( pStage->bundle[1].image[0] != 0 )
		{
			DrawMultitextured( input, stage );
		}
		else
		{
			//
			// set state
			//
			if ( pStage->bundle[0].vertexLightmap && ( r_vertexLight->integer && !r_uiFullScreen->integer ) && r_lightmap->integer )
			{
				R_BindImage( tr.whiteImage );
			}
			else
				R_BindAnimatedImage( &pStage->bundle[0] );

			if (backEnd.currentEntity && (backEnd.currentEntity->e.renderfx & RF_FORCE_ENT_ALPHA))
			{
				ForceAlpha((unsigned char *) tess.svars.colors, backEnd.currentEntity->e.shaderRGBA[3]);
				R_SetStateBits(GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA);
			}
			else
			{
				R_SetStateBits( stateBits );
			}

			//
			// draw
			//
			renderState.multiTexture = qfalse;
			R_DrawElements( input->numIndexes, input->indexes );
		}
	}
}


/*
** RB_StageIteratorGeneric
*/
void RB_StageIteratorGeneric( void )
{
	shaderCommands_t *input;
	int stage;

	input = &tess;

	RB_DeformTessGeometry();
	RB_InitTessWComponents();

	//
	// log this call
	//
	if ( r_logFile->integer )
	{
		// don't just call LogComment, or we will get
		// a call to va() every frame!
		GLimp_LogComment( va("--- RB_StageIteratorGeneric( %s ) ---\n", tess.shader->name) );
	}

	//
	// Cache geometry base (pos/normal/idx) once for all stages
	// Subsequent R_DrawElements calls reuse these offsets.
	//
	VK_CacheTessGeometry();

	//
	// set face culling appropriately
	// Vulkan: culling is handled by the pipeline state object
	//
	R_SetCullMode( input->shader->cullType );

	// set polygon offset if necessary
	// Vulkan: polygon offset is handled by the pipeline + dynamic state
	if ( input->shader->polygonOffset )
	{
		renderState.polygonOffset = qtrue;
	}
	else
	{
		renderState.polygonOffset = qfalse;
	}

	//
	// Vulkan: no need to manage client state arrays - data is uploaded
	// directly in R_DrawElements via VK_DrawIndexed
	//
	// Optimization: we don't need to do this anymore
	// setArraysOnce = qfalse;

	//
	// call shader function
	//
	RB_IterateStagesGeneric( input );

	//
	// now do any dynamic lighting needed
	// Skip legacy dlight projection when RT glow reflections handle it
	//
	if ( tess.dlightBits && tess.shader->sort <= SS_OPAQUE
		&& !(tess.shader->surfaceFlags & (SURF_NODLIGHT | SURF_SKY) )
		&& !(r_DynamicGlowReflections && r_DynamicGlowReflections->integer) ) {
		ProjectDlightTexture();
	}

	//
	// now do fog
	//
	if ( tess.fogNum && tess.shader->fogPass ) {
		RB_FogPass();
	}

	// Now check for surfacesprites.
	if (r_surfaceSprites->integer)
	{
		for ( stage = 1; stage < MAX_SHADER_STAGES; stage++ )
		{
			if (!tess.xstages[stage])
			{
				break;
			}
			if (tess.xstages[stage]->ss.surfaceSpriteType)
			{	// Draw the surfacesprite
				RB_DrawSurfaceSprites(tess.xstages[stage], input);
			}
		}
	}
}


/*
** RB_StageIteratorVertexLitTexture
*/
void RB_StageIteratorVertexLitTexture( void )
{
	shaderCommands_t *input;
	shader_t		*shader;
	int stage;

	input = &tess;

	shader = input->shader;
	RB_InitTessWComponents();

	//
	// Cache geometry base once (pos/normal/idx)
	//
	VK_CacheTessGeometry();

	//
	// GPU: compute diffuse lighting in vertex shader
	//
	GPU_ResetParams();
	currentGPUParams.gpuFlags |= GPU_FLAG_DIFFUSE_LIGHTING;
	{
		trRefEntity_t *ent = backEnd.currentEntity;
		VectorCopy( ent->ambientLight, currentGPUParams.ambientLight );
		VectorCopy( ent->directedLight, currentGPUParams.directedLight );
		VectorCopy( ent->lightDir, currentGPUParams.entityLightDir );
	}
	// Fill white colors — GPU overrides RGB
	Com_Memset( tess.svars.colors, 0xff, tess.numVertexes * 4 );

	//
	// log this call
	//
	if ( r_logFile->integer )
	{
		// don't just call LogComment, or we will get
		// a call to va() every frame!
		GLimp_LogComment( va("--- RB_StageIteratorVertexLitTexturedUnfogged( %s ) ---\n", tess.shader->name) );
	}

	//
	// set face culling appropriately
	//
	R_SetCullMode( input->shader->cullType );

	//
	// Vulkan: texcoords come from tess.texCoords, copy to svars for drawing
	//
	Com_Memcpy( tess.svars.texcoords[0], input->texCoords[0][0], input->numVertexes * sizeof( tess.svars.texcoords[0][0] ) );

	//
	// call special shade routine
	//
	R_BindAnimatedImage( &tess.xstages[0]->bundle[0] );
	R_SetStateBits( tess.xstages[0]->stateBits );
	renderState.multiTexture = qfalse;
	R_DrawElements( input->numIndexes, input->indexes );

	//
	// now do any dynamic lighting needed
	// Skip legacy dlight projection when RT glow reflections handle it
	//
	if ( tess.dlightBits && tess.shader->sort <= SS_OPAQUE
		&& !(r_DynamicGlowReflections && r_DynamicGlowReflections->integer) ) {
		ProjectDlightTexture();
	}

	//
	// now do fog
	//
	if ( tess.fogNum && tess.shader->fogPass ) {
		RB_FogPass();
	}

	// Now check for surfacesprites.
	if (r_surfaceSprites->integer)
	{
		for ( stage = 1; stage < MAX_SHADER_STAGES; stage++ )
		{
			if (!tess.xstages[stage])
			{
				break;
			}
			if (tess.xstages[stage]->ss.surfaceSpriteType)
			{	// Draw the surfacesprite
				RB_DrawSurfaceSprites(tess.xstages[stage], input);
			}
		}
	}
}

//define	REPLACE_MODE

void RB_StageIteratorLightmappedMultitexture( void ) {
	shaderCommands_t *input;
	int stage;

	input = &tess;
	RB_InitTessWComponents();

	//
	// Cache geometry base once (pos/normal/idx)
	//
	VK_CacheTessGeometry();

	//
	// log this call
	//
	if ( r_logFile->integer ) {
		// don't just call LogComment, or we will get
		// a call to va() every frame!
		GLimp_LogComment( va("--- RB_StageIteratorLightmappedMultitexture( %s ) ---\n", tess.shader->name) );
	}

	//
	// set face culling appropriately
	//
	R_SetCullMode( input->shader->cullType );

	//
	// set color and state
	//
	R_SetStateBits( GLS_DEFAULT );
	Com_Memset( tess.svars.colors, 0xff, tess.numVertexes * 4 );

	//
	// Vulkan: copy texcoords to svars for drawing
	//
	Com_Memcpy( tess.svars.texcoords[0], input->texCoords[0][0], input->numVertexes * sizeof( tess.svars.texcoords[0][0] ) );
	Com_Memcpy( tess.svars.texcoords[1], tess.texCoords[1][0], input->numVertexes * sizeof( tess.svars.texcoords[1][0] ) );

	//
	// select base stage
	//
	R_SelectTexture( 0 );
	R_BindAnimatedImage( &tess.xstages[0]->bundle[0] );

	//
	// configure second stage
	//
	R_SelectTexture( 1 );
	R_BindAnimatedImage( &tess.xstages[0]->bundle[1] );

	// GPU: no special features for lightmapped surfaces, just pass-through
	GPU_ResetParams();

	renderState.multiTexture = qtrue;
	R_DrawElements( input->numIndexes, input->indexes );
	renderState.multiTexture = qfalse;

	//
	// select TEXTURE0
	//
	R_SelectTexture( 0 );

	//
	// now do any dynamic lighting needed
	// Skip legacy dlight projection when RT glow reflections handle it
	//
	if ( tess.dlightBits && tess.shader->sort <= SS_OPAQUE
		&& !(r_DynamicGlowReflections && r_DynamicGlowReflections->integer) ) {
		ProjectDlightTexture();
	}

	//
	// now do fog
	//
	if ( tess.fogNum && tess.shader->fogPass ) {
		RB_FogPass();
	}

	// Now check for surfacesprites.
	if (r_surfaceSprites->integer)
	{
		for ( stage = 1; stage < MAX_SHADER_STAGES; stage++ )
		{
			if (!tess.xstages[stage])
			{
				break;
			}
			if (tess.xstages[stage]->ss.surfaceSpriteType)
			{	// Draw the surfacesprite
				RB_DrawSurfaceSprites(tess.xstages[stage], input);
			}
		}
	}
}


/*
** RB_StageIteratorVBO
**
** Draws pre-merged static world geometry directly from GPU-resident buffers.
** This is the fast path for VBO meshes — zero CPU vertex data upload.
*/
void RB_StageIteratorVBO( void ) {
	srfVBOMesh_t *vboMesh = tess.vboMesh;

	if ( !vboMesh || !vk.staticBuffersValid ) {
		// Fallback to lightmapped multitexture path
		RB_StageIteratorLightmappedMultitexture();
		return;
	}

	shaderCommands_t *input = &tess;

	//
	// log this call
	//
	if ( r_logFile->integer ) {
		GLimp_LogComment( va("--- RB_StageIteratorVBO( %s ) ---\n", tess.shader->name) );
	}

	//
	// set face culling appropriately
	//
	R_SetCullMode( input->shader->cullType );

	//
	// set color and state
	//
	R_SetStateBits( GLS_DEFAULT );

	//
	// select base stage
	//
	R_SelectTexture( 0 );
	R_BindAnimatedImage( &tess.xstages[0]->bundle[0] );

	//
	// configure second stage (lightmap)
	//
	R_SelectTexture( 1 );
	R_BindAnimatedImage( &tess.xstages[0]->bundle[1] );

	// GPU: no special features for lightmapped surfaces
	GPU_ResetParams();

	// Bind pipeline
	renderState.multiTexture = qtrue;

	VK_BindPipeline( renderState.stateBits, renderState.faceCulling, renderState.multiTexture, renderState.polygonOffset );

	// Push fragment constants
	{
		float fragConstants[2] = { 0.0f, 0.0f }; // texEnvMode=modulate, alphaTest=none
		if ( tess.shader->multitextureEnv == TEXENV_ADD ) {
			fragConstants[0] = 3.0f;
		}
		VkCommandBuffer cmd = vk.frames[vk.currentFrame].commandBuffer;
		vkCmdPushConstants( cmd, vk.pipelineLayout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			64, sizeof(float) * 2, fragConstants );
	}

	if ( !VK_UpdateGPUParams( &currentGPUParams ) ) return;

	// Draw entirely from static buffers — zero vertex upload!
	VK_DrawFromStaticBuffers(
		vboMesh->firstVertex, vboMesh->numVertices,
		vboMesh->firstIndex, vboMesh->numIndexes,
		NULL, NULL, NULL, qfalse
	);

	renderState.multiTexture = qfalse;
	R_SelectTexture( 0 );

	//
	// now do any dynamic lighting needed
	//
	if ( tess.dlightBits && tess.shader->sort <= SS_OPAQUE
		&& !(tess.shader->surfaceFlags & (SURF_NODLIGHT | SURF_SKY) ) ) {

		if ( backEnd.refdef.num_dlights && tess.dlightBits ) {
			R_BindImage( tr.dlightImage );
			renderState.multiTexture = qfalse;

			// Non-additive dlights from static buffer
			{
				GPU_ResetParams();
				currentGPUParams.gpuFlags |= GPU_FLAG_MULTI_DLIGHT_PASS;
				if ( r_dlightBacks->integer ) {
					currentGPUParams.gpuFlags |= GPU_FLAG_DLIGHT_BACKSIDES;
				}
				int numNonAdditive = PackDlightsForBlendMode( qfalse );
				if ( numNonAdditive > 0 ) {
					currentGPUParams.pad0 = (float)numNonAdditive;
					R_SetStateBits( GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL );
					VK_BindPipeline( renderState.stateBits, renderState.faceCulling, renderState.multiTexture, renderState.polygonOffset );
					{
						float fragConstants[2] = { 0.0f, 0.0f };
						VkCommandBuffer cmd = vk.frames[vk.currentFrame].commandBuffer;
						vkCmdPushConstants( cmd, vk.pipelineLayout,
							VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
							64, sizeof(float) * 2, fragConstants );
					}
					if ( !VK_UpdateGPUParams( &currentGPUParams ) ) return;
					// Colors/TCs computed by GPU dlight shader, but we need dummy data
					// Use static buffer for pos/norm, zero region for TC, white for color
					VK_DrawFromStaticBuffers(
						vboMesh->firstVertex, vboMesh->numVertices,
						vboMesh->firstIndex, vboMesh->numIndexes,
						NULL, NULL, NULL, qfalse
					);
					backEnd.pc.c_totalIndexes += vboMesh->numIndexes;
					backEnd.pc.c_dlightIndexes += vboMesh->numIndexes * numNonAdditive;
				}
			}

			// Additive dlights from static buffer
			{
				GPU_ResetParams();
				currentGPUParams.gpuFlags |= GPU_FLAG_MULTI_DLIGHT_PASS;
				if ( r_dlightBacks->integer ) {
					currentGPUParams.gpuFlags |= GPU_FLAG_DLIGHT_BACKSIDES;
				}
				int numAdditive = PackDlightsForBlendMode( qtrue );
				if ( numAdditive > 0 ) {
					currentGPUParams.pad0 = (float)numAdditive;
					R_SetStateBits( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL );
					VK_BindPipeline( renderState.stateBits, renderState.faceCulling, renderState.multiTexture, renderState.polygonOffset );
					{
						float fragConstants[2] = { 0.0f, 0.0f };
						VkCommandBuffer cmd = vk.frames[vk.currentFrame].commandBuffer;
						vkCmdPushConstants( cmd, vk.pipelineLayout,
							VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
							64, sizeof(float) * 2, fragConstants );
					}
					if ( !VK_UpdateGPUParams( &currentGPUParams ) ) return;
					VK_DrawFromStaticBuffers(
						vboMesh->firstVertex, vboMesh->numVertices,
						vboMesh->firstIndex, vboMesh->numIndexes,
						NULL, NULL, NULL, qfalse
					);
					backEnd.pc.c_totalIndexes += vboMesh->numIndexes;
					backEnd.pc.c_dlightIndexes += vboMesh->numIndexes * numAdditive;
				}
			}
		}
	}

	//
	// now do fog
	//
	if ( tess.fogNum && tess.shader->fogPass ) {
		// fog_t *fog = tr.world->fogs + tess.fogNum; // unused

		GPU_ResetParams();
		currentGPUParams.gpuFlags |= GPU_FLAG_FOG_PASS;
		GPU_SetupFogParams();

		R_BindImage( tr.fogImage );
		if ( tess.shader->fogPass == FP_EQUAL ) {
			R_SetStateBits( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHFUNC_EQUAL );
		} else {
			R_SetStateBits( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA );
		}
		renderState.multiTexture = qfalse;

		VK_BindPipeline( renderState.stateBits, renderState.faceCulling, renderState.multiTexture, renderState.polygonOffset );
		{
			float fragConstants[2] = { 0.0f, 0.0f };
			VkCommandBuffer cmd = vk.frames[vk.currentFrame].commandBuffer;
			vkCmdPushConstants( cmd, vk.pipelineLayout,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				64, sizeof(float) * 2, fragConstants );
		}
		if ( !VK_UpdateGPUParams( &currentGPUParams ) ) return;

		// Fog pass from static buffer — fog TC and colors computed by GPU
		VK_DrawFromStaticBuffers(
			vboMesh->firstVertex, vboMesh->numVertices,
			vboMesh->firstIndex, vboMesh->numIndexes,
			NULL, NULL, NULL, qfalse
		);
	}
}


/*
** RB_EndSurface
*/
void RB_EndSurface( void ) {
	shaderCommands_t *input;

	input = &tess;

	if (input->numIndexes == 0) {
		return;
	}

	// VBO meshes don't populate tess arrays, skip sentinel checks
	if (input->vboMesh == NULL) {
		if (input->indexes[SHADER_MAX_INDEXES-1] != 0) {
			ri.Error (ERR_DROP, "RB_EndSurface() - SHADER_MAX_INDEXES hit");
		}
		if (input->xyz[SHADER_MAX_VERTEXES-1][0] != 0) {
			ri.Error (ERR_DROP, "RB_EndSurface() - SHADER_MAX_VERTEXES hit");
		}
	}

	if ( tess.shader == tr.shadowShader ) {
		RB_ShadowTessEnd();
		return;
	}

	// for debugging of sort order issues, stop rendering after a given sort value
	if ( r_debugSort->integer && r_debugSort->integer < tess.shader->sort ) {
		return;
	}

	//
	// update performance counters
	//
	backEnd.pc.c_shaders++;
	backEnd.pc.c_vertexes += tess.numVertexes;
	backEnd.pc.c_indexes += tess.numIndexes;
	backEnd.pc.c_totalIndexes += tess.numIndexes * tess.numPasses;
	if (tess.fogNum && tess.shader->fogPass > FP_NONE && tess.shader->fogPass < FP_GLFOG)// && r_drawfog->value)
	{
		backEnd.pc.c_totalIndexes += tess.numIndexes;
	}

	//
	// call off to shader specific tess end function
	//
	tess.currentStageIteratorFunc();

	//
	// draw debugging stuff
	//
	if ( r_showtris->integer && com_developer->integer ) {
		DrawTris (input);
	}
	if ( r_shownormals->integer && com_developer->integer ) {
		DrawNormals (input);
	}
	// clear shader so we can tell we don't have any unclosed surfaces
	tess.numIndexes = 0;

	GLimp_LogComment( "----------\n" );
}

