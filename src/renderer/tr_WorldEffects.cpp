#include "tr_local.h"

//#include "stdafx.h"
//#include "q_math.h"
//#include "QSupport.h"

#include "tr_WorldEffects.h"
#include "vk_local.h"




static bool debugShowWind = false;
static int	originContents;

extern qboolean ParseVector( const char **text, int count, float *v );



void MYgluPerspective( double fovy, double aspect, double zNear, double zFar )
{
	// Vulkan: perspective projection is computed via push constants / MVP
	// This is retained as a helper but the actual GL call is removed
	(void)fovy; (void)aspect; (void)zNear; (void)zFar;
}



CWorldEffect::CWorldEffect(CWorldEffect *owner) :
	mNext(0),
	mSlave(0),
	mOwner(owner),
	mEnabled(true),
	mIsSlave(owner ? true : false)
{
}

CWorldEffect::~CWorldEffect(void)
{
	if (mIsSlave && mNext)
	{
		delete mNext;
		mNext = 0;
	}
	if (mSlave)
	{
		delete mSlave;
		mSlave = 0;
	}
}


bool CWorldEffect::Command(const char *command)
{
	if (mSlave)
	{
		if (mSlave->Command(command))
		{
			return true;
		}
	}
	if (mIsSlave && mNext)
	{
		if (mNext->Command(command))
		{
			return true;
		}
	}

	return false;
}

void CWorldEffect::ParmUpdate(CWorldEffectsSystem *system, int which)
{
	if (mSlave)
	{
		mSlave->ParmUpdate(system, which);
	}
	if (mIsSlave && mNext)
	{
		mNext->ParmUpdate(system, which);
	}
}

void CWorldEffect::ParmUpdate(CWorldEffect *effect, int which)
{
	if (mSlave)
	{
		mSlave->ParmUpdate(effect, which);
	}
	if (mIsSlave && mNext)
	{
		mNext->ParmUpdate(effect, which);
	}
}

void CWorldEffect::SetVariable(int which, bool newValue, bool doSlave)
{
	if (doSlave)
	{
		mSlave->SetVariable(which, newValue, doSlave);
	}
	if (doSlave && mIsSlave && mNext)
	{
		mNext->SetVariable(which, newValue, doSlave);
	}

	switch(which)
	{
		case WORLDEFFECT_ENABLED:
			mEnabled = newValue;
			break;
	}
}

void CWorldEffect::SetVariable(int which, float newValue, bool doSlave)
{
	if (doSlave)
	{
		mSlave->SetVariable(which, newValue, doSlave);
	}
	if (doSlave && mIsSlave && mNext)
	{
		mNext->SetVariable(which, newValue, doSlave);
	}
}

void CWorldEffect::SetVariable(int which, int newValue, bool doSlave)
{
	if (doSlave)
	{
		mSlave->SetVariable(which, newValue, doSlave);
	}
	if (doSlave && mIsSlave && mNext)
	{
		mNext->SetVariable(which, newValue, doSlave);
	}
}

void CWorldEffect::SetVariable(int which, vec3_t newValue, bool doSlave)
{
	if (doSlave)
	{
		mSlave->SetVariable(which, newValue, doSlave);
	}
	if (doSlave && mIsSlave && mNext)
	{
		mNext->SetVariable(which, newValue, doSlave);
	}
}

void CWorldEffect::AddSlave(CWorldEffect *slave)
{
	slave->SetNext(mSlave);
	mSlave = slave;

	slave->SetIsSlave(true);
	slave->SetOwner(this);
}

void CWorldEffect::Update(CWorldEffectsSystem *system, float elapseTime)
{
	if (mSlave && mEnabled)
	{
		mSlave->Update(system, elapseTime);
	}
	if (mIsSlave && mNext)
	{
		mNext->Update(system, elapseTime);
	}
}

void CWorldEffect::Render(CWorldEffectsSystem *system)
{
	if (mSlave && mEnabled)
	{
		mSlave->Render(system);
	}
	if (mIsSlave && mNext)
	{
		mNext->Render(system);
	}
}












CWorldEffectsSystem::CWorldEffectsSystem(void) :
	mList(0),
	mLast(0)
{
}

CWorldEffectsSystem::~CWorldEffectsSystem(void)
{
	CWorldEffect	*next;

	while(mList)
	{
		next = mList->GetNext();
		delete mList;
		mList = next;
	}
}

void CWorldEffectsSystem::AddWorldEffect(CWorldEffect *effect)
{
	if (!mList)
	{
		mList = mLast = effect;
	}
	else
	{
		mLast->SetNext(effect);
		mLast = effect;
	}
}

bool CWorldEffectsSystem::Command(const char *command)
{
	CWorldEffect	*current;

	current = mList;
	while(current)
	{
		if (current->Command(command))
		{
			return true;
		}
		current = current->GetNext();
	}

	return false;
}

void CWorldEffectsSystem::Update(float elapseTime)
{
	CWorldEffect	*current;

	current = mList;
	while(current)
	{
		current->Update(this, elapseTime);
		current = current->GetNext();
	}
}

void CWorldEffectsSystem::ParmUpdate(int which)
{
	CWorldEffect	*current;

	current = mList;
	while(current)
	{
		current->ParmUpdate(this, which);
		current = current->GetNext();
	}
}

void CWorldEffectsSystem::Render(void)
{
	CWorldEffect	*current;

	current = mList;
	while(current)
	{
		current->Render(this);
		current = current->GetNext();
	}
}









class CRainSystem : public CWorldEffectsSystem
{
private:
	// configurable
	int			mMaxRain;
	float		mRainHeight;
	vec3_t		mSpread;
	float		mAlpha;
	float		mWindAngle;

	image_t		*mImage;
	vec3_t		mMinVelocity, mMaxVelocity;
	// int			mNextWindGust;
	int mWindDuration, mWindLow;
	float		mWindMin, mWindMax;
	vec3_t		mWindDirection, mNewWindDirection, mWindSpeed;
	int			mWindChange;

	SParticle	*mRainList;
	float		mFadeAlpha;
	bool		mIsRaining;

public:
	enum
	{
		RAINSYSTEM_WIND_DIRECTION,
		RAINSYSTEM_WIND_SPEED,
	};

public:
	CRainSystem(int maxRain);
	~CRainSystem(void);

	virtual	int			GetIntVariable(int which);
	virtual	SParticle	*GetParticleVariable(int which);
	virtual float		GetFloatVariable(int which);
	virtual	float		*GetVecVariable(int which);

	virtual	bool	Command(const char *command);

	virtual	void	Update(float elapseTime);
	virtual	void	Render(void);

			void	Init(void);

			bool	IsRaining() { return mIsRaining; }
};




class CMistyFog : public CWorldEffect
{
private:
//	GLuint		mImage;
//	image_t		*mImage;
	float		mTextureCoords[2][2];
	float		mAlpha;
	bool		mAlphaFade, mRendering, mBuddy;
	float		mSpeed, mAlphaDirection;
	float		mCurrentSize, mMinSize, mMaxSize;
	vec3_t		mWindTransform;

	int				mWidth, mHeight;
	unsigned char	*mData;

	const	float	mSize;

public:
	enum
	{
		MISTYFOG_RENDERING = WORLDEFFECT_END
	};

public:
	CMistyFog(int index, CWorldEffect *owner = 0, bool buddy = false);

//			image_t	*GetImage(void) { return mImage; }

			int				GetWidth(void) { return mWidth; }
			int				GetHeight(void) { return mHeight; }
			byte			*GetData(void) { return mData; }
			float			GetTextureCoord(int s, int y) { return mTextureCoords[s][y]; }
			float			GetAlpha(void) { return mAlpha; }
			bool			GetRendering(void) { return mRendering; }

	virtual	void	Update(CWorldEffectsSystem *system, float elapseTime);
	virtual	void	ParmUpdate(CWorldEffectsSystem *system, int which);
	virtual	void	ParmUpdate(CWorldEffect *effect, int which);
	virtual	void	Render(CWorldEffectsSystem *system);

			void	CreateTextureCoords(void);
};

