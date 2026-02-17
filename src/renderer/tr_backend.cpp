#include "tr_local.h"

#ifndef DEDICATED
#include "vk_local.h"

#if !defined __TR_WORLDEFFECTS_H
	#include "tr_WorldEffects.h"
#endif
#endif

backEndData_t	*backEndData;
backEndState_t	backEnd;

// Whether we are currently rendering only glowing objects or not.
bool g_bRenderGlowingObjects = false;

// Whether the current hardware supports dynamic glows/flares.
bool g_bDynamicGlowSupported = false;

#ifndef DEDICATED

static void RB_DrawGlowOverlay();
static void RB_BlurGlowTexture();

/*
** R_BindImage
*/
void R_BindImage( image_t *image ) {
	if ( !image ) {
		ri.Printf( PRINT_WARNING, "R_BindImage: NULL image\n" );
		image = tr.defaultImage;
	}

	if ( r_nobind->integer && tr.dlightImage ) {		// performance evaluation option
		image = tr.dlightImage;
	}

	image->frameUsed = tr.frameCount;
	VK_BindImage( renderState.currenttmu, image );
}

/*
** R_SelectTexture
*/
void R_SelectTexture( int unit )
{
	if ( renderState.currenttmu == unit )
	{
		return;
	}

	if ( unit < 0 || unit > 1 ) {
		ri.Error( ERR_DROP, "R_SelectTexture: unit = %i", unit );
	}

	renderState.currenttmu = unit;
	// In Vulkan, texture unit selection is handled via descriptor set binding (set index)
}


/*
** R_BindMultitexture
*/
void R_BindMultitexture( image_t *image0, int env0, image_t *image1, int env1 ) {
	if ( r_nobind->integer && tr.dlightImage ) {
		image0 = image1 = tr.dlightImage;
	}

	image0->frameUsed = tr.frameCount;
	image1->frameUsed = tr.frameCount;

	VK_BindImage( 0, image0 );
	VK_BindImage( 1, image1 );

	renderState.currenttmu = 1;
}


/*
** R_SetCullMode
*/
void R_SetCullMode( int cullType ) {
	if ( renderState.faceCulling == cullType ) {
		return;
	}

	renderState.faceCulling = cullType;
	// In Vulkan, cull mode is part of the pipeline state and is handled
	// when binding the pipeline via VK_BindPipeline
}

/*
** R_SetTexEnv
*/
void R_SetTexEnv( int env )
{
	if ( env == renderState.texEnv[renderState.currenttmu] )
	{
		return;
	}

	renderState.texEnv[renderState.currenttmu] = env;
	// In Vulkan, texture environment modes are handled in the fragment shader
	// via push constants (texEnvMode field)
}

/*
** R_SetStateBits
**
** This routine is responsible for setting the most commonly changed state
** in Q3. In Vulkan, most of this is baked into the pipeline object.
** We just record the state bits here; actual pipeline binding happens at draw time.
*/
void R_SetStateBits( unsigned int stateBits )
{
	renderState.stateBits = stateBits;
	// In Vulkan, state changes are handled by binding different pipelines.
	// The stateBits are used when constructing the pipeline key in VK_BindPipeline.
}



/*
================
RB_Hyperspace

A player has predicted a teleport, but hasn't arrived yet
================
*/
static void RB_Hyperspace( void ) {
	float		c;

	if ( !backEnd.isHyperspace ) {
		// do initialization shit
	}

	c = ( backEnd.refdef.time & 255 ) / 255.0f;
	VK_Clear( 0x01, c, c, c, 1.0f );

	backEnd.isHyperspace = qtrue;
}


void SetViewportAndScissor( void ) {
	// Compute MVP = projection * modelview
	// In Vulkan we use push constants for the MVP matrix instead of GL matrix stack

	VK_SetViewport( (float)backEnd.viewParms.viewportX, (float)backEnd.viewParms.viewportY,
		(float)backEnd.viewParms.viewportWidth, (float)backEnd.viewParms.viewportHeight, 0.0f, 1.0f );
	VK_SetScissor( backEnd.viewParms.viewportX, backEnd.viewParms.viewportY,
		backEnd.viewParms.viewportWidth, backEnd.viewParms.viewportHeight );
}

