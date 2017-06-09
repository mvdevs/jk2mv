// defines to setup the

#include "ghoul2_shared.h"

class CMiniHeap;

// internal surface calls  G2_surfaces.cpp
qboolean	G2_SetSurfaceOnOff (const char *fileName, surfaceInfo_v &slist, const char *surfaceName, const int offFlags);
int			G2_IsSurfaceOff (const char *fileName, surfaceInfo_v &slist, const char *surfaceName);
qboolean	G2_SetRootSurface(g2handle_t g2h, CGhoul2Info_v &ghoul2, const int modelIndex, const char *surfaceName);
int			G2_AddSurface(CGhoul2Info *ghoul2, int surfaceNumber, int polyNumber, float BarycentricI, float BarycentricJ, int lod );
qboolean	G2_RemoveSurface(surfaceInfo_v &slist, const int index);
surfaceInfo_t *G2_FindOverrideSurface(int surfaceNum, surfaceInfo_v &surfaceList);
int			G2_IsSurfaceLegal(void *mod, const char *surfaceName, int *flags);
int			G2_GetParentSurface(const char *fileName, const int index);
int			G2_GetSurfaceIndex(const char *fileName, const char *surfaceName);
int			G2_IsSurfaceRendered(const char *fileName, const char *surfaceName, surfaceInfo_v &slist);

// internal bone calls - G2_Bones.cpp
qboolean	G2_Set_Bone_Angles(const char *fileName, boneInfo_v &blist, const char *boneName, const float *angles, const int flags,
							   const Eorientations up, const Eorientations left, const Eorientations forward, qhandle_t *modelList,
							   const int modelIndex, const int blendTime, const int currentTime);
qboolean	G2_Remove_Bone (const char *fileName, boneInfo_v &blist, const char *boneName);
qboolean	G2_Set_Bone_Anim(const char *fileName, boneInfo_v &blist, const char *boneName, const int startFrame,
							 const int endFrame, const int flags, const float animSpeed, const int currentTime, const float setFrame, const int blendTime);
qboolean	G2_Get_Bone_Anim(const char *fileName, boneInfo_v &blist, const char *boneName, const int currentTime,
						  float *currentFrame, int *startFrame, int *endFrame, int *flags, float *retAnimSpeed, qhandle_t *modelList, int modelIndex);
qboolean	G2_Get_Bone_Anim_Range(const char *fileName, boneInfo_v &blist, const char *boneName, int *startFrame, int *endFrame);
qboolean	G2_Pause_Bone_Anim(const char *fileName, boneInfo_v &blist, const char *boneName, const int currentTime );
qboolean	G2_IsPaused(const char *fileName, boneInfo_v &blist, const char *boneName);
qboolean	G2_Stop_Bone_Anim(const char *fileName, boneInfo_v &blist, const char *boneName);
qboolean	G2_Stop_Bone_Angles(const char *fileName, boneInfo_v &blist, const char *boneName);
void		G2_Animate_Bone_List(boneInfo_v &blist, const int currentTime);
void		G2_Init_Bone_List(boneInfo_v &blist);
int			G2_Find_Bone_In_List(boneInfo_v &blist, const int boneNum);
void		G2_RemoveRedundantBoneOverrides(boneInfo_v &blist, int *activeBones);
qboolean	G2_Set_Bone_Angles_Matrix(const char *fileName, boneInfo_v &blist, const char *boneName, const mdxaBone_t &matrix,
									  const int flags, qhandle_t *modelList, const int modelIndex, const int blendTime, const int currentTime);
int			G2_Get_Bone_Index(CGhoul2Info *ghoul2, const char *boneName);
qboolean	G2_Set_Bone_Angles_Index(boneInfo_v &blist, const int index,
							const float *angles, const int flags, const Eorientations yaw,
							const Eorientations pitch, const Eorientations roll, qhandle_t *modelList,
							const int modelIndex, const int blendTime, const int currentTime);