CMistyFog::CMistyFog(int index, CWorldEffect *owner, bool buddy) :
	CWorldEffect(owner),

	mAlpha(1.0f),
	mAlphaFade(false),
	mBuddy(buddy),
	mMinSize(0.05f * 3.0f),
	mMaxSize(0.15f * 2.0f),
	mSize(0.05f * 2.0f)
{
	char			name[MAX_QPATH];

	if (mBuddy)
	{
		mRendering = false;

		mData = ((CMistyFog *)owner)->GetData();
		mWidth = ((CMistyFog *)owner)->GetWidth();
		mHeight = ((CMistyFog *)owner)->GetHeight();
	}
	else
	{
		pixelFormat_t format;
		Com_sprintf(name, MAX_QPATH, "gfx/world/fog%d", index);
		R_LoadImage(name, &mData, &mWidth, &mHeight, &format);
		if (!mData)
		{
			ri.Error (ERR_DROP, "Could not load %s", name);
		}
		if (format != PXF_RGBA)
		{
			ri.Error (ERR_DROP, "Fog image must be RGBA %s", name);
		}

		mRendering = true;
		AddSlave(new CMistyFog(index, this, true));
	}

	mSpeed = flrand(90.0f, 110.0f);

	CreateTextureCoords();
}

void CMistyFog::Update(CWorldEffectsSystem *system, float elapseTime)
{
	bool	removeImage = false;
	float	forwardWind, rightWind;

	CWorldEffect::Update(system, elapseTime);

	if (!mRendering)
	{
		return;
	}

	// translate

	forwardWind = DotProduct(mWindTransform, backEnd.viewParms.ori.axis[0]);
	rightWind = DotProduct(mWindTransform, backEnd.viewParms.ori.axis[1]);

	mTextureCoords[0][0] += rightWind / mSpeed;
	mTextureCoords[1][0] += rightWind / mSpeed;

	mTextureCoords[0][0] -= forwardWind / mSpeed / 4.0f;
	mTextureCoords[0][1] -= forwardWind / mSpeed / 4.0f;
	mTextureCoords[1][0] += forwardWind / mSpeed / 4.0f;
	mTextureCoords[1][1] += forwardWind / mSpeed / 4.0f;

/*	if (mTextureCoords[0][0] > mTextureCoords[1][0] ||
		mTextureCoords[0][1] > mTextureCoords[1][1])
	{

		mAlphaFade = true;
		mAlphaDirection = -1.0;
		mAlpha = -1.0;
	}
*/
	if ((fabsf(mTextureCoords[0][0] - mTextureCoords[1][0]) < mMinSize ||
		fabsf(mTextureCoords[0][1] - mTextureCoords[1][1]) < mMinSize))// && forwardWind > 0.0)
	{
		removeImage = true;
	}

	if ((fabsf(mTextureCoords[0][0] - mTextureCoords[1][0]) > mMaxSize ||
		fabsf(mTextureCoords[0][1] - mTextureCoords[1][1]) > mMaxSize))// && forwardWind < 0.0)
	{
		removeImage = true;
	}

	if (mTextureCoords[0][0] < mCurrentSize || mTextureCoords[0][1] < mCurrentSize ||
		mTextureCoords[0][0] > 1-mCurrentSize || mTextureCoords[0][1] > 1-mCurrentSize)
	{
//		mAlphaFade = true;
	}
	if (mTextureCoords[1][0] < mCurrentSize || mTextureCoords[1][1] < mCurrentSize ||
		mTextureCoords[1][0] > 1-mCurrentSize || mTextureCoords[1][1] > 1-mCurrentSize)
	{
//		mAlphaFade = true;
	}

	if (removeImage && !mAlphaFade)
	{
		mAlphaFade = true;
		mAlphaDirection = -0.025f;
		if (mBuddy)
		{
			mOwner->ParmUpdate(this, MISTYFOG_RENDERING);
		}
		else if (mSlave)
		{
			mSlave->ParmUpdate(this, MISTYFOG_RENDERING);
		}
	}

	if (mAlphaFade)
	{
		mAlpha += mAlphaDirection * 0.4f;
		if (mAlpha < 0.0f)
		{
			mRendering = false;
			mAlpha = 0.0f;
		}
		else if (mAlpha >= 1.0f)
		{
			mAlphaFade = false;
			mAlpha = 1.0f;
		}
	}
}

void CMistyFog::ParmUpdate(CWorldEffectsSystem *system, int which)
{
	CWorldEffect::ParmUpdate(system, which);

	switch(which)
	{
		case CRainSystem::RAINSYSTEM_WIND_DIRECTION:
			VectorCopy(system->GetVecVariable(which), mWindTransform);
			break;
	}
}

void CMistyFog::ParmUpdate(CWorldEffect *effect, int which)
{
	CWorldEffect::ParmUpdate(effect, which);

	switch(which)
	{
		case MISTYFOG_RENDERING:
			if (effect == mOwner || effect == mSlave)
			{
				mAlpha = 0.0f;
				mAlphaDirection = 0.025f;
				mAlphaFade = true;
				CreateTextureCoords();
				mRendering = true;
			}
			break;
	}
}

void CMistyFog::Render(CWorldEffectsSystem *system)
{
	CWorldEffect::Render(system);
}

void CMistyFog::CreateTextureCoords(void)
{
	float	xStart, yStart;
	float	forwardWind, rightWind;

	mSpeed = flrand(200.0f, 700.0f);

	forwardWind = DotProduct(mWindTransform, backEnd.viewParms.ori.axis[0]);
	rightWind = DotProduct(mWindTransform, backEnd.viewParms.ori.axis[1]);

	if (forwardWind > 0.5f)
	{	// moving away, so make the size smaller
		mCurrentSize = flrand(mMinSize, mMinSize + mMinSize * 0.01f);
//		mCurrentSize = mMinSize / 3.0;
	}
	else if (forwardWind < -0.5f)
	{	// moving towards, so make bigger
//		mCurrentSize = (mSize * 0.8) + (FloatRand() * mSize * 0.8);
		mCurrentSize = flrand(mMaxSize - mMinSize, mMaxSize);
	}
	else
	{	// normal range
		mCurrentSize = flrand(mMinSize * 1.5f, mMinSize * 1.5f + mSize);
	}

	mCurrentSize /= 2.0f;

	xStart = (1.0f - mCurrentSize - 0.40f) * flrand(0.0f, 1.0f) + 0.20f;
	yStart = (1.0f - mCurrentSize - 0.40f) * flrand(0.0f, 1.0f) + 0.20f;

	mTextureCoords[0][0] = xStart - mCurrentSize;
	mTextureCoords[0][1] = yStart - mCurrentSize;
	mTextureCoords[1][0] = xStart + mCurrentSize;
	mTextureCoords[1][1] = yStart + mCurrentSize;
}









#define	MISTYFOG_WIDTH	30
#define MISTYFOG_HEIGHT	30


class CMistyFog2 : public CWorldEffect
{
protected:
	vec4_t			mColors[MISTYFOG_HEIGHT][MISTYFOG_WIDTH];
	vec3_t			mVerts[MISTYFOG_HEIGHT][MISTYFOG_WIDTH];
	unsigned int	mIndexes[MISTYFOG_HEIGHT-1][MISTYFOG_WIDTH-1][4];
	float			mAlpha;

	float			mFadeAlpha;

public:
	CMistyFog2(void);

	virtual	bool	Command(const char *command);

			void	UpdateTexture(CMistyFog *fog);

	virtual	void	Update(CWorldEffectsSystem *system, float elapseTime);
	virtual	void	Render(CWorldEffectsSystem *system);
};