/*
=================
RB_BeginDrawingView

Any mirrored or portaled views have already been drawn, so prepare
to actually render the visible surfaces for this view
=================
*/
void RB_BeginDrawingView (void) {
	int clearBits = 0;

	// sync with gl if needed
	if ( r_finish->integer == 1 && !renderState.finishCalled ) {
		// Vulkan doesn't have glFinish, synchronization is handled via fences
		renderState.finishCalled = qtrue;
	}
	if ( r_finish->integer == 0 ) {
		renderState.finishCalled = qtrue;
	}

	if (!com_developer->integer && r_shadows->integer == 2)
	{
		Cvar_Set("cg_shadows", "1");
	}

	// we will need to change the projection matrix before drawing
	// 2D images again
	backEnd.projection2D = qfalse;

	// ensures that depth writes are enabled for the depth clear
	R_SetStateBits( GLS_DEFAULT );

	// Start the render pass (lazily starts the frame if needed)
	VK_BeginRenderPass();

	// Set gamma for 3D rendering
	extern cvar_t *r_gamma;
	float gammaValue = r_gamma->value;
	if ( gammaValue < 0.5f ) gammaValue = 0.5f;
	if ( gammaValue > 3.0f ) gammaValue = 3.0f;
	float invGamma = 1.0f / gammaValue;
	VkCommandBuffer cmd = vk.frames[vk.currentFrame].commandBuffer;
	vkCmdPushConstants( cmd, vk.pipelineLayout,
		VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 100, sizeof(float), &invGamma );

	//
	// set the modelview matrix for the viewer
	// (must be after VK_BeginRenderPass so the frame is started)
	//
	SetViewportAndScissor();

	// clear relevant buffers
	clearBits = 0x02; // depth

	if ( r_measureOverdraw->integer || r_shadows->integer == 2 )
	{
		clearBits |= 0x04; // stencil
	}
	if ( r_fastsky->integer && !( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) && !g_bRenderGlowingObjects )
	{
		clearBits |= 0x01; // color
	}

	if ( /*tr.refdef.rdflags & RDF_AUTOMAP ||*/ (!( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) && r_DynamicGlow->integer && !g_bRenderGlowingObjects ) )
	{
		if (tr.world && tr.world->globalFog != -1)
		{
			clearBits |= 0x01; // color
			// Fog color clear will be handled by the render pass clear values
		}
	}

	// If this pass is to just render the glowing objects, don't clear the depth buffer since
	// we're sharing it with the main scene (since the main scene has already been rendered). -AReis
	if ( g_bRenderGlowingObjects )
	{
		clearBits &= ~0x02;
	}

	if ( clearBits ) {
		VK_Clear( clearBits, 0.0f, 0.0f, 0.0f, 1.0f );
	}

	if ( ( backEnd.refdef.rdflags & RDF_HYPERSPACE ) )
	{
		RB_Hyperspace();
		return;
	}
	else
	{
		backEnd.isHyperspace = qfalse;
	}

	renderState.faceCulling = -1;		// force face culling to set next time

	// we will only draw a sun if there was sky rendered in this view
	backEnd.skyRenderedThisView = qfalse;

	// clip to the plane of the portal
	// In Vulkan, clip planes are handled via push constants or shader logic
	if ( backEnd.viewParms.isPortal ) {
		// Portal clipping is handled in the vertex shader via push constants
	}
}


#define	MAC_EVENT_PUMP_MSEC		5