qboolean	G2_Set_Bone_Angles_Matrix_Index(boneInfo_v &blist, const int index,
								   const mdxaBone_t &matrix, const int flags, qhandle_t *modelList,
								   const int modelIndex, const int blendTime, const int currentTime);
qboolean	G2_Stop_Bone_Anim_Index(boneInfo_v &blist, const int index);
qboolean	G2_Stop_Bone_Angles_Index(boneInfo_v &blist, const int index);
qboolean	G2_Set_Bone_Anim_Index(boneInfo_v &blist, const int index, const int startFrame,
						  const int endFrame, const int flags, const float animSpeed, const int currentTime, const float setFrame, const int blendTime);
qboolean	G2_Get_Bone_Anim_Index( boneInfo_v &blist, const int index, const int currentTime,
						  float *currentFrame, int *startFrame, int *endFrame, int *flags, float *retAnimSpeed, qhandle_t *modelList, int modelIndex);

// misc functions G2_misc.cpp
void		G2_List_Model_Surfaces(const char *fileName);
void		G2_List_Model_Bones(const char *fileName, int frame);
qboolean	G2_GetAnimFileName(const char *fileName, char **filename);
void		G2_TraceModels(CGhoul2Info_v &ghoul2, const vec3_t rayStart, const vec3_t rayEnd, CollisionRecord_t *collRecMap, int entNum, int traceFlags, int useLod, float fRadius);
void		TransformAndTranslatePoint (const vec3_t in, vec3_t out, const mdxaBone_t *mat);
void		G2_TransformModel(CGhoul2Info_v &ghoul2, const int frameNum, const vec3_t scale, CMiniHeap *G2VertSpace, int useLod);
void		G2_GenerateWorldMatrix(const vec3_t angles, const vec3_t origin);
void		TransformPoint (const vec3_t in, vec3_t out, const mdxaBone_t *mat);
void		Inverse_Matrix(const mdxaBone_t *src, mdxaBone_t *dest);
void		*G2_FindSurface(void *mod, int index, int lod);
qboolean	G2_SaveGhoul2Models(CGhoul2Info_v &ghoul2, char **buffer, int *size);
void		G2_LoadGhoul2Model(CGhoul2Info_v &ghoul2, char *buffer);

// internal bolt calls. G2_bolts.cpp
int			G2_Add_Bolt(const char *fileName, boltInfo_v &bltlist, surfaceInfo_v &slist, const char *boneName);
qboolean	G2_Remove_Bolt (boltInfo_v &bltlist, int index);
void		G2_Init_Bolt_List(boltInfo_v &bltlist);
int			G2_Find_Bolt_Bone_Num(boltInfo_v &bltlist, const int boneNum);
int			G2_Find_Bolt_Surface_Num(boltInfo_v &bltlist, const int surfaceNum, const int flags);
int			G2_Add_Bolt_Surf_Num(const char *fileName, boltInfo_v &bltlist, surfaceInfo_v &slist, const int surfNum);
void		G2_RemoveRedundantBolts(boltInfo_v &bltlist, surfaceInfo_v &slist, int *activeSurfaces, int *activeBones);


// API calls - G2_API.cpp
int			G2API_GetMaxModelIndex(bool ricksCrazyOnServer);
qhandle_t	G2API_PrecacheGhoul2Model(const char *fileName);
CGhoul2Info_v *G2API_GetGhoul2Model(g2handle_t g2h);

int			G2API_InitGhoul2Model(g2handle_t *g2hPtr, const char *fileName, int modelIndex, qhandle_t customSkin = 0,
	qhandle_t customShader = 0, int modelFlags = 0, int lodBias = 0);