CMistyFog2::CMistyFog2(void) :
	CWorldEffect(),
	mAlpha(0.3f),

	mFadeAlpha(0.0f)
{
	int			x, y;
	float		xStep, yStep;

	AddSlave(new CMistyFog(2));
	AddSlave(new CMistyFog(2));

	xStep = 20.0f / (MISTYFOG_WIDTH - 1);
	yStep = 20.0f / (MISTYFOG_HEIGHT - 1);

	for(y=0;y<MISTYFOG_HEIGHT;y++)
	{
		for(x=0;x<MISTYFOG_WIDTH;x++)
		{
			mVerts[y][x][0] = -10 + (x * xStep) + flrand(-xStep * (1.0f / 16), xStep * (1.0f / 16));
			mVerts[y][x][1] = 10 - (y * yStep) + flrand(-xStep * (1.0f / 16), xStep * (1.0f / 16));
			mVerts[y][x][2] = -10;

			mColors[y][x][0] = 1.0;
			mColors[y][x][1] = 1.0;
			mColors[y][x][2] = 1.0;

			if (y < MISTYFOG_HEIGHT-1 && x < MISTYFOG_WIDTH-1)
			{
				mIndexes[y][x][0] = (y*MISTYFOG_WIDTH) + x;
				mIndexes[y][x][1] = (y*MISTYFOG_WIDTH) + x+1;
				mIndexes[y][x][2] = ((y+1)*MISTYFOG_WIDTH) + x+1;
				mIndexes[y][x][3] = ((y+1)*MISTYFOG_WIDTH) + x;
			}
		}
	}
}

bool CMistyFog2::Command(const char *command)
{
	char	*token;

	if (CWorldEffect::Command(command))
	{
		return true;
	}

	token = COM_ParseExt((const char **)&command, qfalse);
	if (Q_stricmp(token, "fog") != 0)
	{
		return false;
	}

	token = COM_ParseExt((const char **)&command, qfalse);
	if (Q_stricmp(token, "density") == 0)
	{
		token = COM_ParseExt((const char **)&command, qfalse);
		mAlpha = atof(token);

		return true;
	}

	return false;
}

void CMistyFog2::Update(CWorldEffectsSystem *system, float elapseTime)
{
	CMistyFog	*current;
	int			x, y;

	if (originContents & CONTENTS_OUTSIDE && !(originContents & CONTENTS_WATER))
	{
		if (mFadeAlpha < 1.0f)
		{
			mFadeAlpha += elapseTime * 0.5f;
		}
		if (mFadeAlpha > 1.0f)
		{
			mFadeAlpha = 1.0f;
		}
	}
	else
	{
		if (mFadeAlpha > 0.0f)
		{
			mFadeAlpha -= elapseTime * 0.5f;
		}

		if (mFadeAlpha <= 0.0f)
		{
			return;
		}
	}

	for(y=0;y<MISTYFOG_HEIGHT;y++)
	{
		for(x=0;x<MISTYFOG_WIDTH;x++)
		{
			mColors[y][x][3] = 0.0;
		}
	}

	CWorldEffect::Update(system, elapseTime);

	current = (CMistyFog *)mSlave;
	while(current)
	{
		UpdateTexture(current);
		UpdateTexture((CMistyFog *)current->GetSlave());
		current = (CMistyFog *)current->GetNext();
	}
}

void CMistyFog2::UpdateTexture(CMistyFog *fog)
{
	int				x, y, tx, ty;
	float			xSize, ySize;
	float			xStep, yStep;
	float			xPos, yPos;
	byte			*data = fog->GetData();
	int				width = fog->GetWidth();
	int				height = fog->GetHeight();
	int				andWidth, andHeight;
	float			alpha = fog->GetAlpha() * mAlpha * (1.0f/255) * mFadeAlpha;
	float			*color;

	if (!fog->GetRendering())
	{
		return;
	}

	andWidth = width-1;		// width must be power of 2
	andHeight = height-1;	// height must be power of 2
	xSize = fog->GetTextureCoord(1, 0) - fog->GetTextureCoord(0, 0);
	ySize = fog->GetTextureCoord(1, 1) - fog->GetTextureCoord(0, 1);
	xStep = xSize / (float)MISTYFOG_WIDTH;
	yStep = ySize / (float)MISTYFOG_HEIGHT;

	color = &mColors[0][0][3];
	for(y=0,yPos = fog->GetTextureCoord(0, 1);y<MISTYFOG_HEIGHT;y++, yPos += yStep)
	{
		for(x=0,xPos = fog->GetTextureCoord(0, 0);x<MISTYFOG_WIDTH;x++, xPos += xStep)
		{
			tx = xPos * width;
			tx &= andWidth;
			ty = yPos * height;
			ty &= andHeight;

			(*color) += data[ty * width + tx] * alpha;
			color += 4;
		}
	}
}

void CMistyFog2::Render(CWorldEffectsSystem *system)
{
	if (mFadeAlpha <= 0.0f)
	{
		return;
	}

	// Vulkan: fog mesh rendering
	// Build a custom MVP for the fog and draw via VK_DrawIndexed
	R_SetStateBits(GLS_SRCBLEND_SRC_ALPHA|GLS_DSTBLEND_ONE);

	// Convert quad indexes to triangle indexes
	const int numQuads = (MISTYFOG_HEIGHT-1)*(MISTYFOG_WIDTH-1);
	glIndex_t triIndexes[numQuads * 6];
	int ni = 0;
	for (int q = 0; q < numQuads * 4; q += 4) {
		triIndexes[ni++] = mIndexes[q / 4 / (MISTYFOG_WIDTH-1)][q / 4 % (MISTYFOG_WIDTH-1)][0];
		triIndexes[ni++] = mIndexes[q / 4 / (MISTYFOG_WIDTH-1)][q / 4 % (MISTYFOG_WIDTH-1)][1];
		triIndexes[ni++] = mIndexes[q / 4 / (MISTYFOG_WIDTH-1)][q / 4 % (MISTYFOG_WIDTH-1)][2];
		triIndexes[ni++] = mIndexes[q / 4 / (MISTYFOG_WIDTH-1)][q / 4 % (MISTYFOG_WIDTH-1)][0];
		triIndexes[ni++] = mIndexes[q / 4 / (MISTYFOG_WIDTH-1)][q / 4 % (MISTYFOG_WIDTH-1)][2];
		triIndexes[ni++] = mIndexes[q / 4 / (MISTYFOG_WIDTH-1)][q / 4 % (MISTYFOG_WIDTH-1)][3];
	}

	// Convert float colors to byte colors, and vec3 verts to vec4 for Vulkan
	const int numVerts = MISTYFOG_HEIGHT * MISTYFOG_WIDTH;
	byte byteColors[MISTYFOG_HEIGHT * MISTYFOG_WIDTH][4];
	vec4_t paddedVerts[MISTYFOG_HEIGHT * MISTYFOG_WIDTH];
	for (int i = 0; i < numVerts; i++) {
		int row = i / MISTYFOG_WIDTH;
		int col = i % MISTYFOG_WIDTH;
		byteColors[i][0] = (byte)(mColors[row][col][0] * 255.0f);
		byteColors[i][1] = (byte)(mColors[row][col][1] * 255.0f);
		byteColors[i][2] = (byte)(mColors[row][col][2] * 255.0f);
		byteColors[i][3] = (byte)(mColors[row][col][3] * 255.0f);
		VectorCopy(mVerts[row][col], paddedVerts[i]);
		paddedVerts[i][3] = 1.0f;
	}

	VK_BindPipeline( renderState.stateBits, renderState.faceCulling, qfalse, qfalse );
	VK_DrawIndexed( numVerts, (float*)paddedVerts, NULL, NULL, (byte*)byteColors, ni, triIndexes );
}

















































class CWind : public CWorldEffect
{
private:
	vec4_t	mPlanes[3];		// x y z normal, distance
	float	mMaxDistance[3];
	vec3_t	mVelocity;
	int		mNumPlanes;
	int		mAffectedDuration;
	int		*mAffectedCount;
	vec3_t	mPoint, mSize;
	bool	mGlobal;

public:
	CWind(bool global = false);
	CWind(vec3_t point, vec3_t velocity, vec3_t size, int duration, bool global = false);
	~CWind(void);

	virtual	void	Update(CWorldEffectsSystem *system, float elapseTime);
	virtual	void	ParmUpdate(CWorldEffectsSystem *system, int which);
	virtual	void	Render(CWorldEffectsSystem *system);

