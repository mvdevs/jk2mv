// leave this as first line for PCH reasons...
//


#ifndef __Q_SHARED_H
	#include "../qcommon/q_shared.h"
#endif

#if !defined(TR_LOCAL_H)
	#include "../renderer/tr_local.h"
#endif

#if !defined(G2_H_INC)
	#include "G2.h"
#endif
#include "G2_local.h"

#ifdef G2_COLLISION_ENABLED
#include "../qcommon/MiniHeap.h"
#endif

#include <map>
// #include <algorithm>

// #ifdef assert
// #	undef assert
// #	define assert(x) ((void)0)
// #endif

extern mdxaBone_t		worldMatrix;
extern mdxaBone_t		worldMatrixInv;

typedef map<g2handle_t, CGhoul2Info_v> CGhoul2Info_m;

static CGhoul2Info_m	ghoultable[2];
static g2handle_t		nextGhoul2Handle = (g2handle_t)1;

// game/cgame context
bool RicksCrazyOnServer;

CGhoul2Info_v *G2API_GetGhoul2Model(g2handle_t g2h) {
	CGhoul2Info_m::iterator ghlIt = ghoultable[RicksCrazyOnServer].find(g2h);

	if (ghlIt == ghoultable[RicksCrazyOnServer].end())
	{
		return NULL;
	}

	return &ghlIt->second;
}

void FixGhoul2InfoLeaks(bool clearClient,bool clearServer)
{
	if ( clearClient )
		ghoultable[0].clear();
	if ( clearServer )
		ghoultable[1].clear();
}

void G2API_CleanGhoul2Models(g2handle_t *g2hPtr) {
	ghoultable[RicksCrazyOnServer].erase(*g2hPtr);
	*g2hPtr = 0;
}

qhandle_t G2API_PrecacheGhoul2Model(const char *fileName)
{
	return RE_RegisterModel(fileName);
}

int G2API_InitGhoul2Model(g2handle_t *g2hPtr, const char *fileName, int modelIndex, qhandle_t customSkin,
	qhandle_t customShader, int modelFlags, int lodBias)
{
	CGhoul2Info		newModel;
	g2handle_t		g2h = *g2hPtr;

	// are we actually asking for a model to be loaded.
	if (!(strlen(fileName)))
	{
		assert(0);
		return -1;
	}

	if (g2h == 0)
	{
		*g2hPtr = g2h = nextGhoul2Handle++;
	}

	CGhoul2Info_v &ghoul2 = ghoultable[RicksCrazyOnServer][g2h];

	// find a free spot in the list
	for (CGhoul2Info_v::iterator it = ghoul2.begin(); it != ghoul2.end(); ++it)
	{
		if (it->mModelindex == -1)
		{
			// this is only valid and used on the game side. Client side ignores this
			it->mModelindex = modelIndex;
				// on the game side this is valid. On the client side it is valid only after it has been filled in by trap_G2_SetGhoul2ModelIndexes
			it->mModel = RE_RegisterModel(fileName);
			model_t		*mod_m = R_GetModelByHandle(it->mModel);
			if (mod_m->type == MOD_BAD)
			{
				return -1;
			}

			// init what is necessary for this ghoul2 model
			// fau - what about remaining fields?
			G2_Init_Bone_List(it->mBlist);
			G2_Init_Bolt_List(it->mBltlist);
			it->mCustomShader = customShader;
			it->mCustomSkin = customSkin;
			Q_strncpyz(it->mFileName, fileName, sizeof(it->mFileName));
			it->mCreationID = modelFlags;
			it->mLodBias = lodBias;
			it->mAnimFrameDefault = 0;
			it->mFlags = 0;

			it->mSkelFrameNum = -1;
			it->mMeshFrameNum = -1;

			// we aren't attached to anyone upfront
			it->mModelBoltLink = -1;
			return it - ghoul2.begin();
		}
	}

	// if we got this far, then we didn't find a spare position, so lets insert a new one
	newModel.mModelindex = modelIndex;
	// on the game side this is valid. On the client side it is valid only after it has been filled in by trap_G2_SetGhoul2ModelIndexes
	if (customShader <= -20)
	{ //This means the server is making the function call. And the server does not like registering models.
		newModel.mModel = RE_RegisterServerModel(fileName);
		customShader = 0;
	}
	else
	{
		newModel.mModel = RE_RegisterModel(fileName);
	}
	model_t		*mod_m = R_GetModelByHandle(newModel.mModel);
	if (mod_m->type == MOD_BAD)
	{
		if (ghoul2.size() == 0)//very first model created
		{//you can't have an empty vector, so let's not give it one
			G2API_CleanGhoul2Models(g2hPtr);
		}
		return -1;
	}

	// init what is necessary for this ghoul2 model
	G2_Init_Bone_List(newModel.mBlist);
	G2_Init_Bolt_List(newModel.mBltlist);
	newModel.mCustomShader = customShader;
	newModel.mCustomSkin = customSkin;
	Q_strncpyz(newModel.mFileName, fileName, sizeof(newModel.mFileName));
	newModel.mCreationID = modelFlags;
	newModel.mLodBias = lodBias;
	newModel.mAnimFrameDefault = 0;
	newModel.mFlags = 0;

	// we aren't attached to anyone upfront
	newModel.mModelBoltLink = -1;

	// insert into list.
	ghoul2.push_back(newModel);

	return ghoul2.size() - 1;
}

qboolean G2API_SetLodBias(CGhoul2Info *ghlInfo, int lodBias)
{
	if (ghlInfo)
	{
		ghlInfo->mLodBias = lodBias;
		return qtrue;
	}
	return qfalse;
}