/*
==================
RB_RenderDrawSurfList
==================
*/
void RB_RenderDrawSurfList( drawSurf_t *drawSurfs, int numDrawSurfs ) {
	shader_t		*shader, *oldShader;
	int				fogNum, oldFogNum;
	int				entityNum, oldEntityNum;
	int				dlighted, oldDlighted;
	int				depthRange, oldDepthRange;
	int				i;
	drawSurf_t		*drawSurf;
	unsigned int	oldSort;
	double			originalTime;
	bool			didShadowPass = false;

	if (g_bRenderGlowingObjects)
	{ //only shadow on initial passes
		didShadowPass = true;
	}

	// save original time for entity shader offsets
	originalTime = backEnd.refdef.floatTime;

	// clear the z buffer, set the modelview, etc
	RB_BeginDrawingView ();

	// draw everything
	oldEntityNum = -1;
	backEnd.currentEntity = &tr.worldEntity;
	oldShader = NULL;
	oldFogNum = -1;
	oldDepthRange = qfalse;
	oldDlighted = qfalse;
	oldSort = (unsigned int)-1;
	depthRange = qfalse;

	backEnd.pc.c_surfaces += numDrawSurfs;

	// pre-transform dlights for world orientation once
	R_TransformDlights( backEnd.refdef.num_dlights, backEnd.refdef.dlights, &backEnd.viewParms.world );
	qboolean worldDlightsValid = qtrue;

	for (i = 0, drawSurf = drawSurfs ; i < numDrawSurfs ; i++, drawSurf++) {
		if ( drawSurf->sort == oldSort ) {
			// fast path, same as previous sort
			rb_surfaceTable[ *drawSurf->surface ]( drawSurf->surface );
			continue;
		}

		R_DecomposeSort( drawSurf->sort, &entityNum, &shader, &fogNum, &dlighted );
		// If we're rendering glowing objects, but this shader has no stages with glow, skip it!
		if ( g_bRenderGlowingObjects && !shader->hasGlow )
		{
			shader = oldShader;
			entityNum = oldEntityNum;
			fogNum = oldFogNum;
			dlighted = oldDlighted;
			continue;
		}
		oldSort = drawSurf->sort;

		//
		// change the tess parameters if needed
		// a "entityMergable" shader is a shader that can have surfaces from seperate
		// entities merged into a single batch, like smoke and blood puff sprites
		if (shader != oldShader || fogNum != oldFogNum || dlighted != oldDlighted
			|| ( entityNum != oldEntityNum && (assert(shader), !shader->entityMergable) ) ) {
			if (oldShader != NULL) {
				RB_EndSurface();
			}
			RB_BeginSurface( shader, fogNum );

			oldShader = shader;
			oldFogNum = fogNum;
			oldDlighted = dlighted;
		}

		//
		// change the modelview matrix if needed
		//
		if ( entityNum != oldEntityNum ) {
			depthRange = 0;

			if ( entityNum != ENTITYNUM_WORLD ) {
				backEnd.currentEntity = &backEnd.refdef.entities[entityNum];
				if (backEnd.currentEntity->intShaderTime) {
					backEnd.refdef.floatTime = originalTime - 0.001 * backEnd.currentEntity->e.shaderTime.i;
				} else {
					// precision loss on high server time
					backEnd.refdef.floatTime = originalTime - backEnd.currentEntity->e.shaderTime.f;
				}
				// we have to reset the shaderTime as well otherwise image animations start
				// from the wrong frame
				tess.shaderTime = backEnd.refdef.floatTime - tess.shader->timeOffset;

				// set up the transformation matrix
				R_RotateForEntity( backEnd.currentEntity, &backEnd.viewParms, &backEnd.ori );

				// set up the dynamic lighting if needed
				if ( backEnd.currentEntity->needDlights ) {
					R_TransformDlights( backEnd.refdef.num_dlights, backEnd.refdef.dlights, &backEnd.ori );
					worldDlightsValid = qfalse;
				}

				if ( backEnd.currentEntity->e.renderfx & RF_NODEPTH ) {
					// No depth at all, very rare but some things for seeing through walls
					depthRange = 2;
				}
				else if ( backEnd.currentEntity->e.renderfx & RF_DEPTHHACK ) {
					// hack the depth range to prevent view model from poking into walls
					depthRange = 1;
				}
			} else {
				backEnd.currentEntity = &tr.worldEntity;
				backEnd.refdef.floatTime = originalTime;
				backEnd.ori = backEnd.viewParms.world;
				// we have to reset the shaderTime as well otherwise image animations on
				// the world (like water) continue with the wrong frame
				tess.shaderTime = backEnd.refdef.floatTime - tess.shader->timeOffset;
				// only recompute world dlight transforms if they were overwritten by an entity
				if ( !worldDlightsValid ) {
					R_TransformDlights( backEnd.refdef.num_dlights, backEnd.refdef.dlights, &backEnd.ori );
					worldDlightsValid = qtrue;
				}
			}

			// Vulkan: compute full MVP = projection * modelview and push via push constants
			{
				float mvp[16];
				myGlMultMatrix( backEnd.ori.modelMatrix, backEnd.viewParms.projectionMatrix, mvp );
				VK_SetMVP( mvp );
			}

			//
			// change depthrange if needed
			//
			if ( oldDepthRange != depthRange ) {
				switch ( depthRange ) {
					default:
					case 0:
						VK_SetDepthRange( 0.0f, 1.0f );
						break;

					case 1:
						VK_SetDepthRange( 0.0f, 0.3f );
						break;

					case 2:
						VK_SetDepthRange( 0.0f, 0.0f );
						break;
				}

				oldDepthRange = depthRange;
			}

			oldEntityNum = entityNum;
		}

		// add the triangles for this surface
		rb_surfaceTable[ *drawSurf->surface ]( drawSurf->surface );
	}

	backEnd.refdef.floatTime = originalTime;

	// draw the contents of the last shader batch
	if (oldShader != NULL) {
		RB_EndSurface();
	}

	// go back to the world modelview matrix
	{
		float mvp[16];
		myGlMultMatrix( backEnd.viewParms.world.modelMatrix, backEnd.viewParms.projectionMatrix, mvp );
		VK_SetMVP( mvp );
	}
	if ( depthRange ) {
		VK_SetDepthRange( 0.0f, 1.0f );
	}

	if (!didShadowPass)
	{
		// darken down any stencil shadows
		RB_ShadowFinish();
		didShadowPass = true;
	}

}