			void	UpdateParms(vec3_t point, vec3_t velocity, vec3_t size, int duration);
};








CWind::CWind(bool global) :
	CWorldEffect(),
	mNumPlanes(0),
	mAffectedCount(0),
	mGlobal(global)
{
	mEnabled = false;
}

CWind::CWind(vec3_t point, vec3_t velocity, vec3_t size, int duration, bool global) :
	CWorldEffect(),
	mNumPlanes(0),
	mAffectedCount(0),
	mGlobal(global)
{
	UpdateParms(point, velocity, size, duration);
}

CWind::~CWind(void)
{
	if (mAffectedCount)
	{
		delete [] mAffectedCount;
		mAffectedCount = 0;
	}
}

void CWind::UpdateParms(vec3_t point, vec3_t velocity, vec3_t size, int duration)
{
	vec3_t	normalDistance;

	mNumPlanes = 0;

	VectorCopy(point, mPoint);
	VectorCopy(size, mSize);
	mSize[0] *= 0.5f;
	VectorScale(mSize, 2, mSize);
	VectorCopy(velocity, mVelocity);

	VectorCopy(velocity, mPlanes[mNumPlanes]);
	VectorNormalize(mPlanes[mNumPlanes]);
	mPlanes[mNumPlanes][3] = DotProduct(mPoint, mPlanes[mNumPlanes]);
	mMaxDistance[mNumPlanes] = mSize[0];
	mNumPlanes++;

	VectorScale(mPlanes[0], mPlanes[0][3], normalDistance);
	VectorSubtract(mPoint, normalDistance, mPlanes[mNumPlanes]);
	VectorNormalize(mPlanes[mNumPlanes]);
	mPlanes[mNumPlanes][3] = DotProduct(mPoint, mPlanes[mNumPlanes]);
	mMaxDistance[mNumPlanes] = mSize[1];
	mNumPlanes++;

	CrossProduct(mPlanes[0], mPlanes[1], mPlanes[mNumPlanes]);
	VectorNormalize(mPlanes[mNumPlanes]);
	mPlanes[mNumPlanes][3] = DotProduct(mPoint, mPlanes[mNumPlanes]);
	mMaxDistance[mNumPlanes] = mSize[2];
	mNumPlanes++;

	mPlanes[0][3] -= mSize[0] * 0.5f;
	mPlanes[1][3] -= mSize[1] * 0.5f;
	mPlanes[2][3] -= mSize[2] * 0.5f;

	mAffectedDuration = duration;
}

void CWind::Update(CWorldEffectsSystem *system, float elapseTime)
{
	SParticle				*item;
	int						i, j, *affected;
	float					dist, calcDist[3];
	vec3_t					difference;

	if (!mEnabled)
	{
		return;
	}

	VectorSubtract(backEnd.viewParms.ori.origin, mPoint, difference);
	if (VectorLength(difference) > 300.0f)
	{
		return;
	}

	calcDist[0] = 0.0;
	item = system->GetParticleVariable(WORLDEFFECT_PARTICLES);
	affected = mAffectedCount;
	for(i=system->GetIntVariable(WORLDEFFECT_PARTICLE_COUNT); i; i--)
	{
		if ((*affected))
		{
			(*affected)--;
		}
		else
		{
			if (!mGlobal)
			{
				for(j=0;j<mNumPlanes;j++)
				{
					dist = DotProduct(item->pos, mPlanes[j]) - mPlanes[j][3];

					if (dist < 0.01f || dist > mMaxDistance[j])
					{
						break;
					}
					else
					{
						calcDist[j] = dist;
					}
				}
				if (j != mNumPlanes)
				{
					continue;
				}
			}

			float	scaleLength = 1.0f - (calcDist[0] / mMaxDistance[0]);

			(*affected) = mAffectedDuration * scaleLength;

//			VectorMA(item->velocity, elapseTime, mVelocity);
			VectorMA(item->velocity, elapseTime, mVelocity, item->velocity);
		}
		affected++;
		item++;
	}
}

void CWind::ParmUpdate(CWorldEffectsSystem *system, int which)
{
	CWorldEffect::ParmUpdate(system, which);

	switch(which)
	{
		case WORLDEFFECT_PARTICLE_COUNT:
			if (mAffectedCount)
			{
				delete [] mAffectedCount;
			}
			mAffectedCount = new int[system->GetIntVariable(WORLDEFFECT_PARTICLE_COUNT)];
			memset(mAffectedCount, 0, system->GetIntVariable(WORLDEFFECT_PARTICLE_COUNT)*sizeof(int));
			break;
	}
}

void CWind::Render(CWorldEffectsSystem *system)
{
	if (!mEnabled || !debugShowWind)
	{
		return;
	}

	// Vulkan: debug wind volume rendering stub
	// Would need a dedicated debug rendering pipeline
}












#define CONTENTS_X_SIZE		16
#define CONTENTS_Y_SIZE		16
#define CONTENTS_Z_SIZE		8


class CSnowSystem : public CWorldEffectsSystem
{
private:
	// configurable
	float		mAlpha;
	vec3_t		mMinSpread, mMaxSpread;
	vec3_t		mMinVelocity, mMaxVelocity;
	int			mMaxSnowflakes;
	float		mWindDuration, mWindLow;
	float		mWindMin, mWindMax;
	vec3_t		mWindSize;

	// image_t		*mImage;
	vec3_t		mMins, mMaxs;
	float		mNextWindGust, mWindLowSize;
	CWind		*mWindGust;

	vec3_t		mWindDirection, mWindSpeed;
	int			mWindChange;

	SParticle	*mSnowList;
	int			mContents[CONTENTS_Z_SIZE][CONTENTS_Y_SIZE][CONTENTS_X_SIZE];
	vec3_t		mContentsSize;
	vec3_t		mContentsStart;

	int			mUpdateCount;
	int			mOverallContents;
	bool		mIsSnowing;

	const		float	mVelocityStabilize;
	const		int		mUpdateMax;

public:
	CSnowSystem(int maxSnowflakes);
	~CSnowSystem(void);

	virtual	int			GetIntVariable(int which);
	virtual	SParticle	*GetParticleVariable(int which);
	virtual	float		*GetVecVariable(int which);

	virtual bool	Command(const char *command);

	virtual	void	Update(float elapseTime);
	virtual	void	Render(void);

			void	Init(void);

			bool	IsSnowing() { return mIsSnowing; }
};

CSnowSystem::CSnowSystem(int maxSnowflakes) :
	mAlpha(0.09f),
	mMaxSnowflakes(maxSnowflakes),

	mWindDuration(2.0f),
	mWindLow(3.0f),
	mWindMin(30.0f), // .6 3
	mWindMax(70.0f),
	mNextWindGust(0.0),
	mWindLowSize(0.0),
	mWindGust(0),
	mWindChange(0),

	mUpdateCount(0),
	mOverallContents(0),
	mIsSnowing(false),

	mVelocityStabilize(18),
	mUpdateMax(10)
{
	mMinSpread[0] = -600;
	mMinSpread[1] = -600;
	mMinSpread[2] = -200;
	mMaxSpread[0] = 600;
	mMaxSpread[1] = 600;
	mMaxSpread[2] = 250;

	mMinVelocity[0] = -15.0;
	mMaxVelocity[0] = 15.0;
	mMinVelocity[1] = -15.0;
	mMaxVelocity[1] = 15.0;
	mMinVelocity[2] = -20.0;
	mMaxVelocity[2] = -70.0;

	mWindSize[0] = 1000.0;
	mWindSize[1] = 300.0;
	mWindSize[2] = 300.0;

	mSnowList = new SParticle[mMaxSnowflakes];

	mContentsSize[0] = (mMaxSpread[0] - mMinSpread[0]) / CONTENTS_X_SIZE;
	mContentsSize[1] = (mMaxSpread[1] - mMinSpread[1]) / CONTENTS_Y_SIZE;
	mContentsSize[2] = (mMaxSpread[2] - mMinSpread[2]) / CONTENTS_Z_SIZE;

	Init();

	AddWorldEffect(mWindGust= new CWind(true));
	ParmUpdate(CWorldEffect::WORLDEFFECT_PARTICLE_COUNT);
}