qboolean G2API_SetSkin(CGhoul2Info *ghlInfo, qhandle_t customSkin)
{
	if (ghlInfo)
	{
		ghlInfo->mCustomSkin = customSkin;
		return qtrue;
	}
	return qfalse;
}

qboolean G2API_SetShader(CGhoul2Info *ghlInfo, qhandle_t customShader)
{
	if (ghlInfo)
	{
		ghlInfo->mCustomShader = customShader;
		return qtrue;
	}
	return qfalse;
}

qboolean G2API_SetSurfaceOnOff(g2handle_t g2h, const char *surfaceName, const int flags)
{
	CGhoul2Info_v *ghoul2 = G2API_GetGhoul2Model(g2h);

	if (ghoul2 && !ghoul2->empty()) {
		CGhoul2Info &ghlInfo = ghoul2->front();

		ghlInfo.mMeshFrameNum = 0;
		return G2_SetSurfaceOnOff(ghlInfo.mFileName, ghlInfo.mSlist, surfaceName, flags);
	}

	return qfalse;
}

int G2API_GetSurfaceOnOff(CGhoul2Info *ghlInfo, const char *surfaceName)
{
	if (ghlInfo)
	{
		return G2_IsSurfaceOff(ghlInfo->mFileName, ghlInfo->mSlist, surfaceName);
	}
	return -1;
}

qboolean G2API_SetRootSurface(g2handle_t g2h, const int modelIndex, const char *surfaceName)
{
	CGhoul2Info_v *ghoul2 = G2API_GetGhoul2Model(g2h);

	if (!ghoul2) {
		return qfalse;
	}

	return G2_SetRootSurface(g2h, *ghoul2, modelIndex, surfaceName);
}

int G2API_AddSurface(CGhoul2Info *ghlInfo, int surfaceNumber, int polyNumber, float BarycentricI, float BarycentricJ, int lod )
{

	if (ghlInfo)
	{
		// ensure we flush the cache
		ghlInfo->mMeshFrameNum = 0;
		return G2_AddSurface(ghlInfo, surfaceNumber, polyNumber, BarycentricI, BarycentricJ, lod);
	}
	return -1;
}

qboolean G2API_RemoveSurface(CGhoul2Info *ghlInfo, const int index)
{
	if (ghlInfo)
	{
		// ensure we flush the cache
		ghlInfo->mMeshFrameNum = 0;
		return G2_RemoveSurface(ghlInfo->mSlist, index);
	}
	return qfalse;
}

int G2API_GetParentSurface(CGhoul2Info *ghlInfo, const int index)
{
	if (ghlInfo)
	{
		return G2_GetParentSurface(ghlInfo->mFileName, index);
	}
	return -1;
}

int G2API_GetSurfaceRenderStatus(CGhoul2Info *ghlInfo, const char *surfaceName)
{
	if (ghlInfo)
	{
		return G2_IsSurfaceRendered(ghlInfo->mFileName, surfaceName, ghlInfo->mSlist);
	}
	return -1;
}

qboolean G2API_HasGhoul2ModelOnIndex(const g2handle_t *g2hPtr, const int modelIndex)
{
	CGhoul2Info_v *ghoul2Ptr = G2API_GetGhoul2Model(*g2hPtr);

	if (!ghoul2Ptr)
	{
		assert(0);
		return qfalse;
	}

	CGhoul2Info_v &ghoul2 = *ghoul2Ptr;

	if (ghoul2.size() <= (unsigned)modelIndex || ghoul2[modelIndex].mModelindex == -1)
	{
		return qfalse;
	}

	return qtrue;
}

qboolean G2API_RemoveGhoul2Model(g2handle_t *g2hPtr, const int modelIndex)
{
	CGhoul2Info_v *ghoul2Ptr = G2API_GetGhoul2Model(*g2hPtr);

	if (!ghoul2Ptr)
	{
		assert(0);
		return qfalse;
	}

	CGhoul2Info_v &ghoul2 = *ghoul2Ptr;

	// sanity check
	if (ghoul2.size() <= (unsigned)modelIndex || ghoul2[modelIndex].mModelindex == -1)
	{
		// if we hit this assert then we are trying to delete a ghoul2 model on a ghoul2 instance that
		// one way or another is already gone.
		assert(0);
		return qfalse;
	}

	CGhoul2Info &ghlInfo = ghoul2[modelIndex];

	// clear out the vectors this model used.
	ghlInfo.mBlist.clear();
	ghlInfo.mBltlist.clear();
	ghlInfo.mSlist.clear();

	// set us to be the 'not active' state
	ghlInfo.mModelindex = -1;

	// now look through the list from the back and see if there is a block of -1's we can resize off the end of the list
	CGhoul2Info_v::reverse_iterator it = ghoul2.rbegin();
	while(it != ghoul2.rend() && it->mModelindex == -1)
	{
		ghoul2.pop_back();
		it = ghoul2.rbegin();
	}

	// if we are not using any space, just delete the ghoul2 vector entirely
	if (ghoul2.empty())
	{
		G2API_CleanGhoul2Models(g2hPtr);
	}

	return qtrue;
}