/*
============================================================================

RENDER BACK END THREAD FUNCTIONS

============================================================================
*/

/*
================
RB_SetGL2D

================
*/
void	RB_SetGL2D (void) {
	backEnd.projection2D = qtrue;

	VK_Set2D();

	R_SetStateBits( GLS_DEPTHTEST_DISABLE |
			  GLS_SRCBLEND_SRC_ALPHA |
			  GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA );

	renderState.faceCulling = CT_TWO_SIDED;

	// set time for 2D shaders
	backEnd.refdef.time = ri.Milliseconds();
	backEnd.refdef.floatTime = backEnd.refdef.time * 0.001;
}


/*
=============
RE_StretchRaw

FIXME: not exactly backend
Stretches a raw 32 bit power of 2 bitmap image over the given screen rectangle.
Used for cinematics.
=============
*/
void RE_StretchRaw (int x, int y, int w, int h, int cols, int rows, const byte *data, int client, qboolean dirty)
{
	int			start, end;

	if ( !tr.registered ) {
		return;
	}
	R_SyncRenderThread();

	start = end = 0;
	if ( r_speeds->integer ) {
		start = ri.Milliseconds();
	}

	// make sure rows and cols are powers of 2
	if ( (cols&(cols-1)) || (rows&(rows-1)) )
	{
		ri.Error (ERR_DROP, "Draw_StretchRaw: size not a power of 2: %i by %i", cols, rows);
	}

	// Update the scratch image via Vulkan
	if ( cols != tr.scratchImage[client]->width || rows != tr.scratchImage[client]->height ) {
		tr.scratchImage[client]->width = tr.scratchImage[client]->uploadWidth = cols;
		tr.scratchImage[client]->height = tr.scratchImage[client]->uploadHeight = rows;
		// Recreate the image
		VK_DestroyImage( tr.scratchImage[client] );
		VK_CreateImage( tr.scratchImage[client], data, cols, rows, qfalse, qtrue );
	} else {
		if (dirty) {
			VK_UpdateImage( tr.scratchImage[client], data, cols, rows );
		}
	}

	if ( r_speeds->integer ) {
		end = ri.Milliseconds();
		ri.Printf( PRINT_ALL, "VK_UpdateImage %i, %i: %i msec\n", cols, rows, end - start );
	}

	RB_SetGL2D();

	R_BindImage( tr.scratchImage[client] );

	byte color[4] = { (byte)(tr.identityLight * 255), (byte)(tr.identityLight * 255), (byte)(tr.identityLight * 255), 255 };
	VK_BindPipeline( renderState.stateBits, CT_TWO_SIDED, qfalse, qfalse );
	VK_DrawQuad( (float)x, (float)y, (float)(x+w), (float)(y+h),
		0.5f / cols, 0.5f / rows, (cols - 0.5f) / cols, (rows - 0.5f) / rows, color );
}

