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

#include <set>
#include <algorithm>

// #ifdef assert
// #	undef assert
// #	define assert(x) ((void)0)
// #endif

extern mdxaBone_t		worldMatrix;
extern mdxaBone_t		worldMatrixInv;

// normally a ghoul2 handle is a 4 byte pointer directly to CGhoul2Info_v*
// which is saved and used inside the VM, but on 64 bit we have 8 byte pointers (can't be stored inside int32_t^^)
// solution is to use it as an actual handle and save them in the (already there anyway) table

struct ghoul2entry_t {
	CGhoul2Info_v *ghoul2info;
	qhandle_t handle;

	ghoul2entry_t(CGhoul2Info_v *ghoul2info = NULL, qhandle_t handle = 0) : ghoul2info(ghoul2info), handle(handle) { }

	bool operator<(const ghoul2entry_t &rhs) const {
		return handle < rhs.handle;
	}

	bool operator==(const ghoul2entry_t &rhs) const {
		return (( handle > 0 && rhs.handle > 0 && (handle == rhs.handle) ) || (ghoul2info && rhs.ghoul2info && (ghoul2info == rhs.ghoul2info)));
	}
};

set<ghoul2entry_t>	ghoultable[2];

qhandle_t currghoul2Handle = 1;

// logarithmic complexity
CGhoul2Info_v *GetGhoul2InfovByHandle(qhandle_t handle) {
	ghoul2entry_t ventry(NULL, handle);

	set<ghoul2entry_t>::iterator i = ghoultable[0].find(ventry);
	if (i != ghoultable[0].end()) {
		return i->ghoul2info;
	} else {
		i = ghoultable[1].find(ventry);
		if (i != ghoultable[1].end()) {
			return i->ghoul2info;
		}
	}

	return NULL;
}

// linear complexity
qhandle_t GetHandleByGhoul2Infov(CGhoul2Info_v *ghoul2inf) {
	set<ghoul2entry_t>::iterator i = std::find(ghoultable[0].begin(), ghoultable[0].end(), ghoul2inf);
	if (i != ghoultable[0].end()) {
		return i->handle;
	} else {
		i = std::find(ghoultable[1].begin(), ghoultable[1].end(), ghoul2inf);
		if (i != ghoultable[1].end()) {
			return i->handle;
		}
	}

	return 0;
}

bool RicksCrazyOnServer=true;

void FixGhoul2InfoLeaks(bool clearClient,bool clearServer)
{
	if (clearClient && ghoultable[0].size())
	{
		set<ghoul2entry_t>::iterator i;
		for (i = ghoultable[0].begin(); i != ghoultable[0].end(); ++i) {
			delete i->ghoul2info;
		}
		ghoultable[0].clear();
	}

	if (clearServer && ghoultable[1].size())
	{
		set<ghoul2entry_t>::iterator i;
		for (i = ghoultable[1].begin(); i != ghoultable[1].end(); ++i) {
			delete i->ghoul2info;
		}
		ghoultable[1].clear();
	}
}


class EasyCleaner
{
public:
	~EasyCleaner()
	{
		FixGhoul2InfoLeaks(true,true);
		currghoul2Handle = 1;
	}
};

static EasyCleaner TheEasyCleaner;

// this is the ONLY function to read entity states directly
void G2API_VM_CleanGhoul2Models(qhandle_t *handle) {
	CGhoul2Info_v *ghoul2Ptr;

	ghoul2Ptr = GetGhoul2InfovByHandle(*handle);
	G2API_CleanGhoul2Models(&ghoul2Ptr);
	*handle = GetHandleByGhoul2Infov(ghoul2Ptr);
}

void G2API_CleanGhoul2Models(CGhoul2Info_v **ghoul2Ptr) {
	if (*ghoul2Ptr) {
		set<ghoul2entry_t>::iterator i = std::find(ghoultable[0].begin(), ghoultable[0].end(), *ghoul2Ptr);
		if (i != ghoultable[0].end()) {
			delete i->ghoul2info;
			ghoultable[0].erase(i);
		} else {
			i = std::find(ghoultable[1].begin(), ghoultable[1].end(), *ghoul2Ptr);
			if (i != ghoultable[1].end()) {
				delete i->ghoul2info;
				ghoultable[1].erase(i);
			}
		}

		*ghoul2Ptr = NULL;
	}
}