qboolean G2API_SetBoneAnimIndex(CGhoul2Info *ghlInfo, const int index, const int AstartFrame, const int AendFrame, const int flags, const float animSpeed, const int currentTime, const float AsetFrame, const int blendTime)
{
	int endFrame=AendFrame;
	int startFrame=AstartFrame;
	float setFrame=AsetFrame;
	assert(endFrame>0);
	assert(startFrame>=0);
	assert(endFrame<100000);
	assert(startFrame<100000);
	assert(setFrame>=0.0f||setFrame==-1.0f);
	assert(setFrame<=100000.0f);
	if (endFrame<=0)
	{
		endFrame=1;
	}
	if (endFrame>=100000)
	{
		endFrame=1;
	}
	if (startFrame<0)
	{
		startFrame=0;
	}
	if (startFrame>=100000)
	{
		startFrame=0;
	}
	if (setFrame<0.0f&&setFrame!=-1.0f)
	{
		setFrame=0.0f;
	}
	if (setFrame>100000.0f)
	{
		setFrame=0.0f;
	}
	if (ghlInfo)
	{
		// ensure we flush the cache
		ghlInfo->mSkelFrameNum = 0;
		return G2_Set_Bone_Anim_Index(ghlInfo->mBlist, index, startFrame, endFrame, flags, animSpeed, currentTime, setFrame, blendTime);
	}
	return qfalse;
}

qboolean G2API_SetBoneAnim(g2handle_t g2h, const int modelIndex, const char *boneName, const int AstartFrame, const int AendFrame, const int flags, const float animSpeed, const int currentTime, const float AsetFrame, const int blendTime)
{
	int endFrame=AendFrame;
	int startFrame=AstartFrame;
	float setFrame=AsetFrame;
	assert(endFrame>0);
	assert(startFrame>=0);
	assert(endFrame<100000);
	assert(startFrame<100000);
	assert(setFrame>=0.0f||setFrame==-1.0f);
	assert(setFrame<=100000.0f);
	if (endFrame<=0)
	{
		endFrame=1;
	}
	if (endFrame>=100000)
	{
		endFrame=1;
	}
	if (startFrame<0)
	{
		startFrame=0;
	}
	if (startFrame>=100000)
	{
		startFrame=0;
	}
	if (setFrame<0.0f&&setFrame!=-1.0f)
	{
		setFrame=0.0f;
	}
	if (setFrame>100000.0f)
	{
		setFrame=0.0f;
	}

	CGhoul2Info_v *ghoul2 = G2API_GetGhoul2Model(g2h);

	if (ghoul2 && (unsigned)modelIndex < ghoul2->size())
	{
		CGhoul2Info &ghlInfo = (*ghoul2)[modelIndex];

		// ensure we flush the cache
		ghlInfo.mSkelFrameNum = 0;
		return G2_Set_Bone_Anim(ghlInfo.mFileName, ghlInfo.mBlist, boneName, startFrame, endFrame, flags, animSpeed, currentTime, setFrame, blendTime);
	}
	return qfalse;
}

qboolean G2API_GetBoneAnim(CGhoul2Info *ghlInfo, const char *boneName, const int currentTime, float *currentFrame,
						   int *startFrame, int *endFrame, int *flags, float *animSpeed, int *modelList)
{
	assert(startFrame!=endFrame); //this is bad
	assert(startFrame!=flags); //this is bad
	assert(endFrame!=flags); //this is bad
	assert(currentFrame!=animSpeed); //this is bad
	if (ghlInfo)
	{
 		qboolean ret=G2_Get_Bone_Anim(ghlInfo->mFileName, ghlInfo->mBlist, boneName, currentTime, currentFrame,
			startFrame, endFrame, flags, animSpeed, modelList, ghlInfo->mModelindex);
		assert(*endFrame>0);
		assert(*endFrame<100000);
		assert(*startFrame>=0);
		assert(*startFrame<100000);
		assert(*currentFrame>=0.0f);
		assert(*currentFrame<100000.0f);
		if (*endFrame<1)
		{
			*endFrame=1;
		}
		if (*endFrame>100000)
		{
			*endFrame=1;
		}
		if (*startFrame<0)
		{
			*startFrame=0;
		}
		if (*startFrame>100000)
		{
			*startFrame=1;
		}
		if (*currentFrame<0.0f)
		{
			*currentFrame=0.0f;
		}
		if (*currentFrame>100000)
		{
			*currentFrame=1;
		}
		return ret;
	}
	return qfalse;
}

qboolean G2API_GetAnimRange(CGhoul2Info *ghlInfo, const char *boneName,	int *startFrame, int *endFrame)
{
	assert(startFrame!=endFrame); //this is bad
	if (ghlInfo)
	{
 		qboolean ret=G2_Get_Bone_Anim_Range(ghlInfo->mFileName, ghlInfo->mBlist, boneName, startFrame, endFrame);
		assert(*endFrame>0);
		assert(*endFrame<100000);
		assert(*startFrame>=0);
		assert(*startFrame<100000);
		if (*endFrame<1)
		{
			*endFrame=1;
		}
		if (*endFrame>100000)
		{
			*endFrame=1;
		}
		if (*startFrame<0)
		{
			*startFrame=0;
		}
		if (*startFrame>100000)
		{
			*startFrame=1;
		}
		return ret;
	}
	return qfalse;
}


qboolean G2API_PauseBoneAnim(CGhoul2Info *ghlInfo, const char *boneName, const int currentTime)
{
	if (ghlInfo)
	{
 		return G2_Pause_Bone_Anim(ghlInfo->mFileName, ghlInfo->mBlist, boneName, currentTime);
	}
	return qfalse;
}

qboolean	G2API_IsPaused(CGhoul2Info *ghlInfo, const char *boneName)
{
	if (ghlInfo)
	{
 		return G2_IsPaused(ghlInfo->mFileName, ghlInfo->mBlist, boneName);
	}
	return qfalse;
}

qboolean G2API_StopBoneAnimIndex(CGhoul2Info *ghlInfo, const int index)
{
	if (ghlInfo)
	{
 		return G2_Stop_Bone_Anim_Index(ghlInfo->mBlist, index);
	}
	return qfalse;
}

qboolean G2API_StopBoneAnim(CGhoul2Info *ghlInfo, const char *boneName)
{
	if (ghlInfo)
	{
 		return G2_Stop_Bone_Anim(ghlInfo->mFileName, ghlInfo->mBlist, boneName);
	}
	return qfalse;
}