void RE_UploadCinematic (int cols, int rows, const byte *data, int client, qboolean dirty) {

	// Update the scratch image via Vulkan
	if ( cols != tr.scratchImage[client]->width || rows != tr.scratchImage[client]->height ) {
		tr.scratchImage[client]->width = tr.scratchImage[client]->uploadWidth = cols;
		tr.scratchImage[client]->height = tr.scratchImage[client]->uploadHeight = rows;
		VK_DestroyImage( tr.scratchImage[client] );
		VK_CreateImage( tr.scratchImage[client], data, cols, rows, qfalse, qtrue );
	} else {
		if (dirty) {
			VK_UpdateImage( tr.scratchImage[client], data, cols, rows );
		}
	}
}


/*
=============
RB_SetColor

=============
*/
const void	*RB_SetColor( const void *data ) {
	const setColorCommand_t	*cmd;

	cmd = (const setColorCommand_t *)data;

	backEnd.color2D[0] = cmd->color[0] * 255;
	backEnd.color2D[1] = cmd->color[1] * 255;
	backEnd.color2D[2] = cmd->color[2] * 255;
	backEnd.color2D[3] = cmd->color[3] * 255;

	return (const void *)(cmd + 1);
}

/*
=============
RB_StretchPic
=============
*/
const void *RB_StretchPic ( const void *data ) {
	const stretchPicCommand_t	*cmd;
	shader_t *shader;
	int		numVerts, numIndexes;

	cmd = (const stretchPicCommand_t *)data;

	if ( !backEnd.projection2D ) {
		RB_SetGL2D();
	}

	shader = cmd->shader;
	if ( shader != tess.shader ) {
		if ( tess.numIndexes ) {
			RB_EndSurface();
		}
		backEnd.currentEntity = &backEnd.entity2D;
		RB_BeginSurface( shader, 0 );
	}

	RB_CHECKOVERFLOW( 4, 6 );
	numVerts = tess.numVertexes;
	numIndexes = tess.numIndexes;

	tess.numVertexes += 4;
	tess.numIndexes += 6;

	tess.indexes[ numIndexes ] = numVerts + 3;
	tess.indexes[ numIndexes + 1 ] = numVerts + 0;
	tess.indexes[ numIndexes + 2 ] = numVerts + 2;
	tess.indexes[ numIndexes + 3 ] = numVerts + 2;
	tess.indexes[ numIndexes + 4 ] = numVerts + 0;
	tess.indexes[ numIndexes + 5 ] = numVerts + 1;

	tess.vertexColorsui[ numVerts ] =
		tess.vertexColorsui[ numVerts + 1 ] =
		tess.vertexColorsui[ numVerts + 2 ] =
		tess.vertexColorsui[ numVerts + 3 ] = backEnd.color2Dui;

	tess.xyz[ numVerts ][0] = cmd->x;
	tess.xyz[ numVerts ][1] = cmd->y;
	tess.xyz[ numVerts ][2] = 0;

	tess.texCoords[0][ numVerts ][0] = cmd->s1;
	tess.texCoords[0][ numVerts ][1] = cmd->t1;

	tess.xyz[ numVerts + 1 ][0] = cmd->x + cmd->w;
	tess.xyz[ numVerts + 1 ][1] = cmd->y;
	tess.xyz[ numVerts + 1 ][2] = 0;

	tess.texCoords[0][ numVerts + 1 ][0] = cmd->s2;
	tess.texCoords[0][ numVerts + 1 ][1] = cmd->t1;

	tess.xyz[ numVerts + 2 ][0] = cmd->x + cmd->w;
	tess.xyz[ numVerts + 2 ][1] = cmd->y + cmd->h;
	tess.xyz[ numVerts + 2 ][2] = 0;

	tess.texCoords[0][ numVerts + 2 ][0] = cmd->s2;
	tess.texCoords[0][ numVerts + 2 ][1] = cmd->t2;

	tess.xyz[ numVerts + 3 ][0] = cmd->x;
	tess.xyz[ numVerts + 3 ][1] = cmd->y + cmd->h;
	tess.xyz[ numVerts + 3 ][2] = 0;

	tess.texCoords[0][ numVerts + 3 ][0] = cmd->s1;
	tess.texCoords[0][ numVerts + 3 ][1] = cmd->t2;

	return (const void *)(cmd + 1);
}

