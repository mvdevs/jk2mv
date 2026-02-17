// tr_QuickSprite.cpp: implementation of the CQuickSpriteSystem class.
//
//////////////////////////////////////////////////////////////////////
//#include "../server/exe_headers.h"
#include "tr_local.h"
#include "vk_local.h"

#include "tr_quicksprite.h"

void R_BindAnimatedImage( textureBundle_t *bundle );


//////////////////////////////////////////////////////////////////////
// Singleton System
//////////////////////////////////////////////////////////////////////
CQuickSpriteSystem SQuickSprite;

// Static pre-computed quad index buffer
glIndex_t CQuickSpriteSystem::mQuadIndexes[SHADER_MAX_VERTEXES / 4 * 6];
bool CQuickSpriteSystem::mIndexesInitialized = false;

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CQuickSpriteSystem::CQuickSpriteSystem()
{
	int i;

	for (i=0; i<SHADER_MAX_VERTEXES; i+=4)
	{
		// Bottom right
		mTextureCoords[i+0][0] = 1.0;
		mTextureCoords[i+0][1] = 1.0;
		// Top right
		mTextureCoords[i+1][0] = 1.0;
		mTextureCoords[i+1][1] = 0.0;
		// Top left
		mTextureCoords[i+2][0] = 0.0;
		mTextureCoords[i+2][1] = 0.0;
		// Bottom left
		mTextureCoords[i+3][0] = 0.0;
		mTextureCoords[i+3][1] = 1.0;
	}

	// Pre-compute quad index buffer once — pattern never changes
	if ( !mIndexesInitialized ) {
		int ni = 0;
		for (int q = 0; q < SHADER_MAX_VERTEXES / 4; q++) {
			int base = q * 4;
			mQuadIndexes[ni++] = base + 0;
			mQuadIndexes[ni++] = base + 1;
			mQuadIndexes[ni++] = base + 2;
			mQuadIndexes[ni++] = base + 0;
			mQuadIndexes[ni++] = base + 2;
			mQuadIndexes[ni++] = base + 3;
		}
		mIndexesInitialized = true;
	}
}

CQuickSpriteSystem::~CQuickSpriteSystem()
{

}


void CQuickSpriteSystem::Flush(void)
{
	if (mNextVert==0)
	{
		return;
	}

	//
	// render the main pass
	//
	R_BindAnimatedImage( mTexBundle );
	R_SetStateBits(mGLStateBits);

	//
	// Vulkan: use pre-computed quad index buffer and draw
	//
	{
		int numIndexes = (mNextVert / 4) * 6;

		VK_BindPipeline( renderState.stateBits, renderState.faceCulling, qfalse, qfalse );
		VK_DrawIndexed( mNextVert, (float*)mVerts, (float*)mTextureCoords, NULL, (byte*)mColors, numIndexes, mQuadIndexes );
	}

	backEnd.pc.c_vertexes += mNextVert;
	backEnd.pc.c_indexes += mNextVert;
	backEnd.pc.c_totalIndexes += mNextVert;

	if (mUseFog)
	{
		//
		// render the fog pass
		//
		R_BindImage( tr.fogImage );
		R_SetStateBits( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHFUNC_EQUAL );

		// Vulkan: draw fog pass with fog texture coords and solid fog color
		// Reuse pre-computed quad index buffer
		{
			byte fogColors[SHADER_MAX_VERTEXES][4];
			for (int i = 0; i < mNextVert; i++) {
				((unsigned int*)fogColors)[i] = mFogColor;
			}

			int numIndexes = (mNextVert / 4) * 6;

			VK_BindPipeline( renderState.stateBits, renderState.faceCulling, qfalse, qfalse );
			VK_DrawIndexed( mNextVert, (float*)mVerts, (float*)mFogTextureCoords, NULL, (byte*)fogColors, numIndexes, mQuadIndexes );
		}

		// Second pass from fog
		backEnd.pc.c_totalIndexes += mNextVert;
	}

	mNextVert=0;
}


void CQuickSpriteSystem::StartGroup(textureBundle_t *bundle, unsigned int glbits, unsigned int fogcolor )
{
	mNextVert = 0;

	mTexBundle = bundle;
	mGLStateBits = glbits;
	if (fogcolor)
	{
		mUseFog = qtrue;
		mFogColor = fogcolor;
	}
	else
	{
		mUseFog = qfalse;
	}

	// Vulkan: cull mode is part of the pipeline state
	R_SetCullMode( CT_TWO_SIDED );
}


void CQuickSpriteSystem::EndGroup(void)
{
	Flush();

	// Vulkan: restore default cull mode
	R_SetCullMode( CT_FRONT_SIDED );
}




void CQuickSpriteSystem::Add(float *pointdata, color4ub_t color, vec2_t fog)
{
	float *curcoord;
	float *curfogtexcoord;
	unsigned int *curcolor;

	if (mNextVert>SHADER_MAX_VERTEXES-4)
	{
		Flush();
	}

	curcoord = mVerts[mNextVert];
	memcpy(curcoord, pointdata, 4*sizeof(vec4_t));

	// Set up color
	curcolor = &mColors[mNextVert];
	*curcolor++ = *(unsigned int *)color;
	*curcolor++ = *(unsigned int *)color;
	*curcolor++ = *(unsigned int *)color;
	*curcolor++ = *(unsigned int *)color;

	if (fog)
	{
		curfogtexcoord = &mFogTextureCoords[mNextVert][0];
		*curfogtexcoord++ = fog[0];
		*curfogtexcoord++ = fog[1];

		*curfogtexcoord++ = fog[0];
		*curfogtexcoord++ = fog[1];

		*curfogtexcoord++ = fog[0];
		*curfogtexcoord++ = fog[1];

		*curfogtexcoord++ = fog[0];
		*curfogtexcoord++ = fog[1];

		mUseFog=qtrue;
	}
	else
	{
		mUseFog=qfalse;
	}

	mNextVert+=4;
}