qboolean G2API_SetBoneAnglesIndex(CGhoul2Info *ghlInfo, const int index, const vec3_t angles, const int flags,
							 const Eorientations yaw, const Eorientations pitch, const Eorientations roll,
							 qhandle_t *modelList, int blendTime, int currentTime)
{
	if (ghlInfo)
	{
		// ensure we flush the cache
		ghlInfo->mSkelFrameNum = 0;
		return G2_Set_Bone_Angles_Index( ghlInfo->mBlist, index, angles, flags, yaw, pitch, roll, modelList, ghlInfo->mModelindex, blendTime, currentTime);
	}
	return qfalse;
}

qboolean G2API_SetBoneAngles(g2handle_t g2h, const int modelIndex, const char *boneName, const vec3_t angles, const int flags,
							 const Eorientations up, const Eorientations left, const Eorientations forward,
							 qhandle_t *modelList, int blendTime, int currentTime )
{
	CGhoul2Info_v *ghoul2 = G2API_GetGhoul2Model(g2h);

	if (ghoul2 && (unsigned)modelIndex < ghoul2->size())
	{
		CGhoul2Info &ghlInfo = (*ghoul2)[modelIndex];

		// ensure we flush the cache
		ghlInfo.mSkelFrameNum = 0;
		return G2_Set_Bone_Angles(ghlInfo.mFileName, ghlInfo.mBlist, boneName, angles, flags, up, left, forward, modelList, ghlInfo.mModelindex, blendTime, currentTime);
	}
	return qfalse;
}

qboolean G2API_SetBoneAnglesMatrixIndex(CGhoul2Info *ghlInfo, const int index, const mdxaBone_t &matrix,
								   const int flags, qhandle_t *modelList, int blendTime, int currentTime)
{
	if (ghlInfo)
	{
		// ensure we flush the cache
		ghlInfo->mSkelFrameNum = 0;
		return G2_Set_Bone_Angles_Matrix_Index(ghlInfo->mBlist, index, matrix, flags, modelList, ghlInfo->mModelindex, blendTime, currentTime);
	}
	return qfalse;
}

qboolean G2API_SetBoneAnglesMatrix(CGhoul2Info *ghlInfo, const char *boneName, const mdxaBone_t &matrix,
								   const int flags, qhandle_t *modelList, int blendTime, int currentTime)
{
	if (ghlInfo)
	{
		// ensure we flush the cache
		ghlInfo->mSkelFrameNum = 0;
		return G2_Set_Bone_Angles_Matrix(ghlInfo->mFileName, ghlInfo->mBlist, boneName, matrix, flags, modelList, ghlInfo->mModelindex, blendTime, currentTime);
	}
	return qfalse;
}

qboolean G2API_StopBoneAnglesIndex(CGhoul2Info *ghlInfo, const int index)
{
	if (ghlInfo)
	{
		// ensure we flush the cache
		ghlInfo->mSkelFrameNum = 0;
 		return G2_Stop_Bone_Angles_Index(ghlInfo->mBlist, index);
	}
	return qfalse;
}

qboolean G2API_StopBoneAngles(CGhoul2Info *ghlInfo, const char *boneName)
{
	if (ghlInfo)
	{
		// ensure we flush the cache
		ghlInfo->mSkelFrameNum = 0;
 		return G2_Stop_Bone_Angles(ghlInfo->mFileName, ghlInfo->mBlist, boneName);
	}
	return qfalse;
}

qboolean G2API_RemoveBone(CGhoul2Info *ghlInfo, const char *boneName)
{
	if (ghlInfo)
	{
		// ensure we flush the cache
		ghlInfo->mSkelFrameNum = 0;
 		return G2_Remove_Bone(ghlInfo->mFileName, ghlInfo->mBlist, boneName);
	}
	return qfalse;
}

void G2API_AnimateG2Models(g2handle_t g2h, float speedVar)
{
	CGhoul2Info_v *ghoul2 = G2API_GetGhoul2Model(g2h);

	if (!ghoul2) {
		return;
	}

	// Walk the list and find all models that are active
	for (CGhoul2Info_v::iterator it = ghoul2->begin(); it != ghoul2->end(); ++it)
	{
		if (it->mModel)
		{
			G2_Animate_Bone_List(it->mBlist, speedVar);
		}
	}
}

qboolean G2API_RemoveBolt(CGhoul2Info *ghlInfo, const int index)
{
	if (ghlInfo)
	{
		// ensure we flush the cache
		ghlInfo->mSkelFrameNum = 0;
 		return G2_Remove_Bolt( ghlInfo->mBltlist, index);
	}
	return qfalse;
}

int G2API_AddBolt(g2handle_t g2h, const int modelIndex, const char *boneName)
{
	CGhoul2Info_v *ghoul2 = G2API_GetGhoul2Model(g2h);

	if (ghoul2 && (unsigned)modelIndex < ghoul2->size())
	{
		CGhoul2Info &ghlInfo = (*ghoul2)[modelIndex];

		// ensure we flush the cache
		ghlInfo.mSkelFrameNum = 0;
		return G2_Add_Bolt(ghlInfo.mFileName, ghlInfo.mBltlist, ghlInfo.mSlist, boneName);
	}
	return -1;
}

int G2API_AddBoltSurfNum(CGhoul2Info *ghlInfo, const int surfIndex)
{
	if (ghlInfo)
	{
		// ensure we flush the cache
		ghlInfo->mSkelFrameNum = 0;
		return G2_Add_Bolt_Surf_Num(ghlInfo->mFileName, ghlInfo->mBltlist, ghlInfo->mSlist, surfIndex);
	}
	return -1;
}