CSnowSystem::~CSnowSystem(void)
{
	delete [] mSnowList;
}

void CSnowSystem::Init(void)
{
	int			i;
	SParticle	*item;

	mMins[0] = mMaxs[0] = mMins[1] = mMaxs[1] = mMins[2] = mMaxs[2] = 99999;
	item = mSnowList;
	for(i=mMaxSnowflakes;i;i--)
	{
		item->pos[0] = item->pos[1] = item->pos[2] = 99999;
		item->velocity[0] = item->velocity[1] = item->velocity[2] = 0.0;
		item->flags = 0;
		item++;
	}
}

int CSnowSystem::GetIntVariable(int which)
{
	switch(which)
	{
		case CWorldEffect::WORLDEFFECT_PARTICLE_COUNT:
			return mMaxSnowflakes;
	}

	return CWorldEffectsSystem::GetIntVariable(which);
}

SParticle *CSnowSystem::GetParticleVariable(int which)
{
	switch(which)
	{
		case CWorldEffect::WORLDEFFECT_PARTICLES:
			return mSnowList;
	}

	return CWorldEffectsSystem::GetParticleVariable(which);
}

float *CSnowSystem::GetVecVariable(int which)
{
	switch(which)
	{
		case CRainSystem::RAINSYSTEM_WIND_DIRECTION:
			return mWindDirection;
	}
	return 0;
}

bool CSnowSystem::Command(const char *command)
{
	char	*token;

	if (CWorldEffectsSystem::Command(command))
	{
		return true;
	}

	token = COM_ParseExt((const char **)&command, qfalse);

	if (Q_stricmp(token, "wind") == 0)
	{	// snow wind ( windOriginX windOriginY windOriginZ ) ( windVelocityX windVelocityY windVelocityZ ) ( sizeX sizeY sizeZ )
		vec3_t	origin, velocity, size;

		ParseVector((const char **)&command, 3, origin);
		ParseVector((const char **)&command, 3, velocity);
		ParseVector((const char **)&command, 3, size);

		AddWorldEffect(new CWind(origin, velocity, size, 0));

		return true;
	}
	else if (Q_stricmp(token, "fog") == 0)
	{	// snow fog
		AddWorldEffect(new CMistyFog2);
		mWindChange = 0;
		return true;
	}
	else if (Q_stricmp(token, "alpha") == 0)
	{	// snow alpha <float>											default: 0.09
		token = COM_ParseExt((const char **)&command, qfalse);
		mAlpha = atof(token);
		return true;
	}
	else if (Q_stricmp(token, "spread") == 0)
	{	// snow spread ( minX minY minZ ) ( maxX maxY maxZ )			default: ( -600 -600 -200 ) ( 600 600 250 )
		ParseVector((const char **)&command, 3, mMinSpread);
		ParseVector((const char **)&command, 3, mMaxSpread);
		return true;
	}
	else if (Q_stricmp(token, "velocity") == 0)
	{	// snow velocity ( minX minY minZ ) ( maxX maxY maxZ )			default: ( -15 -15 -20 ) ( 15 15 -70 )
		ParseVector((const char **)&command, 3, mMinSpread);
		ParseVector((const char **)&command, 3, mMaxSpread);
		return true;
	}
	else if (Q_stricmp(token, "blowing") == 0)
	{
		token = COM_ParseExt((const char **)&command, qfalse);
		if (Q_stricmp(token, "duration") == 0)
		{	// snow blowing duration <int>									default: 2
			token = COM_ParseExt((const char **)&command, qfalse);
			mWindDuration = atol(token);
			return true;
		}
		else if (Q_stricmp(token, "low") == 0)
		{	// snow blowing low <int>										default: 3
			token = COM_ParseExt((const char **)&command, qfalse);
			mWindLow = atol(token);
			return true;
		}
		else if (Q_stricmp(token, "velocity") == 0)
		{	// snow blowing velocity ( min max )							default: ( 30 70 )
			float	data[2];

			ParseVector((const char **)&command, 2, data);
			mWindMin = data[0];
			mWindMax = data[1];
			return true;
		}
		else if (Q_stricmp(token, "size") == 0)
		{	// snow blowing size ( minX minY minZ )							default: ( 1000 300 300 )
			ParseVector((const char **)&command, 3, mWindSize);
			return true;
		}
	}

	return false;
}