qboolean	G2API_SetLodBias(CGhoul2Info *ghlInfo, int lodBias);
qboolean	G2API_SetSkin(CGhoul2Info *ghlInfo, qhandle_t customSkin);
qboolean	G2API_SetShader(CGhoul2Info *ghlInfo, qhandle_t customShader);
qboolean	G2API_HasGhoul2ModelOnIndex(const g2handle_t *g2hPtr, const int modelIndex);
qboolean	G2API_RemoveGhoul2Model(g2handle_t *g2hPtr, const int modelIndex);
qboolean	G2API_SetSurfaceOnOff(g2handle_t g2h, const char *surfaceName, const int flags);
int			G2API_GetSurfaceOnOff(CGhoul2Info *ghlInfo, const char *surfaceName);
qboolean	G2API_SetRootSurface(g2handle_t g2h, const int modelIndex, const char *surfaceName);
qboolean	G2API_RemoveSurface(CGhoul2Info *ghlInfo, const int index);
int			G2API_AddSurface(CGhoul2Info *ghlInfo, int surfaceNumber, int polyNumber, float BarycentricI, float BarycentricJ, int lod );
qboolean	G2API_SetBoneAnim(g2handle_t g2h, const int modelIndex, const char *boneName, const int startFrame, const int endFrame,
							  const int flags, const float animSpeed, const int currentTime, const float setFrame = -1, const int blendTime = -1);
qboolean	G2API_GetBoneAnim(CGhoul2Info *ghlInfo, const char *boneName, const int currentTime, float *currentFrame,
							  int *startFrame, int *endFrame, int *flags, float *animSpeed, qhandle_t *modelList);
qboolean	G2API_GetAnimRange(CGhoul2Info *ghlInfo, const char *boneName,	int *startFrame, int *endFrame);
qboolean	G2API_PauseBoneAnim(CGhoul2Info *ghlInfo, const char *boneName, const int currentTime);
qboolean	G2API_IsPaused(CGhoul2Info *ghlInfo, const char *boneName);
qboolean	G2API_StopBoneAnim(CGhoul2Info *ghlInfo, const char *boneName);


qboolean G2API_SetBoneAngles(g2handle_t g2h, const int modelIndex, const char *boneName, const vec3_t angles, const int flags,
							 const Eorientations up, const Eorientations left, const Eorientations forward,
							 qhandle_t *modelList, int blendTime, int currentTime );

qboolean	G2API_StopBoneAngles(CGhoul2Info *ghlInfo, const char *boneName);
qboolean	G2API_RemoveBone(CGhoul2Info *ghlInfo, const char *boneName);
void		G2API_AnimateG2Models(g2handle_t g2h, float speedVar);
qboolean	G2API_RemoveBolt(CGhoul2Info *ghlInfo, const int index);
int			G2API_AddBolt(g2handle_t g2h, const int modelIndex, const char *boneName);
int			G2API_AddBoltSurfNum(CGhoul2Info *ghlInfo, const int surfIndex);
void		G2API_SetBoltInfo(g2handle_t g2h, int modelIndex, int boltInfo);
qboolean	G2API_AttachG2Model(g2handle_t g2hFrom, int modelFrom, g2handle_t g2hTo, int toBoltIndex, int toModel);
qboolean	G2API_DetachG2Model(CGhoul2Info *ghlInfo);
qboolean	G2API_AttachEnt(int *boltInfo, CGhoul2Info *ghlInfoTo, int toBoltIndex, int entNum, int toModelNum);
void		G2API_DetachEnt(int *boltInfo);

qboolean	G2API_GetBoltMatrix(g2handle_t g2h, const int modelIndex, const int boltIndex, mdxaBone_t *matrix,
								const vec3_t angles, const vec3_t position, const int frameNum, const qhandle_t *modelList, const vec3_t scale);

void		G2API_ListSurfaces(g2handle_t g2h, int modelIndex);
void		G2API_ListBones(g2handle_t g2h, int modelIndex, int frame);
qboolean	G2API_HaveWeGhoul2Models(g2handle_t g2h);
void		G2API_SetGhoul2ModelIndexes(g2handle_t g2h, qhandle_t *modelList, qhandle_t *skinList);
qboolean	G2API_SetGhoul2ModelFlags(CGhoul2Info *ghlInfo, const int flags);
int			G2API_GetGhoul2ModelFlags(CGhoul2Info *ghlInfo);