qboolean G2API_AttachG2Model(g2handle_t g2hFrom, int modelFrom, g2handle_t g2hTo, int toBoltIndex, int toModel)
{
	assert( toBoltIndex >= 0 );
	if ( toBoltIndex < 0 )
	{
		return qfalse;
	}

	CGhoul2Info_v *ghoul2From = G2API_GetGhoul2Model(g2hFrom);
	CGhoul2Info_v *ghoul2To = G2API_GetGhoul2Model(g2hTo);

	// make sure we have a model to attach, a model to attach to, and a bolt on that model
	if (ghoul2From &&
		ghoul2To &&
		(unsigned)modelFrom < ghoul2From->size() &&
		(unsigned)toModel < ghoul2To->size())
	{
		if ((*ghoul2To)[toModel].mBltlist[toBoltIndex].boneNumber != -1 ||
			(*ghoul2To)[toModel].mBltlist[toBoltIndex].surfaceNumber != -1)
		{
			// encode the bolt address into the model bolt link
			toModel &= MODEL_AND;
			toBoltIndex &= BOLT_AND;
			(*ghoul2From)[modelFrom].mModelBoltLink = (toModel << MODEL_SHIFT)  | (toBoltIndex << BOLT_SHIFT);
			// ensure we flush the cache
			(*ghoul2From)[modelFrom].mSkelFrameNum = 0;

			return qtrue;
		}
	}
	return qfalse;
}

void G2API_SetBoltInfo(g2handle_t g2h, int modelIndex, int boltInfo)
{
	CGhoul2Info_v *ghoul2 = G2API_GetGhoul2Model(g2h);

	if (ghoul2 && (unsigned)modelIndex < ghoul2->size())
	{
		(*ghoul2)[modelIndex].mModelBoltLink = boltInfo;
			// ensure we flush the cache
		(*ghoul2)[modelIndex].mSkelFrameNum = 0;
	}
}

qboolean G2API_DetachG2Model(CGhoul2Info *ghlInfo)
{
	if (ghlInfo)
	{
	   ghlInfo->mModelBoltLink = -1;
	   // ensure we flush the cache
	   ghlInfo->mSkelFrameNum = 0;
	   return qtrue;
	}
	return qfalse;
}

qboolean G2API_AttachEnt(int *boltInfo, CGhoul2Info *ghlInfoTo, int toBoltIndex, int entNum, int toModelNum)
{
	// make sure we have a model to attach, a model to attach to, and a bolt on that model
	if ((ghlInfoTo) && ((ghlInfoTo->mBltlist[toBoltIndex].boneNumber != -1) || (ghlInfoTo->mBltlist[toBoltIndex].surfaceNumber != -1)))
	{
		// encode the bolt address into the model bolt link
	   toModelNum &= MODEL_AND;
	   toBoltIndex &= BOLT_AND;
	   entNum &= ENTITY_AND;
	   *boltInfo =  (toBoltIndex << BOLT_SHIFT) | (toModelNum << MODEL_SHIFT) | (entNum << ENTITY_SHIFT);
	   return qtrue;
	}
	return qfalse;

}

void G2API_DetachEnt(int *boltInfo)
{
   *boltInfo = 0;
}

qboolean gG2_GBMNoReconstruct;
qboolean gG2_GBMUseSPMethod;

qboolean G2API_GetBoltMatrix_SPMethod(g2handle_t g2h, const int modelIndex, const int boltIndex, mdxaBone_t *matrix, const vec3_t angles,
							 const vec3_t position, const int frameNum, qhandle_t *modelList, const vec3_t scale )
{
	CGhoul2Info_v *ghoul2 = G2API_GetGhoul2Model(g2h);

	if (ghoul2 && (unsigned)modelIndex < ghoul2->size())
	{
		CGhoul2Info &ghlInfo = (*ghoul2)[modelIndex];

		//assert(boltIndex < ghlInfo->mBltlist.size());

		if ((unsigned)boltIndex < ghlInfo.mBltlist.size())
		{
			// make sure we have transformed the skeleton
			if (ghlInfo.mSkelFrameNum != frameNum)
			{
				// make sure it's initialized even if noreconstruct is on
				if (!gG2_GBMNoReconstruct || ghlInfo.mSkelFrameNum == -1)
				{
					G2_ConstructGhoulSkeleton(*ghoul2, frameNum, modelList, true, angles, position, scale, false);
				}
			}

			gG2_GBMNoReconstruct = qfalse;

			mdxaBone_t scaled;
			mdxaBone_t *use;
			use = &ghlInfo.mBltlist[boltIndex].position;

			if (scale[0]||scale[1]||scale[2])
			{
				scaled=*use;
				use=&scaled;

				// scale the bolt position by the scale factor for this model since at this point its still in model space
				if (scale[0])
				{
					scaled.matrix[0][3] *= scale[0];
				}
				if (scale[1])
				{
					scaled.matrix[1][3] *= scale[1];
				}
				if (scale[2])
				{
					scaled.matrix[2][3] *= scale[2];
				}
			}
			// pre generate the world matrix
			G2_GenerateWorldMatrix(angles, position);

			VectorNormalize((float*)use->matrix[0]);
			VectorNormalize((float*)use->matrix[1]);
			VectorNormalize((float*)use->matrix[2]);

			Multiply_3x4Matrix(matrix, &worldMatrix, use);
			return qtrue;
		}
	}
	return qfalse;
}