void CSnowSystem::Update(float elapseTime)
{
	int			i;
	SParticle	*item;
	vec3_t		origin, newMins, newMaxs;
	vec3_t		difference, start;
	bool		resetFlake;
	int			x, y, z;
	int			contents;

	mWindChange--;
	if (mWindChange < 0)
	{
		mWindDirection[0] = flrand(-1.0f, 1.0f);
		mWindDirection[1] = flrand(-1.0f, 1.0f);
		mWindDirection[2] = 0.0f;
		VectorNormalize(mWindDirection);
		VectorScale(mWindDirection, 0.025f, mWindSpeed);

		mWindChange = irand(200, 450);
//		mWindChange = 10;

		ParmUpdate(CRainSystem::RAINSYSTEM_WIND_DIRECTION);
	}

	if ((mOverallContents & CONTENTS_OUTSIDE))
	{
		CWorldEffectsSystem::Update(elapseTime);
	}

	VectorCopy(backEnd.viewParms.ori.origin, origin);

	mNextWindGust -= elapseTime;
	if (mNextWindGust < 0.0f)
	{
		mWindGust->SetVariable(CWorldEffect::WORLDEFFECT_ENABLED, false);
	}

	if (mNextWindGust < mWindLowSize)
	{
		vec3_t		windPos;
		vec3_t		windDirection;

		windDirection[0] = flrand(-1.0f, 1.0f);
		windDirection[1] = flrand(-1.0f, 1.0f);
		windDirection[2] = 0.0f;  //ri.flrand(-0.1, 0.1);
		VectorNormalize(windDirection);
		VectorScale(windDirection, flrand(mWindMin, mWindMax), windDirection);

		VectorCopy(origin, windPos);

		mWindGust->SetVariable(CWorldEffect::WORLDEFFECT_ENABLED, true);
		mWindGust->UpdateParms(windPos, windDirection, mWindSize, 0);

		mNextWindGust = flrand(mWindDuration, mWindDuration * 2.0f);
		mWindLowSize = -flrand(mWindLow, mWindLow * 3.0f);
	}

	newMins[0] = mMinSpread[0] + origin[0];
	newMaxs[0] = mMaxSpread[0] + origin[0];

	newMins[1] = mMinSpread[1] + origin[1];
	newMaxs[1] = mMaxSpread[1] + origin[1];

	newMins[2] = mMinSpread[2] + origin[2];
	newMaxs[2] = mMaxSpread[2] + origin[2];

	for(i=0;i<3;i++)
	{
		difference[i] = newMaxs[i] - mMaxs[i];
		if (difference[i] >= 0.0f)
		{
			if (difference[i] > newMaxs[i]-newMins[i])
			{
				difference[i] = newMaxs[i]-newMins[i];
			}
			start[i] = newMaxs[i] - difference[i];
		}
		else
		{
			if (difference[i] < newMins[i]-newMaxs[i])
			{
				difference[i] = newMins[i]-newMaxs[i];
			}
			start[i] = newMins[i] - difference[i];
		}
	}

//	contentsStart[0] = (((origin[0] + mMinSpread[0]) / mContentsSize[0])) * mContentsSize[0];
//	contentsStart[1] = (((origin[1] + mMinSpread[1]) / mContentsSize[1])) * mContentsSize[1];
//	contentsStart[2] = (((origin[2] + mMinSpread[2]) / mContentsSize[2])) * mContentsSize[2];

	if (fabsf(difference[0]) > 25 || fabsf(difference[1]) > 25 || fabsf(difference[2]) > 25)
	{
		vec3_t		pos;
		int			*store;

		mContentsStart[0] = ((int)((origin[0] + mMinSpread[0]) / mContentsSize[0])) * mContentsSize[0];
		mContentsStart[1] = ((int)((origin[1] + mMinSpread[1]) / mContentsSize[1])) * mContentsSize[1];
		mContentsStart[2] = ((int)((origin[2] + mMinSpread[2]) / mContentsSize[2])) * mContentsSize[2];

		mOverallContents = 0;
		store = (int *)mContents;
		for(z=0,pos[2]=mContentsStart[2];z<CONTENTS_Z_SIZE;z++,pos[2]+=mContentsSize[2])
		{
			for(y=0,pos[1]=mContentsStart[1];y<CONTENTS_Y_SIZE;y++,pos[1]+=mContentsSize[1])
			{
				for(x=0,pos[0]=mContentsStart[0];x<CONTENTS_X_SIZE;x++,pos[0]+=mContentsSize[0])
				{
					contents = ri.CM_PointContents(pos, 0);
					mOverallContents |= contents;
					*store++ = contents;
				}
			}
		}

		item = mSnowList;
		for(i=mMaxSnowflakes;i;i--)
		{
			resetFlake = false;

			if (item->pos[0] < newMins[0] || item->pos[0] > newMaxs[0])
			{
				item->pos[0] = flrand(0.0f, difference[0]) + start[0];
				resetFlake = true;
			}
			if (item->pos[1] < newMins[1] || item->pos[1] > newMaxs[1])
			{
				item->pos[1] = flrand(0.0f, difference[1]) + start[1];
				resetFlake = true;
			}
			if (item->pos[2] < newMins[2] || item->pos[2] > newMaxs[2])
			{
				item->pos[2] = flrand(0.0f, difference[2]) + start[2];
				resetFlake = true;
			}

			if (resetFlake)
			{
				item->velocity[0] = 0.0f;
				item->velocity[1] = 0.0f;
				item->velocity[2] = flrand(mMaxVelocity[2], mMinVelocity[2]);
			}
			item++;
		}

		VectorCopy(newMins, mMins);
		VectorCopy(newMaxs, mMaxs);
	}

	if (!(mOverallContents & CONTENTS_OUTSIDE))
	{
		mIsSnowing = false;
		return;
	}

	mIsSnowing = true;

	mUpdateCount = (mUpdateCount + 1) % mUpdateMax;

	x = y = z = 0;
	item = mSnowList;
	for(i=mMaxSnowflakes;i;i--)
	{
		resetFlake = false;

//		if ((i & mUpdateCount) == 0)   wrong check
		{
			if (item->velocity[0] < mMinVelocity[0])
			{
				item->velocity[0] += mVelocityStabilize * elapseTime;
			}
			else if (item->velocity[0] > mMaxVelocity[0])
			{
				item->velocity[0] -= mVelocityStabilize * elapseTime;
			}
			else
			{
				item->velocity[0] += flrand(-1.4f, 1.4f);
			}
			if (item->velocity[1] < mMinVelocity[1])
			{
				item->velocity[1] += mVelocityStabilize * elapseTime;
			}
			else if (item->velocity[1] > mMaxVelocity[1])
			{
				item->velocity[1] -= mVelocityStabilize * elapseTime;
			}
			else
			{
				item->velocity[1] += flrand(-1.4f, 1.4f);
			}
			if (item->velocity[2] > mMinVelocity[2])
			{
				item->velocity[2] -= mVelocityStabilize*2;
			}
		}
//		VectorMA(item->pos, elapseTime, item->velocity);
		VectorMA(item->pos, elapseTime, item->velocity, item->pos);

		if (item->pos[2] < newMins[2])
		{
			resetFlake = true;
		}
		else
		{
//			if ((i & mUpdateCount) == 0)
			{
				x = (item->pos[0] - mContentsStart[0]) / mContentsSize[0];
				y = (item->pos[1] - mContentsStart[1]) / mContentsSize[1];
				z = (item->pos[2] - mContentsStart[2]) / mContentsSize[2];
				if (x < 0 || x >= CONTENTS_X_SIZE ||
					y < 0 || y >= CONTENTS_Y_SIZE ||
					z < 0 || z >= CONTENTS_Z_SIZE)
				{
					resetFlake = true;
				}
			}
		}

		if (resetFlake)
		{
			item->pos[2] = newMaxs[2] - (newMins[2] - item->pos[2]);
			if (item->pos[2] < newMins[2] || item->pos[2] > newMaxs[2])
			{	// way out of range
				item->pos[2] = flrand(newMins[2], newMaxs[2]);
			}

			item->pos[0] = flrand(newMins[0], newMaxs[0]);
			item->pos[1] = flrand(newMins[1], newMaxs[1]);

			item->velocity[0] = 0.0f;
			item->velocity[1] = 0.0f;
			item->velocity[2] = flrand(mMaxVelocity[2], mMinVelocity[2]);
			item->flags &= ~PARTICLE_FLAG_RENDER;
		}
		else if (mContents[z][y][x] & CONTENTS_OUTSIDE)
		{
			item->flags |= PARTICLE_FLAG_RENDER;
		}
		else
		{
			item->flags &= ~PARTICLE_FLAG_RENDER;
		}

		item++;
	}
}

void CSnowSystem::Render(void)
{
	int			i;
	SParticle	*item;
	vec3_t		origin;

	if (!(mOverallContents & CONTENTS_OUTSIDE))
	{
		return;
	}

	CWorldEffectsSystem::Render();

	VectorAdd(backEnd.viewParms.ori.origin, mMinSpread, origin);

	R_SetStateBits(GLS_ALPHA);
	R_BindImage( tr.whiteImage );

	// Vulkan: render snowflakes as small quads (point primitives not available)
	// Each snowflake becomes a tiny 2-pixel billboard quad
	item = mSnowList;
	float pixelSize = 2.0f;
	byte snowColor[4] = { (byte)(0.8f*255), (byte)(0.8f*255), (byte)(0.8f*255), (byte)(mAlpha*255) };

	for(i=mMaxSnowflakes;i;i--)
	{
		if (item->flags & PARTICLE_FLAG_RENDER)
		{
			// Draw a tiny quad centered at the snowflake position
			vec4_t positions[4];
			glIndex_t indexes[6] = { 0, 1, 2, 0, 2, 3 };
			byte colors[4][4];

			for (int v = 0; v < 4; v++) {
				Com_Memcpy(colors[v], snowColor, 4);
			}

			VectorCopy(item->pos, positions[0]); positions[0][3] = 1.0f;
			VectorCopy(item->pos, positions[1]); positions[1][0] += pixelSize; positions[1][3] = 1.0f;
			VectorCopy(item->pos, positions[2]); positions[2][0] += pixelSize; positions[2][2] += pixelSize; positions[2][3] = 1.0f;
			VectorCopy(item->pos, positions[3]); positions[3][2] += pixelSize; positions[3][3] = 1.0f;

			VK_BindPipeline( renderState.stateBits, renderState.faceCulling, qfalse, qfalse );
			VK_DrawIndexed( 4, (float*)positions, NULL, NULL, (byte*)colors, 6, indexes );
		}
		item++;
	}
}

CSnowSystem	*snowSystem = 0;



















CRainSystem::CRainSystem(int maxRain) :
	mMaxRain(maxRain),
	// mNextWindGust(0),
	mRainHeight(5),
	mAlpha(0.1f),
	mWindAngle(1.0f),

	mFadeAlpha(0.0f),
	mIsRaining(false)

{
	mSpread[0] = M_PI*2;		// angle spread
	mSpread[1] = 20.0f;			// radius spread
	mSpread[2] = 20.0f;			// z spread

	mMinVelocity[0] = 0.1f;
	mMaxVelocity[0] = -0.1f;
	mMinVelocity[1] = 0.1f;
	mMaxVelocity[1] = -0.1f;
	mMinVelocity[2] = -60.0f;
	mMaxVelocity[2] = -50.0f;

	mWindDuration = 15;
	mWindLow = 50;
	mWindMin = 0.01f;
	mWindMax = 0.05f;

	mWindChange = 0;
	mWindDirection[0] = mWindDirection[1] = mWindDirection[2] = 0.0f;

	mRainList = new SParticle[mMaxRain];

	mImage = R_FindImageFile("gfx/world/rain", qfalse, qfalse, qfalse, qfalse);
	// Vulkan: texture filtering is configured via sampler at image creation time

	Init();
}