/*
=============
RB_TransformPic
=============
*/
const void *RB_TransformPic ( const void *data ) {
	const transformPicCommand_t	*cmd;
	shader_t *shader;
	int		numVerts, numIndexes;

	cmd = (const transformPicCommand_t *)data;

	if ( !backEnd.projection2D ) {
		RB_SetGL2D();
	}

	shader = cmd->shader;
	if ( shader != tess.shader ) {
		if ( tess.numIndexes ) {
			RB_EndSurface();
		}
		backEnd.currentEntity = &backEnd.entity2D;
		RB_BeginSurface( shader, 0 );
	}

	RB_CHECKOVERFLOW( 4, 6 );
	numVerts = tess.numVertexes;
	numIndexes = tess.numIndexes;

	tess.numVertexes += 4;
	tess.numIndexes += 6;

	tess.indexes[ numIndexes ] = numVerts + 3;
	tess.indexes[ numIndexes + 1 ] = numVerts + 0;
	tess.indexes[ numIndexes + 2 ] = numVerts + 2;
	tess.indexes[ numIndexes + 3 ] = numVerts + 2;
	tess.indexes[ numIndexes + 4 ] = numVerts + 0;
	tess.indexes[ numIndexes + 5 ] = numVerts + 1;

	tess.vertexColorsui[ numVerts ] =
		tess.vertexColorsui[ numVerts + 1 ] =
		tess.vertexColorsui[ numVerts + 2 ] =
		tess.vertexColorsui[ numVerts + 3 ] = backEnd.color2Dui;

	tess.xyz[ numVerts ][0] = cmd->x;
	tess.xyz[ numVerts ][1] = cmd->y;
	tess.xyz[ numVerts ][2] = 0;

	tess.texCoords[0][ numVerts ][0] = cmd->s1;
	tess.texCoords[0][ numVerts ][1] = cmd->t1;

	tess.xyz[ numVerts + 1 ][0] = cmd->x + cmd->m[0][0];
	tess.xyz[ numVerts + 1 ][1] = cmd->y + cmd->m[1][0];
	tess.xyz[ numVerts + 1 ][2] = 0;

	tess.texCoords[0][ numVerts + 1 ][0] = cmd->s2;
	tess.texCoords[0][ numVerts + 1 ][1] = cmd->t1;

	tess.xyz[ numVerts + 2 ][0] = cmd->x + cmd->m[0][0] + cmd->m[0][1];
	tess.xyz[ numVerts + 2 ][1] = cmd->y + cmd->m[1][0] + cmd->m[1][1];
	tess.xyz[ numVerts + 2 ][2] = 0;

	tess.texCoords[0][ numVerts + 2 ][0] = cmd->s2;
	tess.texCoords[0][ numVerts + 2 ][1] = cmd->t2;

	tess.xyz[ numVerts + 3 ][0] = cmd->x + cmd->m[0][1];
	tess.xyz[ numVerts + 3 ][1] = cmd->y + cmd->m[1][1];
	tess.xyz[ numVerts + 3 ][2] = 0;

	tess.texCoords[0][ numVerts + 3 ][0] = cmd->s1;
	tess.texCoords[0][ numVerts + 3 ][1] = cmd->t2;

	return (const void *)(cmd + 1);
}