qboolean G2API_GetBoltMatrix(g2handle_t g2h, const int modelIndex, const int boltIndex, mdxaBone_t *matrix, const vec3_t angles, const vec3_t position, const int frameNum, qhandle_t *modelList, vec3_t scale )
{
	if (gG2_GBMUseSPMethod)
	{
		gG2_GBMUseSPMethod = qfalse;
		return G2API_GetBoltMatrix_SPMethod(g2h, modelIndex, boltIndex, matrix, angles, position, frameNum, modelList, scale);
	}

	CGhoul2Info_v *ghoul2 = G2API_GetGhoul2Model(g2h);

	if (ghoul2 && (unsigned)modelIndex < ghoul2->size())
	{
		CGhoul2Info &ghlInfo = (*ghoul2)[modelIndex];

		//assert(boltIndex < ghlInfo->mBltlist.size());

		if ((unsigned)boltIndex < ghlInfo.mBltlist.size())
		{
			// make sure we have transformed the skeleton
			if (ghlInfo.mSkelFrameNum != frameNum)
			{
				// make sure it's initialized even if noreconstruct is on
				if (!gG2_GBMNoReconstruct || ghlInfo.mSkelFrameNum == -1)
				{
					G2_ConstructGhoulSkeleton(*ghoul2, frameNum, modelList, true, angles, position, scale, false);
				}
			}

			gG2_GBMNoReconstruct = qfalse;

			mdxaBone_t scaled;
			mdxaBone_t *use;
			use = &ghlInfo.mBltlist[boltIndex].position;

			if (scale[0]||scale[1]||scale[2])
			{
				scaled=*use;
				use=&scaled;

				// scale the bolt position by the scale factor for this model since at this point its still in model space
				if (scale[0])
				{
					scaled.matrix[0][3] *= scale[0];
				}
				if (scale[1])
				{
					scaled.matrix[1][3] *= scale[1];
				}
				if (scale[2])
				{
					scaled.matrix[2][3] *= scale[2];
				}
			}
			// pre generate the world matrix
			G2_GenerateWorldMatrix(angles, position);

			// for some reason we get the end matrix rotated by 90 degrees
			mdxaBone_t	rotMat, tempMatrix;
			vec3_t		newangles = {0,270,0};
			Create_Matrix(newangles, &rotMat);
			// make the model space matrix we have for this bolt into a world matrix
//			Multiply_3x4Matrix(matrix, &worldMatrix,use);
			Multiply_3x4Matrix(&tempMatrix, &worldMatrix,use);
			vec3_t origin;
			origin[0] = tempMatrix.matrix[0][3];
			origin[1] = tempMatrix.matrix[1][3];
			origin[2] = tempMatrix.matrix[2][3];
			tempMatrix.matrix[0][3] = tempMatrix.matrix[1][3] = tempMatrix.matrix[2][3] = 0;
			Multiply_3x4Matrix(matrix, &tempMatrix, &rotMat);
			matrix->matrix[0][3] = origin[0];
			matrix->matrix[1][3] = origin[1];
			matrix->matrix[2][3] = origin[2];
			return qtrue;

		}
	}

	return qfalse;
}

void G2API_ListSurfaces(g2handle_t g2h, int modelIndex)
{
	CGhoul2Info_v *ghoul2 = G2API_GetGhoul2Model(g2h);

	if (ghoul2 && (unsigned)modelIndex < ghoul2->size())
	{
		G2_List_Model_Surfaces((*ghoul2)[modelIndex].mFileName);
	}
}

void G2API_ListBones(g2handle_t g2h, int modelIndex, int frame)
{
	CGhoul2Info_v *ghoul2 = G2API_GetGhoul2Model(g2h);

	if (ghoul2 && (unsigned)modelIndex < ghoul2->size())
	{
		G2_List_Model_Bones((*ghoul2)[modelIndex].mFileName, frame);
	}
}

// decide if we have Ghoul2 models associated with this ghoul list or not
qboolean G2API_HaveWeGhoul2Models(g2handle_t g2h)
{
	CGhoul2Info_v *ghoul2 = G2API_GetGhoul2Model(g2h);

	if (ghoul2)
	{
		for (CGhoul2Info_v::iterator it = ghoul2->begin(); it != ghoul2->end(); ++it)
		{
			if (it->mModelindex != -1)
			{
				return qtrue;
			}
		}
	}
	return qfalse;
}

// run through the Ghoul2 models and set each of the mModel values to the correct one from the cgs.gameModel offset lsit
void G2API_SetGhoul2ModelIndexes(g2handle_t g2h, qhandle_t *modelList, qhandle_t *skinList)
{
	return;
}


char *G2API_GetAnimFileNameIndex(qhandle_t modelIndex)
{
	model_t		*mod_m = R_GetModelByHandle(modelIndex);
	return mod_m->mdxm->animName;
}

/************************************************************************************************
 * G2API_GetAnimFileName
 *    obtains the name of a model's .gla (animation) file
 *
 * Input
 *	pointer to list of CGhoul2Info's, WraithID of specific model in that list
 *
 * Output
 *    true if a good filename was obtained, false otherwise
 *
 ************************************************************************************************/
qboolean G2API_GetAnimFileName(CGhoul2Info *ghlInfo, char **filename)
{
	if (ghlInfo)
	{
		return G2_GetAnimFileName(ghlInfo->mFileName, filename);
	}
	return qfalse;
}

/*
=======================
SV_QsortEntityNumbers
=======================
*/
static int QDECL QsortDistance( const void *a, const void *b ) {
	const float	&ea = ((const CCollisionRecord*)a)->mDistance;
	const float	&eb = ((const CCollisionRecord*)b)->mDistance;

	if ( ea < eb ) {
		return -1;
	}
	return 1;
}