qboolean	G2API_GetAnimFileName(CGhoul2Info *ghlInfo, char **filename);
void		G2API_CollisionDetect(CollisionRecord_t *collRecMap, g2handle_t g2h, const vec3_t angles, const vec3_t position, int frameNumber, int entNum, const vec3_t rayStart, const vec3_t rayEnd, const vec3_t scale, CMiniHeap *G2VertSpace, int traceFlags, int useLod, float fRadius);

void		G2API_GiveMeVectorFromMatrix(const mdxaBone_t *boltMatrix, Eorientations flags, vec3_t vec);
int			G2API_CopyGhoul2Instance(g2handle_t g2hFrom, g2handle_t g2hTo, int modelIndex);
void		G2API_CleanGhoul2Models(g2handle_t *g2hPtr);
int			G2API_GetParentSurface(CGhoul2Info *ghlInfo, const int index);
int			G2API_GetSurfaceIndex(CGhoul2Info *ghlInfo, const char *surfaceName);
char		*G2API_GetSurfaceName(CGhoul2Info *ghlInfo, int surfNumber);
char		*G2API_GetGLAName(g2handle_t g2h, int modelIndex);
qboolean	G2API_SetBoneAnglesMatrix(CGhoul2Info *ghlInfo, const char *boneName, const mdxaBone_t &matrix, const int flags,
									  qhandle_t *modelList, int blendTime = 0, int currentTime = 0);
qboolean	G2API_SetNewOrigin(g2handle_t g2h, const int boltIndex);
int			G2API_GetBoneIndex(CGhoul2Info *ghlInfo, const char *boneName);
qboolean	G2API_StopBoneAnglesIndex(CGhoul2Info *ghlInfo, const int index);
qboolean	G2API_StopBoneAnimIndex(CGhoul2Info *ghlInfo, const int index);
qboolean	G2API_SetBoneAnglesIndex(CGhoul2Info *ghlInfo, const int index, const vec3_t angles, const int flags,
							 const int yaw, const int pitch, const int roll,
							 qhandle_t *modelList, int blendTime, int currentTime);
qboolean	G2API_SetBoneAnglesMatrixIndex(CGhoul2Info *ghlInfo, const int index, const mdxaBone_t &matrix,
								   const int flags, qhandle_t *modelList, int blendTime, int currentTime);
qboolean	G2API_SetBoneAnimIndex(CGhoul2Info *ghlInfo, const int index, const int startFrame, const int endFrame, const int flags, const float animSpeed, const int currentTime, const float setFrame, const int blendTime);
qboolean	G2API_SaveGhoul2Models(g2handle_t g2h, char **buffer, int *size);
void		G2API_LoadGhoul2Models(g2handle_t g2h, char *buffer);
void		G2API_LoadSaveCodeDestructGhoul2Info(g2handle_t g2h);
void		G2API_FreeSaveBuffer(char *buffer);
char		*G2API_GetAnimFileNameIndex(qhandle_t modelIndex);
int			G2API_GetSurfaceRenderStatus(CGhoul2Info *ghlInfo, const char *surfaceName);
void		G2API_CopySpecificG2Model(g2handle_t g2hFrom, int modelFrom, g2handle_t g2hTo, int modelTo);
void		G2API_DuplicateGhoul2Instance(g2handle_t g2hFrom, g2handle_t *g2hToPtr);

extern qboolean gG2_GBMNoReconstruct;
extern qboolean gG2_GBMUseSPMethod;
// From tr_ghoul2.cpp
void		G2_ConstructGhoulSkeleton( CGhoul2Info_v &ghoul2, const int frameNum, const qhandle_t *modelList, bool checkForNewOrigin, const vec3_t angles, const vec3_t position, const vec3_t scale, bool modelSet);