qhandle_t G2API_PrecacheGhoul2Model(const char *fileName)
{
	return RE_RegisterModel(fileName);
}

void CL_InitRef( void );
void R_Register( void );

// initialise all that needs to be on a new Ghoul II model
int G2API_VM_InitGhoul2Model(qhandle_t *hghoul2, const char *fileName, int modelIndex, qhandle_t customSkin,
	qhandle_t customShader, int modelFlags, int lodBias) {
	CGhoul2Info_v *ghoul2Ptr;
	int ret;

	ghoul2Ptr = GetGhoul2InfovByHandle(*hghoul2);
	ret = G2API_InitGhoul2Model(&ghoul2Ptr, fileName, modelIndex, customSkin, customShader, modelFlags, lodBias);

	*hghoul2 = GetHandleByGhoul2Infov(ghoul2Ptr);
	return ret;
}

int G2API_InitGhoul2Model(CGhoul2Info_v **ghoul2Ptr, const char *fileName, int modelIndex, qhandle_t customSkin,
						  qhandle_t customShader, int modelFlags, int lodBias)
{
	int				model = -1;
	CGhoul2Info		newModel;

	// are we actually asking for a model to be loaded.
	if (!(strlen(fileName)))
	{
		assert(0);
		return -1;
	}

	if (!(*ghoul2Ptr))
	{
		ghoul2entry_t entry;
		*ghoul2Ptr = new CGhoul2Info_v;
		entry.ghoul2info = *ghoul2Ptr;
		entry.handle = currghoul2Handle++;

		if (RicksCrazyOnServer) {
			ghoultable[1].insert(entry);
		} else {
			ghoultable[0].insert(entry);
		}
	}

	CGhoul2Info_v &ghoul2 = *(*ghoul2Ptr);

	// find a free spot in the list
	for (model=0; model< ghoul2.size(); model++)
	{
		if (ghoul2[model].mModelindex == -1)
		{
			// this is only valid and used on the game side. Client side ignores this
			ghoul2[model].mModelindex = modelIndex;
				// on the game side this is valid. On the client side it is valid only after it has been filled in by trap_G2_SetGhoul2ModelIndexes
			ghoul2[model].mModel = RE_RegisterModel(fileName);
			model_t		*mod_m = R_GetModelByHandle(ghoul2[model].mModel);
			if (mod_m->type == MOD_BAD)
			{
				return -1;
			}

			// init what is necessary for this ghoul2 model
			// fau - what about remaining fields?
			G2_Init_Bone_List(ghoul2[model].mBlist);
			G2_Init_Bolt_List(ghoul2[model].mBltlist);
			ghoul2[model].mCustomShader = customShader;
			ghoul2[model].mCustomSkin = customSkin;
			strcpy(ghoul2[model].mFileName, fileName);
			ghoul2[model].mCreationID = modelFlags;
			ghoul2[model].mLodBias = lodBias;
			ghoul2[model].mAnimFrameDefault = 0;
			ghoul2[model].mFlags = 0;

			ghoul2[model].mSkelFrameNum = -1;
			ghoul2[model].mMeshFrameNum = -1;

			// we aren't attached to anyone upfront
			ghoul2[model].mModelBoltLink = -1;
			return model;
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
			G2API_CleanGhoul2Models(ghoul2Ptr);
			delete *ghoul2Ptr;
			*ghoul2Ptr = 0;
		}
		return -1;
	}

	// init what is necessary for this ghoul2 model
	G2_Init_Bone_List(newModel.mBlist);
	G2_Init_Bolt_List(newModel.mBltlist);
	newModel.mCustomShader = customShader;
	newModel.mCustomSkin = customSkin;
	strcpy(newModel.mFileName, fileName);
	newModel.mCreationID = modelFlags;
	newModel.mLodBias = lodBias;
	newModel.mAnimFrameDefault = 0;
	newModel.mFlags = 0;

	// we aren't attached to anyone upfront
	newModel.mModelBoltLink = -1;

	// insert into list.
	model = (int)ghoul2.size();
	ghoul2.push_back(newModel);

	return model;
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

qboolean G2API_SetSurfaceOnOff(CGhoul2Info_v *ghoul2, const char *surfaceName, const int flags)
{
	if (ghoul2 && ghoul2->size() > 0) {
		CGhoul2Info &ghlInfo = ghoul2->at(0);

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

qboolean G2API_SetRootSurface(CGhoul2Info_v *ghoul2, const int modelIndex, const char *surfaceName)
{
	if (!ghoul2) {
		return qfalse;
	}

	return G2_SetRootSurface(*ghoul2, modelIndex, surfaceName);
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

qboolean G2API_HasGhoul2ModelOnIndex(CGhoul2Info_v **ghlRemove, const int modelIndex)
{
	CGhoul2Info_v &ghlInfo = **ghlRemove;

	if (!ghlInfo.size() || (ghlInfo.size() <= modelIndex) || (ghlInfo[modelIndex].mModelindex == -1))
	{
		return qfalse;
	}

	return qtrue;
}

qboolean G2API_VM_RemoveGhoul2Model(qhandle_t *ghlRemove, const int modelIndex) {
	CGhoul2Info_v *ghoul2Ptr;
	qboolean ret;

	ghoul2Ptr = GetGhoul2InfovByHandle(*ghlRemove);
	ret = G2API_RemoveGhoul2Model(&ghoul2Ptr, modelIndex);

	*ghlRemove = GetHandleByGhoul2Infov(ghoul2Ptr);
	return ret;
}

qboolean G2API_RemoveGhoul2Model(CGhoul2Info_v **ghlRemove, const int modelIndex)
{
	assert(ghlRemove && *ghlRemove);
	CGhoul2Info_v &ghlInfo = **ghlRemove;

	// sanity check
	if (!ghlInfo.size() || (ghlInfo.size() <= modelIndex) || (ghlInfo[modelIndex].mModelindex == -1))
	{
		// if we hit this assert then we are trying to delete a ghoul2 model on a ghoul2 instance that
		// one way or another is already gone.
		assert(0);
		return qfalse;
	}

	if (ghlInfo.size() > modelIndex)
	{
		// clear out the vectors this model used.
		ghlInfo[modelIndex].mBlist.clear();
		ghlInfo[modelIndex].mBltlist.clear();
		ghlInfo[modelIndex].mSlist.clear();

	   	 // set us to be the 'not active' state
		ghlInfo[modelIndex].mModelindex = -1;

		int newSize = (int)ghlInfo.size();
		// now look through the list from the back and see if there is a block of -1's we can resize off the end of the list
		for (int i=(int)ghlInfo.size()-1; i>-1; i--)
		{
			if (ghlInfo[i].mModelindex == -1)
			{
				newSize = i;
			}
			// once we hit one that isn't a -1, we are done.
			else
			{
				break;
			}
		}
		// do we need to resize?
		if (newSize != ghlInfo.size())
		{
			// yes, so lets do it
			ghlInfo.resize(newSize);
		}

		// if we are not using any space, just delete the ghoul2 vector entirely
		if (!ghlInfo.size())
		{
			delete *ghlRemove;
			*ghlRemove = NULL;
		}
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

qboolean G2API_SetBoneAnim(CGhoul2Info_v *ghoul2, const int modelIndex, const char *boneName, const int AstartFrame, const int AendFrame, const int flags, const float animSpeed, const int currentTime, const float AsetFrame, const int blendTime)
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
	if (ghoul2 && ghoul2->size() > modelIndex)
	{
		CGhoul2Info *ghlInfo = &ghoul2->at(modelIndex);
		if (ghlInfo)
		{
			// ensure we flush the cache
			ghlInfo->mSkelFrameNum = 0;
 			return G2_Set_Bone_Anim(ghlInfo->mFileName, ghlInfo->mBlist, boneName, startFrame, endFrame, flags, animSpeed, currentTime, setFrame, blendTime);
		}
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

qboolean G2API_SetBoneAngles(CGhoul2Info_v *ghoul2, const int modelIndex, const char *boneName, const vec3_t angles, const int flags,
							 const Eorientations up, const Eorientations left, const Eorientations forward,
							 qhandle_t *modelList, int blendTime, int currentTime )
{
	if (ghoul2 && ghoul2->size() > modelIndex)
	{
		CGhoul2Info *ghlInfo = &ghoul2->at(modelIndex);
		if (ghlInfo)
		{
				// ensure we flush the cache
			ghlInfo->mSkelFrameNum = 0;
			return G2_Set_Bone_Angles(ghlInfo->mFileName, ghlInfo->mBlist, boneName, angles, flags, up, left, forward, modelList, ghlInfo->mModelindex, blendTime, currentTime);
		}
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

void G2API_AnimateG2Models(CGhoul2Info_v *ghoul2, float speedVar)
{
	int model;

	if (!ghoul2) {
		return;
	}

	// Walk the list and find all models that are active
	for (model=0; model< ghoul2->size(); model++)
	{
		if (ghoul2->at(model).mModel)
		{
			G2_Animate_Bone_List(*ghoul2, speedVar, model);
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

int G2API_AddBolt(CGhoul2Info_v *ghoul2, const int modelIndex, const char *boneName)
{
	if (ghoul2 && ghoul2->size() > modelIndex)
	{
		CGhoul2Info *ghlInfo = &ghoul2->at(modelIndex);
		if (ghlInfo)
		{
			// ensure we flush the cache
			ghlInfo->mSkelFrameNum = 0;
			return G2_Add_Bolt(ghlInfo->mFileName, ghlInfo->mBltlist, ghlInfo->mSlist, boneName);
		}
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


qboolean G2API_AttachG2Model(CGhoul2Info_v *ghoul2From, int modelFrom, CGhoul2Info_v *ghoul2To, int toBoltIndex, int toModel)
{
	assert( toBoltIndex >= 0 );
	if ( toBoltIndex < 0 )
	{
		return qfalse;
	}
	// make sure we have a model to attach, a model to attach to, and a bolt on that model
	if (ghoul2From &&
		ghoul2To &&
		(ghoul2From->size() > modelFrom) &&
		(ghoul2To->size() > toModel) &&
		((ghoul2To->at(toModel).mBltlist[toBoltIndex].boneNumber != -1) || (ghoul2To->at(toModel).mBltlist[toBoltIndex].surfaceNumber != -1)))
	{
		// encode the bolt address into the model bolt link
	   toModel &= MODEL_AND;
	   toBoltIndex &= BOLT_AND;
	   ghoul2From->at(modelFrom).mModelBoltLink = (toModel << MODEL_SHIFT)  | (toBoltIndex << BOLT_SHIFT);
	   // ensure we flush the cache
	   ghoul2From->at(modelFrom).mSkelFrameNum = 0;
	   return qtrue;
	}
	return qfalse;
}

void G2API_SetBoltInfo(CGhoul2Info_v *ghoul2, int modelIndex, int boltInfo)
{
	if (ghoul2)
	{
		if (ghoul2->size() > modelIndex)
		{
			ghoul2->at(modelIndex).mModelBoltLink = boltInfo;
			// ensure we flush the cache
			ghoul2->at(modelIndex).mSkelFrameNum = 0;
		}
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

qboolean G2API_GetBoltMatrix_SPMethod(CGhoul2Info_v *ghoul2, const int modelIndex, const int boltIndex, mdxaBone_t *matrix, const vec3_t angles,
							 const vec3_t position, const int frameNum, qhandle_t *modelList, const vec3_t scale )
{
	if (ghoul2 && (ghoul2->size() > modelIndex))
	{
		CGhoul2Info *ghlInfo = &ghoul2->at(modelIndex);

		//assert(boltIndex < ghlInfo->mBltlist.size());

		if (ghlInfo && (boltIndex < ghlInfo->mBltlist.size()) && boltIndex >= 0 )
		{
			// make sure we have transformed the skeleton
			if (ghlInfo->mSkelFrameNum != frameNum)
			{
				// make sure it's initialized even if noreconstruct is on
				if (!gG2_GBMNoReconstruct || ghlInfo->mSkelFrameNum == -1)
				{
					G2_ConstructGhoulSkeleton(*ghoul2, frameNum, modelList, true, angles, position, scale, false);
				}
			}

			gG2_GBMNoReconstruct = qfalse;

			mdxaBone_t scaled;
			mdxaBone_t *use;
			use=&ghlInfo->mBltlist[boltIndex].position;

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

qboolean G2API_GetBoltMatrix(CGhoul2Info_v *ghoul2, const int modelIndex, const int boltIndex, mdxaBone_t *matrix, const vec3_t angles, const vec3_t position, const int frameNum, qhandle_t *modelList, vec3_t scale )
{
	if (gG2_GBMUseSPMethod)
	{
		gG2_GBMUseSPMethod = qfalse;
		return G2API_GetBoltMatrix_SPMethod(ghoul2, modelIndex, boltIndex, matrix, angles, position, frameNum, modelList, scale);
	}

	if (ghoul2 && (ghoul2->size() > modelIndex))
	{
		CGhoul2Info *ghlInfo = &ghoul2->at(modelIndex);

		//assert(boltIndex < ghlInfo->mBltlist.size());

		if (ghlInfo && (boltIndex < ghlInfo->mBltlist.size()) && boltIndex >= 0 )
		{
			// make sure we have transformed the skeleton
			if (ghlInfo->mSkelFrameNum != frameNum)
			{
				// make sure it's initialized even if noreconstruct is on
				if (!gG2_GBMNoReconstruct || ghlInfo->mSkelFrameNum == -1)
				{
					G2_ConstructGhoulSkeleton(*ghoul2, frameNum, modelList, true, angles, position, scale, false);
				}
			}

			gG2_GBMNoReconstruct = qfalse;

			mdxaBone_t scaled;
			mdxaBone_t *use;
			use=&ghlInfo->mBltlist[boltIndex].position;

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

void G2API_ListSurfaces(CGhoul2Info *ghlInfo)
{
	if (ghlInfo)
	{
		G2_List_Model_Surfaces(ghlInfo->mFileName);
	}
}

void G2API_ListBones(CGhoul2Info *ghlInfo, int frame)
{
	if (ghlInfo)
	{
		G2_List_Model_Bones(ghlInfo->mFileName, frame);
	}
}

// decide if we have Ghoul2 models associated with this ghoul list or not
qboolean G2API_HaveWeGhoul2Models(CGhoul2Info_v *ghoul2)
{
	int i;
	if (ghoul2)
	{
		for (i=0; i < ghoul2->size(); i++)
		{
			if (ghoul2->at(i).mModelindex != -1)
			{
				return qtrue;
			}
		}
	}
	return qfalse;
}

// run through the Ghoul2 models and set each of the mModel values to the correct one from the cgs.gameModel offset lsit
void G2API_SetGhoul2ModelIndexes(CGhoul2Info_v *ghoul2, qhandle_t *modelList, qhandle_t *skinList)
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


void G2API_CollisionDetect(CollisionRecord_t *collRecMap, CGhoul2Info_v *ghoul2, const vec3_t angles, const vec3_t position,
										  int frameNumber, int entNum, vec3_t rayStart, vec3_t rayEnd, vec3_t scale, CMiniHeap *G2VertSpace, int traceFlags, int useLod, float fRadius)
{

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
int G2API_CopyGhoul2Instance(CGhoul2Info_v *g2From, CGhoul2Info_v *g2To, int modelIndex)
{
	int returnval=-1;

	if (!g2From || !g2To) {
		return -1;
	}

	int	i, model;
	int	from = 0;
	int	to = (int)g2From->size();

	// determing if we are only copying one model or not
	if (modelIndex != -1)
	{
		from = modelIndex;
		to = modelIndex + 1;
	}

	model = 0;
	// now copy the models
	for (i=from; i<to; i++)
	{
		// find a free spot in the list
		for (; model< g2To->size(); model++)
		{
			if (g2To->at(model).mModelindex == -1)
			{
				// Copy model to clear position
				g2To->at(model) = g2From->at(i);

				break;
			}
		}

		if (model >= g2To->size())
		{	// didn't find a spare slot, so new ones to add
			break;
		}
	}

	// fau - perhaps needs forcereconstruct like G2API_CopyGhoul2Instance
	if (i < to)
	{	// add in any other ones to the end
		model = (int)g2To->size();
		g2To->resize(model + to - i);

		for(;i<to;i++)
		{
			g2To->at(model) = g2From->at(i);
			model++;
		}
	}

	return returnval;
}

void G2API_CopySpecificG2Model(CGhoul2Info_v *ghoul2From, int modelFrom, CGhoul2Info_v *ghoul2To, int modelTo)
{
	qboolean forceReconstruct = qfalse;

	// have we real ghoul2 models yet?
	if (ghoul2From && ghoul2To)
	{
		// assume we actually have a model to copy from
		if (ghoul2From->size() > modelFrom)
		{
			// if we don't have enough models on the to side, resize us so we do
			if (ghoul2To->size() <= modelTo)
			{
				ghoul2To->resize(modelTo + 1);
				forceReconstruct = qtrue;
			}
			// do the copy
			ghoul2To->at(modelTo) = ghoul2From->at(modelFrom);

			if (forceReconstruct)
			{ //rww - we should really do this shouldn't we? If we don't mark a reconstruct after this,
			  //and we do a GetBoltMatrix in the same frame, it doesn't reconstruct the skeleton and returns
			  //a completely invalid matrix

				// really all of them? stay safe
				for (int i = 0; i < ghoul2To->size(); i++) {
					ghoul2To->at(i).mSkelFrameNum = 0;
				}
			}
		}
	}
}

// This version will automatically copy everything about this model, and make a new one if necessary.
void G2API_VM_DuplicateGhoul2Instance(CGhoul2Info_v *g2From, qhandle_t *g2To) {
	CGhoul2Info_v *toptr;

	toptr = GetGhoul2InfovByHandle(*g2To);
	G2API_DuplicateGhoul2Instance(g2From, &toptr);
	*g2To = GetHandleByGhoul2Infov(toptr);
}

void G2API_DuplicateGhoul2Instance(CGhoul2Info_v *g2From, CGhoul2Info_v **g2To)
{
	if (!g2From || *g2To) {
		return;
	}

	ghoul2entry_t entry;
	*g2To = new CGhoul2Info_v;
	entry.ghoul2info = *g2To;
	entry.handle = currghoul2Handle++;

	if (RicksCrazyOnServer) {
		ghoultable[1].insert(entry);
	} else {
		ghoultable[0].insert(entry);
	}

	G2API_CopyGhoul2Instance(g2From, *g2To, -1);
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

char *G2API_GetGLAName(CGhoul2Info_v *ghoul2, int modelIndex)
{
	if (ghoul2 && (ghoul2->size() > modelIndex))
	{
		model_t	*mod = R_GetModelByHandle(RE_RegisterModel(ghoul2->at(modelIndex).mFileName));
		return mod->mdxm->animName;
	}
	return NULL;
}

qboolean G2API_SetNewOrigin(CGhoul2Info_v *ghoul2, const int boltIndex)
{
	CGhoul2Info *ghlInfo = NULL;

	if (ghoul2 && ghoul2->size() > 0)
	{
		ghlInfo = &ghoul2->at(0);
	}

	if (ghlInfo)
	{
		ghlInfo->mNewOrigin = boltIndex;
		ghlInfo->mFlags |= GHOUL2_NEWORIGIN;
		// ensure we flush the cache
		ghlInfo->mSkelFrameNum = 0;
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

qboolean G2API_SaveGhoul2Models(CGhoul2Info_v *ghoul2, char **buffer, int *size)
{
	if (!ghoul2) {
		return qfalse;
	}

	return G2_SaveGhoul2Models(*ghoul2, buffer, size);
}

void G2API_LoadGhoul2Models(CGhoul2Info_v *ghoul2, char *buffer)
{
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
void G2API_LoadSaveCodeDestructGhoul2Info(CGhoul2Info_v *ghoul2)
{
	ghoul2->~CGhoul2Info_v();	// so I can load junk over it then memset to 0 without orphaning
}