CRainSystem::~CRainSystem(void)
{
	delete [] mRainList;
}

void CRainSystem::Init(void)
{
	int			i;
	SParticle	*item;

	item = mRainList;
	for(i=mMaxRain;i;i--)
	{
		item->pos[0] = flrand(0.0f, mSpread[0]);
		item->pos[1] = flrand(0.0f, mSpread[1]);
		item->pos[2] = flrand(-mSpread[2], mSpread[2]);
		item->pos[2] = flrand(-mSpread[2], 40.0f);
		item->velocity[0] = flrand(mMinVelocity[0], mMaxVelocity[0]);
		item->velocity[1] = flrand(mMinVelocity[1], mMaxVelocity[1]);
		item->velocity[2] = flrand(mMinVelocity[2], mMaxVelocity[2]);
		item++;
	}
}

int CRainSystem::GetIntVariable(int which)
{
	switch(which)
	{
		case CWorldEffect::WORLDEFFECT_PARTICLE_COUNT:
			return mMaxRain;
	}

	return CWorldEffectsSystem::GetIntVariable(which);
}

SParticle *CRainSystem::GetParticleVariable(int which)
{
	switch(which)
	{
		case CWorldEffect::WORLDEFFECT_PARTICLES:
			return mRainList;
	}

	return CWorldEffectsSystem::GetParticleVariable(which);
}

float CRainSystem::GetFloatVariable(int which)
{
	switch(which)
	{
		case CRainSystem::RAINSYSTEM_WIND_SPEED:
			return mWindAngle * 75.0f;		// pat scaled
	}

	return 0.0f;
}

float *CRainSystem::GetVecVariable(int which)
{
	switch(which)
	{
		case CRainSystem::RAINSYSTEM_WIND_DIRECTION:
			return mWindDirection;
	}
	return 0;
}

bool CRainSystem::Command(const char *command)
{
	char	*token;

	if (CWorldEffectsSystem::Command(command))
	{
		return true;
	}

	token = COM_ParseExt((const char **)&command, qfalse);

	if (Q_stricmp(token, "fog") == 0)
	{	// rain fog
		AddWorldEffect(new CMistyFog2);
		mWindChange = 0;
		return true;
	}
	else if (Q_stricmp(token, "fall") == 0)
	{	// rain fall ( minVelocity maxVelocity )			default: ( -60 -50 )
		float	data[2];

		if (ParseVector((const char **)&command, 2, data))
		{
			mMinVelocity[2] = data[0];
			mMaxVelocity[2] = data[1];
		}
		return true;
	}
	else if (Q_stricmp(token, "spread") == 0)
	{	// rain spread ( radius height )					default: ( 20 20 )
		ParseVector((const char **)&command, 2, &mSpread[1]);
		return true;
	}
	else if (Q_stricmp(token, "alpha") == 0)
	{	// rain alpha <float>								default: 0.15
		token = COM_ParseExt((const char **)&command, qfalse);
		mAlpha = atof(token);
		return true;
	}
	else if (Q_stricmp(token, "height") == 0)
	{	// rain height <float>								default: 1.5
		token = COM_ParseExt((const char **)&command, qfalse);
		mRainHeight = atof(token);
		return true;
	}
	else if (Q_stricmp(token, "angle") == 0)
	{	// rain angle <float>								default: 1.0
		token = COM_ParseExt((const char **)&command, qfalse);
		mWindAngle = atof(token);
		return true;
	}

	return false;
}

void CRainSystem::Update(float elapseTime)
{
	int			i;
	SParticle	*item;
	vec3_t		windDifference;

	mWindChange--;

	if (mWindChange < 0)
	{
		mNewWindDirection[0] = flrand(-1.0f, 1.0f);
		mNewWindDirection[1] = flrand(-1.0f, 1.0f);
		mNewWindDirection[2] = 0.0f;
		VectorNormalize(mNewWindDirection);
		VectorScale(mNewWindDirection, 0.025f, mWindSpeed);

		mWindChange = irand(200, 450);
//		mWindChange = 10;

		ParmUpdate(CRainSystem::RAINSYSTEM_WIND_DIRECTION);
	}

	VectorSubtract(mNewWindDirection, mWindDirection, windDifference);
//	VectorMA(mWindDirection, elapseTime, windDifference);
	VectorMA(mWindDirection, elapseTime, windDifference, mWindDirection);

	CWorldEffectsSystem::Update(elapseTime);

	if (originContents & CONTENTS_OUTSIDE && !(originContents & CONTENTS_WATER))
	{
		mIsRaining = true;
		if (mFadeAlpha < 1.0f)
		{
			mFadeAlpha += elapseTime * 0.5f;
		}
		if (mFadeAlpha > 1.0f)
		{
			mFadeAlpha = 1.0f;
		}
	}
	else
	{
		mIsRaining = false;
		if (mFadeAlpha > 0.0f)
		{
			mFadeAlpha -= elapseTime * 0.5f;
		}

		if (mFadeAlpha <= 0.0f)
		{
			return;
		}
	}

	item = mRainList;
	for(i=mMaxRain;i;i--)
	{
//		VectorMA(item->pos, elapseTime, item->velocity);
		VectorMA(item->pos, elapseTime, item->velocity, item->pos);

		if (item->pos[2] < -mSpread[2])
		{
			item->pos[0] = flrand(0.0f, mSpread[0]);
			item->pos[1] = flrand(0.0f, mSpread[1]);
			item->pos[2] = mSpread[2];
			item->pos[2] = 40.0f;

			item->velocity[0] = flrand(mMinVelocity[0], mMaxVelocity[0]);
			item->velocity[1] = flrand(mMinVelocity[1], mMaxVelocity[1]);
			item->velocity[2] = flrand(mMinVelocity[2], mMaxVelocity[2]);
		}

		item++;
	}
}