/*
=============
RB_DrawSurfs

=============
*/
const void	*RB_DrawSurfs( const void *data ) {
	const drawSurfsCommand_t	*cmd;

	// finish any 2D drawing if needed
	if ( tess.numIndexes ) {
		RB_EndSurface();
	}

	cmd = (const drawSurfsCommand_t *)data;

	backEnd.refdef = cmd->refdef;
	backEnd.viewParms = cmd->viewParms;

	RB_RenderDrawSurfList( cmd->drawSurfs, cmd->numDrawSurfs );

	if ( !(backEnd.refdef.rdflags & RDF_NOWORLDMODEL) && g_bDynamicGlowSupported && r_DynamicGlow->integer )
	{
		// End the main render pass so we can render to offscreen glow target
		VK_EndRenderPass();

		// Render the glowing objects to the full-res offscreen glow image.
		// The glow framebuffer shares the main depth buffer for correct occlusion.
		g_bRenderGlowingObjects = true;

		if ( vk.glow.glowFramebuffer ) {
			VkCommandBuffer vkcmd = vk.frames[vk.currentFrame].commandBuffer;

			VkClearValue clearValues[2] = {};
			clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
			clearValues[1].depthStencil = { 1.0f, 0 };
			VkRenderPassBeginInfo rpBegin = {};
			rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			rpBegin.renderPass = vk.glow.glowRenderPass;
			rpBegin.framebuffer = vk.glow.glowFramebuffer;
			rpBegin.clearValueCount = 2;
			rpBegin.pClearValues = clearValues;
			rpBegin.renderArea.offset = { 0, 0 };
			rpBegin.renderArea.extent = vk.swapchainExtent;

			// Mark render-pass active for wrapper consistency, then begin glow render-pass
			vk.renderPassActive = qtrue;
			vkCmdBeginRenderPass( vkcmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE );

			// Full-res viewport — RB_RenderDrawSurfList will set it from backEnd.viewParms
			// which are already at full resolution, so no scaling needed.
			RB_RenderDrawSurfList( cmd->drawSurfs, cmd->numDrawSurfs );

			vkCmdEndRenderPass( vkcmd );
			vk.renderPassActive = qfalse;
		}
		g_bRenderGlowingObjects = false;

		// Blur the glow texture (downsamples full-res to half-res internally)
		RB_BlurGlowTexture();

		// Resume main render pass (load existing contents) and composite glow on top
		VK_BeginRenderPassLoad();
		RB_DrawGlowOverlay();
	}

	return (const void *)(cmd + 1);
}


/*
=============
RB_DrawBuffer

=============
*/
const void	*RB_DrawBuffer( const void *data ) {
	const drawBufferCommand_t	*cmd;

	cmd = (const drawBufferCommand_t *)data;

	// Don't start the Vulkan frame or render pass here.
	// Frame acquisition and render pass begin are deferred to the first
	// actual draw operation (VK_Set2D, RB_BeginDrawingView, VK_Clear, etc.)
	// via VK_BeginRenderPass which lazily calls VK_BeginFrame.
	// This prevents prematurely acquiring swapchain images when
	// R_SyncRenderThread flushes the command list during map loading.
	//
	// Note: fog/debug clears are handled by RB_BeginDrawingView for the
	// actual scene, so we skip them here to avoid starting a frame
	// that has no matching RC_SWAP_BUFFERS during sync flushes.

	return (const void *)(cmd + 1);
}

/*
===============
RB_ShowImages

Draw all the images to the screen, on top of whatever
was there.  This is used to test for texture thrashing.

Also called by RE_EndRegistration
===============
*/
void RB_ShowImages( void ) {
	image_t	*image;
	float	x, y, w, h;
	int		start, end;

	if ( !backEnd.projection2D ) {
		RB_SetGL2D();
	}

	VK_Clear( 0x01, 0, 0, 0, 1 );

	start = ri.Milliseconds();

	int i=0;
					 R_Images_StartIteration();
	while ( (image = R_Images_GetNextIteration()) != NULL)
	{
		w = glConfig.vidWidth / 20;
		h = glConfig.vidHeight / 15;
		x = i % 20 * w;
		y = i / 20 * h;

		// show in proportional size in mode 2
		if ( r_showImages->integer == 2 ) {
			w *= image->uploadWidth / 512.0f;
			h *= image->uploadHeight / 512.0f;
		}

		R_BindImage( image );
		byte white[4] = { 255, 255, 255, 255 };
		VK_BindPipeline( renderState.stateBits, CT_TWO_SIDED, qfalse, qfalse );
		VK_DrawQuad( x, y, x + w, y + h, 0, 0, 1, 1, white );
		i++;
	}

	end = ri.Milliseconds();
	ri.Printf( PRINT_ALL, "%i msec to draw all images\n", end - start );

}