void G2API_CollisionDetect(CollisionRecord_t *collRecMap, g2handle_t g2h, const vec3_t angles, const vec3_t position,
										  int frameNumber, int entNum, vec3_t rayStart, vec3_t rayEnd, vec3_t scale, CMiniHeap *G2VertSpace, int traceFlags, int useLod, float fRadius)
{

	CGhoul2Info_v *ghoul2 = G2API_GetGhoul2Model(g2h);

	if (ghoul2)
	{
		vec3_t	transRayStart, transRayEnd;

		// make sure we have transformed the whole skeletons for each model
		G2_ConstructGhoulSkeleton(*ghoul2, frameNumber, NULL, true, angles, position, scale, false);

		// pre generate the world matrix - used to transform the incoming ray
		G2_GenerateWorldMatrix(angles, position);

#ifdef G2_COLLISION_ENABLED
		G2VertSpace->ResetHeap();
#endif

		// now having done that, time to build the model
		G2_TransformModel(*ghoul2, frameNumber, scale, G2VertSpace, useLod);

		// model is built. Lets check to see if any triangles are actually hit.
		// first up, translate the ray to model space
		TransformAndTranslatePoint(rayStart, transRayStart, &worldMatrixInv);
		TransformAndTranslatePoint(rayEnd, transRayEnd, &worldMatrixInv);

		// now walk each model and check the ray against each poly - sigh, this is SO expensive. I wish there was a better way to do this.
		G2_TraceModels(*ghoul2, transRayStart, transRayEnd, collRecMap, entNum, traceFlags, useLod, fRadius);
#ifdef G2_COLLISION_ENABLED
		int i;
		for ( i = 0; i < MAX_G2_COLLISIONS && collRecMap[i].mEntityNum != -1; i ++ );

		// now sort the resulting array of collision records so they are distance ordered
		qsort( collRecMap, i,
			sizeof( CCollisionRecord ), QsortDistance );

#else
		// now sort the resulting array of collision records so they are distance ordered
		qsort( collRecMap, MAX_G2_COLLISIONS,
			sizeof( CCollisionRecord ), QsortDistance );
#endif

	}
}

qboolean G2API_SetGhoul2ModelFlags(CGhoul2Info *ghlInfo, const int flags)
{
  	if (ghlInfo)
	{
		ghlInfo->mFlags &= GHOUL2_NEWORIGIN;
		ghlInfo->mFlags |= flags;
		return qtrue;
	}
	return qfalse;
}

int G2API_GetGhoul2ModelFlags(CGhoul2Info *ghlInfo)
{
  	if (ghlInfo)
	{
		return (ghlInfo->mFlags & ~GHOUL2_NEWORIGIN);
	}
	return 0;
}

// given a boltmatrix, return in vec a normalised vector for the axis requested in flags
void G2API_GiveMeVectorFromMatrix(mdxaBone_t *boltMatrix, Eorientations flags, vec3_t vec)
{
	switch (flags)
	{
	case ORIGIN:
		vec[0] = boltMatrix->matrix[0][3];
		vec[1] = boltMatrix->matrix[1][3];
		vec[2] = boltMatrix->matrix[2][3];
		break;
	case POSITIVE_Y:
		vec[0] = boltMatrix->matrix[0][1];
		vec[1] = boltMatrix->matrix[1][1];
		vec[2] = boltMatrix->matrix[2][1];
 		break;
	case POSITIVE_X:
		vec[0] = boltMatrix->matrix[0][0];
		vec[1] = boltMatrix->matrix[1][0];
		vec[2] = boltMatrix->matrix[2][0];
		break;
	case POSITIVE_Z:
		vec[0] = boltMatrix->matrix[0][2];
		vec[1] = boltMatrix->matrix[1][2];
		vec[2] = boltMatrix->matrix[2][2];
		break;
	case NEGATIVE_Y:
		vec[0] = -boltMatrix->matrix[0][1];
		vec[1] = -boltMatrix->matrix[1][1];
		vec[2] = -boltMatrix->matrix[2][1];
		break;
	case NEGATIVE_X:
		vec[0] = -boltMatrix->matrix[0][0];
		vec[1] = -boltMatrix->matrix[1][0];
		vec[2] = -boltMatrix->matrix[2][0];
		break;
	case NEGATIVE_Z:
		vec[0] = -boltMatrix->matrix[0][2];
		vec[1] = -boltMatrix->matrix[1][2];
		vec[2] = -boltMatrix->matrix[2][2];
		break;
	}
}



// copy a model from one ghoul2 instance to another, and reset the root surface on the new model if need be
// NOTE if modelIndex = -1 then copy all the models
// returns the last model index in destination.  -1 equals nothing copied.
int G2API_CopyGhoul2Instance(g2handle_t g2hFrom, g2handle_t g2hTo, int modelIndex)
{
	CGhoul2Info_v *g2From = G2API_GetGhoul2Model(g2hFrom);
	CGhoul2Info_v *g2To = G2API_GetGhoul2Model(g2hTo);

	if (!g2From || !g2To) {
		return -1;
	}
	if (modelIndex != -1) {
		if (g2From->size() <= (unsigned)modelIndex) {
			return -1;
		}
	}

	CGhoul2Info_v::iterator from = g2From->begin();
	CGhoul2Info_v::iterator to = g2From->end();

	// determing if we are only copying one model or not
	if (modelIndex > -1)
	{
		from += modelIndex;
		to = from + 1;
	}

	// now copy the models
	for (CGhoul2Info_v::iterator dest = g2To->begin(); from != to; ++from)
	{
		// find a free spot in the list
		for (; dest != g2To->end(); ++dest)
		{
			if (dest->mModelindex == -1)
			{
				// Copy model to clear position
				*dest = *from;
				break;
			}
		}

		if (dest == g2To->end())
		{
			// didn't find a spare slot, so new ones to add
			g2To->push_back(*from);
			// may be invalidated
			dest = g2To->end() - 1;
		}
	}

	// fau - perhaps needs forcereconstruct like G2API_CopyGhoul2Instance

	return -1;
}