void CRainSystem::Render(void)
{
	int			i;
	SParticle	*item;
	vec4_t		forward, down, left;
	vec3_t		pos;
	float		radius;

	CWorldEffectsSystem::Render();

	if (mFadeAlpha <= 0.0)
	{
		return;
	}

	VectorScale(backEnd.viewParms.ori.axis[0], 1, forward);
	VectorScale(backEnd.viewParms.ori.axis[1], 0.2f, left);
	down[0] = 0 - mWindDirection[0] * mRainHeight * mWindAngle;
	down[1] = 0 - mWindDirection[1] * mRainHeight * mWindAngle;
	down[2] = -mRainHeight;

	R_BindImage(mImage);
	R_SetStateBits(GLS_ALPHA);
	R_SetCullMode( CT_TWO_SIDED );

	// Vulkan: batch rain triangles and draw
	// Each rain drop is 1 triangle (3 verts)
	// We'll batch them and draw in chunks
	const int MAX_BATCH = SHADER_MAX_VERTEXES / 3;
	vec4_t positions[MAX_BATCH * 3];
	vec2_t texcoords[MAX_BATCH * 3];
	byte colors[MAX_BATCH * 3][4];
	glIndex_t indexes[MAX_BATCH * 3];
	int batchCount = 0;

	item = mRainList;
	for(i=mMaxRain;i;i--)
	{
		radius = item->pos[1];
		float alpha;
		if (item->pos[2] < 0)
		{
			alpha = mAlpha * (item->pos[1] / -item->pos[2]);
			if (alpha > mAlpha) alpha = mAlpha;
		}
		else
		{
			alpha = mAlpha * mFadeAlpha;
		}

		pos[0] = sinf(item->pos[0]) * radius + (item->pos[2] * mWindDirection[0] * mWindAngle);
		pos[1] = cosf(item->pos[0]) * radius + (item->pos[2] * mWindDirection[1] * mWindAngle);
		pos[2] = item->pos[2];

		// Offset by camera origin
		vec3_t worldPos;
		VectorAdd(pos, backEnd.viewParms.ori.origin, worldPos);

		int base = batchCount * 3;
		byte a = (byte)(alpha * mFadeAlpha * 255.0f);

		VectorCopy(worldPos, positions[base]); positions[base][3] = 1.0f;
		texcoords[base][0] = 1.0f; texcoords[base][1] = 0.0f;
		colors[base][0] = 255; colors[base][1] = 255; colors[base][2] = 255; colors[base][3] = a;

		positions[base+1][0] = worldPos[0] + left[0];
		positions[base+1][1] = worldPos[1] + left[1];
		positions[base+1][2] = worldPos[2] + left[2];
		positions[base+1][3] = 1.0f;
		texcoords[base+1][0] = 0.0f; texcoords[base+1][1] = 0.0f;
		colors[base+1][0] = 255; colors[base+1][1] = 255; colors[base+1][2] = 255; colors[base+1][3] = a;

		positions[base+2][0] = worldPos[0] + down[0] + left[0];
		positions[base+2][1] = worldPos[1] + down[1] + left[1];
		positions[base+2][2] = worldPos[2] + down[2] + left[2];
		positions[base+2][3] = 1.0f;
		texcoords[base+2][0] = 0.0f; texcoords[base+2][1] = 1.0f;
		colors[base+2][0] = 255; colors[base+2][1] = 255; colors[base+2][2] = 255; colors[base+2][3] = a;

		indexes[base] = base;
		indexes[base+1] = base + 1;
		indexes[base+2] = base + 2;

		batchCount++;

		if (batchCount >= MAX_BATCH)
		{
			VK_BindPipeline( renderState.stateBits, renderState.faceCulling, qfalse, qfalse );
			VK_DrawIndexed( batchCount * 3, (float*)positions, (float*)texcoords, NULL, (byte*)colors, batchCount * 3, indexes );
			batchCount = 0;
		}

		item++;
	}

	// flush remaining
	if (batchCount > 0)
	{
		VK_BindPipeline( renderState.stateBits, renderState.faceCulling, qfalse, qfalse );
		VK_DrawIndexed( batchCount * 3, (float*)positions, (float*)texcoords, NULL, (byte*)colors, batchCount * 3, indexes );
	}

	R_SetCullMode( CT_FRONT_SIDED );
}






CRainSystem	*rainSystem = 0;


void R_InitWorldEffects(void)
{
	if (rainSystem)
	{
		delete rainSystem;
	}

	if (snowSystem)
	{
		delete snowSystem;
	}
}

void R_ShutdownWorldEffects(void)
{
	if (rainSystem)
	{
		delete rainSystem;
		rainSystem = 0;
	}
	if (snowSystem)
	{
		delete snowSystem;
		snowSystem = 0;
	}
}

void SetViewportAndScissor( void ) ;

void RB_RenderWorldEffects(void)
{
	float					elapseTime = backEnd.refdef.frametime / 1000.0;

	if (tr.refdef.rdflags & RDF_NOWORLDMODEL || !tr.world)
	{	//  no world rendering or no world
		return;
	}

	SetViewportAndScissor();
	// Vulkan: model matrix is handled via push constants / MVP

	originContents = ri.CM_PointContents(backEnd.viewParms.ori.origin, 0);

	if (rainSystem)
	{
		rainSystem->Update(elapseTime);
		rainSystem->Render();
	}

	if (snowSystem)
	{
		snowSystem->Update(elapseTime);
		snowSystem->Render();
	}
}

//	console commands for r_we
//
//	SNOW
//		snow init <particles>
//		snow remove
//		snow alpha <float>											default: 0.09
//		snow spread ( minX minY minZ ) ( maxX maxY maxZ )			default: ( -600 -600 -200 ) ( 600 600 250 )
//		snow velocity ( minX minY minZ ) ( maxX maxY maxZ )			default: ( -15 -15 -20 ) ( 15 15 -70 )
//		snow blowing duration <int>									default: 2
//		snow blowing low <int>										default: 3
//		snow blowing velocity ( min max )							default: ( 30 70 )
//		snow blowing size ( minX minY minZ )						default: ( 1000 300 300 )
//		snow wind ( windOriginX windOriginY windOriginZ ) ( windVelocityX windVelocityY windVelocityZ ) ( sizeX sizeY sizeZ )
//		snow fog
//		snow fog density <alpha>									default: 0.3
//
//	RAIN
//		rain init <particles>
//		rain remove
//		rain fog
//		rain fog density <alpha>									default: 0.3
//		rain fall ( minVelocity maxVelocity )						default: ( -60 -50 )
//		rain spread ( radius height )								default: ( 20 20 )
//		rain alpha <float>											default: 0.1
//		rain height <float>											default: 5
//		rain angle <float>											default: 1.0
//
//	DEBUG
//		debug wind

void R_WorldEffectCommand(const char *command)
{
	char		*token;
	const char	*origCommand;

	origCommand = command;
	token = COM_ParseExt((const char **)&command, qfalse);

	if (Q_stricmp(token, "snow") == 0)
	{
		origCommand = command;

		token = COM_ParseExt((const char **)&command, qfalse);
		if (Q_stricmp(token, "init") == 0)
		{	//	snow init <particles>
			token = COM_ParseExt((const char **)&command, qfalse);
			if (snowSystem)
			{
				delete snowSystem;
			}
			snowSystem = new CSnowSystem(atoi(token));
		}
		else if (Q_stricmp(token, "remove") == 0)
		{	//	snow remove
			if (snowSystem)
			{
				delete snowSystem;
				snowSystem = 0;
			}
		}
		else if (snowSystem)
		{
			snowSystem->Command(origCommand);
		}
	}
	else if (Q_stricmp(token, "rain") == 0)
	{
		origCommand = command;

		token = COM_ParseExt((const char **)&command, qfalse);
		if (Q_stricmp(token, "init") == 0)
		{	//	rain init <particles>
			token = COM_ParseExt((const char **)&command, qfalse);
			if (rainSystem)
			{
				delete rainSystem;
			}
			rainSystem = new CRainSystem(atoi(token));
		}
		else if (Q_stricmp(token, "remove") == 0)
		{	//	rain remove
			if (rainSystem)
			{
				delete rainSystem;
				rainSystem = 0;
			}
		}
		else if (rainSystem)
		{
			rainSystem->Command(origCommand);
		}
	}
	else if (Q_stricmp(token, "debug") == 0)
	{
		token = COM_ParseExt((const char **)&command, qfalse);
		if (Q_stricmp(token, "wind") == 0)
		{
			debugShowWind = !debugShowWind;
		}
		else if (Q_stricmp(token, "blah") == 0)
		{
			R_WorldEffectCommand("snow init 1000");
			R_WorldEffectCommand("snow alpha 1");
			R_WorldEffectCommand("snow fog");
		}
	}
	else if (Q_stricmp(token, "exec") == 0)
	{
		ri.Cmd_ExecuteText(EXEC_NOW, command);
	}
}

void R_WorldEffect_f(void)
{
	char		temp[2048];

	ri.Cmd_ArgsBuffer(temp, sizeof(temp));
	R_WorldEffectCommand(temp);
}

bool R_GetWindVector(vec3_t windVector)
{
	if (rainSystem)
	{
		VectorCopy(rainSystem->GetVecVariable(CRainSystem::RAINSYSTEM_WIND_DIRECTION), windVector);
		return true;
	}

	if (snowSystem)
	{
		VectorCopy(snowSystem->GetVecVariable(CRainSystem::RAINSYSTEM_WIND_DIRECTION), windVector);
		return true;
	}


	return false;
}

bool R_GetWindSpeed(float &windSpeed)
{
	if (rainSystem)
	{
		windSpeed = rainSystem->GetFloatVariable(CRainSystem::RAINSYSTEM_WIND_SPEED);
		return true;
	}

	return false;
}

bool R_IsRaining()
{
	if (rainSystem)
	{
		return rainSystem->IsRaining();
	}
	return false;
}

bool R_IsSnowing()
{
	if (snowSystem)
	{
		return snowSystem->IsSnowing();
	}
	return false;
}