/*
=============
RB_SwapBuffers

=============
*/
const void	*RB_SwapBuffers( const void *data ) {
	const swapBuffersCommand_t	*cmd;

	// finish any 2D drawing if needed
	if ( tess.numIndexes ) {
		RB_EndSurface();
	}

	// texture swapping test
	if (r_showImages->integer) {
		RB_ShowImages();
	}

	cmd = (const swapBuffersCommand_t *)data;

	// Stencil overdraw measurement not supported in Vulkan
	if ( r_measureOverdraw->integer ) {
		ri.Printf( PRINT_WARNING, "r_measureOverdraw is not supported with the Vulkan renderer\n" );
		ri.Cvar_Set( "r_measureOverdraw", "0" );
	}

	backEnd.projection2D = qfalse;

	// End frame and present
	VK_EndFrame();

	return (const void *)(cmd + 1);
}

/*
==================
RB_WorldEffects
==================
*/
const void *RB_WorldEffects( const void *data )
{
	const worldEffectsCommand_t	*cmd;

	cmd = (const worldEffectsCommand_t *)data;

	// finish any 2D drawing if needed
	if ( tess.numIndexes ) {
		RB_EndSurface();
	}

	RB_RenderWorldEffects();

	return (const void *)(cmd + 1);
}

/*
==================
RB_GammaCorrection
==================
*/
const void *RB_GammaCorrection( const void *data )
{
	const gammaCorrectionCommand_t	*cmd;

	cmd = (const gammaCorrectionCommand_t *)data;

	// finish any 2D drawing if needed
	if ( tess.numIndexes ) {
		RB_EndSurface();
	}

	// In Vulkan, gamma correction is handled via a fullscreen pass
	// with push constants. Falls back to SDL hardware gamma if
	// offscreen scene rendering is not set up.
	VK_ApplyGammaCorrection();

	return (const void *)(cmd + 1);
}

/*
==================
RB_ReadPixels
==================
*/
const void *RB_ReadPixels( const void *data )
{
	const readPixelsCommand_t	*cmd;

	cmd = (const readPixelsCommand_t *)data;

	// finish any 2D drawing if needed
	if ( tess.numIndexes ) {
		RB_EndSurface();
	}

	VK_ReadPixels( 0, 0, glConfig.vidWidth, glConfig.vidHeight, cmd->format, cmd->buffer );

	return (const void *)(cmd + 1);
}

/*
====================
RB_ExecuteRenderCommands

This function will be called syncronously if running without
smp extensions, or asyncronously by another thread.
====================
*/
void RB_ExecuteRenderCommands( const void *data ) {
	int			t1, t2;

	t1 = ri.Milliseconds()*Cvar_VariableValue("timescale");

	while ( 1 ) {
		data = PADP( data, sizeof( void * ) );

		switch ( *(const int *)data ) {
		case RC_SET_COLOR:
			data = RB_SetColor( data );
			break;
		case RC_STRETCH_PIC:
			data = RB_StretchPic( data );
			break;
		case RC_TRANSFORM_PIC:
			data = RB_TransformPic( data );
			break;
		case RC_DRAW_SURFS:
			data = RB_DrawSurfs( data );
			break;
		case RC_DRAW_BUFFER:
			data = RB_DrawBuffer( data );
			break;
		case RC_SWAP_BUFFERS:
			data = RB_SwapBuffers( data );
			break;
		case RC_WORLD_EFFECTS:
			data = RB_WorldEffects( data );
			break;
		case RC_GAMMA_CORRECTION:
			data = RB_GammaCorrection( data );
			break;
		case RC_READ_PIXELS:
			data = RB_ReadPixels( data );
			break;
		case RC_END_OF_LIST:
			// stop rendering
			t2 = ri.Milliseconds()*Cvar_VariableValue("timescale");
			backEnd.pc.msec = t2 - t1;
			return;
		default:
			ri.Error(ERR_DROP, "Unknown render command");
		}
	}
}

static void RB_BlurGlowTexture()
{
	VK_BlurGlowTexture();
}

// Draw the glow blur over the screen additively.
static void RB_DrawGlowOverlay()
{
	VK_DrawGlowOverlay();
}
#endif //!DEDICATED