void G2API_CopySpecificG2Model(g2handle_t g2hFrom, int modelFrom, g2handle_t g2hTo, int modelTo)
{
	CGhoul2Info_v *ghoul2From = G2API_GetGhoul2Model(g2hFrom);
	CGhoul2Info_v *ghoul2To = G2API_GetGhoul2Model(g2hTo);

	qboolean forceReconstruct = qfalse;

	// have we real ghoul2 models yet?
	if (ghoul2From && ghoul2To)
	{
		// assume we actually have a model to copy from
		if ((unsigned)modelFrom < ghoul2From->size() && 0 <= modelTo)
		{
			// if we don't have enough models on the to side, resize us so we do
			if (ghoul2To->size() <= (unsigned)modelTo)
			{
				ghoul2To->resize(modelTo + 1);
				forceReconstruct = qtrue;
			}
			// do the copy
			(*ghoul2To)[modelTo] = (*ghoul2From)[modelFrom];

			if (forceReconstruct)
			{ //rww - we should really do this shouldn't we? If we don't mark a reconstruct after this,
			  //and we do a GetBoltMatrix in the same frame, it doesn't reconstruct the skeleton and returns
			  //a completely invalid matrix

				// really all of them? stay safe
				for (CGhoul2Info_v::iterator it = ghoul2To->begin(); it != ghoul2To->end(); ++it) {
					it->mSkelFrameNum = 0;
				}
			}
		}
	}
}

// This version will automatically copy everything about this model, and make a new one if necessary.
void G2API_DuplicateGhoul2Instance(g2handle_t g2hFrom, g2handle_t *g2hToPtr)
{
	CGhoul2Info_v *g2From = G2API_GetGhoul2Model(g2hFrom);
	g2handle_t g2hTo;

	if (!g2From || *g2hToPtr) {
		return;
	}

	*g2hToPtr = g2hTo = nextGhoul2Handle++;
	ghoultable[RicksCrazyOnServer][g2hTo] = CGhoul2Info_v();

	G2API_CopyGhoul2Instance(g2hFrom, g2hTo, -1);
	return;
}


char *G2API_GetSurfaceName(CGhoul2Info *ghlInfo, int surfNumber)
{
	static char noSurface[1] = "";
	model_t	*mod = R_GetModelByHandle(RE_RegisterModel(ghlInfo->mFileName));
	mdxmSurface_t		*surf = 0;
	mdxmSurfHierarchy_t	*surfInfo = 0;

	surf = (mdxmSurface_t *)G2_FindSurface((void *)mod, surfNumber, 0);
	if (surf)
	{
		mdxmHierarchyOffsets_t	*surfIndexes = (mdxmHierarchyOffsets_t *)((byte *)mod->mdxm + sizeof(mdxmHeader_t));
		surfInfo = (mdxmSurfHierarchy_t *)((byte *)surfIndexes + surfIndexes->offsets[surf->thisSurfaceIndex]);
		return surfInfo->name;
	}

	return noSurface;
}


int	G2API_GetSurfaceIndex(CGhoul2Info *ghlInfo, const char *surfaceName)
{
	if (ghlInfo)
	{
		return G2_GetSurfaceIndex(ghlInfo->mFileName, surfaceName);
	}
	return -1;
}

char *G2API_GetGLAName(g2handle_t g2h, int modelIndex)
{
	CGhoul2Info_v *ghoul2 = G2API_GetGhoul2Model(g2h);

	if (ghoul2 && (unsigned)modelIndex < ghoul2->size())
	{
		model_t	*mod = R_GetModelByHandle(RE_RegisterModel((*ghoul2)[modelIndex].mFileName));
		return mod->mdxm->animName;
	}
	return NULL;
}

qboolean G2API_SetNewOrigin(g2handle_t g2h, const int boltIndex)
{
	CGhoul2Info_v *ghoul2 = G2API_GetGhoul2Model(g2h);

	if (ghoul2 && !ghoul2->empty())
	{
		CGhoul2Info &ghlInfo = ghoul2->front();

		ghlInfo.mNewOrigin = boltIndex;
		ghlInfo.mFlags |= GHOUL2_NEWORIGIN;
		// ensure we flush the cache
		ghlInfo.mSkelFrameNum = 0;

		return qtrue;
	}
	return qfalse;
}

int G2API_GetBoneIndex(CGhoul2Info *ghlInfo, const char *boneName)
{
	if (ghlInfo)
	{
		return G2_Get_Bone_Index(ghlInfo, boneName);
	}
	return -1;
}

qboolean G2API_SaveGhoul2Models(g2handle_t g2h, char **buffer, int *size)
{
	CGhoul2Info_v *ghoul2 = G2API_GetGhoul2Model(g2h);

	if (!ghoul2) {
		return qfalse;
	}

	return G2_SaveGhoul2Models(*ghoul2, buffer, size);
}

void G2API_LoadGhoul2Models(g2handle_t g2h, char *buffer)
{
	CGhoul2Info_v *ghoul2 = G2API_GetGhoul2Model(g2h);

	if (!ghoul2) {
		return;
	}

	G2_LoadGhoul2Model(*ghoul2, buffer);
}

void G2API_FreeSaveBuffer(char *buffer)
{
	Z_Free(buffer);
}

// this is kinda sad, but I need to call the destructor in this module (exe), not the game.dll...
//
void G2API_LoadSaveCodeDestructGhoul2Info(g2handle_t g2h)
{
	CGhoul2Info_v *ghoul2 = G2API_GetGhoul2Model(g2h);

	ghoul2->~CGhoul2Info_v();	// so I can load junk over it then memset to 0 without orphaning
}
